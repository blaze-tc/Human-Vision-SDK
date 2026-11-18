#pragma once
#include <string_view>

namespace hvinput {
inline constexpr int kAndroidPrivateReaderDepth = 4;

constexpr int AndroidPrivateReaderDepth(std::string_view model, std::string_view api,
                                        std::string_view codec, bool production) noexcept {
  // The HLK-AL00 Android 10 OMX decoder rejects the 14-buffer minimum
  // requested at port reconfiguration with four acquired images allowed.
  // Three allows the observed 13-buffer allocation on the same PRIVATE Surface.
  if (production && model == "HLK-AL00" && api == "29" && codec == "OMX.hisi.video.decoder.avc")
    return 3;
  return kAndroidPrivateReaderDepth;
}
}  // namespace hvinput
