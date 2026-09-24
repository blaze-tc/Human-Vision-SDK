#include "gpu/android/ahb_slot_ring.h"
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <type_traits>
#include <vector>
using namespace humanvision::gpu;
namespace {
SlotContract Contract(){ SlotContract c; c.width=640;c.height=480;c.actual_format=1;c.actual_usage=768;c.camera_session=1;return c; }
struct Owners {
 int created=0,drained=0,closed=0,released=0,fail_slot=-1;
 std::array<uintptr_t,3> handles{};std::vector<uintptr_t> release_order;
 std::atomic<bool> block_drain{false},entered{false};
 static void Close(void* p,int) noexcept { ++static_cast<Owners*>(p)->closed; }
 static void Delete(void* p,uintptr_t h) noexcept { auto& o=*static_cast<Owners*>(p);++o.released;o.release_order.push_back(h); }
 static bool Create(void* p,uint32_t i,const SlotContract&,SlotResources& r) noexcept {
  auto& o=*static_cast<Owners*>(p);++o.created;uintptr_t base=100*o.created;
  r.ahb_allocation={base+1,Delete,p};r.unity_ahb_lease={base+2,Delete,p};r.ncnn_ahb_lease={base+3,Delete,p};
  r.unity_image={base+4,Delete,p};r.ncnn_allocator={base+5,Delete,p};r.ncnn_image_mat={base+6,Delete,p};r.ncnn_import_pipeline={base+7,Delete,p};
  r.unity_memory={base+8,Delete,p};r.unity_view={base+9,Delete,p};r.unity_framebuffer={base+10,Delete,p};
  r.unity_command_pool={base+11,Delete,p};r.unity_command_buffer={base+12,Delete,p};r.unity_fence={base+13,Delete,p};r.unity_export_semaphore={base+14,Delete,p};
  r.ncnn_import_semaphore={base+15,Delete,p};r.ncnn_command_pool={base+16,Delete,p};r.ncnn_ownership_command={base+17,Delete,p};
  r.ncnn_completion={base+18,Delete,p};r.ncnn_preprocessing_buffers={base+19,Delete,p};
  o.handles[i]=base+4;return int(i)!=o.fail_slot;
 }
 static void Drain(void* p,uint32_t,AhbSlotState,SlotResources&,SyncFd&) noexcept {
  auto& o=*static_cast<Owners*>(p);++o.drained;o.entered=true;while(o.block_drain.load())std::this_thread::yield();
 }
 SlotLifecycle Hooks(){ return {this,Create,Drain}; }
 SyncFd Fd(int n){ return SyncFd(n,Close,this); }
};
void Ready(AhbSlotRing& r,Owners& o,SlotToken t){
 ASSERT_EQ(r.Transition(t,AhbSlotState::EventReserved,AhbSlotState::UnityCopySubmitted),SlotResult::Ok);
 ASSERT_EQ(r.Transition(t,AhbSlotState::UnityCopySubmitted,AhbSlotState::ProducerSignalPending),SlotResult::Ok);
 auto fd=o.Fd(int(t.frame_id));ASSERT_EQ(r.PublishReady(t,fd),SlotResult::Ok);EXPECT_EQ(fd.Get(),-1);
}
void Complete(AhbSlotRing& r,SlotToken t){
 ASSERT_EQ(r.Transition(t,AhbSlotState::InferenceRunning,AhbSlotState::ConsumerReleasePending),SlotResult::Ok);
 ASSERT_EQ(r.Transition(t,AhbSlotState::ConsumerReleasePending,AhbSlotState::Free,CompletionProof::GpuQuiescent),SlotResult::Ok);
}
}
TEST(AhbSlotRing, ExhaustiveTransitionMatrixRejectsEveryIllegalEdge){
 using S=AhbSlotState;
 for(int a=0;a<int(S::Count);++a)for(int b=0;b<int(S::Count);++b)for(bool proof:{false,true}){
  S from=S(a),to=S(b);bool normal=(from==S::Free&&to==S::EventReserved)||(from==S::EventReserved&&to==S::UnityCopySubmitted)||
   (from==S::UnityCopySubmitted&&to==S::ProducerSignalPending)||(from==S::ProducerSignalPending&&to==S::ReadyForNcnn)||
   (from==S::ReadyForNcnn&&to==S::InferenceRunning)||(from==S::InferenceRunning&&to==S::ConsumerReleasePending)||
   (from==S::ReadyForNcnn&&to==S::DropDrain);
  bool guarded=proof&&((to==S::Free&&(from==S::ConsumerReleasePending||from==S::DropDrain))||
   (to==S::DropDrain&&from!=S::Free&&from!=S::DropDrain));
  EXPECT_EQ(AhbSlotRing::IsLegalTransition(from,to,proof?CompletionProof::GpuQuiescent:CompletionProof::None),normal||guarded)<<a<<","<<b<<","<<proof;
 }
 EXPECT_FALSE(AhbSlotRing::IsLegalTransition(S::Count,S::Free,CompletionProof::GpuQuiescent));
}
TEST(AhbSlotRing, ThreeSlotsNewestWinsDropsRequireCompletionAndOldTokensFail){
 Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));SlotToken t[3],extra;
 for(int i=0;i<3;++i){ASSERT_EQ(r.Reserve(i+1,100+i,t[i]),SlotResult::Ok);Ready(r,o,t[i]);}
 EXPECT_EQ(r.Reserve(4,104,extra),SlotResult::NoSlot);EXPECT_EQ(r.Counters().no_slot_drops,1u);
 SlotToken selected;SlotMetadata data;ASSERT_EQ(r.ClaimNewest(selected,data),SlotResult::Ok);
 EXPECT_EQ(selected.frame_id,3u);EXPECT_EQ(data.timestamp_us,102);EXPECT_EQ(r.Counters().generation_drops,2u);
 for(int i=0;i<2;++i){SlotSnapshot s;ASSERT_EQ(r.Inspect(t[i].index,s),SlotResult::Ok);EXPECT_EQ(s.state,AhbSlotState::DropDrain);
  EXPECT_EQ(r.Transition(t[i],AhbSlotState::DropDrain,AhbSlotState::Free),SlotResult::Invalid);
  ASSERT_EQ(r.Transition(t[i],AhbSlotState::DropDrain,AhbSlotState::Free,CompletionProof::GpuQuiescent),SlotResult::Ok);}
 EXPECT_EQ(o.closed,2);EXPECT_EQ(o.released,0);Complete(r,selected);EXPECT_EQ(o.closed,3);
 ASSERT_EQ(r.Reserve(4,104,extra),SlotResult::Ok);EXPECT_EQ(extra.index,t[0].index);
 EXPECT_EQ(r.Transition(t[0],AhbSlotState::EventReserved,AhbSlotState::UnityCopySubmitted),SlotResult::Invalid);
 EXPECT_EQ(r.Counters().generation_drops,2u);
}
TEST(AhbSlotRing, DroppedReadyFramesTransferTheirFenceOnceForGpuDrain){
 Owners o; AhbSlotRing r(o.Hooks()); ASSERT_TRUE(r.Reconfigure(Contract()));
 SlotToken first, second, newest; SlotMetadata metadata;
 for(int i=0;i<3;++i){SlotToken* token=i==0?&first:(i==1?&second:&newest);
  ASSERT_EQ(r.Reserve(i+1,i+1,*token),SlotResult::Ok);Ready(r,o,*token);}
 ASSERT_EQ(r.ClaimNewest(newest,metadata),SlotResult::Ok);
 SlotToken dropped; SyncFd fd;
 ASSERT_EQ(r.ClaimDropped(dropped,metadata,fd),SlotResult::Ok);
 EXPECT_EQ(dropped.frame_id,1u);EXPECT_EQ(fd.Get(),1);
 SlotToken occupied;SlotMetadata ignored;
 EXPECT_EQ(r.ClaimDropped(occupied,ignored,fd),SlotResult::Invalid);
 fd.Reset(); ASSERT_EQ(r.RetireConsumer(dropped,CompletionProof::GpuQuiescent),SlotResult::Ok);
 ASSERT_EQ(r.ClaimDropped(dropped,metadata,fd),SlotResult::Ok);
 EXPECT_EQ(dropped.frame_id,2u);fd.Reset();
 ASSERT_EQ(r.RetireConsumer(dropped,CompletionProof::GpuQuiescent),SlotResult::Ok);
 EXPECT_EQ(r.ClaimDropped(dropped,metadata,fd),SlotResult::NoReady);
}
TEST(AhbSlotRing, LatePublishedOlderFrameCannotSupersedeAnAlreadyClaimedFrame){
 Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));SlotToken old,newest,selected;SlotMetadata data;
 ASSERT_EQ(r.Reserve(1,1,old),SlotResult::Ok);ASSERT_EQ(r.Reserve(2,2,newest),SlotResult::Ok);Ready(r,o,newest);
 ASSERT_EQ(r.ClaimNewest(selected,data),SlotResult::Ok);Ready(r,o,old);
 EXPECT_EQ(r.ClaimNewest(selected,data),SlotResult::NoReady);EXPECT_EQ(r.Counters().generation_drops,0u);
 Complete(r,newest);EXPECT_EQ(r.ClaimNewest(selected,data),SlotResult::NoReady);EXPECT_EQ(r.Counters().generation_drops,1u);
 SlotSnapshot s;ASSERT_EQ(r.Inspect(old.index,s),SlotResult::Ok);EXPECT_EQ(s.state,AhbSlotState::DropDrain);
 EXPECT_EQ(r.Reserve(2,3,selected),SlotResult::Invalid);
}
TEST(AhbSlotRing, RebuildKeyIncludesEveryContractFieldAndPreservesIdenticalResources){
 Owners o;AhbSlotRing r(o.Hooks());auto c=Contract();ASSERT_TRUE(r.Reconfigure(c));const auto handles=o.handles;
 EXPECT_TRUE(r.Reconfigure(c));EXPECT_EQ(o.handles,handles);EXPECT_EQ(o.created,3);EXPECT_EQ(o.drained,0);
 std::array<SlotContract,8> changes;changes.fill(c);++changes[0].width;++changes[1].height;++changes[2].actual_format;
 ++changes[3].actual_usage;changes[4].rotation=90;changes[5].mirror=true;++changes[6].camera_session;++changes[7].input_contract_hash[31];
 for(auto changed:changes){auto gen=r.Generation();ASSERT_TRUE(r.Reconfigure(changed));EXPECT_EQ(r.Generation(),gen+1);EXPECT_TRUE(r.Reconfigure(changed));}
 EXPECT_EQ(o.created,27);EXPECT_EQ(o.released,24*19);EXPECT_TRUE(r.Shutdown());EXPECT_EQ(o.released,27*19);
 EXPECT_TRUE(r.Shutdown());EXPECT_EQ(o.released,27*19);
 for(size_t i=0;i<o.release_order.size();i+=19){EXPECT_EQ(o.release_order[i]%100,7u);EXPECT_EQ(o.release_order[i+18]%100,1u);
  std::array<bool,20> seen{};for(size_t j=i;j<i+19;++j){const auto kind=o.release_order[j]%100;EXPECT_FALSE(seen[kind]);seen[kind]=true;}}
}
TEST(AhbSlotRing, FdMoveTransferFailureAndPartialCreationReleaseExactlyOnce){
 static_assert(!std::is_copy_constructible<SyncFd>::value);static_assert(!std::is_copy_constructible<OwnedSlotResource>::value);
 Owners o;{auto a=o.Fd(10);auto b=std::move(a);EXPECT_EQ(a.Get(),-1);auto c=o.Fd(11);c=std::move(b);EXPECT_EQ(o.closed,1);EXPECT_EQ(c.Release(),10);EXPECT_EQ(c.Get(),-1);}EXPECT_EQ(o.closed,1);
 {AhbSlotRing r(o.Hooks());o.fail_slot=1;EXPECT_FALSE(r.Reconfigure(Contract()));EXPECT_EQ(o.created,2);EXPECT_EQ(o.released,38);
  SlotToken t;EXPECT_EQ(r.Reserve(1,1,t),SlotResult::Closed);o.fail_slot=-1;ASSERT_TRUE(r.Reconfigure(Contract()));ASSERT_EQ(r.Reserve(1,1,t),SlotResult::Ok);
  auto fd=o.Fd(12);EXPECT_EQ(r.PublishReady(t,fd),SlotResult::Invalid);EXPECT_EQ(fd.Get(),12);
  Ready(r,o,t);SlotToken selected;SlotMetadata m;ASSERT_EQ(r.ClaimNewest(selected,m),SlotResult::Ok);SyncFd imported;
  ASSERT_EQ(r.TakeProducerFence(selected,imported),SlotResult::Ok);EXPECT_EQ(imported.Release(),1);
  EXPECT_EQ(r.TakeProducerFence(selected,imported),SlotResult::Invalid);Complete(r,selected);
 }EXPECT_EQ(o.closed,2);EXPECT_EQ(o.released,95);
}
TEST(AhbSlotRing, ShutdownDrainsEveryIntermediateStateBeforeReleasingResources){
 for(int state=1;state<int(AhbSlotState::Count);++state){
  Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));SlotToken t;ASSERT_EQ(r.Reserve(1,1,t),SlotResult::Ok);
  if(state>=2)ASSERT_EQ(r.Transition(t,AhbSlotState::EventReserved,AhbSlotState::UnityCopySubmitted),SlotResult::Ok);
  if(state>=3)ASSERT_EQ(r.Transition(t,AhbSlotState::UnityCopySubmitted,AhbSlotState::ProducerSignalPending),SlotResult::Ok);
  if(state>=4){auto fd=o.Fd(1);ASSERT_EQ(r.PublishReady(t,fd),SlotResult::Ok);}
  if(state>=5&&state<7){SlotMetadata m;ASSERT_EQ(r.ClaimNewest(t,m),SlotResult::Ok);}
  if(state==6)ASSERT_EQ(r.Transition(t,AhbSlotState::InferenceRunning,AhbSlotState::ConsumerReleasePending),SlotResult::Ok);
  if(state==7)ASSERT_EQ(r.Transition(t,AhbSlotState::ReadyForNcnn,AhbSlotState::DropDrain),SlotResult::Ok);
  EXPECT_FALSE(r.Shutdown(CallContext::Render));EXPECT_EQ(o.released,0);
  EXPECT_TRUE(r.Shutdown());EXPECT_EQ(o.drained,3);EXPECT_EQ(o.released,57);EXPECT_EQ(o.closed,state>=4?1:0);
 }
}
TEST(AhbSlotRing, RebuildClosesAdmissionsAndNeverBlocksRenderOperations){
 Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));SlotToken old;ASSERT_EQ(r.Reserve(1,1,old),SlotResult::Ok);Ready(r,o,old);
 o.block_drain=true;auto changed=Contract();++changed.camera_session;
 std::thread control([&]{EXPECT_TRUE(r.Reconfigure(changed));});while(!o.entered.load())std::this_thread::yield();
 SlotToken t;EXPECT_EQ(r.Reserve(2,2,t),SlotResult::Closed);
 EXPECT_EQ(r.Transition(old,AhbSlotState::ReadyForNcnn,AhbSlotState::InferenceRunning),SlotResult::Closed);
 EXPECT_FALSE(r.Reconfigure(changed,CallContext::Render));o.block_drain=false;control.join();
 EXPECT_EQ(r.Generation(),2u);EXPECT_EQ(o.closed,1);EXPECT_EQ(o.released,57);
 EXPECT_EQ(r.Counters().generation_drops,1u);
 EXPECT_EQ(r.Transition(old,AhbSlotState::ReadyForNcnn,AhbSlotState::InferenceRunning),SlotResult::Invalid);
}
TEST(AhbSlotRing, ConcurrentProducersCannotReserveOneSlotTwice){
 Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));std::atomic<int> success{0};std::atomic<bool> go{false};
 std::array<SlotToken,12> tokens;std::vector<std::thread> threads;
 // Unique frame numbers need not arrive ordered: stale IDs may be rejected.
 for(int i=0;i<12;++i)threads.emplace_back([&,i]{while(!go.load())std::this_thread::yield();SlotResult result;
  do{result=r.Reserve(i+1,1000+i,tokens[i]);}while(result==SlotResult::Busy);if(result==SlotResult::Ok)++success;});
 go=true;for(auto& thread:threads)thread.join();EXPECT_GT(success.load(),0);EXPECT_LE(success.load(),3);
 for(int i=0;i<12;++i)for(int j=i+1;j<12;++j)if(tokens[i].index<3&&tokens[j].index<3)EXPECT_NE(tokens[i].index,tokens[j].index);
}
TEST(AhbSlotRing, IllegalOperationsNeverMutateAnyReachableState){
 using S=AhbSlotState;
 for(int state=1;state<int(S::Count);++state){
  Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));SlotToken t;ASSERT_EQ(r.Reserve(1,1,t),SlotResult::Ok);
  if(state>=2)ASSERT_EQ(r.Transition(t,S::EventReserved,S::UnityCopySubmitted),SlotResult::Ok);
  if(state>=3)ASSERT_EQ(r.Transition(t,S::UnityCopySubmitted,S::ProducerSignalPending),SlotResult::Ok);
  if(state>=4){auto fd=o.Fd(1);ASSERT_EQ(r.PublishReady(t,fd),SlotResult::Ok);}
  if(state>=5&&state<7){SlotMetadata m;ASSERT_EQ(r.ClaimNewest(t,m),SlotResult::Ok);}
  if(state==6)ASSERT_EQ(r.Transition(t,S::InferenceRunning,S::ConsumerReleasePending),SlotResult::Ok);
  if(state==7)ASSERT_EQ(r.Transition(t,S::ReadyForNcnn,S::DropDrain),SlotResult::Ok);
  for(int target=0;target<int(S::Count);++target)for(bool proof:{false,true}){
   auto evidence=proof?CompletionProof::GpuQuiescent:CompletionProof::None;
   if(AhbSlotRing::IsLegalTransition(S(state),S(target),evidence))continue;
   EXPECT_EQ(r.Transition(t,S(state),S(target),evidence),SlotResult::Invalid);
   SlotSnapshot snapshot;ASSERT_EQ(r.Inspect(t.index,snapshot),SlotResult::Ok);EXPECT_EQ(snapshot.state,S(state));EXPECT_EQ(snapshot.metadata.frame_id,1u);
  }
  if(state!=7){ASSERT_EQ(r.Transition(t,S(state),S::DropDrain,CompletionProof::GpuQuiescent),SlotResult::Ok);}
  ASSERT_EQ(r.Transition(t,S::DropDrain,S::Free,CompletionProof::GpuQuiescent),SlotResult::Ok);
 }
}
TEST(AhbSlotRing, ConcurrentPublicationPreservesMetadataAndExclusiveFdOwnership){
 Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));std::atomic<int> completed{0};
 std::thread worker([&]{
  for(int frame=1;frame<=256;++frame){SlotToken t;SlotMetadata m;SlotResult result;
   do{result=r.ClaimNewest(t,m);std::this_thread::yield();}while(result==SlotResult::NoReady||result==SlotResult::Busy);
   EXPECT_EQ(result,SlotResult::Ok);EXPECT_EQ(m.frame_id,uint64_t(frame));EXPECT_EQ(m.timestamp_us,frame*1234);EXPECT_EQ(m.generation,1u);
   SyncFd fd;do{result=r.TakeProducerFence(t,fd);}while(result==SlotResult::Busy);EXPECT_EQ(result,SlotResult::Ok);EXPECT_EQ(fd.Get(),frame);fd.Reset();
   do{result=r.Transition(t,AhbSlotState::InferenceRunning,AhbSlotState::ConsumerReleasePending);}while(result==SlotResult::Busy);EXPECT_EQ(result,SlotResult::Ok);
   do{result=r.Transition(t,AhbSlotState::ConsumerReleasePending,AhbSlotState::Free,CompletionProof::GpuQuiescent);}while(result==SlotResult::Busy);EXPECT_EQ(result,SlotResult::Ok);
   completed.store(frame,std::memory_order_release);
  }
 });
 for(int frame=1;frame<=256;++frame){SlotToken t;SlotResult result;
  do{result=r.Reserve(frame,frame*1234,t);}while(result==SlotResult::Busy);EXPECT_EQ(result,SlotResult::Ok);
  do{result=r.Transition(t,AhbSlotState::EventReserved,AhbSlotState::UnityCopySubmitted);}while(result==SlotResult::Busy);EXPECT_EQ(result,SlotResult::Ok);
  do{result=r.Transition(t,AhbSlotState::UnityCopySubmitted,AhbSlotState::ProducerSignalPending);}while(result==SlotResult::Busy);EXPECT_EQ(result,SlotResult::Ok);
  auto fd=o.Fd(frame);do{result=r.PublishReady(t,fd);}while(result==SlotResult::Busy);EXPECT_EQ(result,SlotResult::Ok);
  while(completed.load(std::memory_order_acquire)<frame)std::this_thread::yield();
 }
 worker.join();EXPECT_EQ(o.closed,256);EXPECT_EQ(o.created,3);EXPECT_EQ(o.released,0);EXPECT_TRUE(r.Shutdown());EXPECT_EQ(o.released,57);
}
TEST(AhbSlotRing, ConcurrentControlOperationsCannotReopenShutdownAdmission){
 Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));o.block_drain=true;
 auto changed=Contract();++changed.camera_session;
 std::thread rebuild([&]{EXPECT_TRUE(r.Reconfigure(changed));});while(!o.entered.load())std::this_thread::yield();
 std::atomic<bool> shutdown_started{false};std::thread shutdown([&]{shutdown_started=true;EXPECT_TRUE(r.Shutdown());});
 while(!shutdown_started.load())std::this_thread::yield();SlotToken t;EXPECT_EQ(r.Reserve(1,1,t),SlotResult::Closed);
 o.block_drain=false;rebuild.join();shutdown.join();EXPECT_EQ(o.created,6);EXPECT_EQ(o.released,6*19);
 EXPECT_EQ(r.Reserve(1,1,t),SlotResult::Closed);
}
TEST(AhbSlotRing, SignaledPayloadMovesPublishesRetriesAndTakesExactlyOnce){
 for(int value:{9,0,-1}){
  Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));SlotToken t;ASSERT_EQ(r.Reserve(1,1,t),SlotResult::Ok);
  auto fd=o.Fd(value);auto moved=std::move(fd);SyncFd destination;destination=std::move(moved);
  EXPECT_EQ(fd.State(),SyncPayloadState::Empty);EXPECT_EQ(moved.State(),SyncPayloadState::Empty);
  EXPECT_EQ(destination.State(),value>=0?SyncPayloadState::OwnedFd:SyncPayloadState::AlreadySignaled);
  EXPECT_EQ(r.PublishReady(t,destination),SlotResult::Invalid); // Wrong state retains a real payload.
  ASSERT_EQ(r.Transition(t,AhbSlotState::EventReserved,AhbSlotState::UnityCopySubmitted),SlotResult::Ok);
  ASSERT_EQ(r.Transition(t,AhbSlotState::UnityCopySubmitted,AhbSlotState::ProducerSignalPending),SlotResult::Ok);
  EXPECT_EQ(r.PublishReady(t,fd),SlotResult::Invalid);EXPECT_EQ(r.PublishReady(t,moved),SlotResult::Invalid);
  SyncFd empty;EXPECT_EQ(r.PublishReady(t,empty),SlotResult::Invalid);
  ASSERT_EQ(r.PublishReady(t,destination),SlotResult::Ok);
  EXPECT_FALSE(destination.HasPayload());
  SlotMetadata m;ASSERT_EQ(r.ClaimNewest(t,m),SlotResult::Ok);
  auto occupied=o.Fd(-1);EXPECT_EQ(r.TakeProducerFence(t,occupied),SlotResult::Invalid);
  SyncFd taken;ASSERT_EQ(r.TakeProducerFence(t,taken),SlotResult::Ok);EXPECT_EQ(taken.Get(),value);
  EXPECT_TRUE(taken.HasPayload());EXPECT_EQ(taken.State(),value>=0?SyncPayloadState::OwnedFd:SyncPayloadState::AlreadySignaled);
  SyncFd again;EXPECT_EQ(r.TakeProducerFence(t,again),SlotResult::Invalid);
  taken.Reset();EXPECT_EQ(taken.State(),SyncPayloadState::Empty);taken.Reset();Complete(r,t);EXPECT_TRUE(r.Shutdown());EXPECT_EQ(o.closed,value>=0?1:0);
 }
}
TEST(AhbSlotRing, SignaledPayloadDropAndTeardownNeverCloseMinusOne){
 for(bool drop:{false,true}){
  Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));SlotToken t;ASSERT_EQ(r.Reserve(1,1,t),SlotResult::Ok);
  ASSERT_EQ(r.Transition(t,AhbSlotState::EventReserved,AhbSlotState::UnityCopySubmitted),SlotResult::Ok);
  ASSERT_EQ(r.Transition(t,AhbSlotState::UnityCopySubmitted,AhbSlotState::ProducerSignalPending),SlotResult::Ok);
  auto fd=o.Fd(-1);ASSERT_EQ(r.PublishReady(t,fd),SlotResult::Ok);
  if(drop){ASSERT_EQ(r.Transition(t,AhbSlotState::ReadyForNcnn,AhbSlotState::DropDrain),SlotResult::Ok);
   SyncFd taken;ASSERT_EQ(r.TakeProducerFence(t,taken),SlotResult::Ok);EXPECT_EQ(taken.Release(),-1);
   EXPECT_EQ(taken.State(),SyncPayloadState::Empty);
   SyncFd again;EXPECT_EQ(r.TakeProducerFence(t,again),SlotResult::Invalid);
   ASSERT_EQ(r.Transition(t,AhbSlotState::DropDrain,AhbSlotState::Free,CompletionProof::GpuQuiescent),SlotResult::Ok);}
  EXPECT_TRUE(r.Shutdown());EXPECT_EQ(o.closed,0);EXPECT_EQ(o.released,57);
 }
}
TEST(AhbSlotRing, RunningAndReleasePendingBlockAdditionalInferenceUntilFree){
 for(bool release_pending:{false,true}){
  Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));SlotToken first,second,third,selected;SlotMetadata m;
  ASSERT_EQ(r.Reserve(1,1,first),SlotResult::Ok);Ready(r,o,first);ASSERT_EQ(r.ClaimNewest(selected,m),SlotResult::Ok);
  if(release_pending)ASSERT_EQ(r.Transition(first,AhbSlotState::InferenceRunning,AhbSlotState::ConsumerReleasePending),SlotResult::Ok);
  ASSERT_EQ(r.Reserve(2,2,second),SlotResult::Ok);Ready(r,o,second);
  ASSERT_EQ(r.Reserve(3,3,third),SlotResult::Ok);Ready(r,o,third);
  EXPECT_EQ(r.ClaimNewest(selected,m),SlotResult::NoReady);EXPECT_EQ(r.Counters().generation_drops,0u);
  SlotSnapshot s;ASSERT_EQ(r.Inspect(third.index,s),SlotResult::Ok);EXPECT_EQ(s.state,AhbSlotState::ReadyForNcnn);
  if(!release_pending)ASSERT_EQ(r.Transition(first,AhbSlotState::InferenceRunning,AhbSlotState::ConsumerReleasePending),SlotResult::Ok);
  ASSERT_EQ(r.Transition(first,AhbSlotState::ConsumerReleasePending,AhbSlotState::Free,CompletionProof::GpuQuiescent),SlotResult::Ok);
  ASSERT_EQ(r.ClaimNewest(selected,m),SlotResult::Ok);EXPECT_EQ(selected.frame_id,3u);EXPECT_EQ(r.Counters().generation_drops,1u);
  EXPECT_EQ(r.ClaimNewest(selected,m),SlotResult::NoReady);EXPECT_EQ(r.Counters().generation_drops,1u);
 }
}
TEST(AhbSlotRing, TwoConcurrentClaimersCanOnlyStartOneInference){
 for(int active_state=0;active_state<=2;++active_state){
  Owners o;AhbSlotRing r(o.Hooks());ASSERT_TRUE(r.Reconfigure(Contract()));SlotToken first;SlotMetadata metadata;
  ASSERT_EQ(r.Reserve(1,1,first),SlotResult::Ok);Ready(r,o,first);
  if(active_state)ASSERT_EQ(r.ClaimNewest(first,metadata),SlotResult::Ok);
  if(active_state==2)ASSERT_EQ(r.Transition(first,AhbSlotState::InferenceRunning,AhbSlotState::ConsumerReleasePending),SlotResult::Ok);
  for(int frame=2;frame<=3;++frame){SlotToken t;ASSERT_EQ(r.Reserve(frame,frame,t),SlotResult::Ok);Ready(r,o,t);}
  std::atomic<bool> go{false};std::array<SlotResult,2> results{};std::array<SlotToken,2> selected{};
  auto claim=[&](int i){while(!go.load())std::this_thread::yield();SlotMetadata m;
   do{results[i]=r.ClaimNewest(selected[i],m);}while(results[i]==SlotResult::Busy);};
  std::thread a(claim,0),b(claim,1);go=true;a.join();b.join();
  if(!active_state){
   EXPECT_EQ(int(results[0]==SlotResult::Ok)+int(results[1]==SlotResult::Ok),1);
   EXPECT_TRUE(results[0]==SlotResult::NoReady||results[1]==SlotResult::NoReady);
   auto winner=results[0]==SlotResult::Ok?selected[0]:selected[1];EXPECT_EQ(winner.frame_id,3u);
   EXPECT_EQ(r.Counters().generation_drops,2u);
  }else{
   EXPECT_EQ(results[0],SlotResult::NoReady);EXPECT_EQ(results[1],SlotResult::NoReady);EXPECT_EQ(r.Counters().generation_drops,0u);
   if(active_state==1)Complete(r,first);
   else ASSERT_EQ(r.Transition(first,AhbSlotState::ConsumerReleasePending,AhbSlotState::Free,CompletionProof::GpuQuiescent),SlotResult::Ok);
   SlotToken latest;ASSERT_EQ(r.ClaimNewest(latest,metadata),SlotResult::Ok);EXPECT_EQ(latest.frame_id,3u);EXPECT_EQ(r.Counters().generation_drops,1u);
  }
 }
}
