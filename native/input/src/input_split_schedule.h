#pragma once
#include <cstdint>

namespace hvinput {

enum class SplitWork { None, Analysis, Preview, DiscardPreview };

// Owns the identity of at most one completed analysis image that can still
// become a same-frame preview. The caller owns the corresponding AImage lease.
class InputSplitSchedule {
    static constexpr int64_t kPreviewOpportunityUs = 120000;
    uint64_t sequence_ = 0;
    int64_t completed_us_ = 0;

public:
    void OfferPreview(uint64_t sequence, int64_t now) {
        sequence_ = sequence;
        completed_us_ = now;
    }

    uint64_t Sequence() const { return sequence_; }
    int64_t CompletedUs() const { return completed_us_; }

    SplitWork Choose(int64_t now, bool analysis_ready, bool preview_slot_free) const {
        if (analysis_ready) return SplitWork::Analysis;
        if (!sequence_) return SplitWork::None;
        if (!preview_slot_free || now - completed_us_ >= kPreviewOpportunityUs)
            return SplitWork::DiscardPreview;
        return SplitWork::Preview;
    }

    void Reset() { *this = {}; }
};

} // namespace hvinput
