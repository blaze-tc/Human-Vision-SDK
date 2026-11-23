#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>

namespace hvinput {
inline int64_t VideoPacketReceiveTime(int read_status, int stream,
                                      int video_stream, int64_t now_us) {
  return read_status >= 0 && stream == video_stream ? now_us : -1;
}
struct PacketStamp {
  bool pts_valid = false;
  int64_t pts_us = 0, received_us = -1;
};

// Only the native decoder uses private MediaCodec timestamps. Preserve each
// original media PTS (including absent, zero, negative, repeated or reordered)
// independently. A dropped/evicted input can never match a later packet.
class NativePacketLedger {
public:
  static constexpr unsigned kCapacity = 128;
  int64_t Submit(bool valid, int64_t pts, int64_t received) {
    std::lock_guard<std::mutex> lock(mutex_);
    // A unique positive codec timestamp near local arrival also keeps nominal
    // frame intervals for codecs using timestamps as a pacing hint.
    last_ = std::max(received, last_ + 1);
    entries_[next_++ % kCapacity] = {last_, {valid, valid ? pts : 0, received}};
    return last_;
  }
  PacketStamp Take(int64_t key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key > 0)
      for (auto &e : entries_)
        if (e.key == key) {
          e.key = 0;
          return e.packet;
        }
    return {};
  }

private:
  struct Entry {
    int64_t key{};
    PacketStamp packet{};
  };
  std::array<Entry, kCapacity> entries_{};
  std::mutex mutex_;
  unsigned next_{};
  int64_t last_{};
};
struct DecodedFormat {
  int32_t left{}, top{}, right{}, bottom{};
  uint32_t matrix{}, range{}, transfer{}, primaries{};
  bool crop_known{};
  bool operator==(const DecodedFormat &b) const {
    return left == b.left && top == b.top && right == b.right &&
           bottom == b.bottom && matrix == b.matrix && range == b.range &&
           transfer == b.transfer && primaries == b.primaries &&
           crop_known == b.crop_known;
  }
};
struct SurfaceRecord {
  int64_t stamp_ns{};
  uint64_t generation{};
  DecodedFormat format{};
  PacketStamp packet{};
};
enum class SurfaceMatch { Ready, Obsolete, Missing };
// Output-worker only. releaseOutputBufferAtTime assigns this exact timestamp
// to the AImage. Snapshot the format at dequeue, before any subsequent format
// event. acquireLatest may skip any number of releases.
class NativeSurfaceLedger {
public:
  static constexpr unsigned kCapacity = 32;
  explicit NativeSurfaceLedger(uint64_t generation) : generation_(generation) {}
  uint64_t SetFormat(const DecodedFormat &format) {
    if (have_format_ && !(format == current_))
      ++generation_;
    current_ = format;
    have_format_ = true;
    return generation_;
  }
  int64_t Release(int64_t now_ns, PacketStamp packet) {
    last_ = std::max(last_ + 1, now_ns);
    records_[next_++ % kCapacity] = {last_, generation_, current_, packet};
    return last_;
  }
  SurfaceMatch Take(int64_t stamp, SurfaceRecord &out) {
    if (stamp > 0)
      for (auto &entry : records_)
        if (entry.stamp_ns == stamp) {
          out = entry;
          entry.stamp_ns = 0;
          return out.generation == generation_ ? SurfaceMatch::Ready
                                               : SurfaceMatch::Obsolete;
        }
    return SurfaceMatch::Missing;
  }

private:
  std::array<SurfaceRecord, kCapacity> records_{};
  uint64_t generation_{};
  unsigned next_{};
  int64_t last_{};
  bool have_format_{};
  DecodedFormat current_{};
};
} // namespace hvinput
