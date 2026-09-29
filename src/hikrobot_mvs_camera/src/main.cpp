#include "hikrobot_mvs_camera/camera_node.hpp"

#include <exception>
#include <memory>

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  try {
    auto node = std::make_shared<hikrobot_mvs_camera::CameraNode>(
      hikrobot_mvs_camera::make_mvs_camera());
    rclcpp::spin(node);
  } catch (const std::exception& error) {
    RCLCPP_FATAL(rclcpp::get_logger("hikrobot_mvs_camera"), "%s", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
