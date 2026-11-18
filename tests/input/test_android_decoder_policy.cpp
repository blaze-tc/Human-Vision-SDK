#include "input_android_decoder_policy.h"
#include <cstdio>

int main() {
  struct Case {
    const char* model;
    const char* api;
    const char* codec;
    bool production;
    int expected;
  };
  const Case cases[] = {
    {"HLK-AL00", "29", "OMX.hisi.video.decoder.avc", true, 3},
    {"HLK-AL00", "29", "OMX.hisi.video.decoder.avc", false, 4},
    {"HLK-AL00", "29", "OMX.google.h264.decoder", true, 4},
    {"HLK-AL00", "30", "OMX.hisi.video.decoder.avc", true, 4},
    {"HLK-AL10", "29", "OMX.hisi.video.decoder.avc", true, 4},
    {"LE2120", "31", "c2.qti.avc.decoder", true, 4},
    {"", "", "OMX.hisi.video.decoder.avc", true, 4},
  };
  for (const auto& c : cases) {
    const auto actual = hvinput::AndroidPrivateReaderDepth(c.model, c.api, c.codec, c.production);
    if (actual != c.expected) {
      std::fprintf(stderr, "Private reader depth model=%s api=%s codec=%s production=%d: expected %d got %d\n",
                   c.model, c.api, c.codec, c.production, c.expected, actual);
      return 1;
    }
  }
}
