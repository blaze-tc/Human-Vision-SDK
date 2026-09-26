#include "plugins/pipeline/simcc/gpu_track_crops.h"
#include <gtest/gtest.h>
#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<bool> count_allocations{false};
std::atomic<std::size_t> counted_allocations{0};
}

void BeginNativeAllocationProbe() noexcept {
 counted_allocations.store(0,std::memory_order_relaxed);
 count_allocations.store(true,std::memory_order_relaxed);
}
std::size_t EndNativeAllocationProbe() noexcept {
 count_allocations.store(false,std::memory_order_relaxed);
 return counted_allocations.load(std::memory_order_relaxed);
}

void* operator new(std::size_t size) {
 void* memory=std::malloc(size ? size : 1);
 if(!memory) throw std::bad_alloc();
 if(count_allocations.load(std::memory_order_relaxed))
  counted_allocations.fetch_add(1,std::memory_order_relaxed);
 return memory;
}
void* operator new[](std::size_t size) {
 void* memory=std::malloc(size ? size : 1);
 if(!memory) throw std::bad_alloc();
 if(count_allocations.load(std::memory_order_relaxed))
  counted_allocations.fetch_add(1,std::memory_order_relaxed);
 return memory;
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer,std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer,std::size_t) noexcept { std::free(pointer); }

using namespace humanvision::runtime;
using humanvision::Detection;
namespace {
Detection Box(float x, float y=20) { return {x,y,x+40,y+80,.9f}; }
std::vector<HV_CanonicalJointV1> Joints(float x,float y=40) {
 std::vector<HV_CanonicalJointV1> result(6);
 for(int i=0;i<6;++i){ result[i].valid=1; result[i].confidence=.9f; result[i].x_px=x+float(i%3)*10; result[i].y_px=y+float(i/3)*20; }
 return result;
}
DetectorResultMeta Meta(int64_t frame,int64_t time,int64_t arrival) { return {frame,time,arrival,7,3}; }
TEST(GpuTrackCrops, CurrentJointsDriveNextClippedRoi) {
 GpuTrackCrops crops(2,320,240,7,3);
 crops.ApplyDetection(Meta(80,80000,81000),{Box(240)});
 auto first=crops.NextCrops(81,81000); ASSERT_EQ(first.size(),1u); auto id=first.front().track_id;
 EXPECT_TRUE(crops.ApplyPose(81,81000,id,Joints(295),true));
 auto next=crops.NextCrops(82,82000); ASSERT_EQ(next.size(),1u);
 EXPECT_GE(next.front().center_x,300); EXPECT_LE(next.front().box.x2,320);
 EXPECT_EQ(crops.PublishedJointSourceFrame(id),-1);
}
TEST(GpuTrackCrops, DelayedDetectorNeverRewindsPose) {
 GpuTrackCrops crops(2,640,480,7,3);
 crops.ApplyDetection(Meta(75,75000,76000),{Box(250)});
 auto id=crops.NextCrops(85,85000).front().track_id;
 ASSERT_TRUE(crops.ApplyPose(85,85000,id,Joints(300),true));
 EXPECT_TRUE(crops.ApplyDetection(Meta(80,80000,86000),{Box(250)}));
 EXPECT_EQ(crops.PublishedJointSourceFrame(id),85);
 auto next=crops.NextCrops(86,86000); ASSERT_EQ(next.size(),1u);
 EXPECT_GE(next.front().center_x,300); EXPECT_EQ(next.front().track_id,id);
}
TEST(GpuTrackCrops, NewTrackRequiresFreshDetectionAndCorrectContext) {
 GpuTrackCrops crops(1,640,480,7,3);
 EXPECT_FALSE(crops.ApplyDetection(Meta(1,1000,201001),{Box(10)}));
 EXPECT_FALSE(crops.ApplyDetection({2,2000,3000,8,3},{Box(10)}));
 EXPECT_TRUE(crops.NextCrops(3,3000).empty());
 EXPECT_TRUE(crops.ApplyDetection(Meta(4,4000,204000),{Box(10)}));
 EXPECT_EQ(crops.NextCrops(5,204000).size(),1u);
}
TEST(GpuTrackCrops, FailedCurrentPoseRemovesPublicationImmediately) {
 GpuTrackCrops crops(1,640,480,7,3);
 crops.ApplyDetection(Meta(1,1000,2000),{Box(10)});
 auto id=crops.NextCrops(2,2000).front().track_id;
 ASSERT_TRUE(crops.ApplyPose(2,2000,id,Joints(20),true));
 EXPECT_EQ(crops.PublishedJointSourceFrame(id),2);
 crops.NextCrops(3,3000);
 EXPECT_FALSE(crops.ApplyPose(3,3000,id,{},false));
 EXPECT_EQ(crops.PublishedJointSourceFrame(id),-1);
 EXPECT_TRUE(crops.NeedsReacquisition());
}
TEST(GpuTrackCrops, TwoMissesOrDetectorAgeExpireTrack) {
 GpuTrackCrops crops(1,640,480,7,3);
 crops.ApplyDetection(Meta(1,1000,2000),{Box(10)});
 auto id=crops.NextCrops(2,2000).front().track_id;
 crops.ApplyDetection(Meta(3,3000,4000),{});
 EXPECT_EQ(crops.NextCrops(4,4000).size(),1u);
 crops.ApplyDetection(Meta(5,5000,6000),{});
 EXPECT_TRUE(crops.NextCrops(6,6000).empty());
 crops.ApplyDetection(Meta(7,7000,8000),{Box(10)});
 auto id2=crops.NextCrops(8,8000).front().track_id; EXPECT_NE(id,id2);
 EXPECT_TRUE(crops.NextCrops(9,507001).empty());
}
TEST(GpuTrackCrops, CrossingAndOcclusionRetainDistinctIds) {
 GpuTrackCrops crops(2,640,480,7,3);
 crops.ApplyDetection(Meta(1,1000,2000),{Box(20),Box(180)});
 auto initial=crops.NextCrops(2,2000); ASSERT_EQ(initial.size(),2u);
 auto left=initial[0].track_id,right=initial[1].track_id;
 crops.ApplyPose(2,2000,left,Joints(70),true);
 crops.ApplyPose(2,2000,right,Joints(150),true);
 crops.ApplyDetection(Meta(3,3000,4000),{Box(80),Box(140)});
 crops.ApplyDetection(Meta(4,4000,5000),{Box(120),Box(100)});
 auto crossing=crops.NextCrops(5,5000); ASSERT_EQ(crossing.size(),2u);
 EXPECT_EQ(crossing[0].track_id,left); EXPECT_EQ(crossing[1].track_id,right);
 crops.ApplyPose(5,5000,left,{},false);
 EXPECT_EQ(crops.PublishedJointSourceFrame(left),-1);
 EXPECT_EQ(crops.NextCrops(6,6000).size(),2u);
}
TEST(GpuTrackCrops, LateArrivalCannotPublishOnAlreadySelectedFrame) {
 GpuTrackCrops crops(1,640,480,7,3);
 EXPECT_TRUE(crops.NextCrops(10,10000).empty());
 ASSERT_TRUE(crops.ApplyDetection(Meta(9,9000,11000),{Box(10)}));
 auto next=crops.NextCrops(11,11000); ASSERT_EQ(next.size(),1u);
 auto id=next.front().track_id;
 EXPECT_FALSE(crops.ApplyPose(10,10000,id,Joints(20),true));
 EXPECT_EQ(crops.PublishedJointSourceFrame(id),-1);
 EXPECT_TRUE(crops.ApplyPose(11,11000,id,Joints(20),true));
}
TEST(GpuTrackCrops, GenerationInvalidationClearsTracksAndPublishedFrames) {
 GpuTrackCrops crops(1,640,480,7,3);
 crops.ApplyDetection(Meta(1,1000,2000),{Box(10)});
 auto id=crops.NextCrops(2,2000).front().track_id;
 ASSERT_TRUE(crops.ApplyPose(2,2000,id,Joints(20),true));
 crops.InvalidateGeneration();
 EXPECT_EQ(crops.PublishedJointSourceFrame(id),-1);
 EXPECT_FALSE(crops.ApplyDetection(Meta(3,3000,4000),{Box(10)}));
 EXPECT_TRUE(crops.NextCrops(4,4000).empty());
}
TEST(GpuTrackCrops, OlderResultCanCorrectKnownTrackButNotDiscoverEntrant) {
 GpuTrackCrops crops(2,640,480,7,3);
 crops.ApplyDetection(Meta(1,1000,2000),{Box(10)});
 auto id=crops.NextCrops(2,2000).front().track_id;
 ASSERT_TRUE(crops.ApplyPose(2,2000,id,Joints(20),true));
 EXPECT_TRUE(crops.ApplyDetection(Meta(3,3000,253001),{Box(18),Box(300)}));
 auto next=crops.NextCrops(4,253001);
 ASSERT_EQ(next.size(),1u);
 EXPECT_EQ(next.front().track_id,id);
}
TEST(GpuTrackCrops, NewerDetectorDuringPoseCannotCancelSelectedFrame) {
 GpuTrackCrops crops(1,640,480,7,3);
 crops.ApplyDetection(Meta(8,8000,9000),{Box(10)});
 auto id=crops.NextCrops(10,10000).front().track_id;
 ASSERT_TRUE(crops.ApplyDetection(Meta(11,11000,12000),{Box(12)}));
 EXPECT_TRUE(crops.ApplyPose(10,10000,id,Joints(20),true));
 EXPECT_EQ(crops.PublishedJointSourceFrame(id),10);
}
TEST(GpuTrackCrops, RejectsInvalidAndLowScoreDiscoveryBoxes) {
 GpuTrackCrops crops(2,640,480,7,3);
 auto weak=Box(10); weak.score=.1f;
 auto outside=Box(620);
 EXPECT_TRUE(crops.ApplyDetection(Meta(1,1000,2000),{weak,outside}));
 EXPECT_TRUE(crops.NextCrops(2,2000).empty());
}
TEST(GpuTrackCrops, WarmDetectorAssociationDoesNotAllocate) {
 GpuTrackCrops crops(2,640,480,7,3);
 const std::vector<Detection> boxes{Box(10),Box(200)};
 ASSERT_TRUE(crops.ApplyDetection(Meta(1,1000,2000),boxes));
 ASSERT_TRUE(crops.ApplyDetection(Meta(2,2000,3000),boxes));
 counted_allocations.store(0,std::memory_order_relaxed);
 count_allocations.store(true,std::memory_order_relaxed);
 for(int frame=3;frame<10;++frame)
  crops.ApplyDetection(Meta(frame,frame*1000,frame*1000+1000),boxes);
 count_allocations.store(false,std::memory_order_relaxed);
 EXPECT_EQ(counted_allocations.load(std::memory_order_relaxed),0u);
}
TEST(GpuTrackCrops, WarmEightTrackChurnDoesNotAllocate) {
 GpuTrackCrops crops(8,640,480,7,3);
 std::vector<Detection> initial,churn;
 initial.reserve(8); churn.reserve(8);
 for(int i=0;i<8;++i) initial.push_back(Box(float(i*60)));
 for(int i=0;i<7;++i) churn.push_back(initial[i]);
 churn.push_back(Box(420,200));
 ASSERT_TRUE(crops.ApplyDetection(Meta(1,1000,2000),initial));
 counted_allocations.store(0,std::memory_order_relaxed);
 count_allocations.store(true,std::memory_order_relaxed);
 for(int frame=2;frame<10;++frame)
  crops.ApplyDetection(Meta(frame,frame*1000,frame*1000+1000),churn);
 count_allocations.store(false,std::memory_order_relaxed);
 EXPECT_EQ(counted_allocations.load(std::memory_order_relaxed),0u);
 EXPECT_EQ(crops.NextCrops(10,10000).size(),8u);
}
TEST(GpuTrackCrops, DelayedCrossingMatchesDetectorTimeAnchors) {
 humanvision::CenterIouTracker tracker(1);
 std::vector<humanvision::TrackedDetection> tracked;
 tracker.Update({Box(100),Box(220)},75000,tracked);
 ASSERT_EQ(tracked.size(),2u);
 const int first=tracked[0].track_id,second=tracked[1].track_id;
 tracker.ObservePose(first,Box(180),85000);
 tracker.ObservePose(second,Box(140),85000);
 tracker.Update({Box(145),Box(175)},80000,tracked);
 ASSERT_EQ(tracked.size(),2u);
 EXPECT_EQ(tracked[0].track_id,first);
 EXPECT_EQ(tracked[1].track_id,second);
}
TEST(GpuTrackCrops, FutureSecondMissDoesNotCancelSelectedPose) {
 GpuTrackCrops crops(1,640,480,7,3);
 crops.ApplyDetection(Meta(8,8000,9000),{Box(10)});
 auto id=crops.NextCrops(9,9000).front().track_id;
 crops.ApplyDetection(Meta(9,9000,10000),{});
 ASSERT_EQ(crops.NextCrops(10,10000).size(),1u);
 crops.ApplyDetection(Meta(11,11000,12000),{});
 EXPECT_TRUE(crops.ApplyPose(10,10000,id,Joints(20),true));
 EXPECT_EQ(crops.PublishedJointSourceFrame(id),10);
 EXPECT_TRUE(crops.NextCrops(12,12000).empty());
}
TEST(GpuTrackCrops, ExpiringOneOfTwoTracksRequestsReacquisition) {
 GpuTrackCrops crops(2,640,480,7,3);
 crops.ApplyDetection(Meta(1,1000,2000),{Box(10),Box(300)});
 ASSERT_EQ(crops.NextCrops(2,2000).size(),2u);
 crops.ApplyDetection(Meta(3,3000,4000),{Box(300)});
 crops.ApplyDetection(Meta(4,4000,5000),{Box(300)});
 EXPECT_TRUE(crops.NeedsReacquisition());
 EXPECT_EQ(crops.NextCrops(5,5000).size(),1u);
}
TEST(GpuTrackCrops, BoundedIdentityMatcherNeverExceedsEightTracks) {
 humanvision::CenterIouTracker tracker(1);
 std::vector<humanvision::TrackedDetection> tracked;
 std::vector<Detection> initial,entrants;
 for(int i=0;i<8;++i){ initial.push_back(Box(float(i*60)));
  entrants.push_back(Box(float(i*60),200)); }
 tracker.UpdateBounded(initial,1000,tracked,8);
 ASSERT_EQ(tracker.ActiveTrackCount(),8u);
 tracker.UpdateBounded(entrants,2000,tracked,8);
 EXPECT_LE(tracker.ActiveTrackCount(),8u);
 tracker.UpdateBounded(entrants,3000,tracked,8);
 EXPECT_LE(tracker.ActiveTrackCount(),8u);
}
TEST(GpuTrackCrops, CurrentPoseCannotRewindNewerDetectorCorrection) {
 GpuTrackCrops crops(1,640,480,7,3);
 crops.ApplyDetection(Meta(8,8000,9000),{Box(100)});
 auto id=crops.NextCrops(10,10000).front().track_id;
 ASSERT_TRUE(crops.ApplyDetection(Meta(11,11000,12000),{Box(120)}));
 ASSERT_TRUE(crops.ApplyPose(10,10000,id,Joints(300),true));
 EXPECT_EQ(crops.PublishedJointSourceFrame(id),10);
 auto next=crops.NextCrops(12,12000); ASSERT_EQ(next.size(),1u);
 EXPECT_EQ(next.front().track_id,id);
 EXPECT_NEAR(next.front().center_x,140,1);
 ASSERT_TRUE(crops.ApplyDetection(Meta(13,13000,14000),{Box(125)}));
 auto after=crops.NextCrops(14,14000); ASSERT_EQ(after.size(),1u);
 EXPECT_EQ(after.front().track_id,id);
 EXPECT_EQ(after.front().detector_anchor_frame_id,13);
}
}
