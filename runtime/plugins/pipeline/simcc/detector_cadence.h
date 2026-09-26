#pragma once
#include <cstdint>

namespace humanvision::runtime {

enum class DetectorCadenceTrigger : std::uint8_t {
    None, NoTracks, PoseRejected, CropAtEdge, BodyCountChanged, RegionChanged, SessionChanged
};

// Called once for each accepted current-image pose frame. A prepared detector
// token occupies the single job slot until FinishDetector or cancellation.
class DetectorCadenceScheduler {
public:
    DetectorCadenceScheduler(int interval_frames, std::int64_t max_capture_gap_us);
    bool ShouldCapture(std::int64_t frame_id, std::int64_t capture_us,
                       DetectorCadenceTrigger trigger);
    bool AdmitPrepared(std::int64_t frame_id, std::int64_t capture_us,
                       std::uint64_t generation);
    bool FinishDetector(std::int64_t frame_id, std::uint64_t generation);
    bool CancelUnstarted(std::int64_t frame_id, std::uint64_t generation);
    void CancelGeneration(std::uint64_t generation);
    std::uint64_t MissedDeadlines() const noexcept { return missed_deadlines_; }
    std::uint64_t Captures() const noexcept { return captures_; }
    bool Busy() const noexcept { return busy_; }
private:
    int interval_;
    std::int64_t max_gap_, last_frame_ = -1, last_capture_us_ = -1,
                 last_attempt_us_ = -1, active_frame_ = -1;
    std::uint64_t accepted_frames_ = 0, last_capture_index_ = 0,
                  active_generation_ = 0, missed_deadlines_ = 0, captures_ = 0,
                  reported_deadlines_ = 0;
    bool busy_ = false, requested_ = true;
};
}
