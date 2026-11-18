#pragma once
#include <cstdint>

namespace hvinput {

enum class SplitWork { None, Analysis, Preview, DiscardPreview, AnalysisAndPreview, PreviewOnly };

// Owns the identity of at most one completed analysis image that can still
// become a same-frame preview. The caller owns the corresponding AImage lease.
class InputSplitSchedule {
    static constexpr int64_t kPreviewOpportunityUs = 120000;
    uint64_t sequence_ = 0;
    int64_t completed_us_ = 0;
    int64_t decoded_us_ = 0;

public:
    static bool PreviewExpired(int64_t now, int64_t decoded_us) {
        return now - decoded_us >= kPreviewOpportunityUs;
    }

    void OfferPreview(uint64_t sequence, int64_t completed_us, int64_t decoded_us) {
        sequence_ = sequence;
        completed_us_ = completed_us;
        decoded_us_ = decoded_us;
    }

    uint64_t Sequence() const { return sequence_; }
    int64_t CompletedUs() const { return completed_us_; }
    bool Expired(int64_t now) const { return sequence_ && PreviewExpired(now, decoded_us_); }

    SplitWork Choose(int64_t now, bool analysis_ready, bool preview_slot_free,
                     int64_t next_decoded_us) const {
        const bool fresh_input = next_decoded_us > 0 && !PreviewExpired(now, next_decoded_us);
        // Reserve both outputs for the same new decoded image when possible.
        // Analysis keeps its render opportunity even under HD slot pressure.
        if (analysis_ready) {
            if (preview_slot_free && fresh_input)
                return SplitWork::AnalysisAndPreview;
            return SplitWork::Analysis;
        }
        if (preview_slot_free) {
            if (sequence_ && !Expired(now)) return SplitWork::Preview;
            // Analysis leases can fill that ring independently of HD. A new
            // preview-only image has its own sequence and no matching pose.
            if (fresh_input) return SplitWork::PreviewOnly;
        }
        return sequence_ ? SplitWork::DiscardPreview : SplitWork::None;
    }

    void Reset() { *this = {}; }
};

} // namespace hvinput
