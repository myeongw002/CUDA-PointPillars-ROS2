FROM nvcr.io/nvidia/tensorrt:23.08-py3

ARG ROS_DISTRO=humble
ARG CUDA_ARCH=86

ENV DEBIAN_FRONTEND=noninteractive
ENV ROS_DISTRO=${ROS_DISTRO}
ENV CUDA_ARCH=${CUDA_ARCH}
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

SHELL ["/bin/bash", "-c"]

# Install Ubuntu-native bootstrap packages first. ROS development packages are
# installed after the ROS 2 repository is registered below.
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

# Register ROS 2 Humble packages for Ubuntu 22.04 (Jammy).
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
      ros-${ROS_DISTRO}-visualization-msgs \
      python3-rosdep \
      python3-colcon-common-extensions \
 && rm -rf /var/lib/apt/lists/*

# The repository itself is intentionally NOT copied into the image. At run
# time docker/run.sh bind-mounts the host checkout into src/, while build,
# install and log are kept in Docker named volumes.
WORKDIR /workspace/ros2_ws

COPY docker/entrypoint.sh /pointpillars_entrypoint.sh
RUN chmod +x /pointpillars_entrypoint.sh

ENTRYPOINT ["/pointpillars_entrypoint.sh"]
CMD ["bash"]
