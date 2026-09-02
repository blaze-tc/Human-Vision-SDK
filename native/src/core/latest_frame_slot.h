#pragma once

#include "core/frame_buffer.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

namespace humanvision {

enum class FrameSubmitStatus {
    kAccepted,
    kReplacedPending,
};

class LatestFrameSlot {
public:
    bool Submit(
        const HV_VideoFrame& frame,
        FrameSubmitStatus& status,
        std::string& error);
    bool WaitTake(FrameBuffer& destination);
    void Stop();
    std::int64_t dropped_frames() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    FrameBuffer pending_;
    bool has_pending_ = false;
    bool stopped_ = false;
    std::int64_t dropped_frames_ = 0;
};

}  // namespace humanvision
