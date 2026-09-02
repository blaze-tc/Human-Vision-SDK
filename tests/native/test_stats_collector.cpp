#include "core/stats_collector.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace {

TEST(StatsCollector, ReportsCountsFpsAndLatestStageTimings) {
    humanvision::StatsCollector collector;
    collector.RecordSubmitted(false);
    collector.RecordSubmitted(true);
    collector.RecordProcessed(humanvision::StageTimings{10.0F, 20.0F, 1.0F, 31.0F});
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    const HV_Stats stats = collector.Snapshot(1);
    EXPECT_EQ(stats.struct_size, sizeof(HV_Stats));
    EXPECT_EQ(stats.submitted_frames, 2);
    EXPECT_EQ(stats.processed_frames, 1);
    EXPECT_EQ(stats.dropped_frames, 1);
    EXPECT_GT(stats.input_fps, 0.0F);
    EXPECT_GT(stats.inference_fps, 0.0F);
    EXPECT_FLOAT_EQ(stats.detection_ms, 10.0F);
    EXPECT_FLOAT_EQ(stats.pose_ms, 20.0F);
    EXPECT_FLOAT_EQ(stats.tracking_ms, 1.0F);
    EXPECT_FLOAT_EQ(stats.total_ms, 31.0F);
}

}  // namespace
