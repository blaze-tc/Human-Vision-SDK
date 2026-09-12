#include "services/body_services.h"
#include "services/region_mask.h"
#include <gtest/gtest.h>
using namespace humanvision::runtime;
TEST(CommonServices, MaskRemovesPixelsBeforeInference){
 uint8_t pixels[12]{1,2,3,4,5,6,7,8,9,10,11,12};HV_VideoFrame frame{sizeof(frame),4,1,12,HV_PIXEL_BGR24,1,1,pixels,12};
 HV_Rect region{0,0,.5F,1};humanvision::FrameBuffer result;std::string error;
 ASSERT_TRUE(MaskRegions(frame,&region,1,result,error));EXPECT_EQ(result.bytes[3],4);EXPECT_EQ(result.bytes[6],0);EXPECT_EQ(pixels[6],7);
}
namespace {
HV_ObservationFrameV1 Frame(int64_t id,int64_t time,float x){
 HV_ObservationFrameV1 f{};f.source_frame_id=id;f.source_timestamp_us=time;f.width=f.height=1000;f.body_count=1;
 f.bodies[0].bbox_px={x,100,100,400};f.bodies[0].confidence=.9F;
 auto& j=f.bodies[0].joints[HV_CANONICAL_WRIST_LEFT];j.valid=1;j.x_px=x;j.y_px=200;j.x_norm=x/1000;j.y_norm=.2F;j.confidence=.9F;j.observation_timestamp_us=time;return f;
}
}
TEST(CommonServices, StableIdentityPreservesTimeAndRejectsOldRevision){
 BodyServices services;services.Configure(8,nullptr,0,1);
 services.Observe(Frame(1,1000000,100),1);auto first=services.Raw();ASSERT_EQ(first.count,1u);auto id=first.bodies[0].track_id;
 services.Observe(Frame(2,1033000,105),1);EXPECT_EQ(services.Raw().bodies[0].track_id,id);
 auto predicted=services.Sample(1050000);ASSERT_EQ(predicted.count,1u);EXPECT_EQ(predicted.bodies[0].observation_timestamp_us,1033000);
 EXPECT_LE(predicted.bodies[0].joints[HV_CANONICAL_WRIST_LEFT].prediction_ms,25);
 services.Configure(8,nullptr,0,2);services.Observe(Frame(3,1066000,108),1);EXPECT_EQ(services.Raw().count,0u);
}
TEST(CommonServices, RegionsExcludeOutsideAndExpireOldSamples){
 BodyServices services;HV_Rect region{0,0,.4F,1};services.Configure(1,&region,1,3);
 services.Observe(Frame(1,1000000,700),3);EXPECT_EQ(services.Raw().count,0u);
 services.Observe(Frame(2,1033000,100),3);ASSERT_EQ(services.Raw().count,1u);EXPECT_EQ(services.Raw().bodies[0].region_index,0);
 EXPECT_EQ(services.Sample(2033000).count,0u);
}
TEST(CommonServices, StartupRenderHoldKeepsBodyAcrossSlowInferenceGaps) {
 BodyServices s;s.Configure(1,nullptr,0,1);s.Observe(Frame(1,1000000,100),1);
 for(int64_t age:{0,100000,250000,400000,500000}){
  auto sample=s.Sample(1000000+age);ASSERT_EQ(sample.count,1u)<<age;
  EXPECT_TRUE(sample.bodies[0].joints[HV_CANONICAL_WRIST_LEFT].valid);
  EXPECT_EQ(sample.bodies[0].source_frame_id,1);
  EXPECT_EQ(sample.bodies[0].observation_timestamp_us,1000000);
  EXPECT_LE(sample.bodies[0].joints[HV_CANONICAL_WRIST_LEFT].prediction_ms,25.F);
 }
 EXPECT_EQ(s.Sample(1500001).count,0u);
 EXPECT_EQ(s.Raw().count,1u); // Presentation expiry never fabricates or removes a raw observation.
}
TEST(CommonServices, AdaptiveRenderHoldUsesObservationCadenceAndBoundaries) {
 BodyServices s;s.Configure(1,nullptr,0,1);
 s.Observe(Frame(1,1000000,100),1);s.Observe(Frame(2,1200000,105),1);
 EXPECT_EQ(s.Sample(1600000).count,1u);EXPECT_EQ(s.Sample(1700000).count,1u);
 EXPECT_EQ(s.Sample(1700001).count,0u); // 200 ms observation period * 2.5.
 s.Configure(1,nullptr,0,2);s.Observe(Frame(1,2000000,100),2);s.Observe(Frame(2,2033000,105),2);
 EXPECT_EQ(s.Sample(2333000).count,1u);EXPECT_EQ(s.Sample(2333001).count,0u); // Minimum 300 ms.
 s.Configure(1,nullptr,0,3);s.Observe(Frame(1,3000000,100),3);s.Observe(Frame(2,3400000,105),3);
 EXPECT_EQ(s.Sample(4200000).count,1u);EXPECT_EQ(s.Sample(4200001).count,0u); // Maximum 800 ms.
}
TEST(CommonServices, HeldBodyStopsPredictionAndRealObservationReplacesIt) {
 BodyServices s;s.Configure(1,nullptr,0,1);s.Observe(Frame(1,1000000,100),1);s.Observe(Frame(2,1200000,120),1);
 const auto filtered=s.Sample(1200000);const float x=filtered.bodies[0].joints[7].x_px;
 auto predicted=s.Sample(1225000);ASSERT_EQ(predicted.count,1u);
 EXPECT_GT(predicted.bodies[0].joints[7].x_px,x);EXPECT_FLOAT_EQ(predicted.bodies[0].joints[7].prediction_ms,25.F);
 for(int64_t age:{25001,100000,250000,400000}){
  auto held=s.Sample(1200000+age);ASSERT_EQ(held.count,1u);
  EXPECT_FLOAT_EQ(held.bodies[0].joints[7].x_px,x);EXPECT_FLOAT_EQ(held.bodies[0].joints[7].prediction_ms,0.F);
  EXPECT_EQ(held.bodies[0].source_frame_id,2);EXPECT_TRUE(held.bodies[0].joints[7].valid);
 }
 s.Observe(Frame(3,1600000,145),1);auto fresh=s.Sample(1600000);ASSERT_EQ(fresh.count,1u);
 EXPECT_EQ(fresh.bodies[0].source_frame_id,3);EXPECT_EQ(fresh.bodies[0].observation_timestamp_us,1600000);
 EXPECT_GT(fresh.bodies[0].joints[7].x_px,x);EXPECT_FLOAT_EQ(s.Raw().bodies[0].joints[7].x_px,145.F);
}
TEST(CommonServices, RenderExpiryIsIndependentFromTrackLossAndHandExpiry) {
 BodyServices s;s.Configure(1,nullptr,0,1);auto f=Frame(1,1000000,100);s.Observe(f,1);const auto id=s.Raw().bodies[0].track_id;
 HV_HandObservationV1 hand{};hand.request_id=id;hand.fingertip=f.bodies[0].joints[7];s.MergeHands(&hand,1,1);
 EXPECT_TRUE(s.Sample(1200000).bodies[0].joints[9].valid);
 auto held=s.Sample(1250000);ASSERT_EQ(held.count,1u);EXPECT_TRUE(held.bodies[0].joints[7].valid);EXPECT_FALSE(held.bodies[0].joints[9].valid);
 EXPECT_EQ(s.Sample(1500001).count,0u);
 s.Observe(Frame(2,1700000,101),1);ASSERT_EQ(s.Raw().count,1u);EXPECT_EQ(s.Raw().bodies[0].track_id,id);
 s.Observe(Frame(3,2500001,101),1);ASSERT_EQ(s.Raw().count,1u);EXPECT_NE(s.Raw().bodies[0].track_id,id);
}
TEST(CommonServices, SampleDiagnosticsReportsCadenceStateAndSeparateLifetimes) {
 BodyServices s;s.Configure(1,nullptr,0,1);auto empty=s.Diagnostics(1000000);
 EXPECT_EQ(empty.state,BodySampleState::Stale);EXPECT_EQ(empty.sample_age_ms,-1.);EXPECT_EQ(empty.render_hold_ms,500.);
 s.Observe(Frame(1,1000000,100),1);EXPECT_EQ(s.Diagnostics(1025000).state,BodySampleState::Predicted);
 EXPECT_EQ(s.Diagnostics(1025001).state,BodySampleState::Held);
 s.Observe(Frame(2,1200000,101),1);auto slow=s.Diagnostics(1600000);
 EXPECT_EQ(slow.observation_period_ewma_ms,200.);EXPECT_EQ(slow.render_hold_ms,500.);
 EXPECT_EQ(slow.sample_age_ms,400.);EXPECT_EQ(slow.sampled_body_count,1u);EXPECT_EQ(slow.tracked_body_count,1u);
 auto stale=s.Diagnostics(1700001);EXPECT_EQ(stale.state,BodySampleState::Stale);
 EXPECT_EQ(stale.sampled_body_count,0u);EXPECT_EQ(stale.tracked_body_count,1u);EXPECT_EQ(stale.track_lost_ms,800.);
 EXPECT_EQ(s.Diagnostics(2000001).tracked_body_count,0u);EXPECT_EQ(s.Raw().count,1u);
 s.Observe(Frame(3,1300000,102),1);auto faster=s.Diagnostics(1300000);
 EXPECT_EQ(faster.observation_period_ewma_ms,180.);EXPECT_EQ(faster.render_hold_ms,450.);
 s.Observe(Frame(4,1400000,102),2); // Rejected revisions must not change cadence.
 EXPECT_EQ(s.Diagnostics(1400000).observation_period_ewma_ms,180.);
 s.Configure(1,nullptr,0,2);EXPECT_EQ(s.Diagnostics(1400000).render_hold_ms,500.);
 EXPECT_EQ(s.Diagnostics(1400000).observation_period_ewma_ms,0.);
}
TEST(CommonServices, HandSchedulingVisitsBothSidesWithoutSkipping) {
 BodyServices s;s.Configure(1,nullptr,0,1);auto f=Frame(1,1000000,100);
 for(int k:{6,7,13,14}){auto& j=f.bodies[0].joints[k];j.valid=1;j.x_px=float(k*10);j.y_px=100;j.observation_timestamp_us=1000000;}
 s.Observe(f,1);HV_RegionOfInterestV1 requests[2]{};
 ASSERT_EQ(s.HandRequests(requests,2,1000100),2u);
 EXPECT_NE(requests[0].side,requests[1].side);
 EXPECT_EQ(requests[0].request_id,requests[1].request_id);
}
TEST(CommonServices, CrossingKeepsVelocityIdentityWhenDetectionOrderChanges) {
 BodyServices s;s.Configure(2,nullptr,0,1);int64_t first=0;
 for(int step=0;step<4;++step){auto f=Frame(step+1,1000000+step*100000,100+70.F*step);f.body_count=2;
  f.bodies[1]=Frame(1,f.source_timestamp_us,400-70.F*step).bodies[0];
  s.Observe(f,1);auto out=s.Raw();ASSERT_EQ(out.count,2u);
  if(!step)first=out.bodies[0].track_id;
  if(step==3){bool found=false;for(uint32_t i=0;i<out.count;++i)if(out.bodies[i].bbox_px.x>300){found=true;EXPECT_EQ(out.bodies[i].track_id,first);}EXPECT_TRUE(found);}
 }
}
TEST(CommonServices, HandsRejectOldResultsAndSkipDegenerateRois) {
 BodyServices s;s.Configure(1,nullptr,0,1);auto f=Frame(1,1000000,100);s.Observe(f,1);auto id=s.Raw().bodies[0].track_id;
 HV_HandObservationV1 hand{};hand.request_id=id;hand.side=0;
 hand.fingertip=f.bodies[0].joints[7];hand.fingertip.observation_timestamp_us=100000;
 s.MergeHands(&hand,1,1);EXPECT_FALSE(s.Raw().bodies[0].joints[9].valid);
 f=Frame(2,1033000,100);f.bodies[0].joints[6]=f.bodies[0].joints[7];s.Observe(f,1);
 HV_RegionOfInterestV1 roi[2]{};EXPECT_EQ(s.HandRequests(roi,2,1033000),0u);
}
TEST(CommonServices, AllSixteenHandsGetFairServiceAndOldExtensionsExpire) {
 BodyServices s;s.Configure(8,nullptr,0,1);auto f=Frame(1,1000000,20);f.body_count=8;
 for(int i=0;i<8;++i){f.bodies[i]=Frame(1,1000000,20.F+110*i).bodies[0];for(int k:{6,7,13,14}){auto& j=f.bodies[i].joints[k];j.valid=1;j.x_px=30.F+110*i+(k%2)*12;j.y_px=150;j.observation_timestamp_us=1000000;}}
 s.Observe(f,1);bool seen[8][2]{};HV_RegionOfInterestV1 rois[2]{};
 auto bodies=s.Raw();ASSERT_EQ(bodies.count,8u);
 for(int pass=0;pass<8;++pass){ASSERT_EQ(s.HandRequests(rois,2,1000000),2u);for(const auto& roi:rois)for(uint32_t i=0;i<bodies.count;++i)if(bodies.bodies[i].track_id==roi.request_id){EXPECT_FALSE(seen[i][roi.side]);seen[i][roi.side]=true;}}
 for(auto& pair:seen){EXPECT_TRUE(pair[0]);EXPECT_TRUE(pair[1]);}
 HV_HandObservationV1 hand{};hand.request_id=bodies.bodies[0].track_id;hand.fingertip=f.bodies[0].joints[7];
 s.MergeHands(&hand,1,1);EXPECT_TRUE(s.Raw().bodies[0].joints[9].valid);
 f.source_frame_id=2;f.source_timestamp_us=1250000;s.Observe(f,1);EXPECT_FALSE(s.Raw().bodies[0].joints[9].valid);
}
TEST(CommonServices, ShortLossRecoversButExpiredReentryGetsNewIdentity) {
 BodyServices s;s.Configure(1,nullptr,0,1);s.Observe(Frame(1,1000000,100),1);auto id=s.Raw().bodies[0].track_id;
 auto missing=Frame(2,1100000,100);missing.body_count=0;s.Observe(missing,1);EXPECT_EQ(s.Raw().count,0u);
 s.Observe(Frame(3,1150000,101),1);ASSERT_EQ(s.Raw().count,1u);EXPECT_EQ(s.Raw().bodies[0].track_id,id);
 s.Observe(Frame(4,2000000,101),1);EXPECT_NE(s.Raw().bodies[0].track_id,id);
 auto raw=s.Raw();for(int i=0;i<6;++i){auto sample=s.Sample(2000000+i*16666);ASSERT_EQ(sample.count,1u);EXPECT_EQ(sample.bodies[0].source_frame_id,4);EXPECT_LE(sample.bodies[0].joints[7].prediction_ms,25.F);}
 EXPECT_EQ(s.Raw().bodies[0].observation_timestamp_us,raw.bodies[0].observation_timestamp_us);
}
TEST(CommonServices, FastMovingHandGetsPriorityWithoutRepeatingWithinRound) {
 BodyServices s;s.Configure(2,nullptr,0,1);auto f=Frame(1,1000000,100);f.body_count=2;
 f.bodies[1]=Frame(1,1000000,500).bodies[0];
 for(int i=0;i<2;++i)for(int k:{6,7,13,14}){auto& j=f.bodies[i].joints[k];j.valid=1;j.x_px=150.F+400*i+(k%2)*20;j.y_px=200;j.observation_timestamp_us=1000000;}
 s.Observe(f,1);auto bodies=s.Raw();auto fastId=bodies.bodies[1].track_id;
 f.source_frame_id=2;f.source_timestamp_us=1033000;f.bodies[1].joints[7].x_px+=30;s.Observe(f,1);
 HV_RegionOfInterestV1 roi[1]{};ASSERT_EQ(s.HandRequests(roi,1,1033000),1u);EXPECT_EQ(roi[0].request_id,fastId);EXPECT_EQ(roi[0].side,0u);
 for(int i=0;i<3;++i){ASSERT_EQ(s.HandRequests(roi,1,1033000),1u);EXPECT_FALSE(roi[0].request_id==fastId&&roi[0].side==0);}
}

TEST(CommonServices, HandCadenceAppliesPerHandWithoutGloballyThrottlingOtherPeople) {
 BodyServices s;s.Configure(2,nullptr,0,1);auto f=Frame(1,1000000,100);f.body_count=2;f.bodies[1]=Frame(1,1000000,500).bodies[0];
 for(int i=0;i<2;++i)for(int k:{6,7,13,14}){auto& j=f.bodies[i].joints[k];j.valid=1;j.x_px=150.F+400*i+(k%2)*20;j.y_px=200;j.observation_timestamp_us=1000000;}
 s.Observe(f,1);HV_RegionOfInterestV1 rois[2]{};
 ASSERT_EQ(s.HandRequests(rois,2,1000000,66666),2u);
 ASSERT_EQ(s.HandRequests(rois,2,1000000,66666),2u);
 EXPECT_EQ(s.HandRequests(rois,2,1033000,66666),0u);
 EXPECT_EQ(s.HandRequests(rois,2,1066666,66666),2u);
}

TEST(CommonServices, TemporalFilterReducesStationaryJitterAndFollowsReversal) {
 BodyServices s;s.Configure(1,nullptr,0,1);float rawEnergy=0,sampledEnergy=0;
 for(int i=0;i<30;++i){int64_t time=1000000+i*33333;float x=100+(i%2?2.F:-2.F);s.Observe(Frame(i+1,time,x),1);
  auto sample=s.Sample(time);ASSERT_EQ(sample.count,1u);float filtered=sample.bodies[0].joints[7].x_px;
  if(i>5){rawEnergy+=(x-100)*(x-100);sampledEnergy+=(filtered-100)*(filtered-100);}
 }
 EXPECT_LT(sampledEnergy,rawEnergy);
 s.Configure(1,nullptr,0,2);
 for(int i=0;i<10;++i){int64_t time=3000000+i*33333;float x=i<5?100+10.F*i:140-10.F*(i-4);
  s.Observe(Frame(i+1,time,x),2);auto sample=s.Sample(time+16666);ASSERT_EQ(sample.count,1u);
  EXPECT_EQ(sample.bodies[0].source_frame_id,i+1);EXPECT_EQ(sample.bodies[0].observation_timestamp_us,time);
  EXPECT_NEAR(sample.bodies[0].joints[7].x_px,x,18.F);
  if(i==9)EXPECT_LT(sample.bodies[0].joints[7].x_px,110.F);
 }
}
TEST(CommonServices, RegionConstraintRetainsSlotIdentityAcrossCrossingAndOldFrames) {
 BodyServices s;HV_Rect regions[2]{{0,0,.5F,1},{.5F,0,.5F,1}};s.Configure(2,regions,2,4);
 auto f=Frame(1,1000000,380);f.body_count=2;f.bodies[1]=Frame(1,1000000,510).bodies[0];s.Observe(f,4);
 auto before=s.Raw();ASSERT_EQ(before.count,2u);
 f.source_frame_id=2;f.source_timestamp_us=1033333;std::swap(f.bodies[0],f.bodies[1]);s.Observe(f,4);
 auto after=s.Raw();ASSERT_EQ(after.count,2u);
 for(int i=0;i<2;++i){EXPECT_EQ(after.bodies[i].region_index,before.bodies[i].region_index);EXPECT_EQ(after.bodies[i].track_id,before.bodies[i].track_id);}
 s.Observe(Frame(1,1000000,600),4);EXPECT_EQ(s.Raw().bodies[0].source_frame_id,2);
}
