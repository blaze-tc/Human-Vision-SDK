#include "core/latest_frame_slot.h"

#include <algorithm>
#include <cstddef>

namespace humanvision::latest_frame_slot_detail {

void CopyValidatedFrame(const HV_VideoFrame& frame, FrameBuffer& destination) {
    const std::size_t required_bytes =
        static_cast<std::size_t>(frame.stride_bytes) *
        static_cast<std::size_t>(frame.height);
    destination.bytes.resize(required_bytes);
    const auto* source = static_cast<const std::uint8_t*>(frame.data);
    std::copy_n(source, required_bytes, destination.bytes.data());
    destination.width = frame.width;
    destination.height = frame.height;
    destination.stride_bytes = frame.stride_bytes;
    destination.pixel_format = frame.pixel_format;
    destination.frame_id = frame.frame_id;
    destination.timestamp_us = frame.timestamp_us;
}

}  // namespace humanvision::latest_frame_slot_detail
