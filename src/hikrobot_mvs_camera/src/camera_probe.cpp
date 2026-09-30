#include <MvCameraControl.h>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

void print_float(void* handle, const char* key) {
  MVCC_FLOATVALUE value{};
  const int code = MV_CC_GetFloatValue(handle, key, &value);
  if (code == MV_OK) {
    std::cout << key << ": current=" << value.fCurValue << " min=" << value.fMin
              << " max=" << value.fMax << '\n';
  } else {
    std::cout << key << ": unavailable (0x" << std::hex << static_cast<unsigned int>(code)
              << std::dec << ")\n";
  }
}

void print_integer(void* handle, const char* key) {
  MVCC_INTVALUE_EX value{};
  if (MV_CC_GetIntValueEx(handle, key, &value) == MV_OK) {
    std::cout << key << ": current=" << value.nCurValue << " min=" << value.nMin
              << " max=" << value.nMax << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string wanted_serial = argc > 1 ? argv[1] : "";
  const int initialized = MV_CC_Initialize();
  if (initialized != MV_OK) {
    std::cerr << "MVS initialization failed: 0x" << std::hex << initialized << '\n';
    return 1;
  }
  MV_CC_DEVICE_INFO_LIST devices{};
  const int enumerated = MV_CC_EnumDevices(MV_USB_DEVICE, &devices);
  if (enumerated != MV_OK) {
    std::cerr << "MVS enumeration failed: 0x" << std::hex << enumerated << '\n';
    return 1;
  }
  std::cout << "SDK version: 0x" << std::hex << MV_CC_GetSDKVersion() << std::dec << '\n';
  std::cout << "Discovered devices: " << devices.nDeviceNum << '\n';
  MV_CC_DEVICE_INFO* selected = nullptr;
  unsigned int matches = 0;
  for (unsigned int i = 0; i < devices.nDeviceNum; ++i) {
    auto* device = devices.pDeviceInfo[i];
    if (!device) continue;
    if (device->nTLayerType != MV_USB_DEVICE) continue;
    const auto* serial = device->SpecialInfo.stUsb3VInfo.chSerialNumber;
    const auto* model = device->SpecialInfo.stUsb3VInfo.chModelName;
    const auto* version = device->SpecialInfo.stUsb3VInfo.chDeviceVersion;
    std::cout << i << ": USB3 model=" << model
              << " serial=" << serial << '\n';
    std::cout << "   device version=" << version << '\n';
    if (std::strcmp(reinterpret_cast<const char*>(model), "MV-CA016-10UC") == 0 &&
        (wanted_serial.empty() || wanted_serial == reinterpret_cast<const char*>(serial))) {
      selected = device;
      ++matches;
    }
  }
  if (matches != 1) {
    std::cerr << "Select exactly one MV-CA016-10UC by serial number.\n";
    return 1;
  }
  void* handle = nullptr;
  int code = MV_CC_CreateHandle(&handle, selected);
  if (code == MV_OK) code = MV_CC_OpenDevice(handle);
  if (code != MV_OK) {
    std::cerr << "Open failed: 0x" << std::hex << static_cast<unsigned int>(code) << '\n';
    if (handle) MV_CC_DestroyHandle(handle);
    return 1;
  }
  print_integer(handle, "Width");
  print_integer(handle, "Height");
  print_float(handle, "ExposureTime");
  print_float(handle, "Gain");
  print_float(handle, "AcquisitionFrameRate");
  MVCC_STRINGVALUE firmware{};
  if (MV_CC_GetStringValue(handle, "DeviceFirmwareVersion", &firmware) == MV_OK) {
    std::cout << "DeviceFirmwareVersion: " << firmware.chCurValue << '\n';
  }
  MVCC_ENUMVALUE_EX formats{};
  code = MV_CC_GetEnumValueEx(handle, "PixelFormat", &formats);
  if (code == MV_OK) {
    std::cout << "PixelFormat current=0x" << std::hex << formats.nCurValue << std::dec
              << " supported=" << formats.nSupportedNum << '\n';
    for (unsigned int i = 0; i < formats.nSupportedNum; ++i) {
      MVCC_ENUMENTRY entry{};
      entry.nValue = formats.nSupportValue[i];
      if (MV_CC_GetEnumEntrySymbolic(handle, "PixelFormat", &entry) == MV_OK) {
        std::cout << "  " << entry.chSymbolic << " = 0x" << std::hex << entry.nValue
                  << std::dec << (entry.nValue == formats.nCurValue ? " (current)" : "") << '\n';
      }
    }
  } else {
    std::cerr << "PixelFormat query failed: 0x" << std::hex << static_cast<unsigned int>(code)
              << std::dec << '\n';
  }
  MV_CC_CloseDevice(handle);
  MV_CC_DestroyHandle(handle);
  return 0;
}
