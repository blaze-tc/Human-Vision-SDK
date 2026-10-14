#include <gtest/gtest.h>
#include "plugins/pipeline/yolo/yolo_tensor_pipeline.h"
#include "plugins/pipeline/yolo/yolo_decoder.h"
#include "plugins/backend/ort/ort_plugin.h"
#include "common/pipeline_diagnostics.h"
#include "json/json.hpp"
#include "picosha2/picosha2.h"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cmath>
#include <limits>
#include <thread>

void BeginNativeAllocationProbe() noexcept;
std::size_t EndNativeAllocationProbe() noexcept;
namespace {
using Json=nlohmann::json;
const auto project=std::filesystem::path(HV_TEST_PROJECT_ROOT);
const auto root=project/"out/rknn-validation-20261009";
Json ReadJson(const std::filesystem::path& p){std::ifstream f(p);if(!f)throw std::runtime_error("Missing pinned fixture: "+p.string());Json j;f>>j;return j;}
template<class T> std::vector<T> Read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error(p.string());const auto n=static_cast<size_t>(f.tellg());if(n%sizeof(T))throw std::runtime_error("Invalid fixture bytes");std::vector<T> v(n/sizeof(T));f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),n);return v;}
std::string Hash(const std::filesystem::path& p){auto v=Read<uint8_t>(p);return picosha2::hash256_hex_string(v);}
HV_TensorViewV1 View(const char* name,const std::vector<float>& v,int columns){HV_TensorViewV1 t{};t.struct_size=sizeof(t);t.api_version=1;t.name=name;t.element_type=1;t.rank=3;t.dimensions[0]=1;t.dimensions[1]=3024;t.dimensions[2]=columns;t.data=v.data();t.byte_count=v.size()*4;return t;}
struct RecordedBackend {
    int creates=0,runs=0,releases=0;bool fail_create=false,partial_failure=false,invalid_api=false,fail_run=false;
    int malformed=0;
    std::string provider,options,model;
    std::vector<float> det,points;
    std::vector<uint8_t> captured=std::vector<uint8_t>(512*288*3);
    HV_TensorViewV1 input{};
    void Fixture(const char* name){det=Read<float>(root/"non-quantized/simulator"/(std::string(name)+"-out0.fp32"));points=Read<float>(root/"non-quantized/simulator"/(std::string(name)+"-out1.fp32"));}
};
HV_Result HV_CALL Run(void* p,const HV_TensorViewV1* in,uint32_t inputs,HV_TensorViewV1* out,uint32_t capacity,uint32_t* count,HV_ErrorBufferV1*){
    auto& s=*static_cast<RecordedBackend*>(p);++s.runs;if(s.fail_run)return HV_ERR_INTERNAL;
    if(!in||inputs!=1||capacity<2)return HV_ERR_INVALID_ARGUMENT;s.input=*in;
    if(in->byte_count!=s.captured.size()||!in->data)return HV_ERR_INVALID_ARGUMENT;
    std::memcpy(s.captured.data(),in->data,s.captured.size());
    out[0]=View("out0",s.det,65);out[1]=View("out1",s.points,51);*count=2;
    if(s.malformed==1)out[1].rank=2;
    if(s.malformed==2)--out[0].dimensions[1];
    if(s.malformed==3)out[0].element_type=3;
    if(s.malformed==4)--out[1].byte_count;
    if(s.malformed==5)out[1].name="out0";
    if(s.malformed==6)*count=3;
    return HV_OK;
}
const HV_BackendApiV1 backend{sizeof(HV_BackendApiV1),1,nullptr,nullptr,Run,nullptr};
const HV_BackendApiV1 bad_backend{sizeof(HV_BackendApiV1),1,nullptr,nullptr,nullptr,nullptr};
HV_Result HV_CALL CreateBackend(void* p,const HV_BackendConfigV1* c,const HV_BackendApiV1** api,void** session,HV_ErrorBufferV1*){
    auto& s=*static_cast<RecordedBackend*>(p);++s.creates;s.provider=c->requested_provider_utf8?c->requested_provider_utf8:"";s.options=c->options_utf8?c->options_utf8:"";s.model=c->model_path_utf8?c->model_path_utf8:"";
    if(s.fail_create){if(s.partial_failure){*api=&backend;*session=p;}return HV_ERR_MODEL_LOAD;}
    *api=s.invalid_api?&bad_backend:&backend;*session=p;return HV_OK;
}
void HV_CALL ReleaseBackend(void* p,const HV_BackendApiV1*,void*){++static_cast<RecordedBackend*>(p)->releases;}
struct Pipeline {
    HV_PluginApiV1 api{sizeof(HV_PluginApiV1),1};RecordedBackend recorded;void* instance=nullptr;
    char message[1024]{};HV_ErrorBufferV1 error{sizeof(error),1,message,sizeof(message)};
    Json manifest=ReadJson(root/"tensor-pipeline-manifest.json");
    Json profile=ReadJson(project/"tools/models/rknn/templates/android-rknn-npu-quality-low.json");
    std::vector<HV_BodyObservationV1> bodies=std::vector<HV_BodyObservationV1>(8);
    HV_PipelineOutputV1 output{sizeof(output),1,bodies.data(),8};
    Pipeline(){if(HV_QueryYoloTensorPipelineV1(1,&api)!=HV_OK)throw std::runtime_error("Query failed");recorded.Fixture("seven");}
    ~Pipeline(){if(instance)api.pipeline->destroy(instance);}
    HV_Result Create(int max=8){const auto m=manifest.dump(),p=profile.dump(),r=root.u8string();HV_PipelineConfigV1 config{sizeof(config),1,max,0,m.c_str(),r.c_str(),p.c_str()};HV_HostServicesV1 h{sizeof(h),1,&recorded,CreateBackend,ReleaseBackend};return api.pipeline->create(&config,&h,&instance,&error);}
    HV_Result Process(const std::vector<uint8_t>& pixels,int width=1024,int height=576,HV_PixelFormat format=HV_PIXEL_RGB24,int stride=0){const int bpp=(format==HV_PIXEL_RGBA32||format==HV_PIXEL_BGRA32)?4:3;HV_PipelineInputV1 in{};in.struct_size=sizeof(in);in.api_version=1;in.frame={sizeof(HV_VideoFrame),width,height,stride?stride:width*bpp,format,42,1234567,pixels.data(),static_cast<int32_t>(pixels.size())};return api.pipeline->process(instance,&in,&output,&error);}
};
Json CpuManifest() {
    return {{"schema_version",1},{"pack_id","yolov8n-pose-512x288-ort"},{"pack_version","1.0.0"},
        {"pipeline_id","pipeline.yolo.cpu"},{"max_people",8},{"capabilities",{"body_pose","multi_person"}},
        {"models",Json::array({{{"role","body"},{"format","onnx"},{"asset_path","recovered-low.onnx"},
            {"sha256","6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3"},
            {"decoder_id","yolov8_pose_dfl17_v1"},{"source","Pinned local YOLOv8 pose ONNX"},
            {"license","Ultralytics AGPL-3.0 or applicable commercial license"},
            {"input_contract",{{"width",512},{"height",288},{"tensor_layout","NCHW"},
                {"tensor_dtype","fp32"},{"color_order","RGB"},{"input_blob","in0"},
                {"crop_mode","letterbox"},{"pad_rgb",{114,114,114}},
                {"normalization",{{"mean",{0,0,0}},{"norm",{1./255.,1./255.,1./255.}}}}}},
            {"output_contract",{{"decoder","yolov8_pose_dfl17_v1"},{"output_blobs",{"out0","out1"}},
                {"tensor_dtype","fp32"},{"rows",3024},{"columns",{65,51}},
                {"max_output_bytes",{{"out0",3024*65*4},{"out1",3024*51*4}}}}}}})}};
}
Json CpuProfile() {
    return {{"schema_version",1},{"profile","comparison-yolo-cpu"},
        {"body",{{"pipeline","pipeline.yolo.cpu"},{"modelPack","yolov8n-pose-512x288-ort"}}},
        {"hands",{{"enabled",false},{"fps",15}}},{"body_fps",30},{"output",{{"hz",60}}},
        {"required_capabilities",{"body_pose","multi_person","tensor_inference"}},
        {"backend",{{"preference",{"backend.ort.cpu"}},{"allow_fallback",false}}}};
}
void CpuVariant(Pipeline& p,const char* fixture="seven") {
    ASSERT_EQ(HV_QueryYoloCpuPipelineV1(1,&p.api),HV_OK);
    p.manifest=CpuManifest();p.profile=CpuProfile();
    p.recorded.det=Read<float>(root/"onnx-gate"/(std::string(fixture)+"-out0.fp32"));
    p.recorded.points=Read<float>(root/"onnx-gate"/(std::string(fixture)+"-out1.fp32"));
    p.recorded.captured.resize(512*288*3*sizeof(float));
}
std::vector<float> CapturedPlanar(const Pipeline& p) {
    std::vector<float> result(p.recorded.captured.size()/sizeof(float));
    std::memcpy(result.data(),p.recorded.captured.data(),p.recorded.captured.size());return result;
}
struct RealCpuBackend {
    HV_PluginApiV1 plugin{sizeof(plugin),1};void* session=nullptr;
    HV_TensorViewV1 outputs[2]{};uint32_t count=0;int creates=0,runs=0,releases=0;
    std::vector<float> input=std::vector<float>(512*288*3);
};
HV_Result HV_CALL RunRealCpu(void* p,const HV_TensorViewV1* in,uint32_t inputs,HV_TensorViewV1* out,uint32_t capacity,uint32_t* count,HV_ErrorBufferV1* error) {
    auto& s=*static_cast<RealCpuBackend*>(p);++s.runs;
    if(inputs!=1||in->byte_count!=s.input.size()*sizeof(float))return HV_ERR_INVALID_ARGUMENT;
    std::memcpy(s.input.data(),in->data,in->byte_count);
    const auto result=s.plugin.backend->run(s.session,in,inputs,out,capacity,count,error);
    if(result==HV_OK&&*count==2){s.outputs[0]=out[0];s.outputs[1]=out[1];s.count=*count;}
    return result;
}
const HV_BackendApiV1 real_cpu_api{sizeof(real_cpu_api),1,nullptr,nullptr,RunRealCpu,nullptr};
HV_Result HV_CALL CreateRealCpu(void* p,const HV_BackendConfigV1* config,const HV_BackendApiV1** api,void** session,HV_ErrorBufferV1* error) {
    auto& s=*static_cast<RealCpuBackend*>(p);++s.creates;
    if(!config->requested_provider_utf8||std::strcmp(config->requested_provider_utf8,"backend.ort.cpu"))return HV_ERR_INVALID_ARGUMENT;
    auto status=HV_QueryOrtCpuPlugin(1,&s.plugin);if(status!=HV_OK)return status;
    status=s.plugin.backend->create(config,&s.session,error);if(status!=HV_OK)return status;
    HV_BackendSessionInfoV1 info{sizeof(info),1};
    EXPECT_EQ(s.plugin.backend->session_info(s.session,&info),HV_OK);
    EXPECT_STREQ(info.actual,"CPU");EXPECT_EQ(info.accelerated,0u);EXPECT_STREQ(info.fallback_reason,"");
    *api=&real_cpu_api;*session=p;return HV_OK;
}
void HV_CALL ReleaseRealCpu(void* p,const HV_BackendApiV1*,void*) {
    auto& s=*static_cast<RealCpuBackend*>(p);++s.releases;s.plugin.backend->destroy(s.session);s.session=nullptr;
}
HV_Result CreateReal(Pipeline& p,RealCpuBackend& real,int capacity=8) {
    const auto m=p.manifest.dump(),profile=p.profile.dump(),r=root.u8string();
    HV_PipelineConfigV1 config{sizeof(config),1,capacity,0,m.c_str(),r.c_str(),profile.c_str()};
    HV_HostServicesV1 host{sizeof(host),1,&real,CreateRealCpu,ReleaseRealCpu};
    return p.api.pipeline->create(&config,&host,&p.instance,&p.error);
}
}

TEST(YoloCpuPipeline, RuntimeCompositionRegistersExplicitCpuEntry) {
    // Isolated ignored root: acceptance does not change shipping profiles/packs.
    const auto runtime=project/"out/yolo-cpu-pipeline-tests";
    const auto pack=runtime/"modelpacks/yolov8n-pose-512x288-ort";
    std::filesystem::create_directories(pack);
    std::filesystem::create_directories(runtime/"profiles");
    std::filesystem::copy_file(root/"recovered-low.onnx",pack/"recovered-low.onnx",
                              std::filesystem::copy_options::overwrite_existing);
    std::ofstream(pack/"manifest.json")<<CpuManifest().dump(2);
    std::ofstream(runtime/"profiles/comparison-yolo-cpu.json")<<CpuProfile().dump(2);
    const auto assets=runtime.u8string();
    HV_RuntimeConfigV1 config{sizeof(config),HV_API_VERSION_040,assets.c_str(),"comparison-yolo-cpu",8,0};
    HV_RuntimeHandle handle=nullptr;char error[1024]{};
    const auto status=HV_RuntimeCreate(&config,&handle,error,sizeof(error));
    struct Cleanup {HV_RuntimeHandle handle;~Cleanup(){if(handle)HV_RuntimeDestroy(handle);}} cleanup{handle};
    ASSERT_EQ(status,HV_OK)<<error;
    char diagnostics[8192]{};
    ASSERT_EQ(HV_RuntimeGetDiagnostics(handle,diagnostics,sizeof(diagnostics)),HV_OK);
    const std::string text=diagnostics;
    for(const char* field:{"Pipeline=pipeline.yolo.cpu","Requested backend=CPU","Actual backend=CPU","Hands enabled=false"})
        EXPECT_NE(text.find(field),std::string::npos)<<field<<"\n"<<text;
    const auto pixels=Read<uint8_t>(root/"seven/source.rgb");const auto timestamp=HV_RuntimeClockUs();
    HV_VideoFrame frame{sizeof(frame),1024,576,3072,HV_PIXEL_RGB24,42,timestamp,pixels.data(),int32_t(pixels.size())};
    ASSERT_EQ(HV_RuntimeSubmit(handle,&frame),HV_OK);
    HV_CanonicalBodyV1 bodies[8]{};uint32_t count=0;HV_RuntimeStatsV1 stats{sizeof(stats),HV_API_VERSION_040};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!stats.body_sequence&&std::chrono::steady_clock::now()<deadline){
        ASSERT_EQ(HV_RuntimeCopy(handle,0,bodies,8,&count,&stats),HV_OK);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    ASSERT_EQ(count,7u);EXPECT_EQ(stats.source_frame_id,42);EXPECT_EQ(stats.source_timestamp_us,timestamp);
    for(uint32_t i=0;i<count;++i){EXPECT_EQ(bodies[i].source_frame_id,42);EXPECT_EQ(bodies[i].observation_timestamp_us,timestamp);
        EXPECT_EQ(bodies[i].joints[HV_CANONICAL_NOSE].observation_timestamp_us,timestamp);
        EXPECT_EQ(bodies[i].joints[HV_CANONICAL_HAND_LEFT].valid,0);}
}

TEST(YoloCpuPipeline, CpuOnlyAbiIdentityAndExplicitAndroidSelection) {
    HV_PluginApiV1 query{sizeof(query),1};ASSERT_EQ(HV_QueryYoloCpuPipelineV1(1,&query),HV_OK);
    EXPECT_STREQ(query.plugin_id,"pipeline.yolo.cpu");EXPECT_EQ(query.max_people,8u);
    EXPECT_EQ(query.capabilities,HV_CAP_BODY_POSE|HV_CAP_MULTI_PERSON);
    EXPECT_EQ(query.type,HV_PLUGIN_PIPELINE);ASSERT_NE(query.pipeline,nullptr);
    EXPECT_EQ(HV_QueryYoloCpuPipelineV1(2,&query),HV_ERR_INVALID_ARGUMENT);
    query.struct_size=sizeof(query)-1;EXPECT_EQ(HV_QueryYoloCpuPipelineV1(1,&query),HV_ERR_INVALID_ARGUMENT);
    query.struct_size=sizeof(query);query.api_version=0;EXPECT_EQ(HV_QueryYoloCpuPipelineV1(1,&query),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(HV_QueryYoloCpuPipelineV1(1,nullptr),HV_ERR_INVALID_ARGUMENT);
    for(const char* name:{"comparison-yolo-cpu","android-cpu-nohands"}) {
        Pipeline p;CpuVariant(p);p.profile["profile"]=name;ASSERT_EQ(p.Create(),HV_OK)<<p.message;
        EXPECT_EQ(p.recorded.provider,"backend.ort.cpu");EXPECT_EQ(Json::parse(p.recorded.options),Json::object());
        EXPECT_EQ(std::filesystem::u8path(p.recorded.model),std::filesystem::canonical(root/"recovered-low.onnx"));
        EXPECT_EQ(p.recorded.creates,1);EXPECT_EQ(p.recorded.runs,0);
        p.api.pipeline->destroy(p.instance);p.instance=nullptr;EXPECT_EQ(p.recorded.releases,1);
    }
}
TEST(YoloCpuPipeline, RejectsWindowsAliasesFallbackOtherProvidersAndHandsBeforeBackendCreation) {
    for(int bad=0;bad<13;++bad) {Pipeline p;CpuVariant(p);SCOPED_TRACE(bad);
        switch(bad){case 0:p.profile["profile"]="windows-pc-cpu";break;
            case 1:p.profile["profile"]="windows-pc-directml";p.profile["backend"]["preference"]={"backend.ort.directml"};break;
            case 2:p.profile["backend"]["preference"]={"backend.ort.directml"};break;
            case 3:p.profile["backend"]["preference"]={"backend.rknn"};break;
            case 4:p.profile["backend"]["preference"]={"backend.ort.cpu","backend.ort.directml"};break;
            case 5:p.profile["backend"]["allow_fallback"]=true;break;
            case 6:p.profile["hands"]["enabled"]=true;break;case 7:p.profile.erase("hands");break;
            case 8:p.profile["body"]["pipeline"]="pipeline.yolo.tensor";break;
            case 9:p.profile["body"]["modelPack"]="unqualified-yolo";break;
            case 10:p.profile["profile"]="android-cpu-nohands-alias";break;
            case 11:p.profile["schema_version"]=2;break;case 12:p.profile["backend"]["preference"]={"auto"};break;}
        EXPECT_EQ(p.Create(),HV_ERR_MODEL_LOAD)<<p.message;EXPECT_EQ(p.recorded.creates,0);EXPECT_EQ(p.instance,nullptr);
    }
}
TEST(YoloCpuPipeline, RejectsUnpinnedModelNormalizationPathsAndOutputContracts) {
    for(int bad=0;bad<23;++bad) {Pipeline p;CpuVariant(p);auto& m=p.manifest["models"][0];SCOPED_TRACE(bad);
        switch(bad){case 0:m["sha256"]=std::string(64,'0');break;case 1:m["asset_path"]="../recovered-low.onnx";break;
            case 2:m["asset_path"]="missing.onnx";break;case 3:m["asset_path"]="index.json";break;
            case 4:m["input_contract"]["normalization"]["norm"]={1,1,1};break;
            case 5:m["input_contract"]["normalization"]["mean"]={1,0,0};break;
            case 6:m["input_contract"]["tensor_layout"]="NHWC";break;case 7:m["input_contract"]["tensor_dtype"]="uint8";break;
            case 8:m["input_contract"]["color_order"]="BGR";break;case 9:m["input_contract"]["width"]=640;break;
            case 10:m["input_contract"]["height"]=320;break;case 11:m["input_contract"]["crop_mode"]="stretch";break;
            case 12:m["input_contract"]["pad_rgb"]={0,0,0};break;case 13:m["input_contract"]["input_blob"]="images";break;
            case 14:m["output_contract"]["rows"]=3023;break;case 15:m["output_contract"]["columns"]={65,52};break;
            case 16:m["output_contract"]["tensor_dtype"]="fp16";break;case 17:m["output_contract"]["output_blobs"]={"out1","out0"};break;
            case 18:m["output_contract"]["max_output_bytes"]["out0"]=3024*65*4+4;break;
            case 19:m["decoder_id"]="simcc_v1";break;case 20:p.manifest["max_people"]=4;break;
            case 21:p.manifest["pipeline_id"]="pipeline.yolo.tensor";break;
            case 22:m["backend_options"]={{"input_transform",{{"norm",{1,1,1}}}}};break;}
        EXPECT_EQ(p.Create(),HV_ERR_MODEL_LOAD)<<p.message;EXPECT_EQ(p.recorded.creates,0);EXPECT_EQ(p.instance,nullptr);
    }
}
TEST(YoloCpuPipeline, RealRecordedPlanarOracleAndZeroPipelineFrameAllocations) {
    const auto pixels=Read<uint8_t>(root/"seven/source.rgb");const auto oracle=Read<float>(root/"seven/input.fp32");
    Pipeline p;CpuVariant(p);ASSERT_EQ(p.Create(),HV_OK)<<p.message;ASSERT_EQ(p.Process(pixels),HV_OK)<<p.message;
    EXPECT_EQ(p.recorded.input.element_type,1u);EXPECT_EQ(p.recorded.input.rank,4u);EXPECT_STREQ(p.recorded.input.name,"in0");
    EXPECT_EQ(p.recorded.input.dimensions[0],1);EXPECT_EQ(p.recorded.input.dimensions[1],3);
    EXPECT_EQ(p.recorded.input.dimensions[2],288);EXPECT_EQ(p.recorded.input.dimensions[3],512);
    const auto actual=CapturedPlanar(p);ASSERT_EQ(actual.size(),oracle.size());
    for(size_t i=0;i<actual.size();++i)ASSERT_FLOAT_EQ(actual[i],oracle[i])<<i;
    EXPECT_EQ(p.output.body_count,7u);EXPECT_EQ(p.output.hand_count,0u);
    BeginNativeAllocationProbe();const auto result=p.Process(pixels);const auto allocations=EndNativeAllocationProbe();
    EXPECT_EQ(result,HV_OK);EXPECT_EQ(allocations,0u);
}
TEST(YoloCpuPipeline, AllFourFormatsAlphaPaddingAndChangingResizeUseIdenticalRgbRounding) {
    Pipeline p;CpuVariant(p,"empty");ASSERT_EQ(p.Create(),HV_OK)<<p.message;
    struct Oracle {int width,height;const char* sha;};
    const Oracle oracles[]={{512,288,"2cc61dab4f0895e336f8d8d27ab763cb8bef82cb5f8a5489d6f114af06a54b37"},
        {80,45,"362ec089fab9e81c4eb45ea7f4794f55ce721008b0e85883e25361f773bab6f5"},
        {1600,900,"c298baecb6080fa3b6b70550b1c4dacea4670a77322e365a12334a184ad1dacb"}};
    for(const auto& oracle:oracles)for(auto format:{HV_PIXEL_RGB24,HV_PIXEL_BGR24,HV_PIXEL_RGBA32,HV_PIXEL_BGRA32}) {
        SCOPED_TRACE(oracle.width);
        SCOPED_TRACE(format);const int channels=format==HV_PIXEL_RGBA32||format==HV_PIXEL_BGRA32?4:3;
        const bool rgb=format==HV_PIXEL_RGB24||format==HV_PIXEL_RGBA32;const int stride=oracle.width*channels+13;
        std::vector<uint8_t> pixels(size_t(stride)*oracle.height,253);
        for(int y=0;y<oracle.height;++y)for(int x=0;x<oracle.width;++x){auto* px=pixels.data()+size_t(y)*stride+x*channels;
            px[rgb?0:2]=uint8_t((x*19+y*7)%256);px[1]=uint8_t((x*11+y*37)%256);px[rgb?2:0]=uint8_t((x*43+y*13)%256);if(channels==4)px[3]=3;}
        BeginNativeAllocationProbe();const auto result=p.Process(pixels,oracle.width,oracle.height,format,stride);const auto allocations=EndNativeAllocationProbe();
        ASSERT_EQ(result,HV_OK)<<p.message;EXPECT_EQ(allocations,0u);
        const auto actual=CapturedPlanar(p);std::vector<uint8_t> resized(512*288*3);
        for(size_t pixel=0;pixel<512*288;++pixel)for(size_t c=0;c<3;++c){const auto value=uint8_t(std::lround(actual[c*512*288+pixel]*255.f));
            EXPECT_FLOAT_EQ(actual[c*512*288+pixel],value/255.f);resized[pixel*3+c]=value;}
        EXPECT_EQ(picosha2::hash256_hex_string(resized),oracle.sha);
    }
}
TEST(YoloCpuPipeline, AcceptsEveryCapacityOneThroughEightAndClearsBackendFailure) {
    const auto pixels=Read<uint8_t>(root/"seven/source.rgb");
    for(int capacity=1;capacity<=8;++capacity){Pipeline p;CpuVariant(p);ASSERT_EQ(p.Create(capacity),HV_OK)<<p.message;
        p.output.body_capacity=capacity;ASSERT_EQ(p.Process(pixels),HV_OK)<<p.message;EXPECT_EQ(p.output.body_count,uint32_t(std::min(capacity,7)));
        p.recorded.fail_run=true;EXPECT_EQ(p.Process(pixels),HV_ERR_INTERNAL);EXPECT_EQ(p.output.body_count,0u);EXPECT_EQ(p.output.hand_count,0u);}
    for(int capacity:{0,9}){Pipeline p;CpuVariant(p);EXPECT_EQ(p.Create(capacity),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(p.recorded.creates,0);}
}
TEST(YoloCpuPipeline, RejectsMalformedBorrowedOutputsNonfiniteValuesAndInvalidFrames) {
    Pipeline p;CpuVariant(p);ASSERT_EQ(p.Create(),HV_OK)<<p.message;auto pixels=Read<uint8_t>(root/"seven/source.rgb");
    ASSERT_EQ(p.Process(pixels),HV_OK);for(int bad=1;bad<=6;++bad){p.recorded.malformed=bad;EXPECT_EQ(p.Process(pixels),HV_ERR_INTERNAL)<<bad;
        EXPECT_EQ(p.output.body_count,0u);EXPECT_EQ(p.output.hand_count,0u);}p.recorded.malformed=0;
    p.recorded.points.back()=std::numeric_limits<float>::quiet_NaN();EXPECT_EQ(p.Process(pixels),HV_ERR_INTERNAL);EXPECT_EQ(p.output.body_count,0u);
    const auto runs=p.recorded.runs;EXPECT_EQ(p.Process(pixels,1024,576,HV_PIXEL_RGB24,3071),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(p.Process(pixels,1024,576,static_cast<HV_PixelFormat>(999)),HV_ERR_UNSUPPORTED_FORMAT);
    pixels.resize(pixels.size()-1);EXPECT_EQ(p.Process(pixels),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(p.recorded.runs,runs);
}
TEST(YoloCpuPipeline, CleansPartialBackendAndInvalidApiWithoutLeaks) {
    for(int which=0;which<3;++which){Pipeline p;CpuVariant(p);p.recorded.fail_create=which<2;p.recorded.partial_failure=which==1;p.recorded.invalid_api=which==2;
        EXPECT_NE(p.Create(),HV_OK);EXPECT_EQ(p.instance,nullptr);EXPECT_EQ(p.recorded.releases,which==0?0:1);}
}
TEST(YoloCpuPipeline, ActualPinnedOnnxCpuOutputsAndCanonicalDecodingSevenOneEmpty) {
    RealCpuBackend real;Pipeline p;CpuVariant(p);ASSERT_EQ(CreateReal(p,real),HV_OK)<<p.message;
    const auto receipt=ReadJson(root/"onnx-gate/comparison.json");const char* names[]={"seven","one","empty"};
    for(int k=0;k<3;++k){SCOPED_TRACE(names[k]);const auto& record=receipt["fixtures"][k];
        const auto pixels=Read<uint8_t>(root/names[k]/"source.rgb");const auto expected=Read<float>(root/names[k]/"input.fp32");
        ASSERT_EQ(p.Process(pixels),HV_OK)<<p.message;EXPECT_EQ(p.output.body_count,record["expected_people"]);EXPECT_EQ(p.output.hand_count,0u);
        for(size_t i=0;i<expected.size();++i)ASSERT_FLOAT_EQ(real.input[i],expected[i])<<i;
        ASSERT_EQ(real.count,2u);
        for(int t=0;t<2;++t){const auto file=root/"onnx-gate"/(std::string(names[k])+"-out"+char('0'+t)+".fp32");
            EXPECT_EQ(Hash(file),record["output_sha256"][t?"out1":"out0"]);const auto values=Read<float>(file);
            ASSERT_EQ(real.outputs[t].byte_count,values.size()*sizeof(float));const auto* actual=static_cast<const float*>(real.outputs[t].data);
            double maximum=0,mean=0;for(size_t i=0;i<values.size();++i){ASSERT_TRUE(std::isfinite(actual[i]));const double delta=std::abs(double(actual[i])-values[i]);maximum=std::max(maximum,delta);mean+=delta;}
            EXPECT_LT(maximum,.002);EXPECT_LT(mean/values.size(),.00002);}
        for(uint32_t b=0;b<p.output.body_count;++b){const auto& actual=p.bodies[b];const auto& expected_body=record["actual"][b];
            EXPECT_NEAR(actual.bbox_px.x,expected_body["box"][0].get<float>(),.002);
            EXPECT_NEAR(actual.bbox_px.y,expected_body["box"][1].get<float>(),.002);
            EXPECT_NEAR(actual.confidence,expected_body["score"].get<float>(),.00001);
            for(int j=0;j<17;++j){const auto& joint=actual.joints[humanvision::runtime::yolo::CanonicalIndex(j)];
                EXPECT_NEAR(joint.x_px,expected_body["joints"][j][0].get<float>(),.002);EXPECT_NEAR(joint.y_px,expected_body["joints"][j][1].get<float>(),.002);
                EXPECT_EQ(joint.observation_timestamp_us,1234567);EXPECT_EQ(joint.prediction_ms,0u);}
            EXPECT_EQ(actual.joints[HV_CANONICAL_HAND_LEFT].valid,0);EXPECT_EQ(actual.joints[HV_CANONICAL_HANDTIP_RIGHT].valid,0);EXPECT_EQ(actual.joints[HV_CANONICAL_THUMB_RIGHT].valid,0);}
    }
    p.api.pipeline->destroy(p.instance);p.instance=nullptr;EXPECT_EQ(real.creates,1);EXPECT_EQ(real.runs,3);EXPECT_EQ(real.releases,1);
}
TEST(YoloCpuPipeline, LetterboxesNonLandscapeSourceWithoutChangingNpuAdmission) {
    Pipeline cpu;CpuVariant(cpu,"empty");ASSERT_EQ(cpu.Create(),HV_OK)<<cpu.message;
    Pipeline npu;npu.recorded.Fixture("empty");ASSERT_EQ(npu.Create(),HV_OK)<<npu.message;
    std::vector<uint8_t> pixels(640*480*3,37);
    ASSERT_EQ(cpu.Process(pixels,640,480),HV_OK)<<cpu.message;
    const auto actual=CapturedPlanar(cpu);const int left=64;
    for(int c=0;c<3;++c)for(int y=0;y<288;++y)for(int x=0;x<512;++x)
        ASSERT_FLOAT_EQ(actual[c*512*288+y*512+x],(x<left||x>=left+384?114:37)/255.f);
    EXPECT_EQ(npu.Process(pixels,640,480),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(npu.recorded.runs,0);
    humanvision::runtime::yolo::Geometry geometry{};
    EXPECT_FALSE(humanvision::runtime::yolo::BuildGeometry(640,480,512,288,geometry));
    EXPECT_FALSE(humanvision::runtime::yolo::BuildGeometry(218,346,512,288,geometry));
}
TEST(YoloCpuPipeline, LetterboxGeometryAndDecoderOptInLeaveDefaultAdmissionExact) {
    using namespace humanvision::runtime::yolo;Geometry geometry{};
    ASSERT_TRUE(BuildLetterboxGeometry(640,480,512,288,geometry));EXPECT_EQ(geometry.left,64);EXPECT_EQ(geometry.top,0);
    EXPECT_FLOAT_EQ(geometry.scale,.6f);
    ASSERT_TRUE(BuildLetterboxGeometry(218,346,512,288,geometry));EXPECT_EQ(geometry.left,165);EXPECT_EQ(geometry.top,0);
    EXPECT_FLOAT_EQ(geometry.scale,288.f/346.f);
    Pipeline p;CpuVariant(p,"one");HV_TensorViewV1 views[]={View("out0",p.recorded.det,65),View("out1",p.recorded.points,51)};
    HV_BodyObservationV1 bodies[8]{};float scores[8]{};uint32_t count=0;
    Decoder original(512,288),cpu(512,288,true);
    EXPECT_FALSE(original.Decode(views,2,geometry,123,bodies,scores,8,count));
    ASSERT_TRUE(cpu.Decode(views,2,geometry,123,bodies,scores,8,count));
    ++geometry.left;EXPECT_FALSE(cpu.Decode(views,2,geometry,123,bodies,scores,8,count));
    EXPECT_FALSE(BuildLetterboxGeometry(640,480,640,384,geometry));EXPECT_FALSE(BuildLetterboxGeometry(0,480,512,288,geometry));
}
TEST(YoloCpuPipeline, RealSavedLandscapeOutputsDecodeIdenticallyWithDefaultAndCpuGeometry) {
    using namespace humanvision::runtime::yolo;
    for(const char* fixture:{"seven","one","empty"}){SCOPED_TRACE(fixture);Pipeline p;CpuVariant(p,fixture);
        HV_TensorViewV1 views[]={View("out0",p.recorded.det,65),View("out1",p.recorded.points,51)};
        for(const auto shape:{std::pair<int,int>{512,288},{1024,576},{1600,900}}){SCOPED_TRACE(shape.first);
            Geometry original_geometry{},cpu_geometry{};
            ASSERT_TRUE(BuildGeometry(shape.first,shape.second,512,288,original_geometry));
            ASSERT_TRUE(BuildLetterboxGeometry(shape.first,shape.second,512,288,cpu_geometry));
            EXPECT_EQ(original_geometry.left,cpu_geometry.left);EXPECT_EQ(original_geometry.top,cpu_geometry.top);
            EXPECT_EQ(original_geometry.scale,cpu_geometry.scale);
            Decoder original(512,288),cpu(512,288,true);HV_BodyObservationV1 original_bodies[8]{},cpu_bodies[8]{};
            float original_scores[8]{},cpu_scores[8]{};uint32_t original_count=0,cpu_count=0;
            ASSERT_TRUE(original.Decode(views,2,original_geometry,1234567,original_bodies,original_scores,8,original_count));
            ASSERT_TRUE(cpu.Decode(views,2,cpu_geometry,1234567,cpu_bodies,cpu_scores,8,cpu_count));
            ASSERT_EQ(original_count,cpu_count);EXPECT_EQ(std::memcmp(original_bodies,cpu_bodies,original_count*sizeof(HV_BodyObservationV1)),0);
            EXPECT_EQ(std::memcmp(original_scores,cpu_scores,original_count*sizeof(float)),0);
        }
    }
}
TEST(YoloCpuPipeline, FourFormatsAndStrideMatchPreciseLetterboxOpenCvOracleWithNoFrameAllocations) {
    struct Case {const char* name;const char* source_sha;const char* input_sha;};
    const Case cases[]={{"landscape4x3","b7a2eaad72eb0c4c8a2d54cb47edbf50b54942fe36f4f654f0bc3569e67198c6","544bccd3ce3641180d34c5f6e9d613fb5d6ee535915387f7dfff2a0e0c8a99ad"},
        {"portrait","39623d9fc1e654fc8ab16d41c3f0e1bbb03fbacccda7e872a981e5c787133ad6","37a6b4af9958c6e7ee3c326d9c044235cd5ca473effde386665da965ba5a8af5"},
        {"portrait-odd","3a958b056a0b8073550ebc2491224bb051af68422cb7338e2e04538253f050c8","b15a021eb6195f329959d1cd670d8c6a7613e6f78d1d5fa48baa5a5ab9d916c3"}};
    Pipeline p;CpuVariant(p,"empty");ASSERT_EQ(p.Create(),HV_OK)<<p.message;
    for(const auto& sample:cases){SCOPED_TRACE(sample.name);const auto fixture=project/"out/yolo-cpu-letterbox-fixtures"/sample.name;
        ASSERT_EQ(Hash(fixture/"source.rgb"),sample.source_sha);ASSERT_EQ(Hash(fixture/"input.fp32"),sample.input_sha);
        const auto receipt=ReadJson(fixture/"receipt.json");const auto& g=receipt["geometry"];
        const int width=g["source_width"],height=g["source_height"];const auto source=Read<uint8_t>(fixture/"source.rgb");const auto oracle=Read<float>(fixture/"input.fp32");
        for(auto format:{HV_PIXEL_RGB24,HV_PIXEL_BGR24,HV_PIXEL_RGBA32,HV_PIXEL_BGRA32}){SCOPED_TRACE(format);
            const int channels=format==HV_PIXEL_RGBA32||format==HV_PIXEL_BGRA32?4:3;const bool rgb=format==HV_PIXEL_RGB24||format==HV_PIXEL_RGBA32;
            const int stride=width*channels+17;std::vector<uint8_t> pixels(size_t(stride)*height,253);
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto* out=pixels.data()+size_t(y)*stride+x*channels;const auto* in=source.data()+(size_t(y)*width+x)*3;
                out[rgb?0:2]=in[0];out[1]=in[1];out[rgb?2:0]=in[2];if(channels==4)out[3]=3;}
            BeginNativeAllocationProbe();const auto result=p.Process(pixels,width,height,format,stride);const auto allocations=EndNativeAllocationProbe();
            ASSERT_EQ(result,HV_OK)<<p.message;EXPECT_EQ(allocations,0u);const auto actual=CapturedPlanar(p);ASSERT_EQ(actual.size(),oracle.size());
            for(size_t i=0;i<actual.size();++i)ASSERT_FLOAT_EQ(actual[i],oracle[i])<<i;
        }
    }
}
TEST(YoloCpuPipeline, ActualCpuInferenceForFourByThreeAndPortraitRestoresSourceGeometry) {
    RealCpuBackend real;Pipeline p;CpuVariant(p);ASSERT_EQ(CreateReal(p,real),HV_OK)<<p.message;
    const char* names[]={"landscape4x3","portrait","portrait-odd"};const uint32_t people[]={7,1,1};
    for(int k=0;k<3;++k){SCOPED_TRACE(names[k]);const auto fixture=project/"out/yolo-cpu-letterbox-fixtures"/names[k];
        const auto receipt=ReadJson(fixture/"receipt.json");ASSERT_EQ(receipt["onnx_sha256"],p.manifest["models"][0]["sha256"]);
        for(const char* file:{"source.rgb","input.fp32","out0.fp32","out1.fp32"})ASSERT_EQ(Hash(fixture/file),receipt["sha256"][file]);
        const auto pixels=Read<uint8_t>(fixture/"source.rgb");const auto expected_input=Read<float>(fixture/"input.fp32");
        const int width=receipt["geometry"]["source_width"],height=receipt["geometry"]["source_height"];
        ASSERT_EQ(p.Process(pixels,width,height),HV_OK)<<p.message;ASSERT_EQ(p.output.body_count,people[k]);EXPECT_EQ(p.output.hand_count,0u);
        for(size_t i=0;i<real.input.size();++i)ASSERT_FLOAT_EQ(real.input[i],expected_input[i])<<i;
        for(int t=0;t<2;++t){const auto expected=Read<float>(fixture/(t?"out1.fp32":"out0.fp32"));
            const auto* actual=static_cast<const float*>(real.outputs[t].data);double maximum=0,mean=0;
            for(size_t i=0;i<expected.size();++i){const double delta=std::abs(double(actual[i])-expected[i]);maximum=std::max(maximum,delta);mean+=delta;}
            EXPECT_LT(maximum,.002);EXPECT_LT(mean/expected.size(),.00002);}
        for(uint32_t b=0;b<p.output.body_count;++b){const auto& body=p.bodies[b];const auto& oracle=receipt["bodies"][b];
            EXPECT_NEAR(body.bbox_px.x,oracle["box"][0].get<float>(),.002);EXPECT_NEAR(body.bbox_px.y,oracle["box"][1].get<float>(),.002);
            EXPECT_NEAR(body.bbox_px.width,oracle["box"][2].get<float>()-oracle["box"][0].get<float>(),.002);
            EXPECT_NEAR(body.bbox_px.height,oracle["box"][3].get<float>()-oracle["box"][1].get<float>(),.002);
            for(int j=0;j<17;++j){const auto& joint=body.joints[humanvision::runtime::yolo::CanonicalIndex(j)];const auto& expected=oracle["joints"][j];
                EXPECT_NEAR(joint.x_px,expected[0].get<float>(),.002);EXPECT_NEAR(joint.y_px,expected[1].get<float>(),.002);
                EXPECT_NEAR(joint.confidence,expected[2].get<float>(),.00001);EXPECT_FLOAT_EQ(joint.x_norm,joint.x_px/width);EXPECT_FLOAT_EQ(joint.y_norm,joint.y_px/height);
                EXPECT_EQ(joint.observation_timestamp_us,1234567);EXPECT_EQ(joint.prediction_ms,0u);}
        }
    }
}
TEST(YoloCpuPipeline, ActualCpuInferenceRespectsAllEightConfiguredCapacities) {
    const auto pixels=Read<uint8_t>(root/"seven/source.rgb");
    for(int capacity=1;capacity<=8;++capacity){SCOPED_TRACE(capacity);RealCpuBackend real;Pipeline p;CpuVariant(p);
        ASSERT_EQ(CreateReal(p,real,capacity),HV_OK)<<p.message;p.output.body_capacity=capacity;
        ASSERT_EQ(p.Process(pixels),HV_OK)<<p.message;EXPECT_EQ(p.output.body_count,uint32_t(std::min(capacity,7)));EXPECT_EQ(real.runs,1);}
}

TEST(YoloTensorPipeline, InitializesExactBackendSynchronouslyAndReleasesOnce){
    Pipeline p;ASSERT_EQ(p.Create(),HV_OK)<<p.message;EXPECT_EQ(p.recorded.creates,1);EXPECT_EQ(p.recorded.runs,0);EXPECT_EQ(p.recorded.provider,"backend.rknn");
    const auto options=Json::parse(p.recorded.options);EXPECT_EQ(options,Json({{"runtime_library","librknnrt.so"},{"core_mask",7},{"input_layout","nhwc"},{"input_type","uint8"},
        {"input_transform",{{"mean",{0,0,0}},{"norm",{1./255.,1./255.,1./255.}}}}}));
    EXPECT_EQ(std::filesystem::u8path(p.recorded.model),std::filesystem::canonical(root/"non-quantized/candidate.rknn"));p.api.pipeline->destroy(p.instance);p.instance=nullptr;EXPECT_EQ(p.recorded.releases,1);
}
TEST(YoloTensorPipeline, FoldedCandidateRequiresPinnedArtifactsAndExplicitSelection){
    Pipeline p;p.profile["private_graph_candidate"]="folded-output";
    ASSERT_EQ(p.Create(),HV_OK)<<p.message;
    EXPECT_EQ(std::filesystem::u8path(p.recorded.model).filename(),"folded-output.rknn");
    Pipeline invalid;invalid.profile["private_graph_candidate"]="unverified-int8";
    EXPECT_EQ(invalid.Create(),HV_ERR_MODEL_LOAD);EXPECT_EQ(invalid.recorded.creates,0);
}
TEST(YoloTensorPipeline, RejectsUnqualifiedManifestAndProfileBeforeBackendCreation){
    for(const char* key:{"experimental","local_evaluation_only"}){Pipeline p;p.manifest[key]=false;EXPECT_EQ(p.Create(),HV_ERR_MODEL_LOAD);EXPECT_EQ(p.recorded.creates,0);}
    for(const char* key:{"experimental","local_evaluation_only"}){Pipeline p;p.profile[key]=false;EXPECT_EQ(p.Create(),HV_ERR_MODEL_LOAD);EXPECT_EQ(p.recorded.creates,0);}
    for(int which=0;which<14;++which){Pipeline p;auto& m=p.manifest["models"][0];
        switch(which){case 0:p.manifest["schema_version"]=2;break;case 1:p.manifest["execution_contract"]="int8";break;case 2:m["sha256"]=std::string(64,'0');break;case 3:m["asset_path"]="../candidate.rknn";break;case 4:m["input_contract"]["width"]=640;break;case 5:m["input_contract"]["tensor_layout"]="NCHW";break;case 6:m["input_contract"]["color_order"]="BGR";break;case 7:m["output_contract"]["rows"]=2100;break;case 8:m["output_contract"]["output_blobs"]={"out1","out0"};break;case 9:m["backend_options"]["core_mask"]=1;break;case 10:p.manifest["qualification"]["deployment_ready"]=true;break;case 11:p.manifest["qualification"]["offline_numerical_passed"]=false;break;case 12:m["evidence"][0]["sha256"]=std::string(64,'0');break;case 13:p.profile["backend"]["allow_fallback"]=true;break;}
        EXPECT_EQ(p.Create(),HV_ERR_MODEL_LOAD)<<which;EXPECT_EQ(p.recorded.creates,0)<<which;EXPECT_EQ(p.instance,nullptr);
    }
}
TEST(YoloTensorPipeline, CleansPartialBackendAndInvalidApiOnCreationFailure){
    for(int which=0;which<3;++which){Pipeline p;p.recorded.fail_create=which<2;p.recorded.partial_failure=which==1;p.recorded.invalid_api=which==2;EXPECT_NE(p.Create(),HV_OK);EXPECT_EQ(p.instance,nullptr);EXPECT_EQ(p.recorded.releases,which==0?0:1);}
}
TEST(YoloTensorPipeline, RealRecordedSimulatorOutputsAndPreparedOracleSevenOneEmpty){
    const auto comparison=ReadJson(root/"non-quantized/simulator/comparison.json");
    const char* names[]={"seven","one","empty"};const char* raw_hash[]={"814e663406f6756414768311ea62dfb364cd2bc9e0e6cf72bb1b4422ef8bd3c6","a8f2701bf39d8e91f731f58de71e68f24ff4285ce36c9379d292f69cec344f6e","43e775511d1495f5e9a536e0abbe1044effd10d09fb963d7c38e2e62eb836d29"};
    for(int k=0;k<3;++k){SCOPED_TRACE(names[k]);Pipeline p;const auto folder=root/names[k];const auto& receipt=comparison["fixtures"][k];
        ASSERT_EQ(Hash(folder/"source.rgb"),raw_hash[k]);ASSERT_EQ(Hash(folder/"input.fp32"),receipt["input_sha256"]);
        ASSERT_EQ(Hash(root/"non-quantized/simulator"/(std::string(names[k])+"-out0.fp32")),receipt["output_sha256"]["out0"]);
        ASSERT_EQ(Hash(root/"non-quantized/simulator"/(std::string(names[k])+"-out1.fp32")),receipt["output_sha256"]["out1"]);
        auto pixels=Read<uint8_t>(folder/"source.rgb");auto oracle=Read<float>(folder/"input.fp32");p.recorded.Fixture(names[k]);ASSERT_EQ(p.Create(),HV_OK)<<p.message;ASSERT_EQ(p.Process(pixels),HV_OK)<<p.message;
        EXPECT_EQ(p.recorded.input.rank,4u);EXPECT_EQ(p.recorded.input.element_type,3u);EXPECT_STREQ(p.recorded.input.name,"in0");EXPECT_EQ(p.recorded.input.dimensions[0],1);EXPECT_EQ(p.recorded.input.dimensions[1],288);EXPECT_EQ(p.recorded.input.dimensions[2],512);EXPECT_EQ(p.recorded.input.dimensions[3],3);
        size_t mismatches=0;for(size_t pixel=0;pixel<512*288;++pixel)for(int c=0;c<3;++c)if(p.recorded.captured[pixel*3+c]!=std::lround(oracle[c*512*288+pixel]*255.f))++mismatches;EXPECT_EQ(mismatches,0u);
        ASSERT_EQ(p.output.body_count,receipt["expected_people"]);EXPECT_EQ(p.output.hand_count,0u);const auto& actual=receipt["actual"];ASSERT_EQ(actual.size(),p.output.body_count);
        for(uint32_t i=0;i<p.output.body_count;++i){const auto& b=p.bodies[i];EXPECT_NEAR(b.bbox_px.x,actual[i]["box"][0].get<float>(),.002);EXPECT_NEAR(b.bbox_px.y,actual[i]["box"][1].get<float>(),.002);EXPECT_NEAR(b.confidence,actual[i]["score"].get<float>(),.000002);EXPECT_EQ(b.joints[HV_CANONICAL_NOSE].observation_timestamp_us,1234567);EXPECT_EQ(b.joints[HV_CANONICAL_HAND_LEFT].valid,0);EXPECT_EQ(b.joints[HV_CANONICAL_THUMB_RIGHT].valid,0);}
        humanvision::runtime::PipelineDiagnostics diagnostics{};ASSERT_TRUE(humanvision::runtime::CopyPipelineDiagnostics(p.instance,diagnostics));EXPECT_EQ(diagnostics.detector_execution_count,1u);EXPECT_EQ(diagnostics.pose_person_count,p.output.body_count);
        // V1 caller timestamps have no native steady-clock epoch guarantee.
        EXPECT_EQ(diagnostics.last_detector_capture_steady_us,0);
        BeginNativeAllocationProbe();const auto status=p.Process(pixels);const auto allocations=EndNativeAllocationProbe();EXPECT_EQ(status,HV_OK);EXPECT_EQ(allocations,0u);
    }
}
TEST(YoloTensorPipeline, ColorChannelsAlphaPaddedStrideAndBilinearRows){
    // Four reviewed formats must yield identical RGB rows; alpha and padding are not pixels.
    for(auto format:{HV_PIXEL_RGB24,HV_PIXEL_RGBA32,HV_PIXEL_BGR24,HV_PIXEL_BGRA32}){Pipeline p;p.recorded.Fixture("empty");ASSERT_EQ(p.Create(),HV_OK)<<p.message;const int channels=format==HV_PIXEL_RGB24||format==HV_PIXEL_BGR24?3:4;const bool rgb=format==HV_PIXEL_RGB24||format==HV_PIXEL_RGBA32;const int stride=1024*channels+13;std::vector<uint8_t> pixels(stride*576,253);
        for(int y=0;y<576;++y)for(int x=0;x<1024;++x){auto* pixel=pixels.data()+y*stride+x*channels;pixel[rgb?0:2]=uint8_t(x%128);pixel[1]=uint8_t(y%128);pixel[rgb?2:0]=41;if(channels==4)pixel[3]=3;}
        ASSERT_EQ(p.Process(pixels,1024,576,format,stride),HV_OK)<<p.message;EXPECT_EQ(p.recorded.captured[0],1);EXPECT_EQ(p.recorded.captured[1],1);EXPECT_EQ(p.recorded.captured[2],41);EXPECT_EQ(p.recorded.captured[(512*17+23)*3],47);EXPECT_EQ(p.recorded.captured[(512*17+23)*3+1],35);
    }
}
TEST(YoloTensorPipeline, RejectsAspectStrideUnsupportedFormatCapacityAndClearsFailedOutput){
    Pipeline p;ASSERT_EQ(p.Create(),HV_OK)<<p.message;auto pixels=Read<uint8_t>(root/"seven/source.rgb");
    EXPECT_EQ(p.Process(pixels,1024,575),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(p.Process(pixels,1024,576,HV_PIXEL_RGB24,1024*3-1),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(p.Process(pixels,1024,576,static_cast<HV_PixelFormat>(999)),HV_ERR_UNSUPPORTED_FORMAT);EXPECT_EQ(p.recorded.runs,0);
    p.output.body_capacity=1;EXPECT_EQ(p.Process(pixels),HV_ERR_INVALID_ARGUMENT);p.output.body_capacity=8;
    ASSERT_EQ(p.Process(pixels),HV_OK);ASSERT_GT(p.output.body_count,0u);p.recorded.fail_run=true;EXPECT_EQ(p.Process(pixels),HV_ERR_INTERNAL);EXPECT_EQ(p.output.body_count,0u);EXPECT_EQ(p.output.hand_count,0u);
    p.recorded.fail_run=false;for(int malformed=1;malformed<=6;++malformed){p.recorded.malformed=malformed;EXPECT_EQ(p.Process(pixels),HV_ERR_INTERNAL)<<malformed;EXPECT_EQ(p.output.body_count,0u);}p.recorded.malformed=0;p.recorded.points.back()=std::numeric_limits<float>::quiet_NaN();EXPECT_EQ(p.Process(pixels),HV_ERR_INTERNAL);EXPECT_EQ(p.output.body_count,0u);
}
TEST(YoloTensorPipeline, ConfiguredBodyCapacityLimitsActualSevenPersonFrame){
    auto pixels=Read<uint8_t>(root/"seven/source.rgb");for(int limit:{1,2,4,8}){Pipeline p;ASSERT_EQ(p.Create(limit),HV_OK)<<p.message;p.output.body_capacity=limit;ASSERT_EQ(p.Process(pixels),HV_OK)<<p.message;EXPECT_EQ(p.output.body_count,uint32_t(std::min(limit,7)));}
}

TEST(YoloTensorPipeline, NonIntegerResizeAndBordersMatchWholeOpenCvUint8Oracle){
    // Pixel contracts, not device inference: these complete RGB hashes were
    // produced by Python OpenCV 4.13.0 INTER_LINEAR from the formula below.
    // The actual seven/one/empty recorded inference case is verified separately.
    struct Oracle {int width,height;const char* sha;};
    const Oracle oracles[]={
        {48,27,"715e8f60b02a6ac877b19f63fd04983c6a0b97204a789378054af7ff8b10f6d6"},
        {80,45,"362ec089fab9e81c4eb45ea7f4794f55ce721008b0e85883e25361f773bab6f5"},
        {1600,900,"c298baecb6080fa3b6b70550b1c4dacea4670a77322e365a12334a184ad1dacb"},
        {512,288,"2cc61dab4f0895e336f8d8d27ab763cb8bef82cb5f8a5489d6f114af06a54b37"}};
    Pipeline p;p.recorded.Fixture("empty");ASSERT_EQ(p.Create(),HV_OK)<<p.message;
    for(const auto& oracle:oracles){SCOPED_TRACE(oracle.width);std::vector<uint8_t> pixels(size_t(oracle.width)*oracle.height*3);
        for(int y=0;y<oracle.height;++y)for(int x=0;x<oracle.width;++x){auto* rgb=pixels.data()+(size_t(y)*oracle.width+x)*3;
            rgb[0]=uint8_t((x*19+y*7)%256);rgb[1]=uint8_t((x*11+y*37)%256);rgb[2]=uint8_t((x*43+y*13)%256);}
        ASSERT_EQ(p.Process(pixels,oracle.width,oracle.height),HV_OK)<<p.message;
        EXPECT_EQ(picosha2::hash256_hex_string(p.recorded.captured),oracle.sha);
        // Coefficient changes on a source-size change use fixed storage too.
        BeginNativeAllocationProbe();const auto result=p.Process(pixels,oracle.width,oracle.height);const auto allocations=EndNativeAllocationProbe();
        EXPECT_EQ(result,HV_OK);EXPECT_EQ(allocations,0u);
    }
}

TEST(YoloTensorPipeline, RejectsInvalidAbiAndTruncatedPixelBufferWithoutBackendRun){
    HV_PluginApiV1 query{sizeof(query),1};EXPECT_EQ(HV_QueryYoloTensorPipelineV1(2,&query),HV_ERR_INVALID_ARGUMENT);
    query.struct_size=sizeof(query)-1;EXPECT_EQ(HV_QueryYoloTensorPipelineV1(1,&query),HV_ERR_INVALID_ARGUMENT);
    query.struct_size=sizeof(query);query.api_version=0;EXPECT_EQ(HV_QueryYoloTensorPipelineV1(1,&query),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(HV_QueryYoloTensorPipelineV1(1,nullptr),HV_ERR_INVALID_ARGUMENT);
    Pipeline p;const auto manifest=p.manifest.dump(),profile=p.profile.dump(),assets=root.u8string();
    const HV_PipelineConfigV1 config{sizeof(config),1,8,0,manifest.c_str(),assets.c_str(),profile.c_str()};
    const HV_HostServicesV1 host{sizeof(host),1,&p.recorded,CreateBackend,ReleaseBackend};
    for(int which=0;which<9;++which){auto c=config;auto h=host;void* candidate=reinterpret_cast<void*>(1);
        switch(which){case 0:c.struct_size=sizeof(c)-1;break;case 1:c.api_version=2;break;case 2:c.max_bodies=0;break;
            case 3:c.max_bodies=9;break;case 4:h.struct_size=sizeof(h)-1;break;case 5:h.api_version=2;break;
            case 6:h.create_backend=nullptr;break;case 7:h.release_backend=nullptr;break;case 8:c.options_utf8=nullptr;break;}
        EXPECT_EQ(p.api.pipeline->create(&c,&h,&candidate,&p.error),HV_ERR_INVALID_ARGUMENT)<<which;EXPECT_EQ(candidate,nullptr);
    }
    EXPECT_EQ(p.recorded.creates,0);ASSERT_EQ(p.Create(),HV_OK)<<p.message;
    auto pixels=Read<uint8_t>(root/"seven/source.rgb");pixels.resize(pixels.size()-1);
    EXPECT_EQ(p.Process(pixels),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(p.recorded.runs,0);EXPECT_EQ(p.output.body_count,0u);
    p.output.body_count=7;p.output.hand_count=2;
    EXPECT_EQ(p.api.pipeline->process(p.instance,nullptr,&p.output,&p.error),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(p.output.body_count,0u);EXPECT_EQ(p.output.hand_count,0u);p.api.pipeline->destroy(nullptr);
}
