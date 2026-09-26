#pragma once
#include "humanvision_plugin_v3.h"
#include <cstdint>
#include <cstring>

namespace humanvision::runtime {
// ncnn's dense 2-D VkMat output omits the leading ONNX batch dimension.
// Both layouts must still describe exactly the pinned logical [1, rows, cols]
// tensor and exact FP32 download byte count.
inline bool MatchesTopDownTensor(const HV_TensorViewV1& view, const char* name,
                                 int rows, int cols) noexcept {
    if (!view.name || !name || std::strcmp(view.name, name) != 0 ||
        view.element_type != 1 || !view.data || rows <= 0 || cols <= 0 ||
        view.byte_count != static_cast<uint64_t>(rows) * cols * sizeof(float))
        return false;
    if (view.rank == 2)
        return view.dimensions[0] == rows && view.dimensions[1] == cols &&
               view.dimensions[2] == 0 && view.dimensions[3] == 0;
    if (view.rank == 3)
        return view.dimensions[0] == 1 && view.dimensions[1] == rows &&
               view.dimensions[2] == cols && view.dimensions[3] == 0;
    return false;
}
}
