#!/usr/bin/env bash
set -euo pipefail

IMAGE="${IMAGE:-cuda-pointpillars-ros2:humble-trt8.6}"
INPUT_TOPIC="${INPUT_TOPIC:-/point_cloud}"
OUTPUT_TOPIC="${OUTPUT_TOPIC:-/pointpillars/detections}"
ROS_DOMAIN_ID_VALUE="${ROS_DOMAIN_ID:-0}"

MODEL_MOUNT=()
MODEL_ARG=()
if [[ -n "${MODEL_PATH:-}" ]]; then
  MODEL_ABS="$(realpath "${MODEL_PATH}")"
  MODEL_DIR="$(dirname "${MODEL_ABS}")"
  MODEL_FILE="$(basename "${MODEL_ABS}")"
  MODEL_MOUNT=(-v "${MODEL_DIR}:/models")
  MODEL_ARG=("model_path:=/models/${MODEL_FILE}")
fi

docker run --rm -it \
  --gpus all \
  --network host \
  --ipc host \
  -e ROS_DOMAIN_ID="${ROS_DOMAIN_ID_VALUE}" \
  -e NVIDIA_VISIBLE_DEVICES=all \
  -e NVIDIA_DRIVER_CAPABILITIES=compute,utility \
  "${MODEL_MOUNT[@]}" \
  "${IMAGE}" \
  ros2 launch cuda_pointpillars_ros pointpillars.launch.py \
    input_cloud_topic:="${INPUT_TOPIC}" \
    output_detections_topic:="${OUTPUT_TOPIC}" \
    "${MODEL_ARG[@]}"
