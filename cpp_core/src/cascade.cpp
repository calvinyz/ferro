// Two-rate controller. A fast deterministic control loop and a slow
// variable-latency policy thread, decoupled by latest-value channels so policy
// jitter cannot reach the actuator.
//
//   control loop (fast, RT)  --obs_ch-->  policy thread (slow)
//   control loop (fast, RT)  <--cmd_ch--  policy thread (slow)

#include "latency.h"
#include "mujoco_wrapper.h"
#include "realtime.h"
#include "sidecar.h"
#include "target_channel.h"
#include "tasks.h"
#include "tick_record.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mujoco/mujoco.h>
#include <onnxruntime_cxx_api.h>
#include <random>
#include <string>
#include <thread>
#include <vector>

using std::chrono::duration_cast;
using std::chrono::nanoseconds;
using std::chrono::steady_clock;

namespace {

uint64_t now_ns() {
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

struct Args {
    std::string task = "cartpole";
    int ticks = 5000;
    int control_us = 1000;  // 1 kHz
    int policy_us = 0;      // 0 means the task's trained rate
    int stall_ms = 0;       // injected policy stall every 10 cycles
    int die_after = 0;      // policy thread exits after N cycles
    uint32_t seed = 0;
    const char* trajectory = nullptr;
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string k = argv[i];
        const char* v = argv[i + 1];
        if (k == "--task") a.task = v;
        else if (k == "--ticks") a.ticks = std::atoi(v);
        else if (k == "--control-us") a.control_us = std::atoi(v);
        else if (k == "--policy-us") a.policy_us = std::atoi(v);
        else if (k == "--stall-ms") a.stall_ms = std::atoi(v);
        else if (k == "--die-after") a.die_after = std::atoi(v);
        else if (k == "--seed") a.seed = static_cast<uint32_t>(std::atoi(v));
        else if (k == "--trajectory") a.trajectory = v;
        else throw std::runtime_error("unknown flag " + k);
    }
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    Args args = parse(argc, argv);
    const TaskSpec& task = tasks::find(args.task);
    const int policy_us = args.policy_us > 0 ? args.policy_us : task.policy_us;

    // Command older than this falls back rather than driving the actuator.
    const uint64_t max_age_ns = static_cast<uint64_t>(policy_us) * 1000 * 3;

    Model model(task.model_path);
    Data data(model);
    const mjModel* m = model.get();
    mjData* d = data.get();

    validate_record_dims(m->nq, m->nu);
    if (task.obs_dim > (int)kMaxTargetDim || task.act_dim > (int)kMaxTargetDim) {
        throw std::runtime_error("task dims exceed channel capacity");
    }

    // Sim time tracks wall time at the control rate.
    model.get()->opt.timestep = args.control_us * 1e-6;

    std::mt19937 rng(args.seed);
    task.reset(m, d, rng);

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "ferro_policy");
    Ort::Session session(env, task.policy_path, Ort::SessionOptions{});

    auto in_shape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (in_shape.back() != task.obs_dim) {
        throw std::runtime_error("policy expects " + std::to_string(in_shape.back()) +
                                 " inputs, task provides " + std::to_string(task.obs_dim));
    }

    TargetChannel obs_ch;
    TargetChannel cmd_ch;

    // Seed the observation channel so the policy's first cycle has a state.
    Target first{};
    first.policy_tick = 1;
    first.published_ns = now_ns();
    task.observe(m, d, first.value);
    obs_ch.publish(first);

    static TickRing ring{};
    ring.control.head.store(0, std::memory_order_relaxed);
    ring.control.tail.store(0, std::memory_order_relaxed);
    ring.control.capacity = kRingCapacity;
    ring.control.record_size = sizeof(TickRecord);
    ring.control.qpos_capacity = kMaxQposDim;

    SidecarConsumer sidecar(&ring, args.trajectory);
    if (args.trajectory && !sidecar.recording()) {
        throw std::runtime_error(std::string("cannot open ") + args.trajectory);
    }

    std::atomic<bool> running{true};

    LatencyRecorder control_tick(args.ticks);
    LatencyRecorder control_period(args.ticks);
    LatencyRecorder inference(args.ticks / std::max(1, policy_us / args.control_us) + 16);

    uint64_t reused_ticks = 0;
    uint64_t fallback_ticks = 0;
    uint64_t max_age_seen = 0;
    uint64_t ring_drops = 0;

    std::thread policy_thread([&] {
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const char* input_names[] = {"observation"};
        const char* output_names[] = {"action"};

        std::vector<float> obs(task.obs_dim);
        std::vector<int64_t> shape = {1, task.obs_dim};
        uint64_t cycle = 0;
        auto next = steady_clock::now();

        while (running.load(std::memory_order_relaxed)) {
            if (args.die_after > 0 && cycle >= static_cast<uint64_t>(args.die_after)) {
                std::printf("policy thread exiting after %llu cycles\n",
                            (unsigned long long)cycle);
                return;
            }
            next += std::chrono::microseconds(policy_us);

            Target state{};
            if (obs_ch.try_read(state) && state.policy_tick > 0) {
                std::copy(state.value, state.value + task.obs_dim, obs.begin());
                auto input = Ort::Value::CreateTensor<float>(
                    memory_info, obs.data(), obs.size(), shape.data(), shape.size());

                auto t0 = steady_clock::now();
                auto out = session.Run(
                    Ort::RunOptions{nullptr}, input_names, &input, 1, output_names, 1);
                auto elapsed = steady_clock::now() - t0;
                inference.record(elapsed);

                Target cmd{};
                cmd.policy_tick = ++cycle;
                cmd.published_ns = now_ns();
                cmd.compute_ns = duration_cast<nanoseconds>(elapsed).count();
                const float* a = out[0].GetTensorData<float>();
                std::copy(a, a + task.act_dim, cmd.value);
                cmd_ch.publish(cmd);
            }

            // Deliberate stall, to show control timing is unaffected by it.
            if (args.stall_ms > 0 && cycle % 10 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(args.stall_ms));
            }

            std::this_thread::sleep_until(next);
        }
    });

    RealtimeParams rt = {
        .period_ns = static_cast<int64_t>(args.control_us) * 1000,
        .computation_ns = std::min<int64_t>(200'000, static_cast<int64_t>(args.control_us) * 250),
        .constraint_ns = std::min<int64_t>(500'000, static_cast<int64_t>(args.control_us) * 500),
        .fifo_priority = 80,
    };
    std::printf("task: %s, control %d us (%.0f Hz), policy %d us (%.0f Hz)\n", task.name,
                args.control_us, 1e6 / args.control_us, policy_us, 1e6 / policy_us);
    std::printf("real-time scheduling: %s\n",
                request_realtime(rt) ? realtime_backend() : "refused, default priority");

    float held[kMaxTargetDim] = {};
    uint64_t last_cmd_tick = 0;
    uint64_t last_compute_ns = 0;

    auto next_deadline = steady_clock::now();
    auto last_wake = next_deadline;
    int missed = 0;

    for (int i = 0; i < args.ticks; i++) {
        next_deadline += std::chrono::microseconds(args.control_us);
        auto tick_start = steady_clock::now();

        Target cmd{};
        if (cmd_ch.try_read(cmd) && cmd.policy_tick > 0) {
            uint64_t age = now_ns() - cmd.published_ns;
            max_age_seen = std::max(max_age_seen, age);

            if (age > max_age_ns) {
                // Stale command: decay toward zero rather than hold a stale one.
                for (int j = 0; j < task.act_dim; j++) held[j] *= 0.95f;
                fallback_ticks++;
            } else {
                std::copy(cmd.value, cmd.value + task.act_dim, held);
                last_compute_ns = cmd.compute_ns;
                if (cmd.policy_tick == last_cmd_tick) reused_ticks++;
                last_cmd_tick = cmd.policy_tick;
            }
        }

        for (int j = 0; j < m->nu; j++) d->ctrl[j] = held[j];
        mj_step(m, d);

        Target state{};
        state.policy_tick = static_cast<uint64_t>(i) + 2;
        state.published_ns = now_ns();
        task.observe(m, d, state.value);
        obs_ch.publish(state);

        auto tick_elapsed = steady_clock::now() - tick_start;
        control_tick.record(tick_elapsed);

        TickRecord rec{};
        rec.tick = static_cast<uint64_t>(i);
        rec.timestamp_ns = now_ns();
        rec.inference_ns = static_cast<uint32_t>(last_compute_ns);
        rec.tick_work_ns = static_cast<uint32_t>(duration_cast<nanoseconds>(tick_elapsed).count());
        rec.qpos_dim = static_cast<uint16_t>(m->nq);
        rec.action_dim = static_cast<uint16_t>(m->nu);
        for (int j = 0; j < m->nq; j++) rec.qpos[j] = (float)d->qpos[j];
        for (int j = 0; j < m->nu; j++) rec.action[j] = held[j];
        if (!tick_ring_push(&ring, rec)) ring_drops++;

        auto before_sleep = steady_clock::now();
        while (next_deadline < before_sleep) {
            next_deadline += std::chrono::microseconds(args.control_us);
            missed++;
        }
        std::this_thread::sleep_until(next_deadline);

        auto wake = steady_clock::now();
        control_period.record(wake - last_wake);
        last_wake = wake;
    }

    running.store(false, std::memory_order_relaxed);
    policy_thread.join();
    sidecar.stop();

    std::printf("\n");
    task.report(m, d);
    print_latency("control_tick", control_tick.stats());
    print_latency("control_period", control_period.stats());
    print_latency("inference", inference.stats());

    std::printf("policy cycles     : %llu\n", (unsigned long long)cmd_ch.publications());
    std::printf("reused command    : %llu / %d ticks\n", (unsigned long long)reused_ticks,
                args.ticks);
    std::printf("fallback engaged  : %llu ticks\n", (unsigned long long)fallback_ticks);
    std::printf("worst command age : %.1f us\n", max_age_seen / 1000.0);
    std::printf("missed deadlines  : %d / %d\n", missed, args.ticks);
    std::printf("ring drops        : %llu\n", (unsigned long long)ring_drops);
    sidecar.print_summary();
    if (args.trajectory) std::printf("trajectory        : %s\n", args.trajectory);

    return 0;
}
