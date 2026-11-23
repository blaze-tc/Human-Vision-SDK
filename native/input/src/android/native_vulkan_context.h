#pragma once
#include "humanvision_input_native.h"
#include "input_conversion.h"
#include <string>
namespace hvinput {
struct NativeRgbaSlot {
  AHardwareBuffer *ahb{};
  uint64_t usage{};
  VkImage image{};
  VkDeviceMemory memory{};
  VkBuffer staging{};
  VkDeviceMemory staging_memory{};
  void *mapped{};
  bool coherent{}, written{};
  HV_InputNativeFrameV1 frame{};
  int producer_fd = -1;
};
// One instance, logical device, compute queue and command stream per Input
// session.
class NativeVulkanContext : public InputConversionApi {
  void *library_{};
  VkInstance instance_{};
  PFN_vkGetInstanceProcAddr proc_{};
  VkPhysicalDeviceMemoryProperties memory_{};
  VkImage scratch_{};
  VkImageView scratch_view_{};
  VkDeviceMemory scratch_memory_{};
  VkCommandPool pool_{};
  VkCommandBuffer command_{};
  VkFence fence_{};
  VkSemaphore acquire_{}, ready_{};
  bool scratch_used_{};
  struct Capture {
    uint32_t width{}, height{};
    VkImage image{};
    VkImageView view{};
    VkDeviceMemory memory{};
    NativeRgbaSlot staging{};
    bool initialized{};
  } capture_;
  bool capturing_{}, capture_visible_{};
  bool MakeScratch(uint32_t, uint32_t, VkImage &, VkDeviceMemory &,
                   VkImageView &);
  bool MakeStaging(NativeRgbaSlot &, uint32_t, uint32_t);
  bool PrepareCapture(uint32_t, uint32_t);
  void DestroyCapture();
  void DestroyStaging(NativeRgbaSlot &);
  uint32_t width_{}, height_{};
  VkQueue queue_{};
#define NATIVE_VK_EXTRA(X)                                                     \
  X(DestroyDevice)                                                             \
  X(GetDeviceQueue) X(GetImageMemoryRequirements) X(CreateBuffer)              \
      X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory)      \
          X(MapMemory) X(UnmapMemory) X(InvalidateMappedMemoryRanges)          \
              X(CmdCopyImageToBuffer) X(WaitForFences)
#define DECL(name) PFN_vk##name name{};
  NATIVE_VK_EXTRA(DECL)
#undef DECL
  bool MakeOutput(NativeRgbaSlot &, bool readback);
  uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags required) const;
  bool Check(VkResult result, const char *stage);
  bool Fail(const char *reason);
  bool ReadContract(AndroidDecodedImage &, ImageContract &);
  void Record(AndroidDecodedImage &, CacheEntry &, NativeRgbaSlot &, int, bool);
  bool RunFence(NativeRgbaSlot &, AndroidDecodedImage &, CacheEntry &);

public:
  VkDevice device{};
  VkPhysicalDevice physical{};
  InputBufferRegistry registry;
  Counters stats;
  uint64_t completed{};
  InputConversionImporter importer;
  InputImageCache cache;
  std::array<NativeRgbaSlot, 3> slots{};
  HV_InputNativeDeviceV1 identity{};
  std::string error;
  bool unfenced = false;
  NativeVulkanContext();
  ~NativeVulkanContext();
  bool Open(const HV_InputNativeOptionsV1 &);
  bool Convert(AndroidDecodedImage &, int slot, int rotation, bool mirror,
               bool capture);
  bool CaptureVisible() const { return capture_visible_; }
  const void *CaptureBytes() const { return capture_.staging.mapped; }
  uint32_t CaptureWidth() const { return capture_.width; }
  uint32_t CaptureHeight() const { return capture_.height; }
};
} // namespace hvinput
