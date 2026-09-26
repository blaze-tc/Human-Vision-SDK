#include "core/stats_collector.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
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

TEST(StatsV2, CountsCompleteFramesNotBodiesOrSamples) {
    humanvision::StatsCollectorV2 stats;
    EXPECT_TRUE(stats.Publish(1, 1, 1000, 6000));
    EXPECT_TRUE(stats.Publish(2, 4, 2000, 7000));
    EXPECT_TRUE(stats.Publish(3, 8, 3000, 8000));
    stats.Sample(3, 8000);
    stats.Sample(3, 9000);
    EXPECT_FALSE(stats.Publish(3, 8, 3000, 9000));
    EXPECT_EQ(stats.Snapshot().fresh_observation_frames, 3u);
    EXPECT_EQ(stats.Snapshot().output_samples, 2u);
}

TEST(StatsV2, RejectsStaleCloneAndIncompatibleCaptureClock) {
    humanvision::StatsCollectorV2 stats;
    EXPECT_FALSE(stats.Publish(1, 1, 0, 1000));
    EXPECT_FALSE(stats.Publish(1, 1, 2000, 1000));
    EXPECT_FALSE(stats.Publish(1, 1, 1000, 11001000));
    EXPECT_TRUE(stats.Publish(1, 1, 1000, 6000));
    EXPECT_FALSE(stats.Publish(2, 1, 2000, 7000, 1));
    EXPECT_FALSE(stats.Publish(0, 1, 3000, 8000));
    EXPECT_EQ(stats.Snapshot().fresh_observation_frames, 1u);
}

TEST(StatsV2, QuantilesAreBoundedAndDeterministic) {
    humanvision::StatsCollectorV2 stats;
    for (int i = 1; i <= 100; ++i)
        EXPECT_TRUE(stats.Publish(i, 1, 1000000LL * i, 1000000LL * i + i * 1000));
    auto snapshot = stats.Snapshot();
    EXPECT_FLOAT_EQ(snapshot.age_p50_ms, 50.0f);
    EXPECT_FLOAT_EQ(snapshot.age_p95_ms, 95.0f);
}

TEST(StatsV2, SeparatesPoseOnlyAndDetectorKeyframeAgeAndPerPersonTiming) {
    humanvision::StatsCollectorV2 stats;
    EXPECT_TRUE(stats.Publish(1, 4, 1000, 11000, 1, 20.f, false, 4));
    EXPECT_TRUE(stats.Publish(2, 1, 2000, 22000, 2, 8.f, true, 2));
    const auto snapshot=stats.Snapshot();
    EXPECT_EQ(snapshot.fresh_observation_frames,2u);
    EXPECT_FLOAT_EQ(snapshot.pose_age_p50_ms,10.f);
    EXPECT_FLOAT_EQ(snapshot.scheduled_detector_frame_age_p50_ms,20.f);
    EXPECT_FLOAT_EQ(snapshot.pose_per_body_p50_ms,4.f);
    EXPECT_FLOAT_EQ(snapshot.pose_per_body_p95_ms,5.f);
}

TEST(StatsV2, FreshAndSampleRatesDecayDuringStalls) {
    humanvision::StatsCollectorV2 stats;
    for(int i=1;i<=30;++i){
        const auto time=1'000'000LL+i*33'333;
        ASSERT_TRUE(stats.Publish(i,1,time-1000,time));
        stats.Sample(i,time);
    }
    EXPECT_GT(stats.Snapshot(2'000'000).fresh_observation_fps,28.f);
    EXPECT_GT(stats.Snapshot(2'000'000).output_sampling_fps,28.f);
    EXPECT_EQ(stats.Snapshot(12'000'000).fresh_observation_fps,0.f);
    EXPECT_EQ(stats.Snapshot(12'000'000).output_sampling_fps,0.f);
    stats.Sample(30,-1);
    stats.Sample(30,2'000'010);
    stats.Sample(30,2'000'000);
    EXPECT_EQ(stats.Snapshot(2'000'000).output_samples,31u);
}

TEST(StatsV2, EmptyFrameDoesNotAddPerBodyPoseTiming) {
    humanvision::StatsCollectorV2 stats;
    ASSERT_TRUE(stats.Publish(1,1,1000,6000,1,5.f));
    ASSERT_TRUE(stats.Publish(2,0,2000,7000,2,200.f));
    EXPECT_FLOAT_EQ(stats.Snapshot().pose_per_body_p95_ms,5.f);
}

TEST(StatsV2, CaptureProvenanceKeepsSensorAgeUnavailableUnlessVerified) {
    humanvision::StatsCollectorV2 stats;
    ASSERT_TRUE(stats.Publish(1, 1, 1000, 6000));
    ASSERT_TRUE(stats.Publish(2, 1, 2000, 12000));
    const auto unknown=stats.Snapshot(12000, HV_CAPTURE_PROVENANCE_UNKNOWN);
    EXPECT_TRUE(std::isnan(unknown.sensor_capture_age_p50_ms));
    EXPECT_TRUE(std::isnan(unknown.sensor_capture_age_p95_ms));
    const auto observed=stats.Snapshot(12000, HV_CAPTURE_PROVENANCE_UNITY_OBSERVED);
    EXPECT_FLOAT_EQ(observed.age_p50_ms, 5.f);
    EXPECT_FLOAT_EQ(observed.age_p95_ms, 10.f);
    EXPECT_TRUE(std::isnan(observed.sensor_capture_age_p50_ms));
    EXPECT_TRUE(std::isnan(observed.sensor_capture_age_p95_ms));
    const auto verified=stats.Snapshot(12000, HV_CAPTURE_PROVENANCE_SENSOR_VERIFIED);
    EXPECT_FLOAT_EQ(verified.sensor_capture_age_p50_ms, 5.f);
    EXPECT_FLOAT_EQ(verified.sensor_capture_age_p95_ms, 10.f);
}

}  // namespace
