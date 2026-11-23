#pragma once
#include "android_input_gpu.h"
#include "input_image_cache.h"
#include <cstring>
namespace hvinput {
inline ImageContract DecodedSamplingContract(const InputGpuCapabilities &c) {
  ImageContract contract{};
  contract.external_format = c.external_format;
  // PRIVATE decoding always selects the queried external representation. Even
  // when the AHB also advertises a concrete format, image, conversion and view
  // must all use VK_FORMAT_UNDEFINED with this external-format chain.
  contract.format = 0;
  contract.features = c.format_features;
  contract.model = c.model;
  contract.range = c.range;
  contract.x_chroma = c.x_chroma;
  contract.y_chroma = c.y_chroma;
  std::memcpy(contract.components, c.components, sizeof(c.components));
  return contract;
}
} // namespace hvinput
