#include "composition/region_assignment.h"
#include "services/body_services.h"
#include <gtest/gtest.h>

using namespace humanvision::runtime;
namespace {
HV_ObservationFrameV1 Frame() {
    HV_ObservationFrameV1 f{}; f.width=f.height=1000; f.source_frame_id=88;
    f.source_timestamp_us=1000000; return f;
}
void Body(HV_ObservationFrameV1& f, uint32_t index, float box_x, float pelvis_x,
          float pose, float detector) {
    auto& b=f.bodies[index]; b.bbox_px={box_x,100,100,400}; b.confidence=detector;
    auto& p=b.joints[HV_CANONICAL_PELVIS];p.valid=1;p.x_px=pelvis_x;p.y_px=300;
    p.x_norm=pelvis_x/1000;p.y_norm=.3F;p.confidence=pose;
    f.body_count=std::max(f.body_count,index+1);
}
}

TEST(RegionGpu, PelvisWinsOverBoxCenterAndOldRevisionCannotPublish) {
    auto f=Frame();Body(f,0,800,250,.8F,.7F);
    RegionSet regions{}; regions.count=2;regions.rects[0]={0,0,.1F,1};regions.rects[1]={.2F,0,.2F,1};
    auto out=AssignRegions(f,regions,7,{});
    ASSERT_EQ(out.body_count,1u);EXPECT_EQ(out.region_indices[0],1);
    EXPECT_EQ(out.frame.bodies[0].bbox_px.x,800);
    EXPECT_TRUE(CanPublish(out,7));EXPECT_FALSE(CanPublish(out,8));
}
TEST(RegionGpu, NoRegionsPreservesPluginBodyOrder) {
    auto f=Frame();Body(f,0,100,150,.2F,.2F);Body(f,1,800,850,.9F,.9F);
    const int32_t crop_ids[2]{17,29};
    auto out=AssignRegions(f,{},1,{},nullptr,crop_ids);
    ASSERT_EQ(out.body_count,2u);EXPECT_EQ(out.frame.bodies[0].bbox_px.x,100);
    EXPECT_EQ(out.region_indices[0],-1);EXPECT_EQ(out.crop_track_ids[0],17);
}
TEST(RegionGpu, BoxCenterOnlyWhenPelvisInvalidAndOutsideIsDiscarded) {
    auto f=Frame();Body(f,0,100,900,.8F,.7F);Body(f,1,800,850,.8F,.7F);
    f.bodies[1].joints[HV_CANONICAL_PELVIS].valid=0;
    RegionSet regions{};regions.count=1;regions.rects[0]={0,0,.3F,1};
    auto out=AssignRegions(f,regions,1,{});EXPECT_EQ(out.body_count,0u);
    f.bodies[0].joints[HV_CANONICAL_PELVIS].valid=0;
    out=AssignRegions(f,regions,1,{});ASSERT_EQ(out.body_count,1u);
    EXPECT_EQ(out.frame.bodies[0].bbox_px.x,100);
}
TEST(RegionGpu, DerivesPelvisFromBothValidHipsBeforeBoxFallback) {
    auto f=Frame();Body(f,0,800,850,.8F,.8F);
    f.bodies[0].joints[HV_CANONICAL_PELVIS].valid=0;
    for(int joint:{HV_CANONICAL_HIP_LEFT,HV_CANONICAL_HIP_RIGHT}){
        auto& hip=f.bodies[0].joints[joint];hip.valid=1;
        hip.x_px=joint==HV_CANONICAL_HIP_LEFT?220.F:280.F;
        hip.y_px=300;
    }
    RegionSet regions{};regions.count=1;regions.rects[0]={.2F,0,.2F,1};
    auto out=AssignRegions(f,regions,1,{});
    ASSERT_EQ(out.body_count,1u);EXPECT_EQ(out.region_indices[0],0);
    f.bodies[0].joints[HV_CANONICAL_HIP_RIGHT].valid=0;
    out=AssignRegions(f,regions,1,{});EXPECT_EQ(out.body_count,0u);
}
TEST(RegionGpu, CollisionUsesAnchorThenPoseDetectorAndSourceOrder) {
    RegionSet regions{};regions.count=1;regions.rects[0]={0,0,1,1};
    auto f=Frame();Body(f,0,100,150,.5F,.6F);Body(f,1,110,160,.9F,.9F);
    TrackAnchors anchors{};anchors.count=1;anchors.items[0].track_id=42;
    anchors.items[0].region_index=0;anchors.items[0].bbox_px=f.bodies[0].bbox_px;
    auto out=AssignRegions(f,regions,2,anchors);
    ASSERT_EQ(out.body_count,1u);EXPECT_EQ(out.frame.bodies[0].bbox_px.x,100);
    out=AssignRegions(f,regions,2,{});EXPECT_EQ(out.frame.bodies[0].bbox_px.x,110);
    f.bodies[0].joints[HV_CANONICAL_PELVIS].confidence=.9F;
    f.bodies[0].confidence=1.F;
    out=AssignRegions(f,regions,2,{});EXPECT_EQ(out.frame.bodies[0].bbox_px.x,100);
    f.bodies[1].confidence=1.F;
    out=AssignRegions(f,regions,2,{});EXPECT_EQ(out.frame.bodies[0].bbox_px.x,100);
    f.bodies[0].confidence=f.bodies[1].confidence=.8F;
    const float detector_scores[2]{.2F,.9F};
    out=AssignRegions(f,regions,2,{},detector_scores);
    EXPECT_EQ(out.frame.bodies[0].bbox_px.x,110);
    const float equal_scores[2]{.9F,.9F};
    out=AssignRegions(f,regions,2,{},equal_scores);
    EXPECT_EQ(out.frame.bodies[0].bbox_px.x,100);
}
TEST(RegionGpu, OverlapCanUseSecondRegionAndTrackerKeepsIdentitySeparate) {
    RegionSet regions{};regions.count=2;
    regions.rects[0]={0,0,.7F,1};regions.rects[1]={.3F,0,.7F,1};
    auto f=Frame();Body(f,0,100,400,.9F,.9F);Body(f,1,650,600,.8F,.8F);
    auto out=AssignRegions(f,regions,4,{});
    ASSERT_EQ(out.body_count,2u);EXPECT_NE(out.region_indices[0],out.region_indices[1]);
    BodyServices service;service.Configure(2,regions.rects.data(),regions.count,4);
    service.Observe(out.frame,4,false,out.region_indices.data());
    auto first=service.Raw();ASSERT_EQ(first.count,2u);
    EXPECT_NE(first.bodies[0].track_id,first.bodies[0].region_index);
    f.source_frame_id=89;f.source_timestamp_us=1033000;
    Body(f,0,110,410,.9F,.9F);Body(f,1,640,590,.8F,.8F);
    out=AssignRegions(f,regions,4,service.Anchors());
    service.Observe(out.frame,4,false,out.region_indices.data());
    auto second=service.Raw();ASSERT_EQ(second.count,2u);
    EXPECT_EQ(second.bodies[0].track_id,first.bodies[0].track_id);
    EXPECT_EQ(second.bodies[1].track_id,first.bodies[1].track_id);
}
TEST(RegionGpu, CrossingRegionsKeepsPublicTrackIdentityFromCropSidecar) {
    RegionSet regions{};regions.count=2;
    regions.rects[0]={0,0,.5F,1};regions.rects[1]={.5F,0,.5F,1};
    BodyServices service;service.Configure(2,regions.rects.data(),regions.count,5);
    auto f=Frame();Body(f,0,100,150,.8F,.8F);Body(f,1,800,850,.8F,.8F);
    const int32_t crop_ids[2]{17,29};
    auto first=AssignRegions(f,regions,5,{},nullptr,crop_ids);
    service.Observe(first.frame,5,false,first.region_indices.data(),first.crop_track_ids.data());
    auto snapshot=service.Raw();ASSERT_EQ(snapshot.count,2u);
    const auto left_id=snapshot.bodies[0].track_id,right_id=snapshot.bodies[1].track_id;
    f.source_frame_id=89;f.source_timestamp_us=1033000;
    Body(f,0,800,850,.8F,.8F);Body(f,1,100,150,.8F,.8F);
    auto crossed=AssignRegions(f,regions,5,service.Anchors(),nullptr,crop_ids);
    ASSERT_EQ(crossed.body_count,2u);
    service.Observe(crossed.frame,5,false,crossed.region_indices.data(),crossed.crop_track_ids.data());
    snapshot=service.Raw();ASSERT_EQ(snapshot.count,2u);
    EXPECT_EQ(snapshot.bodies[0].track_id,left_id);
    EXPECT_EQ(snapshot.bodies[0].region_index,1);
    EXPECT_EQ(snapshot.bodies[1].track_id,right_id);
    EXPECT_EQ(snapshot.bodies[1].region_index,0);
}
TEST(RegionGpu, TwoRegionAnchorsCannotClaimOneCandidate) {
    RegionSet regions{};regions.count=2;
    regions.rects[0]={0,0,.6F,1};regions.rects[1]={.2F,0,.8F,1};
    auto f=Frame();Body(f,0,300,400,.9F,.9F);Body(f,1,530,600,.8F,.8F);
    f.bodies[0].bbox_px.width=200;f.bodies[1].bbox_px.width=200;
    TrackAnchors anchors{};anchors.count=2;
    anchors.items[0].region_index=0;anchors.items[0].bbox_px={300,100,200,400};
    anchors.items[1].region_index=1;anchors.items[1].bbox_px={400,100,200,400};
    auto assigned=AssignRegions(f,regions,1,anchors);
    ASSERT_EQ(assigned.body_count,2u);
    EXPECT_EQ(assigned.frame.bodies[0].bbox_px.x,300);
    EXPECT_EQ(assigned.region_indices[0],0);
    EXPECT_EQ(assigned.frame.bodies[1].bbox_px.x,530);
    EXPECT_EQ(assigned.region_indices[1],1);
}
TEST(RegionGpu, NewCropIdCanReplaceOldTrackAtFullCapacity) {
    BodyServices service;service.Configure(1,nullptr,0,1);
    auto f=Frame();Body(f,0,100,150,.9F,.9F);
    const int32_t first_crop[1]{17};
    auto first=AssignRegions(f,{},1,{},nullptr,first_crop);
    service.Observe(first.frame,1,false,first.region_indices.data(),first.crop_track_ids.data());
    auto initial=service.Raw();ASSERT_EQ(initial.count,1u);
    f.source_frame_id=89;f.source_timestamp_us=1033000;
    const int32_t new_crop[1]{29};
    auto second=AssignRegions(f,{},1,service.Anchors(),nullptr,new_crop);
    service.Observe(second.frame,1,false,second.region_indices.data(),second.crop_track_ids.data());
    auto replacement=service.Raw();ASSERT_EQ(replacement.count,1u);
    EXPECT_NE(replacement.bodies[0].track_id,initial.bodies[0].track_id);
    EXPECT_EQ(replacement.bodies[0].source_frame_id,89);
}
