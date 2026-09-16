#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "vision_msgs/msg/detection3_d_array.hpp"
#include "visualization_msgs/msg/marker.hpp"
#include "visualization_msgs/msg/marker_array.hpp"

class PointPillarsVisualizer : public rclcpp::Node
{
public:
  PointPillarsVisualizer()
  : Node("pointpillars_visualizer")
  {
    input_topic_ = declare_parameter<std::string>(
      "input_detections_topic", "/pointpillars/detections");
    output_topic_ = declare_parameter<std::string>(
      "output_markers_topic", "/pointpillars/markers");
    box_alpha_ = declare_parameter<double>("box_alpha", 0.25);
    text_scale_ = declare_parameter<double>("text_scale", 0.6);
    marker_lifetime_sec_ = declare_parameter<double>("marker_lifetime_sec", 0.5);
    show_labels_ = declare_parameter<bool>("show_labels", true);

    box_alpha_ = std::clamp(box_alpha_, 0.0, 1.0);
    text_scale_ = std::max(text_scale_, 0.01);
    marker_lifetime_sec_ = std::max(marker_lifetime_sec_, 0.0);

    publisher_ = create_publisher<visualization_msgs::msg::MarkerArray>(output_topic_, 10);
    subscription_ = create_subscription<vision_msgs::msg::Detection3DArray>(
      input_topic_, 10,
      std::bind(&PointPillarsVisualizer::callback, this, std::placeholders::_1));

    RCLCPP_INFO(get_logger(), "Detection input: %s", input_topic_.c_str());
    RCLCPP_INFO(get_logger(), "RViz marker output: %s", output_topic_.c_str());
  }

private:
  static void set_class_color(
    visualization_msgs::msg::Marker & marker, const std::string & class_id, double alpha)
  {
    marker.color.a = static_cast<float>(alpha);

    if (class_id == "Car") {
      marker.color.r = 0.15f;
      marker.color.g = 0.85f;
      marker.color.b = 0.25f;
    } else if (class_id == "Pedestrian") {
      marker.color.r = 0.95f;
      marker.color.g = 0.35f;
      marker.color.b = 0.10f;
    } else if (class_id == "Cyclist") {
      marker.color.r = 0.15f;
      marker.color.g = 0.55f;
      marker.color.b = 1.00f;
    } else {
      marker.color.r = 0.95f;
      marker.color.g = 0.85f;
      marker.color.b = 0.15f;
    }
  }

  void set_lifetime(visualization_msgs::msg::Marker & marker) const
  {
    if (marker_lifetime_sec_ <= 0.0) {
      marker.lifetime.sec = 0;
      marker.lifetime.nanosec = 0;
      return;
    }

    const auto total_nanoseconds = static_cast<int64_t>(marker_lifetime_sec_ * 1.0e9);
    marker.lifetime.sec = static_cast<int32_t>(total_nanoseconds / 1000000000LL);
    marker.lifetime.nanosec = static_cast<uint32_t>(total_nanoseconds % 1000000000LL);
  }

  void callback(const vision_msgs::msg::Detection3DArray::ConstSharedPtr msg)
  {
    visualization_msgs::msg::MarkerArray marker_array;
    marker_array.markers.reserve(1 + msg->detections.size() * (show_labels_ ? 2 : 1));

    visualization_msgs::msg::Marker clear_marker;
    clear_marker.header = msg->header;
    clear_marker.action = visualization_msgs::msg::Marker::DELETEALL;
    marker_array.markers.push_back(clear_marker);

    int marker_id = 0;
    for (const auto & detection : msg->detections) {
      std::string class_id = "unknown";
      double score = 0.0;
      if (!detection.results.empty()) {
        class_id = detection.results.front().hypothesis.class_id;
        score = detection.results.front().hypothesis.score;
      }

      visualization_msgs::msg::Marker box_marker;
      box_marker.header = msg->header;
      box_marker.ns = "pointpillars_boxes";
      box_marker.id = marker_id++;
      box_marker.type = visualization_msgs::msg::Marker::CUBE;
      box_marker.action = visualization_msgs::msg::Marker::ADD;
      box_marker.pose = detection.bbox.center;
      box_marker.scale.x = std::max(detection.bbox.size.x, 0.01);
      box_marker.scale.y = std::max(detection.bbox.size.y, 0.01);
      box_marker.scale.z = std::max(detection.bbox.size.z, 0.01);
      set_class_color(box_marker, class_id, box_alpha_);
      set_lifetime(box_marker);
      marker_array.markers.push_back(box_marker);

      if (!show_labels_) {
        continue;
      }

      visualization_msgs::msg::Marker text_marker;
      text_marker.header = msg->header;
      text_marker.ns = "pointpillars_labels";
      text_marker.id = marker_id++;
      text_marker.type = visualization_msgs::msg::Marker::TEXT_VIEW_FACING;
      text_marker.action = visualization_msgs::msg::Marker::ADD;
      text_marker.pose.position = detection.bbox.center.position;
      text_marker.pose.position.z += detection.bbox.size.z * 0.5 + 0.35;
      text_marker.pose.orientation.w = 1.0;
      text_marker.scale.z = text_scale_;
      text_marker.color.r = 1.0f;
      text_marker.color.g = 1.0f;
      text_marker.color.b = 1.0f;
      text_marker.color.a = 1.0f;

      std::ostringstream label;
      label << class_id << " " << std::fixed << std::setprecision(2) << score;
      text_marker.text = label.str();
      set_lifetime(text_marker);
      marker_array.markers.push_back(text_marker);
    }

    publisher_->publish(marker_array);
  }

  std::string input_topic_;
  std::string output_topic_;
  double box_alpha_ = 0.25;
  double text_scale_ = 0.6;
  double marker_lifetime_sec_ = 0.5;
  bool show_labels_ = true;

  rclcpp::Subscription<vision_msgs::msg::Detection3DArray>::SharedPtr subscription_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr publisher_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PointPillarsVisualizer>());
  rclcpp::shutdown();
  return 0;
}
