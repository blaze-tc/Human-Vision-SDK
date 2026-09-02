#include "core/result_snapshot_store.h"

#include <gtest/gtest.h>

#include <array>

namespace {

humanvision::ResultSnapshot MakeSnapshot(int count) {
    humanvision::ResultSnapshot snapshot;
    snapshot.meta.struct_size = sizeof(HV_ResultMeta);
    snapshot.meta.result_sequence = 5;
    snapshot.meta.source_frame_id = 42;
    snapshot.meta.source_timestamp_us = 123456;
    snapshot.meta.body_count = count;
    snapshot.bodies.resize(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        snapshot.bodies[static_cast<std::size_t>(index)].struct_size = sizeof(HV_Body);
        snapshot.bodies[static_cast<std::size_t>(index)].track_id = 100 + index;
    }
    return snapshot;
}

TEST(ResultSnapshotStore, ReturnsOnlyCompletedSnapshot) {
    humanvision::ResultSnapshotStore store;
    HV_ResultMeta meta{};
    meta.struct_size = sizeof(HV_ResultMeta);
    EXPECT_EQ(store.GetMeta(meta), HV_NO_NEW_RESULT);

    store.Publish(MakeSnapshot(2));

    EXPECT_EQ(store.GetMeta(meta), HV_OK);
    EXPECT_EQ(meta.source_frame_id, 42);
    EXPECT_EQ(meta.body_count, 2);
    EXPECT_EQ(store.BodyCount(), 2);
}

TEST(ResultSnapshotStore, ReportsRequiredCapacityWithoutPartialCopy) {
    humanvision::ResultSnapshotStore store;
    store.Publish(MakeSnapshot(2));
    std::array<HV_Body, 2> bodies{};
    bodies[0].track_id = -77;
    int written = -1;

    EXPECT_EQ(store.CopyBodies(bodies.data(), 1, &written), HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(written, 2);
    EXPECT_EQ(bodies[0].track_id, -77);

    EXPECT_EQ(store.CopyBodies(bodies.data(), 2, &written), HV_OK);
    EXPECT_EQ(written, 2);
    EXPECT_EQ(bodies[0].track_id, 100);
    EXPECT_EQ(bodies[1].track_id, 101);
}

}  // namespace
