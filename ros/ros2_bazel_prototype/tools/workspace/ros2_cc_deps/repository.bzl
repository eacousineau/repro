load("//tools/skylark/cmake:cmake_cc.bzl", "cmake_cc_repository")

_ros = "/opt/ros/crystal"
_ros_pylib = _ros + "/lib/python3.6/site-packages"


def ros2_cc_deps_repository(name, hack_workspace_dir):
    # For hacking.
    hack_overlay = hack_workspace_dir + "/external/overlay_ws/install"
    workspaces = [
        hack_overlay,
        _ros,
    ]
    cmake_cc_repository(
        name = name,
        packages = [
            "rclcpp",
            "std_msgs",
            "rmw_fastrtps_cpp",  # Specify RMW implementation.
        ],
        cache_entries = dict(
            CMAKE_PREFIX_PATH = ";".join(workspaces),
        ),
        env_vars = dict(
            PYTHONPATH = _ros_pylib,
        ),
        libdir_order_preference = workspaces,
    )