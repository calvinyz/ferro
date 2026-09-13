// Two-rate controller. A fast deterministic control loop and a slow
// variable-latency policy thread, decoupled by latest-value channels so policy
// jitter cannot reach the actuator.
//
//   control loop (fast, RT)  --obs_ch-->  policy thread (slow)
//   control loop (fast, RT)  <--cmd_ch--  policy thread (slow)

#include "latency.h"
#include "mujoco_wrapper.h"
#include "realtime.h"
#include "target_channel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mujoco/mujoco.h>
#include <onnxruntime_cxx_api.h>
#include <thread>
#include <vector>

using std::chrono::duration_cast;
using std::chrono::nanoseconds;
using std::chrono::steady_clock;

namespace {

uint64_t now_ns() {
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

int main(int argc, char** argv) {
    const int num_ticks = (argc > 1) ? std::atoi(argv[1]) : 5000;
    const int control_us = (argc > 2) ? std::atoi(argv[2]) : 1000;   // 1 kHz
    const int policy_us = (argc > 3) ? std::atoi(argv[3]) : 40000;   // 25 Hz, the trained dt
    const int stall_ms = (argc > 4) ? std::atoi(argv[4]) : 0;        // injected policy stall
    const int die_after = (argc > 5) ? std::atoi(argv[5]) : 0;       // policy exits after N cycles

    // Command older than this falls back rather than driving the actuator.
    const uint64_t max_age_ns = static_cast<uint64_t>(policy_us) * 1000 * 3;

    Model model(INVERTED_PENDULUM_PATH);
    Data data(model);

    // Sim time tracks wall time at the control rate.
    model.get()->opt.timestep = control_us * 1e-6;

    TargetChannel obs_ch;  // control loop publishes state
    TargetChannel cmd_ch;  // policy publishes action

    std::atomic<bool> running{true};

    LatencyRecorder control_tick(num_ticks);
    LatencyRecorder control_period(num_ticks);
    LatencyRecorder inference(num_ticks / (policy_us / control_us) + 16);

    std::atomic<uint64_t> stale_ticks{0};
    std::atomic<uint64_t> fallback_ticks{0};
    std::atomic<uint64_t> max_age_seen{0};

    std::thread policy_thread([&] {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "ferro_policy");
        Ort::Session session(env, POLICY_PATH, Ort::SessionOptions{});
        Ort::AllocatorWithDefaultOptions alloc;
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        const char* input_names[] = {"observation"};
        const char* output_names[] = {"action"};

        uint64_t cycle = 0;
        auto next = steady_clock::now();

        while (running.load(std::memory_order_relaxed)) {
            if (die_after > 0 && cycle >= static_cast<uint64_t>(die_after)) {
                std::printf("policy thread exiting after %llu cycles\n",
                            (unsigned long long)cycle);
                return;
            }
            next += std::chrono::microseconds(policy_us);

            Target state{};
            if (obs_ch.try_read(state) && state.policy_tick > 0) {
                std::vector<float> obs(state.value, state.value + 4);
                std::vector<int64_t> shape = {1, 4};
                auto input = Ort::Value::CreateTensor<float>(
                    memory_info, obs.data(), obs.size(), shape.data(), shape.size());

                auto t0 = steady_clock::now();
                auto out = session.Run(
                    Ort::RunOptions{nullptr}, input_names, &input, 1, output_names, 1);
                inference.record(steady_clock::now() - t0);

                Target cmd{};
                cmd.policy_tick = ++cycle;
                cmd.published_ns = now_ns();
                cmd.value[0] = out[0].GetTensorData<float>()[0];
                cmd_ch.publish(cmd);
            }

            // Deliberate stall, to show control timing is unaffected by it.
            if (stall_ms > 0 && cycle % 10 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(stall_ms));
            }

            std::this_thread::sleep_until(next);
        }
    });

    RealtimeParams rt = {
        .period_ns = static_cast<int64_t>(control_us) * 1000,
        .computation_ns = std::min<int64_t>(200'000, static_cast<int64_t>(control_us) * 250),
        .constraint_ns = std::min<int64_t>(500'000, static_cast<int64_t>(control_us) * 500),
        .fifo_priority = 80,
    };
    std::printf("control loop: %d us (%.0f Hz), policy: %d us (%.0f Hz)\n",
                control_us, 1e6 / control_us, policy_us, 1e6 / policy_us);
    std::printf("real-time scheduling: %s\n",
                request_realtime(rt) ? realtime_backend() : "refused, default priority");

    float held_action = 0.0f;
    uint64_t last_cmd_tick = 0;

    auto next_deadline = steady_clock::now();
    auto last_wake = next_deadline;
    int missed = 0;

    for (int i = 0; i < num_ticks; i++) {
        next_deadline += std::chrono::microseconds(control_us);
        auto tick_start = steady_clock::now();

        Target cmd{};
        if (cmd_ch.try_read(cmd) && cmd.policy_tick > 0) {
            uint64_t age = now_ns() - cmd.published_ns;
            uint64_t prev = max_age_seen.load(std::memory_order_relaxed);
            if (age > prev) max_age_seen.store(age, std::memory_order_relaxed);

            if (age > max_age_ns) {
                // Stale command: decay toward zero rather than hold a stale one.
                held_action *= 0.95f;
                fallback_ticks.fetch_add(1, std::memory_order_relaxed);
            } else {
                held_action = cmd.value[0];
                if (cmd.policy_tick == last_cmd_tick) {
                    stale_ticks.fetch_add(1, std::memory_order_relaxed);
                }
                last_cmd_tick = cmd.policy_tick;
            }
        }

        data.get()->ctrl[0] = held_action;
        mj_step(model.get(), data.get());

        Target state{};
        state.policy_tick = static_cast<uint64_t>(i) + 1;
        state.published_ns = now_ns();
        state.value[0] = (float)data.get()->qpos[0];
        state.value[1] = (float)data.get()->qpos[1];
        state.value[2] = (float)data.get()->qvel[0];
        state.value[3] = (float)data.get()->qvel[1];
        obs_ch.publish(state);

        control_tick.record(steady_clock::now() - tick_start);

        auto before_sleep = steady_clock::now();
        while (next_deadline < before_sleep) {
            next_deadline += std::chrono::microseconds(control_us);
            missed++;
        }
        std::this_thread::sleep_until(next_deadline);

        auto wake = steady_clock::now();
        control_period.record(wake - last_wake);
        last_wake = wake;
    }

    running.store(false, std::memory_order_relaxed);
    policy_thread.join();

    std::printf("\nfinal pole angle: %.5f\n", data.get()->qpos[1]);
    print_latency("control_tick", control_tick.stats());
    print_latency("control_period", control_period.stats());
    print_latency("inference", inference.stats());

    std::printf("policy cycles     : %llu\n", (unsigned long long)cmd_ch.publications());
    std::printf("reused command    : %llu / %d ticks\n",
                (unsigned long long)stale_ticks.load(), num_ticks);
    std::printf("fallback engaged  : %llu ticks\n", (unsigned long long)fallback_ticks.load());
    std::printf("worst command age : %.1f us\n", max_age_seen.load() / 1000.0);
    std::printf("missed deadlines  : %d / %d\n", missed, num_ticks);

    return 0;
}
