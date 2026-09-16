#!/usr/bin/env bash
set -euo pipefail

IMAGE="${IMAGE:-cuda-pointpillars-ros2:humble-trt8.6}"
ROS_DOMAIN_ID_VALUE="${ROS_DOMAIN_ID:-0}"
CUDA_ARCH_VALUE="${CUDA_ARCH:-86}"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE="/workspace/ros2_ws"
PACKAGE_MOUNT="${WORKSPACE}/src/CUDA-PointPillars-ROS2"

BUILD_VOLUME="${BUILD_VOLUME:-pointpillars_ros2_build}"
INSTALL_VOLUME="${INSTALL_VOLUME:-pointpillars_ros2_install}"
LOG_VOLUME="${LOG_VOLUME:-pointpillars_ros2_log}"

docker run --rm -it \
  --gpus all \
  --network host \
  --ipc host \
  -e ROS_DOMAIN_ID="${ROS_DOMAIN_ID_VALUE}" \
  -e CUDA_ARCH="${CUDA_ARCH_VALUE}" \
  -e NVIDIA_VISIBLE_DEVICES=all \
  -e NVIDIA_DRIVER_CAPABILITIES=compute,utility \
  -v "${REPO_ROOT}:${PACKAGE_MOUNT}:rw" \
  -v "${BUILD_VOLUME}:${WORKSPACE}/build" \
  -v "${INSTALL_VOLUME}:${WORKSPACE}/install" \
  -v "${LOG_VOLUME}:${WORKSPACE}/log" \
  "${IMAGE}" \
  bash
