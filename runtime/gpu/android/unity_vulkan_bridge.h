#pragma once

#include "gpu/android/ahb_capabilities.h"
#include "gpu/android/ahb_slot_ring.h"
#include "humanvision/humanvision_android_gpu.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace humanvision::gpu {

enum class BridgeResult { Ok, DroppedNoSlot, Busy, Closed, Invalid, GpuError };
enum class BridgeImageLayout : uint32_t {
  Undefined,
  SourceCurrent,
  TransferSource,
  TransferDestination,
  ShaderRead,
  ColorAttachment,
  External
};

struct UnityVulkanDeviceContext {
  uintptr_t instance = 0;
  uintptr_t physical_device = 0;
  uintptr_t device = 0;
  uintptr_t graphics_queue = 0;
  uint32_t graphics_queue_family = 0;
  std::array<uint8_t, HV_ANDROID_GPU_UUID_SIZE> device_uuid{};
  std::array<uint8_t, HV_ANDROID_GPU_UUID_SIZE> driver_uuid{};
};

struct BridgeBarrier {
  uintptr_t image = 0;
  BridgeImageLayout old_layout = BridgeImageLayout::Undefined;
  BridgeImageLayout new_layout = BridgeImageLayout::Undefined;
  uint32_t source_queue_family = 0;
  uint32_t destination_queue_family = 0;
};

struct UnityTextureAccess {
  uintptr_t image = 0;
  BridgeImageLayout layout = BridgeImageLayout::SourceCurrent;
};

struct UnityVulkanSlotCache {
  uintptr_t ahb = 0;
  uintptr_t image = 0;
  uintptr_t memory = 0;
  uintptr_t image_view = 0;
  uintptr_t framebuffer = 0;
  uintptr_t command_buffer = 0;
  uintptr_t export_semaphore = 0;
  uintptr_t sampler = 0;
  uintptr_t descriptor_set_layout = 0;
  uintptr_t descriptor_pool = 0;
  uintptr_t descriptor_set = 0;
  uintptr_t render_pass = 0;
  uintptr_t pipeline_layout = 0;
  uintptr_t pipeline = 0;
};

// Deliberately contains no wait, readback, staging-buffer or host-mapping hook.
// Android binds these operations to Vulkan/Unity; host tests bind deterministic
// fakes to the same production bridge.
struct UnityVulkanBridgeDispatch {
  void *context = nullptr;
  bool (*create_slot)(void *, uint32_t, const UnityVulkanDeviceContext &,
                      const SlotContract &, const AhbSelection &,
                      UnityVulkanSlotCache &) noexcept = nullptr;
  void (*drain_slot)(void *, uint32_t, AhbSlotState, UnityVulkanSlotCache &,
                     SyncFd &) noexcept = nullptr;
  bool (*access_texture)(void *, void *,
                         UnityTextureAccess &) noexcept = nullptr;
  void (*release_texture)(void *, void *,
                          const UnityTextureAccess &) noexcept = nullptr;
  bool (*queue_access)(void *, void (*)(void *) noexcept,
                       void *) noexcept = nullptr;
  bool (*record_blit)(void *, const UnityVulkanSlotCache &,
                      const UnityTextureAccess &, const BridgeBarrier *,
                      uint32_t) noexcept = nullptr;
  bool (*record_color)(void *, const UnityVulkanSlotCache &,
                       const UnityTextureAccess &, const BridgeBarrier *,
                       uint32_t, bool gpu_shader_conversion) noexcept = nullptr;
  bool (*submit_signal)(void *, const UnityVulkanDeviceContext &,
                        const UnityVulkanSlotCache &) noexcept = nullptr;
  bool (*export_sync_fd)(void *, const UnityVulkanDeviceContext &,
                         const UnityVulkanSlotCache &,
                         SyncFd &) noexcept = nullptr;
  void (*cancel_and_drain_events)(void *) noexcept = nullptr;
};

class UnityVulkanBridge {
public:
  struct EventRecord {
    UnityVulkanBridge *bridge = nullptr;
    SlotToken token{};
    void *unity_texture = nullptr;
    uint64_t generation = 0;
    std::atomic<bool> pending{false};
    std::atomic<bool> queue_pending{false};
    std::atomic<bool> texture_accessed{false};
    UnityTextureAccess access{};
    std::array<BridgeBarrier, 4> barriers{};
  };

  explicit UnityVulkanBridge(UnityVulkanBridgeDispatch dispatch) noexcept;
  ~UnityVulkanBridge();
  UnityVulkanBridge(const UnityVulkanBridge &) = delete;
  UnityVulkanBridge &operator=(const UnityVulkanBridge &) = delete;

  bool Initialize(const UnityVulkanDeviceContext &, const AhbSelection &,
                  const SlotContract &) noexcept;
  void Shutdown() noexcept;
  BridgeResult Prepare(const HV_AndroidGpuSubmissionV1 &,
                       void **event_data) noexcept;
  BridgeResult Render(EventRecord *) noexcept;
  void GetStatus(HV_AndroidGpuBridgeStatusV1 &) const noexcept;
  uint64_t Generation() const noexcept { return ring_.Generation(); }
  static void RenderEvent(int event_id, void *data) noexcept;
  static void QueueEvent(void *data) noexcept;

private:
  static bool CreateSlot(void *, uint32_t, const SlotContract &,
                         SlotResources &) noexcept;
  static void DrainSlot(void *, uint32_t, AhbSlotState, SlotResources &,
                        SyncFd &) noexcept;
  void RetireWithoutSubmission(const SlotToken &) noexcept;
  BridgeResult ExecuteQueue(EventRecord *) noexcept;

  UnityVulkanBridgeDispatch dispatch_;
  UnityVulkanDeviceContext device_{};
  AhbSelection selection_{};
  SlotContract contract_{};
  std::array<UnityVulkanSlotCache, AhbSlotRing::kSlotCount> slots_{};
  std::array<EventRecord, AhbSlotRing::kSlotCount> records_{};
  AhbSlotRing ring_;
  std::atomic<bool> initialized_{false};
  std::atomic<uint64_t> submitted_frames_{0};
  std::atomic<uint64_t> imported_frames_{0};
};

} // namespace humanvision::gpu
