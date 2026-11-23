#include "composition/preview_mailbox.h"
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>

using namespace humanvision::runtime;
namespace {
HV_RuntimePreviewV1 Info(uint64_t frame,uint64_t generation=2) {
 return {sizeof(HV_RuntimePreviewV1),1,2,2,8,16,41,generation,frame,3,1000};
}
void Publish(PreviewMailbox& mailbox,uint64_t sequence) {
 PreviewMailbox::CopyTicket ticket;ASSERT_TRUE(mailbox.TryBeginCopy(0,false,false,ticket));
 std::memset(ticket.data,int(sequence),16);ASSERT_TRUE(mailbox.FinishCopy(ticket,Info(sequence),true,0));
}
struct BlockingAllocator {
 std::atomic<bool> block_next{false};
 std::promise<void> entered,release;
 std::shared_future<void> resumed=release.get_future().share();
 static void Allocate(void* context,std::vector<uint8_t>& bytes,uint32_t count) {
  auto& self=*static_cast<BlockingAllocator*>(context);
  if(self.block_next.exchange(false)){self.entered.set_value();self.resumed.wait();}
  bytes.resize(count);
 }
 PreviewMailbox::AllocationPolicy Policy(){return {this,Allocate};}
};
}
TEST(PreviewAllocationProgress, SourceAdmissionCompletesWhileAllocatorIsActuallyBlocked) {
 BlockingAllocator allocator;auto entered=allocator.entered.get_future();
 PreviewMailbox mailbox(allocator.Policy());ASSERT_TRUE(mailbox.Configure(16));
 ASSERT_TRUE(mailbox.AdmitSourceGeneration(41,2,3));Publish(mailbox,1);
 uint64_t request=0;ASSERT_EQ(mailbox.RequestSnapshot(&request),0);std::array<uint8_t,16> pixels{};
 ASSERT_TRUE(mailbox.StoreSnapshotCandidate(request,Info(1),pixels.data(),pixels.size()));
 ASSERT_TRUE(mailbox.ConfirmSnapshot(request,41,2,1,3));
 allocator.block_next=true;
 auto growth=std::async(std::launch::async,[&]{return mailbox.ConfigureDetailed(64);});
 const bool allocating=entered.wait_for(std::chrono::seconds(2))==std::future_status::ready;
 if(!allocating){allocator.release.set_value();growth.wait();ASSERT_TRUE(allocating);return;}
 auto admission=std::async(std::launch::async,[&]{return mailbox.AdmitSourceGeneration(41,3,3);});
 const bool progressed=admission.wait_for(std::chrono::milliseconds(250))==std::future_status::ready;
 allocator.release.set_value();const auto configured=growth.get();const auto admitted=admission.get();
 EXPECT_TRUE(progressed)<<"The source must admit generation3 while optional allocation is still held";
 EXPECT_TRUE(admitted);EXPECT_EQ(configured,PreviewMailbox::ConfigureResult::Busy);
 auto info=Info(0);EXPECT_EQ(mailbox.CopySnapshot(request,pixels.data(),pixels.size(),&info),HV_CONTROL_NO_DATA);
 PreviewMailbox::CopyTicket ticket;ASSERT_TRUE(mailbox.TryBeginCopy(0,false,false,ticket,true));
 EXPECT_EQ(ticket.capacity,16u);mailbox.FinishCopy(ticket,Info(2,3),true,0,false);
}
TEST(PreviewAllocationProgress, SameCapacityLetsOtherSlotsServeLiveAndExactWhileOneReaderIsPinned) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));Publish(mailbox,1);auto pinned=mailbox.AcquireLatest(0);ASSERT_TRUE(pinned);
 EXPECT_EQ(mailbox.ConfigureCandidate(16),PreviewMailbox::ConfigureResult::Ready);
 PreviewMailbox::CopyTicket live;ASSERT_TRUE(mailbox.TryBeginCopy(0,false,false,live));
 std::memset(live.data,2,16);ASSERT_TRUE(mailbox.FinishCopy(live,Info(2),true,0));
 EXPECT_EQ(mailbox.ConfigureCandidate(16),PreviewMailbox::ConfigureResult::Ready);
 PreviewMailbox::CopyTicket exact;ASSERT_TRUE(mailbox.TryBeginCopy(0,false,false,exact,true));
 std::memset(exact.data,3,16);uint64_t request=0;ASSERT_EQ(mailbox.RequestSnapshot(&request),0);
 ASSERT_TRUE(mailbox.StoreSnapshotCandidate(request,Info(3),exact.data,16));
 ASSERT_TRUE(mailbox.ConfirmSnapshot(request,41,2,3,3));mailbox.FinishCopy(exact,Info(3),true,0,false);
 std::array<uint8_t,16> pixels{};auto info=Info(0);ASSERT_EQ(mailbox.CopySnapshot(request,pixels.data(),pixels.size(),&info),0);
 for(auto value:pixels)EXPECT_EQ(value,3);for(uint32_t i=0;i<16;++i)EXPECT_EQ(pinned.Data()[i],1);
 ASSERT_EQ(mailbox.CopyLatest(0,pixels.data(),pixels.size(),&info),0);EXPECT_EQ(info.frame_id,2u);
}
TEST(PreviewAllocationProgress, SameCapacityNoOpDoesNotRetireAnUnprovedWriter) {
 PreviewMailbox mailbox;ASSERT_TRUE(mailbox.Configure(16));PreviewMailbox::CopyTicket ticket;
 ASSERT_TRUE(mailbox.TryBeginCopy(0,false,false,ticket,true));
 EXPECT_EQ(mailbox.ConfigureCandidate(16),PreviewMailbox::ConfigureResult::Ready);EXPECT_TRUE(mailbox.HasInFlight());
 EXPECT_FALSE(mailbox.FinishCopy(ticket,Info(1),false,0));EXPECT_TRUE(mailbox.HasInFlight());
 EXPECT_TRUE(mailbox.FinishCopy(ticket,Info(1),true,0));EXPECT_FALSE(mailbox.HasInFlight());
}
TEST(PreviewAllocationProgress, AllocationCommitRechecksAWriterAdmittedDuringAllocation) {
 BlockingAllocator allocator;auto entered=allocator.entered.get_future();PreviewMailbox mailbox(allocator.Policy());
 ASSERT_TRUE(mailbox.Configure(16));allocator.block_next=true;
 auto growth=std::async(std::launch::async,[&]{return mailbox.ConfigureDetailed(64);});
 const bool allocating=entered.wait_for(std::chrono::seconds(2))==std::future_status::ready;
 if(!allocating){allocator.release.set_value();growth.wait();ASSERT_TRUE(allocating);return;}
 PreviewMailbox::CopyTicket ticket;const bool admitted=mailbox.TryBeginCopy(0,false,false,ticket,true);
 allocator.release.set_value();const auto configured=growth.get();
 EXPECT_TRUE(admitted);EXPECT_EQ(configured,PreviewMailbox::ConfigureResult::Busy);
 if(admitted){EXPECT_EQ(ticket.capacity,16u);EXPECT_TRUE(mailbox.HasInFlight());
  EXPECT_FALSE(mailbox.FinishCopy(ticket,Info(1),false,0));EXPECT_TRUE(mailbox.HasInFlight());
  EXPECT_TRUE(mailbox.FinishCopy(ticket,Info(1),true,0));}
}
