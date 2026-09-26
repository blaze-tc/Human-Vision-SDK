#include "gpu/android/unity_vulkan_bridge.h"

#include <algorithm>
#include <limits>

namespace humanvision::gpu {
namespace {
constexpr uint32_t kExternalQueueFamily = UINT32_MAX - 1u;

bool HasRequiredObjects(const UnityVulkanSlotCache& slot, HV_AndroidGpuCopyPath path) noexcept {
    const bool common = slot.ahb && slot.ahb_buffer && slot.image && slot.memory && slot.image_view &&
                        slot.command_buffer && slot.export_semaphore;
    if (!common) return false;
    if (path != HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT) return true;
    return slot.framebuffer && slot.sampler && slot.descriptor_set_layout &&
           slot.descriptor_set && slot.render_pass && slot.pipeline;
}
BridgeResult MapReserve(SlotResult result) noexcept {
    switch (result) {
    case SlotResult::Ok: return BridgeResult::Ok;
    case SlotResult::NoSlot: return BridgeResult::DroppedNoSlot;
    case SlotResult::Busy: return BridgeResult::Busy;
    case SlotResult::Closed: return BridgeResult::Closed;
    default: return BridgeResult::Invalid;
    }
}
}

std::atomic<UnityVulkanBridge*> UnityVulkanBridge::callback_bridge_{nullptr};

UnityVulkanBridge::UnityVulkanBridge(UnityVulkanBridgeDispatch dispatch) noexcept
    : dispatch_(dispatch), ring_({this, &UnityVulkanBridge::CreateSlot, &UnityVulkanBridge::DrainSlot}) {
    for (auto& record : records_) record.bridge = this;
    callback_bridge_.store(this, std::memory_order_release);
}
UnityVulkanBridge::~UnityVulkanBridge() {
    Shutdown();
    auto* expected = this;
    callback_bridge_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

bool UnityVulkanBridge::Enter() const noexcept {
    if (!accepting_calls_.load(std::memory_order_acquire)) return false;
    active_calls_.fetch_add(1, std::memory_order_acq_rel);
    if (accepting_calls_.load(std::memory_order_acquire)) return true;
    Leave();
    return false;
}
void UnityVulkanBridge::Leave() const noexcept {
    if (active_calls_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard<std::mutex> lock(active_mutex_);
        active_cv_.notify_all();
    }
}
void* UnityVulkanBridge::EncodeIdentity(uint32_t index, uint64_t reservation) noexcept {
    constexpr unsigned kIndexBits = 2;
    const uintptr_t value = (static_cast<uintptr_t>(reservation) << kIndexBits) | (index + 1u);
    return reinterpret_cast<void*>(value);
}
bool UnityVulkanBridge::DecodeIdentity(void* identity, uint32_t& index, uint64_t& reservation) const noexcept {
    constexpr unsigned kIndexBits = 2;
    const uintptr_t value = reinterpret_cast<uintptr_t>(identity);
    const uintptr_t encoded_index = value & ((1u << kIndexBits) - 1u);
    if (encoded_index == 0 || encoded_index > AhbSlotRing::kSlotCount) return false;
    index = static_cast<uint32_t>(encoded_index - 1u);
    reservation = static_cast<uint64_t>(value >> kIndexBits);
    return reservation != 0;
}

bool UnityVulkanBridge::Initialize(const UnityVulkanDeviceContext& device,
                                   const AhbSelection& selection,
                                   const SlotContract& contract) noexcept {
    std::lock_guard<std::mutex> control(control_mutex_);
    if (quarantined_.load(std::memory_order_acquire)) return false;
    ShutdownLocked();
    // The worker can discover an unproved GPU completion while shutdown waits
    // for its lease. Once all calls/leases have returned, admission stays closed;
    // recheck the terminal latch before allocating or reopening a generation.
    if (quarantined_.load(std::memory_order_acquire)) return false;
    if (!dispatch_.create_slot || !dispatch_.drain_slot || !dispatch_.access_texture ||
        !dispatch_.prepare_source || !dispatch_.release_texture ||
        !dispatch_.record_blit || !dispatch_.record_color ||
        !dispatch_.queue_access || !dispatch_.submit_signal || !dispatch_.export_sync_fd ||
        !dispatch_.submission_complete || !dispatch_.cancel_and_drain_events ||
        !device.instance || !device.physical_device || !device.device || !device.graphics_queue ||
        !contract.width || !contract.height || selection.path == HV_ANDROID_GPU_COPY_UNAVAILABLE ||
        selection.contract.width != contract.width || selection.contract.height != contract.height ||
        selection.contract.format != contract.actual_format || selection.contract.usage != contract.actual_usage)
        return false;
    const AhbSelection audited = SelectAhbCopyPath(selection.candidates);
    if (audited.path != selection.path || audited.contract.width != selection.contract.width ||
        audited.contract.height != selection.contract.height || audited.contract.format != selection.contract.format ||
        audited.contract.usage != selection.contract.usage) return false;
    const auto nonzero=[](const auto& uuid){return std::any_of(uuid.begin(),uuid.end(),[](uint8_t b){return b!=0;});};
    const auto& source=selection.source;
    if (!selection.producer_identity.queried || !selection.consumer_identity.queried ||
        !nonzero(device.device_uuid) || !nonzero(device.driver_uuid) ||
        device.device_uuid!=selection.producer_identity.device_uuid ||
        device.driver_uuid!=selection.producer_identity.driver_uuid ||
        device.device_uuid!=selection.consumer_identity.device_uuid ||
        device.driver_uuid!=selection.consumer_identity.driver_uuid ||
        !source.width || !source.height || (source.format!=37u && source.format!=44u) ||
        source.samples!=1u || source.layers!=1u || source.image_type!=1u || source.tiling>1u)
        return false;

    device_ = device;
    selection_ = selection;
    contract_ = contract;
    submitted_frames_.store(0, std::memory_order_relaxed);
    imported_frames_.store(0, std::memory_order_relaxed);
    last_error_.store(0, std::memory_order_relaxed);
    source_contract_signature_.store(0, std::memory_order_relaxed);
    if (!ring_.Reconfigure(contract_)) {
        for (uint32_t i = 0; i < slots_.size(); ++i) {
            if (!slots_[i].ahb) continue;
            SyncFd empty;
            dispatch_.drain_slot(dispatch_.context, i, AhbSlotState::Free, slots_[i], empty);
            slots_[i] = {};
        }
        selection_ = {}; contract_ = {}; device_ = {};
        return false;
    }
    for (uint32_t i = 0; i < records_.size(); ++i) {
        auto& record = records_[i];
        record.token = {}; record.unity_texture = nullptr; record.generation = ring_.Generation();
        record.reservation_id = 0; record.pending.store(false); record.queue_pending.store(false);
        record.texture_accessed.store(false); recovery_[i].kind.store(0); recovery_[i].fd.Reset();
    }
    initialized_.store(true, std::memory_order_release);
    accepting_calls_.store(true, std::memory_order_release);
    return true;
}

void UnityVulkanBridge::Shutdown() noexcept {
    std::lock_guard<std::mutex> control(control_mutex_);
    ShutdownLocked();
}

void UnityVulkanBridge::ShutdownLocked() noexcept {
    bool has_resources = false;
    for (const auto& slot : slots_) has_resources = has_resources || slot.ahb != 0;
    if (!initialized_.load(std::memory_order_acquire) && !accepting_calls_.load(std::memory_order_acquire) &&
        active_calls_.load(std::memory_order_acquire) == 0 &&
        consumer_leases_.load(std::memory_order_acquire) == 0 && !has_resources) return;
    accepting_calls_.store(false, std::memory_order_release);
    initialized_.store(false, std::memory_order_release);
    for (auto& record : records_) record.pending.store(false, std::memory_order_release);
    if (dispatch_.cancel_and_drain_events) dispatch_.cancel_and_drain_events(dispatch_.context);
    {
        std::unique_lock<std::mutex> lock(active_mutex_);
        active_cv_.wait(lock, [&] { return active_calls_.load(std::memory_order_acquire) == 0 &&
            consumer_leases_.load(std::memory_order_acquire) == 0; });
    }
    for (auto& record : records_) record.queue_pending.store(false, std::memory_order_release);
    ring_.Shutdown();
    for (auto& recovery : recovery_) { recovery.kind.store(0); recovery.fd.Reset(); }
    selection_ = {}; contract_ = {}; device_ = {};
}

bool UnityVulkanBridge::CreateSlot(void* context, uint32_t index, const SlotContract& contract,
                                   SlotResources&) noexcept {
    auto& self = *static_cast<UnityVulkanBridge*>(context);
    auto& slot = self.slots_[index]; slot = {};
    if (!self.dispatch_.create_slot(self.dispatch_.context, index, self.device_, contract,
                                    self.selection_, slot) || !HasRequiredObjects(slot, self.selection_.path)) {
        SyncFd empty;
        self.dispatch_.drain_slot(self.dispatch_.context, index, AhbSlotState::Free, slot, empty);
        slot = {};
        return false;
    }
    return true;
}
void UnityVulkanBridge::DrainSlot(void* context, uint32_t index, AhbSlotState state,
                                  SlotResources&, SyncFd& fd) noexcept {
    auto& self = *static_cast<UnityVulkanBridge*>(context);
    if (self.quarantined_.load(std::memory_order_acquire)) {
        // Process-lifetime retention: ahb is the AndroidSlot heap owner, which
        // holds the allocation/importer AHB refs and every producer Vulkan
        // object (including source views). Never dispatch its drain/destructor:
        // it may wait forever or free memory still used by the consumer device.
        // This ring's SlotResources are empty (CreateSlot uses slots_ only).
        // Ring teardown still closes outstanding sync fds exactly once; closing
        // an fd is not GPU completion proof and does not release the AHB owners.
        self.slots_[index] = {};
        return;
    }
    self.dispatch_.drain_slot(self.dispatch_.context, index, state, self.slots_[index], fd);
    self.slots_[index] = {};
}

void UnityVulkanBridge::Recover() noexcept {
    for (uint32_t i = 0; i < recovery_.size(); ++i) {
        auto& r = recovery_[i];
        uint8_t kind = r.kind.load(std::memory_order_acquire);
        if (!kind || kind == 0xffu ||
            !r.kind.compare_exchange_strong(kind, 0xffu,
                                            std::memory_order_acq_rel))
            continue;
        if (kind == 3) {
            const SlotToken token = r.token;
            SyncFd fd = std::move(r.fd);
            r.kind.store(0, std::memory_order_release);
            const SlotResult published = ring_.PublishSubmitted(token, fd);
            if (published == SlotResult::Ok) {
                imported_frames_.fetch_add(1);
            } else if (published == SlotResult::Busy) {
                r.fd = std::move(fd);
                r.kind.store(3, std::memory_order_release);
            } else {
                if (published != SlotResult::Closed) last_error_.store(7);
                r.fd = std::move(fd);
                r.kind.store(2, std::memory_order_release);
            }
            continue;
        }
        if (kind == 2 &&
            !dispatch_.submission_complete(dispatch_.context, device_, slots_[i])) {
            r.kind.store(2, std::memory_order_release);
            continue;
        }
        SlotSnapshot snapshot;
        if (ring_.Inspect(i, snapshot) != SlotResult::Ok) {
            r.kind.store(kind, std::memory_order_release);
            continue;
        }
        const SlotResult drop = snapshot.state == AhbSlotState::DropDrain ? SlotResult::Ok :
            ring_.Transition(r.token, snapshot.state, AhbSlotState::DropDrain,
                             CompletionProof::GpuQuiescent);
        if (drop == SlotResult::Busy) {
            r.kind.store(kind, std::memory_order_release);
            continue;
        }
        const SlotToken token = r.token;
        r.fd.Reset();
        r.kind.store(0, std::memory_order_release);
        if (drop != SlotResult::Ok || ring_.Transition(token, AhbSlotState::DropDrain,
             AhbSlotState::Free, CompletionProof::GpuQuiescent) != SlotResult::Ok) {
            r.kind.store(kind, std::memory_order_release);
        }
    }
}

BridgeResult UnityVulkanBridge::Prepare(const HV_AndroidGpuSubmissionV1& submission,
                                        void** event_data) noexcept {
    if (event_data) *event_data = nullptr;
    if (!event_data || submission.struct_size < sizeof(submission) ||
        submission.api_version != HV_ANDROID_GPU_API_V1 || !submission.unity_texture ||
        submission.width <= 0 || submission.height <= 0 || submission.frame_id <= 0 ||
        submission.timestamp_us < 0 || (submission.rotation_degrees != 0 &&
        submission.rotation_degrees != 90 && submission.rotation_degrees != 180 &&
        submission.rotation_degrees != 270) || submission.mirrored > 1) return BridgeResult::Invalid;
    if (!Enter()) return BridgeResult::Closed;
    struct Guard { const UnityVulkanBridge* b; ~Guard(){b->Leave();} } guard{this};
    Recover();
    if (submission.rotation_degrees != contract_.rotation ||
        (submission.mirrored != 0) != contract_.mirror) return BridgeResult::Invalid;
    SlotToken token;
    const BridgeResult result = MapReserve(ring_.Reserve(static_cast<uint64_t>(submission.frame_id),
                                                         submission.timestamp_us, token));
    if (result != BridgeResult::Ok) return result;
    auto& record = records_[token.index];
    const uint64_t reservation = next_reservation_.fetch_add(1, std::memory_order_relaxed);
    record.token = token; record.unity_texture = submission.unity_texture;
    record.submitted_width = static_cast<uint32_t>(submission.width);
    record.submitted_height = static_cast<uint32_t>(submission.height);
    record.generation = token.generation;
    record.queue_pending.store(false); record.texture_accessed.store(false);
    record.pending.store(true, std::memory_order_release);
    record.reservation_id.store(reservation, std::memory_order_release);
    submitted_frames_.fetch_add(1, std::memory_order_relaxed);
    *event_data = EncodeIdentity(token.index, reservation);
    return BridgeResult::Ok;
}
void UnityVulkanBridge::RetireWithoutSubmission(const SlotToken& token) noexcept {
    auto& recovery = recovery_[token.index]; recovery.token = token; recovery.kind.store(1, std::memory_order_release);
    Recover();
}

BridgeResult UnityVulkanBridge::Render(void* identity) noexcept {
    if (!Enter()) return BridgeResult::Closed;
    struct Guard { const UnityVulkanBridge* b; ~Guard(){b->Leave();} } guard{this};
    uint32_t index; uint64_t reservation;
    if (!DecodeIdentity(identity, index, reservation)) return BridgeResult::Invalid;
    auto& record = records_[index];
    if (!record.reservation_id.compare_exchange_strong(reservation, 0, std::memory_order_acq_rel) ||
        !record.pending.exchange(false, std::memory_order_acq_rel))
        return BridgeResult::Closed;
    if (record.generation != ring_.Generation()) return BridgeResult::Closed;
    if (!dispatch_.access_texture(dispatch_.context, record.unity_texture, record.access)) {
        last_error_.store(1);
        RetireWithoutSubmission(record.token); return BridgeResult::GpuError;
    }
    const auto& a = record.access;
    const auto& measured = selection_.source;
    const bool source_valid = a.image && a.width == record.submitted_width &&
        a.height == record.submitted_height && (a.format == 37u || a.format == 44u) &&
        a.samples == 1u && a.image_type == 1u && a.layers == 1u &&
        a.width == measured.width && a.height == measured.height && a.format == measured.format &&
        a.usage == measured.usage && a.samples == measured.samples && a.image_type == measured.image_type &&
        a.tiling == measured.tiling && a.layers == measured.layers &&
        (selection_.path == HV_ANDROID_GPU_COPY_BLIT ? (a.usage & 1u) != 0 : (a.usage & 4u) != 0);
    uint64_t signature = a.width;
    signature = signature * 1099511628211ull ^ a.height;
    signature = signature * 1099511628211ull ^ a.format;
    signature = signature * 1099511628211ull ^ a.usage;
    signature = signature * 1099511628211ull ^ a.samples;
    signature = signature * 1099511628211ull ^ a.image_type;
    uint64_t expected_signature = 0;
    if (!source_valid || (!source_contract_signature_.compare_exchange_strong(
            expected_signature, signature, std::memory_order_acq_rel) && expected_signature != signature)) {
        dispatch_.release_texture(dispatch_.context, record.unity_texture, record.access);
        RetireWithoutSubmission(record.token);
        last_error_.store(2);
        CloseAdmission();
        return BridgeResult::Closed;
    }
    const SourcePreparation prepared =
        dispatch_.prepare_source(dispatch_.context, slots_[index], record.access);
    if (prepared != SourcePreparation::Ready) {
        dispatch_.release_texture(dispatch_.context, record.unity_texture,
                                  record.access);
        RetireWithoutSubmission(record.token);
        if (prepared == SourcePreparation::Unsupported) { last_error_.store(3); CloseAdmission(); }
        return prepared == SourcePreparation::Warmed ? BridgeResult::Busy
                                                      : BridgeResult::GpuError;
    }
    record.texture_accessed.store(true, std::memory_order_release);
    auto& barriers = record.barriers; const auto& access = record.access; auto& slot = slots_[index];
    barriers[0] = {access.image, access.layout, selection_.path == HV_ANDROID_GPU_COPY_BLIT ?
        BridgeImageLayout::TransferSource : BridgeImageLayout::ShaderRead,
        device_.graphics_queue_family, device_.graphics_queue_family};
    barriers[1] = {slot.image, BridgeImageLayout::External, selection_.path == HV_ANDROID_GPU_COPY_BLIT ?
        BridgeImageLayout::TransferDestination : BridgeImageLayout::ColorAttachment,
        kExternalQueueFamily, device_.graphics_queue_family};
    barriers[2] = {slot.image, barriers[1].new_layout, BridgeImageLayout::External,
        device_.graphics_queue_family, kExternalQueueFamily};
    barriers[3] = {access.image, barriers[0].new_layout, access.layout,
        device_.graphics_queue_family, device_.graphics_queue_family};
    if (!accepting_calls_.load(std::memory_order_acquire)) {
        dispatch_.release_texture(dispatch_.context, record.unity_texture, record.access);
        record.texture_accessed.store(false); RetireWithoutSubmission(record.token); return BridgeResult::Closed;
    }
    record.queue_pending.store(true, std::memory_order_release);
    if (!dispatch_.queue_access(dispatch_.context, &UnityVulkanBridge::QueueEvent, &record)) {
        last_error_.store(4);
        record.queue_pending.store(false); dispatch_.release_texture(dispatch_.context, record.unity_texture, record.access);
        record.texture_accessed.store(false); RetireWithoutSubmission(record.token); return BridgeResult::GpuError;
    }
    return BridgeResult::Ok;
}

NcnnRoleStart NextNcnnRole(const ConsumerFrame& frame) noexcept {
    if (!frame.claimed || frame.role_owner || frame.complete_role) return NcnnRoleStart::Invalid;
    if (frame.producer_fd.HasPayload())
        return frame.ncnn_role_complete ? NcnnRoleStart::Invalid : NcnnRoleStart::WaitForProducer;
    return frame.ncnn_role_complete ? NcnnRoleStart::AcquireAfterPriorRole : NcnnRoleStart::Invalid;
}

bool CompleteGpuRole(ConsumerFrame& frame, bool final_role, std::string& error) noexcept {
    if (!frame.claimed || !frame.role_owner || !frame.complete_role) {
        error = "GPU observation has no active model role"; return false;
    }
    auto* owner = frame.role_owner;
    const auto complete = frame.complete_role;
    frame.role_owner = nullptr;
    frame.complete_role = nullptr;
    return complete(owner, frame, final_role, error);
}

SlotResult RetireUnsubmittedConsumer(UnityVulkanBridge& bridge, ConsumerFrame& frame,
                                     ProducerProofWait wait, void* context) noexcept {
    if (!frame.claimed || !wait) return SlotResult::Invalid;
    if (frame.role_owner) {
        std::string error;
        if (CompleteGpuRole(frame, true, error) && !frame.claimed) return SlotResult::Ok;
        if (frame.claimed) bridge.QuarantineConsumer(frame);
        return SlotResult::Closed;
    }
    const bool producer_complete = frame.producer_fd.HasPayload()
        ? wait(context, frame.producer_fd)
        : frame.ncnn_role_complete;
    if (!producer_complete) {
        bridge.QuarantineConsumer(frame);
        return SlotResult::Closed;
    }
    const auto retired = bridge.RetireConsumer(frame, CompletionProof::GpuQuiescent);
    if (retired != SlotResult::Ok && frame.claimed) bridge.QuarantineConsumer(frame);
    return retired;
}

SlotResult UnityVulkanBridge::ClaimConsumer(ConsumerFrame& frame) noexcept {
    if (frame.claimed || frame.producer_fd.HasPayload()) return SlotResult::Invalid;
    if (!Enter()) return SlotResult::Closed;
    struct Guard { const UnityVulkanBridge* b; ~Guard(){b->Leave();} } guard{this};
    Recover();
    SlotToken token;
    SlotMetadata metadata;
    const auto claimed = ring_.ClaimNewest(token, metadata);
    if (claimed != SlotResult::Ok) return claimed;
    SyncFd fd;
    SlotResult taken;
    do { taken = ring_.TakeProducerFence(token, fd); } while (taken == SlotResult::Busy);
    if (taken != SlotResult::Ok) return taken;
    const auto ahb = slots_[token.index].ahb_buffer;
    // Slot creation requires the borrowed buffer handle. It stays alive until
    // this consumer lease has been retired and shutdown drains the generation.
    frame.token = token;
    frame.metadata = metadata;
    frame.ahb_buffer = ahb;
    frame.producer_fd = std::move(fd);
    frame.claimed = true;
    frame.ncnn_role_complete = false;
    frame.role_owner = nullptr; frame.complete_role = nullptr;
    consumer_leases_.fetch_add(1, std::memory_order_acq_rel);
    return SlotResult::Ok;
}

SlotResult UnityVulkanBridge::RetireConsumer(ConsumerFrame& frame, CompletionProof proof) noexcept {
    if (!frame.claimed || proof != CompletionProof::GpuQuiescent) return SlotResult::Invalid;
    const auto result = ring_.RetireConsumer(frame.token, proof);
    if (result != SlotResult::Ok) return result;
    frame.producer_fd.Reset();
    frame.ahb_buffer = 0;
    frame.claimed = false;
    frame.ncnn_role_complete = false;
    frame.role_owner = nullptr; frame.complete_role = nullptr;
    if (consumer_leases_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard<std::mutex> lock(active_mutex_);
        active_cv_.notify_all();
    }
    return SlotResult::Ok;
}

SlotResult UnityVulkanBridge::QuarantineConsumer(ConsumerFrame& frame) noexcept {
    if (!frame.claimed) return SlotResult::Invalid;
    accepting_calls_.store(false, std::memory_order_release);
    quarantined_.store(true, std::memory_order_release);
    frame.producer_fd.Reset();
    frame.ahb_buffer = 0;
    frame.claimed = false;
    frame.ncnn_role_complete = false;
    frame.role_owner = nullptr; frame.complete_role = nullptr;
    if (consumer_leases_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard<std::mutex> lock(active_mutex_);
        active_cv_.notify_all();
    }
    return SlotResult::Ok;
}

SlotResult UnityVulkanBridge::ClaimDropped(ConsumerFrame& frame) noexcept {
    if (frame.claimed || frame.producer_fd.HasPayload()) return SlotResult::Invalid;
    if (!Enter()) return SlotResult::Closed;
    struct Guard { const UnityVulkanBridge* b; ~Guard(){b->Leave();} } guard{this};
    SlotToken token; SlotMetadata metadata; SyncFd fd;
    const auto result = ring_.ClaimDropped(token, metadata, fd);
    if (result != SlotResult::Ok) return result;
    frame.token = token; frame.metadata = metadata;
    frame.ahb_buffer = slots_[token.index].ahb_buffer;
    frame.producer_fd = std::move(fd); frame.claimed = true;
    frame.ncnn_role_complete = false;
    frame.role_owner = nullptr; frame.complete_role = nullptr;
    consumer_leases_.fetch_add(1, std::memory_order_acq_rel);
    return SlotResult::Ok;
}

SlotResult UnityVulkanBridge::RetainGeneration(ConsumerGeneration& generation,
                                               void (*retain_ahb)(uintptr_t) noexcept) noexcept {
    if (!retain_ahb) return SlotResult::Invalid;
    if (!Enter()) return SlotResult::Closed;
    struct Guard { const UnityVulkanBridge* b; ~Guard(){b->Leave();} } guard{this};
    ConsumerGeneration result;
    result.generation = ring_.Generation();
    result.contract = contract_;
    for (uint32_t i = 0; i < slots_.size(); ++i) {
        const auto ahb = slots_[i].ahb_buffer;
        if (!ahb) return SlotResult::Invalid;
        result.retained_ahb[i] = ahb;
    }
    for (const auto ahb : result.retained_ahb) retain_ahb(ahb);
    generation = result;
    return SlotResult::Ok;
}

BridgeResult UnityVulkanBridge::ExecuteQueue(EventRecord* record) noexcept {
    if (!record) return BridgeResult::Closed;
    const auto release = [&] {
        if (record->texture_accessed.exchange(false, std::memory_order_acq_rel))
            dispatch_.release_texture(dispatch_.context, record->unity_texture, record->access);
    };
    if (!record->queue_pending.exchange(false, std::memory_order_acq_rel) ||
        !accepting_calls_.load(std::memory_order_acquire) || record->generation != ring_.Generation()) {
        release();
        return BridgeResult::Closed;
    }
    auto& slot = slots_[record->token.index];
    const bool recorded = selection_.path == HV_ANDROID_GPU_COPY_BLIT ?
        dispatch_.record_blit(dispatch_.context, slot, record->access, record->barriers.data(), 4) :
        dispatch_.record_color(dispatch_.context, slot, record->access, record->barriers.data(), 4, true);
    if (!recorded) {
        last_error_.store(5);
        release();
        recovery_[record->token.index].token = record->token; recovery_[record->token.index].kind.store(1);
        return BridgeResult::GpuError;
    }
    if (!dispatch_.submit_signal(dispatch_.context, device_, slot)) {
        last_error_.store(6);
        release();
        recovery_[record->token.index].token = record->token; recovery_[record->token.index].kind.store(1);
        return BridgeResult::GpuError;
    }
    auto& recovery = recovery_[record->token.index]; recovery.token = record->token;
    SyncFd fd;
    if (!dispatch_.export_sync_fd(dispatch_.context, device_, slot, fd)) {
        last_error_.store(8);
        release(); recovery.kind.store(2); return BridgeResult::GpuError;
    }
    // No callback reads mutable event/source data after publication.
    release();
    const SlotToken token = record->token;
    const SlotResult published = ring_.PublishSubmitted(token, fd);
    if (published == SlotResult::Ok) { last_error_.store(0); imported_frames_.fetch_add(1); return BridgeResult::Ok; }
    recovery.fd = std::move(fd);
    if (published == SlotResult::Busy) {
        recovery.kind.store(3, std::memory_order_release);
        return BridgeResult::Busy;
    }
    if (published != SlotResult::Closed) last_error_.store(7);
    recovery.kind.store(2, std::memory_order_release);
    return published == SlotResult::Closed ? BridgeResult::Closed : BridgeResult::GpuError;
}

void UnityVulkanBridge::GetStatus(HV_AndroidGpuBridgeStatusV1& status) const noexcept {
    const uint32_t size=status.struct_size, version=status.api_version; status={}; status.struct_size=size; status.api_version=version;
    if (!Enter()) { status.copy_path=HV_ANDROID_GPU_COPY_UNAVAILABLE; return; }
    struct Guard { const UnityVulkanBridge* b; ~Guard(){b->Leave();} } guard{this};
    status.copy_path=static_cast<uint32_t>(selection_.path); status.ahb_format=contract_.actual_format; status.ahb_usage=contract_.actual_usage;
    const auto selected=std::find_if(selection_.candidates.begin(),selection_.candidates.end(),[&](const AhbCandidate& c){return c.path==selection_.path;});
    if(selected!=selection_.candidates.end())status.ahb_format_features=selected->producer.format_features;
    status.submitted_frames=submitted_frames_.load();status.imported_frames=imported_frames_.load();const auto counters=ring_.Counters();
    status.dropped_no_slot=counters.no_slot_drops;status.dropped_generation=counters.generation_drops;
    std::copy(device_.device_uuid.begin(),device_.device_uuid.end(),status.unity_device_uuid);
    std::copy(device_.driver_uuid.begin(),device_.driver_uuid.end(),status.unity_driver_uuid);
    std::copy(selection_.consumer_identity.device_uuid.begin(),selection_.consumer_identity.device_uuid.end(),status.ncnn_device_uuid);
    std::copy(selection_.consumer_identity.driver_uuid.begin(),selection_.consumer_identity.driver_uuid.end(),status.ncnn_driver_uuid);
    if(last_error_.load(std::memory_order_acquire))status.copy_path=HV_ANDROID_GPU_COPY_UNAVAILABLE;
}
const char* UnityVulkanBridge::Diagnostic() const noexcept {
    if (IsQuarantined())
        return "GPU completion is unproven; retain the Unity source texture, stop GPU use and restart the process";
    switch(last_error_.load(std::memory_order_acquire)) {
    case 1:return "Unity Vulkan AccessTexture failed";
    case 2:return "Unity Vulkan source differs from the measured source contract; rebuild the generation";
    case 3:return "Unity Vulkan source cache cannot admit this image; rebuild the generation";
    case 4:return "Unity Vulkan queue admission is closed";
    case 5:return "Unity Vulkan copy command recording failed; reservation retained for recovery";
    case 6:return "Unity Vulkan queue submission failed; reservation retained for recovery";
    case 7:return "Unity Vulkan slot publication rejected an invalid state; GPU completion recovery pending";
    case 8:return "Unity Vulkan sync-fd export failed; slot and semaphore retained until safe recovery";
    default:return "";
    }
}
void UnityVulkanBridge::RenderEvent(int, void* data) noexcept { if(auto* bridge=callback_bridge_.load(std::memory_order_acquire))bridge->Render(data); }
void UnityVulkanBridge::QueueEvent(void* data) noexcept { auto* r=static_cast<EventRecord*>(data);if(r&&r->bridge)r->bridge->ExecuteQueue(r); }
}
