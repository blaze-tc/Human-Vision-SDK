#pragma once
#include <array>
#include <cstdint>
namespace hvinput {
// All methods run under the session lock. Keep acquired slots address-stable
// through generation changes. Only the conversion worker owns Writing slots.
class NativeOutputSlots {
  enum class State { Free, Writing, Published, Leased };
  struct Slot {
    State state{};
    uint64_t generation{}, sequence{};
  };
  std::array<Slot, 3> slots_{};
  int latest_ = -1;

public:
  bool CanBegin() const {
    if (latest_ >= 0)
      return true;
    for (const auto &slot : slots_)
      if (slot.state == State::Free)
        return true;
    return false;
  }
  int Begin() {
    for (int i = 0; i < 3; ++i) {
      if (slots_[i].state == State::Free) {
        slots_[i].state = State::Writing;
        return i;
      }
    }
    if (latest_ < 0)
      return -1;
    int i = latest_;
    latest_ = -1;
    slots_[i].state = State::Writing;
    return i;
  }
  bool Publish(int i, uint64_t gen, uint64_t seq, bool fence_complete,
               bool cpu_visible) {
    if (i < 0 || i >= 3 || slots_[i].state != State::Writing ||
        !fence_complete || !cpu_visible)
      return false;
    Invalidate();
    slots_[i] = {State::Published, gen, seq};
    latest_ = i;
    return true;
  }
  void Discard(int i) {
    if (i >= 0 && i < 3 && slots_[i].state == State::Writing)
      slots_[i] = {};
  }
  void Invalidate() {
    if (latest_ >= 0)
      slots_[latest_] = {};
    latest_ = -1;
  }
  bool Available(uint64_t after) const {
    return latest_ >= 0 && slots_[latest_].sequence > after;
  }
  int Acquire(uint64_t after) {
    int i = latest_;
    if (i < 0 || slots_[i].sequence <= after)
      return -1;
    slots_[i].state = State::Leased;
    latest_ = -1;
    return i;
  }
  bool Leased(unsigned i, uint64_t gen, uint64_t seq) const {
    return i < 3 && slots_[i].state == State::Leased &&
           slots_[i].generation == gen && slots_[i].sequence == seq;
  }
  bool Release(unsigned i, uint64_t gen, uint64_t seq) {
    if (!Leased(i, gen, seq))
      return false;
    slots_[i] = {};
    return true;
  }
  bool Retired() const {
    for (const auto &s : slots_)
      if (s.state != State::Free)
        return false;
    return true;
  }
};
} // namespace hvinput
