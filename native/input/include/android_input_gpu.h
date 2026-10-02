#pragma once
#include <cstdint>
namespace hvinput {
// Physical support, successful Unity device creation and actual decoded-buffer
// properties are independent facts. Entry point existence proves none of them.
struct InputGpuCapabilities {
  uint64_t external_format = 0, usage = 0;
  uint32_t ahb_format = 0, format = 0, format_features = 0;
  uint32_t model = 0, range = 0, x_chroma = 0, y_chroma = 0;
  uint32_t components[4]{};
  uint32_t sync_fd_features = 0, sync_fd_compatible_types = 0;
  bool sampled = false, physical_ycbcr = false, physical_sync_fd = false;
  bool logical_ycbcr = false, logical_sync_fd = false, logical_ahb = false;
  bool logical_proven = false;
};
enum class CapabilityFailure {
  None, MissingExternalFormat, MissingSampled, MissingPhysicalYcbcr,
  MissingPhysicalSyncFd, LogicalDeviceUnproven, MissingLogicalYcbcr,
  MissingLogicalSyncFd, MissingLogicalAhb
};
inline CapabilityFailure AdmitDecodedBuffer(const InputGpuCapabilities& c) {
  if (!c.external_format) return CapabilityFailure::MissingExternalFormat;
  if (!c.sampled) return CapabilityFailure::MissingSampled;
  if (!c.physical_ycbcr) return CapabilityFailure::MissingPhysicalYcbcr;
  if (!c.physical_sync_fd) return CapabilityFailure::MissingPhysicalSyncFd;
  if (!c.logical_proven) return CapabilityFailure::LogicalDeviceUnproven;
  if (!c.logical_ycbcr) return CapabilityFailure::MissingLogicalYcbcr;
  if (!c.logical_sync_fd) return CapabilityFailure::MissingLogicalSyncFd;
  if (!c.logical_ahb) return CapabilityFailure::MissingLogicalAhb;
  return CapabilityFailure::None;
}
}
#ifdef __ANDROID__
#include <android/hardware_buffer.h>
#include <media/NdkImage.h>
#include <vulkan/vulkan.h>
namespace hvinput {
struct AndroidDecodedImage {
  AImage* image = nullptr;
  AHardwareBuffer* buffer = nullptr;
  int acquire_fd = -1;
  bool image_counted = false;
  uint64_t generation = 0;
  int64_t pts_us = 0, received_us = 0, decoded_us = 0;
  AndroidDecodedImage() = default;
  AndroidDecodedImage(const AndroidDecodedImage&) = delete;
  AndroidDecodedImage& operator=(const AndroidDecodedImage&) = delete;
  ~AndroidDecodedImage();
};
bool ProbeDecodedBuffer(AHardwareBuffer*, VkPhysicalDevice, InputGpuCapabilities*);
void RecordDecodedCapability(AndroidDecodedImage&, const char* codec);
void StartCapabilityProbe(const char* url);
void StopCapabilityProbe();
}
#endif
