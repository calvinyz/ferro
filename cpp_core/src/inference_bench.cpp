// Times single-sample inference for one ONNX model at a fixed call period, on
// CPU or CUDA. Inputs and outputs live in host memory, so CUDA timings include
// the copies a control loop would pay.

#include "latency.h"

#include <onnxruntime_cxx_api.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using std::chrono::steady_clock;

namespace {

struct Args {
    std::string model;
    std::string provider = "cpu";
    int period_us = 0;  // 0 means back to back
    int iters = 2000;
    int warmup = 200;
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string k = argv[i];
        const char* v = argv[i + 1];
        if (k == "--model") a.model = v;
        else if (k == "--provider") a.provider = v;
        else if (k == "--period-us") a.period_us = std::atoi(v);
        else if (k == "--iters") a.iters = std::atoi(v);
        else if (k == "--warmup") a.warmup = std::atoi(v);
        else throw std::runtime_error("unknown flag " + k);
    }
    if (a.model.empty()) throw std::runtime_error("--model is required");
    return a;
}

int run(int argc, char** argv) {
    Args args = parse(argc, argv);

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "inference_bench");
    Ort::SessionOptions so;
    if (args.provider == "cuda") {
        OrtCUDAProviderOptions cuda{};
        so.AppendExecutionProvider_CUDA(cuda);
        // Fail rather than quietly run unsupported nodes on the CPU.
        so.AddConfigEntry("session.disable_cpu_ep_fallback", "1");
    } else if (args.provider != "cpu") {
        throw std::runtime_error("provider must be cpu or cuda");
    }
    Ort::Session session(env, args.model.c_str(), so);

    auto shape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    const int64_t in_dim = shape.back();
    std::vector<int64_t> in_shape = {1, in_dim};

    std::mt19937 rng(0);
    std::normal_distribution<float> noise(0.0f, 1.0f);
    std::vector<float> obs(in_dim);
    for (auto& v : obs) v = noise(rng);

    auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::AllocatorWithDefaultOptions alloc;
    std::string in_name = session.GetInputNameAllocated(0, alloc).get();
    std::string out_name = session.GetOutputNameAllocated(0, alloc).get();
    const char* in_names[] = {in_name.c_str()};
    const char* out_names[] = {out_name.c_str()};

    LatencyRecorder latency(args.iters);
    auto next = steady_clock::now();

    for (int i = 0; i < args.warmup + args.iters; i++) {
        auto input = Ort::Value::CreateTensor<float>(mem, obs.data(), obs.size(),
                                                     in_shape.data(), in_shape.size());
        auto t0 = steady_clock::now();
        auto out = session.Run(Ort::RunOptions{nullptr}, in_names, &input, 1, out_names, 1);
        auto elapsed = steady_clock::now() - t0;
        if (i >= args.warmup) latency.record(elapsed);

        if (args.period_us > 0) {
            next += std::chrono::microseconds(args.period_us);
            std::this_thread::sleep_until(next);
        }
    }

    std::printf("%s %s period_us=%d\n", args.model.c_str(), args.provider.c_str(), args.period_us);
    print_latency("inference", latency.stats());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
