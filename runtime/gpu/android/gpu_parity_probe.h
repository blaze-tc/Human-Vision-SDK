#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace humanvision::gpu {

class ParityControlEpoch {
public:
    // Even values identify pending sessions; the low bit marks completion.
    // Starting a new session atomically invalidates every earlier completion.
    uint64_t Begin() noexcept {
        auto previous=state_.load(std::memory_order_acquire);
        uint64_t next;
        do { next=(previous&~uint64_t{1})+2; }
        while(!state_.compare_exchange_weak(previous,next,std::memory_order_acq_rel));
        return next;
    }
    bool Complete(uint64_t epoch) noexcept {
        if(!epoch || (epoch&1)) return false;
        return state_.compare_exchange_strong(epoch,epoch|1,std::memory_order_acq_rel);
    }
    bool IsComplete() const noexcept { return (state_.load(std::memory_order_acquire)&1)!=0; }
private:
    std::atomic<uint64_t> state_{0};
};

enum class ParityStage : uint8_t { Source, Producer, ImportedRgb, Normalized, Packed };
enum class ParityDtype : uint8_t { Unorm8, Fp16, Fp32 };
enum class ParityIdentity : uint8_t { Production, IndependentGolden };

constexpr uint32_t kParitySampleCount = 9;
constexpr uint32_t kParityTicketCount = 3;
constexpr uint32_t kNoParityMismatch = UINT32_MAX;

// Native opaque GPU handle. In particular, this cannot carry a CPU pixel pointer
// in an Android evaluation build; the backend dispatch owns resource validation.
struct ParityGpuView {
    uintptr_t resource = 0;
    uint64_t byte_offset=0,byte_capacity=0;
    std::array<uint8_t, 16> device_uuid{};
    std::array<uint8_t, 32> content_sha256{};
    uint64_t generation = 0;
    uint64_t source_id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    uint32_t row_stride_elements = 0;
    uint32_t channel_stride_elements = 0;
    ParityDtype dtype = ParityDtype::Unorm8;
    uint8_t elempack = 1;
    ParityIdentity identity = ParityIdentity::Production;
};

struct ParityContract {
    std::array<uint8_t, 32> golden_sha256{};
    uint64_t generation = 0;
    uint64_t source_id = 0;
    uint32_t width = 0;  // Zero means use the golden view's shape.
    uint32_t height = 0;
    uint32_t channels = 0;
    ParityDtype dtype = ParityDtype::Unorm8;
    uint8_t elempack = 0; // Zero means use the golden view's packing.
    float element_tolerance = 0;
    float max_error_limit = 0;
    float mean_error_limit = 0;
};

// Fixed-size payload produced by a GPU reduction. Error_sum is reduced on GPU;
// no source image or tensor bytes are returned through this interface.
struct ParityReduction {
    uint32_t element_count = 0;
    uint32_t mismatch_count = 0;
    uint32_t first_mismatch = kNoParityMismatch;
    float max_error = 0;
    float error_sum = 0;
    std::array<float, kParitySampleCount> samples{};
};

struct ParitySummary {
    std::array<uint8_t,16> device_uuid{};
    std::array<uint8_t,32> golden_sha256{};
    uint64_t generation = 0;
    uint64_t source_id = 0;
    ParityStage stage = ParityStage::Source;
    ParityDtype dtype = ParityDtype::Unorm8;
    uint8_t elempack = 0;
    bool passed = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    uint32_t row_stride_elements = 0;
    uint32_t channel_stride_elements = 0;
    uint32_t element_count = 0;
    uint32_t mismatch_count = 0;
    uint32_t first_mismatch = kNoParityMismatch;
    uint32_t first_x=kNoParityMismatch, first_y=kNoParityMismatch, first_channel=kNoParityMismatch;
    float max_error = 0;
    float mean_error = 0;
    uint32_t sample_count = kParitySampleCount;
    std::array<float, kParitySampleCount> samples{};
};

struct ParityTicket {
    uint64_t serial = 0;
    uint32_t slot = kParityTicketCount;
};

// Both callbacks must use the actual GPU resources. collect must poll completion
// without waiting; a host fake may only be used by unit tests.
struct ParityDispatch {
    void* context = nullptr;
    bool (*record)(void*, const ParityGpuView&, const ParityGpuView&,
                   const ParityContract&, uint32_t slot, ParityReduction&) noexcept = nullptr;
    bool (*collect)(void*, uint32_t slot, ParityReduction&) noexcept = nullptr;
};

class GpuParityProbe {
public:
    explicit GpuParityProbe(ParityDispatch dispatch) noexcept : dispatch_(dispatch) {}
    bool Record(ParityStage, const ParityGpuView&, const ParityGpuView&,
                const ParityContract&, ParityTicket&) noexcept;
    bool TryCollect(ParityTicket, ParitySummary&) noexcept;

private:
    struct Slot {
        bool active = false;
        uint64_t serial = 0;
        ParityStage stage = ParityStage::Source;
        ParityGpuView view{};
        ParityContract contract{};
        ParityReduction reduction{};
    };
    ParityDispatch dispatch_{};
    std::array<Slot, kParityTicketCount> slots_{};
    uint64_t next_serial_ = 1;
    std::mutex mutex_;
};

} // namespace humanvision::gpu
