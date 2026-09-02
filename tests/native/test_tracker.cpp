#include "tracking/center_iou_tracker.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

humanvision::Detection Box(const float x, const float y = 0.0F) {
    return humanvision::Detection{x, y, x + 20.0F, y + 40.0F, 0.9F};
}

int TrackAtX(
    const std::vector<humanvision::TrackedDetection>& tracks,
    const float x) {
    const auto found = std::min_element(
        tracks.begin(), tracks.end(), [x](const auto& left, const auto& right) {
            return std::abs(left.detection.x1 - x) <
                   std::abs(right.detection.x1 - x);
        });
    return found == tracks.end() ? -1 : found->track_id;
}

TEST(CenterIouTracker, KeepsIdDuringContinuousMotion) {
    humanvision::CenterIouTracker tracker;
    std::vector<humanvision::TrackedDetection> output;
    tracker.Update({Box(0.0F)}, 0, output);
    ASSERT_EQ(output.size(), 1U);
    const int id = output[0].track_id;
    for (int frame = 1; frame <= 8; ++frame) {
        tracker.Update({Box(frame * 5.0F)}, frame * 33333LL, output);
        ASSERT_EQ(output.size(), 1U);
        EXPECT_EQ(output[0].track_id, id);
    }
}

TEST(CenterIouTracker, RetainsIdAcrossShortMissingDetections) {
    humanvision::CenterIouTracker tracker;
    std::vector<humanvision::TrackedDetection> output;
    tracker.Update({Box(10.0F)}, 0, output);
    ASSERT_EQ(output.size(), 1U);
    const int id = output[0].track_id;
    tracker.Update({}, 33333, output);
    EXPECT_TRUE(output.empty());
    tracker.Update({}, 66666, output);
    EXPECT_TRUE(output.empty());
    tracker.Update({Box(18.0F)}, 99999, output);
    ASSERT_EQ(output.size(), 1U);
    EXPECT_EQ(output[0].track_id, id);
}

TEST(CenterIouTracker, VelocityPredictionPreservesIdsThroughTwoPersonCrossing) {
    humanvision::CenterIouTracker tracker;
    std::vector<humanvision::TrackedDetection> output;
    tracker.Update({Box(0.0F), Box(80.0F)}, 0, output);
    ASSERT_EQ(output.size(), 2U);
    const int left_to_right_id = TrackAtX(output, 0.0F);
    const int right_to_left_id = TrackAtX(output, 80.0F);
    ASSERT_NE(left_to_right_id, right_to_left_id);

    tracker.Update({Box(65.0F), Box(15.0F)}, 33333, output);
    EXPECT_EQ(TrackAtX(output, 15.0F), left_to_right_id);
    EXPECT_EQ(TrackAtX(output, 65.0F), right_to_left_id);
    tracker.Update({Box(50.0F), Box(30.0F)}, 66666, output);
    EXPECT_EQ(TrackAtX(output, 30.0F), left_to_right_id);
    EXPECT_EQ(TrackAtX(output, 50.0F), right_to_left_id);
    tracker.Update({Box(35.0F), Box(45.0F)}, 99999, output);
    EXPECT_EQ(TrackAtX(output, 45.0F), left_to_right_id);
    EXPECT_EQ(TrackAtX(output, 35.0F), right_to_left_id);
    tracker.Update({Box(60.0F), Box(20.0F)}, 133332, output);
    EXPECT_EQ(TrackAtX(output, 60.0F), left_to_right_id);
    EXPECT_EQ(TrackAtX(output, 20.0F), right_to_left_id);
}

TEST(CenterIouTracker, AssignsUniqueMonotonicIds) {
    humanvision::CenterIouTracker tracker;
    std::vector<humanvision::TrackedDetection> output;
    tracker.Update({Box(0.0F), Box(100.0F), Box(200.0F)}, 0, output);
    ASSERT_EQ(output.size(), 3U);
    EXPECT_GT(output[0].track_id, 0);
    EXPECT_NE(output[0].track_id, output[1].track_id);
    EXPECT_NE(output[0].track_id, output[2].track_id);
    EXPECT_NE(output[1].track_id, output[2].track_id);
}

}  // namespace
