from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    package_share = Path(get_package_share_directory("cuda_pointpillars_ros"))
    default_config = str(package_share / "config" / "pointpillars.yaml")

    return LaunchDescription([
        DeclareLaunchArgument("config_file", default_value=default_config),
        DeclareLaunchArgument("model_path", default_value=""),
        DeclareLaunchArgument("input_cloud_topic", default_value="/point_cloud"),
        DeclareLaunchArgument(
            "output_detections_topic", default_value="/pointpillars/detections"
        ),
        Node(
            package="cuda_pointpillars_ros",
            executable="pointpillars_node",
            name="pointpillars_node",
            output="screen",
            parameters=[
                LaunchConfiguration("config_file"),
                {
                    "model_path": LaunchConfiguration("model_path"),
                    "input_cloud_topic": LaunchConfiguration("input_cloud_topic"),
                    "output_detections_topic": LaunchConfiguration(
                        "output_detections_topic"
                    ),
                },
            ],
        ),
    ])
