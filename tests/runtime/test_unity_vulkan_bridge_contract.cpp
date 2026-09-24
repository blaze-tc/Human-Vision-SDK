#include "gpu/android/unity_vulkan_bridge.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace humanvision::gpu {
namespace {

// Exercise the real B3 transition mutex/state without a production test API.
template<class Tag, typename Tag::type Member> struct MemberAccess {
  friend typename Tag::type GetMember(Tag) { return Member; }
};
struct RingMember { using type = AhbSlotRing UnityVulkanBridge::*; friend type GetMember(RingMember); };
template struct MemberAccess<RingMember, &UnityVulkanBridge::ring_>;

struct FakeVulkan {
  std::vector<std::string> calls;
  std::vector<BridgeBarrier> barriers;
  bool access_ok = true;
  bool record_ok = true;
  bool submit_ok = true;
  bool export_ok = true;
  bool complete = true;
  int create_fail_at = -1;
  uint32_t source_format = 37;
  uint32_t source_width = 640;
  uint32_t source_height = 480;
  uint32_t source_usage = 0x5;
  SourcePreparation source_preparation = SourcePreparation::Ready;
  int next_fd = -1;
  uint32_t created = 0;
  uint32_t drained = 0;
  uint32_t released = 0;
  UnityVulkanDeviceContext last_device{};
  bool defer_queue = false;
  void (*queued_callback)(void *) noexcept = nullptr;
  void *queued_data = nullptr;
  std::mutex access_mutex;
  std::condition_variable access_cv;
  bool block_access = false;
  bool access_entered = false;

  UnityVulkanBridgeDispatch Dispatch() {
    return {this, Create, Drain, Access, PrepareSource, Release, Queue, Blit,
            Color, Submit, Export, Complete, Cancel};
  }
  static bool Create(void *p, uint32_t index,
                     const UnityVulkanDeviceContext &device,
                     const SlotContract &, const AhbSelection &selection,
                     UnityVulkanSlotCache &out) noexcept {
    auto &f = *static_cast<FakeVulkan *>(p);
    f.calls.push_back("create:" + std::to_string(index));
    ++f.created;
    if (static_cast<int>(index) == f.create_fail_at)
      return false;
    f.last_device = device;
    out.ahb = 100 + index;
    out.ahb_buffer = 130 + index;
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
    {
      std::unique_lock<std::mutex> lock(f.access_mutex);
      f.access_entered = true;
      f.access_cv.notify_all();
      f.access_cv.wait(lock, [&] { return !f.block_access; });
    }
    out = {77, BridgeImageLayout::SourceCurrent};
    out.native_layout = 1;
    out.native_stage = 0x10000;
    out.native_access = 0x20;
    out.format = f.source_format;
    out.width = f.source_width;
    out.height = f.source_height;
    out.usage = f.source_usage;
    out.samples = 1;
    out.image_type = 1;
    out.layers = 1;
    return f.access_ok;
  }
  static SourcePreparation PrepareSource(
      void *p, const UnityVulkanSlotCache &,
      const UnityTextureAccess &) noexcept {
    auto &f = *static_cast<FakeVulkan *>(p);
    f.calls.push_back("prepare-source");
    return f.source_preparation;
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
  static bool Complete(void *p, const UnityVulkanDeviceContext &,
                       const UnityVulkanSlotCache &) noexcept {
    return static_cast<FakeVulkan *>(p)->complete;
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
  if (path == HV_ANDROID_GPU_COPY_UNAVAILABLE) return {};
  AhbCandidate c;
  c.path = path;
  c.requested = {640, 480, 1, 1,
      path == HV_ANDROID_GPU_COPY_BLIT ? 256u : 768u, 0};
  c.actual = c.requested; c.actual.stride = 640;
  c.allocated = c.described = c.source_supported = true;
  c.source_transfer_src = c.source_blit_src = c.source_sampled = true;
  c.blit_conversion = true;
  const auto fill = [](AhbImageFacts& f, uint32_t usage) {
    f.properties=f.external_query=f.importable=f.compatible_handle=true;
    f.usage_compatible=f.extent_supported=f.sampled=true;
    f.image_created=f.memory_imported=f.memory_bound=f.view_created=true;
    f.vk_format=37; f.image_usage=usage;
  };
  fill(c.producer, path == HV_ANDROID_GPU_COPY_BLIT ? 6u : 20u);
  fill(c.consumer, 4);
  c.producer.transfer_dst=c.producer.blit_dst=c.producer.color_attachment=true;
  c.producer.framebuffer_created=true;
  auto selection=SelectAhbCopyPath({c});
  selection.source={640,480,37,5,0,1,1,1};
  selection.producer_identity.queried=selection.consumer_identity.queried=true;
  selection.producer_identity.device_uuid[0]=selection.consumer_identity.device_uuid[0]=0x11;
  selection.producer_identity.driver_uuid[0]=selection.consumer_identity.driver_uuid[0]=0x22;
  return selection;
}
SlotContract Contract() {
  SlotContract contract;
  contract.width = 640;
  contract.height = 480;
  contract.actual_format = 1;
  contract.actual_usage = 256;
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
                                      "access", "prepare-source", "queue-access", "blit",
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
  auto contract = Contract(); contract.actual_usage = 768;
  ASSERT_TRUE(bridge.Initialize(
      Device(), Selection(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT), contract));
  void *data = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(2), &data), BridgeResult::Ok);
  ASSERT_EQ(bridge.Render(static_cast<UnityVulkanBridge::EventRecord *>(data)),
            BridgeResult::Ok);
  EXPECT_EQ(fake.created, 3u);
  EXPECT_EQ(std::count(fake.calls.begin(), fake.calls.end(), "color"), 1);
  EXPECT_EQ(fake.released, 1u);
}

TEST(UnityVulkanBridgeContract, ConsumerLeaseBorrowsCachedAhbAndFenceUntilGpuProof) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT), Contract()));
  void* event = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &event), BridgeResult::Ok);
  ASSERT_EQ(bridge.Render(event), BridgeResult::Ok);
  ConsumerFrame frame;
  ASSERT_EQ(bridge.ClaimConsumer(frame), SlotResult::Ok);
  EXPECT_TRUE(frame.claimed);
  EXPECT_EQ(frame.ahb_buffer, 130u);
  EXPECT_EQ(frame.metadata.frame_id, 1u);
  EXPECT_TRUE(frame.producer_fd.HasPayload());
  EXPECT_EQ(bridge.RetireConsumer(frame, CompletionProof::None), SlotResult::Invalid);
  EXPECT_EQ(bridge.Prepare(Submission(2), &event), BridgeResult::Ok);
  EXPECT_EQ(bridge.RetireConsumer(frame, CompletionProof::GpuQuiescent), SlotResult::Ok);
  EXPECT_FALSE(frame.claimed);
  EXPECT_EQ(bridge.ClaimConsumer(frame), SlotResult::NoReady);
}

TEST(UnityVulkanBridgeContract, OneProducerFenceSpansDetectorAndPoseRoleHandoff) {
  struct Role {
    UnityVulkanBridge* bridge;
    int calls = 0;
    static bool Finish(void* owner, ConsumerFrame& frame, bool final_role,
                       std::string& error) noexcept {
      auto& role = *static_cast<Role*>(owner);
      ++role.calls;
      if (final_role) return role.bridge->RetireConsumer(frame, CompletionProof::GpuQuiescent) == SlotResult::Ok;
      frame.ncnn_role_complete = true;
      error.clear(); return true;
    }
  };
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT), Contract()));
  void* event = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &event), BridgeResult::Ok);
  ASSERT_EQ(bridge.Render(event), BridgeResult::Ok);
  ConsumerFrame frame;
  ASSERT_EQ(bridge.ClaimConsumer(frame), SlotResult::Ok);
  EXPECT_EQ(NextNcnnRole(frame), NcnnRoleStart::WaitForProducer);
  frame.producer_fd.Release(); // The first Vulkan role imports the one producer fd.
  EXPECT_EQ(NextNcnnRole(frame), NcnnRoleStart::Invalid);
  Role detector{&bridge};
  frame.role_owner = &detector; frame.complete_role = &Role::Finish;
  EXPECT_EQ(NextNcnnRole(frame), NcnnRoleStart::Invalid); // Handoff waits for release.
  std::string error;
  ASSERT_TRUE(CompleteGpuRole(frame, false, error)); // Detector GPU release.
  EXPECT_EQ(detector.calls, 1);
  EXPECT_FALSE(CompleteGpuRole(frame, false, error)); // One-shot callback.
  EXPECT_EQ(NextNcnnRole(frame), NcnnRoleStart::AcquireAfterPriorRole);
  EXPECT_TRUE(frame.claimed); // Pose still owns the same observation slot.
  EXPECT_EQ(bridge.RetireConsumer(frame, CompletionProof::None), SlotResult::Invalid);
  Role pose{&bridge};
  frame.role_owner = &pose; frame.complete_role = &Role::Finish;
  ASSERT_TRUE(CompleteGpuRole(frame, true, error));
  EXPECT_EQ(pose.calls, 1);
  EXPECT_FALSE(CompleteGpuRole(frame, true, error));
  EXPECT_EQ(NextNcnnRole(frame), NcnnRoleStart::Invalid);
}

TEST(UnityVulkanBridgeContract, FailedRoleCompletionQuarantinesAndClearsCallback) {
  struct FailingRole {
    UnityVulkanBridge* bridge;
    static bool Finish(void* owner, ConsumerFrame& frame, bool,
                       std::string& error) noexcept {
      auto& role = *static_cast<FailingRole*>(owner);
      role.bridge->QuarantineConsumer(frame);
      error = "GPU release failed";
      return false;
    }
  };
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT), Contract()));
  void* event = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &event), BridgeResult::Ok);
  ASSERT_EQ(bridge.Render(event), BridgeResult::Ok);
  ConsumerFrame frame;
  ASSERT_EQ(bridge.ClaimConsumer(frame), SlotResult::Ok);
  FailingRole role{&bridge};
  frame.role_owner = &role; frame.complete_role = &FailingRole::Finish;
  std::string error;
  EXPECT_FALSE(CompleteGpuRole(frame, true, error));
  EXPECT_EQ(error, "GPU release failed");
  EXPECT_FALSE(frame.claimed);
  EXPECT_EQ(frame.role_owner, nullptr);
  EXPECT_EQ(frame.complete_role, nullptr);
  EXPECT_FALSE(CompleteGpuRole(frame, true, error));
}

TEST(UnityVulkanBridgeContract, SupersededFramesExposeFenceAndRetireAfterGpuProof) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT), Contract()));
  for (int64_t id = 1; id <= 3; ++id) {
    void* event = nullptr;
    ASSERT_EQ(bridge.Prepare(Submission(id), &event), BridgeResult::Ok);
    ASSERT_EQ(bridge.Render(event), BridgeResult::Ok);
  }
  ConsumerFrame newest;
  ASSERT_EQ(bridge.ClaimConsumer(newest), SlotResult::Ok);
  EXPECT_EQ(newest.metadata.frame_id, 3u);
  for (uint64_t id = 1; id <= 2; ++id) {
    ConsumerFrame dropped;
    ASSERT_EQ(bridge.ClaimDropped(dropped), SlotResult::Ok);
    EXPECT_EQ(dropped.metadata.frame_id, id);
    EXPECT_TRUE(dropped.producer_fd.HasPayload());
    EXPECT_EQ(bridge.RetireConsumer(dropped, CompletionProof::None), SlotResult::Invalid);
    ASSERT_EQ(bridge.RetireConsumer(dropped, CompletionProof::GpuQuiescent), SlotResult::Ok);
  }
  ConsumerFrame none;
  EXPECT_EQ(bridge.ClaimDropped(none), SlotResult::NoReady);
  ASSERT_EQ(bridge.RetireConsumer(newest, CompletionProof::GpuQuiescent), SlotResult::Ok);
  void* next = nullptr;
  EXPECT_EQ(bridge.Prepare(Submission(4), &next), BridgeResult::Ok);
}

TEST(UnityVulkanBridgeContract, UnprovenGpuFaultQuarantinesCachesWithoutShutdownHang) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT), Contract()));
  void* event = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &event), BridgeResult::Ok);
  ASSERT_EQ(bridge.Render(event), BridgeResult::Ok);
  ConsumerFrame frame;
  ASSERT_EQ(bridge.ClaimConsumer(frame), SlotResult::Ok);
  EXPECT_EQ(bridge.QuarantineConsumer(frame), SlotResult::Ok);
  EXPECT_FALSE(frame.claimed);
  bridge.Shutdown();
  EXPECT_EQ(fake.drained, 0u);
  EXPECT_EQ(bridge.Prepare(Submission(2), &event), BridgeResult::Closed);
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

TEST(UnityVulkanBridgeContract,
     StalePreparedIdentityCannotConsumeReservationAfterRebuild) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  void *stale = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &stale), BridgeResult::Ok);
  bridge.Shutdown();
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  void *current = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(2), &current), BridgeResult::Ok);
  ASSERT_NE(stale, current);
  EXPECT_EQ(bridge.Render(stale), BridgeResult::Closed);
  EXPECT_EQ(bridge.Render(current), BridgeResult::Ok);
  EXPECT_EQ(std::count(fake.calls.begin(), fake.calls.end(), "access"), 1);
}

TEST(UnityVulkanBridgeContract,
     FailedGenerationConstructionRollsBackEveryCompletedCache) {
  for (int failure = 0; failure < 3; ++failure) {
    FakeVulkan fake;
    fake.create_fail_at = failure;
    UnityVulkanBridge bridge(fake.Dispatch());
    EXPECT_FALSE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                   Contract()));
    EXPECT_EQ(fake.drained, static_cast<uint32_t>(failure + 1));
  }
}

TEST(UnityVulkanBridgeContract,
     SubmitAndExportFailuresEventuallyRecoverAllThreeSlots) {
  for (const bool submit_failure : {true, false}) {
    FakeVulkan fake;
    fake.submit_ok = !submit_failure;
    fake.export_ok = submit_failure;
    UnityVulkanBridge bridge(fake.Dispatch());
    ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                  Contract()));
    for (int64_t frame = 1; frame <= 3; ++frame) {
      void *data = nullptr;
      ASSERT_EQ(bridge.Prepare(Submission(frame), &data), BridgeResult::Ok);
      EXPECT_EQ(bridge.Render(data), BridgeResult::Ok);
    }
    fake.submit_ok = true;
    fake.export_ok = true;
    void *recovered = nullptr;
    EXPECT_EQ(bridge.Prepare(Submission(4), &recovered), BridgeResult::Ok);
    ASSERT_NE(recovered, nullptr);
    EXPECT_EQ(bridge.Render(recovered), BridgeResult::Ok);
  }
}

TEST(UnityVulkanBridgeContract, RecoveryResumesAfterDropDrainTransitionWasAlreadyCommitted) {
  FakeVulkan fake;
  fake.submit_ok=false;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(),Selection(HV_ANDROID_GPU_COPY_BLIT),Contract()));
  std::array<void*,3> events{};
  for (int i=0;i<3;++i) ASSERT_EQ(bridge.Prepare(Submission(i+1),&events[i]),BridgeResult::Ok);
  auto& ring=bridge.*GetMember(RingMember{});
  for (uint32_t i=0;i<3;++i) {
    ASSERT_EQ(bridge.Render(events[i]),BridgeResult::Ok);
    SlotSnapshot snapshot;
    ASSERT_EQ(ring.Inspect(i,snapshot),SlotResult::Ok);
    SlotToken token{i,snapshot.metadata.generation,snapshot.metadata.frame_id};
    ASSERT_EQ(ring.Transition(token,AhbSlotState::EventReserved,AhbSlotState::DropDrain,
                             CompletionProof::GpuQuiescent),SlotResult::Ok);
  }
  fake.submit_ok=true;
  void* recovered=nullptr;
  EXPECT_EQ(bridge.Prepare(Submission(4),&recovered),BridgeResult::Ok);
  EXPECT_NE(recovered,nullptr);
}

TEST(UnityVulkanBridgeContract,
     ShutdownWaitsForRenderCallerBeforeDrainingSlotCaches) {
  FakeVulkan fake;
  fake.block_access = true;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  void *data = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &data), BridgeResult::Ok);
  auto render = std::async(std::launch::async,
                           [&] { return bridge.Render(data); });
  {
    std::unique_lock<std::mutex> lock(fake.access_mutex);
    ASSERT_TRUE(fake.access_cv.wait_for(
        lock, std::chrono::seconds(2), [&] { return fake.access_entered; }));
  }
  auto shutdown = std::async(std::launch::async, [&] { bridge.Shutdown(); });
  EXPECT_EQ(shutdown.wait_for(std::chrono::milliseconds(30)),
            std::future_status::timeout);
  EXPECT_EQ(fake.drained, 0u);
  {
    std::lock_guard<std::mutex> lock(fake.access_mutex);
    fake.block_access = false;
  }
  fake.access_cv.notify_all();
  EXPECT_EQ(render.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_EQ(shutdown.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_EQ(fake.drained, 3u);
}

TEST(UnityVulkanBridgeContract,
     SourceWarmEventDropsWithoutQueueThenAcceptedFrameUsesCache) {
  FakeVulkan fake;
  fake.source_preparation = SourcePreparation::Warmed;
  UnityVulkanBridge bridge(fake.Dispatch());
  auto contract = Contract();
  contract.actual_usage = 768;
  ASSERT_TRUE(bridge.Initialize(
      Device(), Selection(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT), contract));
  void *warm = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &warm), BridgeResult::Ok);
  EXPECT_EQ(bridge.Render(warm), BridgeResult::Busy);
  EXPECT_EQ(std::count(fake.calls.begin(), fake.calls.end(), "queue-access"), 0);
  fake.source_preparation = SourcePreparation::Ready;
  void *accepted = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(2), &accepted), BridgeResult::Ok);
  EXPECT_EQ(bridge.Render(accepted), BridgeResult::Ok);
  EXPECT_EQ(std::count(fake.calls.begin(), fake.calls.end(), "queue-access"), 1);
}

TEST(UnityVulkanBridgeContract, UnconfiguredValidSubmissionIsClosedNotInvalid) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  void *data = nullptr;
  EXPECT_EQ(bridge.Prepare(Submission(1), &data), BridgeResult::Closed);
  EXPECT_EQ(data, nullptr);
}

TEST(UnityVulkanBridgeContract,
     ObservedSourceContractChangesAreRejectedWithoutQueueSubmission) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  void *first = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &first), BridgeResult::Ok);
  ASSERT_EQ(bridge.Render(first), BridgeResult::Ok);
  fake.source_format = 44;
  void *changed = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(2), &changed), BridgeResult::Ok);
  EXPECT_EQ(bridge.Render(changed), BridgeResult::Closed);
  EXPECT_EQ(std::count(fake.calls.begin(), fake.calls.end(), "queue-access"), 1);
}

TEST(UnityVulkanBridgeContract,
     SrgbSourceIsRejectedBecauseGenerationColorContractIsLinearUnorm) {
  FakeVulkan fake;
  fake.source_format = 43; // VK_FORMAT_R8G8B8A8_SRGB
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(), Selection(HV_ANDROID_GPU_COPY_BLIT),
                                Contract()));
  void *data = nullptr;
  ASSERT_EQ(bridge.Prepare(Submission(1), &data), BridgeResult::Ok);
  EXPECT_EQ(bridge.Render(data), BridgeResult::Closed);
  EXPECT_EQ(std::count(fake.calls.begin(), fake.calls.end(), "queue-access"), 0);
}

TEST(UnityVulkanBridgeContract,
     SourceGeometryMayDifferFromAnalysisGeometryWhenSubmissionMatchesSource) {
  FakeVulkan fake;
  fake.source_width = 1280;
  fake.source_height = 720;
  UnityVulkanBridge bridge(fake.Dispatch());
  auto selection=Selection(HV_ANDROID_GPU_COPY_BLIT);
  selection.source.width=1280;selection.source.height=720;
  ASSERT_TRUE(bridge.Initialize(Device(), selection,
                                Contract()));
  auto submission = Submission(1);
  submission.width = 1280;
  submission.height = 720;
  void *data = nullptr;
  ASSERT_EQ(bridge.Prepare(submission, &data), BridgeResult::Ok);
  EXPECT_EQ(bridge.Render(data), BridgeResult::Ok);
}

TEST(UnityVulkanBridgeContract, RejectsSelectionFromOtherDeviceOrUnmeasuredSource) {
  FakeVulkan fake;
  UnityVulkanBridge bridge(fake.Dispatch());
  auto selection=Selection(HV_ANDROID_GPU_COPY_BLIT);
  selection.producer_identity.device_uuid[0]^=1;
  EXPECT_FALSE(bridge.Initialize(Device(),selection,Contract()));
  selection=Selection(HV_ANDROID_GPU_COPY_BLIT);
  selection.consumer_identity.driver_uuid[0]^=1;
  EXPECT_FALSE(bridge.Initialize(Device(),selection,Contract()));
  selection=Selection(HV_ANDROID_GPU_COPY_BLIT);
  selection.source={};
  EXPECT_FALSE(bridge.Initialize(Device(),selection,Contract()));
}
TEST(UnityVulkanBridgeContract, FirstSourceMustMatchTheActuallyMeasuredSourceFormat) {
  FakeVulkan fake;fake.source_format=44;
  UnityVulkanBridge bridge(fake.Dispatch());
  ASSERT_TRUE(bridge.Initialize(Device(),Selection(HV_ANDROID_GPU_COPY_BLIT),Contract()));
  void* data=nullptr;ASSERT_EQ(bridge.Prepare(Submission(1),&data),BridgeResult::Ok);
  EXPECT_EQ(bridge.Render(data),BridgeResult::Closed);
  EXPECT_EQ(std::count(fake.calls.begin(),fake.calls.end(),"queue-access"),0);
}

} // namespace
} // namespace humanvision::gpu
