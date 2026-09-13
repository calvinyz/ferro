#include "latency.h"
#include "mujoco_wrapper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mujoco/mujoco.h>
#include <onnxruntime_cxx_api.h>
#include <random>
#include <vector>

using std::chrono::steady_clock;

// Episode rules mirror gymnasium's InvertedPendulum-v5, so the success rate is
// comparable to what the policy scored during training.
constexpr int kMaxEpisodeSteps = 1000;
constexpr double kAngleLimit = 0.2;
constexpr double kResetNoise = 0.01;

int main(int argc, char** argv) {
    const int num_episodes = (argc > 1) ? std::atoi(argv[1]) : 20;
    const uint32_t base_seed = (argc > 2) ? static_cast<uint32_t>(std::atoi(argv[2])) : 0;

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "ferro_eval");
    Ort::Session session(env, POLICY_PATH, Ort::SessionOptions{});
    Model model(INVERTED_PENDULUM_PATH);
    Data data(model);

    LatencyRecorder inference(static_cast<size_t>(num_episodes) * kMaxEpisodeSteps);
    int successes = 0;
    int64_t total_steps = 0;

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const char* input_names[] = {"observation"};
    const char* output_names[] = {"action"};

    // Not rate limited. This is batch evaluation, so it runs flat out and the
    // latency figures below are inference cost, not control-loop timing.
    for (int ep = 0; ep < num_episodes; ep++) {
        mj_resetData(model.get(), data.get());

        // Seeded per episode so a run is reproducible from the base seed.
        std::mt19937 rng(base_seed + static_cast<uint32_t>(ep));
        std::uniform_real_distribution<double> noise(-kResetNoise, kResetNoise);
        for (int j = 0; j < model.get()->nq; j++) data.get()->qpos[j] += noise(rng);
        for (int j = 0; j < model.get()->nv; j++) data.get()->qvel[j] += noise(rng);

        int steps = 0;
        bool terminated = false;

        while (steps < kMaxEpisodeSteps && !terminated) {
            std::vector<float> obs(4);
            obs[0] = (float)data.get()->qpos[0];
            obs[1] = (float)data.get()->qpos[1];
            obs[2] = (float)data.get()->qvel[0];
            obs[3] = (float)data.get()->qvel[1];

            std::vector<int64_t> input_shape = {1, 4};
            auto input_tensor = Ort::Value::CreateTensor<float>(
                memory_info, obs.data(), 4, input_shape.data(), 2);

            auto inf_start = steady_clock::now();
            auto outputs = session.Run(
                Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);
            inference.record(steady_clock::now() - inf_start);

            data.get()->ctrl[0] = outputs[0].GetTensorData<float>()[0];
            mj_step(model.get(), data.get());
            steps++;

            // Checked after the step, on the resulting state, as gymnasium does.
            double angle = data.get()->qpos[1];
            terminated = !std::isfinite(angle) || std::abs(angle) > kAngleLimit;
        }

        total_steps += steps;
        if (!terminated) successes++;

        std::printf("episode %3d: %4d steps%s\n", ep, steps, terminated ? " (fell)" : "");
    }

    double success_rate = 100.0 * successes / num_episodes;
    double mean_steps = static_cast<double>(total_steps) / num_episodes;

    std::printf("\nepisodes     : %d (base seed %u)\n", num_episodes, base_seed);
    std::printf("success rate : %.1f%% (%d/%d)\n", success_rate, successes, num_episodes);
    std::printf("mean length  : %.1f / %d steps\n", mean_steps, kMaxEpisodeSteps);
    print_latency("inference", inference.stats());

    return successes == num_episodes ? 0 : 1;
}
