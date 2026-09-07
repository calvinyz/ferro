"""Train a PPO policy on a MuJoCo task and export it to ONNX."""

import gymnasium as gym
import torch
import torch.nn as nn
from stable_baselines3 import PPO

ENV_ID = "InvertedPendulum-v5"
TOTAL_TIMESTEPS = 50_000
ONNX_PATH = "toy_policy.onnx"


class OnnxPolicyWrapper(nn.Module):
    """Wraps an SB3 policy so ONNX export only sees the deterministic action path."""

    def __init__(self, policy):
        super().__init__()
        self.policy = policy

    def forward(self, observation):
        return self.policy(observation, deterministic=True)[0]


def main():
    env = gym.make(ENV_ID)
    model = PPO("MlpPolicy", env, verbose=1)
    model.learn(total_timesteps=TOTAL_TIMESTEPS)

    mean_reward = evaluate(model, env)
    print(f"mean reward over 10 eval episodes: {mean_reward:.2f}")

    wrapped = OnnxPolicyWrapper(model.policy).eval()
    obs_dim = env.observation_space.shape[0]
    dummy_input = torch.zeros(1, obs_dim)

    torch.onnx.export(
        wrapped,
        dummy_input,
        ONNX_PATH,
        input_names=["observation"],
        output_names=["action"],
        dynamic_axes={"observation": {0: "batch"}, "action": {0: "batch"}},
        opset_version=17,
        dynamo=False,
    )
    print(f"exported ONNX policy to {ONNX_PATH}")


def evaluate(model, env, episodes=10):
    total = 0.0
    for _ in range(episodes):
        obs, _ = env.reset()
        done = False
        while not done:
            action, _ = model.predict(obs, deterministic=True)
            obs, reward, terminated, truncated, _ = env.step(action)
            total += reward
            done = terminated or truncated
    return total / episodes


if __name__ == "__main__":
    main()
