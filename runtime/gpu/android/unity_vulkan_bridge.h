#pragma once

#include "gpu/android/ahb_capabilities.h"
#include "gpu/android/ahb_slot_ring.h"
#include "humanvision/humanvision_android_gpu.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

namespace humanvision::gpu {

enum class BridgeResult { Ok, DroppedNoSlot, Busy, Closed, Invalid, GpuError };
enum class SourcePreparation : uint8_t { Ready, Warmed, Unsupported };
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
  uint32_t native_layout = 0;
  uint32_t native_stage = 0;
  uint32_t native_access = 0;
  uint32_t format = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t usage = 0;
  uint32_t samples = 0;
  uint32_t image_type = 0;
  uint32_t tiling = 0;
  uint32_t layers = 0;
  void *texture = nullptr;
};

struct UnityVulkanSlotCache {
  uintptr_t ahb = 0;
  uintptr_t ahb_buffer = 0; // Borrowed AHardwareBuffer, valid for this generation.
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

struct ConsumerFrame {
  SlotToken token{};
  SlotMetadata metadata{};
  uintptr_t ahb_buffer = 0;
  SyncFd producer_fd{};
  bool claimed = false;
  // Internal ncnn role handoff. The producer fd is consumed by the first
  // role; later roles acquire after the prior role has completed and released.
  bool ncnn_role_complete = false;
  // A one-shot internal completion hook installed by the backend that last
  // ran this observation. It is cleared before dispatch and on retirement.
  void* role_owner = nullptr;
  bool (*complete_role)(void*, ConsumerFrame&, bool, std::string&) noexcept = nullptr;
};

enum class NcnnRoleStart { Invalid, WaitForProducer, AcquireAfterPriorRole };
NcnnRoleStart NextNcnnRole(const ConsumerFrame& frame) noexcept;
bool CompleteGpuRole(ConsumerFrame& frame, bool final_role, std::string& error) noexcept;

struct ConsumerGeneration {
  uint64_t generation = 0;
  SlotContract contract{};
  std::array<uintptr_t, 3> retained_ahb{};
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
  SourcePreparation (*prepare_source)(void *, const UnityVulkanSlotCache &,
                                      const UnityTextureAccess &) noexcept = nullptr;
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
  bool (*submission_complete)(void *, const UnityVulkanDeviceContext &,
                              const UnityVulkanSlotCache &) noexcept = nullptr;
  void (*cancel_and_drain_events)(void *) noexcept = nullptr;
};

class UnityVulkanBridge {
public:
  struct EventRecord {
    UnityVulkanBridge *bridge = nullptr;
    SlotToken token{};
    void *unity_texture = nullptr;
    uint64_t generation = 0;
    std::atomic<uint64_t> reservation_id{0};
    uint32_t submitted_width = 0;
    uint32_t submitted_height = 0;
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
  BridgeResult Render(void *event_identity) noexcept;
  // Native worker only. The borrowed AHB and fd stay valid until RetireConsumer.
  SlotResult ClaimConsumer(ConsumerFrame&) noexcept;
  SlotResult ClaimDropped(ConsumerFrame&) noexcept;
  SlotResult RetireConsumer(ConsumerFrame&, CompletionProof) noexcept;
  // Terminal device fault: abandon this generation's GPU handles in place
  // when completion cannot be proved. Allows shutdown to finish without
  // falsely recycling or destroying an in-flight AHB.
  SlotResult QuarantineConsumer(ConsumerFrame&) noexcept;
  // Control-thread warm-up borrows each AHB while shutdown is excluded. Caller
  // releases exactly the references it retained after all ncnn imports drain.
  SlotResult RetainGeneration(ConsumerGeneration&,
                              void (*retain_ahb)(uintptr_t) noexcept) noexcept;
  void GetStatus(HV_AndroidGpuBridgeStatusV1 &) const noexcept;
  void CloseAdmission() noexcept { accepting_calls_.store(false, std::memory_order_release); }
  bool IsClosed() const noexcept { return !initialized_.load(std::memory_order_acquire); }
  const char* Diagnostic() const noexcept;
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
  bool Enter() const noexcept;
  void Leave() const noexcept;
  void Recover() noexcept;
  void ShutdownLocked() noexcept;
  static void *EncodeIdentity(uint32_t index, uint64_t reservation) noexcept;
  bool DecodeIdentity(void *, uint32_t &, uint64_t &) const noexcept;

  struct RecoveryRecord {
    SlotToken token{};
    SyncFd fd{};
    std::atomic<uint8_t> kind{0}; // 0 none, 1 pre-submit, 2 submitted, 3 publish retry
  };

  UnityVulkanBridgeDispatch dispatch_;
  UnityVulkanDeviceContext device_{};
  AhbSelection selection_{};
  SlotContract contract_{};
  std::array<UnityVulkanSlotCache, AhbSlotRing::kSlotCount> slots_{};
  std::array<EventRecord, AhbSlotRing::kSlotCount> records_{};
  std::array<RecoveryRecord, AhbSlotRing::kSlotCount> recovery_{};
  AhbSlotRing ring_;
  std::atomic<bool> initialized_{false};
  std::atomic<bool> accepting_calls_{false};
  mutable std::atomic<uint32_t> active_calls_{0};
  std::atomic<uint32_t> consumer_leases_{0};
  std::atomic<bool> quarantined_{false};
  mutable std::mutex active_mutex_;
  mutable std::condition_variable active_cv_;
  std::mutex control_mutex_;
  std::atomic<uint64_t> next_reservation_{1};
  std::atomic<uint64_t> source_contract_signature_{0};
  std::atomic<uint64_t> submitted_frames_{0};
  std::atomic<uint64_t> imported_frames_{0};
  std::atomic<uint32_t> last_error_{0};
  static std::atomic<UnityVulkanBridge *> callback_bridge_;
};

} // namespace humanvision::gpu
