#pragma once

#include <cstdint>
#include <cstring>

// Also compiled by the pinned NCNN dependency's C++11 toolchain.
namespace humanvision { namespace runtime { namespace ncnn_backend {

// Command batching is limited to the measured device and FP32 option combination.
// This policy removes no waits: CPU transitions and final GPU outputs still wait.
struct DispatchPrecision {
    bool fp16_packed, fp16_storage, fp16_arithmetic, subgroup, packing_layout;
    DispatchPrecision(bool packed = false, bool storage = false, bool arithmetic = false,
                      bool subgroup_ops = false, bool packing = true) noexcept
        : fp16_packed(packed), fp16_storage(storage), fp16_arithmetic(arithmetic),
          subgroup(subgroup_ops), packing_layout(packing) {}
};

inline uint32_t PendingDispatchBudget(const char* device_name,
                                      const DispatchPrecision& precision) noexcept {
    constexpr uint32_t baseline = 32u * 1024u;
    if (!device_name || std::strcmp(device_name, "Adreno (TM) 660") != 0 ||
        precision.fp16_packed || precision.fp16_storage || precision.fp16_arithmetic ||
        precision.subgroup || !precision.packing_layout) {
        return baseline;
    }
    // A finite 256K budget; neither unlimited batching nor a falsified device score.
    return 256u * 1024u;
}

}}} // namespace humanvision::runtime::ncnn_backend
