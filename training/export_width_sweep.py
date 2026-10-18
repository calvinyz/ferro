"""Export MLPs shaped like the pusher policy at increasing widths, for serving
benchmarks. Weights are random; inference cost does not depend on them."""

import argparse
from pathlib import Path

import torch
import torch.nn as nn

OBS_DIM, ACT_DIM = 23, 7


def mlp(width):
    return nn.Sequential(
        nn.Linear(OBS_DIM, width), nn.ReLU(),
        nn.Linear(width, width), nn.ReLU(),
        nn.Linear(width, ACT_DIM), nn.Tanh(),
    ).eval()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="../bench/models")
    ap.add_argument("--widths", type=int, nargs="+", default=[64, 256, 1024, 2048, 4096, 8192])
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    torch.manual_seed(0)

    for w in args.widths:
        model = mlp(w)
        params = sum(p.numel() for p in model.parameters())
        path = out / f"mlp_w{w}.onnx"
        torch.onnx.export(
            model, torch.zeros(1, OBS_DIM), path,
            input_names=["observation"], output_names=["action"],
            dynamic_axes={"observation": {0: "batch"}, "action": {0: "batch"}},
            opset_version=17, dynamo=False,
        )
        print(f"width {w:5d}  {params:>11,} params  {path.stat().st_size / 1e6:7.1f} MB  {path}")


if __name__ == "__main__":
    main()
