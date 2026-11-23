#pragma once

#include "core/frame_buffer.h"

#include <array>
#include <chrono>
#include <cstdint>

namespace humanvision::runtime {

enum class FrameClockDomain : std::uint8_t {
    kUnspecified,
    kRuntimeMonotonic,
    kInputMonotonic,
    kMediaPresentation,
};

enum class FrameTimeSource : std::uint8_t {
    kUnspecified,
    kCaller,
    kPresentation,
    kReceived,
    kDecoded,
    kSubmitted,
};

struct FrameTime {
    std::int64_t microseconds = 0;
    std::uint64_t clock_id = 0;
    FrameClockDomain domain = FrameClockDomain::kUnspecified;
    FrameTimeSource source = FrameTimeSource::kUnspecified;
    bool valid = false;

    bool SharesClockWith(const FrameTime& other) const noexcept {
        return valid && other.valid && domain != FrameClockDomain::kUnspecified &&
               domain == other.domain && clock_id != 0 && clock_id == other.clock_id;
    }
};

struct FrameCrop {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;

    // Only the default all-zero rectangle denotes an unspecified crop.
    bool IsSpecified() const noexcept {
        return x != 0 || y != 0 || width != 0 || height != 0;
    }

    bool FitsWithin(std::int32_t frame_width, std::int32_t frame_height) const noexcept {
        return x >= 0 && y >= 0 && width > 0 && height > 0 &&
               std::int64_t{x} + width <= frame_width && std::int64_t{y} + height <= frame_height;
    }
};

struct FramePlane {
    std::uint64_t offset_bytes = 0;
    std::int32_t row_stride_bytes = 0;
    std::int32_t pixel_stride_bytes = 0;
};

enum class FrameColorMatrix : std::uint8_t { kUnspecified, kRgb, kBt601, kBt709, kBt2020 };
enum class FrameColorRange : std::uint8_t { kUnspecified, kFull, kLimited };
enum class FrameCompletion : std::uint8_t { kUnspecified, kReady };

// Internal-only, fixed-size data carried by a single job. A zero identity or
// invalid time has not been supplied by a producer. The copied-frame path owns
// its pixels in FrameBuffer and has no external buffer token.
struct FrameEnvelope {
    std::uint64_t source_id = 0;
    std::uint64_t generation = 0;
    std::int64_t frame_id = 0;
    std::uint64_t config_epoch = 0;
    std::int64_t region_revision = 0;
    FrameTime source_timestamp;
    FrameTime presentation;
    FrameTime received;
    FrameTime decoded;
    FrameTime submitted;
    std::int32_t width = 0;
    std::int32_t height = 0;
    FrameCrop crop;
    std::array<FramePlane, 3> planes{};
    std::uint8_t plane_count = 0;
    HV_PixelFormat format = static_cast<HV_PixelFormat>(0);
    std::uint64_t format_modifier = 0;
    FrameColorMatrix color_matrix = FrameColorMatrix::kUnspecified;
    FrameColorRange color_range = FrameColorRange::kUnspecified;
    std::int32_t rotation_degrees = 0;
    bool mirrored = false;
    // Row-major 2x3 affine transform, identity when there is no known transform.
    std::array<float, 6> source_to_analysis{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    std::uintptr_t buffer_token = 0;
    FrameCompletion completion = FrameCompletion::kUnspecified;

    static FrameEnvelope ForCopiedFrame(const HV_VideoFrame& frame, std::int64_t revision) noexcept {
        FrameEnvelope envelope;
        envelope.region_revision = revision;
        envelope.BindCopiedFrame(frame);
        return envelope;
    }

    void BindCopiedFrame(const HV_VideoFrame& frame) noexcept {
        frame_id = frame.frame_id;
        if (!source_timestamp.valid) {
            source_timestamp = {frame.timestamp_us, 0, FrameClockDomain::kUnspecified,
                                FrameTimeSource::kCaller, true};
        }

        const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        // Clock IDs are meaningful only within their explicit clock domain.
        submitted = {now, 1, FrameClockDomain::kRuntimeMonotonic, FrameTimeSource::kSubmitted, true};

        width = frame.width;
        height = frame.height;
        if (!crop.IsSpecified()) crop = {0, 0, width, height};
        planes = {};
        planes[0] = {0, frame.stride_bytes, BytesPerPixel(frame.pixel_format)};
        plane_count = 1;
        format = frame.pixel_format;
        format_modifier = 0;
        buffer_token = 0;
        completion = FrameCompletion::kReady;
    }
};

}  // namespace humanvision::runtime
