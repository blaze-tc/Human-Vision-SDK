#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace hvinput {
// Owns only the output worker, not the codec/reader. Declare AFTER the resources
// and callback captures so every return/reconnect joins before their destruction.
// Network reads must never gate output draining. The callback must be bounded
// (MediaCodec dequeue timeout, no network read or GPU fence wait).
class InputOutputPump final {
 public:
  template<class Drain, class Failure>
  InputOutputPump(Drain drain, Failure failure)
      : worker_([this, drain, failure] {
          try {
            while (!stop_.load()) {
              drain();
              // AImage arrival can lag releaseOutputBuffer; poll again even if
              // no new network packet arrives, without spinning on an idle codec.
              std::unique_lock<std::mutex> lock(mutex_);
              wake_.wait_for(lock, std::chrono::milliseconds(1),
                             [this] { return stop_.load(); });
            }
          } catch (...) { failure(); }
        }) {}
  ~InputOutputPump() { StopAndJoin(); }
  void StopAndJoin() {
    { std::lock_guard<std::mutex> lock(mutex_); stop_=true; }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
  }
  InputOutputPump(const InputOutputPump&)=delete;
  InputOutputPump& operator=(const InputOutputPump&)=delete;
 private:
  std::atomic<bool> stop_{false};
  std::mutex mutex_;
  std::condition_variable wake_;
  std::thread worker_;
};
}
