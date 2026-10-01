#include <gtest/gtest.h>
#include "plugins/pipeline/yolo/yolo_gpu_pipeline.h"
#include "plugins/backend/ncnn/ncnn_model_options.h"
#include "plugins/backend/ncnn/ncnn_execution_contract.h"
#include "host/model_pack_manager.h"
#include "host/profile_manager.h"
#include "host/backend_factory.h"
#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "plugins/pipeline/simcc/topdown_gpu_pipeline.h"
#include "json/json.hpp"
#include "gpu/android/unity_vulkan_bridge.h"
#include <fstream>
#include <chrono>

void BeginNativeAllocationProbe() noexcept;
std::size_t EndNativeAllocationProbe() noexcept;

namespace {
nlohmann::json Load(const std::filesystem::path& path){std::ifstream f(path);if(!f)throw std::runtime_error(path.string());nlohmann::json j;f>>j;return j;}
std::filesystem::path Root(){return std::filesystem::path(HV_TEST_PROJECT_ROOT)/"out/android-yolo/runtime-square320";}
struct Fake {
    int creates=0,runs=0,releases=0,completions=0;
    bool fail=false,malformed=false,missing_completion=false,completion_fail=false,throw_after_run=false;
    HV_GpuImageTransformV1 transform{};
    std::vector<float> detection=std::vector<float>(2100*65,-80),points=std::vector<float>(2100*51,0);
};
bool Complete(void* opaque,humanvision::gpu::ConsumerFrame& lease,bool final_role,std::string& error) noexcept {
    auto& self=*static_cast<Fake*>(opaque);++self.completions;
    EXPECT_TRUE(final_role);EXPECT_EQ(lease.role_owner,nullptr);EXPECT_EQ(lease.complete_role,nullptr);
    if(self.completion_fail){error="GPU release proof unavailable";return false;}
    lease.ncnn_role_complete=true;lease.claimed=false;return true;
}
HV_Result HV_CALL Run(void* opaque,const HV_GpuFrameRefV1* frame,const HV_GpuImageTransformV1* transform,
    HV_TensorViewV1* views,uint32_t capacity,uint32_t* count,HV_ErrorBufferV1*) {
    auto& self=*static_cast<Fake*>(opaque);++self.runs;self.transform=*transform;
    if(self.fail)return HV_ERR_INTERNAL;if(capacity<2)return HV_ERR_INVALID_ARGUMENT;
    for(int i=0;i<2;++i){auto& v=views[i];v={};v.struct_size=sizeof(v);v.api_version=HV_PLUGIN_API_V1;
        v.name=i?"out1":"out0";v.element_type=1;v.rank=2;v.dimensions[0]=self.detection.size()/65;v.dimensions[1]=i?51:65;
        const auto& data=i?self.points:self.detection;v.data=data.data();v.byte_count=data.size()*4;}
    auto& lease=*static_cast<humanvision::gpu::ConsumerFrame*>(frame->opaque_slot);
    lease.claimed=true;lease.ncnn_role_complete=false;
    if(!self.missing_completion){lease.role_owner=&self;lease.complete_role=Complete;}
    if(self.throw_after_run)throw std::runtime_error("Backend threw after acquiring role");
    if(self.malformed)--views[1].dimensions[0];
    *count=2;return HV_OK;
}
const HV_GpuBackendApiV2 backend{{sizeof(HV_GpuBackendApiV2),HV_GPU_FRAME_API_V1,nullptr,nullptr,Run,nullptr},nullptr};
HV_Result HV_CALL Create(void* context,const HV_GpuBackendConfigV1* config,const HV_GpuDeviceContextV1*,
    const HV_GpuBackendApiV2** api,void** out,HV_ErrorBufferV1*) {
    auto& self=*static_cast<Fake*>(context);++self.creates;
    const auto manifest=nlohmann::json::parse(config->model_manifest_utf8);
    EXPECT_EQ(manifest.at("active_role"),"body");*api=&backend;*out=context;return HV_OK;
}
void HV_CALL Release(void* context,const HV_GpuBackendApiV2*,void*){++static_cast<Fake*>(context)->releases;}
HV_PluginApiV3 Query(){HV_PluginApiV3 api{};api.v1.struct_size=sizeof(api);api.v1.api_version=HV_PLUGIN_API_V3;
    EXPECT_EQ(HV_QueryYoloGpuPipelineV3(HV_PLUGIN_API_V3,&api),HV_OK);return api;}
}
TEST(YoloContract, ExplicitGenericFp32OptionsRejectMissingOrIncompatibleFlags) {
    auto manifest=Load(Root()/"modelpacks/yolov8n-pose-square320-fp32-local/modelpack.json");
    const auto model=manifest.at("models").at(0);std::string error;
    humanvision::runtime::ncnn_backend::BackendOptions options{};
    ASSERT_TRUE(humanvision::runtime::ncnn_backend::ParseBackendOptions(model,options,error))<<error;
    EXPECT_TRUE(options.raw_tensor);EXPECT_TRUE(options.use_packing_layout);EXPECT_FALSE(options.use_fp16_arithmetic);
    for(const char* key:{"use_packing_layout","use_subgroup_ops","use_fp16_packed","use_fp16_storage","use_fp16_arithmetic"}) {
        auto changed=model;changed["backend_options"].erase(key);
        EXPECT_FALSE(humanvision::runtime::ncnn_backend::ParseBackendOptions(changed,options,error));
        changed=model;changed["backend_options"][key]=!changed["backend_options"][key].get<bool>();
        EXPECT_FALSE(humanvision::runtime::ncnn_backend::ParseBackendOptions(changed,options,error));
    }
}
TEST(YoloContract, DataOnlyLocalPacksAndProfileResolveWithoutFp16Claims) {
    using namespace humanvision::runtime;
    for(const char* size:{"320","416"}) {
        const auto root=std::filesystem::path(HV_TEST_PROJECT_ROOT)/(std::string("out/android-yolo/runtime-square")+size);
        const auto id=std::string("yolov8n-pose-square")+size+"-fp32-local";
        ModelPackManager packs(root/"modelpacks");std::string error;auto pack=packs.Resolve(id,error);ASSERT_TRUE(pack)<<error;
        EXPECT_EQ(pack->capabilities&(HV_CAP_FP16_STORAGE|HV_CAP_FP16_ARITHMETIC),0u);
        BackendFactory factory({},false);ASSERT_TRUE(factory.RegisterV3(HV_QueryNcnnVulkanPluginV3,error));
        ASSERT_TRUE(factory.RegisterV3(HV_QueryTopDownGpuPipelineV3,error));ASSERT_TRUE(factory.RegisterV3(HV_QueryYoloGpuPipelineV3,error));
        PluginRegistry registry;auto profile=ProfileManager(root/"profiles").Resolve("android-ncnn-vulkan",8,registry,packs,error,&factory);
        ASSERT_TRUE(profile)<<error;EXPECT_TRUE(profile->gpu_route);EXPECT_FALSE(profile->allow_backend_fallback);
        EXPECT_STREQ(profile->gpu_body->api.v1.plugin_id,"pipeline.yolo.pose");
    }
    const auto root=std::filesystem::path(HV_TEST_PROJECT_ROOT)/"out/android-yolo/runtime-rectangle640x384-arm-verified";
    ModelPackManager packs(root/"modelpacks");BackendFactory factory({},false);std::string error;
    ASSERT_TRUE(factory.RegisterV3(HV_QueryNcnnVulkanPluginV3,error));ASSERT_TRUE(factory.RegisterV3(HV_QueryYoloGpuPipelineV3,error));
    PluginRegistry registry;auto profile=ProfileManager(root/"profiles").Resolve("android-ncnn-vulkan",8,registry,packs,error,&factory);
    ASSERT_TRUE(profile)<<error;EXPECT_TRUE(profile->gpu_route);EXPECT_FALSE(profile->allow_backend_fallback);
}
TEST(YoloContract, GenericRawBoundaryRejectsProductionHalfAndPackedInput) {
    using namespace humanvision::runtime::ncnn_backend;
    BackendOptions options{false,false,true,true};InputContract input{};input.output_type=HV_GPU_TENSOR_FP32;
    input.output_elempack=1;input.cast_type_to=1;std::string error;
    ASSERT_TRUE(ValidateRawTensorBoundary(options,input,true,error));
    EXPECT_FALSE(ValidateRawTensorBoundary(options,input,false,error));
    input.output_type=HV_GPU_TENSOR_FP16;EXPECT_FALSE(ValidateRawTensorBoundary(options,input,true,error));
    input.output_type=HV_GPU_TENSOR_FP32;input.output_elempack=4;EXPECT_FALSE(ValidateRawTensorBoundary(options,input,true,error));
    input.output_elempack=1;input.cast_type_to=2;EXPECT_FALSE(ValidateRawTensorBoundary(options,input,true,error));
}
TEST(YoloGpuPipeline, ExplicitRectangleRestoresPixelsAndRejectsUnreviewedSourceBeforeBackend) {
    const auto root=std::filesystem::path(HV_TEST_PROJECT_ROOT)/"out/android-yolo/runtime-rectangle640x384-arm-verified";
    auto manifest=Load(root/"modelpacks/yolov8n-pose-rectangle640x384-fp32-local/modelpack.json");
    const auto profile=Load(root/"profiles/android-ncnn-vulkan.json").dump();
    Fake fake;fake.detection.assign(5040*65,-80);fake.points.assign(5040*51,0);
    // Last cell of each rectangular stride proves offsets use width*height.
    const int anchors[]={80*48-1,80*48+40*24-1,5040-1};
    for(int anchor:anchors)fake.detection[anchor*65+64]=8;
    auto api=Query();HV_HostServicesV3 host{};host.v2.v1.struct_size=sizeof(host);
    host.v2.v1.api_version=HV_PLUGIN_API_V1;host.v2.v1.context=&fake;
    host.create_gpu_backend_v3=Create;host.release_gpu_backend_v3=Release;
    const auto text=manifest.dump();HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,8,0,text.c_str(),"fixture",profile.c_str()};
    void* instance=nullptr;ASSERT_EQ(api.gpu_pipeline->create(&config,&host,&instance,nullptr),HV_OK);
    struct Cleanup{const HV_GpuPipelineApiV2* api;void* instance;~Cleanup(){api->destroy(instance);}}cleanup{api.gpu_pipeline,instance};
    humanvision::gpu::ConsumerFrame lease{};
    HV_GpuFrameRefV1 frame{sizeof(frame),HV_GPU_FRAME_API_V1,&lease,1024,768,10,10000,1,HV_GPU_IMAGE_RGBA8_UNORM,0};
    HV_GpuObservationFrameV3 out{};out.v1.struct_size=sizeof(out);out.v1.api_version=HV_PLUGIN_API_V1;
    char message[256]{};HV_ErrorBufferV1 error{sizeof(error),HV_PLUGIN_API_V1,message,sizeof(message)};
    EXPECT_EQ(api.gpu_pipeline->process_gpu(instance,&frame,&out.v1,&error),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(fake.creates,0);EXPECT_EQ(fake.runs,0);EXPECT_NE(std::string(message).find("16:9"),std::string::npos);
    frame.height=576;ASSERT_EQ(api.gpu_pipeline->process_gpu(instance,&frame,&out.v1,&error),HV_OK)<<message;
    EXPECT_EQ(fake.transform.output_width,640u);EXPECT_EQ(fake.transform.output_height,384u);
    EXPECT_FLOAT_EQ(fake.transform.source_rect_px.x,0);EXPECT_FLOAT_EQ(fake.transform.source_rect_px.y,-19.2f);
    EXPECT_FLOAT_EQ(fake.transform.source_rect_px.width,1024);EXPECT_FLOAT_EQ(fake.transform.source_rect_px.height,614.4f);
    EXPECT_EQ(fake.completions,1);EXPECT_FALSE(lease.claimed);EXPECT_TRUE(lease.ncnn_role_complete);
    EXPECT_GT(out.v1.body_count,0u);EXPECT_EQ(out.v1.source_frame_id,10);
    for(const int width:{624,1072,1232,1920}) {
        SCOPED_TRACE(width);frame.width=width;frame.height=width/16*9;++frame.frame_id;
        ASSERT_EQ(api.gpu_pipeline->process_gpu(instance,&frame,&out.v1,&error),HV_OK)<<message;
        const float scale=640.f/width;
        EXPECT_FLOAT_EQ(fake.transform.source_rect_px.y,-12/scale);
        EXPECT_FLOAT_EQ(fake.transform.source_rect_px.width,float(width));
        EXPECT_FLOAT_EQ(fake.transform.source_rect_px.height,384/scale);
    }
    // Explicit rectangular dimensions cannot be replaced by square640.
    manifest["models"][0]["input_contract"]["height"]=640;
    const auto invalid=manifest.dump();config.model_manifest_utf8=invalid.c_str();void* rejected=nullptr;
    EXPECT_EQ(api.gpu_pipeline->create(&config,&host,&rejected,&error),HV_ERR_MODEL_LOAD);EXPECT_EQ(rejected,nullptr);
}
TEST(YoloGpuPipeline, OneNetworkPerSourceFrameProvenanceAndInvalidHands) {
    const auto manifest=Load(Root()/"modelpacks/yolov8n-pose-square320-fp32-local/modelpack.json").dump();
    const auto profile=Load(Root()/"profiles/android-ncnn-vulkan.json").dump();
    Fake fake;fake.detection[820*65+64]=8;auto api=Query();HV_HostServicesV3 host{};
    host.v2.v1.struct_size=sizeof(host);host.v2.v1.api_version=HV_PLUGIN_API_V1;host.v2.v1.context=&fake;
    host.create_gpu_backend_v3=Create;host.release_gpu_backend_v3=Release;
    HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,8,0,manifest.c_str(),"fixture",profile.c_str()};
    void* instance=nullptr;ASSERT_EQ(api.gpu_pipeline->create(&config,&host,&instance,nullptr),HV_OK);
    HV_GpuFrameRefRegionV1 frame{};frame.v1.struct_size=sizeof(frame);frame.v1.api_version=HV_GPU_FRAME_API_V1;
    humanvision::gpu::ConsumerFrame lease{};frame.v1.opaque_slot=&lease;
    frame.v1.width=1024;frame.v1.height=576;frame.v1.generation=1;
    frame.v1.frame_id=10;frame.v1.timestamp_us=10000;
    HV_GpuObservationFrameV3 out{};out.v1.struct_size=sizeof(out);out.v1.api_version=HV_PLUGIN_API_V1;
    ASSERT_EQ(api.gpu_pipeline->process_gpu(instance,&frame.v1,&out.v1,nullptr),HV_OK);
    EXPECT_EQ(fake.creates,1);EXPECT_EQ(fake.runs,1);EXPECT_EQ(out.v1.body_count,1u);EXPECT_EQ(out.v1.hand_count,0u);
    EXPECT_EQ(out.v1.source_frame_id,10);EXPECT_EQ(out.v1.source_timestamp_us,10000);
    EXPECT_EQ(out.crop_track_ids[0],0);EXPECT_GT(out.detector_scores[0],.99f);
    EXPECT_FALSE(out.v1.bodies[0].joints[HV_CANONICAL_HAND_LEFT].valid);
    EXPECT_EQ(fake.transform.output_elempack,1u);EXPECT_EQ(fake.transform.output_type,HV_GPU_TENSOR_FP32);
    EXPECT_NEAR(fake.transform.source_rect_px.y,-(320-180)/2/.3125f,1e-4f);
    EXPECT_EQ(api.gpu_pipeline->process_gpu(instance,&frame.v1,&out.v1,nullptr),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(fake.runs,1);frame.v1.frame_id=11;frame.v1.timestamp_us=11000;fake.fail=true;
    EXPECT_EQ(api.gpu_pipeline->process_gpu(instance,&frame.v1,&out.v1,nullptr),HV_ERR_INTERNAL);EXPECT_EQ(out.v1.body_count,0u);
    fake.fail=false;std::fill(fake.detection.begin(),fake.detection.end(),-80.f);frame.v1.frame_id=12;
    EXPECT_EQ(api.gpu_pipeline->process_gpu(instance,&frame.v1,&out.v1,nullptr),HV_OK);EXPECT_EQ(out.v1.body_count,0u);
    frame.v1.generation=2;frame.v1.frame_id=13;
    EXPECT_EQ(api.gpu_pipeline->process_gpu(instance,&frame.v1,&out.v1,nullptr),HV_OK);EXPECT_EQ(fake.creates,2);EXPECT_EQ(fake.releases,1);
    frame.v1.frame_id=14;fake.malformed=true;
    EXPECT_EQ(api.gpu_pipeline->process_gpu(instance,&frame.v1,&out.v1,nullptr),HV_ERR_INTERNAL);EXPECT_EQ(out.v1.body_count,0u);
    frame.v1.frame_id=15;fake.malformed=false;
    BeginNativeAllocationProbe();
    const auto result=api.gpu_pipeline->process_gpu(instance,&frame.v1,&out.v1,nullptr);
    const auto allocations=EndNativeAllocationProbe();
    EXPECT_EQ(result,HV_OK);EXPECT_EQ(allocations,0u);
    api.gpu_pipeline->destroy(instance);EXPECT_EQ(fake.releases,2);
}
TEST(YoloGpuPipeline, CreationRejectsPrecisionGeometryAndDecoderDrift) {
    const auto manifest=Load(Root()/"modelpacks/yolov8n-pose-square320-fp32-local/modelpack.json");
    const auto profile=Load(Root()/"profiles/android-ncnn-vulkan.json");
    Fake fake;auto api=Query();HV_HostServicesV3 host{};host.v2.v1.struct_size=sizeof(host);
    host.v2.v1.api_version=HV_PLUGIN_API_V1;host.v2.v1.context=&fake;host.create_gpu_backend_v3=Create;host.release_gpu_backend_v3=Release;
    for(int mutation=0;mutation<10;++mutation) {
        auto changed=manifest;auto options=profile;
        switch(mutation) {
            case 0:changed["local_evaluation_only"]=false;break;
            case 1:options["local_evaluation_only"]=false;break;
            case 2:changed["models"][0]["decoder_id"]="simcc_body26_v1";break;
            case 3:changed["models"][0]["input_contract"]["tensor_dtype"]="fp16";break;
            case 4:changed["models"][0]["input_contract"]["width"]=640;break;
            case 5:changed["models"][0]["backend_options"]["use_fp16_storage"]=true;break;
            case 6:changed["models"][0]["input_contract"]["normalization"]["norm"][1]=1;break;
            case 7:changed["models"][0]["input_contract"]["pad_rgb"][0]=0;break;
            case 8:changed["models"][0]["output_contract"]["max_output_bytes"]["out0"]=1;break;
            case 9:changed["max_people"]=1;break;
        }
        const auto text=changed.dump(),option_text=options.dump();void* instance=nullptr;
        HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,8,0,text.c_str(),"fixture",option_text.c_str()};
        EXPECT_EQ(api.gpu_pipeline->create(&config,&host,&instance,nullptr),HV_ERR_MODEL_LOAD)<<mutation;
        EXPECT_EQ(instance,nullptr);EXPECT_EQ(fake.creates,0);
    }
}
TEST(YoloContract, LocalFp32ExceptionPreservesProductionFp16CapabilityGuard) {
    using namespace humanvision::runtime;
    const auto source=Root()/"modelpacks/yolov8n-pose-square320-fp32-local";
    const auto root=std::filesystem::temp_directory_path()/("hv-yolo-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{std::filesystem::path root;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}}cleanup{root};
    const auto target=root/"yolov8n-pose-square320-fp32-local";std::filesystem::create_directories(root);
    std::filesystem::copy(source,target,std::filesystem::copy_options::recursive);
    const auto original=Load(target/"modelpack.json");ModelPackManager packs(root);std::string error;
    for(int mutation=0;mutation<3;++mutation) {
        auto manifest=original;
        if(mutation==0)manifest["local_evaluation_only"]=false;
        if(mutation==1)manifest["execution_contract"]="unknown";
        if(mutation==2)manifest["capabilities"].push_back("fp16-storage");
        {std::ofstream output(target/"modelpack.json");output<<manifest.dump();}
        EXPECT_FALSE(packs.Resolve("yolov8n-pose-square320-fp32-local",error))<<mutation;
    }
}

// Exercise the production one-shot ConsumerFrame dispatcher; only device work is doubled.
TEST(YoloGpuPipeline, SuccessRetiresFinalRoleAndCompletionFailureRejectsPublication) {
    const auto manifest=Load(Root()/"modelpacks/yolov8n-pose-square320-fp32-local/modelpack.json").dump();
    const auto profile=Load(Root()/"profiles/android-ncnn-vulkan.json").dump();
    for(int scenario=0;scenario<7;++scenario) {
        SCOPED_TRACE(scenario);
        Fake fake;fake.detection[820*65+64]=8;
        fake.missing_completion=scenario==2;fake.completion_fail=scenario==3;
        fake.malformed=scenario==4;fake.fail=scenario==5;fake.throw_after_run=scenario==6;
        if(scenario==1)std::fill(fake.detection.begin(),fake.detection.end(),-80.f);
        auto api=Query();HV_HostServicesV3 host{};host.v2.v1.struct_size=sizeof(host);
        host.v2.v1.api_version=HV_PLUGIN_API_V1;host.v2.v1.context=&fake;
        host.create_gpu_backend_v3=Create;host.release_gpu_backend_v3=Release;
        HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,8,0,manifest.c_str(),"fixture",profile.c_str()};
        void* instance=nullptr;ASSERT_EQ(api.gpu_pipeline->create(&config,&host,&instance,nullptr),HV_OK);
        struct Cleanup { const HV_GpuPipelineApiV2* api;void* instance;~Cleanup(){api->destroy(instance);} } cleanup{api.gpu_pipeline,instance};
        humanvision::gpu::ConsumerFrame lease{};lease.claimed=true;
        HV_GpuFrameRefV1 frame{sizeof(frame),HV_GPU_FRAME_API_V1,&lease,1024,576,10,10000,1,HV_GPU_IMAGE_RGBA8_UNORM,0};
        HV_GpuObservationFrameV3 out{};out.v1.struct_size=sizeof(out);out.v1.api_version=HV_PLUGIN_API_V1;
        char text[256]{};HV_ErrorBufferV1 error{sizeof(error),HV_PLUGIN_API_V1,text,sizeof(text)};
        const auto status=api.gpu_pipeline->process_gpu(instance,&frame,&out.v1,&error);
        if(scenario<2) {
            EXPECT_EQ(status,HV_OK);EXPECT_EQ(out.v1.body_count,scenario==0?1u:0u);
            EXPECT_EQ(fake.completions,1);EXPECT_FALSE(lease.claimed);EXPECT_TRUE(lease.ncnn_role_complete);
            EXPECT_EQ(lease.role_owner,nullptr);EXPECT_EQ(lease.complete_role,nullptr);
        }else {
            EXPECT_EQ(status,HV_ERR_INTERNAL);EXPECT_EQ(out.v1.body_count,0u);EXPECT_TRUE(lease.claimed);
            EXPECT_FALSE(lease.ncnn_role_complete);if(scenario!=5)EXPECT_NE(text[0],0);
            EXPECT_EQ(fake.completions,scenario==3?1:0);
            // Decode/exception errors retain the callback for the host error drain.
            EXPECT_EQ(lease.role_owner,scenario==4||scenario==6?&fake:nullptr);
            EXPECT_EQ(lease.complete_role,scenario==4||scenario==6?Complete:nullptr);
            if(scenario==4||scenario==6) {
                std::string reason;EXPECT_TRUE(humanvision::gpu::CompleteGpuRole(lease,true,reason));
                EXPECT_FALSE(lease.claimed);
            }
        }
    }
}
