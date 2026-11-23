#pragma once

#include "core/frame_buffer.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

namespace humanvision {

enum class FrameSubmitStatus {
    kAccepted,
    kReplacedPending,
};

namespace latest_frame_slot_detail {
struct NoMetadata {};
void CopyValidatedFrame(const HV_VideoFrame& frame, FrameBuffer& destination);
}

// A pending replacement and a worker take each move the frame and its
// metadata together under the same lock.
template<class Metadata>
class LatestFrameSlotWithMetadata {
    static_assert(std::is_nothrow_copy_assignable_v<Metadata>,
                  "Copying pending frame metadata must not throw");

public:
    bool Submit(
        const HV_VideoFrame& frame,
        FrameSubmitStatus& status,
        std::string& error) {
        return Submit(frame, Metadata{}, status, error);
    }

    bool Submit(
        const HV_VideoFrame& frame,
        const Metadata& metadata,
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

        latest_frame_slot_detail::CopyValidatedFrame(frame, pending_);
        pending_metadata_ = metadata;
        status = has_pending_ ? FrameSubmitStatus::kReplacedPending
                              : FrameSubmitStatus::kAccepted;
        if (has_pending_) {
            ++dropped_frames_;
        }
        has_pending_ = true;
        error.clear();
        condition_.notify_one();
        return true;
    }

    bool WaitTake(FrameBuffer& destination) {
        Metadata metadata{};
        return WaitTake(destination, metadata);
    }

    bool WaitTake(FrameBuffer& destination, Metadata& metadata) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return stopped_ || has_pending_; });
        if (!has_pending_) {
            return false;
        }

        std::swap(destination, pending_);
        metadata = pending_metadata_;
        has_pending_ = false;
        return true;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        condition_.notify_all();
    }

    std::int64_t dropped_frames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_frames_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    FrameBuffer pending_;
    Metadata pending_metadata_{};
    bool has_pending_ = false;
    bool stopped_ = false;
    std::int64_t dropped_frames_ = 0;
};

using LatestFrameSlot = LatestFrameSlotWithMetadata<latest_frame_slot_detail::NoMetadata>;

}  // namespace humanvision
