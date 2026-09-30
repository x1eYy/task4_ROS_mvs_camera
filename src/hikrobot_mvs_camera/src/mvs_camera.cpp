#include "hikrobot_mvs_camera/camera_backend.hpp"

#include <MvCameraControl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace hikrobot_mvs_camera {
namespace {

std::string sdk_error(const char* operation, int code) {
  std::ostringstream out;
  out << operation << " failed (MVS 0x" << std::hex << static_cast<uint32_t>(code) << ')';
  return out.str();
}

std::string bounded_string(const unsigned char* value, std::size_t capacity) {
  const auto* end = static_cast<const unsigned char*>(std::memchr(value, 0, capacity));
  return std::string(reinterpret_cast<const char*>(value),
                     end ? static_cast<std::size_t>(end - value) : capacity);
}

Device describe(const MV_CC_DEVICE_INFO& raw) {
  Device device;
  const auto& info = raw.SpecialInfo.stUsb3VInfo;
  device.serial_number = bounded_string(info.chSerialNumber, sizeof(info.chSerialNumber));
  device.model = bounded_string(info.chModelName, sizeof(info.chModelName));
  return device;
}

bool plain_mono(MvGvspPixelType type) {
  switch (type) {
    case PixelType_Gvsp_Mono8:
    case PixelType_Gvsp_Mono10:
    case PixelType_Gvsp_Mono10_Packed:
    case PixelType_Gvsp_Mono12:
    case PixelType_Gvsp_Mono12_Packed:
    case PixelType_Gvsp_Mono14:
    case PixelType_Gvsp_Mono16:
      return true;
    default:
      return false;
  }
}

bool bayer_format(MvGvspPixelType type) {
  switch (type) {
    case PixelType_Gvsp_BayerGR8: case PixelType_Gvsp_BayerRG8:
    case PixelType_Gvsp_BayerGB8: case PixelType_Gvsp_BayerBG8:
    case PixelType_Gvsp_BayerRBGG8: case PixelType_Gvsp_BayerBRGG8:
    case PixelType_Gvsp_BayerGR10: case PixelType_Gvsp_BayerRG10:
    case PixelType_Gvsp_BayerGB10: case PixelType_Gvsp_BayerBG10:
    case PixelType_Gvsp_BayerGR12: case PixelType_Gvsp_BayerRG12:
    case PixelType_Gvsp_BayerGB12: case PixelType_Gvsp_BayerBG12:
    case PixelType_Gvsp_BayerGR10_Packed: case PixelType_Gvsp_BayerRG10_Packed:
    case PixelType_Gvsp_BayerGB10_Packed: case PixelType_Gvsp_BayerBG10_Packed:
    case PixelType_Gvsp_BayerGR12_Packed: case PixelType_Gvsp_BayerRG12_Packed:
    case PixelType_Gvsp_BayerGB12_Packed: case PixelType_Gvsp_BayerBG12_Packed:
    case PixelType_Gvsp_BayerGR16: case PixelType_Gvsp_BayerRG16:
    case PixelType_Gvsp_BayerGB16: case PixelType_Gvsp_BayerBG16:
      return true;
    default:
      return false;
  }
}

bool supported_input(MvGvspPixelType type) {
  return plain_mono(type) || bayer_format(type) ||
         type == PixelType_Gvsp_RGB8_Packed || type == PixelType_Gvsp_BGR8_Packed ||
         type == PixelType_Gvsp_YUV422_Packed || type == PixelType_Gvsp_YUV422_YUYV_Packed;
}

const char* feature_name(Feature feature) {
  switch (feature) {
    case Feature::Exposure: return "ExposureTime";
    case Feature::Gain: return "Gain";
    case Feature::FrameRate: return "AcquisitionFrameRate";
  }
  return "";
}

class MvsCamera final : public CameraBackend {
public:
  MvsCamera() {
    const int result = MV_CC_Initialize();
    if (result != MV_OK) throw std::runtime_error(sdk_error("MV_CC_Initialize", result));
  }

  ~MvsCamera() override {
    disconnect();
    // MVS may still have background threads at process exit. Finalize can unload
    // their code before they stop, causing an intermittent shutdown segfault.
  }

  bool connect(const Selector& selector, std::string& error) override {
    disconnect();
    MV_CC_DEVICE_INFO_LIST list{};
    const int enumeration = MV_CC_EnumDevices(MV_USB_DEVICE, &list);
    if (enumeration != MV_OK) {
      error = sdk_error("MV_CC_EnumDevices", enumeration);
      return false;
    }
    std::vector<Device> devices;
    std::vector<MV_CC_DEVICE_INFO*> raw_devices;
    for (unsigned int i = 0; i < list.nDeviceNum; ++i) {
      auto* raw = list.pDeviceInfo[i];
      if (raw && raw->nTLayerType == MV_USB_DEVICE &&
          describe(*raw).model == "MV-CA016-10UC") {
        devices.push_back(describe(*raw));
        raw_devices.push_back(raw);
      }
    }
    Selector effective = selector;
    if (effective.serial_number.empty() && !pinned_serial_.empty()) {
      effective.serial_number = pinned_serial_;
    }
    std::size_t index = 0;
    if (!select_device(devices, effective, index, error)) return false;
    const int created = MV_CC_CreateHandle(&handle_, raw_devices[index]);
    if (created != MV_OK) {
      handle_ = nullptr;
      error = sdk_error("MV_CC_CreateHandle", created);
      return false;
    }
    const int opened = MV_CC_OpenDevice(handle_, MV_ACCESS_Exclusive, 0);
    if (opened != MV_OK) {
      error = sdk_error("MV_CC_OpenDevice", opened);
      disconnect();
      return false;
    }
    opened_ = true;
    device_ = devices[index];
    if (!device_.serial_number.empty()) pinned_serial_ = device_.serial_number;
    const int trigger = MV_CC_SetEnumValue(handle_, "TriggerMode", MV_TRIGGER_MODE_OFF);
    if (trigger != MV_OK) {
      error = sdk_error("set TriggerMode=Off", trigger);
      disconnect();
      return false;
    }
    const int started = MV_CC_StartGrabbing(handle_);
    if (started != MV_OK) {
      error = sdk_error("MV_CC_StartGrabbing", started);
      disconnect();
      return false;
    }
    grabbing_ = true;
    return true;
  }

  void disconnect() override {
    if (handle_) {
      if (grabbing_) MV_CC_StopGrabbing(handle_);
      if (opened_) MV_CC_CloseDevice(handle_);
      MV_CC_DestroyHandle(handle_);
    }
    handle_ = nullptr;
    opened_ = false;
    grabbing_ = false;
  }

  bool connected() const override { return handle_ && opened_; }

  std::string device_label() const override {
    return device_.model + " [" + device_.serial_number + "]";
  }

  GrabResult grab(Frame& frame, std::string& error) override {
    if (!connected() || !MV_CC_IsDeviceConnected(handle_)) {
      error = "camera disconnected";
      return GrabResult::Disconnected;
    }
    MV_FRAME_OUT raw{};
    const int result = MV_CC_GetImageBuffer(handle_, &raw, 200);
    if (static_cast<uint32_t>(result) == MV_E_NODATA ||
        static_cast<uint32_t>(result) == MV_E_TIMEOUT) return GrabResult::Timeout;
    if (result != MV_OK) {
      error = sdk_error("MV_CC_GetImageBuffer", result);
      return MV_CC_IsDeviceConnected(handle_) ? GrabResult::Error : GrabResult::Disconnected;
    }
    // The SDK owns raw.pBufAddr. Always return it, including after conversion errors.
    struct BufferGuard {
      void* handle;
      MV_FRAME_OUT* raw;
      ~BufferGuard() { MV_CC_FreeImageBuffer(handle, raw); }
    } guard{handle_, &raw};
    const auto& info = raw.stFrameInfo;
    const uint32_t width = info.nExtendWidth ? info.nExtendWidth : info.nWidth;
    const uint32_t height = info.nExtendHeight ? info.nExtendHeight : info.nHeight;
    const uint64_t source_length = info.nFrameLenEx ? info.nFrameLenEx : info.nFrameLen;
    if (!supported_input(info.enPixelType)) {
      error = "camera pixel format is not supported by the ROS image conversion path";
      return GrabResult::Error;
    }
    const bool mono = plain_mono(info.enPixelType);
    const uint32_t bytes_per_pixel = mono ? 1 : 3;
    const uint64_t required = static_cast<uint64_t>(width) * height * bytes_per_pixel;
    if (!raw.pBufAddr || !width || !height || !required ||
        required > std::numeric_limits<unsigned int>::max() ||
        source_length > std::numeric_limits<unsigned int>::max()) {
      error = "invalid or oversized MVS frame";
      return GrabResult::Error;
    }
    frame.width = width;
    frame.height = height;
    frame.step = width * bytes_per_pixel;
    frame.sequence = info.nFrameNum;
    frame.lost_packets = info.nLostPacket;
    frame.encoding = mono ? "mono8" : "bgr8";
    frame.data.resize(static_cast<std::size_t>(required));
    if (info.enPixelType == PixelType_Gvsp_Mono8 || info.enPixelType == PixelType_Gvsp_BGR8_Packed) {
      if (source_length < required) {
        error = "MVS frame payload is shorter than its dimensions";
        return GrabResult::Error;
      }
      std::memcpy(frame.data.data(), raw.pBufAddr, static_cast<std::size_t>(required));
      return GrabResult::Frame;
    }
    MV_CC_PIXEL_CONVERT_PARAM_EX conversion{};
    conversion.nWidth = width;
    conversion.nHeight = height;
    conversion.enSrcPixelType = info.enPixelType;
    conversion.pSrcData = raw.pBufAddr;
    conversion.nSrcDataLen = static_cast<unsigned int>(source_length);
    conversion.enDstPixelType = mono ? PixelType_Gvsp_Mono8 : PixelType_Gvsp_BGR8_Packed;
    conversion.pDstBuffer = frame.data.data();
    conversion.nDstBufferSize = static_cast<unsigned int>(required);
    const int converted = MV_CC_ConvertPixelTypeEx(handle_, &conversion);
    if (converted != MV_OK || conversion.nDstLen != required) {
      error = converted == MV_OK ? "MVS conversion returned an unexpected image length" :
              sdk_error("MV_CC_ConvertPixelTypeEx", converted);
      return GrabResult::Error;
    }
    return GrabResult::Frame;
  }

  bool set_float(Feature feature, double value, std::string& error) override {
    if (!connected()) { error = "camera is not connected"; return false; }
    const char* name = feature_name(feature);
    MVCC_FLOATVALUE range{};
    int result = MV_CC_GetFloatValue(handle_, name, &range);
    if (result != MV_OK) { error = sdk_error(name, result); return false; }
    if (!std::isfinite(value) || value < range.fMin || value > range.fMax) {
      std::ostringstream out;
      out << name << " must be in [" << range.fMin << ", " << range.fMax << ']';
      error = out.str();
      return false;
    }
    if (feature == Feature::Exposure || feature == Feature::Gain) {
      const char* auto_name = feature == Feature::Exposure ? "ExposureAuto" : "GainAuto";
      MVCC_ENUMVALUE auto_value{};
      result = MV_CC_GetEnumValue(handle_, auto_name, &auto_value);
      if (result != MV_OK) { error = sdk_error(auto_name, result); return false; }
      result = MV_CC_SetEnumValue(handle_, auto_name, 0);
      if (result != MV_OK) { error = sdk_error("disable auto mode", result); return false; }
      result = MV_CC_SetFloatValue(handle_, name, static_cast<float>(value));
      if (result != MV_OK) {
        MV_CC_SetEnumValue(handle_, auto_name, auto_value.nCurValue);
        error = sdk_error(name, result);
        return false;
      }
    } else {
      bool enabled = false;
      result = MV_CC_GetBoolValue(handle_, "AcquisitionFrameRateEnable", &enabled);
      if (result != MV_OK) { error = sdk_error("AcquisitionFrameRateEnable", result); return false; }
      result = MV_CC_SetBoolValue(handle_, "AcquisitionFrameRateEnable", true);
      if (result != MV_OK) { error = sdk_error("enable frame rate control", result); return false; }
      result = MV_CC_SetFloatValue(handle_, name, static_cast<float>(value));
      if (result != MV_OK) {
        MV_CC_SetBoolValue(handle_, "AcquisitionFrameRateEnable", enabled);
        error = sdk_error(name, result);
        return false;
      }
    }
    MVCC_FLOATVALUE actual{};
    result = MV_CC_GetFloatValue(handle_, name, &actual);
    if (result != MV_OK) {
      MV_CC_SetFloatValue(handle_, name, range.fCurValue);
      error = sdk_error("read back camera setting", result);
      return false;
    }
    if (std::fabs(actual.fCurValue - value) > std::max(0.01, std::fabs(value) * 0.01)) {
      std::ostringstream out;
      out << name << " readback " << actual.fCurValue << " differs from requested " << value;
      error = out.str();
      MV_CC_SetFloatValue(handle_, name, range.fCurValue);
      return false;
    }
    return true;
  }

  bool set_pixel_format(const std::string& value, std::string& error) override {
    if (!connected()) { error = "camera is not connected"; return false; }
    MVCC_ENUMVALUE_EX formats{};
    int result = MV_CC_GetEnumValueEx(handle_, "PixelFormat", &formats);
    if (result != MV_OK) { error = sdk_error("get PixelFormat", result); return false; }
    unsigned int requested = 0;
    bool supported = false;
    for (unsigned int i = 0; i < formats.nSupportedNum; ++i) {
      MVCC_ENUMENTRY entry{};
      entry.nValue = formats.nSupportValue[i];
      if (MV_CC_GetEnumEntrySymbolic(handle_, "PixelFormat", &entry) == MV_OK &&
          value == entry.chSymbolic &&
          supported_input(static_cast<MvGvspPixelType>(entry.nValue))) {
        requested = entry.nValue;
        supported = true;
        break;
      }
    }
    if (!supported) { error = "unsupported PixelFormat: " + value; return false; }
    if (formats.nCurValue == requested) return true;
    if (grabbing_) {
      result = MV_CC_StopGrabbing(handle_);
      if (result != MV_OK) { error = sdk_error("stop grabbing for PixelFormat", result); return false; }
      grabbing_ = false;
    }
    result = MV_CC_SetEnumValue(handle_, "PixelFormat", requested);
    if (result != MV_OK) {
      error = sdk_error("set PixelFormat", result);
      restart_after_format(error);
      return false;
    }
    MVCC_ENUMVALUE_EX readback{};
    result = MV_CC_GetEnumValueEx(handle_, "PixelFormat", &readback);
    if (result != MV_OK || readback.nCurValue != requested) {
      error = result == MV_OK ? "PixelFormat readback mismatch" : sdk_error("read back PixelFormat", result);
      MV_CC_SetEnumValue(handle_, "PixelFormat", formats.nCurValue);
      restart_after_format(error);
      return false;
    }
    result = MV_CC_StartGrabbing(handle_);
    if (result != MV_OK) {
      error = sdk_error("restart grabbing after PixelFormat", result);
      MV_CC_SetEnumValue(handle_, "PixelFormat", formats.nCurValue);
      restart_after_format(error);
      return false;
    }
    grabbing_ = true;
    return true;
  }

private:
  void restart_after_format(std::string& error) {
    const int result = MV_CC_StartGrabbing(handle_);
    if (result == MV_OK) grabbing_ = true;
    else error += "; " + sdk_error("restore grabbing", result);
  }

  void* handle_ = nullptr;
  bool opened_ = false;
  bool grabbing_ = false;
  Device device_;
  std::string pinned_serial_;
};

}  // namespace

bool select_device(const std::vector<Device>& devices, const Selector& selector,
                   std::size_t& index, std::string& error) {
  std::vector<std::size_t> matches;
  for (std::size_t i = 0; i < devices.size(); ++i) {
    if (devices[i].model == "MV-CA016-10UC" &&
        (selector.serial_number.empty() || devices[i].serial_number == selector.serial_number)) {
      matches.push_back(i);
    }
  }
  if (matches.size() != 1) {
    error = matches.empty() ? "no matching camera found" :
            "multiple cameras match; set serial_number";
    return false;
  }
  index = matches.front();
  return true;
}

std::unique_ptr<CameraBackend> make_mvs_camera() { return std::make_unique<MvsCamera>(); }

}  // namespace hikrobot_mvs_camera
