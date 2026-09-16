from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _launch_node(context):
    config_file = LaunchConfiguration("config_file").perform(context)
    model_path = LaunchConfiguration("model_path").perform(context)
    input_cloud_topic = LaunchConfiguration("input_cloud_topic").perform(context)
    output_detections_topic = LaunchConfiguration(
        "output_detections_topic"
    ).perform(context)

    parameters = [config_file]
    overrides = {}

    if model_path:
        overrides["model_path"] = model_path
    if input_cloud_topic:
        overrides["input_cloud_topic"] = input_cloud_topic
    if output_detections_topic:
        overrides["output_detections_topic"] = output_detections_topic

    if overrides:
        parameters.append(overrides)

    return [
        Node(
            package="cuda_pointpillars_ros",
            executable="pointpillars_node",
            name="pointpillars_node",
            output="screen",
            parameters=parameters,
        )
    ]


def generate_launch_description():
    package_share = Path(get_package_share_directory("cuda_pointpillars_ros"))
    default_config = str(package_share / "config" / "pointpillars.yaml")

    return LaunchDescription([
        DeclareLaunchArgument("config_file", default_value=default_config),
        # Empty launch arguments mean "use the YAML value". They are only
        # applied when explicitly supplied by the caller.
        DeclareLaunchArgument("model_path", default_value=""),
        DeclareLaunchArgument("input_cloud_topic", default_value=""),
        DeclareLaunchArgument("output_detections_topic", default_value=""),
        OpaqueFunction(function=_launch_node),
    ])
