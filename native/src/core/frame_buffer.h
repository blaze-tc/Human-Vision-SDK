#pragma once

#include "humanvision/humanvision_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace humanvision {

struct FrameBuffer {
    int width = 0;
    int height = 0;
    int stride_bytes = 0;
    HV_PixelFormat pixel_format = HV_PIXEL_BGR24;
    std::int64_t frame_id = 0;
    std::int64_t timestamp_us = 0;
    std::vector<std::uint8_t> bytes;
};

int BytesPerPixel(HV_PixelFormat format) noexcept;
HV_Result ValidateVideoFrame(const HV_VideoFrame* frame, std::string& error);

}  // namespace humanvision
