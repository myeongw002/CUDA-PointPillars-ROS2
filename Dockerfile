FROM nvcr.io/nvidia/tensorrt:23.08-py3

ARG ROS_DISTRO=humble
ARG CUDA_ARCH=86

ENV DEBIAN_FRONTEND=noninteractive
ENV ROS_DISTRO=${ROS_DISTRO}
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

SHELL ["/bin/bash", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    curl \
    gnupg2 \
    locales \
    software-properties-common \
    ca-certificates \
    git \
    git-lfs \
    python3-pip \
    python3-rosdep \
    python3-colcon-common-extensions \
 && locale-gen en_US.UTF-8 \
 && rm -rf /var/lib/apt/lists/*

RUN curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
      -o /usr/share/keyrings/ros-archive-keyring.gpg \
 && echo "deb [arch=amd64 signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu jammy main" \
      > /etc/apt/sources.list.d/ros2.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      ros-${ROS_DISTRO}-ros-base \
      ros-${ROS_DISTRO}-ament-index-cpp \
      ros-${ROS_DISTRO}-sensor-msgs \
      ros-${ROS_DISTRO}-vision-msgs \
      ros-${ROS_DISTRO}-geometry-msgs \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/ros2_ws
COPY . /workspace/ros2_ws/src/CUDA-PointPillars-ROS2

RUN source /opt/ros/${ROS_DISTRO}/setup.bash \
 && colcon build --symlink-install \
      --packages-select cuda_pointpillars_ros \
      --cmake-args -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=${CUDA_ARCH}

COPY docker/entrypoint.sh /pointpillars_entrypoint.sh
RUN chmod +x /pointpillars_entrypoint.sh

ENTRYPOINT ["/pointpillars_entrypoint.sh"]
CMD ["ros2", "launch", "cuda_pointpillars_ros", "pointpillars.launch.py"]
