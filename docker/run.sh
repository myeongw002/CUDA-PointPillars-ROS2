#!/usr/bin/env bash
set -euo pipefail

IMAGE="${IMAGE:-cuda-pointpillars-ros2:humble-trt10.8-cuda12.8}"
ROS_DOMAIN_ID_VALUE="${ROS_DOMAIN_ID:-0}"
CUDA_ARCH_VALUE="${CUDA_ARCH:-120}"
FASTDDS_TRANSPORTS_VALUE="${FASTDDS_BUILTIN_TRANSPORTS:-UDPv4}"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE="/workspace/ros2_ws"
PACKAGE_MOUNT="${WORKSPACE}/src/CUDA-PointPillars-ROS2"

BUILD_VOLUME="${BUILD_VOLUME:-pointpillars_ros2_build}"
INSTALL_VOLUME="${INSTALL_VOLUME:-pointpillars_ros2_install}"
LOG_VOLUME="${LOG_VOLUME:-pointpillars_ros2_log}"

RUNTIME_ENV=(
  -e ROS_DOMAIN_ID="${ROS_DOMAIN_ID_VALUE}"
  -e CUDA_ARCH="${CUDA_ARCH_VALUE}"
  -e FASTDDS_BUILTIN_TRANSPORTS="${FASTDDS_TRANSPORTS_VALUE}"
  -e NVIDIA_VISIBLE_DEVICES=all
  -e NVIDIA_DRIVER_CAPABILITIES=compute,utility
)

# Topic environment variables are optional. If omitted, the values in
# config/pointpillars.yaml are used unchanged.
if [[ -n "${INPUT_TOPIC:-}" ]]; then
  RUNTIME_ENV+=(-e "INPUT_TOPIC=${INPUT_TOPIC}")
fi
if [[ -n "${OUTPUT_TOPIC:-}" ]]; then
  RUNTIME_ENV+=(-e "OUTPUT_TOPIC=${OUTPUT_TOPIC}")
fi

MODEL_MOUNT=()
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
  RUNTIME_ENV+=(-e "POINTPILLARS_MODEL_PATH=/models/${MODEL_FILE}")
fi

docker run --rm -it \
  --gpus all \
  --network host \
  --ipc host \
  "${RUNTIME_ENV[@]}" \
  -v "${REPO_ROOT}:${PACKAGE_MOUNT}:rw" \
  -v "${BUILD_VOLUME}:${WORKSPACE}/build" \
  -v "${INSTALL_VOLUME}:${WORKSPACE}/install" \
  -v "${LOG_VOLUME}:${WORKSPACE}/log" \
  "${MODEL_MOUNT[@]}" \
  "${IMAGE}" \
  bash "${PACKAGE_MOUNT}/docker/start.sh"
