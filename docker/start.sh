#!/usr/bin/env bash
set -euo pipefail

ROS_DISTRO_VALUE="${ROS_DISTRO:-humble}"
CUDA_ARCH_VALUE="${CUDA_ARCH:-86}"
INPUT_TOPIC_VALUE="${INPUT_TOPIC:-/point_cloud}"
OUTPUT_TOPIC_VALUE="${OUTPUT_TOPIC:-/pointpillars/detections}"
WORKSPACE="/workspace/ros2_ws"

source "/opt/ros/${ROS_DISTRO_VALUE}/setup.bash"
cd "${WORKSPACE}"

colcon build --symlink-install \
  --packages-select cuda_pointpillars_ros \
  --build-base "${WORKSPACE}/build" \
  --install-base "${WORKSPACE}/install" \
  --log-base "${WORKSPACE}/log" \
  --cmake-args \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CUDA_ARCHITECTURES="${CUDA_ARCH_VALUE}"

source "${WORKSPACE}/install/setup.bash"

LAUNCH_ARGS=(
  "input_cloud_topic:=${INPUT_TOPIC_VALUE}"
  "output_detections_topic:=${OUTPUT_TOPIC_VALUE}"
)

if [[ -n "${POINTPILLARS_MODEL_PATH:-}" ]]; then
  LAUNCH_ARGS+=("model_path:=${POINTPILLARS_MODEL_PATH}")
fi

exec ros2 launch cuda_pointpillars_ros pointpillars.launch.py "${LAUNCH_ARGS[@]}"
