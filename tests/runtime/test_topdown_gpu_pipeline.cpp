#include "plugins/pipeline/simcc/detector_cadence.h"
#include "plugins/pipeline/simcc/topdown_gpu_pipeline.h"
#include "gpu/android/unity_vulkan_bridge.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include "json/json.hpp"

using humanvision::runtime::DetectorCadenceScheduler;
using humanvision::runtime::DetectorCadenceTrigger;
void BeginNativeAllocationProbe() noexcept;
std::size_t EndNativeAllocationProbe() noexcept;

TEST(TopDownGpu, CadenceCountsAcceptedPoseFramesAndCaptureTime) {
    for (int interval = 2; interval <= 6; ++interval) {
        DetectorCadenceScheduler cadence(interval, 200000);
        EXPECT_TRUE(cadence.ShouldCapture(1, 1000, DetectorCadenceTrigger::NoTracks));
        EXPECT_TRUE(cadence.AdmitPrepared(1, 1000, 7));
        EXPECT_TRUE(cadence.FinishDetector(1, 7));
        for (int frame = 2; frame < interval; ++frame)
            EXPECT_FALSE(cadence.ShouldCapture(frame, frame * 1000, DetectorCadenceTrigger::None));
        EXPECT_TRUE(cadence.ShouldCapture(interval + 1, 201001, DetectorCadenceTrigger::None));
    }
}

TEST(TopDownGpu, BusyDeadlineAndGenerationAreCounted) {
    DetectorCadenceScheduler cadence(4, 200000);
    EXPECT_TRUE(cadence.ShouldCapture(1, 1000, DetectorCadenceTrigger::NoTracks));
    ASSERT_TRUE(cadence.AdmitPrepared(1, 1000, 7));
    EXPECT_TRUE(cadence.ShouldCapture(2, 202000, DetectorCadenceTrigger::None));
    EXPECT_FALSE(cadence.AdmitPrepared(2, 202000, 7));
    EXPECT_EQ(cadence.MissedDeadlines(), 1u);
    EXPECT_TRUE(cadence.ShouldCapture(3, 403000, DetectorCadenceTrigger::None));
    EXPECT_EQ(cadence.MissedDeadlines(), 2u);
    EXPECT_FALSE(cadence.FinishDetector(1, 6));
    cadence.CancelGeneration(7);
    EXPECT_TRUE(cadence.ShouldCapture(4, 404000, DetectorCadenceTrigger::RegionChanged));
    EXPECT_TRUE(cadence.AdmitPrepared(4, 404000, 8));
}

TEST(TopDownGpu, RejectionEdgeAndRegionRequestEarlierDetection) {
    for (auto trigger : {DetectorCadenceTrigger::NoTracks, DetectorCadenceTrigger::PoseRejected,
                         DetectorCadenceTrigger::CropAtEdge, DetectorCadenceTrigger::RegionChanged}) {
        DetectorCadenceScheduler cadence(6, 200000);
        EXPECT_TRUE(cadence.ShouldCapture(10, 1000, trigger));
    }
}

TEST(TopDownGpu, UnstartedDetectorCanBeSupersededButRunningRefMustFinish) {
    DetectorCadenceScheduler cadence(4,200000);
    EXPECT_TRUE(cadence.ShouldCapture(10,1000,DetectorCadenceTrigger::NoTracks));
    ASSERT_TRUE(cadence.AdmitPrepared(10,1000,5));
    EXPECT_FALSE(cadence.CancelUnstarted(10,6));
    EXPECT_TRUE(cadence.Busy());
    EXPECT_TRUE(cadence.CancelUnstarted(10,5));
    EXPECT_TRUE(cadence.ShouldCapture(11,2000,DetectorCadenceTrigger::NoTracks));
    EXPECT_TRUE(cadence.AdmitPrepared(11,2000,5));
    EXPECT_TRUE(cadence.FinishDetector(11,5));
    EXPECT_FALSE(cadence.CancelUnstarted(11,5));
}

TEST(TopDownGpu, ProductionV3RouteExposesCurrentImagePipeline) {
    HV_PluginApiV3 api{};
    api.v1.struct_size=sizeof(api);api.v1.api_version=HV_PLUGIN_API_V3;
    ASSERT_EQ(HV_QueryTopDownGpuPipelineV3(HV_PLUGIN_API_V3,&api),HV_OK);
    ASSERT_NE(api.gpu_pipeline,nullptr);
    EXPECT_STREQ(api.v1.plugin_id,"pipeline.topdown");
    EXPECT_EQ(api.v1.max_people,8u);
    EXPECT_NE(api.v1.capabilities & HV_CAP_GPU_INPUT,0u);
}

TEST(TopDownGpu, CreateRejectsTruncatedOrMismatchedV3HostPrefix){
    HV_PluginApiV3 api{};api.v1.struct_size=sizeof(api);api.v1.api_version=HV_PLUGIN_API_V3;
    ASSERT_EQ(HV_QueryTopDownGpuPipelineV3(HV_PLUGIN_API_V3,&api),HV_OK);
    const auto manifest=nlohmann::json{{"schema_version",2},{"models",nlohmann::json::array()}}.dump();
    const std::string options=R"({"detector":{"cadence_interval_frames":4,"max_capture_gap_us":200000}})";
    HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,2,0,
        manifest.c_str(),"fixture",options.c_str()};
    HV_HostServicesV3 host{};host.v2.v1.struct_size=sizeof(HV_HostServicesV1);
    host.v2.v1.api_version=HV_PLUGIN_API_V1;
    host.create_gpu_backend_v3=reinterpret_cast<decltype(host.create_gpu_backend_v3)>(1);
    host.release_gpu_backend_v3=reinterpret_cast<decltype(host.release_gpu_backend_v3)>(1);
    void* instance=nullptr;char text[256]{};
    HV_ErrorBufferV1 error{sizeof(error),HV_PLUGIN_API_V1,text,sizeof(text)};
    EXPECT_EQ(api.gpu_pipeline->create(&config,&host,&instance,&error),HV_ERR_INVALID_ARGUMENT);
    host.v2.v1.struct_size=sizeof(host);host.v2.v1.api_version=HV_PLUGIN_API_V3;
    EXPECT_EQ(api.gpu_pipeline->create(&config,&host,&instance,&error),HV_ERR_INVALID_ARGUMENT);
}

TEST(TopDownGpu, RegionFrameExtensionPreservesFrozenV1Prefix) {
    EXPECT_EQ(sizeof(HV_GpuFrameRefV1),56u);
    EXPECT_EQ(offsetof(HV_GpuFrameRefRegionV1,region_revision),sizeof(HV_GpuFrameRefV1));
    EXPECT_EQ(offsetof(HV_GpuFrameRefRegionV1,v1),0u);
}

TEST(TopDownGpu, GpuPoseCropMatchesFourRealGoldenAffines) {
    for(const char* name:{"full-body","clipped-person","mirrored","rotated"}){
        const auto path=std::filesystem::path(HV_TEST_PROJECT_ROOT)/
            "out/c3-local-runtime/strict-vkmat/golden"/name/"crop.json";
        std::ifstream stream(path);ASSERT_TRUE(stream.good())<<path.string();
        nlohmann::json crop;stream>>crop;
        const auto bbox=crop.at("bbox");
        humanvision::Detection box{bbox[0].get<float>(),bbox[1].get<float>(),
            bbox[2].get<float>(),bbox[3].get<float>(),1};
        HV_GpuImageTransformV1 transform{};
        HV_Rect inverse{};
        ASSERT_TRUE(humanvision::runtime::BuildGpuPoseCrop(box,transform,inverse));
        const auto expected=crop.at("inverse_affine");
        EXPECT_NEAR(inverse.width/192.f,expected[0][0].get<float>(),.0001f)<<name;
        EXPECT_NEAR(inverse.height/256.f,expected[1][1].get<float>(),.0001f)<<name;
        EXPECT_NEAR(inverse.x,expected[0][2].get<float>(),.0001f)<<name;
        EXPECT_NEAR(inverse.y,expected[1][2].get<float>(),.0001f)<<name;
        EXPECT_NEAR(transform.source_rect_px.x+.5f*inverse.width/192.f-.5f,
                    inverse.x,.0001f)<<name;
    }
}

namespace {
struct FakeGpu {
    std::array<float,2100> cls{};
    std::array<float,8400> bbox{};
    std::array<float,26*384> x{};
    std::array<float,26*512> y{};
    std::atomic<int> detector_runs{0};
    std::atomic<int> prepares{0};
    std::atomic<bool> detector_running{false};
    std::atomic<int> pose_overlap{0};
    std::vector<int64_t> posed_frames;
    int people=2,stall_ms=0,pose_entry_delay_ms=0;
    bool fail_prepare=false,fail_pose=false,fail_complete=false;
    FakeGpu(){
        cls.fill(-20.f);bbox.fill(0.f);x.fill(-10.f);y.fill(-10.f);
        for(int index:{410,430}){cls[index]=10.f;for(int n=0;n<4;++n)bbox[index*4+n]=25.f;}
        for(int k=0;k<26;++k){x[k*384+100+k*2]=.9f;y[k*512+100+k*2]=.9f;}
    }
};
FakeGpu* active=nullptr;
void Fill(HV_TensorViewV1& out,const char* name,const float* data,int rows,int cols){
    out={};out.struct_size=sizeof(out);out.api_version=HV_PLUGIN_API_V1;
    out.name=name;out.element_type=1;out.rank=3;
    out.dimensions[0]=1;out.dimensions[1]=rows;out.dimensions[2]=cols;
    out.data=data;out.byte_count=uint64_t(rows)*cols*sizeof(float);
}
HV_Result HV_CALL FakeBackendCreate(const HV_GpuBackendConfigV1*,const HV_GpuDeviceContextV1*,void**,HV_ErrorBufferV1*){return HV_ERR_INVALID_ARGUMENT;}
void HV_CALL FakeBackendDestroy(void*){}
bool Complete(void*,humanvision::gpu::ConsumerFrame& frame,bool,std::string&) noexcept{
    if(active->fail_complete)return false;
    frame.ncnn_role_complete=true;return true;
}
HV_Result HV_CALL FakePose(void*,const HV_GpuFrameRefV1* frame,const HV_GpuImageTransformV1*,
                          HV_TensorViewV1* views,uint32_t capacity,uint32_t* count,HV_ErrorBufferV1*){
    if(capacity<2)return HV_ERR_INVALID_ARGUMENT;
    if(active->fail_pose)return HV_ERR_INTERNAL;
    if(active->pose_entry_delay_ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(active->pose_entry_delay_ms));
    if(active->detector_running)++active->pose_overlap;
    active->posed_frames.push_back(frame->frame_id);
    Fill(views[0],"simcc_x",active->x.data(),26,384);
    Fill(views[1],"simcc_y",active->y.data(),26,512);*count=2;
    auto& lease=*static_cast<humanvision::gpu::ConsumerFrame*>(frame->opaque_slot);
    lease.role_owner=active;lease.complete_role=Complete;
    return HV_OK;
}
HV_Result HV_CALL FakeInfo(void*,HV_BackendSessionInfoV1*){return HV_OK;}
HV_Result HV_CALL FakePrepare(void*,const HV_GpuFrameRefV1* frame,const HV_GpuImageTransformV1*,
                              HV_GpuPreparedRefV1* ref,HV_ErrorBufferV1*){
    ++active->prepares;
    if(active->fail_prepare)return HV_ERR_INTERNAL;
    static std::atomic<uint64_t> token{0};
    *ref={sizeof(*ref),HV_GPU_PREPARED_API_V1,++token,frame->generation,frame->frame_id,frame->timestamp_us,0,0};
    auto& lease=*static_cast<humanvision::gpu::ConsumerFrame*>(frame->opaque_slot);
    lease.ncnn_role_complete=true;return HV_OK;
}
HV_Result HV_CALL FakeDetector(void*,const HV_GpuPreparedRefV1*,HV_TensorViewV1* views,
                               uint32_t capacity,uint32_t* count,HV_ErrorBufferV1*){
    if(capacity<2)return HV_ERR_INVALID_ARGUMENT;
    active->detector_running=true;
    if(active->stall_ms)std::this_thread::sleep_for(std::chrono::milliseconds(active->stall_ms));
    active->detector_runs++;
    if(active->people<2)active->cls[430]=-20.f;
    Fill(views[0],"cls",active->cls.data(),2100,1);
    Fill(views[1],"bbox",active->bbox.data(),2100,4);*count=2;
    active->detector_running=false;return HV_OK;
}
HV_Result HV_CALL FakeDiscard(void*,const HV_GpuPreparedRefV1*,HV_ErrorBufferV1*){return HV_OK;}
const HV_GpuPreparedApiV1 prepared{sizeof(prepared),HV_GPU_PREPARED_API_V1,FakePrepare,FakeDetector,FakeDiscard};
const HV_GpuBackendApiV2 detector_api{{sizeof(detector_api),HV_GPU_FRAME_API_V1,FakeBackendCreate,
    FakeBackendDestroy,FakePose,FakeInfo},&prepared};
const HV_GpuBackendApiV2 pose_api{{sizeof(pose_api),HV_GPU_FRAME_API_V1,FakeBackendCreate,
    FakeBackendDestroy,FakePose,FakeInfo},&prepared};
HV_Result HV_CALL CreateBackend(void*,const HV_GpuBackendConfigV1* config,const HV_GpuDeviceContextV1*,
    const HV_GpuBackendApiV2** api,void** instance,HV_ErrorBufferV1*){
    const auto json=nlohmann::json::parse(config->model_manifest_utf8);
    *api=json.at("active_role")=="detector"?&detector_api:&pose_api;
    *instance=active;return HV_OK;
}
void HV_CALL ReleaseBackend(void*,const HV_GpuBackendApiV2*,void*){}
std::string Manifest(){
    auto input=[](int w,int h,int pack){return nlohmann::json{{"image_format","rgba8-unorm"},
        {"color_order","rgb"},{"normalization",{{"mean",{0,0,0}},{"norm",{1,1,1}}}},
        {"tensor_dtype","fp16"},{"elempack",pack},{"width",w},{"height",h},{"input_blob","in0"}};};
    nlohmann::json models=nlohmann::json::array();
    models.push_back({{"role","detector"},{"input_contract",input(320,320,1)},
        {"output_contract",{{"output_blobs",{"cls","bbox"}}}}});
    models.push_back({{"role","body"},{"input_contract",input(192,256,4)},
        {"output_contract",{{"output_blobs",{"simcc_x","simcc_y"}}}}});
    return nlohmann::json{{"schema_version",2},{"models",models}}.dump();
}
struct PipelineFixture {
    HV_PluginApiV3 api{};void* instance=nullptr;std::string manifest=Manifest();
    std::string options=R"({"detector":{"cadence_interval_frames":4,"max_capture_gap_us":200000}})";
    PipelineFixture(){
        api.v1.struct_size=sizeof(api);api.v1.api_version=HV_PLUGIN_API_V3;
        EXPECT_EQ(HV_QueryTopDownGpuPipelineV3(HV_PLUGIN_API_V3,&api),HV_OK);
        HV_HostServicesV3 host{};host.v2.v1.struct_size=sizeof(host);
        host.v2.v1.api_version=HV_PLUGIN_API_V1;
        host.create_gpu_backend_v3=CreateBackend;
        host.release_gpu_backend_v3=ReleaseBackend;
        HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,2,0,
            manifest.c_str(),"fixture",options.c_str()};
        char message[256]{};HV_ErrorBufferV1 error{sizeof(error),HV_PLUGIN_API_V1,message,sizeof(message)};
        EXPECT_EQ(api.gpu_pipeline->create(&config,&host,&instance,&error),HV_OK)<<message;
    }
    ~PipelineFixture(){if(instance)api.gpu_pipeline->destroy(instance);}
    HV_ObservationFrameV1 Process(int64_t id,int64_t timestamp,uint64_t generation=7,
                                  int64_t revision=0,int64_t steady_capture=0,
                                  HV_Result expected=HV_OK){
        humanvision::gpu::ConsumerFrame lease{};lease.claimed=true;
        HV_GpuFrameRefRegionV1 frame{{sizeof(frame),HV_GPU_FRAME_API_V1,&lease,320,320,
            id,timestamp,generation,HV_GPU_IMAGE_RGBA8_UNORM,0},revision,steady_capture};
        HV_ObservationFrameV1 out{};out.struct_size=sizeof(out);out.api_version=HV_PLUGIN_API_V1;
        char message[256]{};HV_ErrorBufferV1 error{sizeof(error),HV_PLUGIN_API_V1,message,sizeof(message)};
        EXPECT_EQ(api.gpu_pipeline->process_gpu(instance,&frame.v1,&out,&error),expected)<<message;
        return out;
    }
};
int64_t Timestamp(){return std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
}

TEST(TopDownGpu, PreparedDetectorDoesNotBlockCurrentPoseAndPublishesTwoFreshBodiesOnce){
    FakeGpu fake;fake.stall_ms=40;active=&fake;
    PipelineFixture pipeline;
    const auto base=Timestamp();
    const auto start=std::chrono::steady_clock::now();
    const auto empty=pipeline.Process(90,base);
    EXPECT_EQ(empty.body_count,0u);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-start).count(),30);
    for(int n=0;n<100&&fake.detector_runs.load()==0;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(fake.detector_runs.load(),1);
    // Give the detached worker time to publish its completed detector anchor.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto two=pipeline.Process(91,base+33000);
    EXPECT_EQ(two.body_count,2u);
    const auto again=pipeline.Process(92,base+66000);
    EXPECT_EQ(again.body_count,2u);
    EXPECT_EQ(fake.posed_frames,(std::vector<int64_t>{91,91,92,92}));
    EXPECT_EQ(fake.detector_runs.load(),1);
    for(unsigned i=0;i<two.body_count;++i)
        EXPECT_EQ(two.bodies[i].joints[HV_CANONICAL_NOSE].observation_timestamp_us,base+33000);
    for(unsigned i=0;i<again.body_count;++i)
        EXPECT_EQ(again.bodies[i].joints[HV_CANONICAL_NOSE].observation_timestamp_us,base+66000);
    active=nullptr;
}

TEST(TopDownGpu, GenerationRestartDropsOldDetectorAndOldJoints){
    FakeGpu fake;active=&fake;PipelineFixture pipeline;const auto base=Timestamp();
    EXPECT_EQ(pipeline.Process(1,base).body_count,0u);
    for(int n=0;n<100&&fake.detector_runs.load()==0;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(fake.detector_runs.load(),1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_EQ(pipeline.Process(2,Timestamp()).body_count,2u);
    EXPECT_EQ(pipeline.Process(3,Timestamp(),8).body_count,0u);
    for(int n=0;n<100&&fake.detector_runs.load()<2;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(fake.detector_runs.load(),2);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto current=Timestamp();
    const auto after=pipeline.Process(4,current,8);
    ASSERT_EQ(after.body_count,2u);
    EXPECT_EQ(after.bodies[0].joints[HV_CANONICAL_NOSE].observation_timestamp_us,current);
    active=nullptr;
}

TEST(TopDownGpu, RegionRevisionDropsOldDetectorAndOldJoints){
    FakeGpu fake;fake.stall_ms=40;active=&fake;PipelineFixture pipeline;
    const auto base=Timestamp();
    EXPECT_EQ(pipeline.Process(1,base,7,1).body_count,0u);
    // Revision advances while the old detector job is still outstanding.
    EXPECT_EQ(pipeline.Process(2,base+33000,7,2).body_count,0u);
    for(int n=0;n<150&&fake.detector_runs.load()<2;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(fake.detector_runs.load(),2);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto current=base+66000;
    const auto out=pipeline.Process(3,current,7,2);
    ASSERT_EQ(out.body_count,2u);
    for(unsigned i=0;i<out.body_count;++i)
        EXPECT_EQ(out.bodies[i].joints[HV_CANONICAL_NOSE].observation_timestamp_us,current);
    active=nullptr;
}

TEST(TopDownGpu, DetectorArrivalIncludesQueueDelayBeforePreparation){
    FakeGpu fake;fake.stall_ms=40;active=&fake;PipelineFixture pipeline;
    const auto captured=Timestamp()-180000;
    EXPECT_EQ(pipeline.Process(1,1000000,7,0,captured).body_count,0u);
    for(int n=0;n<150&&fake.detector_runs.load()==0;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(fake.detector_runs.load(),1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_EQ(pipeline.Process(2,1033000,7,0,Timestamp()).body_count,0u);
    for(int n=0;n<150&&fake.detector_runs.load()<2;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    active=nullptr;
}

TEST(TopDownGpu, PrepareFailureEntersTerminalErrorInsteadOfWedgingCadence){
    FakeGpu fake;fake.fail_prepare=true;active=&fake;PipelineFixture pipeline;
    const auto base=Timestamp();
    pipeline.Process(1,base,7,0,base,HV_ERR_INTERNAL);
    fake.fail_prepare=false;
    pipeline.Process(2,base+33000,7,0,base+33000,HV_ERR_INTERNAL);
    EXPECT_EQ(fake.prepares.load(),1);
    active=nullptr;
}

TEST(TopDownGpu, QueuedDetectorIsWokenEvenWhenPoseFails){
    FakeGpu fake;active=&fake;PipelineFixture pipeline;const auto base=Timestamp();
    EXPECT_EQ(pipeline.Process(1,base).body_count,0u);
    for(int n=0;n<100&&fake.detector_runs.load()==0;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(fake.detector_runs.load(),1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_EQ(pipeline.Process(2,base+33000).body_count,2u);
    fake.fail_pose=true;
    pipeline.Process(3,base+201000,7,0,0,HV_ERR_INTERNAL);
    for(int n=0;n<100&&fake.detector_runs.load()<2;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(fake.detector_runs.load(),2);
    active=nullptr;
}

TEST(TopDownGpu, QueuedDetectorIsWokenEvenWhenRoleCompletionFails){
    FakeGpu fake;active=&fake;PipelineFixture pipeline;const auto base=Timestamp();
    EXPECT_EQ(pipeline.Process(1,base).body_count,0u);
    for(int n=0;n<100&&fake.detector_runs.load()==0;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(fake.detector_runs.load(),1);
    EXPECT_EQ(pipeline.Process(2,base+33000).body_count,2u);
    fake.fail_complete=true;
    pipeline.Process(3,base+201000,7,0,0,HV_ERR_INTERNAL);
    for(int n=0;n<100&&fake.detector_runs.load()<2;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(fake.detector_runs.load(),2);
    active=nullptr;
}

TEST(TopDownGpu, AdmittedKeyframeWaitsForCurrentPoseToFinish){
    FakeGpu fake;fake.stall_ms=40;fake.pose_entry_delay_ms=5;
    active=&fake;PipelineFixture pipeline;const auto base=Timestamp();
    EXPECT_EQ(pipeline.Process(1,base).body_count,0u);
    for(int n=0;n<100&&fake.detector_runs.load()==0;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(fake.detector_runs.load(),1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_EQ(pipeline.Process(2,base+33000).body_count,2u);
    EXPECT_EQ(pipeline.Process(3,base+201000).body_count,2u);
    for(int n=0;n<100&&fake.detector_runs.load()<2;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(fake.detector_runs.load(),2);
    EXPECT_EQ(fake.pose_overlap.load(),0);
    active=nullptr;
}

TEST(TopDownGpu, OneBodyAndPoseRejectionNeverPublishOldJoints){
    FakeGpu fake;fake.people=1;active=&fake;PipelineFixture pipeline;
    const auto base=Timestamp();
    EXPECT_EQ(pipeline.Process(10,base).body_count,0u);
    for(int n=0;n<100&&fake.detector_runs.load()==0;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(fake.detector_runs.load(),1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto good=pipeline.Process(11,Timestamp());
    ASSERT_EQ(good.body_count,1u);
    fake.x.fill(-10.f);fake.y.fill(-10.f);
    const auto rejected=pipeline.Process(12,Timestamp());
    EXPECT_EQ(rejected.body_count,0u);
    EXPECT_EQ(fake.posed_frames,(std::vector<int64_t>{11,12}));
    active=nullptr;
}

TEST(TopDownGpu, WarmedPoseOnlyObservationReusesPipelineStorage){
    FakeGpu fake;fake.posed_frames.reserve(32);active=&fake;PipelineFixture pipeline;
    pipeline.Process(20,Timestamp());
    for(int n=0;n<100&&fake.detector_runs.load()==0;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(fake.detector_runs.load(),1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ASSERT_EQ(pipeline.Process(21,Timestamp()).body_count,2u);
    BeginNativeAllocationProbe();
    const auto result=pipeline.Process(22,Timestamp());
    const auto allocations=EndNativeAllocationProbe();
    EXPECT_EQ(result.body_count,2u);
    EXPECT_EQ(allocations,0u);
    active=nullptr;
}

TEST(TopDownGpu, DetectorArrivalUsesCaptureClockRatherThanNativeClockEpoch){
    FakeGpu fake;active=&fake;PipelineFixture pipeline;
    EXPECT_EQ(pipeline.Process(30,1000,7,0,Timestamp()).body_count,0u);
    for(int n=0;n<100&&fake.detector_runs.load()==0;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(fake.detector_runs.load(),1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_EQ(pipeline.Process(31,34000,7,0,Timestamp()).body_count,2u);
    active=nullptr;
}
