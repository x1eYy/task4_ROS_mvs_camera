#pragma once

#include "hikrobot_mvs_camera/camera_backend.hpp"

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/float64.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace hikrobot_mvs_camera {

class CameraNode final : public rclcpp::Node {
public:
  explicit CameraNode(std::unique_ptr<CameraBackend> backend,
                      const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  ~CameraNode() override;

private:
  struct Settings {
    double exposure = -1.0;
    double gain = -1.0;
    double frame_rate = -1.0;
    std::string pixel_format;
  };

  bool apply_settings(std::string& error);
  void run();
  rcl_interfaces::msg::SetParametersResult on_parameters(
    const std::vector<rclcpp::Parameter>& parameters);

  std::unique_ptr<CameraBackend> backend_;
  Selector selector_;
  Settings settings_;
  std::string frame_id_;
  std::mutex camera_mutex_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> refresh_readback_{false};
  std::thread worker_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_publisher_;
  rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr fps_publisher_;
  OnSetParametersCallbackHandle::SharedPtr parameter_callback_;
};

}  // namespace hikrobot_mvs_camera
