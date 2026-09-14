#include "humanvision/humanvision_v2.h"
#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include "humanvision/humanvision_android_gpu.h"
#include "composition/session.h"
TEST(AndroidGpuAbi, Exact64BitLayoutAndExportSignatures) {
#define O(T,F,N) static_assert(offsetof(T,F)==N); EXPECT_EQ(offsetof(T,F),size_t(N))
 static_assert(sizeof(HV_AndroidGpuSubmissionV1)==48);EXPECT_EQ(sizeof(HV_AndroidGpuSubmissionV1),48u);
 O(HV_AndroidGpuSubmissionV1,struct_size,0);O(HV_AndroidGpuSubmissionV1,api_version,4);O(HV_AndroidGpuSubmissionV1,unity_texture,8);O(HV_AndroidGpuSubmissionV1,width,16);O(HV_AndroidGpuSubmissionV1,height,20);O(HV_AndroidGpuSubmissionV1,frame_id,24);O(HV_AndroidGpuSubmissionV1,timestamp_us,32);O(HV_AndroidGpuSubmissionV1,rotation_degrees,40);O(HV_AndroidGpuSubmissionV1,mirrored,44);
 static_assert(sizeof(HV_AndroidGpuBridgeStatusV1)==128);EXPECT_EQ(sizeof(HV_AndroidGpuBridgeStatusV1),128u);
 O(HV_AndroidGpuBridgeStatusV1,struct_size,0);O(HV_AndroidGpuBridgeStatusV1,api_version,4);O(HV_AndroidGpuBridgeStatusV1,copy_path,8);O(HV_AndroidGpuBridgeStatusV1,ahb_format,12);O(HV_AndroidGpuBridgeStatusV1,ahb_usage,16);O(HV_AndroidGpuBridgeStatusV1,ahb_format_features,24);O(HV_AndroidGpuBridgeStatusV1,submitted_frames,32);O(HV_AndroidGpuBridgeStatusV1,imported_frames,40);O(HV_AndroidGpuBridgeStatusV1,dropped_no_slot,48);O(HV_AndroidGpuBridgeStatusV1,dropped_generation,56);O(HV_AndroidGpuBridgeStatusV1,unity_device_uuid,64);O(HV_AndroidGpuBridgeStatusV1,ncnn_device_uuid,80);O(HV_AndroidGpuBridgeStatusV1,unity_driver_uuid,96);O(HV_AndroidGpuBridgeStatusV1,ncnn_driver_uuid,112);
#undef O
 static_assert(HV_ANDROID_GPU_UUID_SIZE==16 && HV_ANDROID_GPU_COPY_UNAVAILABLE==0 && HV_ANDROID_GPU_COPY_BLIT==1 && HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT==2);
 static_assert(std::is_same_v<decltype(&HV_RuntimePrepareAndroidGpuFrame),HV_Result(HV_CALL*)(HV_RuntimeHandle,const HV_AndroidGpuSubmissionV1*,void**)>);
 static_assert(std::is_same_v<decltype(&HV_GetAndroidGpuRenderEventAndDataFunction),void*(HV_CALL*)(void)>);
 static_assert(std::is_same_v<decltype(&HV_RuntimeGetAndroidGpuBridgeStatus),HV_Result(HV_CALL*)(HV_RuntimeHandle,HV_AndroidGpuBridgeStatusV1*)>);
}
TEST(AndroidGpuAbi, UnsupportedBuildReturnsActionableErrorAndNoWork) {
 humanvision::runtime::RuntimeSession runtime;
 HV_AndroidGpuSubmissionV1 submission{sizeof(submission),1,&runtime,640,480,17,42,0,0};
 void* event=&runtime;
 EXPECT_EQ(HV_RuntimePrepareAndroidGpuFrame(&runtime,&submission,&event),HV_ANDROID_GPU_ERR_UNSUPPORTED_PLATFORM);
 EXPECT_EQ(event,nullptr);EXPECT_EQ(HV_GetAndroidGpuRenderEventAndDataFunction(),nullptr);
 char text[512]{};ASSERT_EQ(HV_RuntimeGetError(&runtime,text,sizeof(text)),HV_OK);
 EXPECT_NE(std::string(text).find("Android"),std::string::npos);EXPECT_NE(std::string(text).find("Vulkan"),std::string::npos);EXPECT_NE(std::string(text).find("rebuild"),std::string::npos);
 HV_AndroidGpuBridgeStatusV1 status;std::memset(&status,0xff,sizeof(status));status.struct_size=sizeof(status);status.api_version=1;
 EXPECT_EQ(HV_RuntimeGetAndroidGpuBridgeStatus(&runtime,&status),HV_ANDROID_GPU_ERR_UNSUPPORTED_PLATFORM);
 EXPECT_EQ(status.copy_path,HV_ANDROID_GPU_COPY_UNAVAILABLE);EXPECT_EQ(status.submitted_frames,0u);EXPECT_EQ(status.ahb_usage,0u);
 EXPECT_EQ(status.unity_device_uuid[0],0);EXPECT_EQ(status.ncnn_driver_uuid[15],0);
}
TEST(AndroidGpuAbi, RejectsInvalidArgumentsWithoutTouchingCallerStorage) {
 humanvision::runtime::RuntimeSession runtime;void* event=&runtime;
 EXPECT_EQ(HV_RuntimePrepareAndroidGpuFrame(nullptr,nullptr,&event),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(event,nullptr);
 HV_AndroidGpuSubmissionV1 submission{sizeof(submission),1,&runtime,640,480,1,1,0,0};
 for(int fault=0;fault<7;++fault){auto s=submission;switch(fault){case 0:s.struct_size=8;break;case 1:s.api_version=2;break;case 2:s.width=0;break;case 3:s.height=-1;break;case 4:s.unity_texture=nullptr;break;case 5:s.rotation_degrees=45;break;case 6:s.mirrored=2;break;}
 EXPECT_EQ(HV_RuntimePrepareAndroidGpuFrame(&runtime,&s,&event),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(event,nullptr);}
 HV_AndroidGpuBridgeStatusV1 status{8,1};auto before=status;
 EXPECT_EQ(HV_RuntimeGetAndroidGpuBridgeStatus(&runtime,&status),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(std::memcmp(&before,&status,sizeof(status)),0);
}
