#!/usr/bin/env bash
set -eo pipefail

ROS_DISTRO_VALUE="${ROS_DISTRO:-humble}"
CUDA_ARCH_VALUE="${CUDA_ARCH:-120}"
WORKSPACE="/workspace/ros2_ws"

# ROS 2 setup scripts are not guaranteed to be compatible with `set -u`
# (nounset). Keep errexit/pipefail enabled, but do not enable nounset here.
source "/opt/ros/${ROS_DISTRO_VALUE}/setup.bash"
cd "${WORKSPACE}"

# --log-base is a global colcon option, so it must appear before the `build`
# subcommand. --build-base and --install-base are build-subcommand options.
colcon --log-base "${WORKSPACE}/log" build --symlink-install \
  --packages-select cuda_pointpillars_ros \
  --build-base "${WORKSPACE}/build" \
  --install-base "${WORKSPACE}/install" \
  --cmake-args \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CUDA_ARCHITECTURES="${CUDA_ARCH_VALUE}"

source "${WORKSPACE}/install/setup.bash"

LAUNCH_ARGS=()

# Only override YAML values when the caller explicitly supplied an override.
if [[ -n "${INPUT_TOPIC:-}" ]]; then
  LAUNCH_ARGS+=("input_cloud_topic:=${INPUT_TOPIC}")
fi
if [[ -n "${OUTPUT_TOPIC:-}" ]]; then
  LAUNCH_ARGS+=("output_detections_topic:=${OUTPUT_TOPIC}")
fi
if [[ -n "${POINTPILLARS_MODEL_PATH:-}" ]]; then
  LAUNCH_ARGS+=("model_path:=${POINTPILLARS_MODEL_PATH}")
fi

exec ros2 launch cuda_pointpillars_ros pointpillars.launch.py "${LAUNCH_ARGS[@]}"
