#include "mujoco_wrapper.h"

#include <iostream>
#include <mujoco/mujoco.h>
#include <onnxruntime_cxx_api.h>
#include <vector>

int main() {
    const char* model_path = INVERTED_PENDULUM_PATH;
    const char* policy_path = POLICY_PATH;

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "ferro");
    Ort::Session session(env, policy_path, Ort::SessionOptions{});
    {
        Model model(model_path);
        Data data(model);

        std::cout << "Stepping simulation 100 times...\n";
        for (int i = 0; i < 100; i++) {
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
            auto output_tensors = session.Run(
                Ort::RunOptions{nullptr},
                input_names, &input_tensor, 1,
                output_names, 1
            );

            // extract action and apply to control
            const float* action_data = output_tensors[0].GetTensorData<float>();
            data.get()->ctrl[0] = action_data[0];

            // step simulation
            mj_step(model.get(), data.get());

            if (i % 10 == 0) {
                std::cout << "Step " << i << ": pole_angle=" << data.get()->qpos[1]
                            << ", action=" << action_data[0] << "\n";
            }
        }
    }

    return 0;
}
