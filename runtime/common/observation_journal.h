#pragma once

#include "common/frame_envelope.h"
#include "humanvision_plugin.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace humanvision::runtime {

enum class ObservationKind : std::uint8_t { Body, Hand };
struct ObservationEntry {
    std::uint64_t sequence = 0; // journal cursor, independent of model sequences
    ObservationKind kind = ObservationKind::Body;
    std::int64_t body_sequence = 0, hand_sequence = 0;
    FrameEnvelope envelope;
    FrameTime published;
    std::uint32_t body_count = 0;
    std::array<HV_CanonicalBodyV1, HV_MAX_PEOPLE> bodies{};
};
struct ObservationRead {
    std::uint32_t written = 0;
    std::uint64_t lost_count = 0, next_cursor = 0;
};

// Readers own their cursors. A slow or absent reader never holds producer
// credits, and copying into caller buffers cannot expose mutable result storage.
class ObservationJournal {
public:
    explicit ObservationJournal(std::uint32_t capacity = 64)
        : capacity_(capacity), entries_(capacity ? std::make_unique<ObservationEntry[]>(capacity) : nullptr) {
        if (!capacity) throw std::invalid_argument("Observation journal capacity must be positive");
    }
    void Publish(const ObservationEntry& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& entry = entries_[sequence_ % capacity_];
        entry = value;
        entry.sequence = ++sequence_;
    }
    ObservationRead ReadAfter(std::uint64_t cursor, ObservationEntry* output, std::uint32_t capacity) const {
        std::lock_guard<std::mutex> lock(mutex_);
        ObservationRead result{};
        result.next_cursor = cursor;
        if (cursor >= sequence_ || !output || !capacity) return result;
        const auto oldest = sequence_ > capacity_ ? sequence_ - capacity_ + 1 : 1;
        const auto first = std::max(cursor + 1, oldest);
        result.lost_count = first - cursor - 1;
        result.written = static_cast<std::uint32_t>(std::min<std::uint64_t>(capacity, sequence_ - first + 1));
        for (std::uint32_t i = 0; i < result.written; ++i) output[i] = entries_[(first + i - 1) % capacity_];
        result.next_cursor = first + result.written - 1;
        return result;
    }
private:
    const std::uint32_t capacity_;
    std::unique_ptr<ObservationEntry[]> entries_;
    mutable std::mutex mutex_;
    std::uint64_t sequence_ = 0;
};

} // namespace humanvision::runtime
