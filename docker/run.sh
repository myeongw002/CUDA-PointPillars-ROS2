#!/usr/bin/env bash
set -euo pipefail

IMAGE="${IMAGE:-cuda-pointpillars-ros2:humble-trt8.6}"
INPUT_TOPIC="${INPUT_TOPIC:-/point_cloud}"
OUTPUT_TOPIC="${OUTPUT_TOPIC:-/pointpillars/detections}"
ROS_DOMAIN_ID_VALUE="${ROS_DOMAIN_ID:-0}"
CUDA_ARCH_VALUE="${CUDA_ARCH:-86}"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE="/workspace/ros2_ws"
PACKAGE_MOUNT="${WORKSPACE}/src/CUDA-PointPillars-ROS2"

BUILD_VOLUME="${BUILD_VOLUME:-pointpillars_ros2_build}"
INSTALL_VOLUME="${INSTALL_VOLUME:-pointpillars_ros2_install}"
LOG_VOLUME="${LOG_VOLUME:-pointpillars_ros2_log}"

MODEL_MOUNT=()
MODEL_ENV=()
if [[ -n "${MODEL_PATH:-}" ]]; then
  MODEL_ABS="$(realpath "${MODEL_PATH}")"
  if [[ ! -f "${MODEL_ABS}" ]]; then
    echo "MODEL_PATH does not exist: ${MODEL_ABS}" >&2
    exit 1
  fi
  MODEL_DIR="$(dirname "${MODEL_ABS}")"
  MODEL_FILE="$(basename "${MODEL_ABS}")"
  # TensorRT writes <model>.cache next to the ONNX file, so mount the model
  # directory read-write rather than mounting the file read-only.
  MODEL_MOUNT=(-v "${MODEL_DIR}:/models:rw")
  MODEL_ENV=(-e "POINTPILLARS_MODEL_PATH=/models/${MODEL_FILE}")
fi

docker run --rm -it \
  --gpus all \
  --network host \
  --ipc host \
  -e ROS_DOMAIN_ID="${ROS_DOMAIN_ID_VALUE}" \
  -e CUDA_ARCH="${CUDA_ARCH_VALUE}" \
  -e INPUT_TOPIC="${INPUT_TOPIC}" \
  -e OUTPUT_TOPIC="${OUTPUT_TOPIC}" \
  -e NVIDIA_VISIBLE_DEVICES=all \
  -e NVIDIA_DRIVER_CAPABILITIES=compute,utility \
  "${MODEL_ENV[@]}" \
  -v "${REPO_ROOT}:${PACKAGE_MOUNT}:rw" \
  -v "${BUILD_VOLUME}:${WORKSPACE}/build" \
  -v "${INSTALL_VOLUME}:${WORKSPACE}/install" \
  -v "${LOG_VOLUME}:${WORKSPACE}/log" \
  "${MODEL_MOUNT[@]}" \
  "${IMAGE}" \
  bash "${PACKAGE_MOUNT}/docker/start.sh"
