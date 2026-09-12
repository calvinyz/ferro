#include "mujoco_wrapper.h"
#include "latency.h"
#include "realtime.h"

#include <iostream>
#include <mujoco/mujoco.h>
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <thread>

int main() {
    const char* model_path = INVERTED_PENDULUM_PATH;
    const char* policy_path = POLICY_PATH;

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "ferro");
    Ort::Session session(env, policy_path, Ort::SessionOptions{});
    {
        Model model(model_path);
        Data data(model);

        const int num_steps = 500;
        LatencyRecorder inference(num_steps);
        LatencyRecorder tick_work(num_steps);
        LatencyRecorder period(num_steps);
        LatencyRecorder wake_lateness(num_steps);

        const auto target_period = std::chrono::microseconds(20000);  // 50 Hz
        auto next_deadline = std::chrono::steady_clock::now();
        auto last_wake = next_deadline;
        int missed_deadlines = 0;

        // computation/constraint are budgets for the work inside one period,
        // sized off the measured tick_work p99 of roughly 0.6ms.
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

        std::cout << "Stepping simulation " << num_steps << " times...\n";
        for (int i = 0; i < num_steps; i++) {
            next_deadline += target_period;
            auto tick_start = std::chrono::steady_clock::now();

            // build observation: [qpos[0], qpos[1], qvel[0], qvel[1]]
            std::vector<float> obs(4);
            obs[0] = (float)data.get()->qpos[0];  // cart position
            obs[1] = (float)data.get()->qpos[1];  // pole angle
            obs[2] = (float)data.get()->qvel[0];  // cart velocity
            obs[3] = (float)data.get()->qvel[1];  // pole angular velocity

            // create input tensor (batch size 1)
            std::vector<int64_t> input_shape = {1, 4};
            auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            auto input_tensor = Ort::Value::CreateTensor<float>(
                memory_info,
                obs.data(),
                4,
                input_shape.data(),
                2
            );

            // run inference
            const char* input_names[] = {"observation"};
            const char* output_names[] = {"action"};
            
            auto inf_start = std::chrono::steady_clock::now();
            auto output_tensors = session.Run(
                Ort::RunOptions{nullptr},
                input_names, &input_tensor, 1,
                output_names, 1
            );
            inference.record(std::chrono::steady_clock::now() - inf_start);

            // extract action and apply to control
            const float* action_data = output_tensors[0].GetTensorData<float>();
            data.get()->ctrl[0] = action_data[0];

            // step simulation
            mj_step(model.get(), data.get());

            tick_work.record(std::chrono::steady_clock::now() - tick_start);

            // Logged after the measurement so console I/O stays out of it.
            if (i % 10 == 0) {
                std::cout << "Step " << i << ": pole_angle=" << data.get()->qpos[1]
                            << ", action=" << action_data[0] << "\n";
            }

            // Skip whole periods rather than sprinting through the backlog when
            // a tick lands past its deadline.
            auto before_sleep = std::chrono::steady_clock::now();
            while (next_deadline < before_sleep) {
                next_deadline += target_period;
                missed_deadlines++;
            }

            std::this_thread::sleep_until(next_deadline);

            auto wake = std::chrono::steady_clock::now();
            wake_lateness.record(wake - next_deadline);
            period.record(wake - last_wake);
            last_wake = wake;
        }

        std::cout << "\n";
        print_latency("inference", inference.stats());
        print_latency("tick_work", tick_work.stats());
        print_latency("period", period.stats());
        print_latency("wake_lateness", wake_lateness.stats());
        std::cout << "missed deadlines: " << missed_deadlines << " / " << num_steps << "\n";
    }

    return 0;
}
