#include "models/rtmpose/rtmpose_preprocess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace humanvision {

namespace {

constexpr int kInputWidth = 192;
constexpr int kInputHeight = 256;
constexpr std::array<float, 3> kMean = {123.675F, 116.28F, 103.53F};
constexpr std::array<float, 3> kStd = {58.395F, 57.12F, 57.375F};

std::array<std::uint8_t, 3> ReadRgb(
    const FrameBuffer& frame,
    const int x,
    const int y, const int bytes_per_pixel) {
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
        return {0, 0, 0};
    }
    const std::size_t offset =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride_bytes) +
        static_cast<std::size_t>(x) * static_cast<std::size_t>(bytes_per_pixel);
    const auto* pixel = frame.bytes.data() + offset;
    switch (frame.pixel_format) {
        case HV_PIXEL_RGB24:
        case HV_PIXEL_RGBA32:
            return {pixel[0], pixel[1], pixel[2]};
        case HV_PIXEL_BGR24:
        case HV_PIXEL_BGRA32:
            return {pixel[2], pixel[1], pixel[0]};
        default:
            return {0, 0, 0};
    }
}

std::array<std::uint8_t, 3> BilinearRgb(
    const FrameBuffer& frame,
    const float source_x,
    const float source_y,
    const int bytes_per_pixel) {
    const int x0 = static_cast<int>(std::floor(source_x));
    const int y0 = static_cast<int>(std::floor(source_y));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const float fraction_x = source_x - static_cast<float>(x0);
    const float fraction_y = source_y - static_cast<float>(y0);
    const auto top_left = ReadRgb(frame, x0, y0, bytes_per_pixel);
    const auto top_right = ReadRgb(frame, x1, y0, bytes_per_pixel);
    const auto bottom_left = ReadRgb(frame, x0, y1, bytes_per_pixel);
    const auto bottom_right = ReadRgb(frame, x1, y1, bytes_per_pixel);
    std::array<std::uint8_t, 3> result{};
    for (int channel = 0; channel < 3; ++channel) {
        const float top = top_left[static_cast<std::size_t>(channel)] *
                              (1.0F - fraction_x) +
                          top_right[static_cast<std::size_t>(channel)] * fraction_x;
        const float bottom = bottom_left[static_cast<std::size_t>(channel)] *
                                 (1.0F - fraction_x) +
                             bottom_right[static_cast<std::size_t>(channel)] * fraction_x;
        result[channel] = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(std::lround(top * (1.0F - fraction_y) + bottom * fraction_y)),
            0,
            255));
    }
    return result;
}

}  // namespace

bool PreprocessRtmpose(
    const FrameBuffer& frame,
    const Detection& detection,
    PoseInput& destination,
    std::string& error) {
    const int bytes_per_pixel = BytesPerPixel(frame.pixel_format);
    if (frame.width <= 0 || frame.height <= 0 || bytes_per_pixel == 0 ||
        frame.stride_bytes < frame.width * bytes_per_pixel ||
        frame.bytes.size() <
            static_cast<std::size_t>(frame.stride_bytes) * frame.height) {
        error = "invalid owned frame for RTMPose preprocessing";
        return false;
    }
    if (!BuildPoseAffine(detection, destination.transform, error)) {
        return false;
    }

    constexpr std::size_t plane_size =
        static_cast<std::size_t>(kInputWidth) * kInputHeight;
    destination.normalized_chw.resize(plane_size * 3U);
    for (int y = 0; y < kInputHeight; ++y) {
        for (int x = 0; x < kInputWidth; ++x) {
            const Point2f source = TransformPoint(
                destination.transform.input_to_source,
                Point2f{static_cast<float>(x), static_cast<float>(y)});
            const auto pixel = BilinearRgb(frame, source.x, source.y, bytes_per_pixel);
            for (int channel = 0; channel < 3; ++channel) {
                const std::uint8_t value =
                    pixel[channel];
                const std::size_t index =
                    static_cast<std::size_t>(channel) * plane_size +
                    static_cast<std::size_t>(y) * kInputWidth + x;
                destination.normalized_chw[index] =
                    (static_cast<float>(value) - kMean[channel]) / kStd[channel];
            }
        }
    }
    error.clear();
    return true;
}

}  // namespace humanvision
