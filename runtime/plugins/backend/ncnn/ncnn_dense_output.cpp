#include "plugins/backend/ncnn/ncnn_dense_output.h"
#include <cstring>
#include <limits>

namespace humanvision::runtime::ncnn_backend {
bool DescribeDenseOutput(int dims, int w, int h, int d, int c, size_t cstep,
                         uint64_t max_output_bytes, DenseOutputLayout& result) noexcept {
    result = {};
    if (dims < 1 || dims > 4 || w <= 0 || (dims >= 2 && h <= 0) ||
        (dims >= 3 && c <= 0) || (dims == 4 && d <= 0)) return false;
    size_t per_channel = static_cast<size_t>(w);
    if (dims >= 2) {
        if (per_channel > std::numeric_limits<size_t>::max() / static_cast<size_t>(h)) return false;
        per_channel *= static_cast<size_t>(h);
    }
    if (dims == 4) {
        if (per_channel > std::numeric_limits<size_t>::max() / static_cast<size_t>(d)) return false;
        per_channel *= static_cast<size_t>(d);
    }
    const size_t channels = dims >= 3 ? static_cast<size_t>(c) : 1;
    if (per_channel > std::numeric_limits<size_t>::max() / channels ||
        per_channel * channels > max_output_bytes / sizeof(float) ||
        cstep < per_channel || cstep - per_channel > 3 || cstep % 4 != 0 ||
        cstep > std::numeric_limits<size_t>::max() / channels / sizeof(float)) return false;
    result.rank = static_cast<uint32_t>(dims);
    result.dimensions[dims - 1] = w;
    if (dims >= 2) result.dimensions[dims - 2] = h;
    if (dims == 3) result.dimensions[0] = c;
    if (dims == 4) { result.dimensions[0] = c; result.dimensions[1] = d; }
    result.values_per_channel = per_channel;
    result.channels = channels;
    result.channel_stride = cstep;
    result.logical_bytes = per_channel * channels * sizeof(float);
    result.storage_bytes = cstep * channels * sizeof(float);
    return true;
}

bool ValidateDenseDownload(const DenseOutputLayout& layout,
                           size_t physical_values) noexcept {
    return layout.rank >= 1 && layout.rank <= 4 &&
           layout.storage_bytes / sizeof(float) == physical_values;
}

bool CompactDenseFp32(const float* padded, const DenseOutputLayout& layout,
                      float* dense, size_t dense_capacity) noexcept {
    if (!padded || !dense || !layout.rank || !layout.values_per_channel ||
        layout.values_per_channel > layout.channel_stride ||
        layout.channels > dense_capacity / layout.values_per_channel) return false;
    for (size_t channel = 0; channel < layout.channels; ++channel)
        std::memcpy(dense + channel * layout.values_per_channel,
                    padded + channel * layout.channel_stride,
                    layout.values_per_channel * sizeof(float));
    return true;
}
}
