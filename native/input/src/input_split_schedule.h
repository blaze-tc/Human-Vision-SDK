#pragma once
#include <cstdint>

namespace hvinput {
enum class PreviewDecision { Wait, Convert, Discard };

// The preview must never get ahead of the analysis consumer for this image.
// A stalled consumer may hold the AImage only for this bounded handoff.
class InputSplitSchedule {
    static constexpr int64_t kMaxHandoffUs = 42000;
    uint64_t generation_ = 0;
    uint64_t sequence_ = 0;
    int64_t completed_us_ = 0;
    int64_t acknowledged_us_ = 0;

public:
    void CompleteAnalysis(uint64_t generation, uint64_t sequence, int64_t now) {
        generation_ = generation;
        sequence_ = sequence;
        completed_us_ = now;
        acknowledged_us_ = 0;
    }

    bool Acknowledge(uint64_t generation, uint64_t sequence, int64_t now) {
        if (!Waiting() || generation != generation_ || sequence != sequence_) return false;
        if (!acknowledged_us_) acknowledged_us_ = now;
        return true;
    }

    bool Waiting() const { return sequence_ != 0; }
    uint64_t Sequence() const { return sequence_; }
    int64_t CompletedUs() const { return completed_us_; }
    int64_t AcknowledgedUs() const { return acknowledged_us_; }

    PreviewDecision Decide(int64_t now, bool preview_slot_free, bool newer_pending) const {
        if (!preview_slot_free) return PreviewDecision::Discard;
        if (acknowledged_us_) return PreviewDecision::Convert;
        if (now - completed_us_ < kMaxHandoffUs) return PreviewDecision::Wait;
        return newer_pending ? PreviewDecision::Discard : PreviewDecision::Convert;
    }

    void Reset() { *this = {}; }
};
} // namespace hvinput
