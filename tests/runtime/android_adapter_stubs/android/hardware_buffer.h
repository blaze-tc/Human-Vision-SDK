#pragma once
#include <cstdint>

struct AHardwareBuffer {
  uint32_t references = 1;
};
struct AHardwareBuffer_Desc {
  uint32_t width = 0, height = 0, layers = 0, format = 0;
  uint64_t usage = 0;
  uint32_t stride = 0, rfu0 = 0;
  uint64_t rfu1 = 0;
};
extern "C" {
int AHardwareBuffer_allocate(const AHardwareBuffer_Desc *, AHardwareBuffer **);
void AHardwareBuffer_acquire(AHardwareBuffer *);
void AHardwareBuffer_describe(const AHardwareBuffer *, AHardwareBuffer_Desc *);
void AHardwareBuffer_release(AHardwareBuffer *);
}
