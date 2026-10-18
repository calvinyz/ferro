#!/usr/bin/env bash
# Builds and runs ferro inside the ROS 2 container. Usage:
#   scripts/ros.sh build
#   scripts/ros.sh run [ferro_cascade args...]
#   scripts/ros.sh shell
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE=ferro-ros:jazzy

docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" -f "$REPO/docker/ros.Dockerfile" "$REPO/docker"

# SYS_NICE plus an rtprio limit lets the control thread take SCHED_FIFO.
# Cargo needs a writable home; the toolchain itself is read-only in the image.
run() {
    docker run --rm -i --network host --ipc host \
        --cap-add SYS_NICE --ulimit rtprio=99 \
        --user "$(id -u):$(id -g)" -e HOME=/tmp -e CARGO_HOME=/tmp/cargo -e ROS_LOG_DIR=/tmp/ros_log \
        -v "$REPO:/ferro" -w /ferro "$IMAGE" \
        bash -c "source /opt/ros/jazzy/setup.bash && $*"
}

case "${1:-}" in
    build) run "cmake -B build-ros && cmake --build build-ros --target ferro_cascade" ;;
    run)   shift; run "./build-ros/cpp_core/ferro_cascade $*" ;;
    shell) run "bash" ;;
    *)     echo "usage: $0 build|run|shell" >&2; exit 1 ;;
esac
