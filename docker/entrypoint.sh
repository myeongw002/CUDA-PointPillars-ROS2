#!/usr/bin/env bash
set -e

source "/opt/ros/${ROS_DISTRO:-humble}/setup.bash"
source /workspace/ros2_ws/install/setup.bash

exec "$@"
