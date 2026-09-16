FROM nvcr.io/nvidia/tensorrt:23.08-py3

ARG ROS_DISTRO=humble
ARG CUDA_ARCH=86

ENV DEBIAN_FRONTEND=noninteractive
ENV ROS_DISTRO=${ROS_DISTRO}
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

SHELL ["/bin/bash", "-c"]

# Install only Ubuntu-native bootstrap packages first.  ROS development
# packages such as rosdep/colcon are installed after the ROS 2 repository is
# registered below.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      gnupg2 \
      locales \
      software-properties-common \
      git \
      git-lfs \
 && add-apt-repository universe -y \
 && locale-gen en_US.UTF-8 \
 && rm -rf /var/lib/apt/lists/*

# Register the ROS 2 Jammy repository.  The key published at ros.key is ASCII
# armored, so convert it to a binary keyring instead of saving it directly
# with a .gpg suffix.
RUN mkdir -p /usr/share/keyrings \
 && curl -fsSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
      | gpg --dearmor -o /usr/share/keyrings/ros-archive-keyring.gpg \
 && echo "deb [arch=amd64 signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu jammy main" \
      > /etc/apt/sources.list.d/ros2.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      ros-${ROS_DISTRO}-ros-base \
      ros-${ROS_DISTRO}-ament-index-cpp \
      ros-${ROS_DISTRO}-sensor-msgs \
      ros-${ROS_DISTRO}-vision-msgs \
      ros-${ROS_DISTRO}-geometry-msgs \
      python3-rosdep \
      python3-colcon-common-extensions \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/ros2_ws
COPY . /workspace/ros2_ws/src/CUDA-PointPillars-ROS2

# Build output stays inside the image (/workspace/ros2_ws/{build,install,log});
# it is not shared with a host colcon workspace.
RUN source /opt/ros/${ROS_DISTRO}/setup.bash \
 && colcon build --symlink-install \
      --packages-select cuda_pointpillars_ros \
      --cmake-args -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=${CUDA_ARCH}

COPY docker/entrypoint.sh /pointpillars_entrypoint.sh
RUN chmod +x /pointpillars_entrypoint.sh

ENTRYPOINT ["/pointpillars_entrypoint.sh"]
CMD ["ros2", "launch", "cuda_pointpillars_ros", "pointpillars.launch.py"]
