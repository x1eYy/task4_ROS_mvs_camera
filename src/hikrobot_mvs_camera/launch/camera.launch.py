from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import EnvironmentVariable, LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    arguments = [
        DeclareLaunchArgument('serial_number', default_value=''),
        DeclareLaunchArgument('mvs_lib_dir', default_value='/opt/MVS/lib/64'),
        DeclareLaunchArgument('image_topic', default_value='/image_raw'),
        DeclareLaunchArgument('frame_id', default_value='camera_optical_frame'),
        DeclareLaunchArgument('exposure_time_us', default_value='-1.0'),
        DeclareLaunchArgument('gain_db', default_value='-1.0'),
        DeclareLaunchArgument('frame_rate_hz', default_value='-1.0'),
        DeclareLaunchArgument('pixel_format', default_value=''),
    ]
    node = Node(
        package='hikrobot_mvs_camera',
        executable='camera_node',
        name='hikrobot_mvs_camera',
        output='screen',
        additional_env={
            'LD_LIBRARY_PATH': [
                LaunchConfiguration('mvs_lib_dir'), ':',
                EnvironmentVariable('LD_LIBRARY_PATH', default_value=''),
            ],
        },
        parameters=[{
            'serial_number': LaunchConfiguration('serial_number'),
            'image_topic': LaunchConfiguration('image_topic'),
            'frame_id': LaunchConfiguration('frame_id'),
            'exposure_time_us': LaunchConfiguration('exposure_time_us'),
            'gain_db': LaunchConfiguration('gain_db'),
            'frame_rate_hz': LaunchConfiguration('frame_rate_hz'),
            'pixel_format': LaunchConfiguration('pixel_format'),
        }],
    )
    return LaunchDescription(arguments + [node])
