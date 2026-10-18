# ROS 2 Jazzy build and run environment for the ferro_cascade ROS bridge.
# Run as the host user so build output in the mounted repo is not root-owned.
FROM ros:jazzy

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake g++ curl ca-certificates ros-jazzy-foxglove-bridge \
    && rm -rf /var/lib/apt/lists/*

ENV RUSTUP_HOME=/opt/rust/rustup CARGO_HOME=/opt/rust/cargo
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain 1.98.1 \
    && chmod -R a+rX /opt/rust
ENV PATH=/opt/rust/cargo/bin:$PATH
