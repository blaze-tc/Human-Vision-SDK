#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>

namespace humanvision::runtime {

// Optional native-coordinator handoff. Reserve a credit before admitting work,
// so every in-flight job owns room for its completion. No Unity reader is ever
// involved. Storage is allocated once, outside the processing loop.
template<class Record, std::size_t Capacity = 3>
class CompletionMailbox {
public:
    using Notify = void (*)(void*);
    // Configure only while producers are stopped. Retained over Start/Stop.
    void Configure(void* context, Notify notify) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (notify && !records_) records_ = std::make_unique<std::array<Record, Capacity>>();
        context_ = context;
        notify_ = notify;
    }
    void Start() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = size_ = reserved_ = 0;
        stopped_ = false;
    }
    void Stop() {
        { std::lock_guard<std::mutex> lock(mutex_); stopped_ = true; }
        ready_.notify_all();
    }
    bool Reserve() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!notify_) return !stopped_;
        ready_.wait(lock,[&]{ return stopped_ || reserved_ < Capacity; });
        if (stopped_) return false;
        ++reserved_;
        return true;
    }
    void Cancel() {
        { std::lock_guard<std::mutex> lock(mutex_); if (notify_) --reserved_; }
        ready_.notify_one();
    }
    void Publish(const Record& record) {
        Notify notify = nullptr;
        void* context = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!notify_) return;
            (*records_)[(head_ + size_) % Capacity] = record;
            ++size_;
            notify = notify_;
            context = context_;
        }
        notify(context); // Never under a Host or mailbox lock.
    }
    bool TryTake(Record& record) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!size_) return false;
            record = (*records_)[head_];
            head_ = (head_ + 1) % Capacity;
            --size_;
            --reserved_;
        }
        ready_.notify_one();
        return true;
    }
private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::unique_ptr<std::array<Record, Capacity>> records_;
    std::size_t head_ = 0, size_ = 0, reserved_ = 0;
    bool stopped_ = true;
    void* context_ = nullptr;
    Notify notify_ = nullptr;
};

} // namespace humanvision::runtime
