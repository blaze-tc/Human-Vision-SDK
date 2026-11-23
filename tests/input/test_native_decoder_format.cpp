#include "android/decoded_sampling_contract.h"
#include <cstdio>
int main() {
  for (auto concrete : {0u, 1000156003u}) {
    hvinput::InputGpuCapabilities reported{};
    reported.external_format = 987654;
    reported.format = concrete;
    reported.model = 2;
    auto selected = hvinput::DecodedSamplingContract(reported);
    if (selected.format != 0 || selected.external_format != 987654 ||
        selected.model != 2) {
      std::fprintf(stderr,
                   "external image/conversion/view must use "
                   "VK_FORMAT_UNDEFINED, got %u\n",
                   selected.format);
      return 1;
    }
  }
  return 0;
}
