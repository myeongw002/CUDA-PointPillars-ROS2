#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "ament_index_cpp/get_package_share_directory.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "sensor_msgs/msg/point_field.hpp"
#include "vision_msgs/msg/detection3_d_array.hpp"

#include "pointpillar.h"

namespace
{

void check_cuda(cudaError_t status, const char * operation)
{
  if (status != cudaSuccess) {
    throw std::runtime_error(
            std::string(operation) + ": " + cudaGetErrorString(status));
  }
}

const sensor_msgs::msg::PointField * find_field(
  const sensor_msgs::msg::PointCloud2 & cloud, const std::string & name)
{
  for (const auto & field : cloud.fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

template<typename T>
T load_scalar(const uint8_t * data)
{
  T value{};
  std::memcpy(&value, data, sizeof(T));
  return value;
}

float read_numeric_field(const uint8_t * point, const sensor_msgs::msg::PointField & field)
{
  const auto * data = point + field.offset;
  using PF = sensor_msgs::msg::PointField;
  switch (field.datatype) {
    case PF::INT8:
      return static_cast<float>(load_scalar<int8_t>(data));
    case PF::UINT8:
      return static_cast<float>(load_scalar<uint8_t>(data));
    case PF::INT16:
      return static_cast<float>(load_scalar<int16_t>(data));
    case PF::UINT16:
      return static_cast<float>(load_scalar<uint16_t>(data));
    case PF::INT32:
      return static_cast<float>(load_scalar<int32_t>(data));
    case PF::UINT32:
      return static_cast<float>(load_scalar<uint32_t>(data));
    case PF::FLOAT32:
      return load_scalar<float>(data);
    case PF::FLOAT64:
      return static_cast<float>(load_scalar<double>(data));
    default:
      throw std::runtime_error("Unsupported PointCloud2 field datatype");
  }
}

bool is_git_lfs_pointer(const std::string & path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return false;
  }
  std::string first_line;
  std::getline(input, first_line);
  return first_line.rfind("version https://git-lfs.github.com/spec/v1", 0) == 0;
}

}  // namespace

class PointPillarsNode : public rclcpp::Node
{
public:
  PointPillarsNode()
  : Node("pointpillars_node")
  {
    input_cloud_topic_ = declare_parameter<std::string>("input_cloud_topic", "/point_cloud");
    output_topic_ = declare_parameter<std::string>(
      "output_detections_topic", "/pointpillars/detections");
    model_path_ = declare_parameter<std::string>("model_path", "");
    intensity_field_ = declare_parameter<std::string>("intensity_field", "intensity");
    intensity_scale_ = declare_parameter<double>("intensity_scale", 1.0);
    allow_missing_intensity_ = declare_parameter<bool>("allow_missing_intensity", false);
    score_threshold_ = declare_parameter<double>("score_threshold", 0.1);
    nms_iou_threshold_ = declare_parameter<double>("nms_iou_threshold", 0.01);
    class_aware_nms_ = declare_parameter<bool>("class_aware_nms", true);
    const int initial_capacity = declare_parameter<int>("initial_point_capacity", 250000);
    class_names_ = declare_parameter<std::vector<std::string>>(
      "class_names", {"Car", "Pedestrian", "Cyclist"});

    if (intensity_scale_ <= 0.0) {
      throw std::runtime_error("intensity_scale must be > 0");
    }
    if (initial_capacity <= 0) {
      throw std::runtime_error("initial_point_capacity must be > 0");
    }

    if (model_path_.empty()) {
      model_path_ =
        ament_index_cpp::get_package_share_directory("cuda_pointpillars_ros") +
        "/model/pointpillar.onnx";
    }

    if (!std::filesystem::exists(model_path_)) {
      throw std::runtime_error("PointPillars model not found: " + model_path_);
    }
    if (is_git_lfs_pointer(model_path_)) {
      throw std::runtime_error(
              "The ONNX file is still a Git LFS pointer. Run `git lfs pull` before starting the node.");
    }

    int device_count = 0;
    check_cuda(cudaGetDeviceCount(&device_count), "cudaGetDeviceCount");
    if (device_count < 1) {
      throw std::runtime_error("No CUDA-capable GPU is visible");
    }

    cudaDeviceProp device{};
    check_cuda(cudaGetDeviceProperties(&device, 0), "cudaGetDeviceProperties");
    RCLCPP_INFO(
      get_logger(), "Using GPU: %s (compute capability %d.%d)",
      device.name, device.major, device.minor);

    check_cuda(cudaStreamCreate(&stream_), "cudaStreamCreate");
    point_capacity_ = static_cast<std::size_t>(initial_capacity);
    check_cuda(
      cudaMalloc(reinterpret_cast<void **>(&device_points_), point_capacity_ * 4 * sizeof(float)),
      "cudaMalloc input buffer");

    pointpillar_ = std::make_unique<PointPillar>(model_path_, stream_);

    publisher_ = create_publisher<vision_msgs::msg::Detection3DArray>(output_topic_, 10);
    subscription_ = create_subscription<sensor_msgs::msg::PointCloud2>(
      input_cloud_topic_, rclcpp::SensorDataQoS(),
      std::bind(&PointPillarsNode::cloud_callback, this, std::placeholders::_1));

    RCLCPP_INFO(get_logger(), "PointPillars model: %s", model_path_.c_str());
    RCLCPP_INFO(get_logger(), "Input cloud: %s", input_cloud_topic_.c_str());
    RCLCPP_INFO(get_logger(), "Detection output: %s", output_topic_.c_str());
  }

  ~PointPillarsNode() override
  {
    pointpillar_.reset();
    if (device_points_ != nullptr) {
      cudaFree(device_points_);
      device_points_ = nullptr;
    }
    if (stream_ != nullptr) {
      cudaStreamDestroy(stream_);
      stream_ = nullptr;
    }
  }

private:
  bool validate_field(const sensor_msgs::msg::PointField * field, uint32_t point_step) const
  {
    if (field == nullptr) {
      return false;
    }

    std::size_t size = 0;
    using PF = sensor_msgs::msg::PointField;
    switch (field->datatype) {
      case PF::INT8:
      case PF::UINT8:
        size = 1;
        break;
      case PF::INT16:
      case PF::UINT16:
        size = 2;
        break;
      case PF::INT32:
      case PF::UINT32:
      case PF::FLOAT32:
        size = 4;
        break;
      case PF::FLOAT64:
        size = 8;
        break;
      default:
        return false;
    }
    return static_cast<std::size_t>(field->offset) + size <= point_step;
  }

  void ensure_point_capacity(std::size_t point_count)
  {
    if (point_count <= point_capacity_) {
      return;
    }

    check_cuda(cudaStreamSynchronize(stream_), "cudaStreamSynchronize before resize");
    check_cuda(cudaFree(device_points_), "cudaFree old input buffer");
    point_capacity_ = std::max(point_count, point_capacity_ * 2);
    check_cuda(
      cudaMalloc(reinterpret_cast<void **>(&device_points_), point_capacity_ * 4 * sizeof(float)),
      "cudaMalloc resized input buffer");
    RCLCPP_WARN(
      get_logger(), "Resized PointPillars input buffer to %zu points", point_capacity_);
  }

  void cloud_callback(const sensor_msgs::msg::PointCloud2::ConstSharedPtr msg)
  {
    const auto begin = std::chrono::steady_clock::now();

    if (msg->is_bigendian) {
      RCLCPP_ERROR(get_logger(), "Big-endian PointCloud2 is not supported");
      return;
    }

    const auto * x_field = find_field(*msg, "x");
    const auto * y_field = find_field(*msg, "y");
    const auto * z_field = find_field(*msg, "z");
    const auto * intensity_field = find_field(*msg, intensity_field_);

    if (!validate_field(x_field, msg->point_step) ||
      !validate_field(y_field, msg->point_step) ||
      !validate_field(z_field, msg->point_step))
    {
      RCLCPP_ERROR(get_logger(), "Input PointCloud2 must contain numeric x, y and z fields");
      return;
    }

    if (!validate_field(intensity_field, msg->point_step) && !allow_missing_intensity_) {
      RCLCPP_ERROR(
        get_logger(), "Input PointCloud2 has no usable '%s' field. Set allow_missing_intensity=true to use zero intensity.",
        intensity_field_.c_str());
      return;
    }

    packed_points_.clear();
    packed_points_.reserve(
      static_cast<std::size_t>(msg->width) * static_cast<std::size_t>(msg->height) * 4);

    try {
      for (uint32_t row = 0; row < msg->height; ++row) {
        const std::size_t row_offset = static_cast<std::size_t>(row) * msg->row_step;
        for (uint32_t col = 0; col < msg->width; ++col) {
          const std::size_t point_offset = row_offset + static_cast<std::size_t>(col) * msg->point_step;
          if (point_offset + msg->point_step > msg->data.size()) {
            continue;
          }

          const uint8_t * point = msg->data.data() + point_offset;
          const float x = read_numeric_field(point, *x_field);
          const float y = read_numeric_field(point, *y_field);
          const float z = read_numeric_field(point, *z_field);
          if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            continue;
          }

          float intensity = 0.0f;
          if (validate_field(intensity_field, msg->point_step)) {
            intensity = read_numeric_field(point, *intensity_field);
            if (!std::isfinite(intensity)) {
              intensity = 0.0f;
            }
          }

          packed_points_.push_back(x);
          packed_points_.push_back(y);
          packed_points_.push_back(z);
          packed_points_.push_back(static_cast<float>(intensity / intensity_scale_));
        }
      }
    } catch (const std::exception & error) {
      RCLCPP_ERROR(get_logger(), "PointCloud2 parsing failed: %s", error.what());
      return;
    }

    const std::size_t point_count = packed_points_.size() / 4;
    auto output = vision_msgs::msg::Detection3DArray();
    output.header = msg->header;

    if (point_count == 0) {
      publisher_->publish(output);
      return;
    }

    try {
      ensure_point_capacity(point_count);
      check_cuda(
        cudaMemcpyAsync(
          device_points_, packed_points_.data(), packed_points_.size() * sizeof(float),
          cudaMemcpyHostToDevice, stream_),
        "cudaMemcpyAsync input cloud");

      std::vector<Bndbox> boxes;
      boxes.reserve(128);
      if (pointpillar_->doinfer(
          device_points_, static_cast<unsigned int>(point_count), boxes,
          static_cast<float>(score_threshold_), static_cast<float>(nms_iou_threshold_),
          class_aware_nms_) != 0)
      {
        RCLCPP_ERROR(get_logger(), "TensorRT inference failed");
        return;
      }

      output.detections.reserve(boxes.size());
      for (const auto & box : boxes) {
        vision_msgs::msg::Detection3D detection;
        detection.header = msg->header;
        detection.bbox.center.position.x = box.x;
        detection.bbox.center.position.y = box.y;
        detection.bbox.center.position.z = box.z;
        detection.bbox.size.x = box.w;
        detection.bbox.size.y = box.l;
        detection.bbox.size.z = box.h;
        detection.bbox.center.orientation.x = 0.0;
        detection.bbox.center.orientation.y = 0.0;
        detection.bbox.center.orientation.z = std::sin(box.rt * 0.5f);
        detection.bbox.center.orientation.w = std::cos(box.rt * 0.5f);

        vision_msgs::msg::ObjectHypothesisWithPose result;
        if (box.id >= 0 && static_cast<std::size_t>(box.id) < class_names_.size()) {
          result.hypothesis.class_id = class_names_[static_cast<std::size_t>(box.id)];
        } else {
          result.hypothesis.class_id = std::to_string(box.id);
        }
        result.hypothesis.score = box.score;
        detection.results.push_back(result);
        output.detections.push_back(detection);
      }

      publisher_->publish(output);

      const auto end = std::chrono::steady_clock::now();
      const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - begin).count();
      RCLCPP_DEBUG(
        get_logger(), "PointPillars: %zu input points, %zu detections, %.2f ms",
        point_count, boxes.size(), elapsed_ms);
    } catch (const std::exception & error) {
      RCLCPP_ERROR(get_logger(), "PointPillars callback failed: %s", error.what());
    }
  }

  std::string input_cloud_topic_;
  std::string output_topic_;
  std::string model_path_;
  std::string intensity_field_;
  double intensity_scale_ = 1.0;
  bool allow_missing_intensity_ = false;
  double score_threshold_ = 0.1;
  double nms_iou_threshold_ = 0.01;
  bool class_aware_nms_ = true;
  std::vector<std::string> class_names_;

  cudaStream_t stream_ = nullptr;
  float * device_points_ = nullptr;
  std::size_t point_capacity_ = 0;
  std::vector<float> packed_points_;
  std::unique_ptr<PointPillar> pointpillar_;

  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr subscription_;
  rclcpp::Publisher<vision_msgs::msg::Detection3DArray>::SharedPtr publisher_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<PointPillarsNode>());
  } catch (const std::exception & error) {
    RCLCPP_FATAL(rclcpp::get_logger("pointpillars_node"), "%s", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
