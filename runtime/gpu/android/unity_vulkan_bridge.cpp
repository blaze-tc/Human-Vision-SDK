#include "gpu/android/unity_vulkan_bridge.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace humanvision::gpu {
namespace {
// Vulkan defines VK_QUEUE_FAMILY_EXTERNAL as ~1U. Keep the host contract free
// of Vulkan headers while preserving the exact wire value.
constexpr uint32_t kExternalQueueFamily = UINT32_MAX - 1u;

bool HasRequiredObjects(const UnityVulkanSlotCache &slot,
                        HV_AndroidGpuCopyPath path) noexcept {
  const bool common = slot.ahb && slot.image && slot.memory &&
                      slot.image_view && slot.command_buffer &&
                      slot.export_semaphore;
  if (!common)
    return false;
  if (path != HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT)
    return true;
  return slot.framebuffer && slot.sampler && slot.descriptor_set_layout &&
         slot.descriptor_set && slot.render_pass && slot.pipeline;
}

BridgeResult MapReserve(SlotResult result) noexcept {
  switch (result) {
  case SlotResult::Ok:
    return BridgeResult::Ok;
  case SlotResult::NoSlot:
    return BridgeResult::DroppedNoSlot;
  case SlotResult::Busy:
    return BridgeResult::Busy;
  case SlotResult::Closed:
    return BridgeResult::Closed;
  default:
    return BridgeResult::Invalid;
  }
}
} // namespace

UnityVulkanBridge::UnityVulkanBridge(
    UnityVulkanBridgeDispatch dispatch) noexcept
    : dispatch_(dispatch), ring_({this, &UnityVulkanBridge::CreateSlot,
                                  &UnityVulkanBridge::DrainSlot}) {
  for (auto &record : records_)
    record.bridge = this;
}

UnityVulkanBridge::~UnityVulkanBridge() { Shutdown(); }

bool UnityVulkanBridge::Initialize(const UnityVulkanDeviceContext &device,
                                   const AhbSelection &selection,
                                   const SlotContract &contract) noexcept {
  if (!dispatch_.create_slot || !dispatch_.drain_slot ||
      !dispatch_.access_texture || !dispatch_.release_texture ||
      !dispatch_.record_blit || !dispatch_.record_color ||
      !dispatch_.queue_access || !dispatch_.submit_signal ||
      !dispatch_.export_sync_fd || !dispatch_.cancel_and_drain_events ||
      !device.instance || !device.physical_device || !device.device ||
      !device.graphics_queue || !contract.width || !contract.height ||
      selection.path == HV_ANDROID_GPU_COPY_UNAVAILABLE ||
      selection.contract.width != contract.width ||
      selection.contract.height != contract.height ||
      selection.contract.format != contract.actual_format ||
      selection.contract.usage != contract.actual_usage)
    return false;
  const bool selected_measured =
      std::any_of(selection.candidates.begin(), selection.candidates.end(),
                  [&](const AhbCandidate &candidate) {
                    return candidate.path == selection.path;
                  });
  if (!selected_measured)
    return false;

  Shutdown();
  device_ = device;
  selection_ = selection;
  contract_ = contract;
  submitted_frames_.store(0, std::memory_order_relaxed);
  imported_frames_.store(0, std::memory_order_relaxed);
  if (!ring_.Reconfigure(contract_)) {
    selection_ = {};
    device_ = {};
    return false;
  }
  for (auto &record : records_) {
    record.token = {};
    record.unity_texture = nullptr;
    record.generation = ring_.Generation();
    record.pending.store(false, std::memory_order_release);
    record.queue_pending.store(false, std::memory_order_release);
    record.texture_accessed.store(false, std::memory_order_release);
  }
  initialized_.store(true, std::memory_order_release);
  return true;
}

void UnityVulkanBridge::Shutdown() noexcept {
  if (!initialized_.exchange(false, std::memory_order_acq_rel))
    return;
  // The platform adapter invalidates queued Unity event records and joins any
  // callback already using them before the ring destroys generation objects.
  for (auto &record : records_) {
    record.pending.store(false, std::memory_order_release);
    record.queue_pending.store(false, std::memory_order_release);
  }
  dispatch_.cancel_and_drain_events(dispatch_.context);
  ring_.Shutdown();
  selection_ = {};
  contract_ = {};
  device_ = {};
}

bool UnityVulkanBridge::CreateSlot(void *context, uint32_t index,
                                   const SlotContract &contract,
                                   SlotResources &) noexcept {
  auto &self = *static_cast<UnityVulkanBridge *>(context);
  auto &slot = self.slots_[index];
  slot = {};
  if (!self.dispatch_.create_slot(self.dispatch_.context, index, self.device_,
                                  contract, self.selection_, slot) ||
      !HasRequiredObjects(slot, self.selection_.path)) {
    SyncFd empty;
    self.dispatch_.drain_slot(self.dispatch_.context, index, AhbSlotState::Free,
                              slot, empty);
    slot = {};
    return false;
  }
  return true;
}

void UnityVulkanBridge::DrainSlot(void *context, uint32_t index,
                                  AhbSlotState state, SlotResources &,
                                  SyncFd &fd) noexcept {
  auto &self = *static_cast<UnityVulkanBridge *>(context);
  self.dispatch_.drain_slot(self.dispatch_.context, index, state,
                            self.slots_[index], fd);
  self.slots_[index] = {};
}

BridgeResult
UnityVulkanBridge::Prepare(const HV_AndroidGpuSubmissionV1 &submission,
                           void **event_data) noexcept {
  if (event_data)
    *event_data = nullptr;
  if (!event_data || !initialized_.load(std::memory_order_acquire) ||
      submission.struct_size < sizeof(submission) ||
      submission.api_version != HV_ANDROID_GPU_API_V1 ||
      !submission.unity_texture ||
      submission.width != static_cast<int32_t>(contract_.width) ||
      submission.height != static_cast<int32_t>(contract_.height) ||
      submission.frame_id <= 0 || submission.timestamp_us < 0 ||
      submission.rotation_degrees != contract_.rotation ||
      (submission.mirrored != 0) != contract_.mirror)
    return BridgeResult::Invalid;
  SlotToken token;
  const BridgeResult result =
      MapReserve(ring_.Reserve(static_cast<uint64_t>(submission.frame_id),
                               submission.timestamp_us, token));
  if (result != BridgeResult::Ok)
    return result;
  auto &record = records_[token.index];
  record.token = token;
  record.unity_texture = submission.unity_texture;
  record.generation = token.generation;
  record.pending.store(true, std::memory_order_release);
  record.queue_pending.store(false, std::memory_order_release);
  record.texture_accessed.store(false, std::memory_order_release);
  submitted_frames_.fetch_add(1, std::memory_order_relaxed);
  *event_data = &record;
  return BridgeResult::Ok;
}

void UnityVulkanBridge::RetireWithoutSubmission(
    const SlotToken &token) noexcept {
  if (ring_.Transition(token, AhbSlotState::EventReserved,
                       AhbSlotState::DropDrain,
                       CompletionProof::GpuQuiescent) == SlotResult::Ok)
    ring_.Transition(token, AhbSlotState::DropDrain, AhbSlotState::Free,
                     CompletionProof::GpuQuiescent);
}

BridgeResult UnityVulkanBridge::Render(EventRecord *record) noexcept {
  if (!record || record->bridge != this ||
      !record->pending.exchange(false, std::memory_order_acq_rel))
    return initialized_.load(std::memory_order_acquire) ? BridgeResult::Invalid
                                                        : BridgeResult::Closed;
  if (!initialized_.load(std::memory_order_acquire) ||
      record->generation != ring_.Generation())
    return BridgeResult::Closed;

  if (!dispatch_.access_texture(dispatch_.context, record->unity_texture,
                                record->access)) {
    RetireWithoutSubmission(record->token);
    return BridgeResult::GpuError;
  }
  record->texture_accessed.store(true, std::memory_order_release);
  auto &slot = slots_[record->token.index];
  auto &barriers = record->barriers;
  const auto &access = record->access;
  barriers[0] = {access.image, access.layout,
                 selection_.path == HV_ANDROID_GPU_COPY_BLIT
                     ? BridgeImageLayout::TransferSource
                     : BridgeImageLayout::ShaderRead,
                 device_.graphics_queue_family, device_.graphics_queue_family};
  barriers[1] = {slot.image, BridgeImageLayout::External,
                 selection_.path == HV_ANDROID_GPU_COPY_BLIT
                     ? BridgeImageLayout::TransferDestination
                     : BridgeImageLayout::ColorAttachment,
                 kExternalQueueFamily, device_.graphics_queue_family};
  barriers[2] = {slot.image, barriers[1].new_layout,
                 BridgeImageLayout::External, device_.graphics_queue_family,
                 kExternalQueueFamily};
  barriers[3] = {access.image, barriers[0].new_layout, access.layout,
                 device_.graphics_queue_family, device_.graphics_queue_family};

  record->queue_pending.store(true, std::memory_order_release);
  if (!dispatch_.queue_access(dispatch_.context, &UnityVulkanBridge::QueueEvent,
                              record)) {
    record->queue_pending.store(false, std::memory_order_release);
    dispatch_.release_texture(dispatch_.context, record->unity_texture,
                              record->access);
    record->texture_accessed.store(false, std::memory_order_release);
    RetireWithoutSubmission(record->token);
    return BridgeResult::GpuError;
  }
  return BridgeResult::Ok;
}

BridgeResult UnityVulkanBridge::ExecuteQueue(EventRecord *record) noexcept {
  if (!record)
    return BridgeResult::Closed;
  if (!record->queue_pending.exchange(false, std::memory_order_acq_rel) ||
      !initialized_.load(std::memory_order_acquire) ||
      record->generation != ring_.Generation()) {
    if (record->texture_accessed.exchange(false, std::memory_order_acq_rel))
      dispatch_.release_texture(dispatch_.context, record->unity_texture,
                                record->access);
    return BridgeResult::Closed;
  }
  auto &slot = slots_[record->token.index];
  const bool recorded =
      selection_.path == HV_ANDROID_GPU_COPY_BLIT
          ? dispatch_.record_blit(dispatch_.context, slot, record->access,
                                  record->barriers.data(),
                                  record->barriers.size())
          : dispatch_.record_color(dispatch_.context, slot, record->access,
                                   record->barriers.data(),
                                   record->barriers.size(), true);
  if (!recorded) {
    dispatch_.release_texture(dispatch_.context, record->unity_texture,
                              record->access);
    record->texture_accessed.store(false, std::memory_order_release);
    RetireWithoutSubmission(record->token);
    return BridgeResult::GpuError;
  }
  if (ring_.Transition(record->token, AhbSlotState::EventReserved,
                       AhbSlotState::UnityCopySubmitted) != SlotResult::Ok ||
      !dispatch_.submit_signal(dispatch_.context, device_, slot) ||
      ring_.Transition(record->token, AhbSlotState::UnityCopySubmitted,
                       AhbSlotState::ProducerSignalPending) != SlotResult::Ok) {
    dispatch_.release_texture(dispatch_.context, record->unity_texture,
                              record->access);
    record->texture_accessed.store(false, std::memory_order_release);
    return BridgeResult::GpuError;
  }
  SyncFd fd;
  if (!dispatch_.export_sync_fd(dispatch_.context, device_, slot, fd) ||
      ring_.PublishReady(record->token, fd) != SlotResult::Ok) {
    dispatch_.release_texture(dispatch_.context, record->unity_texture,
                              record->access);
    record->texture_accessed.store(false, std::memory_order_release);
    return BridgeResult::GpuError;
  }
  imported_frames_.fetch_add(1, std::memory_order_relaxed);
  dispatch_.release_texture(dispatch_.context, record->unity_texture,
                            record->access);
  record->texture_accessed.store(false, std::memory_order_release);
  return BridgeResult::Ok;
}

void UnityVulkanBridge::GetStatus(
    HV_AndroidGpuBridgeStatusV1 &status) const noexcept {
  const uint32_t size = status.struct_size;
  const uint32_t version = status.api_version;
  status = {};
  status.struct_size = size;
  status.api_version = version;
  status.copy_path = initialized_.load(std::memory_order_acquire)
                         ? static_cast<uint32_t>(selection_.path)
                         : HV_ANDROID_GPU_COPY_UNAVAILABLE;
  status.ahb_format = contract_.actual_format;
  status.ahb_usage = contract_.actual_usage;
  const auto selected =
      std::find_if(selection_.candidates.begin(), selection_.candidates.end(),
                   [&](const AhbCandidate &candidate) {
                     return candidate.path == selection_.path;
                   });
  if (selected != selection_.candidates.end())
    status.ahb_format_features = selected->producer.format_features;
  status.submitted_frames = submitted_frames_.load(std::memory_order_relaxed);
  status.imported_frames = imported_frames_.load(std::memory_order_relaxed);
  const auto counters = ring_.Counters();
  status.dropped_no_slot = counters.no_slot_drops;
  status.dropped_generation = counters.generation_drops;
  std::copy(device_.device_uuid.begin(), device_.device_uuid.end(),
            status.unity_device_uuid);
  std::copy(device_.driver_uuid.begin(), device_.driver_uuid.end(),
            status.unity_driver_uuid);
}

void UnityVulkanBridge::RenderEvent(int, void *data) noexcept {
  auto *record = static_cast<EventRecord *>(data);
  if (record && record->bridge)
    record->bridge->Render(record);
}

void UnityVulkanBridge::QueueEvent(void *data) noexcept {
  auto *record = static_cast<EventRecord *>(data);
  if (record && record->bridge)
    record->bridge->ExecuteQueue(record);
}

} // namespace humanvision::gpu
