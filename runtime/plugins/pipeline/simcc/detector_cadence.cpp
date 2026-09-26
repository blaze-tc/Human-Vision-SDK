#include "plugins/pipeline/simcc/detector_cadence.h"
#include <stdexcept>

namespace humanvision::runtime {
DetectorCadenceScheduler::DetectorCadenceScheduler(int interval_frames,
    std::int64_t max_capture_gap_us) : interval_(interval_frames), max_gap_(max_capture_gap_us) {
    if (interval_frames < 2 || interval_frames > 6 || max_capture_gap_us < 1 ||
        max_capture_gap_us > 200000)
        throw std::invalid_argument("Detector cadence requires interval 2-6 and gap <=200000 us");
}

bool DetectorCadenceScheduler::ShouldCapture(std::int64_t frame_id,
    std::int64_t capture_us, DetectorCadenceTrigger trigger) {
    if (frame_id <= last_frame_ || capture_us < 0 ||
        (last_capture_us_ >= 0 && capture_us < last_capture_us_)) return false;
    last_frame_ = frame_id; last_capture_us_ = capture_us; ++accepted_frames_;
    if (trigger != DetectorCadenceTrigger::None) requested_ = true;
    const bool deadline = last_attempt_us_ >= 0 && capture_us - last_attempt_us_ >= max_gap_;
    const bool interval_due = last_attempt_us_ >= 0 &&
        accepted_frames_ - last_capture_index_ >= static_cast<std::uint64_t>(interval_);
    if (deadline && busy_) {
        const auto expired=static_cast<std::uint64_t>((capture_us-last_attempt_us_)/max_gap_);
        if(expired>reported_deadlines_){missed_deadlines_+=expired-reported_deadlines_;
            reported_deadlines_=expired;}
    }
    return requested_ || interval_due || deadline;
}

bool DetectorCadenceScheduler::AdmitPrepared(std::int64_t frame_id,
    std::int64_t capture_us, std::uint64_t generation) {
    if (busy_ || !generation || frame_id != last_frame_ || capture_us != last_capture_us_ ||
        (!requested_ && last_attempt_us_ >= 0 &&
         accepted_frames_ - last_capture_index_ < static_cast<std::uint64_t>(interval_) &&
         capture_us - last_attempt_us_ < max_gap_)) return false;
    busy_ = true; active_frame_ = frame_id; active_generation_ = generation;
    last_attempt_us_ = capture_us; last_capture_index_ = accepted_frames_;
    requested_ = false; reported_deadlines_ = 0; ++captures_; return true;
}

bool DetectorCadenceScheduler::FinishDetector(std::int64_t frame_id,
    std::uint64_t generation) {
    if (!busy_ || active_frame_ != frame_id || active_generation_ != generation) return false;
    busy_ = false; active_frame_ = -1; active_generation_ = 0; return true;
}

bool DetectorCadenceScheduler::CancelUnstarted(std::int64_t frame_id,
    std::uint64_t generation) {
    if(!FinishDetector(frame_id,generation))return false;
    requested_=true;return true;
}

void DetectorCadenceScheduler::CancelGeneration(std::uint64_t generation) {
    if (busy_ && active_generation_ == generation) {
        busy_ = false; active_frame_ = -1; active_generation_ = 0;
    }
    requested_ = true; last_attempt_us_ = -1; reported_deadlines_ = 0;
}
}
