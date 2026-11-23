#include "android/native_decode_metadata.h"
#include <cstdio>
using namespace hvinput;
#define CHECK(e)                                                               \
  do {                                                                         \
    if (!(e)) {                                                                \
      std::fprintf(stderr, "line %d: %s\n", __LINE__, #e);                     \
      return 1;                                                                \
    }                                                                          \
  } while (false)
int main() {
  std::atomic<int64_t> legacy_activity{0};
  CHECK(RecordDemuxRead(0, 2, 2, 100, &legacy_activity) == 100);
  CHECK(RecordDemuxRead(0, 3, 2, 101, &legacy_activity) == -1);
  CHECK(legacy_activity.load() == 101); // audio read updates legacy Unity metadata
  CHECK(RecordDemuxRead(-1, -1, 2, 102, &legacy_activity) == -1);
  CHECK(legacy_activity.load() == 102); // failed read is still demux activity
  NativePacketLedger inputs;
  auto arrival = RecordDemuxRead(0, 2, 2, 100);
  auto absent = inputs.Submit(false, 0, arrival);
  CHECK(RecordDemuxRead(0, 3, 2, 101) == -1); // interleaved audio
  CHECK(RecordDemuxRead(-1, 2, 2, 102) == -1);
  auto zero = inputs.Submit(true, 0, RecordDemuxRead(0, 2, 2, 103));
  auto negative = inputs.Submit(true, -40000, 104);
  CHECK(zero != absent && negative != zero);
  auto b = inputs.Take(zero);
  CHECK(b.pts_valid && b.pts_us == 0 && b.received_us == 103);
  auto a = inputs.Take(absent);
  CHECK(!a.pts_valid && a.received_us == 100);
  auto c = inputs.Take(negative);
  CHECK(c.pts_valid && c.pts_us == -40000 && c.received_us == 104);
  CHECK(inputs.Take(zero).received_us == -1); // unmatched/replayed output
  auto old = inputs.Submit(true, 99, 105);
  for (unsigned i = 0; i < NativePacketLedger::kCapacity; ++i)
    inputs.Submit(true, 99, 106 + i);
  CHECK(inputs.Take(old).received_us == -1);
  NativeSurfaceLedger surface(7);
  DecodedFormat old_format{0, 0, 1280, 720, 1, 2, 3, 4, true};
  CHECK(surface.SetFormat(old_format) == 7);
  auto late = surface.Release(2000, a);
  auto changed = old_format;
  changed.left += 8;
  changed.right += 8;
  changed.matrix = 2;
  changed.range = 1;
  CHECK(surface.SetFormat(changed) == 8);
  auto newer =
      surface.Release(2000, b); // equal monotonic clock tick, unique release
  CHECK(newer > late);
  SurfaceRecord result;
  CHECK(surface.Take(late, result) == SurfaceMatch::Obsolete);
  CHECK(surface.Take(newer, result) == SurfaceMatch::Ready);
  CHECK(result.generation == 8 && result.format.left == 8 &&
        result.format.right == 1288 && result.format.matrix == 2 &&
        result.format.range == 1 && result.packet.pts_valid &&
        result.packet.received_us == 103);
  CHECK(surface.Take(newer, result) == SurfaceMatch::Missing);
  CHECK(surface.SetFormat(changed) == 8);
  auto before_color = surface.Release(2020, b);
  changed.transfer = 7;
  CHECK(surface.SetFormat(changed) == 9);
  CHECK(surface.Take(before_color, result) == SurfaceMatch::Obsolete);
  auto retained = surface.Release(2040, c);
  CHECK(surface.Take(retained, result) == SurfaceMatch::Ready &&
        result.format.transfer == 7 && result.packet.pts_us == -40000);
  auto stale = surface.Release(2100, c);
  for (unsigned i = 0; i < NativeSurfaceLedger::kCapacity; ++i)
    surface.Release(2200 + i, a);
  CHECK(surface.Take(stale, result) == SurfaceMatch::Missing);
  return 0;
}
