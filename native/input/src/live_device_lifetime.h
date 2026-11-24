#pragma once

#include <cstdint>
#include <mutex>

namespace hvinput {
// A device event waits only for host calls that already entered. The lease
// never waits for Vulkan completion and must not span Unity's AccessQueue.
class LiveDeviceLifetime {
public:
    class Lease {
    public:
        uint64_t Epoch() const noexcept { return epoch_; }
        bool Matches(uint64_t epoch) const noexcept { return epoch != 0 && epoch_ == epoch; }

    private:
        friend class LiveDeviceLifetime;
        explicit Lease(LiveDeviceLifetime& owner)
            : lock_(owner.mutex_), epoch_(owner.active_ ? owner.epoch_ : 0) {}
        std::unique_lock<std::mutex> lock_;
        uint64_t epoch_;
    };

    Lease Acquire() { return Lease(*this); }

    void Activate() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++epoch_;
        active_ = true;
    }

    void Invalidate() {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
    }

private:
    std::mutex mutex_;
    uint64_t epoch_{};
    bool active_{};
};
}
