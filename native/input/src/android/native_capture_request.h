#pragma once
#include <cstdint>
namespace hvinput {
class NativeCaptureRequest {
  enum class Phase { Idle, Pending, Recording, Ready, Failed };
  Phase phase_{};
  uint64_t id_{};

public:
  uint64_t Request() {
    if (phase_ != Phase::Idle)
      return 0;
    phase_ = Phase::Pending;
    return ++id_;
  }
  bool Begin() {
    if (phase_ != Phase::Pending)
      return false;
    phase_ = Phase::Recording;
    return true;
  }
  void Finish(bool visible) {
    if (phase_ == Phase::Recording)
      phase_ = visible ? Phase::Ready : Phase::Failed;
  }
  bool Matches(uint64_t id) const {
    return id && id == id_ && phase_ != Phase::Idle;
  }
  bool Ready(uint64_t id) const {
    return Matches(id) && phase_ == Phase::Ready;
  }
  bool Failed(uint64_t id) const {
    return Matches(id) && phase_ == Phase::Failed;
  }
  void Consume(uint64_t id) {
    if (Matches(id))
      Invalidate();
  }
  void Invalidate() { phase_ = Phase::Idle; }
};
} // namespace hvinput
