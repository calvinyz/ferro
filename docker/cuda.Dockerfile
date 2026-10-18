# CUDA 13.2 with cuDNN 9, matching r11's driver, for the GPU inference sweep.
FROM nvidia/cuda:13.2.1-cudnn-runtime-ubuntu24.04

RUN apt-get update && apt-get install -y --no-install-recommends cmake g++ make \
    && rm -rf /var/lib/apt/lists/*
