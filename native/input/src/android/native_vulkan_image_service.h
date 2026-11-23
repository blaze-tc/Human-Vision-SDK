#pragma once
#include "android_input_gpu.h"
#include "humanvision_input_native.h"
#include "humanvision_input_live.h"
#include <memory>
#include <string>
namespace hvinput {
struct Session;
class NativeVulkanImageService {
  struct State;
  std::unique_ptr<State> state_;

public:
  explicit NativeVulkanImageService(Session &);
  ~NativeVulkanImageService();
  bool Open(const HV_InputNativeOptionsV1 &, std::string &error);
  void Queue(AndroidDecodedImage &);
  void Removed(AImageReader *, AHardwareBuffer *);
  void DrainReader();
  void Close();
  bool Retired();
  int Device(HV_InputNativeDeviceV1 &);
  int Pool(HV_InputNativePoolV1 &);
  int Acquire(uint64_t, HV_InputNativeFrameV1 &, uint32_t timeout_ms = 0);
  int Copy(uint32_t, uint64_t, uint64_t, void *, uint32_t);
  int RequestCapture(uint64_t &);
  int CopyCapture(uint64_t, void *, uint32_t, HV_InputFrameInfo &);
  int Release(uint32_t, uint64_t, uint64_t);
  int ConfigureLive(const HV_InputLiveConfigV1 &, std::string &error);
  int LiveGeometry(HV_InputLiveGeometryV1 &);
  int AcquireLive(uint64_t, HV_InputNativeFrameV1 &);
  int ReleaseLive(uint32_t, uint64_t, uint64_t);
  void FillProgress(HV_InputSourceProgressV1 &);
  void DisableLive();
  std::string LiveError();
};
} // namespace hvinput
