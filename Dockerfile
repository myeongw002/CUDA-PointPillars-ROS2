FROM nvidia/cuda:12.8.1-devel-ubuntu22.04

ARG ROS_DISTRO=humble
ARG CUDA_ARCH=120
ARG TENSORRT_VERSION=10.8.0.43-1+cuda12.8

ENV DEBIAN_FRONTEND=noninteractive
ENV ROS_DISTRO=${ROS_DISTRO}
ENV CUDA_ARCH=${CUDA_ARCH}
ENV TENSORRT_VERSION=${TENSORRT_VERSION}
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

SHELL ["/bin/bash", "-c"]

# Bootstrap packages. The CUDA base image already configures NVIDIA's CUDA
# network repository, which also exposes TensorRT Debian packages.
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

# Pin TensorRT 10.8 built for CUDA 12.8. TensorRT 10.8 is the first release
# with Blackwell support, and this branch intentionally stays on TensorRT 10.x.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libnvinfer10="${TENSORRT_VERSION}" \
      libnvinfer-dev="${TENSORRT_VERSION}" \
      libnvinfer-plugin10="${TENSORRT_VERSION}" \
      libnvinfer-plugin-dev="${TENSORRT_VERSION}" \
      libnvonnxparsers10="${TENSORRT_VERSION}" \
      libnvonnxparsers-dev="${TENSORRT_VERSION}" \
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

WORKDIR /workspace/ros2_ws

COPY docker/entrypoint.sh /pointpillars_entrypoint.sh
RUN chmod +x /pointpillars_entrypoint.sh

ENTRYPOINT ["/pointpillars_entrypoint.sh"]
CMD ["bash"]
