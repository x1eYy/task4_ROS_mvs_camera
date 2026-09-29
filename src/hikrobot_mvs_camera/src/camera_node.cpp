#include "hikrobot_mvs_camera/camera_node.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace hikrobot_mvs_camera {

CameraNode::CameraNode(std::unique_ptr<CameraBackend> backend, const rclcpp::NodeOptions& options)
: rclcpp::Node("hikrobot_mvs_camera", options), backend_(std::move(backend)) {
  if (!backend_) throw std::invalid_argument("camera backend is required");
  selector_.serial_number = declare_parameter<std::string>("serial_number", "");
  selector_.camera_ip = declare_parameter<std::string>("camera_ip", "");
  frame_id_ = declare_parameter<std::string>("frame_id", "camera_optical_frame");
  const auto image_topic = declare_parameter<std::string>("image_topic", "/image_raw");
  settings_.exposure = declare_parameter<double>("exposure_time_us", -1.0);
  settings_.gain = declare_parameter<double>("gain_db", -1.0);
  settings_.frame_rate = declare_parameter<double>("frame_rate_hz", -1.0);
  settings_.pixel_format = declare_parameter<std::string>("pixel_format", "");
  if (!selector_.serial_number.empty() && !selector_.camera_ip.empty()) {
    throw std::invalid_argument("set either serial_number or camera_ip, not both");
  }
  if (image_topic.empty() || frame_id_.empty()) {
    throw std::invalid_argument("image_topic and frame_id must not be empty");
  }
  if (!std::isfinite(settings_.exposure) ||
      (settings_.exposure != -1.0 && settings_.exposure <= 0.0) ||
      !std::isfinite(settings_.gain) ||
      (settings_.gain != -1.0 && settings_.gain < 0.0) ||
      !std::isfinite(settings_.frame_rate) ||
      (settings_.frame_rate != -1.0 && settings_.frame_rate <= 0.0)) {
    throw std::invalid_argument("invalid initial camera setting");
  }
  image_publisher_ = create_publisher<sensor_msgs::msg::Image>(image_topic, rclcpp::SensorDataQoS());
  fps_publisher_ = create_publisher<std_msgs::msg::Float64>("~/actual_fps", 10);
  parameter_callback_ = add_on_set_parameters_callback(
    [this](const std::vector<rclcpp::Parameter>& params) { return on_parameters(params); });
  worker_ = std::thread([this] { run(); });
}

CameraNode::~CameraNode() {
  stopping_.store(true);
  if (worker_.joinable()) worker_.join();
  std::lock_guard<std::mutex> lock(camera_mutex_);
  backend_->disconnect();
}

bool CameraNode::apply_settings(std::string& error) {
  if (settings_.exposure >= 0 &&
      !backend_->set_float(Feature::Exposure, settings_.exposure, error)) return false;
  if (settings_.gain >= 0 &&
      !backend_->set_float(Feature::Gain, settings_.gain, error)) return false;
  if (settings_.frame_rate >= 0 &&
      !backend_->set_float(Feature::FrameRate, settings_.frame_rate, error)) return false;
  if (!settings_.pixel_format.empty() &&
      !backend_->set_pixel_format(settings_.pixel_format, error)) return false;
  return true;
}

rcl_interfaces::msg::SetParametersResult CameraNode::on_parameters(
  const std::vector<rclcpp::Parameter>& params) {
  rcl_interfaces::msg::SetParametersResult response;
  response.successful = true;
  unsigned int camera_updates = 0;
  for (const auto& param : params) {
    const auto& name = param.get_name();
    if (name == "serial_number" || name == "camera_ip" || name == "image_topic" ||
        name == "frame_id") {
      response.successful = false;
      response.reason = name + " is fixed after startup";
      return response;
    }
    if (name == "exposure_time_us" || name == "gain_db" || name == "frame_rate_hz" ||
        name == "pixel_format") ++camera_updates;
  }
  if (camera_updates > 1) {
    response.successful = false;
    response.reason = "set one camera parameter per request";
    return response;
  }
  std::lock_guard<std::mutex> lock(camera_mutex_);
  for (const auto& param : params) {
    Feature feature;
    double* slot = nullptr;
    const auto& name = param.get_name();
    if (name == "exposure_time_us") { feature = Feature::Exposure; slot = &settings_.exposure; }
    else if (name == "gain_db") { feature = Feature::Gain; slot = &settings_.gain; }
    else if (name == "frame_rate_hz") { feature = Feature::FrameRate; slot = &settings_.frame_rate; }
    if (slot) {
      if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE) {
        response.successful = false; response.reason = name + " must be a double"; return response;
      }
      const double value = param.as_double();
      if (!std::isfinite(value) || (name == "gain_db" ? value < 0.0 : value <= 0.0)) {
        response.successful = false;
        response.reason = name + (name == "gain_db" ? " must be nonnegative" : " must be positive");
        return response;
      }
      if (!backend_->connected()) {
        response.successful = false; response.reason = "camera is not connected"; return response;
      }
      if (!backend_->set_float(feature, value, response.reason)) {
        response.successful = false; return response;
      }
      *slot = value;
    } else if (name == "pixel_format") {
      if (param.get_type() != rclcpp::ParameterType::PARAMETER_STRING || param.as_string().empty()) {
        response.successful = false; response.reason = "pixel_format must be a non-empty string";
        return response;
      }
      if (!backend_->connected()) {
        response.successful = false; response.reason = "camera is not connected"; return response;
      }
      if (!backend_->set_pixel_format(param.as_string(), response.reason)) {
        response.successful = false; return response;
      }
      settings_.pixel_format = param.as_string();
    }
  }
  return response;
}

void CameraNode::run() {
  using clock = std::chrono::steady_clock;
  auto window_start = clock::now();
  uint64_t frames_in_window = 0;
  uint64_t lost_packets = 0;
  unsigned int consecutive_errors = 0;
  while (!stopping_.load() && rclcpp::ok()) {
    Frame frame;
    std::string error;
    GrabResult result = GrabResult::Timeout;
    bool connected_now = false;
    {
      std::lock_guard<std::mutex> lock(camera_mutex_);
      if (!backend_->connected()) {
        backend_->disconnect();
        if (backend_->connect(selector_, error)) {
          if (apply_settings(error)) {
            connected_now = true;
          } else {
            backend_->disconnect();
          }
        }
      } else {
        result = backend_->grab(frame, error);
        if (result == GrabResult::Disconnected) backend_->disconnect();
      }
    }
    if (connected_now) {
      RCLCPP_INFO(get_logger(), "connected to %s", backend_->device_label().c_str());
      window_start = clock::now();
      frames_in_window = 0;
      lost_packets = 0;
      consecutive_errors = 0;
      continue;
    }
    if (result == GrabResult::Frame) {
      const uint64_t expected = static_cast<uint64_t>(frame.step) * frame.height;
      if (!frame.width || !frame.height || frame.data.size() != expected) {
        RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000, "invalid frame dimensions or data length");
        continue;
      }
      sensor_msgs::msg::Image message;
      message.header.stamp = now();
      message.header.frame_id = frame_id_;
      message.height = frame.height;
      message.width = frame.width;
      message.encoding = frame.encoding;
      message.is_bigendian = false;
      message.step = frame.step;
      message.data = std::move(frame.data);
      image_publisher_->publish(std::move(message));
      ++frames_in_window;
      lost_packets += frame.lost_packets;
      consecutive_errors = 0;
      if (clock::now() - window_start >= std::chrono::seconds(5)) {
        const double elapsed = std::chrono::duration<double>(clock::now() - window_start).count();
        std_msgs::msg::Float64 fps;
        fps.data = frames_in_window / elapsed;
        fps_publisher_->publish(fps);
        RCLCPP_INFO(get_logger(), "actual_fps=%.2f, frame_lost_packets=%lu",
                    fps.data, static_cast<unsigned long>(lost_packets));
        window_start = clock::now();
        frames_in_window = 0;
        lost_packets = 0;
      }
    } else if (result == GrabResult::Error) {
      ++consecutive_errors;
      RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000, "%s", error.c_str());
      if (consecutive_errors >= 5) {
        std::lock_guard<std::mutex> lock(camera_mutex_);
        backend_->disconnect();
        consecutive_errors = 0;
      }
    } else if (result == GrabResult::Disconnected || !error.empty()) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "camera unavailable: %s", error.c_str());
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

}  // namespace hikrobot_mvs_camera
