"""Plot control-loop tick intervals from sidecar trajectories, default priority
against SCHED_FIFO, idle against every core loaded."""

import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

RUNS = Path(__file__).resolve().parent.parent / "bench" / "runs"
OUT = Path(__file__).resolve().parent.parent / "docs" / "period_jitter.png"

SURFACE, TEXT, TEXT_2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
SERIES = {"default": "#2a78d6", "fifo": "#eb6834"}
LABEL = {"default": "default priority", "fifo": "SCHED_FIFO"}
PERIOD_US = 1000.0


def intervals(path):
    with open(path) as f:
        t = np.array([int(r[1]) for r in list(csv.reader(f))[1:]], dtype=np.int64)
    return (t[1:] - t[0]) / 1e9, np.diff(t) / 1e3


def main():
    plt.rcParams.update({"font.size": 10, "text.color": TEXT, "axes.labelcolor": TEXT_2,
                         "xtick.color": TEXT_2, "ytick.color": TEXT_2})
    fig, axes = plt.subplots(2, 2, figsize=(11, 6), sharex=True, sharey=True, facecolor=SURFACE)

    for row, mode in enumerate(["default", "fifo"]):
        for col, load in enumerate(["idle", "loaded"]):
            ax = axes[row][col]
            t, dt = intervals(RUNS / f"period_{mode}_{load}.csv")
            ax.set_facecolor(SURFACE)
            ax.axhline(PERIOD_US, color=TEXT_2, lw=1, ls=(0, (4, 3)), zorder=1)
            ax.plot(t, dt, color=SERIES[mode], lw=1, zorder=2)
            ax.set_title(f"{LABEL[mode]}, {'idle' if load == 'idle' else 'all 20 cores loaded'}\n"
                         f"worst {dt.max():.0f}us", loc="left", fontsize=10, color=TEXT)
            ax.grid(axis="y", color=GRID, lw=0.8)
            for side in ("top", "right"):
                ax.spines[side].set_visible(False)
            for side in ("left", "bottom"):
                ax.spines[side].set_color(GRID)
            ax.tick_params(length=0)
            if col == 0:
                ax.set_ylabel("tick interval (us)")
            if row == 1:
                ax.set_xlabel("time (s)")

    axes[0][0].text(0.2, PERIOD_US + 120, "1 ms target", color=TEXT_2, fontsize=9)
    fig.suptitle("Control loop at 1 kHz, pusher task, r11 (i9-10900KF)", x=0.06, ha="left",
                 fontsize=12, color=TEXT)
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    fig.savefig(OUT, dpi=110, facecolor=SURFACE)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
