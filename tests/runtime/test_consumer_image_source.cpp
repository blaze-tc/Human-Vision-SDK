#include "gpu/android/consumer_image_source.h"
#include <gtest/gtest.h>

using namespace humanvision::gpu;
namespace {
class Source final : public ConsumerImageSource {
public:
 DeviceIdentity Identity() const override { DeviceIdentity value; value.queried=true; value.device_uuid.fill(1); value.driver_uuid.fill(2); return value; }
 SlotResult RetainGeneration(ConsumerGeneration&,void (*)(uintptr_t) noexcept) noexcept override { return SlotResult::Ok; }
 SlotResult ClaimDropped(ConsumerFrame&) noexcept override { return SlotResult::NoReady; }
 SlotResult RetireConsumer(ConsumerFrame& frame,CompletionProof proof) noexcept override {
  if(proof!=CompletionProof::GpuQuiescent)return SlotResult::Invalid;
  frame.claimed=false;++retired;return SlotResult::Ok;
 }
 SlotResult QuarantineConsumer(ConsumerFrame& frame) noexcept override { frame.claimed=false;++quarantined;return SlotResult::Closed; }
 int retired=0,quarantined=0;
};
bool Ready(void*,SyncFd& fd) noexcept { return fd.State()==SyncPayloadState::AlreadySignaled; }
bool Pending(void*,SyncFd&) noexcept { return false; }
}
TEST(ConsumerImageSource, NativeLeaseDoesNotRequireUnityBridge) {
 Source source; ConsumerFrame frame; frame.producer=&source;frame.claimed=true;frame.producer_fd=SyncFd(-1);
 EXPECT_EQ(RetireUnsubmittedConsumer(source,frame,Ready,nullptr),SlotResult::Ok);
 EXPECT_FALSE(frame.claimed);EXPECT_EQ(source.retired,1);EXPECT_EQ(source.quarantined,0);
 EXPECT_TRUE(source.Identity().queried);
}
TEST(ConsumerImageSource, MissingOrUnprovedCompletionNeverRecycles) {
 Source source;ConsumerFrame frame;frame.claimed=true;
 EXPECT_EQ(RetireUnsubmittedConsumer(source,frame,Ready,nullptr),SlotResult::Closed);
 EXPECT_EQ(source.retired,0);EXPECT_EQ(source.quarantined,1);
 frame.claimed=true;frame.producer_fd=SyncFd(-1);
 EXPECT_EQ(RetireUnsubmittedConsumer(source,frame,Pending,nullptr),SlotResult::Closed);
 EXPECT_EQ(source.retired,0);EXPECT_EQ(source.quarantined,2);
}
TEST(ConsumerImageSource, CompletedPriorRoleHasExplicitProof) {
 Source source;ConsumerFrame frame;frame.claimed=true;frame.ncnn_role_complete=true;
 EXPECT_EQ(RetireUnsubmittedConsumer(source,frame,Ready,nullptr),SlotResult::Ok);
 EXPECT_EQ(source.retired,1);
}
