#pragma once
#include <algorithm>
#include <cstdint>

namespace humanvision::runtime {
// Capture-time credit avoids the 30 -> 15 FPS aliasing of "last accepted +
// 33333us" when capture intervals alternate just below/above that threshold.
// At most two credits survive an idle period; no accumulated catch-up queue.
class FrameRateCredit {
    int64_t last_time_=-1;
    double credit_=1;
public:
    bool Admit(int64_t timestamp_us,int fps) noexcept {
        if(timestamp_us<0||fps<1||fps>120||timestamp_us<=last_time_)return false;
        if(last_time_>=0)credit_=std::min(2.0,credit_+double(timestamp_us-last_time_)*fps/1000000.0);
        last_time_=timestamp_us;
        if(credit_+1e-9<1)return false;
        credit_=std::max(0.0,credit_-1);return true;
    }
};
}
