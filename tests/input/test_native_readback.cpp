#include "android/native_vulkan_readback.h"
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
  int waits = 0, invalidations = 0;
  auto failed_wait = [&] {
    ++waits;
    return false;
  };
  auto wait = [&] {
    ++waits;
    return true;
  };
  auto invalidate = [&] {
    ++invalidations;
    return true;
  };
  auto failed_invalidate = [&] {
    ++invalidations;
    return false;
  };
  CHECK(CompleteNativeReadback(false, failed_wait, invalidate) ==
        NativeReadbackProof::WaitFailed);
  CHECK(waits == 1 && invalidations == 0);
  CHECK(CompleteNativeReadback(false, wait, failed_invalidate) ==
        NativeReadbackProof::InvalidateFailed);
  CHECK(waits == 2 && invalidations == 1);
  CHECK(CompleteNativeReadback(true, wait, failed_invalidate) ==
        NativeReadbackProof::Visible);
  CHECK(waits == 3 && invalidations == 1);
  CHECK(CompleteNativeReadback(false, wait, invalidate) ==
        NativeReadbackProof::Visible);
  CHECK(waits == 4 && invalidations == 2);
  return 0;
}
