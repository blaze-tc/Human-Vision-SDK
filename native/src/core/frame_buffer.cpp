#include "core/frame_buffer.h"

#include <cstddef>
#include <limits>

namespace humanvision {

int BytesPerPixel(const HV_PixelFormat format) noexcept {
    switch (format) {
        case HV_PIXEL_RGBA32:
        case HV_PIXEL_BGRA32:
            return 4;
        case HV_PIXEL_RGB24:
        case HV_PIXEL_BGR24:
            return 3;
        default:
            return 0;
    }
}

HV_Result ValidateVideoFrame(const HV_VideoFrame* frame, std::string& error) {
    if (frame == nullptr) {
        error = "frame is null";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (frame->struct_size < static_cast<std::int32_t>(sizeof(HV_VideoFrame))) {
        error = "frame.struct_size is smaller than HV_VideoFrame";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (frame->width <= 0 || frame->height <= 0) {
        error = "frame width and height must be positive";
        return HV_ERR_INVALID_ARGUMENT;
    }
    const int bytes_per_pixel = BytesPerPixel(frame->pixel_format);
    if (bytes_per_pixel == 0) {
        error = "unsupported pixel format";
        return HV_ERR_UNSUPPORTED_FORMAT;
    }
    if (frame->width > std::numeric_limits<std::int32_t>::max() / bytes_per_pixel) {
        error = "frame row size overflows int32";
        return HV_ERR_INVALID_ARGUMENT;
    }
    const std::int32_t minimum_stride = frame->width * bytes_per_pixel;
    if (frame->stride_bytes < minimum_stride) {
        error = "frame stride is smaller than the packed row size";
        return HV_ERR_INVALID_ARGUMENT;
    }
    const std::int64_t required_bytes =
        static_cast<std::int64_t>(frame->stride_bytes) * frame->height;
    if (required_bytes > std::numeric_limits<std::int32_t>::max() ||
        frame->data_bytes < required_bytes) {
        error = "frame data_bytes is smaller than stride_bytes * height";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (frame->data == nullptr) {
        error = "frame data is null";
        return HV_ERR_INVALID_ARGUMENT;
    }
    error.clear();
    return HV_OK;
}

}  // namespace humanvision
