"""Render a trajectory CSV written by the C++ sidecar to a GIF.

All physics and control ran in C++; this only replays recorded qpos.
"""

import argparse
import csv

import imageio.v2 as imageio
import mujoco
import numpy as np

CAMERAS = {
    "pusher": dict(lookat=(0.3, -0.3, -0.3), distance=1.6, azimuth=180, elevation=-50),
    "cartpole": dict(lookat=(0.0, 0.0, 0.2), distance=2.2, azimuth=90, elevation=-10),
}


def load(path):
    with open(path) as f:
        rows = list(csv.reader(f))
    header, rows = rows[0], rows[1:]
    qcols = [i for i, h in enumerate(header) if h.startswith("qpos")]
    t_ns = np.array([int(r[1]) for r in rows], dtype=np.int64)
    qpos = np.array([[float(r[i]) for i in qcols] for r in rows])
    return t_ns, qpos


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("model")
    ap.add_argument("out")
    ap.add_argument("--camera", choices=CAMERAS, required=True)
    ap.add_argument("--fps", type=int, default=25)
    ap.add_argument("--width", type=int, default=480)
    ap.add_argument("--height", type=int, default=360)
    args = ap.parse_args()

    t_ns, qpos = load(args.csv)
    model = mujoco.MjModel.from_xml_path(args.model)
    data = mujoco.MjData(model)
    renderer = mujoco.Renderer(model, height=args.height, width=args.width)

    cam = mujoco.MjvCamera()
    for k, v in CAMERAS[args.camera].items():
        setattr(cam, k, v)

    # One frame per 1/fps of recorded wall time.
    elapsed = (t_ns - t_ns[0]) / 1e9
    frame_times = np.arange(0.0, elapsed[-1], 1.0 / args.fps)
    idx = np.searchsorted(elapsed, frame_times)

    frames = []
    for i in idx:
        data.qpos[: qpos.shape[1]] = qpos[i]
        mujoco.mj_forward(model, data)
        renderer.update_scene(data, camera=cam)
        frames.append(renderer.render())

    imageio.mimsave(args.out, frames, duration=1000 / args.fps, loop=0)
    print(f"{len(frames)} frames, {elapsed[-1]:.1f}s -> {args.out}")


if __name__ == "__main__":
    main()
