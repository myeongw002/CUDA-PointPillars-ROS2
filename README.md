# CUDA PointPillars ROS 2

ROS 2 Humble wrapper for NVIDIA CUDA-PointPillars / TensorRT inference.

This fork is being adapted for streaming `sensor_msgs/msg/PointCloud2` input, persistent TensorRT inference, and object-level camera-LiDAR fusion experiments.

## Current interface

Input:

- `sensor_msgs/msg/PointCloud2`
- numeric `x`, `y`, `z` fields
- an intensity field (`intensity` by default)

Output:

- `vision_msgs/msg/Detection3DArray`
- class ID and score
- 3D box center, dimensions, and yaw

Default topics:

```text
/point_cloud
    ↓
PointPillars
    ↓
/pointpillars/detections
```

The input and output topic names are ROS parameters and can be changed without recompiling.

## Important model note

The bundled model is managed by Git LFS. After cloning, run:

```bash
git lfs install
git lfs pull
```

If `model/pointpillar.onnx` is still an LFS pointer, the node will stop with an explicit error instead of passing the pointer file to TensorRT.

The values in `include/params.h` (point-cloud range, voxel size, anchors, class count, feature-map dimensions) must match the OpenPCDet model used to generate the ONNX file.

## Docker environment

The provided Docker image uses NVIDIA TensorRT `23.08-py3` as its base and installs ROS 2 Humble inside the container. The base image provides Ubuntu 22.04, CUDA 12.2.x, and TensorRT 8.6.x.

### Host requirements

- Linux with an NVIDIA GPU
- NVIDIA driver compatible with the container CUDA version
- Docker
- NVIDIA Container Toolkit

Verify GPU access first:

```bash
docker run --rm --gpus all nvcr.io/nvidia/tensorrt:23.08-py3 nvidia-smi
```

### Build

For an RTX 30-series GPU (SM 8.6):

```bash
./docker/build.sh
```

Equivalent command:

```bash
docker build \
  --build-arg CUDA_ARCH=86 \
  -t cuda-pointpillars-ros2:humble-trt8.6 \
  .
```

To build for another GPU, override `CUDA_ARCH`.

### Run

The container uses host networking so ROS 2 DDS traffic can be shared with ROS 2 nodes running directly on the host.

```bash
INPUT_TOPIC=/point_cloud ./docker/run.sh
```

Example with another PointCloud2 topic:

```bash
INPUT_TOPIC=/my_point_cloud ./docker/run.sh
```

The output remains:

```text
/pointpillars/detections
```

To use a different ONNX model without rebuilding the image:

```bash
MODEL_PATH=/absolute/path/to/pointpillar.onnx \
INPUT_TOPIC=/point_cloud \
./docker/run.sh
```

The model directory is mounted writable because TensorRT stores its serialized engine cache next to the ONNX file as `<model>.cache`. Delete that cache after changing the ONNX model, TensorRT version, or GPU architecture.

If the host uses a non-zero ROS domain ID:

```bash
ROS_DOMAIN_ID=10 ./docker/run.sh
```

## Native ROS 2 build

A native build is also possible when CUDA and TensorRT are already installed on the host.

```bash
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws/src
git clone https://github.com/myeongw002/CUDA-PointPillars-ROS2.git
cd CUDA-PointPillars-ROS2
git lfs pull

cd ~/ros2_ws
source /opt/ros/humble/setup.bash
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install \
  --packages-select cuda_pointpillars_ros \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=86
source install/setup.bash
```

Run:

```bash
ros2 launch cuda_pointpillars_ros pointpillars.launch.py \
  input_cloud_topic:=/point_cloud
```

## Parameters

Default values are in `config/pointpillars.yaml`.

```yaml
input_cloud_topic: /point_cloud
output_detections_topic: /pointpillars/detections
model_path: ""
intensity_field: intensity
intensity_scale: 1.0
allow_missing_intensity: false
score_threshold: 0.1
nms_iou_threshold: 0.01
class_aware_nms: true
initial_point_capacity: 250000
class_names: [Car, Pedestrian, Cyclist]
```

`model_path: ""` selects the model installed with this package.

`intensity_scale` should match the preprocessing used during model training. For example, set it to `255.0` only if the incoming intensity is in approximately `[0, 255]` while the training pipeline used normalized intensity.

## Changes in this fork

Compared with the original ROS 2 wrapper, this fork currently:

- targets ROS 2 Humble / Ubuntu 22.04;
- keeps the CUDA stream, TensorRT engine, and PointPillars buffers alive across frames instead of rebuilding them in every subscription callback;
- parses `PointCloud2` by field metadata instead of assuming a fixed 16-byte XYZI memory layout;
- handles organized clouds and row padding;
- removes the hard-coded local model path;
- uses ROS parameters for topics, model path, thresholds, intensity handling, and class names;
- uses `rclcpp::SensorDataQoS()` for PointCloud2 subscription;
- publishes `vision_msgs/msg/Detection3DArray`;
- enables FP16 TensorRT engine building when the GPU supports fast FP16;
- uses class-aware NMS by default;
- guards zero-overlap cases in the rotated-box intersection code;
- provides an RTX 30-series CUDA architecture default (`SM 86`) that can be overridden at build time;
- provides a Docker environment that isolates ROS 2 / CUDA / TensorRT dependencies from the host system.

## Camera-LiDAR fusion roadmap

The current ROS output is post-NMS `Detection3DArray`, which is suitable for the first object-level fusion implementation:

```text
2D detector
    +
PointPillars Detection3DArray
    ↓
3D-box projection
    ↓
2D/3D association
    ↓
fused object
```

A later step can expose pre-NMS PointPillars candidates so the camera evidence can rescore candidates before final NMS, following a CLOCs-style late-fusion design.

## Upstream

This repository is based on:

- NVIDIA-AI-IOT/CUDA-PointPillars
- cdefg/CUDA-PointPillars-ROS2
- OpenPCDet

## License

Apache-2.0. See `LICENSE` and `NOTICE`.
