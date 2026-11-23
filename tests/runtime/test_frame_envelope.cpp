#include "common/frame_envelope.h"
#include "core/latest_frame_slot.h"

#include <array>
#include <gtest/gtest.h>

namespace {

using namespace humanvision;
using namespace humanvision::runtime;

TEST(FrameEnvelope, DelayedCompletionBeyond64Frames) {
    LatestFrameSlotWithMetadata<FrameEnvelope> slot;
    FrameSubmitStatus status{};
    std::string error;
    std::array<std::uint8_t, 4> pixels{7, 0, 0, 255};
    HV_VideoFrame frame{sizeof(frame), 1, 1, 4, HV_PIXEL_RGBA32, 1, 1000, pixels.data(), 4};
    auto envelope = FrameEnvelope::ForCopiedFrame(frame, 31);
    envelope.source_id = 17;
    envelope.generation = 2;
    envelope.config_epoch = 23;
    envelope.source_to_analysis[2] = 3.5F;
    ASSERT_TRUE(slot.Submit(frame, envelope, status, error)) << error;

    FrameBuffer active;
    FrameEnvelope active_envelope;
    ASSERT_TRUE(slot.WaitTake(active, active_envelope));
    for (std::int64_t id = 2; id <= 129; ++id) {
        frame.frame_id = id;
        frame.timestamp_us = id * 1000;
        pixels[0] = static_cast<std::uint8_t>(id);
        envelope = FrameEnvelope::ForCopiedFrame(frame, id + 31);
        envelope.source_id = 91;
        ASSERT_TRUE(slot.Submit(frame, envelope, status, error)) << error;
    }

    EXPECT_EQ(active.bytes[0], 7);
    EXPECT_EQ(active_envelope.frame_id, 1);
    EXPECT_EQ(active_envelope.source_id, 17u);
    EXPECT_EQ(active_envelope.generation, 2u);
    EXPECT_EQ(active_envelope.config_epoch, 23u);
    EXPECT_EQ(active_envelope.region_revision, 31);
    EXPECT_EQ(active_envelope.source_timestamp.microseconds, 1000);
    EXPECT_FLOAT_EQ(active_envelope.source_to_analysis[2], 3.5F);

    FrameBuffer latest;
    FrameEnvelope latest_envelope;
    ASSERT_TRUE(slot.WaitTake(latest, latest_envelope));
    EXPECT_EQ(latest.bytes[0], 129);
    EXPECT_EQ(latest_envelope.frame_id, 129);
    EXPECT_EQ(latest_envelope.region_revision, 160);
    EXPECT_EQ(slot.dropped_frames(), 127);
}

TEST(FrameEnvelope, OriginalMetadataSurvivesReplacement) {
    LatestFrameSlotWithMetadata<FrameEnvelope> slot;
    FrameSubmitStatus status{};
    std::string error;
    std::array<std::uint8_t, 8> pixels{5, 3, 2, 255, 114, 115, 116, 117};
    HV_VideoFrame frame{sizeof(frame), 1, 1, 8, HV_PIXEL_RGBA32, 64, 8000, pixels.data(), 8};
    const auto first = FrameEnvelope::ForCopiedFrame(frame, 20);
    ASSERT_TRUE(slot.Submit(frame, first, status, error));

    // A repeated V1 frame identity is legal with one worker. Replacing it
    // still replaces the entire pending job.
    auto replacement = FrameEnvelope::ForCopiedFrame(frame, 21);
    replacement.source_id = 6;
    replacement.source_to_analysis[5] = 2.25F;
    pixels[0] = 8;
    ASSERT_TRUE(slot.Submit(frame, replacement, status, error));
    ASSERT_EQ(status, FrameSubmitStatus::kReplacedPending);
    pixels.fill(99);
    replacement = {};

    FrameBuffer taken;
    FrameEnvelope taken_envelope;
    ASSERT_TRUE(slot.WaitTake(taken, taken_envelope));
    EXPECT_EQ(taken.bytes[0], 8);
    EXPECT_EQ(taken.bytes[7], 117);
    EXPECT_EQ(taken_envelope.frame_id, 64);
    EXPECT_EQ(taken_envelope.region_revision, 21);
    EXPECT_EQ(taken_envelope.source_id, 6u);
    EXPECT_FLOAT_EQ(taken_envelope.source_to_analysis[5], 2.25F);
    EXPECT_EQ(taken_envelope.width, 1);
    EXPECT_EQ(taken_envelope.height, 1);
    EXPECT_EQ(taken_envelope.crop.width, 1);
    EXPECT_EQ(taken_envelope.planes[0].row_stride_bytes, 8);
    EXPECT_EQ(taken_envelope.planes[0].pixel_stride_bytes, 4);
    EXPECT_EQ(taken_envelope.format, HV_PIXEL_RGBA32);
}

TEST(FrameEnvelope, ClockDomainsAreExplicit) {
    const HV_VideoFrame frame{sizeof(frame), 1, 1, 4, HV_PIXEL_RGBA32, 9, 482000, nullptr, 4};
    const auto envelope = FrameEnvelope::ForCopiedFrame(frame, 0);
    EXPECT_TRUE(envelope.source_timestamp.valid);
    EXPECT_EQ(envelope.source_timestamp.source, FrameTimeSource::kCaller);
    EXPECT_EQ(envelope.source_timestamp.domain, FrameClockDomain::kUnspecified);
    EXPECT_EQ(envelope.source_timestamp.microseconds, 482000);
    EXPECT_TRUE(envelope.submitted.valid);
    EXPECT_EQ(envelope.submitted.domain, FrameClockDomain::kRuntimeMonotonic);
    EXPECT_FALSE(envelope.presentation.valid);
    EXPECT_FALSE(envelope.received.valid);
    EXPECT_FALSE(envelope.decoded.valid);
    EXPECT_FALSE(envelope.source_timestamp.SharesClockWith(envelope.submitted));
    EXPECT_FALSE(envelope.source_timestamp.SharesClockWith(envelope.source_timestamp));
    const FrameTime input_time{482000, 1, FrameClockDomain::kInputMonotonic, FrameTimeSource::kReceived, true};
    EXPECT_FALSE(input_time.SharesClockWith(envelope.submitted));
    EXPECT_TRUE(envelope.submitted.SharesClockWith(FrameEnvelope::ForCopiedFrame(frame, 0).submitted));
}

TEST(FrameEnvelope, LeaseReleasedExactlyOnce) {
    LatestFrameSlotWithMetadata<FrameEnvelope> slot;
    FrameSubmitStatus status{};
    std::string error;
    std::array<std::uint8_t, 4> pixels{5, 0, 0, 255};
    HV_VideoFrame frame{sizeof(frame), 1, 1, 4, HV_PIXEL_RGBA32, 1, 1000, pixels.data(), 4};
    FrameBuffer active;
    FrameEnvelope envelope;

    ASSERT_TRUE(slot.Submit(frame, FrameEnvelope::ForCopiedFrame(frame, 0), status, error));
    ASSERT_TRUE(slot.WaitTake(active, envelope));
    const auto* first_owner = active.bytes.data();
    frame.frame_id = 2;
    ASSERT_TRUE(slot.Submit(frame, FrameEnvelope::ForCopiedFrame(frame, 0), status, error));
    ASSERT_TRUE(slot.WaitTake(active, envelope));
    const auto* second_owner = active.bytes.data();
    EXPECT_NE(first_owner, second_owner);

    // Taking the next copied job releases the previous backing allocation
    // into the pending slot. Stop can still drain the one submitted job.
    frame.frame_id = 3;
    ASSERT_TRUE(slot.Submit(frame, FrameEnvelope::ForCopiedFrame(frame, 0), status, error));
    slot.Stop();
    ASSERT_TRUE(slot.WaitTake(active, envelope));
    EXPECT_EQ(active.bytes.data(), first_owner);
    EXPECT_EQ(envelope.frame_id, 3);
    EXPECT_FALSE(slot.WaitTake(active, envelope));
    EXPECT_EQ(active.bytes.data(), first_owner);
    EXPECT_EQ(envelope.frame_id, 3);
}

}  // namespace
