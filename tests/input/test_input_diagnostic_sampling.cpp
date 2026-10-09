#include <cstdio>
#if __has_include("input_diagnostic_sampling.h")
#include "input_diagnostic_sampling.h"
#endif

int main() {
#if !__has_include("input_diagnostic_sampling.h")
    std::puts("FAIL missing bounded production input diagnostic policy");
    return 1;
#else
    unsigned logged = 0;
    for (uint64_t frame = 1; frame <= 10000; ++frame) {
        const bool selected = hvinput::ShouldLogInputFrame(frame, false);
        if (selected) ++logged;
        if (selected != (frame <= 3 || frame % 64 == 0)) return 2;
        if (!hvinput::ShouldLogInputFrame(frame, true)) return 3; // Private qualification keeps full proof.
    }
    if (logged != 159 || hvinput::ShouldLogInputFrame(0, false)) return 4;
    if (!hvinput::ShouldLogInputFrame(UINT64_MAX - 63, false)) return 5;
    std::printf("PASS 10000 production frames -> %u details; diagnostic gate retains every frame\n", logged);
    return 0;
#endif
}
