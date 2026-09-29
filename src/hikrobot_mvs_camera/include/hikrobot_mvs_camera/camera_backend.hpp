#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hikrobot_mvs_camera {

struct Selector {
  std::string serial_number;
  std::string camera_ip;
};

struct Device {
  std::string serial_number;
  std::string camera_ip;
  std::string model;
  bool gige = false;
};

// Return the unique matching device. An empty selector is only safe with one camera.
bool select_device(const std::vector<Device>& devices, const Selector& selector,
                   std::size_t& index, std::string& error);

enum class Feature { Exposure, Gain, FrameRate };
enum class GrabResult { Frame, Timeout, Disconnected, Error };

struct Frame {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t step = 0;
  uint32_t sequence = 0;
  uint32_t lost_packets = 0;
  std::string encoding;
  std::vector<uint8_t> data;
};

class CameraBackend {
public:
  virtual ~CameraBackend() = default;
  virtual bool connect(const Selector& selector, std::string& error) = 0;
  virtual void disconnect() = 0;
  virtual bool connected() const = 0;
  virtual std::string device_label() const = 0;
  virtual GrabResult grab(Frame& frame, std::string& error) = 0;
  virtual bool set_float(Feature feature, double value, std::string& error) = 0;
  virtual bool set_pixel_format(const std::string& value, std::string& error) = 0;
};

std::unique_ptr<CameraBackend> make_mvs_camera();

}  // namespace hikrobot_mvs_camera
