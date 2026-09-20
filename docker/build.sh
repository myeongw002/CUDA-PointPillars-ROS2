#!/usr/bin/env bash
set -euo pipefail

IMAGE="${IMAGE:-cuda-pointpillars-ros2:humble-trt10.8-cuda12.8}"
CUDA_ARCH="${CUDA_ARCH:-120}"

if command -v git-lfs >/dev/null 2>&1; then
  git lfs pull
else
  echo "warning: git-lfs is not installed; make sure model/pointpillar.onnx is not an LFS pointer" >&2
fi

echo "Building dependency image only; the ROS package source is bind-mounted at runtime."
docker build \
  --progress=plain \
  --build-arg CUDA_ARCH="${CUDA_ARCH}" \
  --build-arg TENSORRT_VERSION="${TENSORRT_VERSION:-10.8.0.43-1+cuda12.8}" \
  -t "${IMAGE}" \
  .
