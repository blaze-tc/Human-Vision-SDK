#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace humanvision::runtime::ncnn_backend {
struct DenseOutputLayout {
    uint32_t rank = 0;
    std::array<int32_t, 4> dimensions{};
    size_t values_per_channel = 0;
    size_t channels = 0;
    size_t channel_stride = 0;
    size_t logical_bytes = 0;
    size_t storage_bytes = 0;
};

bool DescribeDenseOutput(int dims, int w, int h, int d, int c, size_t cstep,
                         uint64_t max_output_bytes, DenseOutputLayout& result) noexcept;
bool CompactDenseFp32(const float* padded, const DenseOutputLayout& layout,
                      float* dense, size_t dense_capacity) noexcept;
bool ValidateDenseDownload(const DenseOutputLayout& layout,
                           size_t physical_values) noexcept;
}
