#include "core/latest_frame_slot.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using humanvision::FrameBuffer;
using humanvision::FrameSubmitStatus;
using humanvision::LatestFrameSlot;

TEST(LatestFrameSlot, CopiesCallerBufferBeforeReturning) {
    LatestFrameSlot slot;
    std::vector<std::uint8_t> pixels(12, 17);
    const HV_VideoFrame frame = humanvision::test::MakeBgrFrame(pixels, 2, 2, 7);
    std::string error;
    FrameSubmitStatus status{};

    ASSERT_TRUE(slot.Submit(frame, status, error)) << error;
    std::fill(pixels.begin(), pixels.end(), 99);

    FrameBuffer taken;
    ASSERT_TRUE(slot.WaitTake(taken));
    EXPECT_EQ(taken.frame_id, 7);
    ASSERT_EQ(taken.bytes.size(), 12U);
    EXPECT_EQ(taken.bytes.front(), 17);
    EXPECT_EQ(status, FrameSubmitStatus::kAccepted);
}

TEST(LatestFrameSlot, FakeSlowWorkerConsumesNewestFrameAndCountsDrops) {
    LatestFrameSlot slot;
    std::vector<std::int64_t> processed_ids;
    std::mutex mutex;
    std::condition_variable first_taken_cv;
    bool first_taken = false;

    std::vector<std::uint8_t> pixels(12, 1);
    std::string error;
    FrameSubmitStatus status{};
    ASSERT_TRUE(slot.Submit(
        humanvision::test::MakeBgrFrame(pixels, 2, 2, 1), status, error));

    std::thread worker([&] {
        FrameBuffer frame;
        ASSERT_TRUE(slot.WaitTake(frame));
        processed_ids.push_back(frame.frame_id);
        {
            std::lock_guard<std::mutex> lock(mutex);
            first_taken = true;
        }
        first_taken_cv.notify_one();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_TRUE(slot.WaitTake(frame));
        processed_ids.push_back(frame.frame_id);
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(first_taken_cv.wait_for(
            lock, std::chrono::seconds(2), [&] { return first_taken; }));
    }

    EXPECT_TRUE(slot.Submit(
        humanvision::test::MakeBgrFrame(pixels, 2, 2, 2), status, error));
    EXPECT_EQ(status, FrameSubmitStatus::kAccepted);
    EXPECT_TRUE(slot.Submit(
        humanvision::test::MakeBgrFrame(pixels, 2, 2, 3), status, error));
    EXPECT_EQ(status, FrameSubmitStatus::kReplacedPending);
    EXPECT_TRUE(slot.Submit(
        humanvision::test::MakeBgrFrame(pixels, 2, 2, 4), status, error));
    EXPECT_EQ(status, FrameSubmitStatus::kReplacedPending);

    worker.join();
    ASSERT_EQ(processed_ids.size(), 2U);
    EXPECT_EQ(processed_ids[0], 1);
    EXPECT_EQ(processed_ids[1], 4);
    EXPECT_EQ(slot.dropped_frames(), 2);
}

}  // namespace
