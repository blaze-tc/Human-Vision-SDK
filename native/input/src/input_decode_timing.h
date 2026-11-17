#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace hvinput {
namespace {
class InputDecodeTiming final {
 public:
  static constexpr std::size_t kCapacity = 128;

  struct Window {
    uint64_t samples = 0;
    uint64_t total_us = 0;
    uint64_t max_us = 0;
    uint64_t unmatched_outputs = 0;
    uint64_t invalid_inputs = 0;
    uint64_t invalid_outputs = 0;
    uint64_t ambiguous_outputs = 0;
    uint64_t evicted_inputs = 0;
    uint64_t pending_inputs = 0;
    uint64_t skipped_outputs = 0;
  };

  // Record immediately before queueInputBuffer, which can expose an output on
  // the other worker before it returns. The tracker never locks across codec calls.
  void RecordInput(int64_t pts, bool valid, int64_t queued_us) {
    const std::lock_guard<std::mutex> lock(mutex_);
    Pending* empty = nullptr;
    Pending* oldest = nullptr;
    bool ambiguous = false;
    for (auto& input : inputs_) {
      if (!input.occupied) {
        if (!empty) empty = &input;
        continue;
      }
      if (!oldest || input.queued_us < oldest->queued_us) oldest = &input;
      if (input.pts == pts) {
        input.ambiguous = true;
        ambiguous = true;
      }
    }
    if (!empty) {
      empty = oldest;
      ++window_.evicted_inputs;
    } else {
      ++pending_inputs_;
    }
    *empty = {pts, queued_us, true, valid, ambiguous};
    if (!valid) ++window_.invalid_inputs;
  }

  void RecordOutput(int64_t pts, int64_t dequeued_us) {
    const std::lock_guard<std::mutex> lock(mutex_);
    Pending* match = nullptr;
    for (auto& input : inputs_) {
      if (input.occupied && input.pts == pts &&
          (!match || input.queued_us < match->queued_us)) {
        match = &input;
      }
    }
    if (!match) {
      ++window_.unmatched_outputs;
      return;
    }
    match->occupied = false;
    --pending_inputs_;
    if (match->ambiguous) {
      ++window_.ambiguous_outputs;
      return;
    }
    if (!match->valid || dequeued_us < match->queued_us) {
      ++window_.invalid_outputs;
      return;
    }
    const auto elapsed_us = static_cast<uint64_t>(dequeued_us - match->queued_us);
    ++window_.samples;
    window_.total_us += elapsed_us;
    if (elapsed_us > window_.max_us) window_.max_us = elapsed_us;
  }

  // Called only by the output worker, which also takes the report snapshot.
  void RecordSkippedOutput() { ++window_.skipped_outputs; }

  Window TakeWindow() {
    const std::lock_guard<std::mutex> lock(mutex_);
    Window result = window_;
    result.pending_inputs = pending_inputs_;
    window_ = {};
    return result;
  }

 private:
  struct Pending {
    int64_t pts = 0;
    int64_t queued_us = 0;
    bool occupied = false;
    bool valid = false;
    bool ambiguous = false;
  };
  std::mutex mutex_;
  std::array<Pending, kCapacity> inputs_{};
  Window window_{};
  uint64_t pending_inputs_ = 0;
};
}
}
