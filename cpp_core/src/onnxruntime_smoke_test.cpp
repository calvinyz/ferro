#include <array>
#include <cstdint>
#include <cstdio>
#include <onnxruntime_cxx_api.h>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <model.onnx>\n", argv[0]);
        return 1;
    }

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "onnxruntime_smoke_test");
    Ort::SessionOptions session_options;
    Ort::Session session(env, argv[1], session_options);

    Ort::AllocatorWithDefaultOptions allocator;
    std::string input_name = session.GetInputNameAllocated(0, allocator).get();
    std::string output_name = session.GetOutputNameAllocated(0, allocator).get();

    std::vector<float> observation = {0.0f, 0.0f, 0.0f, 0.0f};
    std::array<int64_t, 2> input_shape = {1, static_cast<int64_t>(observation.size())};

    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, observation.data(), observation.size(), input_shape.data(), input_shape.size());

    const char* input_names[] = {input_name.c_str()};
    const char* output_names[] = {output_name.c_str()};

    auto outputs = session.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

    float* action = outputs[0].GetTensorMutableData<float>();
    size_t action_dim = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();

    std::printf("action:");
    for (size_t i = 0; i < action_dim; ++i) {
        std::printf(" %f", action[i]);
    }
    std::printf("\n");

    return 0;
}
