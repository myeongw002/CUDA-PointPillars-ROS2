from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _launch_nodes(context):
    config_file = LaunchConfiguration("config_file").perform(context)
    model_path = LaunchConfiguration("model_path").perform(context)
    input_cloud_topic = LaunchConfiguration("input_cloud_topic").perform(context)
    output_detections_topic = LaunchConfiguration(
        "output_detections_topic"
    ).perform(context)
    output_markers_topic = LaunchConfiguration("output_markers_topic").perform(context)
    enable_visualizer = LaunchConfiguration("enable_visualizer").perform(context)

    pointpillars_parameters = [config_file]
    pointpillars_overrides = {}

    if model_path:
        pointpillars_overrides["model_path"] = model_path
    if input_cloud_topic:
        pointpillars_overrides["input_cloud_topic"] = input_cloud_topic
    if output_detections_topic:
        pointpillars_overrides["output_detections_topic"] = output_detections_topic

    if pointpillars_overrides:
        pointpillars_parameters.append(pointpillars_overrides)

    nodes = [
        Node(
            package="cuda_pointpillars_ros",
            executable="pointpillars_node",
            name="pointpillars_node",
            output="screen",
            parameters=pointpillars_parameters,
        )
    ]

    if enable_visualizer.lower() in ("1", "true", "yes", "on"):
        visualizer_parameters = [config_file]
        visualizer_overrides = {}

        # Keep the visualizer subscribed to the actual detector output when
        # that output topic is overridden at launch time.
        if output_detections_topic:
            visualizer_overrides["input_detections_topic"] = output_detections_topic
        if output_markers_topic:
            visualizer_overrides["output_markers_topic"] = output_markers_topic

        if visualizer_overrides:
            visualizer_parameters.append(visualizer_overrides)

        nodes.append(
            Node(
                package="cuda_pointpillars_ros",
                executable="pointpillars_visualizer",
                name="pointpillars_visualizer",
                output="screen",
                parameters=visualizer_parameters,
            )
        )

    return nodes


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
        DeclareLaunchArgument("output_markers_topic", default_value=""),
        DeclareLaunchArgument("enable_visualizer", default_value="true"),
        OpaqueFunction(function=_launch_nodes),
    ])
