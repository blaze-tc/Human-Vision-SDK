#pragma once
#include <cmath>
#include <cstddef>
#include <cstring>
#include "common/finite_tensor.h"

namespace humanvision::runtime::rknn {
// Qualification uses complete FP32 tensors from the SAME RGB frame. Warm-up
// comparisons still enforce equality; only their timing is excluded.
class InputQualification {
public:
    enum class State { Pending, Accepted, Rejected };
    State state = State::Pending;
    unsigned compared = 0, measured = 0;
    double baseline_ms = 0, candidate_ms = 0;
    static bool Equal(const float* baseline, const float* candidate, size_t count) {
        if (!baseline || !candidate || !count) return false;
        // A whole-buffer comparison is equivalent to per-value bit equality,
        // including signed zero. Once equal, validate only one copy for NaNs.
        return count<=SIZE_MAX/sizeof(float) &&
            std::memcmp(baseline,candidate,count*sizeof(float))==0 && AllFinite(baseline,count);
    }
    void Observe(bool equal, double baseline, double candidate) {
        if (state != State::Pending) return;
        if (!equal || !std::isfinite(baseline) || !std::isfinite(candidate) || baseline<=0 || candidate<=0) {
            state=State::Rejected;return;
        }
        if (++compared<=2) return;
        baseline_ms+=baseline;candidate_ms+=candidate;
        if (++measured==5) state=candidate_ms<=baseline_ms*0.95?State::Accepted:State::Rejected;
    }
};
}
