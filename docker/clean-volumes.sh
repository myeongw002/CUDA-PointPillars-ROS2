#!/usr/bin/env bash
set -euo pipefail

BUILD_VOLUME="${BUILD_VOLUME:-pointpillars_ros2_build}"
INSTALL_VOLUME="${INSTALL_VOLUME:-pointpillars_ros2_install}"
LOG_VOLUME="${LOG_VOLUME:-pointpillars_ros2_log}"

docker volume rm -f \
  "${BUILD_VOLUME}" \
  "${INSTALL_VOLUME}" \
  "${LOG_VOLUME}"
