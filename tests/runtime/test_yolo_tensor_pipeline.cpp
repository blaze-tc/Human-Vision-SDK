#include <gtest/gtest.h>
#include "plugins/pipeline/yolo/yolo_tensor_pipeline.h"
#include "common/pipeline_diagnostics.h"
#include "json/json.hpp"
#include "picosha2/picosha2.h"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cmath>
#include <limits>

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
}

TEST(YoloTensorPipeline, InitializesExactBackendSynchronouslyAndReleasesOnce){
    Pipeline p;ASSERT_EQ(p.Create(),HV_OK)<<p.message;EXPECT_EQ(p.recorded.creates,1);EXPECT_EQ(p.recorded.runs,0);EXPECT_EQ(p.recorded.provider,"backend.rknn");
    const auto options=Json::parse(p.recorded.options);EXPECT_EQ(options,Json({{"runtime_library","librknnrt.so"},{"core_mask",7},{"input_layout","nhwc"},{"input_type","uint8"}}));
    EXPECT_EQ(std::filesystem::u8path(p.recorded.model),std::filesystem::canonical(root/"non-quantized/candidate.rknn"));p.api.pipeline->destroy(p.instance);p.instance=nullptr;EXPECT_EQ(p.recorded.releases,1);
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
