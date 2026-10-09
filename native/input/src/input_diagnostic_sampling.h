#pragma once
#include <cstdint>

namespace hvinput {
// Diagnostic qualification retains every frame. Production emits startup proof
// and one complete input detail set per 64 frames instead of formatting/logging
// the same crop, fd and conversion details on every frame. Errors, codec changes
// and terminal ownership reports are never gated by this selector.
inline bool ShouldLogInputFrame(uint64_t frame, bool diagnostic) noexcept {
    return diagnostic || (frame != 0 && (frame <= 3 || frame % 64 == 0));
}
}
