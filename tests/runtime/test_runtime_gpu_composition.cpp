#include "host/gpu_runtime_host.h"
#include "composition/session.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <filesystem>
#include <fstream>
#include <thread>
#include "json/json.hpp"
#include "picosha2/picosha2.h"

using namespace humanvision::runtime;
using namespace humanvision::gpu;

namespace {
struct FakeSource final : GpuConsumerSource {
    std::atomic<int> claims{0}, retires{0}, quarantines{0};
    std::atomic<bool> ready{false};
    bool stale_token = false;
    int64_t frame_id = 88, timestamp_us = 1000;
    int64_t capture_steady_us = 0;
    AhbSlotRing ring{SlotLifecycle{this,Create,Drain}};
    static bool Create(void*,uint32_t,const SlotContract&,SlotResources&) noexcept { return true; }
    static void Drain(void*,uint32_t,AhbSlotState,SlotResources&,SyncFd&) noexcept {}
    FakeSource() { Reconfigure(); }
    void Reconfigure() { SlotContract contract{};contract.width=320;contract.height=240;contract.actual_format=1;ring.Reconfigure(contract); }
    SlotResult Claim(ConsumerFrame& frame) noexcept override {
        if(quarantines.load())return SlotResult::Closed;
        if (ready.exchange(false)) {
            SlotToken prepared{};
            if (ring.Reserve(static_cast<uint64_t>(frame_id),timestamp_us,prepared,capture_steady_us)!=SlotResult::Ok ||
                ring.Transition(prepared,AhbSlotState::EventReserved,AhbSlotState::UnityCopySubmitted)!=SlotResult::Ok ||
                ring.Transition(prepared,AhbSlotState::UnityCopySubmitted,AhbSlotState::ProducerSignalPending)!=SlotResult::Ok)
                return SlotResult::Invalid;
            SyncFd fd(-1);
            if(ring.PublishReady(prepared,fd)!=SlotResult::Ok)return SlotResult::Invalid;
        }
        SlotMetadata metadata{};SlotToken token{};
        if(ring.ClaimNewest(token,metadata)!=SlotResult::Ok)return SlotResult::NoReady;
        frame.claimed = true;
        frame.token = token;
        if(stale_token)--frame.token.generation;
        frame.metadata = metadata;
        frame.ahb_buffer = 42;
        if(ring.TakeProducerFence(token,frame.producer_fd)!=SlotResult::Ok)return SlotResult::Invalid;
        ++claims;
        return SlotResult::Ok;
    }
    SlotResult Retire(ConsumerFrame& frame) noexcept override {
        if (!frame.claimed || frame.producer_fd.HasPayload() || !frame.ncnn_role_complete) return SlotResult::Invalid;
        if(ring.Transition(frame.token,AhbSlotState::InferenceRunning,AhbSlotState::ConsumerReleasePending)!=SlotResult::Ok ||
           ring.RetireConsumer(frame.token,CompletionProof::GpuQuiescent)!=SlotResult::Ok)return SlotResult::Invalid;
        frame.claimed = false; ++retires; return SlotResult::Ok;
    }
    void Quarantine(ConsumerFrame& frame) noexcept override { frame.claimed = false; ++quarantines; }
    bool DrainUnsubmitted(ConsumerFrame& frame) noexcept override {
        frame.producer_fd.Reset(); frame.ncnn_role_complete = true; return Retire(frame) == SlotResult::Ok;
    }
    uint64_t Generation() const noexcept override { return ring.Generation(); }
    uint32_t Width() const noexcept override { return 320; }
    uint32_t Height() const noexcept override { return 240; }
};
struct FakePipeline {
    std::atomic<int> creates{0}, processes{0}, destroys{0};
    std::atomic<bool> fail_create{false}, fail_run{false}, throw_after_create{false};
    std::atomic<bool> pause_run{false}, entered{false};
    std::atomic<int64_t> frame{0};
    std::atomic<int64_t> observed_capture_steady{0},observed_region_revision{0};
    bool snapshot_mode=false,sidecar_mode=false,region_mode=false,invalid_version=false;
};
FakePipeline* active = nullptr;
HV_Result HV_CALL Create(const HV_PipelineConfigV1*, const HV_HostServicesV3*, void** out, HV_ErrorBufferV1* error) {
    ++active->creates;
    if(active->throw_after_create){*out=active;throw std::runtime_error("partial create");}
    if (active->fail_create) { std::snprintf(error->data, error->capacity, "fake V3 creation failed"); return HV_ERR_INTERNAL; }
    *out = active; return HV_OK;
}
void HV_CALL Destroy(void* p) { ++static_cast<FakePipeline*>(p)->destroys; }
HV_Result HV_CALL Process(void* p, const HV_GpuFrameRefV1* input, HV_ObservationFrameV1* output, HV_ErrorBufferV1* error) {
    auto& fake = *static_cast<FakePipeline*>(p);
    ++fake.processes; fake.frame = input->frame_id;
    if(input->struct_size>=sizeof(HV_GpuFrameRefRegionV1)){
        const auto* extension=reinterpret_cast<const HV_GpuFrameRefRegionV1*>(input);
        fake.observed_capture_steady=extension->capture_steady_us;
        fake.observed_region_revision=extension->region_revision;
    }
    fake.entered=true;
    while (fake.pause_run) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (fake.fail_run) { std::snprintf(error->data, error->capacity, "fake V3 run failed"); return HV_ERR_INTERNAL; }
    auto& lease = *static_cast<ConsumerFrame*>(input->opaque_slot);
    lease.producer_fd.Reset(); lease.ncnn_role_complete = true;
    if(fake.invalid_version)output->api_version=HV_PLUGIN_API_V1+1;
    if(fake.region_mode){
        output->body_count=1;
        auto& body=output->bodies[0];body.bbox_px={250,30,60,150};body.confidence=.8F;
        auto& pelvis=body.joints[HV_CANONICAL_PELVIS];pelvis.valid=1;
        pelvis.x_px=80;pelvis.y_px=100;pelvis.x_norm=.25F;
        pelvis.y_norm=100.F/240;pelvis.confidence=.8F;
        pelvis.observation_timestamp_us=input->timestamp_us;
        if(output->struct_size>=sizeof(HV_GpuObservationFrameV3)){
            auto* ext=reinterpret_cast<HV_GpuObservationFrameV3*>(output);
            ext->detector_scores[0]=.9F;ext->crop_track_ids[0]=17;
        }
        return HV_OK;
    }
    if(fake.snapshot_mode){
        output->body_count=input->frame_id==3?0:1;
        if(output->body_count){
            auto& body=output->bodies[0];body.bbox_px={100,100,100,400};body.confidence=.9f;
            auto& joint=body.joints[HV_CANONICAL_HANDTIP_LEFT];
            joint.valid=input->frame_id==1?1:0;joint.confidence=.9f;
            joint.x_px=120;joint.y_px=220;joint.x_norm=.375f;joint.y_norm=.916f;
            joint.observation_timestamp_us=input->timestamp_us;
        }
        return HV_OK;
    }
    output->body_count = 2;
    output->bodies[0].confidence = 0.7f; output->bodies[1].confidence = 0.9f;
    if(fake.sidecar_mode&&output->struct_size>=sizeof(HV_GpuObservationFrameV3)){
        auto* extended=reinterpret_cast<HV_GpuObservationFrameV3*>(output);
        extended->detector_scores[0]=.8F;extended->detector_scores[1]=.4F;
        extended->crop_track_ids[0]=17;extended->crop_track_ids[1]=29;
    }
    return HV_OK;
}
const HV_GpuPipelineApiV2 api{sizeof(api), HV_GPU_PIPELINE_API_V2, Create, Destroy, Process};
std::shared_ptr<GpuPluginModuleV3> Module() {
    auto module = std::make_shared<GpuPluginModuleV3>();
    module->api.v1 = {sizeof(HV_PluginApiV3), HV_PLUGIN_API_V3, "fixture.gpu", "1", HV_PLUGIN_PIPELINE,
                      HV_CAP_BODY_POSE | HV_CAP_GPU_INPUT, 8, nullptr, nullptr, 0};
    module->api.gpu_pipeline = &api;
    return module;
}
HV_Result HV_CALL QueryFixtureGpu(uint32_t version,HV_PluginApiV3* out){
    if(version!=HV_PLUGIN_API_V3||!out||out->v1.struct_size<sizeof(*out))return HV_ERR_INVALID_ARGUMENT;
    *out={};out->v1={sizeof(*out),HV_PLUGIN_API_V3,"fixture.gpu","1",HV_PLUGIN_PIPELINE,
        HV_CAP_BODY_POSE|HV_CAP_MULTI_PERSON|HV_CAP_GPU_INPUT,8,nullptr,nullptr,0};
    out->gpu_pipeline=&api;return HV_OK;
}
struct SnapshotProfileFixture {
    std::filesystem::path root;
    SnapshotProfileFixture(){
        root=std::filesystem::temp_directory_path()/
            ("hv-gpu-snapshot-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root/"profiles");
        std::filesystem::create_directories(root/"modelpacks"/"fixture");
        auto folder=root/"modelpacks"/"fixture";
        for(const char* file:{"detector.param","detector.bin"})std::ofstream(folder/file)<<"abc";
        nlohmann::json profile={{"schema_version",1},{"profile","android-ncnn-vulkan"},
            {"body",{{"pipeline","fixture.gpu"},{"modelPack","fixture"}}},
            {"hands",{{"enabled",false}}},
            {"detector",{{"cadence_interval_frames",4},{"max_capture_gap_us",200000}}},
            {"backend",{{"preference",{"backend.ncnn.vulkan"}},{"allow_fallback",false}}},
            {"required_capabilities",{"body_pose","multi_person","gpu_input","vulkan",
                "fp16-storage","fp16-arithmetic","android-hardware-buffer","external-sync-fd"}}};
        const auto path=root/"profiles"/"android-ncnn-vulkan.json";
        std::ofstream(path)<<profile.dump();
        auto hash=picosha2::hash256_hex_string(profile.dump());
        nlohmann::json model={{"role","detector"},{"format","ncnn"},
            {"decoder_id","rtmdet_nano_raw_v1"},
            {"param_path","detector.param"},{"bin_path","detector.bin"},
            {"param_sha256","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
            {"bin_sha256","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
            {"input_contract",{{"image_format","rgba8-unorm"},{"color_order","rgb"},
                {"normalization",{{"mean",{0,0,0}},{"norm",{1,1,1}}}},
                {"tensor_dtype","fp16"},{"elempack",1},{"width",320},{"height",320},
                {"input_blob","in0"}}},
            {"output_contract",{{"decoder","rtmdet_nano_raw_v1"},{"output_blobs",{"cls","bbox"}}}},
            {"source","fixture"},{"license","test-only"},{"conversion_recipe","fixture"}};
        nlohmann::json pack={{"schema_version",2},{"pack_id","fixture"},{"pack_version","2.0.0"},
            {"pipeline_id","fixture.gpu"},{"profile_sha256",hash},{"max_people",2},
            {"capabilities",{"body_pose","multi_person","gpu_input","vulkan","fp16-storage",
                "fp16-arithmetic","android-hardware-buffer","external-sync-fd"}},
            {"models",nlohmann::json::array({model})}};
        std::ofstream(folder/"modelpack.json")<<pack.dump();
    }
    ~SnapshotProfileFixture(){std::filesystem::remove_all(root);}
};
bool Await(const std::function<bool()>& predicate) {
    for (int i=0;i<200;++i) { if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    return false;
}
}

TEST(RuntimeGpuComposition, ClaimsAndPublishesCompleteCurrentFrameWithoutCpuSubmission) {
    FakeSource source; FakePipeline fake; active=&fake;
    GpuRuntimeHost host(source); auto module=Module();
    HV_PipelineConfigV1 config{sizeof(config), HV_PLUGIN_API_V1, 2};
    HV_HostServicesV3 services{}; services.v2.v1.struct_size=sizeof(services.v2.v1);
    std::string error;
    ASSERT_TRUE(host.Start(module, services, config, error)) << error;
    source.ready=true;
    ASSERT_TRUE(Await([&]{HV_ObservationFrameV1 next{};int64_t revision=0;return host.CopyLatest(next,revision);}));
    HV_ObservationFrameV1 observation{}; int64_t revision=0;
    ASSERT_TRUE(host.CopyLatest(observation, revision));
    EXPECT_EQ(observation.source_frame_id,88);
    EXPECT_EQ(observation.source_timestamp_us,1000);
    EXPECT_EQ(observation.body_count,2u);
    EXPECT_FLOAT_EQ(observation.bodies[0].confidence,0.7f);
    EXPECT_FLOAT_EQ(observation.bodies[1].confidence,0.9f);
    EXPECT_EQ(source.claims,1); EXPECT_EQ(source.retires,1); EXPECT_EQ(source.quarantines,0);
    host.Stop(); EXPECT_EQ(fake.destroys,1);
}

TEST(RegionGpu, HostPreservesV3ScoreAndCropIdentitySidecarInBodyOrder) {
    FakeSource source;FakePipeline fake;fake.sidecar_mode=true;active=&fake;
    GpuRuntimeHost host(source);auto module=Module();
    HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,2};HV_HostServicesV3 services{};
    std::string error;ASSERT_TRUE(host.Start(module,services,config,error))<<error;
    host.SetRevision(7);source.ready=true;
    ASSERT_TRUE(Await([&]{HV_GpuObservationFrameV3 next{};int64_t revision;
        return host.CopyLatest(next,revision);}));
    HV_GpuObservationFrameV3 out{};int64_t revision=0;
    ASSERT_TRUE(host.CopyLatest(out,revision));EXPECT_EQ(revision,7);
    EXPECT_EQ(out.v1.body_count,2u);
    EXPECT_FLOAT_EQ(out.detector_scores[0],.8F);
    EXPECT_FLOAT_EQ(out.detector_scores[1],.4F);
    EXPECT_EQ(out.crop_track_ids[0],17);EXPECT_EQ(out.crop_track_ids[1],29);
    EXPECT_EQ(out.v1.bodies[0].reserved,0u);
    EXPECT_EQ(out.v1.bodies[1].reserved,0u);
    HV_ObservationFrameV1 prefix{};
    ASSERT_TRUE(host.CopyLatest(prefix,revision));
    EXPECT_EQ(prefix.struct_size,sizeof(prefix));
    host.Stop();active=nullptr;
}

TEST(RegionGpu, HostRejectsIncompatibleObservationVersion) {
    FakeSource source;FakePipeline fake;fake.invalid_version=true;active=&fake;
    GpuRuntimeHost host(source);auto module=Module();
    HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,2};HV_HostServicesV3 services{};
    std::string error;ASSERT_TRUE(host.Start(module,services,config,error))<<error;
    source.ready=true;
    ASSERT_TRUE(Await([&]{return source.retires.load()>=1;}));
    HV_ObservationFrameV1 out{};int64_t revision=0;
    EXPECT_FALSE(host.CopyLatest(out,revision));
    host.Stop();active=nullptr;
}

TEST(RegionGpu, SessionPublishesPelvisRegionAndDropsOldRevisionResult) {
    SnapshotProfileFixture files;FakeSource source;FakePipeline fake;fake.region_mode=true;
    fake.pause_run=true;active=&fake;RuntimeSession runtime;std::string error;
    ASSERT_TRUE(runtime.Start(files.root,"android-ncnn-vulkan",1,error,QueryFixtureGpu,&source))<<error;
    HV_Rect region{0,0,.4F,1};
    ASSERT_TRUE(runtime.SetRegions(&region,1,1,error));
    source.ready=true;ASSERT_TRUE(Await([&]{return fake.entered.load();}));
    ASSERT_TRUE(runtime.SetRegions(&region,1,2,error));
    fake.pause_run=false;
    ASSERT_TRUE(Await([&]{return source.retires.load()>=1;}));
    HV_RuntimeStatsV1 stats{};auto old=runtime.Copy(0,stats);
    EXPECT_EQ(old.count,0u);EXPECT_EQ(stats.source_frame_id,0);
    source.frame_id=89;source.timestamp_us=1033000;source.ready=true;
    BodySnapshot fresh{};
    ASSERT_TRUE(Await([&]{fresh=runtime.Copy(0,stats);return stats.source_frame_id==89;}));
    ASSERT_EQ(fresh.count,1u);EXPECT_EQ(fresh.bodies[0].region_index,0);
    EXPECT_EQ(fresh.bodies[0].region_revision,2);
    EXPECT_EQ(fresh.bodies[0].source_frame_id,89);
    active=nullptr;
}

TEST(RuntimeGpuComposition, RejectsTruncatedOrMismatchedV3CallbackTable){
    FakeSource source;FakePipeline fake;active=&fake;GpuRuntimeHost host(source);
    HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,2};HV_HostServicesV3 services{};
    std::string error;
    auto module=Module();
    HV_GpuPipelineApiV2 short_api=api;short_api.struct_size=sizeof(HV_GpuPipelineApiV2)-1;
    module->api.gpu_pipeline=&short_api;
    EXPECT_FALSE(host.Start(module,services,config,error));EXPECT_EQ(fake.creates,0);
    short_api=api;short_api.api_version=HV_GPU_PIPELINE_API_V2+1;
    EXPECT_FALSE(host.Start(module,services,config,error));EXPECT_EQ(fake.creates,0);
}

TEST(RuntimeGpuComposition, PreservesPairedCaptureClockAndRegionInV3FrameExtension){
    FakeSource source;source.capture_steady_us=7654321;
    FakePipeline fake;active=&fake;GpuRuntimeHost host(source);auto module=Module();
    HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,2};HV_HostServicesV3 services{};
    std::string error;ASSERT_TRUE(host.Start(module,services,config,error))<<error;
    host.SetRevision(19);source.ready=true;
    ASSERT_TRUE(Await([&]{return fake.processes.load()==1;}));
    EXPECT_EQ(fake.observed_capture_steady.load(),7654321);
    EXPECT_EQ(fake.observed_region_revision.load(),19);
    host.Stop();active=nullptr;
}

TEST(RuntimeGpuComposition, PublicGpuSnapshotNeverCarriesPreviousJointsOrBodies){
    SnapshotProfileFixture files;FakeSource source;FakePipeline fake;fake.snapshot_mode=true;active=&fake;
    RuntimeSession runtime;std::string error;
    ASSERT_TRUE(runtime.Start(files.root,"android-ncnn-vulkan",1,error,QueryFixtureGpu,&source))<<error;
    auto next=[&](int64_t id,int64_t timestamp){
        source.frame_id=id;source.timestamp_us=timestamp;source.ready=true;
        HV_RuntimeStatsV1 stats{};BodySnapshot snapshot;
        const bool ready=Await([&]{snapshot=runtime.Copy(timestamp+10000,stats);
            return stats.source_frame_id==id;});
        EXPECT_TRUE(ready);return snapshot;
    };
    const auto first=next(1,1000000);
    ASSERT_EQ(first.count,1u);EXPECT_TRUE(first.bodies[0].joints[HV_CANONICAL_HANDTIP_LEFT].valid);
    const auto second=next(2,1033000);
    ASSERT_EQ(second.count,1u);EXPECT_FALSE(second.bodies[0].joints[HV_CANONICAL_HANDTIP_LEFT].valid);
    const auto third=next(3,1066000);
    EXPECT_EQ(third.count,0u);
    active=nullptr;
}

TEST(RuntimeGpuComposition, V3ErrorsDoNotPublishOrFallback) {
    FakeSource source; FakePipeline fake; active=&fake;
    GpuRuntimeHost host(source); auto module=Module();
    HV_PipelineConfigV1 config{sizeof(config), HV_PLUGIN_API_V1, 2}; HV_HostServicesV3 services{};
    std::string error;
    fake.fail_create=true;
    EXPECT_FALSE(host.Start(module,services,config,error)); EXPECT_NE(error.find("creation failed"),std::string::npos);
    fake.fail_create=false; fake.fail_run=true;
    ASSERT_TRUE(host.Start(module,services,config,error)) << error;
    source.ready=true; ASSERT_TRUE(Await([&]{return !host.LastError().empty();}));
    HV_ObservationFrameV1 observation{}; int64_t revision=0;
    EXPECT_FALSE(host.CopyLatest(observation,revision));
    EXPECT_NE(host.LastError().find("run failed"),std::string::npos);
    host.Stop(); EXPECT_EQ(source.claims,1); EXPECT_EQ(source.retires,1);
}

TEST(RuntimeGpuComposition, StopAndRestartDrainClaimedSlotsExactlyOnce) {
    FakeSource source; FakePipeline fake; active=&fake;
    GpuRuntimeHost host(source); auto module=Module();
    HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,2}; HV_HostServicesV3 services{};
    std::string error;
    ASSERT_TRUE(host.Start(module,services,config,error));
    fake.pause_run=true; source.ready=true;
    ASSERT_TRUE(Await([&]{return fake.entered.load();}));
    std::thread stop([&]{host.Stop();});
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(source.retires,0);
    fake.pause_run=false; stop.join();
    EXPECT_EQ(source.claims,1);EXPECT_EQ(source.retires,1);EXPECT_EQ(source.quarantines,0);
    source.Reconfigure();source.frame_id=89;source.timestamp_us=2000;fake.entered=false;
    ASSERT_TRUE(host.Start(module,services,config,error))<<error;
    source.ready=true;
    ASSERT_TRUE(Await([&]{HV_ObservationFrameV1 next{};int64_t revision=0;return host.CopyLatest(next,revision);}));
    HV_ObservationFrameV1 output{};int64_t revision=0;
    ASSERT_TRUE(host.CopyLatest(output,revision));EXPECT_EQ(output.source_frame_id,89);
    host.Stop();EXPECT_EQ(source.claims,2);EXPECT_EQ(source.retires,2);
}

TEST(RuntimeGpuComposition, StaleGenerationDrainsWithoutPipelineCallOrPublication) {
    FakeSource source;source.stale_token=true;FakePipeline fake;active=&fake;
    GpuRuntimeHost host(source);auto module=Module();
    HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,2};HV_HostServicesV3 services{};
    std::string error;ASSERT_TRUE(host.Start(module,services,config,error));
    source.ready=true;ASSERT_TRUE(Await([&]{return source.quarantines==1;}));
    EXPECT_EQ(fake.processes,0);EXPECT_NE(host.LastError().find("stale"),std::string::npos);
    HV_ObservationFrameV1 output{};int64_t revision=0;
    EXPECT_FALSE(host.CopyLatest(output,revision));
    host.Stop();EXPECT_EQ(source.claims,1);EXPECT_EQ(source.retires,0);
}

TEST(RuntimeGpuComposition, PartiallyCreatedThrowDestroysInstance) {
    FakeSource source;FakePipeline fake;active=&fake;fake.throw_after_create=true;
    GpuRuntimeHost host(source);auto module=Module();
    HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,2};HV_HostServicesV3 services{};
    std::string error;EXPECT_FALSE(host.Start(module,services,config,error));
    EXPECT_EQ(fake.creates,1);EXPECT_EQ(fake.destroys,1);
    EXPECT_NE(error.find("threw"),std::string::npos);
}

namespace {
int begin_count=0,end_count=0,prepare_count=0;bool first_active=false,second_active=false;
uint32_t prepared_width=0,prepared_height=0;
std::atomic<bool> end_entered{false},end_released{false};
bool BeginLease(void*) noexcept {++begin_count;return true;}
void EndLease() noexcept {++end_count;}
void BlockingEndLease() noexcept {
    end_entered=true;
    while (!end_released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
void ActiveLease(void* owner,bool is_active) noexcept {*static_cast<bool*>(owner)=is_active;}
BridgeResult PrepareLease(const HV_AndroidGpuSubmissionV1&,void**) noexcept {++prepare_count;return BridgeResult::Ok;}
void RecordDimensions(void*,uint32_t width,uint32_t height) noexcept {prepared_width=width;prepared_height=height;}
void ActiveRuntime(void* owner,bool is_active) noexcept {
    static_cast<RuntimeSession*>(owner)->SetGpuSourceLeaseActive(is_active);
}
}
TEST(RuntimeGpuComposition, ForeignRuntimeCannotEndOrConsumeGlobalProducerLease) {
    auto& coordinator=GpuSourceLeaseCoordinator::Instance();
    begin_count=end_count=prepare_count=0;first_active=second_active=false;
    std::string error;
    ASSERT_TRUE(coordinator.Begin(&first_active,reinterpret_cast<void*>(1),BeginLease,EndLease,ActiveLease,error));
    EXPECT_TRUE(first_active);EXPECT_TRUE(coordinator.Owns(&first_active));
    EXPECT_FALSE(coordinator.Begin(&second_active,reinterpret_cast<void*>(2),BeginLease,EndLease,ActiveLease,error));
    EXPECT_FALSE(coordinator.End(&second_active,error));
    EXPECT_TRUE(coordinator.Owns(&first_active));EXPECT_TRUE(first_active);EXPECT_EQ(end_count,0);
    EXPECT_FALSE(coordinator.Owns(&second_active));
    HV_AndroidGpuSubmissionV1 submission{};submission.width=320;submission.height=240;void* event=nullptr;
    EXPECT_EQ(coordinator.Prepare(&second_active,submission,&event,PrepareLease,RecordDimensions),BridgeResult::Closed);
    EXPECT_EQ(prepare_count,0);
    EXPECT_EQ(coordinator.Prepare(&first_active,submission,&event,PrepareLease,RecordDimensions),BridgeResult::Ok);
    EXPECT_EQ(prepare_count,1);
    EXPECT_EQ(prepared_width,320u);EXPECT_EQ(prepared_height,240u);
    ASSERT_TRUE(coordinator.End(&first_active,error));
    EXPECT_FALSE(first_active);EXPECT_EQ(end_count,1);
    ASSERT_TRUE(coordinator.Begin(&second_active,reinterpret_cast<void*>(2),BeginLease,EndLease,ActiveLease,error));
    EXPECT_TRUE(second_active);EXPECT_FALSE(first_active);
    ASSERT_TRUE(coordinator.End(&second_active,error));EXPECT_EQ(begin_count,2);EXPECT_EQ(end_count,2);
}

TEST(RuntimeGpuComposition, PrepareReturnsBusyWhileLeaseEndDrains) {
    auto& coordinator=GpuSourceLeaseCoordinator::Instance();
    bool owner_active=false;std::string error;
    end_entered=false;end_released=false;prepared_width=prepared_height=0;
    ASSERT_TRUE(coordinator.Begin(&owner_active,reinterpret_cast<void*>(1),BeginLease,
                                  BlockingEndLease,ActiveLease,error));
    std::thread ending([&]{std::string end_error;coordinator.End(&owner_active,end_error);});
    if (!Await([&]{return end_entered.load();})) {
        end_released=true;ending.join();FAIL() << "lease end did not enter drain";return;
    }
    HV_AndroidGpuSubmissionV1 submission{};submission.width=320;submission.height=240;void* event=nullptr;
    std::thread watchdog([]{std::this_thread::sleep_for(std::chrono::milliseconds(500));end_released=true;});
    const auto start=std::chrono::steady_clock::now();
    const auto result=coordinator.Prepare(&owner_active,submission,&event,PrepareLease,RecordDimensions);
    const auto elapsed=std::chrono::steady_clock::now()-start;
    end_released=true;ending.join();watchdog.join();
    EXPECT_EQ(result,BridgeResult::Busy);
    EXPECT_LT(elapsed,std::chrono::milliseconds(150));
    EXPECT_EQ(prepared_width,0u);EXPECT_EQ(prepared_height,0u);
}

TEST(RuntimeGpuComposition, DestroyingOwnerEndsOnlyItsOwnSourceLease) {
    auto& coordinator=GpuSourceLeaseCoordinator::Instance();
    begin_count=end_count=0;std::string error;
    auto owner=std::make_unique<RuntimeSession>();
    auto foreign=std::make_unique<RuntimeSession>();
    ASSERT_TRUE(coordinator.Begin(owner.get(),reinterpret_cast<void*>(1),BeginLease,EndLease,ActiveRuntime,error));
    foreign.reset();
    EXPECT_TRUE(coordinator.Owns(owner.get()));EXPECT_EQ(end_count,0);
    owner.reset();
    EXPECT_EQ(end_count,1);
    auto next=std::make_unique<RuntimeSession>();
    ASSERT_TRUE(coordinator.Begin(next.get(),reinterpret_cast<void*>(2),BeginLease,EndLease,ActiveRuntime,error));
    next.reset();EXPECT_EQ(end_count,2);
}
