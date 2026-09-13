#include "mujoco_wrapper.h"
#include "latency.h"
#include "realtime.h"
#include "tick_record.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <mujoco/mujoco.h>
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <thread>

using std::chrono::duration_cast;
using std::chrono::nanoseconds;
using std::chrono::steady_clock;

int main(int argc, char** argv) {
    const char* model_path = INVERTED_PENDULUM_PATH;
    const char* policy_path = POLICY_PATH;

    // > kRingCapacity exercises the drop path, since nothing drains the ring
    const int num_steps = (argc > 1) ? std::atoi(argv[1]) : 500;

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "ferro");
    Ort::Session session(env, policy_path, Ort::SessionOptions{});

    // static asserts can't catch a policy width that disagrees with kObsDim
    auto input_dims = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    validate_obs_dim(static_cast<size_t>(input_dims.back()));

    {
        Model model(model_path);
        Data data(model);

        LatencyRecorder inference(num_steps);
        LatencyRecorder tick_work(num_steps);
        LatencyRecorder period(num_steps);
        LatencyRecorder wake_lateness(num_steps);
        LatencyRecorder telemetry(num_steps);

        const auto target_period = std::chrono::microseconds(20000);  // 50 Hz
        auto next_deadline = steady_clock::now();
        auto last_wake = next_deadline;

        int missed_deadlines = 0;
        int dropped_count = 0;

        // budgets sized off the measured tick_work p99, roughly 0.6ms
        RealtimeParams rt = {
            .period_ns = 20'000'000,
            .computation_ns = 1'000'000,
            .constraint_ns = 2'000'000,
            .fifo_priority = 80,
        };

        if (request_realtime(rt)) {
            std::cout << "real-time scheduling: " << realtime_backend() << "\n";
        } else {
            std::cout << "real-time scheduling refused, running at default priority\n";
        }

        static TickRing ring{};  // ~65KB, keep it off the stack
        ring.control.head.store(0, std::memory_order_relaxed);
        ring.control.tail.store(0, std::memory_order_relaxed);
        ring.control.capacity = kRingCapacity;
        ring.control.record_size = sizeof(TickRecord);
        ring.control.obs_dim = kObsDim;

        std::cout << "Stepping simulation " << num_steps << " times...\n";

        for (int i = 0; i < num_steps; i++) {
            next_deadline += target_period;
            auto tick_start = steady_clock::now();

            // observation is [qpos, qvel], the order gymnasium trained against
            std::vector<float> obs(4);
            obs[0] = (float)data.get()->qpos[0];  // cart position
            obs[1] = (float)data.get()->qpos[1];  // pole angle
            obs[2] = (float)data.get()->qvel[0];  // cart velocity
            obs[3] = (float)data.get()->qvel[1];  // pole angular velocity

            std::vector<int64_t> input_shape = {1, 4};
            auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            auto input_tensor = Ort::Value::CreateTensor<float>(
                memory_info,
                obs.data(),
                4,
                input_shape.data(),
                2
            );

            const char* input_names[] = {"observation"};
            const char* output_names[] = {"action"};

            auto inf_start = steady_clock::now();
            auto output_tensors = session.Run(
                Ort::RunOptions{nullptr},
                input_names, &input_tensor, 1,
                output_names, 1
            );
            auto inf_elapsed = steady_clock::now() - inf_start;
            inference.record(inf_elapsed);

            const float* action_data = output_tensors[0].GetTensorData<float>();
            data.get()->ctrl[0] = action_data[0];

            mj_step(model.get(), data.get());

            // excludes the push below, which can't report its own cost
            auto tick_elapsed = steady_clock::now() - tick_start;
            tick_work.record(tick_elapsed);

            // marshalling counts as telemetry cost, so time it too
            auto telem_start = steady_clock::now();

            TickRecord rec{};
            rec.tick = i;
            rec.timestamp_ns =
                duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
            std::copy(obs.begin(), obs.end(), rec.obs);
            rec.action = action_data[0];
            rec.inference_ns = static_cast<uint32_t>(duration_cast<nanoseconds>(inf_elapsed).count());
            rec.tick_work_ns = static_cast<uint32_t>(duration_cast<nanoseconds>(tick_elapsed).count());

            bool pushed = tick_ring_push(&ring, rec);
            telemetry.record(steady_clock::now() - telem_start);

            if (!pushed) dropped_count++;

            // after the measurement so console I/O stays out of it
            if (i % 10 == 0) {
                std::cout << "Step " << i << ": pole_angle=" << data.get()->qpos[1]
                            << ", action=" << action_data[0] << "\n";
            }

            // skip whole periods instead of sprinting through the backlog
            auto before_sleep = steady_clock::now();
            while (next_deadline < before_sleep) {
                next_deadline += target_period;
                missed_deadlines++;
            }

            std::this_thread::sleep_until(next_deadline);

            auto wake = steady_clock::now();
            wake_lateness.record(wake - next_deadline);
            period.record(wake - last_wake);
            last_wake = wake;
        }

        std::cout << "\n";
        print_latency("inference", inference.stats());
        print_latency("tick_work", tick_work.stats());
        print_latency("telemetry", telemetry.stats());
        print_latency("period", period.stats());
        print_latency("wake_lateness", wake_lateness.stats());

        std::cout << "missed deadlines: " << missed_deadlines << " / " << num_steps << "\n";
        std::cout << "dropped records: " << dropped_count << " / " << num_steps << "\n";
    }

    return 0;
}
