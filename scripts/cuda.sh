#!/usr/bin/env bash
# Builds and runs the inference sweep inside a CUDA container. Needs
# third_party/onnxruntime-gpu from scripts/fetch_onnxruntime.sh --gpu. Usage:
#   scripts/cuda.sh build
#   scripts/cuda.sh sweep <out.csv>
#   scripts/cuda.sh shell
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE=ferro-cuda:13.2

docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" -f "$REPO/docker/cuda.Dockerfile" "$REPO/docker"

run() {
    docker run --rm -i --gpus all --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$REPO:/ferro" -w /ferro "$IMAGE" bash -c "$*"
}

case "${1:-}" in
    build) run "cmake -B build-cuda -DFERRO_ONNXRUNTIME_DIR=/ferro/third_party/onnxruntime-gpu && cmake --build build-cuda --target inference_bench" ;;
    sweep) run "scripts/bench_sweep.sh build-cuda ${2:?out.csv} cpu cuda" ;;
    shell) run "bash" ;;
    *)     echo "usage: $0 build|sweep <out.csv>|shell" >&2; exit 1 ;;
esac
