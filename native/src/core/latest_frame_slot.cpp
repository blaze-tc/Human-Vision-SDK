#include "core/latest_frame_slot.h"

#include <algorithm>
#include <cstddef>

namespace humanvision {

bool LatestFrameSlot::Submit(
    const HV_VideoFrame& frame,
    FrameSubmitStatus& status,
    std::string& error) {
    if (ValidateVideoFrame(&frame, error) != HV_OK) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
        error = "frame slot is stopped";
        return false;
    }

    status = has_pending_ ? FrameSubmitStatus::kReplacedPending
                          : FrameSubmitStatus::kAccepted;
    if (has_pending_) {
        ++dropped_frames_;
    }

    pending_.width = frame.width;
    pending_.height = frame.height;
    pending_.stride_bytes = frame.stride_bytes;
    pending_.pixel_format = frame.pixel_format;
    pending_.frame_id = frame.frame_id;
    pending_.timestamp_us = frame.timestamp_us;
    const std::size_t required_bytes =
        static_cast<std::size_t>(frame.stride_bytes) *
        static_cast<std::size_t>(frame.height);
    pending_.bytes.resize(required_bytes);
    const auto* source = static_cast<const std::uint8_t*>(frame.data);
    std::copy_n(source, required_bytes, pending_.bytes.data());
    has_pending_ = true;
    error.clear();
    condition_.notify_one();
    return true;
}

bool LatestFrameSlot::WaitTake(FrameBuffer& destination) {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] { return stopped_ || has_pending_; });
    if (!has_pending_) {
        return false;
    }

    destination.width = pending_.width;
    destination.height = pending_.height;
    destination.stride_bytes = pending_.stride_bytes;
    destination.pixel_format = pending_.pixel_format;
    destination.frame_id = pending_.frame_id;
    destination.timestamp_us = pending_.timestamp_us;
    destination.bytes.swap(pending_.bytes);
    has_pending_ = false;
    return true;
}

void LatestFrameSlot::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    condition_.notify_all();
}

std::int64_t LatestFrameSlot::dropped_frames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_frames_;
}

}  // namespace humanvision
