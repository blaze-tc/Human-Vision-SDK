#include "gpu/android/unity_vulkan_bridge.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace humanvision::gpu {
namespace {

struct FakeVulkan {
  std::vector<std::string> calls;
  std::vector<BridgeBarrier> barriers;
  bool access_ok = true;
  bool record_ok = true;
  bool submit_ok = true;
  bool export_ok = true;
  int next_fd = -1;
  uint32_t created = 0;
  uint32_t drained = 0;
  uint32_t released = 0;
  UnityVulkanDeviceContext last_device{};
  bool defer_queue = false;
  void (*queued_callback)(void *) noexcept = nullptr;
  void *queued_data = nullptr;

  UnityVulkanBridgeDispatch Dispatch() {
    return {this, Create, Drain,  Access, Release, Queue,
            Blit, Color,  Submit, Export, Cancel};
  }
  static bool Create(void *p, uint32_t index,
                     const UnityVulkanDeviceContext &device,
                     const SlotContract &, const AhbSelection &selection,
                     UnityVulkanSlotCache &out) noexcept {
    auto &f = *static_cast<FakeVulkan *>(p);
    f.calls.push_back("create:" + std::to_string(index));
    ++f.created;
    f.last_device = device;
    out.ahb = 100 + index;
    out.image = 200 + index;
    out.memory = 300 + index;
    out.image_view = 400 + index;
    out.command_buffer = 500 + index;
    out.export_semaphore = 600 + index;
    if (selection.path == HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT) {
      out.framebuffer = 700 + index;
      out.sampler = 800 + index;
      out.descriptor_set_layout = 900 + index;
      out.descriptor_set = 1000 + index;
      out.render_pass = 1100 + index;
      out.pipeline = 1200 + index;
    }
    return true;
  }
  static void Drain(void *p, uint32_t index, AhbSlotState,
                    UnityVulkanSlotCache &slot, SyncFd &fd) noexcept {
    auto &f = *static_cast<FakeVulkan *>(p);
    f.calls.push_back("drain:" + std::to_string(index));
    ++f.drained;
    fd.Reset();
    slot = {};
  }
  static bool Access(void *p, void *, UnityTextureAccess &out) noexcept {
    auto &f = *static_cast<FakeVulkan *>(p);
    f.calls.push_back("access");
    out = {77, BridgeImageLayout::SourceCurrent};
    return f.access_ok;
  }
  static void Release(void *p, void *, const UnityTextureAccess &) noexcept {
    auto &f = *static_cast<FakeVulkan *>(p);
    f.calls.push_back("release");
    ++f.released;
  }
  static bool Queue(void *p, void (*callback)(void *) noexcept,
                    void *data) noexcept {
    static_cast<FakeVulkan *>(p)->calls.push_back("queue-access");
    auto &f = *static_cast<FakeVulkan *>(p);
    if (f.defer_queue) {
      f.queued_callback = callback;
      f.queued_data = data;
      return true;
    }
    callback(data);
    return true;
  }
  static bool Record(void *p, const char *name,
                     const UnityVulkanSlotCache &slot,
                     const BridgeBarrier *barriers, uint32_t count) noexcept {
    auto &f = *static_cast<FakeVulkan *>(p);
    f.calls.push_back(name);
    f.barriers.assign(barriers, barriers + count);
    EXPECT_NE(slot.command_buffer, 0u);
    return f.record_ok;
  }
  static bool Blit(void *p, const UnityVulkanSlotCache &s,
                   const UnityTextureAccess &, const BridgeBarrier *b,
                   uint32_t n) noexcept {
    return Record(p, "blit", s, b, n);
  }
  static bool Color(void *p, const UnityVulkanSlotCache &s,
                    const UnityTextureAccess &, const BridgeBarrier *b,
                    uint32_t n, bool shader) noexcept {
    auto &f = *static_cast<FakeVulkan *>(p);
    EXPECT_TRUE(shader);
    EXPECT_NE(s.sampler, 0u);
    EXPECT_NE(s.descriptor_set_layout, 0u);
    EXPECT_NE(s.pipeline, 0u);
    EXPECT_NE(s.render_pass, 0u);
    EXPECT_NE(s.image_view, 0u);
    EXPECT_NE(s.framebuffer, 0u);
    return Record(p, "color", s, b, n);
  }
  static bool Submit(void *p, const UnityVulkanDeviceContext &,
                     const UnityVulkanSlotCache &) noexcept {
    auto &f = *static_cast<FakeVulkan *>(p);
    f.calls.push_back("signal");
    return f.submit_ok;
  }
  static bool Export(void *p, const UnityVulkanDeviceContext &,
                     const UnityVulkanSlotCache &, SyncFd &fd) noexcept {
    auto &f = *static_cast<FakeVulkan *>(p);
    f.calls.push_back("export");
    if (!f.export_ok)
      return false;
    fd = SyncFd(f.next_fd);
    return true;
  }
  static void Cancel(void *p) noexcept {
    auto &f = *static_cast<FakeVulkan *>(p);
    f.calls.push_back("cancel-events");
    if (f.queued_callback) {
      f.queued_callback(f.queued_data);
      f.queued_callback = nullptr;
    }
  }
};

AhbSelection Selection(HV_AndroidGpuCopyPath path) {
  AhbSelection selection;
  selection.path = path;
  selection.contract = {640, 480, 1, 1, 0x1234, 640};
  selection.candidates.push_back({path});
  return selection;
}
SlotContract Contract() {
  SlotContract contract;
  contract.width = 640;
  contract.height = 480;
  contract.actual_format = 1;
  contract.actual_usage = 0x1234;
  contract.camera_session = 9;
  return contract;
}
UnityVulkanDeviceContext Device() {
  UnityVulkanDeviceContext device;
  device.instance = 1;
  device.physical_device = 2;
  device.device = 3;
  device.graphics_queue = 4;
  device.graphics_queue_family = 5;
  device.device_uuid[0] = 0x11;
  device.driver_uuid[0] = 0x22;
  return device;
}
HV_AndroidGpuSubmissionV1 Submission(int64_t frame) {
  return {sizeof(HV_AndroidGpuSubmissionV1),
          HV_ANDROID_GPU_API_V1,
          reinterpret_cast<void *>(0x99),
          640,
          480,
          frame,
          frame * 1000,
          0,
          0};
}

TEST(UnityVulkanBridgeContract, BlitUsesMeasuredPathAndExactOwnershipBarriers) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  EXPECT_EQ(fake.created, 3u);
  void *data = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &data), BridgeResult::Ok);
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(bridge.Render(static_cast<UnityVulkanBridge::EventRecord *>(data)),
            BridgeResult::Ok);
  EXPECT_EQ(fake.calls,
            (std::vector<std::string>{"create:0", "create:1", "create:2",
                                      "access", "queue-access", "blit",
                                      "signal", "export", "release"}));
  ASSERT_EQ(fake.barriers.size(), 4u);
  EXPECT_EQ(fake.barriers[0].old_layout, BridgeImageLayout::SourceCurrent);
  EXPECT_EQ(fake.barriers[0].new_layout, BridgeImageLayout::TransferSource);
  EXPECT_EQ(fake.barriers[1].old_layout, BridgeImageLayout::External);
  EXPECT_EQ(fake.barriers[1].new_layout,
            BridgeImageLayout::TransferDestination);
  EXPECT_EQ(fake.barriers[1].source_queue_family, UINT32_MAX - 1u);
  EXPECT_EQ(fake.barriers[1].destination_queue_family, 5u);
  EXPECT_EQ(fake.barriers[2].old_layout,
            BridgeImageLayout::TransferDestination);
  EXPECT_EQ(fake.barriers[2].new_layout, BridgeImageLayout::External);
  EXPECT_EQ(fake.barriers[2].source_queue_family, 5u);
  EXPECT_EQ(fake.barriers[2].destination_queue_family, UINT32_MAX - 1u);
  EXPECT_EQ(fake.barriers[3].old_layout, BridgeImageLayout::TransferSource);
  EXPECT_EQ(fake.barriers[3].new_layout, BridgeImageLayout::SourceCurrent);
  HV_AndroidGpuBridgeStatusV1 status{sizeof(status), HV_ANDROID_GPU_API_V1};
  bridge.GetStatus(status);
  EXPECT_EQ(status.copy_path, HV_ANDROID_GPU_COPY_BLIT);
  EXPECT_EQ(status.submitted_frames, 1u);
  EXPECT_EQ(status.imported_frames, 1u);
}

TEST(UnityVulkanBridgeContract, ColorPathReusesGenerationCachedGpuObjects) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(
      Device(), Selection(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT), Contract()));
  void *data = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(2), &data), BridgeResult::Ok);
  ASSERT_EQ(bridge.Render(static_cast<UnityVulkanBridge::EventRecord *>(data)),
            BridgeResult::Ok);
  EXPECT_EQ(fake.created, 3u);
  EXPECT_EQ(std::count(fake.calls.begin(), fake.calls.end(), "color"), 1);
  EXPECT_EQ(fake.released, 1u);
}

TEST(UnityVulkanBridgeContract,
     ReleasesTextureOnEveryPostAccessFailureWithoutWaiting) {
  FakeVulkan fake;
  fake.record_ok = false;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  void *data = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(3), &data), BridgeResult::Ok);
  EXPECT_EQ(bridge.Render(static_cast<UnityVulkanBridge::EventRecord *>(data)),
            BridgeResult::Ok);
  EXPECT_EQ(fake.released, 1u);
  EXPECT_EQ(std::count(fake.calls.begin(), fake.calls.end(), "signal"), 0);
}

TEST(UnityVulkanBridgeContract, ReleasesTextureWhenSubmitOrFenceExportFails) {
  for (const bool fail_submit : {true, false}) {
    FakeVulkan fake;
    fake.submit_ok = !fail_submit;
    fake.export_ok = fail_submit;
    UnityVulkanBridge bridge(fake.Dispatch());
    ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                  Contract()));
    void *data = nullptr;
    ASSERT_EQ(bridge.Prepare(Submission(30 + fail_submit), &data),
              BridgeResult::Ok);
    EXPECT_EQ(
        bridge.Render(static_cast<UnityVulkanBridge::EventRecord *>(data)),
        BridgeResult::Ok);
    EXPECT_EQ(fake.released, 1u);
  }
}

TEST(UnityVulkanBridgeContract,
     DeviceReinitializeIsAGenerationBoundaryAndCapturesExactQueue) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  const auto first = Device();
  ASSERT_TRUE(bridge.Initialize(first, Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  const auto first_generation = bridge.Generation();
  auto second = first;
  second.device = 0x1234;
  second.graphics_queue = 0x5678;
  second.graphics_queue_family = 9;
  ASSERT_TRUE(bridge.Initialize(second, Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  EXPECT_GT(bridge.Generation(), first_generation);
  EXPECT_EQ(fake.drained, 3u);
  EXPECT_EQ(fake.last_device.device, second.device);
  EXPECT_EQ(fake.last_device.graphics_queue, second.graphics_queue);
  EXPECT_EQ(fake.last_device.graphics_queue_family,
            second.graphics_queue_family);
}

TEST(UnityVulkanBridgeContract, FourthReservationDropsBeforeIssuingEvent) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  std::array<void *, 4> data{};
  EXPECT_EQ(bridge.Prepare(Submission(1), &data[0]), BridgeResult::Ok);
  EXPECT_EQ(bridge.Prepare(Submission(2), &data[1]), BridgeResult::Ok);
  EXPECT_EQ(bridge.Prepare(Submission(3), &data[2]), BridgeResult::Ok);
  EXPECT_EQ(bridge.Prepare(Submission(4), &data[3]),
            BridgeResult::DroppedNoSlot);
  EXPECT_EQ(data[3], nullptr);
  HV_AndroidGpuBridgeStatusV1 status{sizeof(status), HV_ANDROID_GPU_API_V1};
  bridge.GetStatus(status);
  EXPECT_EQ(status.dropped_no_slot, 1u);
}

TEST(UnityVulkanBridgeContract, ShutdownCancelsEventsBeforeDrainingGeneration) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  void *data = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &data), BridgeResult::Ok);
  bridge.Shutdown();
  ASSERT_GE(fake.calls.size(), 7u);
  EXPECT_EQ(fake.calls[3], "cancel-events");
  EXPECT_EQ(fake.drained, 3u);
  EXPECT_EQ(bridge.Render(static_cast<UnityVulkanBridge::EventRecord *>(data)),
            BridgeResult::Closed);
}

TEST(UnityVulkanBridgeContract,
     ShutdownDrainsQueuedQueueAccessBeforeDestroyingSlots) {
  FakeVulkan fake;
  fake.defer_queue = true;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  void *data = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &data), BridgeResult::Ok);
  ASSERT_EQ(bridge.Render(static_cast<UnityVulkanBridge::EventRecord *>(data)),
            BridgeResult::Ok);
  EXPECT_EQ(fake.released, 0u);
  bridge.Shutdown();
  EXPECT_EQ(fake.released, 1u);
  const auto cancel =
      std::find(fake.calls.begin(), fake.calls.end(), "cancel-events");
  const auto drain = std::find(fake.calls.begin(), fake.calls.end(), "drain:0");
  ASSERT_NE(cancel, fake.calls.end());
  ASSERT_NE(drain, fake.calls.end());
  EXPECT_LT(cancel, drain);
}

TEST(UnityVulkanBridgeContract, RejectsUnmeasuredAndUnavailableSelections) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  auto selection = Selection(HV_ANDROID_GPU_COPY_UNAVAILABLE);
  EXPECT_FALSE(bridge.Initialize(Device(), selection, Contract()));
  selection.path = HV_ANDROID_GPU_COPY_BLIT;
  selection.contract.usage ^= 1;
  EXPECT_FALSE(bridge.Initialize(Device(), selection, Contract()));
  EXPECT_EQ(fake.created, 0u);
}

} // namespace
} // namespace humanvision::gpu
