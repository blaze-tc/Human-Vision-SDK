#include "plugins/backend/ncnn/ncnn_prepared_input.h"
#include "gpu/android/unity_vulkan_bridge.h"
#include <gtest/gtest.h>
#include <array>
#include <string>
#include <vector>

using humanvision::runtime::ncnn_backend::PreparedInputState;

namespace {
HV_GpuFrameRefV1 Frame(uint64_t generation, int64_t id) {
    HV_GpuFrameRefV1 frame{};
    frame.struct_size = sizeof(frame);
    frame.api_version = HV_GPU_FRAME_API_V1;
    frame.generation = generation;
    frame.frame_id = id;
    frame.timestamp_us = id * 1000;
    return frame;
}
struct FakeGpuBridge {
    std::vector<std::string> events;
    bool source_alive = true;
    bool copy_proved = false;
    std::array<uint16_t, 3> detector_tensor{};
    size_t copied_bytes = 0;
    static bool Complete(void* owner, humanvision::gpu::ConsumerFrame& frame,
                         bool final_role, std::string&) noexcept {
        auto& self = *static_cast<FakeGpuBridge*>(owner);
        if (!self.copy_proved) return false;
        self.events.emplace_back(final_role ? "pose_final" : "detector_handoff");
        frame.ncnn_role_complete = true;
        if (final_role) { self.source_alive = false; frame.claimed = false; }
        return true;
    }
    void RecordPreparedCopy(uint32_t width, uint32_t height, uint32_t channels) {
        ASSERT_TRUE(source_alive);
        ASSERT_EQ(width, 320u);
        ASSERT_EQ(height, 320u);
        ASSERT_EQ(channels, 3u);
        copied_bytes = static_cast<size_t>(width) * height * channels * sizeof(uint16_t);
        detector_tensor = {1, 2, 3};
        events.emplace_back("gpu_copy");
    }
    void ProveComplete() { copy_proved = true; events.emplace_back("gpu_proof"); }
    bool FakeExtractorInput() {
        events.emplace_back("extractor_input");
        return !source_alive && detector_tensor == std::array<uint16_t, 3>{1, 2, 3};
    }
};
}

TEST(NcnnPreparedInput, GPUProofPrecedesAhbHandoffAndDetachedRun) {
    PreparedInputState state;
    ASSERT_TRUE(state.Initialize(8));
    const auto source = Frame(8, 88);
    HV_GpuPreparedRefV1 ref{};
    ASSERT_TRUE(state.Begin(source, ref));
    FakeGpuBridge bridge;
    humanvision::gpu::ConsumerFrame lease;
    lease.claimed = true;
    lease.role_owner = &bridge;
    lease.complete_role = &FakeGpuBridge::Complete;
    bridge.RecordPreparedCopy(320, 320, 3);
    EXPECT_EQ(bridge.copied_bytes, 320u * 320u * 3u * sizeof(uint16_t));
    EXPECT_EQ(ref.frame_id, 88);
    EXPECT_EQ(ref.generation, 8u);
    EXPECT_FALSE(state.ReleaseSourceRole());
    EXPECT_FALSE(state.Consume(ref));
    std::string error;
    EXPECT_FALSE(humanvision::gpu::CompleteGpuRole(lease, false, error));
    EXPECT_TRUE(bridge.events == std::vector<std::string>{"gpu_copy"});
    lease.role_owner = &bridge;
    lease.complete_role = &FakeGpuBridge::Complete;
    bridge.ProveComplete();
    ASSERT_TRUE(state.ProveGpuCopy());
    ASSERT_TRUE(humanvision::gpu::CompleteGpuRole(lease, false, error));
    ASSERT_TRUE(state.ReleaseSourceRole());
    ASSERT_TRUE(bridge.source_alive);
    lease.role_owner = &bridge; // A separate current-frame pose role.
    lease.complete_role = &FakeGpuBridge::Complete;
    ASSERT_TRUE(humanvision::gpu::CompleteGpuRole(lease, true, error));
    ASSERT_FALSE(bridge.source_alive);
    EXPECT_TRUE(state.Consume(ref));
    EXPECT_TRUE(bridge.FakeExtractorInput());
    EXPECT_EQ(bridge.events, (std::vector<std::string>{"gpu_copy", "gpu_proof",
        "detector_handoff", "pose_final", "extractor_input"}));
    EXPECT_FALSE(state.Consume(ref));
    EXPECT_TRUE(state.Idle());
}

TEST(NcnnPreparedInput, OneCachedJobRejectsBusyAndStaleTokens) {
    PreparedInputState state;
    ASSERT_TRUE(state.Initialize(9));
    HV_GpuPreparedRefV1 first{}, blocked{};
    ASSERT_TRUE(state.Begin(Frame(9, 1), first));
    EXPECT_FALSE(state.Begin(Frame(9, 2), blocked));
    EXPECT_EQ(blocked.token, 0u);
    ASSERT_TRUE(state.ProveGpuCopy());
    ASSERT_TRUE(state.ReleaseSourceRole());
    auto stale = first;
    stale.generation++;
    EXPECT_FALSE(state.Discard(stale));
    EXPECT_TRUE(state.Discard(first));
    EXPECT_FALSE(state.Discard(first));
    HV_GpuPreparedRefV1 second{};
    ASSERT_TRUE(state.Begin(Frame(9, 2), second));
    EXPECT_GT(second.token, first.token);
}

TEST(NcnnPreparedInput, RotationOrRestartRejectsOldGeneration) {
    PreparedInputState state;
    ASSERT_TRUE(state.Initialize(10));
    HV_GpuPreparedRefV1 old{};
    ASSERT_TRUE(state.Begin(Frame(10, 3), old));
    ASSERT_TRUE(state.ProveGpuCopy());
    ASSERT_TRUE(state.ReleaseSourceRole());
    EXPECT_FALSE(state.Initialize(11)); // Busy job must be drained first.
    ASSERT_TRUE(state.Discard(old));
    ASSERT_TRUE(state.Initialize(11));
    EXPECT_FALSE(state.Consume(old));
    HV_GpuPreparedRefV1 next{};
    ASSERT_TRUE(state.Begin(Frame(11, 4), next));
    EXPECT_EQ(next.generation, 11u);
    EXPECT_GT(next.token, old.token);
}

TEST(NcnnPreparedInput, UnprovenCompletionQuarantinesResources) {
    PreparedInputState state;
    ASSERT_TRUE(state.Initialize(4));
    HV_GpuPreparedRefV1 ref{};
    ASSERT_TRUE(state.Begin(Frame(4, 7), ref));
    state.Quarantine();
    EXPECT_TRUE(state.RequiresProcessLifetimeRetention(true));
    EXPECT_TRUE(state.Quarantined());
    EXPECT_FALSE(state.ReleaseSourceRole());
    EXPECT_FALSE(state.Discard(ref));
    EXPECT_FALSE(state.Initialize(5));
    HV_GpuPreparedRefV1 blocked{};
    EXPECT_FALSE(state.Begin(Frame(5, 8), blocked));
}

TEST(NcnnPreparedInput, DestructionPolicyOnlyReleasesProvedResources) {
    PreparedInputState state;
    ASSERT_TRUE(state.Initialize(6));
    HV_GpuPreparedRefV1 ref{};
    ASSERT_TRUE(state.Begin(Frame(6, 9), ref));
    // Destruction while a GPU copy might still reference the tensor is unsafe
    // even before the backend has marked its terminal fault.
    EXPECT_TRUE(state.RequiresProcessLifetimeRetention(false));
    ASSERT_TRUE(state.ProveGpuCopy());
    ASSERT_TRUE(state.ReleaseSourceRole());
    EXPECT_FALSE(state.RequiresProcessLifetimeRetention(false));
    EXPECT_TRUE(state.RequiresProcessLifetimeRetention(true));
}
