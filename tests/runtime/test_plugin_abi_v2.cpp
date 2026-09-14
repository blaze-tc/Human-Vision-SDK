#include "humanvision_plugin.h"
#include <cstddef>
#include <type_traits>
#include <gtest/gtest.h>

// V1 checks deliberately precede both additive headers.
static_assert(sizeof(void*) == 8, "The recorded V1 ABI contract is Windows x64.");

static_assert(HV_OK == 0);
static_assert(HV_NO_NEW_RESULT == 1);
static_assert(HV_ERR_INVALID_ARGUMENT == -1);
static_assert(HV_ERR_NOT_INITIALIZED == -2);
static_assert(HV_ERR_MODEL_LOAD == -3);
static_assert(HV_ERR_UNSUPPORTED_FORMAT == -4);
static_assert(HV_ERR_INTERNAL == -5);

static_assert(HV_BACKEND_AUTO == 0);
static_assert(HV_BACKEND_ONNX_CPU == 1);

static_assert(HV_PIXEL_RGBA32 == 1);
static_assert(HV_PIXEL_BGRA32 == 2);
static_assert(HV_PIXEL_RGB24 == 3);
static_assert(HV_PIXEL_BGR24 == 4);

static_assert(HV_JOINT_NOSE == 0);
static_assert(HV_JOINT_LEFT_EYE == 1);
static_assert(HV_JOINT_RIGHT_EYE == 2);
static_assert(HV_JOINT_LEFT_EAR == 3);
static_assert(HV_JOINT_RIGHT_EAR == 4);
static_assert(HV_JOINT_LEFT_SHOULDER == 5);
static_assert(HV_JOINT_RIGHT_SHOULDER == 6);
static_assert(HV_JOINT_LEFT_ELBOW == 7);
static_assert(HV_JOINT_RIGHT_ELBOW == 8);
static_assert(HV_JOINT_LEFT_WRIST == 9);
static_assert(HV_JOINT_RIGHT_WRIST == 10);
static_assert(HV_JOINT_LEFT_HIP == 11);
static_assert(HV_JOINT_RIGHT_HIP == 12);
static_assert(HV_JOINT_LEFT_KNEE == 13);
static_assert(HV_JOINT_RIGHT_KNEE == 14);
static_assert(HV_JOINT_LEFT_ANKLE == 15);
static_assert(HV_JOINT_RIGHT_ANKLE == 16);
static_assert(HV_JOINT_COUNT == 17);

#define HV_EXPECT_LAYOUT(type, size, alignment) \
    static_assert(sizeof(type) == size);         \
    static_assert(alignof(type) == alignment);   \
    static_assert(std::is_standard_layout_v<type>)

HV_EXPECT_LAYOUT(HV_Config, 48, 8);
static_assert(offsetof(HV_Config, struct_size) == 0);
static_assert(offsetof(HV_Config, max_bodies) == 4);
static_assert(offsetof(HV_Config, detection_threshold) == 8);
static_assert(offsetof(HV_Config, pose_threshold) == 12);
static_assert(offsetof(HV_Config, detection_interval) == 16);
static_assert(offsetof(HV_Config, enable_tracking) == 20);
static_assert(offsetof(HV_Config, backend) == 24);
static_assert(offsetof(HV_Config, detector_model_path_utf8) == 32);
static_assert(offsetof(HV_Config, pose_model_path_utf8) == 40);

HV_EXPECT_LAYOUT(HV_VideoFrame, 56, 8);
static_assert(offsetof(HV_VideoFrame, struct_size) == 0);
static_assert(offsetof(HV_VideoFrame, width) == 4);
static_assert(offsetof(HV_VideoFrame, height) == 8);
static_assert(offsetof(HV_VideoFrame, stride_bytes) == 12);
static_assert(offsetof(HV_VideoFrame, pixel_format) == 16);
static_assert(offsetof(HV_VideoFrame, frame_id) == 24);
static_assert(offsetof(HV_VideoFrame, timestamp_us) == 32);
static_assert(offsetof(HV_VideoFrame, data) == 40);
static_assert(offsetof(HV_VideoFrame, data_bytes) == 48);

HV_EXPECT_LAYOUT(HV_Joint, 24, 4);
static_assert(offsetof(HV_Joint, x_px) == 0);
static_assert(offsetof(HV_Joint, y_px) == 4);
static_assert(offsetof(HV_Joint, x_norm) == 8);
static_assert(offsetof(HV_Joint, y_norm) == 12);
static_assert(offsetof(HV_Joint, confidence) == 16);
static_assert(offsetof(HV_Joint, valid) == 20);
static_assert(offsetof(HV_Joint, reserved) == 21);

HV_EXPECT_LAYOUT(HV_Rect, 16, 4);
static_assert(offsetof(HV_Rect, x) == 0);
static_assert(offsetof(HV_Rect, y) == 4);
static_assert(offsetof(HV_Rect, width) == 8);
static_assert(offsetof(HV_Rect, height) == 12);

HV_EXPECT_LAYOUT(HV_Body, 436, 4);
static_assert(offsetof(HV_Body, struct_size) == 0);
static_assert(offsetof(HV_Body, track_id) == 4);
static_assert(offsetof(HV_Body, bbox_px) == 8);
static_assert(offsetof(HV_Body, detection_confidence) == 24);
static_assert(offsetof(HV_Body, joints) == 28);

HV_EXPECT_LAYOUT(HV_ResultMeta, 40, 8);
static_assert(offsetof(HV_ResultMeta, struct_size) == 0);
static_assert(offsetof(HV_ResultMeta, result_sequence) == 8);
static_assert(offsetof(HV_ResultMeta, source_frame_id) == 16);
static_assert(offsetof(HV_ResultMeta, source_timestamp_us) == 24);
static_assert(offsetof(HV_ResultMeta, body_count) == 32);

HV_EXPECT_LAYOUT(HV_Stats, 56, 8);
static_assert(offsetof(HV_Stats, struct_size) == 0);
static_assert(offsetof(HV_Stats, input_fps) == 4);
static_assert(offsetof(HV_Stats, inference_fps) == 8);
static_assert(offsetof(HV_Stats, detection_ms) == 12);
static_assert(offsetof(HV_Stats, pose_ms) == 16);
static_assert(offsetof(HV_Stats, tracking_ms) == 20);
static_assert(offsetof(HV_Stats, total_ms) == 24);
static_assert(offsetof(HV_Stats, submitted_frames) == 32);
static_assert(offsetof(HV_Stats, processed_frames) == 40);
static_assert(offsetof(HV_Stats, dropped_frames) == 48);


#undef HV_EXPECT_LAYOUT

#define L(T,S,A) static_assert(sizeof(T)==S && alignof(T)==A && std::is_standard_layout_v<T>); EXPECT_EQ(sizeof(T),size_t(S)); EXPECT_EQ(alignof(T),size_t(A))
#define O(T,F,N) static_assert(offsetof(T,F)==N); EXPECT_EQ(offsetof(T,F),size_t(N))
#define V(X,N) static_assert(X==N); EXPECT_EQ(X,N)
TEST(PluginAbi, FrozenV1LayoutsAndValues) {
 L(HV_ErrorBufferV1,24,8);
 O(HV_ErrorBufferV1,struct_size,0);
 O(HV_ErrorBufferV1,api_version,4);
 O(HV_ErrorBufferV1,data,8);
 O(HV_ErrorBufferV1,capacity,16);
 L(HV_TensorViewV1,104,8);
 O(HV_TensorViewV1,struct_size,0);
 O(HV_TensorViewV1,api_version,4);
 O(HV_TensorViewV1,name,8);
 O(HV_TensorViewV1,element_type,16);
 O(HV_TensorViewV1,rank,20);
 O(HV_TensorViewV1,dimensions,24);
 O(HV_TensorViewV1,data,88);
 O(HV_TensorViewV1,byte_count,96);
 L(HV_BackendConfigV1,32,8);
 O(HV_BackendConfigV1,struct_size,0);
 O(HV_BackendConfigV1,api_version,4);
 O(HV_BackendConfigV1,model_path_utf8,8);
 O(HV_BackendConfigV1,options_utf8,16);
 O(HV_BackendConfigV1,requested_provider_utf8,24);
 L(HV_BackendSessionInfoV1,656,4);
 O(HV_BackendSessionInfoV1,struct_size,0);
 O(HV_BackendSessionInfoV1,api_version,4);
 O(HV_BackendSessionInfoV1,requested,8);
 O(HV_BackendSessionInfoV1,actual,72);
 O(HV_BackendSessionInfoV1,fallback_reason,136);
 O(HV_BackendSessionInfoV1,accelerated,648);
 O(HV_BackendSessionInfoV1,reserved,652);
 L(HV_BackendApiV1,40,8);
 O(HV_BackendApiV1,struct_size,0);
 O(HV_BackendApiV1,api_version,4);
 O(HV_BackendApiV1,create,8);
 O(HV_BackendApiV1,destroy,16);
 O(HV_BackendApiV1,run,24);
 O(HV_BackendApiV1,session_info,32);
 L(HV_HostServicesV1,32,8);
 O(HV_HostServicesV1,struct_size,0);
 O(HV_HostServicesV1,api_version,4);
 O(HV_HostServicesV1,context,8);
 O(HV_HostServicesV1,create_backend,16);
 O(HV_HostServicesV1,release_backend,24);
 L(HV_PipelineConfigV1,40,8);
 O(HV_PipelineConfigV1,struct_size,0);
 O(HV_PipelineConfigV1,api_version,4);
 O(HV_PipelineConfigV1,max_bodies,8);
 O(HV_PipelineConfigV1,reserved,12);
 O(HV_PipelineConfigV1,model_manifest_utf8,16);
 O(HV_PipelineConfigV1,asset_root_utf8,24);
 O(HV_PipelineConfigV1,options_utf8,32);
 L(HV_RegionOfInterestV1,40,8);
 O(HV_RegionOfInterestV1,struct_size,0);
 O(HV_RegionOfInterestV1,api_version,4);
 O(HV_RegionOfInterestV1,request_id,8);
 O(HV_RegionOfInterestV1,bbox_px,16);
 O(HV_RegionOfInterestV1,side,32);
 O(HV_RegionOfInterestV1,reserved,36);
 L(HV_PipelineInputV1,80,8);
 O(HV_PipelineInputV1,struct_size,0);
 O(HV_PipelineInputV1,api_version,4);
 O(HV_PipelineInputV1,frame,8);
 O(HV_PipelineInputV1,rois,64);
 O(HV_PipelineInputV1,roi_count,72);
 O(HV_PipelineInputV1,reserved,76);
 L(HV_BodyObservationV1,1568,8);
 O(HV_BodyObservationV1,struct_size,0);
 O(HV_BodyObservationV1,api_version,4);
 O(HV_BodyObservationV1,bbox_px,8);
 O(HV_BodyObservationV1,confidence,24);
 O(HV_BodyObservationV1,reserved,28);
 O(HV_BodyObservationV1,joints,32);
 L(HV_HandObservationV1,168,8);
 O(HV_HandObservationV1,struct_size,0);
 O(HV_HandObservationV1,api_version,4);
 O(HV_HandObservationV1,request_id,8);
 O(HV_HandObservationV1,side,16);
 O(HV_HandObservationV1,reserved,20);
 O(HV_HandObservationV1,palm,24);
 O(HV_HandObservationV1,fingertip,72);
 O(HV_HandObservationV1,thumb,120);
 L(HV_PipelineOutputV1,56,8);
 O(HV_PipelineOutputV1,struct_size,0);
 O(HV_PipelineOutputV1,api_version,4);
 O(HV_PipelineOutputV1,bodies,8);
 O(HV_PipelineOutputV1,body_capacity,16);
 O(HV_PipelineOutputV1,body_count,20);
 O(HV_PipelineOutputV1,hands,24);
 O(HV_PipelineOutputV1,hand_capacity,32);
 O(HV_PipelineOutputV1,hand_count,36);
 O(HV_PipelineOutputV1,preprocess_ms,40);
 O(HV_PipelineOutputV1,inference_ms,44);
 O(HV_PipelineOutputV1,postprocess_ms,48);
 L(HV_PipelineApiV1,32,8);
 O(HV_PipelineApiV1,struct_size,0);
 O(HV_PipelineApiV1,api_version,4);
 O(HV_PipelineApiV1,create,8);
 O(HV_PipelineApiV1,destroy,16);
 O(HV_PipelineApiV1,process,24);
 L(HV_PluginApiV1,72,8);
 O(HV_PluginApiV1,struct_size,0);
 O(HV_PluginApiV1,api_version,4);
 O(HV_PluginApiV1,plugin_id,8);
 O(HV_PluginApiV1,plugin_version,16);
 O(HV_PluginApiV1,type,24);
 O(HV_PluginApiV1,capabilities,32);
 O(HV_PluginApiV1,max_people,40);
 O(HV_PluginApiV1,pipeline,48);
 O(HV_PluginApiV1,backend,56);
 O(HV_PluginApiV1,priority,64);
 L(HV_ObservationFrameV1,15296,8);
 O(HV_ObservationFrameV1,struct_size,0);
 O(HV_ObservationFrameV1,api_version,4);
 O(HV_ObservationFrameV1,sequence,8);
 O(HV_ObservationFrameV1,source_frame_id,16);
 O(HV_ObservationFrameV1,source_timestamp_us,24);
 O(HV_ObservationFrameV1,width,32);
 O(HV_ObservationFrameV1,height,36);
 O(HV_ObservationFrameV1,body_count,40);
 O(HV_ObservationFrameV1,hand_count,44);
 O(HV_ObservationFrameV1,bodies,48);
 O(HV_ObservationFrameV1,hands,12592);
 O(HV_ObservationFrameV1,preprocess_ms,15280);
 O(HV_ObservationFrameV1,inference_ms,15284);
 O(HV_ObservationFrameV1,postprocess_ms,15288);
 L(HV_CanonicalJointV1,48,8);
 O(HV_CanonicalJointV1,struct_size,0);
 O(HV_CanonicalJointV1,api_version,4);
 O(HV_CanonicalJointV1,x_px,8);
 O(HV_CanonicalJointV1,y_px,12);
 O(HV_CanonicalJointV1,x_norm,16);
 O(HV_CanonicalJointV1,y_norm,20);
 O(HV_CanonicalJointV1,confidence,24);
 O(HV_CanonicalJointV1,valid,28);
 O(HV_CanonicalJointV1,derived,29);
 O(HV_CanonicalJointV1,reserved,30);
 O(HV_CanonicalJointV1,observation_timestamp_us,32);
 O(HV_CanonicalJointV1,prediction_ms,40);
 O(HV_CanonicalJointV1,reserved2,44);
 L(HV_CanonicalBodyV1,1608,8);
 O(HV_CanonicalBodyV1,struct_size,0);
 O(HV_CanonicalBodyV1,api_version,4);
 O(HV_CanonicalBodyV1,track_id,8);
 O(HV_CanonicalBodyV1,region_index,16);
 O(HV_CanonicalBodyV1,lifecycle,20);
 O(HV_CanonicalBodyV1,region_revision,24);
 O(HV_CanonicalBodyV1,source_frame_id,32);
 O(HV_CanonicalBodyV1,observation_timestamp_us,40);
 O(HV_CanonicalBodyV1,bbox_px,48);
 O(HV_CanonicalBodyV1,confidence,64);
 O(HV_CanonicalBodyV1,reserved,68);
 O(HV_CanonicalBodyV1,joints,72);
 L(HV_RuntimeConfigV1,32,8);
 O(HV_RuntimeConfigV1,struct_size,0);
 O(HV_RuntimeConfigV1,api_version,4);
 O(HV_RuntimeConfigV1,runtime_root_utf8,8);
 O(HV_RuntimeConfigV1,profile_id_utf8,16);
 O(HV_RuntimeConfigV1,max_people,24);
 O(HV_RuntimeConfigV1,reserved,28);
 L(HV_RuntimeStatsV1,80,8);
 O(HV_RuntimeStatsV1,struct_size,0);
 O(HV_RuntimeStatsV1,api_version,4);
 O(HV_RuntimeStatsV1,body_sequence,8);
 O(HV_RuntimeStatsV1,hand_sequence,16);
 O(HV_RuntimeStatsV1,source_frame_id,24);
 O(HV_RuntimeStatsV1,source_timestamp_us,32);
 O(HV_RuntimeStatsV1,region_revision,40);
 O(HV_RuntimeStatsV1,dropped_frames,48);
 O(HV_RuntimeStatsV1,body_fps,56);
 O(HV_RuntimeStatsV1,hand_fps,60);
 O(HV_RuntimeStatsV1,preprocess_ms,64);
 O(HV_RuntimeStatsV1,inference_ms,68);
 O(HV_RuntimeStatsV1,postprocess_ms,72);
 O(HV_RuntimeStatsV1,reserved,76);
 V(HV_CANONICAL_PELVIS,0);
 V(HV_CANONICAL_SPINE_NAVEL,1);
 V(HV_CANONICAL_SPINE_CHEST,2);
 V(HV_CANONICAL_NECK,3);
 V(HV_CANONICAL_CLAVICLE_LEFT,4);
 V(HV_CANONICAL_SHOULDER_LEFT,5);
 V(HV_CANONICAL_ELBOW_LEFT,6);
 V(HV_CANONICAL_WRIST_LEFT,7);
 V(HV_CANONICAL_HAND_LEFT,8);
 V(HV_CANONICAL_HANDTIP_LEFT,9);
 V(HV_CANONICAL_THUMB_LEFT,10);
 V(HV_CANONICAL_CLAVICLE_RIGHT,11);
 V(HV_CANONICAL_SHOULDER_RIGHT,12);
 V(HV_CANONICAL_ELBOW_RIGHT,13);
 V(HV_CANONICAL_WRIST_RIGHT,14);
 V(HV_CANONICAL_HAND_RIGHT,15);
 V(HV_CANONICAL_HANDTIP_RIGHT,16);
 V(HV_CANONICAL_THUMB_RIGHT,17);
 V(HV_CANONICAL_HIP_LEFT,18);
 V(HV_CANONICAL_KNEE_LEFT,19);
 V(HV_CANONICAL_ANKLE_LEFT,20);
 V(HV_CANONICAL_FOOT_LEFT,21);
 V(HV_CANONICAL_HIP_RIGHT,22);
 V(HV_CANONICAL_KNEE_RIGHT,23);
 V(HV_CANONICAL_ANKLE_RIGHT,24);
 V(HV_CANONICAL_FOOT_RIGHT,25);
 V(HV_CANONICAL_HEAD,26);
 V(HV_CANONICAL_NOSE,27);
 V(HV_CANONICAL_EYE_LEFT,28);
 V(HV_CANONICAL_EAR_LEFT,29);
 V(HV_CANONICAL_EYE_RIGHT,30);
 V(HV_CANONICAL_EAR_RIGHT,31);
 V(HV_PLUGIN_API_V1,1u); V(HV_PLUGIN_PIPELINE,1u); V(HV_PLUGIN_BACKEND,2u);
 V(HV_CAP_BODY_POSE,1ull);
 V(HV_CAP_HAND_POSE,2ull);
 V(HV_CAP_MULTI_PERSON,4ull);
 V(HV_CAP_TENSOR_INFERENCE,8ull);
 V(HV_CAP_DYNAMIC_INPUT,16ull);
 V(HV_CAP_BATCH,32ull);
 V(HV_CAP_GPU_INPUT,64ull);
 V(HV_CAP_VULKAN,128ull);
 V(HV_CAP_FP16_STORAGE,256ull);
 V(HV_CAP_FP16_ARITHMETIC,512ull);
 V(HV_CAP_ANDROID_HARDWARE_BUFFER,1024ull);
 V(HV_CAP_EXTERNAL_SYNC_FD,2048ull);
}
static_assert(std::is_same_v<HV_QueryPluginFn, HV_Result(HV_CALL*)(uint32_t,HV_PluginApiV1*)>);
static_assert(std::is_same_v<decltype(&HV_QueryPlugin),HV_QueryPluginFn>);
TEST(PluginAbi, ExistingNativeExportsRemainLinkStable) {
#define S(name) { auto volatile address=&name; EXPECT_NE(address,nullptr); }
 S(HV_GetVersionString);
 S(HV_Create);
 S(HV_Reconfigure);
 S(HV_SubmitFrame);
 S(HV_GetLatestResultMeta);
 S(HV_GetBodyCount);
 S(HV_GetBodies);
 S(HV_GetHandJoints);
 S(HV_GetStats);
 S(HV_SetRegions);
 S(HV_GetRegionAssignments);
 S(HV_GetLastError);
 S(HV_Destroy);
 S(HV_RuntimeClockUs);
 S(HV_RuntimeCreate);
 S(HV_RuntimeSubmit);
 S(HV_RuntimeSetRegions);
 S(HV_RuntimeCopy);
 S(HV_RuntimeGetError);
 S(HV_RuntimeGetDiagnostics);
 S(HV_RuntimeDestroy);
#undef S
}

TEST(PluginAbi, LegacyPublicV1RuntimeLayoutsAndEnums) {
 EXPECT_EQ(sizeof(HV_Config),48u); EXPECT_EQ(alignof(HV_Config),8u);
 EXPECT_EQ(sizeof(HV_VideoFrame),56u); EXPECT_EQ(alignof(HV_VideoFrame),8u);
 EXPECT_EQ(sizeof(HV_Joint),24u); EXPECT_EQ(alignof(HV_Joint),4u);
 EXPECT_EQ(sizeof(HV_Rect),16u); EXPECT_EQ(alignof(HV_Rect),4u);
 EXPECT_EQ(sizeof(HV_Body),436u); EXPECT_EQ(alignof(HV_Body),4u);
 EXPECT_EQ(sizeof(HV_ResultMeta),40u); EXPECT_EQ(alignof(HV_ResultMeta),8u);
 EXPECT_EQ(sizeof(HV_Stats),56u); EXPECT_EQ(alignof(HV_Stats),8u);
 EXPECT_TRUE(HV_OK == 0);
 EXPECT_TRUE(HV_NO_NEW_RESULT == 1);
 EXPECT_TRUE(HV_ERR_INVALID_ARGUMENT == -1);
 EXPECT_TRUE(HV_ERR_NOT_INITIALIZED == -2);
 EXPECT_TRUE(HV_ERR_MODEL_LOAD == -3);
 EXPECT_TRUE(HV_ERR_UNSUPPORTED_FORMAT == -4);
 EXPECT_TRUE(HV_ERR_INTERNAL == -5);
 EXPECT_TRUE(HV_BACKEND_AUTO == 0);
 EXPECT_TRUE(HV_BACKEND_ONNX_CPU == 1);
 EXPECT_TRUE(HV_PIXEL_RGBA32 == 1);
 EXPECT_TRUE(HV_PIXEL_BGRA32 == 2);
 EXPECT_TRUE(HV_PIXEL_RGB24 == 3);
 EXPECT_TRUE(HV_PIXEL_BGR24 == 4);
 EXPECT_TRUE(HV_JOINT_NOSE == 0);
 EXPECT_TRUE(HV_JOINT_LEFT_EYE == 1);
 EXPECT_TRUE(HV_JOINT_RIGHT_EYE == 2);
 EXPECT_TRUE(HV_JOINT_LEFT_EAR == 3);
 EXPECT_TRUE(HV_JOINT_RIGHT_EAR == 4);
 EXPECT_TRUE(HV_JOINT_LEFT_SHOULDER == 5);
 EXPECT_TRUE(HV_JOINT_RIGHT_SHOULDER == 6);
 EXPECT_TRUE(HV_JOINT_LEFT_ELBOW == 7);
 EXPECT_TRUE(HV_JOINT_RIGHT_ELBOW == 8);
 EXPECT_TRUE(HV_JOINT_LEFT_WRIST == 9);
 EXPECT_TRUE(HV_JOINT_RIGHT_WRIST == 10);
 EXPECT_TRUE(HV_JOINT_LEFT_HIP == 11);
 EXPECT_TRUE(HV_JOINT_RIGHT_HIP == 12);
 EXPECT_TRUE(HV_JOINT_LEFT_KNEE == 13);
 EXPECT_TRUE(HV_JOINT_RIGHT_KNEE == 14);
 EXPECT_TRUE(HV_JOINT_LEFT_ANKLE == 15);
 EXPECT_TRUE(HV_JOINT_RIGHT_ANKLE == 16);
 EXPECT_TRUE(HV_JOINT_COUNT == 17);
 EXPECT_TRUE(offsetof(HV_Config, struct_size) == 0);
 EXPECT_TRUE(offsetof(HV_Config, max_bodies) == 4);
 EXPECT_TRUE(offsetof(HV_Config, detection_threshold) == 8);
 EXPECT_TRUE(offsetof(HV_Config, pose_threshold) == 12);
 EXPECT_TRUE(offsetof(HV_Config, detection_interval) == 16);
 EXPECT_TRUE(offsetof(HV_Config, enable_tracking) == 20);
 EXPECT_TRUE(offsetof(HV_Config, backend) == 24);
 EXPECT_TRUE(offsetof(HV_Config, detector_model_path_utf8) == 32);
 EXPECT_TRUE(offsetof(HV_Config, pose_model_path_utf8) == 40);
 EXPECT_TRUE(offsetof(HV_VideoFrame, struct_size) == 0);
 EXPECT_TRUE(offsetof(HV_VideoFrame, width) == 4);
 EXPECT_TRUE(offsetof(HV_VideoFrame, height) == 8);
 EXPECT_TRUE(offsetof(HV_VideoFrame, stride_bytes) == 12);
 EXPECT_TRUE(offsetof(HV_VideoFrame, pixel_format) == 16);
 EXPECT_TRUE(offsetof(HV_VideoFrame, frame_id) == 24);
 EXPECT_TRUE(offsetof(HV_VideoFrame, timestamp_us) == 32);
 EXPECT_TRUE(offsetof(HV_VideoFrame, data) == 40);
 EXPECT_TRUE(offsetof(HV_VideoFrame, data_bytes) == 48);
 EXPECT_TRUE(offsetof(HV_Joint, x_px) == 0);
 EXPECT_TRUE(offsetof(HV_Joint, y_px) == 4);
 EXPECT_TRUE(offsetof(HV_Joint, x_norm) == 8);
 EXPECT_TRUE(offsetof(HV_Joint, y_norm) == 12);
 EXPECT_TRUE(offsetof(HV_Joint, confidence) == 16);
 EXPECT_TRUE(offsetof(HV_Joint, valid) == 20);
 EXPECT_TRUE(offsetof(HV_Joint, reserved) == 21);
 EXPECT_TRUE(offsetof(HV_Rect, x) == 0);
 EXPECT_TRUE(offsetof(HV_Rect, y) == 4);
 EXPECT_TRUE(offsetof(HV_Rect, width) == 8);
 EXPECT_TRUE(offsetof(HV_Rect, height) == 12);
 EXPECT_TRUE(offsetof(HV_Body, struct_size) == 0);
 EXPECT_TRUE(offsetof(HV_Body, track_id) == 4);
 EXPECT_TRUE(offsetof(HV_Body, bbox_px) == 8);
 EXPECT_TRUE(offsetof(HV_Body, detection_confidence) == 24);
 EXPECT_TRUE(offsetof(HV_Body, joints) == 28);
 EXPECT_TRUE(offsetof(HV_ResultMeta, struct_size) == 0);
 EXPECT_TRUE(offsetof(HV_ResultMeta, result_sequence) == 8);
 EXPECT_TRUE(offsetof(HV_ResultMeta, source_frame_id) == 16);
 EXPECT_TRUE(offsetof(HV_ResultMeta, source_timestamp_us) == 24);
 EXPECT_TRUE(offsetof(HV_ResultMeta, body_count) == 32);
 EXPECT_TRUE(offsetof(HV_Stats, struct_size) == 0);
 EXPECT_TRUE(offsetof(HV_Stats, input_fps) == 4);
 EXPECT_TRUE(offsetof(HV_Stats, inference_fps) == 8);
 EXPECT_TRUE(offsetof(HV_Stats, detection_ms) == 12);
 EXPECT_TRUE(offsetof(HV_Stats, pose_ms) == 16);
 EXPECT_TRUE(offsetof(HV_Stats, tracking_ms) == 20);
 EXPECT_TRUE(offsetof(HV_Stats, total_ms) == 24);
 EXPECT_TRUE(offsetof(HV_Stats, submitted_frames) == 32);
 EXPECT_TRUE(offsetof(HV_Stats, processed_frames) == 40);
 EXPECT_TRUE(offsetof(HV_Stats, dropped_frames) == 48);
 static_assert(sizeof(HV_Result)==4); EXPECT_EQ(sizeof(HV_Result),4u);
 static_assert(sizeof(HV_Backend)==4); EXPECT_EQ(sizeof(HV_Backend),4u);
 static_assert(sizeof(HV_PixelFormat)==4); EXPECT_EQ(sizeof(HV_PixelFormat),4u);
 static_assert(sizeof(HV_JointType)==4); EXPECT_EQ(sizeof(HV_JointType),4u);
 static_assert(sizeof(HV_CanonicalJointId)==4); EXPECT_EQ(sizeof(HV_CanonicalJointId),4u);
}

#include "humanvision_plugin_v2.h"
#include "host/backend_factory.h"
#include "plugins/backend/ort/ort_plugin.h"
#include <cstring>
TEST(PluginAbi, V2PrefixAndGpuLayouts) {
 L(HV_GpuFrameRefV1,56,8); O(HV_GpuFrameRefV1,struct_size,0); O(HV_GpuFrameRefV1,api_version,4); O(HV_GpuFrameRefV1,opaque_slot,8); O(HV_GpuFrameRefV1,width,16); O(HV_GpuFrameRefV1,height,20); O(HV_GpuFrameRefV1,frame_id,24); O(HV_GpuFrameRefV1,timestamp_us,32); O(HV_GpuFrameRefV1,generation,40); O(HV_GpuFrameRefV1,image_format,48); O(HV_GpuFrameRefV1,flags,52);
 L(HV_GpuImageTransformV1,76,4); O(HV_GpuImageTransformV1,struct_size,0); O(HV_GpuImageTransformV1,api_version,4); O(HV_GpuImageTransformV1,source_rect_px,8); O(HV_GpuImageTransformV1,output_width,24); O(HV_GpuImageTransformV1,output_height,28); O(HV_GpuImageTransformV1,output_type,32); O(HV_GpuImageTransformV1,output_elempack,36); O(HV_GpuImageTransformV1,channel_order,40); O(HV_GpuImageTransformV1,mean,44); O(HV_GpuImageTransformV1,norm,60);
 L(HV_GpuDeviceContextV1,56,8); O(HV_GpuDeviceContextV1,struct_size,0); O(HV_GpuDeviceContextV1,api_version,4); O(HV_GpuDeviceContextV1,host_context,8); O(HV_GpuDeviceContextV1,device_uuid,16); O(HV_GpuDeviceContextV1,driver_uuid,32); O(HV_GpuDeviceContextV1,graphics_queue_family,48); O(HV_GpuDeviceContextV1,reserved,52);
 L(HV_GpuBackendConfigV1,32,8); O(HV_GpuBackendConfigV1,struct_size,0); O(HV_GpuBackendConfigV1,api_version,4); O(HV_GpuBackendConfigV1,model_manifest_utf8,8); O(HV_GpuBackendConfigV1,asset_root_utf8,16); O(HV_GpuBackendConfigV1,requested_provider_utf8,24);
 L(HV_GpuBackendApiV1,40,8); O(HV_GpuBackendApiV1,struct_size,0); O(HV_GpuBackendApiV1,api_version,4); O(HV_GpuBackendApiV1,create,8); O(HV_GpuBackendApiV1,destroy,16); O(HV_GpuBackendApiV1,run_image,24); O(HV_GpuBackendApiV1,session_info,32);
 L(HV_HostServicesV2,48,8); O(HV_HostServicesV2,v1,0); O(HV_HostServicesV2,create_gpu_backend,32); O(HV_HostServicesV2,release_gpu_backend,40);
 L(HV_GpuPipelineApiV1,32,8); O(HV_GpuPipelineApiV1,struct_size,0); O(HV_GpuPipelineApiV1,api_version,4); O(HV_GpuPipelineApiV1,create,8); O(HV_GpuPipelineApiV1,destroy,16); O(HV_GpuPipelineApiV1,process_gpu,24);
 L(HV_PluginApiV2,88,8); O(HV_PluginApiV2,v1,0); O(HV_PluginApiV2,gpu_backend,72); O(HV_PluginApiV2,gpu_pipeline,80);
 V(HV_PLUGIN_API_V2,2u); V(HV_GPU_FRAME_API_V1,1u); V(HV_GPU_IMAGE_RGBA8_UNORM,1); V(HV_GPU_IMAGE_BGRA8_UNORM,2); V(HV_GPU_TENSOR_FP32,1); V(HV_GPU_TENSOR_FP16,2);
 static_assert(std::is_same_v<HV_QueryPluginV2Fn,HV_Result(HV_CALL*)(uint32_t,HV_PluginApiV2*)>);
 static_assert(std::is_same_v<decltype(&HV_QueryPluginV2),HV_QueryPluginV2Fn>);
}
namespace {
int created=0, destroyed=0;
bool reject_create=false, throw_create=false;
HV_Result HV_CALL GpuCreate(const HV_GpuBackendConfigV1* c,const HV_GpuDeviceContextV1* d,void** p,HV_ErrorBufferV1*) {
 ++created; *p=new int(17);
 if(throw_create)throw std::runtime_error("fixture creation error");
 if(reject_create)return HV_ERR_MODEL_LOAD;
 return c&&d&&d->host_context&&std::strcmp(c->requested_provider_utf8,"fixture.gpu")==0?HV_OK:HV_ERR_INVALID_ARGUMENT;
}
void HV_CALL GpuDestroy(void* p){++destroyed;delete static_cast<int*>(p);}
HV_Result HV_CALL GpuRun(void* p,const HV_GpuFrameRefV1* f,const HV_GpuImageTransformV1* t,HV_TensorViewV1* o,uint32_t cap,uint32_t* n,HV_ErrorBufferV1*) {
 if(!p||!f||!t||!o||!cap||!n)return HV_ERR_INVALID_ARGUMENT;
 if(f->flags)throw std::runtime_error("fixture run error");
 *n=1;o[0]={sizeof(*o),1,"value",2,1,{1},&f->frame_id,sizeof(f->frame_id)};return HV_OK;
}
HV_Result HV_CALL GpuInfo(void* p,HV_BackendSessionInfoV1* info){if(!p||!info)return HV_ERR_INVALID_ARGUMENT;std::memcpy(info->actual,"fixture GPU",sizeof("fixture GPU"));return HV_OK;}
HV_Result HV_CALL PipelineCreate(const HV_PipelineConfigV1*,const HV_HostServicesV2*,void** p,HV_ErrorBufferV1*){*p=new int(1);return HV_OK;}
HV_Result HV_CALL PipelineProcess(void*,const HV_GpuFrameRefV1* f,HV_ObservationFrameV1* o,HV_ErrorBufferV1*){o->source_frame_id=f->frame_id;return HV_OK;}
HV_GpuBackendApiV1 gpu{sizeof(gpu),1,GpuCreate,GpuDestroy,GpuRun,GpuInfo};
HV_GpuPipelineApiV1 pipeline{sizeof(pipeline),1,PipelineCreate,GpuDestroy,PipelineProcess};
HV_PluginApiV2 offered{};
HV_Result HV_CALL QueryV2(uint32_t version,HV_PluginApiV2* out){
 if(version!=2||!out||out->v1.struct_size!=88||out->v1.api_version!=2)return HV_ERR_INVALID_ARGUMENT;
 *out=offered;return HV_OK;
}
HV_Result HV_CALL QueryThrows(uint32_t,HV_PluginApiV2*){throw std::runtime_error("query");}
void Reset(bool backend=true,bool pipe=false){
 offered={{sizeof(offered),2,"fixture.gpu","1.0",backend?HV_PLUGIN_BACKEND:HV_PLUGIN_PIPELINE,
 HV_CAP_GPU_INPUT|(backend?HV_CAP_TENSOR_INFERENCE:0)|(pipe?HV_CAP_BODY_POSE:0),pipe?8u:0u,nullptr,nullptr,0},backend?&gpu:nullptr,pipe?&pipeline:nullptr};
 created=destroyed=0;reject_create=throw_create=false;
}
}
TEST(PluginAbi, V1OnlyPluginStillRegistersAndLoads) {
 using namespace humanvision::runtime;
 PluginRegistry registry;std::string error;
 ASSERT_TRUE(registry.Register(HV_QueryOrtCpuPlugin,error))<<error;
 auto module=registry.Find("backend.ort.cpu",HV_CAP_TENSOR_INFERENCE,error);ASSERT_NE(module,nullptr);
 BackendFactory factory({module});auto services=factory.ServicesV2();
 EXPECT_EQ(services.v1.api_version,HV_PLUGIN_API_V1); // V1 callbacks retain their original header contract.
 const HV_BackendApiV1* api=nullptr;void* session=nullptr;
 HV_BackendConfigV1 config{sizeof(config),1,HV_TEST_BACKEND_MODEL_PATH,nullptr,nullptr};
 ASSERT_EQ(services.v1.create_backend(services.v1.context,&config,&api,&session,nullptr),HV_OK);
 HV_BackendSessionInfoV1 info{sizeof(info),1};EXPECT_EQ(api->session_info(session,&info),HV_OK);EXPECT_STREQ(info.actual,"CPU");
 services.v1.release_backend(nullptr,api,session);
}
TEST(PluginAbi, RegistersBackendPipelineAndCombinedTables) {
 using namespace humanvision::runtime;
 for(auto flags: {1,2,3}){Reset((flags&1)!=0,(flags&2)!=0);BackendFactory factory({});std::string error;
  ASSERT_TRUE(factory.RegisterV2(QueryV2,error))<<error;
  auto module=factory.FindV2("fixture.gpu",HV_CAP_GPU_INPUT,error);ASSERT_NE(module,nullptr);
  EXPECT_EQ(module->api.gpu_backend!=nullptr,(flags&1)!=0);EXPECT_EQ(module->api.gpu_pipeline!=nullptr,(flags&2)!=0);
  EXPECT_EQ(factory.FindV2("fixture.gpu",HV_CAP_HAND_POSE,error),nullptr);EXPECT_NE(error.find("capabilit"),std::string::npos);
 }
}
TEST(PluginAbi, RejectsMalformedMetadataTablesAndDuplicateIds) {
 using namespace humanvision::runtime;
 for(int fault=0;fault<15;++fault){Reset(true,true);BackendFactory factory({});std::string error;
  HV_GpuBackendApiV1 bad_gpu=gpu;HV_GpuPipelineApiV1 bad_pipe=pipeline;
  offered.gpu_backend=&bad_gpu;offered.gpu_pipeline=&bad_pipe;
  switch(fault){case 0:offered.v1.struct_size=72;break;case 1:offered.v1.api_version=1;break;case 2:offered.v1.plugin_id="";break;
  case 3:offered.gpu_backend=nullptr;offered.gpu_pipeline=nullptr;break;case 4:offered.v1.capabilities=0;break;
  case 5:bad_gpu.struct_size=8;break;case 6:bad_gpu.api_version=2;break;case 7:bad_gpu.create=nullptr;break;
  case 8:bad_gpu.run_image=nullptr;break;case 9:bad_gpu.destroy=nullptr;break;case 10:bad_gpu.session_info=nullptr;break;
  case 11:bad_pipe.process_gpu=nullptr;break;case 12:bad_pipe.api_version=2;break;case 13:offered.v1.max_people=0;break;case 14:offered.v1.type=99;break;}
  EXPECT_FALSE(factory.RegisterV2(QueryV2,error))<<fault;EXPECT_FALSE(error.empty());
 }
 Reset();BackendFactory factory({});std::string error;
 EXPECT_FALSE(factory.RegisterV2(nullptr,error));EXPECT_FALSE(factory.RegisterV2(QueryThrows,error));
 ASSERT_TRUE(factory.RegisterV2(QueryV2,error));EXPECT_FALSE(factory.RegisterV2(QueryV2,error));EXPECT_NE(error.find("Duplicate"),std::string::npos);
}
TEST(PluginAbi, GpuLeasePreservesModuleAndForwardsFrameWithoutV1Fallback) {
 using namespace humanvision::runtime;Reset();
 auto owner=std::make_shared<int>(42);std::weak_ptr<int> weak=owner;
 const HV_GpuBackendApiV1* api=nullptr;void* instance=nullptr;HV_HostServicesV2 services{};
 HV_GpuDeviceContextV1 device{sizeof(device),1,&created};
 HV_GpuBackendConfigV1 config{sizeof(config),1,"manifest","assets","fixture.gpu"};
 {
  BackendFactory factory({});std::string error;ASSERT_TRUE(factory.RegisterV2(QueryV2,error,owner));owner.reset();
  services=factory.ServicesV2();ASSERT_EQ(services.create_gpu_backend(services.v1.context,&config,&device,&api,&instance,nullptr),HV_OK);
 }
 EXPECT_FALSE(weak.expired());HV_GpuFrameRefV1 frame{sizeof(frame),1,&created,640,480,123,456,1,HV_GPU_IMAGE_RGBA8_UNORM,0};
 HV_GpuImageTransformV1 transform{sizeof(transform),1};HV_TensorViewV1 tensor{};uint32_t count=0;
 ASSERT_EQ(api->run_image(instance,&frame,&transform,&tensor,1,&count,nullptr),HV_OK);EXPECT_EQ(count,1u);EXPECT_EQ(*static_cast<const int64_t*>(tensor.data),123);
 frame.flags=1;char text[128]{};HV_ErrorBufferV1 error{sizeof(error),1,text,sizeof(text)};
 EXPECT_EQ(api->run_image(instance,&frame,&transform,&tensor,1,&count,&error),HV_ERR_INTERNAL);EXPECT_EQ(count,0u);EXPECT_NE(std::string(text).find("C ABI"),std::string::npos);
 services.release_gpu_backend(nullptr,api,instance);EXPECT_EQ(destroyed,1);EXPECT_TRUE(weak.expired());
}
TEST(PluginAbi, StrictGpuSelectionRejectsMissingProviderAndCleansPartialCreation) {
 using namespace humanvision::runtime;Reset();BackendFactory factory({});std::string error;
 ASSERT_TRUE(factory.RegisterV2(QueryV2,error));const HV_GpuBackendApiV1* api=nullptr;void* instance=nullptr;
 HV_GpuDeviceContextV1 device{sizeof(device),1,&created};HV_GpuBackendConfigV1 config{sizeof(config),1,"manifest","assets","missing"};
 char text[256]{};HV_ErrorBufferV1 buffer{sizeof(buffer),1,text,sizeof(text)};
 EXPECT_EQ(factory.CreateGpuBackend(&config,&device,&api,&instance,&buffer),HV_ERR_MODEL_LOAD);EXPECT_EQ(created,0);EXPECT_NE(std::string(text).find("missing"),std::string::npos);
 config.requested_provider_utf8="fixture.gpu";reject_create=true;
 EXPECT_EQ(factory.CreateGpuBackend(&config,&device,&api,&instance,&buffer),HV_ERR_MODEL_LOAD);EXPECT_EQ(created,1);EXPECT_EQ(destroyed,1);EXPECT_EQ(api,nullptr);EXPECT_EQ(instance,nullptr);
 throw_create=true;EXPECT_EQ(factory.CreateGpuBackend(&config,&device,&api,&instance,&buffer),HV_ERR_INTERNAL);EXPECT_EQ(destroyed,2);
 device.api_version=2;EXPECT_EQ(factory.CreateGpuBackend(&config,&device,&api,&instance,&buffer),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(created,2);
}

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
TEST(PluginAbi, LoadsCQueryAndRetainsDynamicLibraryUntilGpuSessionRelease) {
 using namespace humanvision::runtime;
 PluginRegistry registry;std::string error;
 ASSERT_TRUE(registry.Load(std::filesystem::u8path(HV_TEST_V1_PLUGIN_PATH),error))<<error;
 const HV_GpuBackendApiV1* api=nullptr;void* instance=nullptr;HV_HostServicesV2 services{};
 bool unloaded=false;
 {
  HMODULE library=LoadLibraryW(std::filesystem::u8path(HV_TEST_GPU_PLUGIN_PATH).c_str());ASSERT_NE(library,nullptr);
  auto owner=std::shared_ptr<const void>(library,[&unloaded](const void* p){FreeLibrary((HMODULE)p);unloaded=true;});
  auto query=reinterpret_cast<HV_QueryPluginV2Fn>(GetProcAddress(library,"HV_QueryPluginV2"));ASSERT_NE(query,nullptr);
  EXPECT_EQ(GetProcAddress(library,"HV_QueryPlugin"),nullptr); // V2-only is not treated as V1.
  BackendFactory factory({});ASSERT_TRUE(factory.RegisterV2(query,error,owner))<<error;
  HV_GpuBackendConfigV1 config{sizeof(config),1,"manifest","assets","fixture.c.gpu"};
  HV_GpuDeviceContextV1 device{sizeof(device),1};services=factory.ServicesV2();
  ASSERT_EQ(services.create_gpu_backend(services.v1.context,&config,&device,&api,&instance,nullptr),HV_OK);
 }
 EXPECT_FALSE(unloaded);HV_BackendSessionInfoV1 info{sizeof(info),1};
 EXPECT_EQ(api->session_info(instance,&info),HV_OK);EXPECT_STREQ(info.actual,"C DLL fixture");
 services.release_gpu_backend(nullptr,api,instance);EXPECT_TRUE(unloaded);
}
#endif
