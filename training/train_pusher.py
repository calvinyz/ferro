"""Train SAC on Pusher-v5 and export the deterministic actor to ONNX."""

import argparse

import gymnasium as gym
import numpy as np
import torch
import torch.nn as nn
from stable_baselines3 import SAC
from stable_baselines3.common.callbacks import CheckpointCallback

ENV_ID = "Pusher-v5"


class OnnxActor(nn.Module):
    """Deterministic actor with the [-1, 1] squash rescaled to the action bounds."""

    def __init__(self, actor, low, high):
        super().__init__()
        self.actor = actor
        self.register_buffer("low", torch.as_tensor(low, dtype=torch.float32))
        self.register_buffer("high", torch.as_tensor(high, dtype=torch.float32))

    def forward(self, observation):
        a = self.actor(observation, deterministic=True)
        return self.low + (a + 1.0) * 0.5 * (self.high - self.low)


def evaluate(model, episodes=10):
    env = gym.make(ENV_ID)
    returns, final_dists = [], []
    for ep in range(episodes):
        obs, _ = env.reset(seed=ep)
        done, total = False, 0.0
        while not done:
            action, _ = model.predict(obs, deterministic=True)
            obs, r, term, trunc, _ = env.step(action)
            total += r
            done = term or trunc
        returns.append(total)
        final_dists.append(float(np.linalg.norm(obs[17:19] - obs[20:22])))
    return float(np.mean(returns)), float(np.mean(final_dists))


def export(model, path):
    env = gym.make(ENV_ID)
    wrapped = OnnxActor(model.policy.actor, env.action_space.low, env.action_space.high).eval()
    dummy = torch.zeros(1, env.observation_space.shape[0])
    torch.onnx.export(
        wrapped, dummy, path,
        input_names=["observation"], output_names=["action"],
        dynamic_axes={"observation": {0: "batch"}, "action": {0: "batch"}},
        opset_version=17, dynamo=False,
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--steps", type=int, default=400_000)
    ap.add_argument("--out", default="pusher_policy.onnx")
    ap.add_argument("--resume", help="saved model to continue training from")
    args = ap.parse_args()

    torch.set_num_threads(4)
    if args.resume:
        model = SAC.load(args.resume, env=gym.make(ENV_ID), device="cpu")
    else:
        model = SAC("MlpPolicy", gym.make(ENV_ID), verbose=0, device="cpu")
    model.learn(
        total_timesteps=args.steps,
        callback=CheckpointCallback(50_000, "checkpoints", name_prefix="pusher"),
        reset_num_timesteps=not args.resume,
        progress_bar=False,
    )
    model.save("pusher_sac")

    mean_return, mean_dist = evaluate(model)
    print(f"mean return {mean_return:.2f}, mean final object-goal distance {mean_dist:.3f} m")

    export(model, args.out)
    print(f"exported {args.out}")


if __name__ == "__main__":
    main()
