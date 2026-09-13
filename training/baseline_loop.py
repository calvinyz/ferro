"""Python equivalent of ferro_core, for comparison against the C++ numbers."""

import argparse
import gc
import time

import mujoco
import numpy as np
import onnxruntime as ort

MODEL_PATH = "../third_party/models/inverted_pendulum.xml"
POLICY_PATH = "../third_party/policies/toy_policy.onnx"


def percentile(sorted_ns, p):
    if not sorted_ns:
        return 0.0
    rank = max(1, min(len(sorted_ns), int(-(-p / 100.0 * len(sorted_ns) // 1))))
    return sorted_ns[rank - 1] / 1000.0


def report(label, samples_ns):
    s = sorted(samples_ns)
    mean_us = sum(s) / len(s) / 1000.0 if s else 0.0
    print(
        f"{label:<16} n={len(s):<6} mean={mean_us:7.1f}us p50={percentile(s, 50):7.1f}us "
        f"p99={percentile(s, 99):7.1f}us min={s[0] / 1000.0:7.1f}us max={s[-1] / 1000.0:7.1f}us"
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ticks", nargs="?", type=int, default=500)
    ap.add_argument("period_us", nargs="?", type=int, default=20000)
    ap.add_argument("--no-gc", action="store_true", help="disable the cycle collector")
    args = ap.parse_args()

    if args.no_gc:
        gc.disable()

    model = mujoco.MjModel.from_xml_path(MODEL_PATH)
    data = mujoco.MjData(model)
    session = ort.InferenceSession(POLICY_PATH, providers=["CPUExecutionProvider"])

    inference_ns, tick_ns, period_ns, wake_ns = [], [], [], []
    missed = 0

    period = args.period_us * 1000
    next_deadline = time.perf_counter_ns()
    last_wake = next_deadline

    print(f"python baseline: {args.ticks} ticks at {args.period_us}us "
          f"({1e6 / args.period_us:.0f} Hz), gc={'off' if args.no_gc else 'on'}")

    for _ in range(args.ticks):
        next_deadline += period
        tick_start = time.perf_counter_ns()

        obs = np.array(
            [[data.qpos[0], data.qpos[1], data.qvel[0], data.qvel[1]]], dtype=np.float32
        )

        inf_start = time.perf_counter_ns()
        action = session.run(None, {"observation": obs})[0]
        inference_ns.append(time.perf_counter_ns() - inf_start)

        data.ctrl[0] = action[0][0]
        mujoco.mj_step(model, data)

        tick_ns.append(time.perf_counter_ns() - tick_start)

        # Skip whole periods rather than sprinting through the backlog.
        before_sleep = time.perf_counter_ns()
        while next_deadline < before_sleep:
            next_deadline += period
            missed += 1

        remaining = next_deadline - time.perf_counter_ns()
        if remaining > 0:
            time.sleep(remaining / 1e9)

        wake = time.perf_counter_ns()
        wake_ns.append(wake - next_deadline)
        period_ns.append(wake - last_wake)
        last_wake = wake

    print()
    report("inference", inference_ns)
    report("tick_work", tick_ns)
    report("period", period_ns)
    report("wake_lateness", wake_ns)
    print(f"missed deadlines: {missed} / {args.ticks}")
    print(f"final pole angle: {data.qpos[1]:.5f}")


if __name__ == "__main__":
    main()
