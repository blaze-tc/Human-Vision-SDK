#pragma once
#include <algorithm>
#include <cstdint>

namespace hvinput {
// Original-size mode is bounded by an explicit supported extent, never silently
// reduced to that extent. Positive bounds preserve the existing opt-in resize.
constexpr uint32_t kMaxInputRgbaDimension = 4096;
inline bool ValidInputBounds(uint32_t width, uint32_t height) {
  return (!width && !height) ||
      (width && height && width <= kMaxInputRgbaDimension && height <= kMaxInputRgbaDimension);
}
inline bool FitInputDimensions(uint32_t source_width, uint32_t source_height,
                               uint32_t max_width, uint32_t max_height,
                               uint32_t& width, uint32_t& height) {
  width = height = 0;
  if (!source_width || !source_height || !ValidInputBounds(max_width, max_height)) return false;
  if (!max_width) {
    if (source_width > kMaxInputRgbaDimension || source_height > kMaxInputRgbaDimension) return false;
    width = source_width; height = source_height;
    return true;
  }
  const double scale = std::min(1.0, std::min(double(max_width) / source_width,
                                             double(max_height) / source_height));
  width = std::max(1u, static_cast<uint32_t>(source_width * scale));
  height = std::max(1u, static_cast<uint32_t>(source_height * scale));
  return true;
}
// Android decoder initialization always needs a real positive hint. The hint
// does not select or resize the output; actual visible geometry comes from crop.
inline bool ResolveInputDecoderHint(uint32_t max_width, uint32_t max_height,
                                     uint32_t source_width, uint32_t source_height,
                                     uint32_t& width, uint32_t& height) {
  width = height = 0;
  if (!ValidInputBounds(max_width, max_height)) return false;
  if (max_width) { width = max_width; height = max_height; return true; }
  return FitInputDimensions(source_width, source_height, 0, 0, width, height);
}
} // namespace hvinput
