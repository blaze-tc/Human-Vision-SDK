#include "composition/preview_mailbox.h"
#include "composition/source_binding.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <chrono>
#include <thread>
#include <filesystem>
#include <limits>
#include <new>
using namespace humanvision::runtime;
namespace {
HV_RuntimePreviewV1 Info(uint64_t frame,uint64_t generation=2) {
 return {sizeof(HV_RuntimePreviewV1),1,2,2,8,16,41,generation,frame,3,1000};
}
void Publish(PreviewMailbox& mailbox,uint64_t frame) {
 PreviewMailbox::CopyTicket ticket;
 ASSERT_TRUE(mailbox.TryBeginCopy(3,true,false,ticket));
 std::memset(ticket.data,int(frame),16);
 ASSERT_TRUE(mailbox.FinishCopy(ticket,Info(frame),true,100));
}
}
TEST(PreviewMailbox, ReservesTwoSourceCreditsAndOnlyOneActualCopy) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));PreviewMailbox::CopyTicket ticket,second;
 EXPECT_FALSE(mailbox.TryBeginCopy(2,true,false,ticket));
 ASSERT_TRUE(mailbox.TryBeginCopy(3,true,false,ticket));
 EXPECT_FALSE(mailbox.TryBeginCopy(3,true,false,second));
 EXPECT_FALSE(mailbox.FinishCopy(ticket,Info(1),false,10));
 EXPECT_TRUE(mailbox.HasInFlight());
 EXPECT_TRUE(mailbox.FinishCopy(ticket,Info(1),true,10));
 EXPECT_FALSE(mailbox.HasInFlight());
}
TEST(PreviewMailbox, AnalysisPendingAndOverBudgetSkipBeforeTakingLease) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));PreviewMailbox::CopyTicket ticket;
 EXPECT_FALSE(mailbox.TryBeginCopy(3,true,true,ticket));
 ASSERT_TRUE(mailbox.TryBeginCopy(0,false,false,ticket)); // Already-owned pixels take no decoder credit.
 std::memset(ticket.data,1,16);ASSERT_TRUE(mailbox.FinishCopy(ticket,Info(1),true,20001));
 EXPECT_TRUE(mailbox.BudgetExceeded());
 EXPECT_FALSE(mailbox.TryBeginCopy(3,true,false,ticket));
}
TEST(PreviewMailbox, NoConsumerAcrossTwoSecondsKeepsNewestWithThreeBuffers) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));
 std::atomic<bool> running{true},failed{false};std::atomic<uint64_t> produced{0};
 std::thread producer([&] {
  uint64_t sequence=0;while(running) {
   PreviewMailbox::CopyTicket ticket;if(!mailbox.TryBeginCopy(3,true,false,ticket)){failed=true;break;}
   ++sequence;std::memset(ticket.data,int(sequence),16);
   if(!mailbox.FinishCopy(ticket,Info(sequence),true,100)){failed=true;break;}
   produced=sequence;std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
 });
 std::this_thread::sleep_for(std::chrono::milliseconds(2000));running=false;producer.join();
 ASSERT_FALSE(failed);ASSERT_GT(produced.load(),1u);
 std::array<uint8_t,16> bytes{};auto info=Info(0);
 ASSERT_EQ(mailbox.CopyLatest(0,bytes.data(),bytes.size(),&info),HV_CONTROL_OK);
 EXPECT_EQ(info.frame_id,produced.load());for(auto pixel:bytes)EXPECT_EQ(pixel,uint8_t(produced.load()));
 EXPECT_EQ(mailbox.CopyLatest(produced.load(),bytes.data(),bytes.size(),&info),HV_CONTROL_NO_DATA);
 EXPECT_EQ(mailbox.BufferCount(),3u);
}
TEST(PreviewMailbox, ThreePinnedReadersDropCopyWithoutWaiting) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));
 Publish(mailbox,1);auto first=mailbox.AcquireLatest(0);
 Publish(mailbox,2);auto second=mailbox.AcquireLatest(1);
 Publish(mailbox,3);auto third=mailbox.AcquireLatest(2);
 PreviewMailbox::CopyTicket ticket;EXPECT_FALSE(mailbox.TryBeginCopy(3,true,false,ticket));
 first.Reset();EXPECT_TRUE(mailbox.TryBeginCopy(3,true,false,ticket));
 EXPECT_TRUE(mailbox.FinishCopy(ticket,Info(4),true,10));
}
TEST(PreviewMailbox, CancellationDoesNotRecycleUnprovedCopy) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));PreviewMailbox::CopyTicket ticket,second;
 ASSERT_TRUE(mailbox.TryBeginCopy(3,true,false,ticket));mailbox.Invalidate();
 EXPECT_TRUE(mailbox.HasInFlight());EXPECT_FALSE(mailbox.TryBeginCopy(3,true,false,second));
 EXPECT_FALSE(mailbox.FinishCopy(ticket,Info(1),false,0));EXPECT_TRUE(mailbox.HasInFlight());
 EXPECT_FALSE(mailbox.FinishCopy(ticket,Info(1),true,0));EXPECT_FALSE(mailbox.HasInFlight());
 std::array<uint8_t,16> bytes{};auto info=Info(0);
 EXPECT_EQ(mailbox.CopyLatest(0,bytes.data(),bytes.size(),&info),HV_CONTROL_NO_DATA);
}
TEST(PreviewMailbox, ExactSnapshotHasOneRequestAndRequiresExactAcceptedIdentity) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));uint64_t request=0,other=0;
 ASSERT_EQ(mailbox.RequestSnapshot(&request),HV_CONTROL_OK);
 EXPECT_EQ(mailbox.RequestSnapshot(&other),HV_CONTROL_BUSY);
 std::array<uint8_t,16> bytes{};bytes.fill(9);auto info=Info(12);
 ASSERT_TRUE(mailbox.StoreSnapshotCandidate(request,info,bytes.data(),bytes.size()));
 EXPECT_FALSE(mailbox.ConfirmSnapshot(request,41,2,13,3));
 auto output=Info(0);EXPECT_EQ(mailbox.CopySnapshot(request,nullptr,0,&output),HV_CONTROL_NO_DATA);
 ASSERT_TRUE(mailbox.ConfirmSnapshot(request,41,2,12,3));
 EXPECT_EQ(mailbox.CopySnapshot(request,nullptr,0,&output),HV_CONTROL_BUFFER_TOO_SMALL);
 EXPECT_EQ(output.frame_id,12u);EXPECT_EQ(output.source_id,41u);
 bytes.fill(0);ASSERT_EQ(mailbox.CopySnapshot(request,bytes.data(),bytes.size(),&output),HV_CONTROL_OK);
 for(auto pixel:bytes)EXPECT_EQ(pixel,9);
 EXPECT_EQ(mailbox.CopySnapshot(request,bytes.data(),bytes.size(),&output),HV_CONTROL_NO_DATA);
}
TEST(PreviewMailbox, InvalidationCancelsExactRequestAndNeverSubstitutesLatestFrame) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));uint64_t request=0;
 ASSERT_EQ(mailbox.RequestSnapshot(&request),0);std::array<uint8_t,16> bytes{};
 ASSERT_TRUE(mailbox.StoreSnapshotCandidate(request,Info(1),bytes.data(),bytes.size()));
 mailbox.Invalidate();Publish(mailbox,2);auto info=Info(0);
 EXPECT_EQ(mailbox.CopySnapshot(request,bytes.data(),bytes.size(),&info),HV_CONTROL_NO_DATA);
 uint64_t next=0;ASSERT_EQ(mailbox.RequestSnapshot(&next),0);EXPECT_NE(next,request);
}
TEST(PreviewMailbox, ExactDeadlineReportsTimeoutWithoutHoldingAdmission) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));uint64_t first=0,next=0;
 ASSERT_EQ(mailbox.RequestSnapshot(&first),0);mailbox.FailSnapshot(first,HV_CONTROL_TIMEOUT);
 EXPECT_EQ(mailbox.PendingSnapshot(),0u);auto info=Info(0);
 EXPECT_EQ(mailbox.CopySnapshot(first,nullptr,0,&info),HV_CONTROL_TIMEOUT);
 ASSERT_EQ(mailbox.RequestSnapshot(&next),0);EXPECT_NE(first,next);
}
TEST(PreviewMailbox, ControlCopiesAreUnavailableWithoutInventingPreviewData) {
 HV_RuntimeControlApiV1 api{sizeof(api),1};ASSERT_EQ(HV_QueryRuntimeControlApiV1(1,&api),0);
 HV_RuntimeControllerConfigV1 config{sizeof(config),1,{sizeof(HV_RuntimeConfigV1),HV_API_VERSION_040,"Z:/no-model-loading","cpu",1,0},0,1000};
 HV_RuntimeControllerHandle handle=nullptr;char error[128]{};ASSERT_EQ(api.CreateDeferred(&config,&handle,error,sizeof(error)),0);
 auto info=Info(0);EXPECT_EQ(api.CopyPreview(handle,0,nullptr,0,&info),HV_CONTROL_NO_DATA);
 EXPECT_EQ(api.CopySnapshot(handle,1,nullptr,0,&info),HV_CONTROL_NO_DATA);
 uint64_t request=0;ASSERT_EQ(api.RequestClose(handle,&request),0);
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 int result=HV_CONTROL_BUSY;while((result=api.TryRelease(handle))==HV_CONTROL_BUSY&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
 EXPECT_EQ(result,0);
}
TEST(PreviewMailbox, PublicSourceFlagIsOptInAndUnknownFlagsAreRejected) {
 HV_RuntimeSourceV1 source{sizeof(source),1};source.kind=HV_NATIVE_SOURCE_RTSP;source.width=2;source.height=2;
 source.transport_tcp=1;source.url_utf8="rtsp://127.0.0.1/example";source.flags=HV_NATIVE_SOURCE_LIVE_PREVIEW;
 OwnedSourceConfig config;std::string error;EXPECT_TRUE(CopySourceConfig(source,config,error))<<error;
 EXPECT_EQ(config.flags,HV_NATIVE_SOURCE_LIVE_PREVIEW);source.flags=2;EXPECT_FALSE(CopySourceConfig(source,config,error));
 source.flags=0;EXPECT_TRUE(CopySourceConfig(source,config,error));
}
namespace {
struct AllocationFailure {
 int calls=0,fail_at=std::numeric_limits<int>::max();
 static void Allocate(void* context,std::vector<uint8_t>& buffer,uint32_t bytes) {
  auto& self=*static_cast<AllocationFailure*>(context);
  if(++self.calls==self.fail_at)throw std::bad_alloc();
  buffer.resize(bytes);
 }
 PreviewMailbox::AllocationPolicy Policy(){return {this,Allocate};}
};
struct ControlledClock {
 int64_t now=100;
 static int64_t Now(void* context) noexcept {return static_cast<ControlledClock*>(context)->now;}
 PreviewMailbox::ClockSource Clock(){return {this,Now};}
};
}
TEST(PreviewMailbox, SourceAdmissionInvalidatesConfirmedCaptureWithLivePreviewOff) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));ASSERT_TRUE(mailbox.AdmitSourceGeneration(41,2,3));
 uint64_t request=0;ASSERT_EQ(mailbox.RequestSnapshot(&request),0);std::array<uint8_t,16> bytes{};
 ASSERT_TRUE(mailbox.StoreSnapshotCandidate(request,Info(1),bytes.data(),bytes.size()));
 ASSERT_TRUE(mailbox.ConfirmSnapshot(request,41,2,1,3));
 ASSERT_TRUE(mailbox.AdmitSourceGeneration(41,3,3));auto info=Info(0);
 EXPECT_EQ(mailbox.CopySnapshot(request,bytes.data(),bytes.size(),&info),HV_CONTROL_NO_DATA);
 EXPECT_FALSE(mailbox.StoreSnapshotCandidate(request,Info(1),bytes.data(),bytes.size()));
 EXPECT_FALSE(mailbox.AdmitSourceGeneration(41,2,3));
}
TEST(PreviewMailbox, SourceAdmissionInvalidatesSuspendedLiveImageAndLateOldCompletion) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));ASSERT_TRUE(mailbox.AdmitSourceGeneration(41,2,3));
 Publish(mailbox,1);mailbox.SuspendLive();ASSERT_TRUE(mailbox.AdmitSourceGeneration(41,3,3));
 auto info=Info(0);std::array<uint8_t,16> bytes{};
 EXPECT_EQ(mailbox.CopyLatest(0,bytes.data(),bytes.size(),&info),HV_CONTROL_NO_DATA);
 PreviewMailbox::CopyTicket ticket;ASSERT_TRUE(mailbox.TryBeginCopy(0,false,false,ticket,true));
 EXPECT_FALSE(mailbox.FinishCopy(ticket,Info(2),true,0));
 EXPECT_EQ(mailbox.CopyLatest(0,bytes.data(),bytes.size(),&info),HV_CONTROL_NO_DATA);
}
TEST(PreviewMailbox, PartialGrowthAllocationFailurePreservesEveryPreviousBuffer) {
 AllocationFailure allocation;PreviewMailbox mailbox(allocation.Policy());ASSERT_TRUE(mailbox.Configure(16));Publish(mailbox,7);
 uint64_t request=0;ASSERT_EQ(mailbox.RequestSnapshot(&request),0);std::array<uint8_t,16> pixels{};pixels.fill(9);
 ASSERT_TRUE(mailbox.StoreSnapshotCandidate(request,Info(12),pixels.data(),pixels.size()));
 ASSERT_TRUE(mailbox.ConfirmSnapshot(request,41,2,12,3));
 allocation.calls=0;allocation.fail_at=3;
 EXPECT_EQ(mailbox.ConfigureDetailed(64),PreviewMailbox::ConfigureResult::AllocationFailure);EXPECT_EQ(allocation.calls,3);
 auto info=Info(0);pixels.fill(0);ASSERT_EQ(mailbox.CopyLatest(0,pixels.data(),pixels.size(),&info),0);
 EXPECT_EQ(info.frame_id,7u);for(auto value:pixels)EXPECT_EQ(value,7);
 ASSERT_EQ(mailbox.CopySnapshot(request,pixels.data(),pixels.size(),&info),0);for(auto value:pixels)EXPECT_EQ(value,9);
 PreviewMailbox::CopyTicket ticket;ASSERT_TRUE(mailbox.TryBeginCopy(0,false,false,ticket));EXPECT_EQ(ticket.capacity,16u);
 EXPECT_TRUE(mailbox.FinishCopy(ticket,Info(8),true,0));
}
TEST(PreviewMailbox, UnqualifiedLargeCaptureIsRejectedBeforeAnyAllocation) {
 AllocationFailure allocation;PreviewMailbox mailbox(allocation.Policy());
 EXPECT_EQ(mailbox.ConfigureCandidate(3840u*2160u*4u),PreviewMailbox::ConfigureResult::BudgetExceeded);
 EXPECT_EQ(allocation.calls,0);
}
TEST(PreviewMailbox, OptionalAllocationFailureDoesNotFailNativeSourceInitialization) {
 const auto input=std::filesystem::path(HV_TEST_PROJECT_ROOT)/"out/input-native/windows/Release/humanvision_input.dll";
 ASSERT_TRUE(std::filesystem::exists(input))<<"Build the existing independent Input target first";
 AllocationFailure allocation;allocation.fail_at=2;NativeSourceBinding source(allocation.Policy());
 OwnedSourceConfig config;config.kind=HV_NATIVE_SOURCE_RTSP;config.flags=HV_NATIVE_SOURCE_LIVE_PREVIEW;
 config.source_id=41;config.width=config.height=2;config.timeout_ms=100;config.reconnect_delay_ms=50;
 config.library=input.u8string();config.url="rtsp://127.0.0.1:1/optional-preview-allocation";std::string error;
 ASSERT_TRUE(source.Open(config,false,3,error))<<error;
 EXPECT_NE(source.PreviewDiagnostics().find("allocation"),std::string::npos);EXPECT_EQ(source.Progress().errors,0u);
 source.Close();
}
TEST(PreviewMailbox, CallerCanCancelConfirmedCaptureWithoutReadingPixels) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));uint64_t first=0,next=0;
 ASSERT_EQ(mailbox.RequestSnapshot(&first),0);std::array<uint8_t,16> pixels{};ASSERT_TRUE(mailbox.StoreSnapshotCandidate(first,Info(1),pixels.data(),pixels.size()));
 ASSERT_TRUE(mailbox.ConfirmSnapshot(first,41,2,1,3));ASSERT_EQ(mailbox.CancelSnapshot(first),0);
 auto info=Info(0);EXPECT_EQ(mailbox.CopySnapshot(first,nullptr,0,&info),HV_CONTROL_NO_DATA);
 ASSERT_EQ(mailbox.RequestSnapshot(&next),0);EXPECT_NE(first,next);
}
TEST(PreviewMailbox, CancelledRequestDoesNotRetireAnUnprovedHardwareReservation) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));PreviewMailbox::CopyTicket ticket;
 ASSERT_TRUE(mailbox.TryBeginCopy(0,false,false,ticket,true));uint64_t request=0;ASSERT_EQ(mailbox.RequestSnapshot(&request),0);
 ASSERT_EQ(mailbox.CancelSnapshot(request),0);EXPECT_TRUE(mailbox.HasInFlight());
 EXPECT_FALSE(mailbox.FinishCopy(ticket,Info(1),false,0));EXPECT_TRUE(mailbox.HasInFlight());
 mailbox.FinishCopy(ticket,Info(1),true,0,false);EXPECT_FALSE(mailbox.HasInFlight());
}
TEST(PreviewMailbox, DeadlineAndRequestIdentityAreCheckedBeforeConfirmation) {
 ControlledClock clock;PreviewMailbox mailbox({},clock.Clock());ASSERT_TRUE(mailbox.Configure(16));uint64_t first=0,next=0;
 ASSERT_EQ(mailbox.RequestSnapshot(&first),0);std::array<uint8_t,16> pixels{};
 ASSERT_TRUE(mailbox.StoreSnapshotCandidate(first,Info(1),pixels.data(),pixels.size()));
 clock.now+=5000001;
 EXPECT_FALSE(mailbox.ConfirmSnapshot(first,41,2,1,3));auto info=Info(0);
 EXPECT_EQ(mailbox.CopySnapshot(first,nullptr,0,&info),HV_CONTROL_TIMEOUT);
 ASSERT_EQ(mailbox.RequestSnapshot(&next),0);EXPECT_NE(first,next);
 EXPECT_TRUE(mailbox.StoreSnapshotCandidate(next,Info(2),pixels.data(),pixels.size()));
 EXPECT_TRUE(mailbox.ConfirmSnapshot(next,41,2,2,3));
}
TEST(PreviewMailbox, ControlTableAppendsCancelWithoutMovingExistingMembers) {
 EXPECT_EQ(offsetof(HV_RuntimeControlApiV1,CancelSnapshot),offsetof(HV_RuntimeControlApiV1,CopySnapshot)+sizeof(void*));
 HV_RuntimeControlApiV1 api{sizeof(api),1};ASSERT_EQ(HV_QueryRuntimeControlApiV1(1,&api),0);ASSERT_NE(api.CancelSnapshot,nullptr);
 HV_RuntimeControllerConfigV1 config{sizeof(config),1,{sizeof(HV_RuntimeConfigV1),HV_API_VERSION_040,"Z:/no-model-loading","cpu",1,0},0,1000};
 HV_RuntimeControllerHandle handle=nullptr;char error[128]{};ASSERT_EQ(api.CreateDeferred(&config,&handle,error,sizeof(error)),0);
 EXPECT_EQ(api.CancelSnapshot(handle,1),HV_CONTROL_NO_DATA);uint64_t close=0;ASSERT_EQ(api.RequestClose(handle,&close),0);
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);int result=HV_CONTROL_BUSY;
 while((result=api.TryRelease(handle))==HV_CONTROL_BUSY&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();EXPECT_EQ(result,0);
}
TEST(PreviewMailbox, OriginalTwentyEntryTablePrefixRemainsQueryable) {
 HV_RuntimeControlApiV1 api{uint32_t(offsetof(HV_RuntimeControlApiV1,CancelSnapshot)),1};
 const auto sentinel=reinterpret_cast<decltype(api.CancelSnapshot)>(uintptr_t(0x1234));api.CancelSnapshot=sentinel;
 ASSERT_EQ(HV_QueryRuntimeControlApiV1(1,&api),0);
 EXPECT_EQ(api.struct_size,168u);EXPECT_EQ(api.CancelSnapshot,sentinel);ASSERT_NE(api.CopySnapshot,nullptr);
}
