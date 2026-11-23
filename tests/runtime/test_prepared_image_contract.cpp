#include "common/cpu_image_service.h"
#include "common/image_transform_contract.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <limits>

using namespace humanvision::runtime;
namespace {
HV_ImageDescriptorV1 Source(const void* data,uint64_t bytes,int width=2,int height=2,int stride=8,uint32_t format=HV_IMAGE_RGBA8) {
 HV_ImageDescriptorV1 out{};out.struct_size=sizeof(out);out.api_version=1;
 out.identity={7,3,9,42,1000,5};out.width=width;out.height=height;out.pixel_format=format;
 out.plane_count=1;out.planes[0]={data,bytes,0,static_cast<uint32_t>(stride),format==HV_IMAGE_RGBA8?4u:3u};
 out.memory_kind=HV_IMAGE_MEMORY_CPU;out.completion=HV_IMAGE_COMPLETED;out.acquire_sync_fd=-1;
 out.row_origin=HV_IMAGE_TOP_LEFT;out.color_matrix=HV_IMAGE_COLOR_RGB;out.color_range=HV_IMAGE_RANGE_FULL;
 return out;
}
HV_ImageTransformPlanV1 Plan(const HV_ImageDescriptorV1& source) {return IdentityImagePlan(source,123);}
struct Service {
 CpuImageService service;HV_ImageServiceApiV1 api{};
 Service(){api.struct_size=sizeof(api);api.api_version=1;EXPECT_EQ(service.Query(1,&api),HV_OK);}
};
}
TEST(PreparedImageContract, ExplicitCompletionIsRequiredAfterCancellation) {
 PreparedLeasePool pool;uint64_t token=0;
 ASSERT_EQ(pool.Reserve(token),HV_IMAGE_READY);
 EXPECT_EQ(pool.Cancel(token),HV_IMAGE_PENDING);
 EXPECT_EQ(pool.Release(token),HV_IMAGE_BUSY); // cancellation is not hardware completion
 ASSERT_TRUE(pool.Complete(token));
 EXPECT_EQ(pool.Release(token),HV_IMAGE_READY);
 EXPECT_EQ(pool.Release(token),HV_ERR_INVALID_ARGUMENT);
}
TEST(PreparedImageContract, CpuOracleCopiesStrideAndConvertsBgrWithoutTouchingPadding) {
 const std::array<uint8_t,16> pixels{3,2,1,6,5,4,211,212,9,8,7,12,11,10,213,214};
 auto source=Source(pixels.data(),pixels.size(),2,2,8,HV_IMAGE_BGR8);auto plan=Plan(source);
 plan.output_format=HV_IMAGE_RGBA8;plan.plan_fingerprint=ImagePlanFingerprint(plan);
 Service s;HV_PreparedImageLeaseV1 lease{};lease.struct_size=sizeof(lease);lease.api_version=1;
 ASSERT_EQ(s.api.SubmitTransform(s.api.context,&source,&plan,&lease,nullptr),HV_IMAGE_READY);
 EXPECT_EQ(s.api.WaitTransform(s.api.context,&lease,0,nullptr),HV_IMAGE_READY);
 const std::array<uint8_t,16> expected{1,2,3,255,4,5,6,255,7,8,9,255,10,11,12,255};
 EXPECT_EQ(lease.image.planes[0].row_stride_bytes,8u);
 EXPECT_EQ(std::memcmp(lease.image.planes[0].data,expected.data(),expected.size()),0);
 EXPECT_EQ(lease.normalization_passes,0u);EXPECT_EQ(ValidatePreparedImage(plan,lease),HV_OK);
 EXPECT_EQ(s.api.ReleasePrepared(s.api.context,&lease,nullptr),HV_IMAGE_READY);
}
TEST(PreparedImageContract, TransformOracleRotatesAndNormalizesExactlyOnce) {
 const uint8_t pixels[]={1,2,3,4,5,6};
 auto source=Source(pixels,sizeof(pixels),2,1,6,HV_IMAGE_RGB8);auto plan=Plan(source);
 plan.output_width=1;plan.output_height=2;plan.rotation_degrees=90;plan.output_format=HV_IMAGE_RGB32F;
 plan.normalization_owner=HV_IMAGE_NORMALIZE_SERVICE;
 for(int c=0;c<3;++c){plan.mean[c]=1;plan.norm[c]=.5f;}
 plan.plan_fingerprint=ImagePlanFingerprint(plan);
 Service s;HV_PreparedImageLeaseV1 lease{sizeof(lease),1};
 ASSERT_EQ(s.api.SubmitTransform(s.api.context,&source,&plan,&lease,nullptr),HV_IMAGE_READY);
 const auto* actual=static_cast<const float*>(lease.image.planes[0].data);
 const float expected[]={0,.5f,1,1.5f,2,2.5f};
 EXPECT_EQ(std::memcmp(actual,expected,sizeof(expected)),0);EXPECT_EQ(lease.normalization_passes,1u);
 EXPECT_EQ(ValidatePreparedImage(plan,lease),HV_OK);
 EXPECT_EQ(s.api.ReleasePrepared(s.api.context,&lease,nullptr),HV_IMAGE_READY);
}
TEST(PreparedImageContract, PrivatePendingAndUnknownLayoutsNeverBecomeCpuFallback) {
 std::array<uint8_t,16> pixels{};auto source=Source(pixels.data(),pixels.size());auto plan=Plan(source);Service s;
 for(int fault=0;fault<5;++fault){auto bad=source;
  if(fault==0)bad.memory_kind=HV_IMAGE_MEMORY_OPAQUE;
  if(fault==1)bad.acquire_sync_fd=4;
  if(fault==2)bad.completion=HV_IMAGE_IN_FLIGHT;
  if(fault==3)bad.planes[0].pixel_stride_bytes=2;
  if(fault==4)bad.row_origin=99;
  EXPECT_NE(s.api.QuerySupport(s.api.context,&bad,&plan,nullptr),HV_IMAGE_READY);
 }
}
TEST(PreparedImageContract, FingerprintsEpochsAndNormalizationRejectMismatches) {
 std::array<uint8_t,16> pixels{};auto source=Source(pixels.data(),pixels.size());auto plan=Plan(source);Service s;
 HV_PreparedImageLeaseV1 lease{sizeof(lease),1};ASSERT_EQ(s.api.SubmitTransform(s.api.context,&source,&plan,&lease,nullptr),HV_IMAGE_READY);
 for(int fault=0;fault<7;++fault){auto bad=lease;
  if(fault==0)++bad.plan_fingerprint;
  if(fault==1)++bad.contract_fingerprint;
  if(fault==2)++bad.image.identity.config_epoch;
  if(fault==3)++bad.image.identity.region_revision;
  if(fault==4)++bad.normalization_passes;
  if(fault==5)bad.completion=HV_IMAGE_IN_FLIGHT;
  if(fault==6)++bad.image.identity.source_id;
  EXPECT_EQ(ValidatePreparedImage(plan,bad),HV_ERR_INVALID_ARGUMENT);
 }
 auto missing=plan;missing.normalization_owner=HV_IMAGE_NORMALIZE_NONE;missing.plan_fingerprint=ImagePlanFingerprint(missing);
 EXPECT_EQ(s.api.QuerySupport(s.api.context,&source,&missing,nullptr),HV_ERR_INVALID_ARGUMENT);
 EXPECT_EQ(s.api.ReleasePrepared(s.api.context,&lease,nullptr),HV_IMAGE_READY);
}
TEST(PreparedImageContract, ThreeLeasesStayOwnedAcrossCancelUntilExplicitRelease) {
 std::array<uint8_t,16> pixels{};auto source=Source(pixels.data(),pixels.size());auto plan=Plan(source);Service s;
 HV_PreparedImageLeaseV1 leases[4]{};for(auto& lease:leases){lease.struct_size=sizeof(lease);lease.api_version=1;}
 for(int i=0;i<3;++i)ASSERT_EQ(s.api.SubmitTransform(s.api.context,&source,&plan,&leases[i],nullptr),HV_IMAGE_READY);
 EXPECT_EQ(s.api.CancelTransform(s.api.context,&leases[0],nullptr),HV_IMAGE_CANCELLED);
 EXPECT_EQ(s.api.SubmitTransform(s.api.context,&source,&plan,&leases[3],nullptr),HV_IMAGE_BUSY);
 EXPECT_EQ(s.api.ReleasePrepared(s.api.context,&leases[0],nullptr),HV_IMAGE_READY);
 EXPECT_EQ(s.api.SubmitTransform(s.api.context,&source,&plan,&leases[3],nullptr),HV_IMAGE_READY);
 EXPECT_NE(leases[0].lease_token,leases[3].lease_token);
 for(int i=1;i<4;++i)EXPECT_EQ(s.api.ReleasePrepared(s.api.context,&leases[i],nullptr),HV_IMAGE_READY);
}
