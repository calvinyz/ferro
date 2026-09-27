"""Check C++ task observations and dynamics against gymnasium.

Each random state gets a random control applied for one env step, so both the
observation formula and the model file have to match.
"""

import subprocess
import sys

import gymnasium as gym
import numpy as np

ENVS = {"cartpole": "InvertedPendulum-v5", "pusher": "Pusher-v5"}


def main(binary, n=200):
    worst = 0.0
    for task, env_id in ENVS.items():
        env = gym.make(env_id).unwrapped
        rng = np.random.default_rng(0)
        lo, hi = env.action_space.low, env.action_space.high
        lines, expected = [], []
        for _ in range(n):
            qpos = env.init_qpos + rng.uniform(-0.5, 0.5, env.model.nq)
            qvel = rng.uniform(-2, 2, env.model.nv)
            ctrl = rng.uniform(lo, hi)
            env.set_state(qpos, qvel)
            env.do_simulation(ctrl, env.frame_skip)
            expected.append(env._get_obs())
            lines.append(" ".join(f"{v:.17g}" for v in np.concatenate([qpos, qvel, ctrl])))

        out = subprocess.run([binary, task, str(env.frame_skip)], input="\n".join(lines) + "\n",
                             capture_output=True, text=True, check=True).stdout
        rows = [ln for ln in out.splitlines() if ln and (ln[0].isdigit() or ln[0] in "-.")]
        got = np.array([[float(x) for x in ln.split()] for ln in rows])
        err = float(np.max(np.abs(got - np.array(expected))))
        worst = max(worst, err)
        print(f"{task:9s} {n} states x {env.frame_skip} steps, obs dim {got.shape[1]}, "
              f"max abs error {err:.2e}")

    ok = worst < 1e-4
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main(sys.argv[1])
