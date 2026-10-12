#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace humanvision::runtime {
// Check the entire tensor, including background anchors. Integer exponent
// tests keep NaN rejection independent of floating-point control settings.
// ARM64 checks four values at once; tails never read outside the input.
inline bool AllFinite(const float* values, size_t count) noexcept {
    if (!values) return count == 0;
    size_t i = 0;
#if defined(__aarch64__)
    const auto exponent = vdupq_n_u32(0x7f800000u);
    for (; i + 4 <= count; i += 4) {
        const auto bits = vreinterpretq_u32_f32(vld1q_f32(values + i));
        if (vmaxvq_u32(vceqq_u32(vandq_u32(bits, exponent), exponent))) return false;
    }
#endif
    for (; i < count; ++i) {
        uint32_t bits; std::memcpy(&bits, values + i, sizeof(bits));
        if ((bits & 0x7f800000u) == 0x7f800000u) return false;
    }
    return true;
}
}
