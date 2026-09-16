#!/usr/bin/env bash
set -euo pipefail

IMAGE="${IMAGE:-cuda-pointpillars-ros2:humble-trt8.6}"
CUDA_ARCH="${CUDA_ARCH:-86}"

if command -v git-lfs >/dev/null 2>&1; then
  git lfs pull
else
  echo "warning: git-lfs is not installed; make sure model/pointpillar.onnx is not an LFS pointer" >&2
fi

docker build \
  --build-arg CUDA_ARCH="${CUDA_ARCH}" \
  -t "${IMAGE}" \
  .
