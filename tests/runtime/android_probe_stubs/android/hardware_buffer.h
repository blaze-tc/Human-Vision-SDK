#pragma once
#include <cstdint>

struct AHardwareBuffer {};
struct AHardwareBuffer_Desc {
    uint32_t width, height, layers, format;
    uint64_t usage;
    uint32_t stride, rfu0;
    uint64_t rfu1;
};

int AHardwareBuffer_allocate(const AHardwareBuffer_Desc*, AHardwareBuffer**);
void AHardwareBuffer_describe(const AHardwareBuffer*, AHardwareBuffer_Desc*);
void AHardwareBuffer_release(AHardwareBuffer*);
