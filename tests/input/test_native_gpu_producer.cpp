#include "android/native_capture_request.h"
#include "android/native_output_slots.h"
#include <cstdio>
using hvinput::NativeOutputSlots;
#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr);                  \
      return 1;                                                                \
    }                                                                          \
  } while (false)
int main() {
  NativeOutputSlots slots;
  const int a = slots.Begin();
  CHECK(a >= 0);
  CHECK(slots.Acquire(0) == -1);
  CHECK(!slots.Publish(a, 1, 10, false, true));
  CHECK(!slots.Publish(a, 1, 10, true, false));
  CHECK(slots.Acquire(0) == -1);
  CHECK(slots.Publish(a, 1, 10, true, true));
  CHECK(slots.Acquire(10) == -1);
  CHECK(slots.Acquire(9) == a);
  CHECK(slots.Acquire(0) == -1);
  const int b = slots.Begin();
  CHECK(b != a && slots.Publish(b, 1, 11, true, true));
  CHECK(slots.Acquire(10) == b);
  const int c = slots.Begin();
  CHECK(c != a && c != b && slots.Publish(c, 1, 12, true, true));
  CHECK(slots.Acquire(11) == c);
  CHECK(!slots.CanBegin() && slots.Begin() == -1);
  slots.Invalidate();
  CHECK(!slots.Release(a, 2, 10));
  CHECK(!slots.Release(a, 1, 11));
  CHECK(slots.Leased(a, 1, 10));
  CHECK(slots.Release(a, 1, 10));
  CHECK(slots.CanBegin() && slots.Begin() == a);
  CHECK(slots.Publish(a, 2, 13, true, true));
  slots.Invalidate();
  CHECK(slots.Acquire(0) == -1 && !slots.Retired());
  CHECK(slots.Release(b, 1, 11) && slots.Release(c, 1, 12));
  CHECK(slots.Retired());
  hvinput::NativeCaptureRequest photo;
  uint64_t request = photo.Request();
  CHECK(request && !photo.Request());
  CHECK(photo.Begin() && !photo.Begin());
  CHECK(!photo.Ready(request)); // no GPU/copy proof
  photo.Finish(false);
  CHECK(photo.Failed(request) && !photo.Ready(request));
  photo.Consume(request);
  uint64_t next = photo.Request();
  CHECK(next > request && photo.Begin());
  photo.Finish(true);
  CHECK(!photo.Ready(request) && photo.Ready(next));
  CHECK(!photo.Request()); // cached capture cannot be overwritten while being
                           // copied
  photo.Invalidate();
  CHECK(!photo.Ready(next) && photo.Request());
  return 0;
}
