#include "common/observation_journal.h"
#include "common/completion_mailbox.h"
#include <gtest/gtest.h>
#include <future>

using namespace humanvision::runtime;

TEST(ObservationJournal, SlowReaderReportsOverrun) {
 ObservationJournal journal(3);
 ObservationEntry entry{};
 for(int i=1;i<=8;++i){entry.envelope.frame_id=i;entry.body_sequence=i;journal.Publish(entry);}
 auto output=std::make_unique<ObservationEntry[]>(3);
 const auto slow=journal.ReadAfter(0,output.get(),3);
 ASSERT_EQ(slow.written,3u);EXPECT_EQ(slow.lost_count,5u);EXPECT_EQ(slow.next_cursor,8u);
 EXPECT_EQ(output[0].sequence,6u);EXPECT_EQ(output[0].envelope.frame_id,6);
 EXPECT_EQ(output[2].sequence,8u);
 const auto fast=journal.ReadAfter(7,output.get(),3);
 ASSERT_EQ(fast.written,1u);EXPECT_EQ(fast.lost_count,0u);EXPECT_EQ(fast.next_cursor,8u);
 EXPECT_EQ(journal.ReadAfter(8,output.get(),3).written,0u);
}

TEST(ObservationJournal, BodyAndHandSequencesRemainDistinct) {
 ObservationJournal journal;
 ObservationEntry entry{};entry.kind=ObservationKind::Body;entry.body_sequence=1;
 journal.Publish(entry);
 entry.kind=ObservationKind::Hand;entry.hand_sequence=1;journal.Publish(entry);
 entry.hand_sequence=2;journal.Publish(entry);
 auto output=std::make_unique<ObservationEntry[]>(3);
 const auto read=journal.ReadAfter(0,output.get(),3);
 ASSERT_EQ(read.written,3u);
 EXPECT_EQ(output[0].kind,ObservationKind::Body);EXPECT_EQ(output[0].body_sequence,1);
 EXPECT_EQ(output[1].kind,ObservationKind::Hand);EXPECT_EQ(output[1].body_sequence,1);
 EXPECT_EQ(output[1].hand_sequence,1);EXPECT_EQ(output[2].hand_sequence,2);
}

TEST(ObservationJournal, CompletionCreditBoundsAdmissionAndStopWakesProducer) {
 CompletionMailbox<int,1> mailbox;
 int notifications=0;
 mailbox.Configure(&notifications,[](void* p){++*static_cast<int*>(p);});
 mailbox.Start();ASSERT_TRUE(mailbox.Reserve());mailbox.Publish(11);
 auto blocked=std::async(std::launch::async,[&]{return mailbox.Reserve();});
 EXPECT_EQ(blocked.wait_for(std::chrono::milliseconds(25)),std::future_status::timeout);
 int value=0;ASSERT_TRUE(mailbox.TryTake(value));EXPECT_EQ(value,11);EXPECT_EQ(notifications,1);
 ASSERT_EQ(blocked.wait_for(std::chrono::seconds(1)),std::future_status::ready);
 ASSERT_TRUE(blocked.get());mailbox.Publish(12);
 auto closing=std::async(std::launch::async,[&]{return mailbox.Reserve();});
 mailbox.Stop();ASSERT_EQ(closing.wait_for(std::chrono::seconds(1)),std::future_status::ready);
 EXPECT_FALSE(closing.get());
}
