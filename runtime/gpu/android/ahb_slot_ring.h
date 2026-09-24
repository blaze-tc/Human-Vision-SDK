#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace humanvision::gpu {
// Revision 2 protocol. Intermediate states distinguish GPU submission from
// export/import and from the fence which proves safe reuse.
enum class AhbSlotState : uint8_t {
    Free, EventReserved, UnityCopySubmitted, ProducerSignalPending,
    ReadyForNcnn, InferenceRunning, ConsumerReleasePending, DropDrain, Count
};
enum class CompletionProof { None, GpuQuiescent };
enum class SlotResult { Ok, Busy, Closed, NoSlot, Invalid, NoReady };
enum class CallContext { Control, Render };
enum class SyncPayloadState { Empty, OwnedFd, AlreadySignaled };

class SyncFd {
public:
    using Closer = void (*)(void*, int) noexcept;
    SyncFd() noexcept = default;
    // Explicit -1 is a present, already-signaled SYNC_FD payload. A default or
    // moved-from object is Empty even though Get() also returns -1. Values below
    // -1 are rejected as Empty. Import/wait and ownership barriers are still
    // required for AlreadySignaled; only OS descriptor close is omitted.
    explicit SyncFd(int fd) noexcept;
    SyncFd(int fd, Closer closer, void* context) noexcept;
    ~SyncFd();
    SyncFd(const SyncFd&) = delete;
    SyncFd& operator=(const SyncFd&) = delete;
    SyncFd(SyncFd&& other) noexcept;
    SyncFd& operator=(SyncFd&& other) noexcept;
    int Get() const noexcept { return fd_; }
    SyncPayloadState State() const noexcept { return state_; }
    bool HasPayload() const noexcept { return state_ != SyncPayloadState::Empty; }
    // Call only after successful Vulkan import of a present payload: ownership
    // transfers to Vulkan and presence becomes Empty, including for sentinel -1.
    int Release() noexcept;
    void Reset() noexcept;
private:
    int fd_ = -1;
    SyncPayloadState state_ = SyncPayloadState::Empty;
    Closer closer_ = nullptr;
    void* context_ = nullptr;
};

class OwnedSlotResource {
public:
    using Deleter = void (*)(void*, uintptr_t) noexcept;
    OwnedSlotResource() noexcept = default;
    OwnedSlotResource(uintptr_t handle, Deleter deleter, void* context) noexcept;
    ~OwnedSlotResource();
    OwnedSlotResource(const OwnedSlotResource&) = delete;
    OwnedSlotResource& operator=(const OwnedSlotResource&) = delete;
    OwnedSlotResource(OwnedSlotResource&& other) noexcept;
    OwnedSlotResource& operator=(OwnedSlotResource&& other) noexcept;
    uintptr_t Get() const noexcept { return handle_; }
    void Reset() noexcept;
private:
    uintptr_t handle_ = 0;
    Deleter deleter_ = nullptr;
    void* context_ = nullptr;
};

// Opaque owned handles allow B4/B5 to install Vulkan/ncnn objects without a
// dependency on either API here. These holders are stable for a whole generation.
// Factories take long-lived AHB owner references; frames take no AHB references.
struct SlotResources {
    OwnedSlotResource ahb_allocation, unity_ahb_lease, ncnn_ahb_lease;
    OwnedSlotResource unity_memory, unity_image, unity_view, unity_framebuffer;
    OwnedSlotResource unity_command_pool, unity_command_buffer, unity_fence, unity_export_semaphore;
    OwnedSlotResource ncnn_allocator, ncnn_image_mat, ncnn_import_pipeline;
    OwnedSlotResource ncnn_import_semaphore, ncnn_command_pool, ncnn_ownership_command;
    OwnedSlotResource ncnn_completion, ncnn_preprocessing_buffers;
    ~SlotResources();
    void Reset() noexcept;
};
struct SlotContract {
    uint32_t width = 0, height = 0, actual_format = 0, rotation = 0;
    uint64_t actual_usage = 0, camera_session = 0;
    bool mirror = false;
    std::array<uint8_t, 32> input_contract_hash{};
    bool operator==(const SlotContract& other) const noexcept;
};
struct SlotToken {
    uint32_t index = 3;
    uint64_t generation = 0, frame_id = 0;
};
struct SlotMetadata {
    uint64_t generation = 0, frame_id = 0;
    int64_t timestamp_us = 0;
};
struct SlotSnapshot { AhbSlotState state = AhbSlotState::Free; SlotMetadata metadata; };
struct SlotCounters { uint64_t no_slot_drops = 0, generation_drops = 0; };

// Hooks run only under serialized control-thread rebuild/teardown. Drain must
// cancel queued render events and prove BOTH devices finished touching resources
// before returning; it may consume/transfer fd. It must not call back into ring.
struct SlotLifecycle {
    void* context = nullptr;
    bool (*create)(void*, uint32_t, const SlotContract&, SlotResources&) noexcept = nullptr;
    void (*drain)(void*, uint32_t, AhbSlotState, SlotResources&, SyncFd&) noexcept = nullptr;
};

class AhbSlotRing {
public:
    static constexpr uint32_t kSlotCount = 3;
    explicit AhbSlotRing(SlotLifecycle lifecycle) noexcept;
    ~AhbSlotRing(); // Must be called on control thread after external callers join.
    AhbSlotRing(const AhbSlotRing&) = delete;
    AhbSlotRing& operator=(const AhbSlotRing&) = delete;
    bool Reconfigure(const SlotContract&, CallContext = CallContext::Control);
    bool Shutdown(CallContext = CallContext::Control);
    SlotResult Reserve(uint64_t frame_id, int64_t timestamp_us, SlotToken& token);
    SlotResult Transition(const SlotToken&, AhbSlotState from, AhbSlotState to,
                          CompletionProof = CompletionProof::None);
    // Failed publication leaves fd owned by the caller for retry/error cleanup.
    SlotResult PublishReady(const SlotToken&, SyncFd& fd);
    SlotResult ClaimNewest(SlotToken& token, SlotMetadata& metadata);
    // Native worker takes a superseded ready frame's fence for a GPU-only
    // ownership drain before its AHB can be reused.
    SlotResult ClaimDropped(SlotToken& token, SlotMetadata& metadata, SyncFd& fd);
    SlotResult TakeProducerFence(const SlotToken&, SyncFd& fd);
    // A claimed consumer may finish while control admission is closed. The
    // caller supplies GPU completion proof before the slot can be recycled.
    SlotResult RetireConsumer(const SlotToken&, CompletionProof);
    SlotResult Inspect(uint32_t index, SlotSnapshot& snapshot) const;
    SlotCounters Counters() const noexcept;
    uint64_t Generation() const noexcept { return generation_.load(std::memory_order_acquire); }
    static bool IsLegalTransition(AhbSlotState from, AhbSlotState to,
                                 CompletionProof = CompletionProof::None) noexcept;
private:
    struct Slot {
        std::atomic<AhbSlotState> state{AhbSlotState::Free};
        SlotMetadata metadata;
        SlotResources resources;
        SyncFd producer_fd;
    };
    bool Matches(const SlotToken&) const noexcept;
    void DrainLocked() noexcept;
    void RecycleLocked(Slot&) noexcept;
    std::array<Slot, kSlotCount> slots_;
    SlotLifecycle lifecycle_;
    // Render/main/worker methods use try_lock, never wait. Control methods alone
    // may block. This also serializes token validation with metadata publication,
    // preventing an ABA slot reuse from racing a stale token.
    mutable std::mutex mutex_;
    std::mutex lifecycle_mutex_;
    std::atomic<bool> accepting_{false};
    std::atomic<uint64_t> generation_{0}, no_slot_drops_{0}, generation_drops_{0};
    SlotContract contract_;
    bool initialized_ = false;
    uint64_t last_reserved_frame_ = 0, last_claimed_frame_ = 0;
};
}
