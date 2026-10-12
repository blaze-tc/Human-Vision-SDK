#include "plugins/backend/rknn/rknn_backend.h"
#include "plugins/backend/rknn/rknn_vendor_boundary.h"
#include "plugins/backend/rknn/rknn_input_qualification.h"
#include "plugins/backend/rknn/rknn_native_input.h"
#include "plugins/backend/rknn/rknn_native_output.h"
#include "common/backend_diagnostics.h"
#include "common/finite_tensor.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <thread>
#include <chrono>

namespace {
TEST(FiniteTensor, ChecksEveryLaneAndTailWithoutChangingBits) {
    using humanvision::runtime::AllFinite;
    std::array<float,39> values{};
    for(size_t n=0;n<=values.size();++n) {
        values.fill(-0.0f);EXPECT_TRUE(AllFinite(values.data(),n));
        for(size_t bad=0;bad<n;++bad)for(uint32_t bits:{0x7f800000u,0xff800000u,0x7fc00001u,0x7f800001u}) {
            std::memcpy(&values[bad],&bits,4);
            EXPECT_FALSE(AllFinite(values.data(),n))<<n<<"/"<<bad;
            uint32_t retained=0;std::memcpy(&retained,&values[bad],4);EXPECT_EQ(bits,retained);
            values[bad]=-0.0f;
        }
    }
    EXPECT_TRUE(AllFinite(nullptr,0));EXPECT_FALSE(AllFinite(nullptr,1));
}
TEST(RknnNativeInput, ReusedTightAndPaddedBuffersClearOnlyOwnedBytes) {
    using namespace humanvision::runtime::rknn;
    NativeInputTransform transform;const float mean[]={0.f,127.f,1.f},norm[]={1.f/255,1.f/128,-1.f};
    ASSERT_TRUE(transform.Configure(mean,norm));
    for(unsigned channels:{3u,4u,8u,16u})for(unsigned stride:{7u,9u})for(unsigned extra:{0u,14u}) {
        NativeInputLayout layout;const unsigned bytes=stride*3*channels*2+extra;
        ASSERT_TRUE(layout.Configure(7,3,stride,channels,bytes));
        std::vector<uint16_t> output(bytes/2+1,0xbeef);std::vector<uint8_t> rgb(7*3*3);
        for(unsigned round=0;round<2;++round) {
            for(size_t i=0;i<rgb.size();++i)rgb[i]=uint8_t(i*37+round*53);
            std::fill(output.begin(),output.end(),0xbeef);
            ASSERT_TRUE(transform.Pack(rgb.data(),uint32_t(rgb.size()),layout,output.data(),bytes));
            for(unsigned y=0;y<3;++y)for(unsigned x=0;x<stride;++x)for(unsigned c=0;c<channels;++c)
                EXPECT_EQ(output[(y*stride+x)*channels+c],x<7&&c<3?transform.Value(c,rgb[(y*7+x)*3+c]):0);
            for(size_t i=stride*3*channels;i<bytes/2;++i)EXPECT_EQ(output[i],0);
            EXPECT_EQ(output.back(),0xbeef);
        }
    }
}
TEST(RknnNativeOutput, HandlesUnalignedInputAndEveryVectorTailAndInvalidLane) {
    using namespace humanvision::runtime::rknn;
    const uint16_t pattern[]={0,0x8000,1,0x8001,0x03ff,0x0400,0x3c00,0xbc00,0x7bff};
    for(uint64_t n=1;n<=39;++n) {
        NativeOutputLayout layout;ASSERT_TRUE(layout.Configure(1,&n,1,&n,n,n*2));
        std::vector<uint8_t> input(size_t(n*2+2),0xda);std::vector<float> output(size_t(n+1),123.f);
        for(size_t i=0;i<n;++i)std::memcpy(input.data()+1+i*2,&pattern[i%9],2);
        ASSERT_TRUE(layout.Decode(input.data()+1,uint32_t(n*2),output.data(),uint32_t(n*4)));
        EXPECT_EQ(output.back(),123.f);EXPECT_EQ(input.back(),0xda);
        for(size_t bad=0;bad<n;++bad) {
            const uint16_t invalid=0x7c00;std::memcpy(input.data()+1+bad*2,&invalid,2);
            EXPECT_FALSE(layout.Decode(input.data()+1,uint32_t(n*2),output.data(),uint32_t(n*4)));
            std::memcpy(input.data()+1+bad*2,&pattern[bad%9],2);
        }
    }
}
TEST(RknnNativeOutput, DecodesEveryFiniteHalfExactlyIncludingSubnormalAndSignedZero) {
    using namespace humanvision::runtime::rknn;
    const uint64_t dims[]={1,65536};
    NativeOutputLayout layout;
    ASSERT_TRUE(layout.Configure(2,dims,2,dims,65536,131072));
    std::vector<uint16_t> input(65536);
    std::vector<float> output(65536);
    for(unsigned bits=0;bits<65536;++bits)
        input[bits]=(bits&0x7c00)==0x7c00?0:uint16_t(bits);
    ASSERT_TRUE(layout.Decode(input.data(),131072,output.data(),262144));
    for(unsigned bits=0;bits<65536;++bits) {
        const unsigned half=input[bits],exp=(half>>10)&31,fraction=half&1023;
        float expected=exp?std::ldexp(float(1024+fraction),int(exp)-25):std::ldexp(float(fraction),-24);
        if(half&0x8000)expected=-expected;
        EXPECT_EQ(0,std::memcmp(&expected,&output[bits],sizeof(float)))<<bits;
    }
}
TEST(RknnNativeOutput, RejectsLayoutGuessingAndOversizedOrTruncatedBuffers) {
    using namespace humanvision::runtime::rknn;
    NativeOutputLayout layout;const uint64_t dims[]={1,2,3},swapped[]={1,3,2};
    EXPECT_FALSE(layout.Configure(3,dims,3,swapped,6,12));
    EXPECT_FALSE(layout.Configure(3,dims,3,dims,6,16)); // padding not described by dense layout
    EXPECT_FALSE(layout.Configure(3,dims,3,dims,5,12));
    const uint64_t huge[]={UINT64_MAX,2};
    EXPECT_FALSE(layout.Configure(2,huge,2,huge,UINT64_MAX,12));
    ASSERT_TRUE(layout.Configure(3,dims,3,dims,6,12));
    const uint16_t input[]={0x3c00,0xbc00,0x4000,0x0400,1,0x7bff};float output[7]{};output[6]=123;
    EXPECT_FALSE(layout.Decode(input,11,output,24));
    EXPECT_FALSE(layout.Decode(input,12,output,23));
    ASSERT_TRUE(layout.Decode(input,12,output,24));
    EXPECT_FLOAT_EQ(output[0],1);EXPECT_FLOAT_EQ(output[1],-1);EXPECT_FLOAT_EQ(output[5],65504);
    EXPECT_FLOAT_EQ(output[6],123);
}
TEST(RknnNativeOutput, RejectsNonfiniteRatherThanPublishingInvalidOutput) {
    using namespace humanvision::runtime::rknn;
    NativeOutputLayout layout;const uint64_t dims[]={2};ASSERT_TRUE(layout.Configure(1,dims,1,dims,2,4));
    uint16_t input[]={0x3c00,0x7c00};float output[2]{};
    EXPECT_FALSE(layout.Decode(input,4,output,8));
    input[1]=0x7e00;EXPECT_FALSE(layout.Decode(input,4,output,8));
}
TEST(RknnNativeInput, PacksRgbHalfWithQueriedRowAndChannelPadding) {
    using namespace humanvision::runtime::rknn;
    NativeInputTransform transform;
    const float mean[3]={0,0,0},norm[3]={1.f/255,1.f/255,1.f/255};
    ASSERT_TRUE(transform.Configure(mean,norm));
    NativeInputLayout layout;
    ASSERT_TRUE(layout.Configure(2,2,3,4,48));
    const uint8_t rgb[]={0,255,0,255,0,255,255,255,255,0,0,0};
    std::array<uint16_t,25> packed;packed.fill(0xdead);
    ASSERT_TRUE(transform.Pack(rgb,sizeof(rgb),layout,packed.data(),48));
    EXPECT_EQ(packed[0],0);EXPECT_EQ(packed[1],0x3c00);EXPECT_EQ(packed[3],0);
    EXPECT_EQ(packed[4],0x3c00);EXPECT_EQ(packed[8],0);EXPECT_EQ(packed[11],0);
    EXPECT_EQ(packed[12],0x3c00);EXPECT_EQ(packed[14],0x3c00);EXPECT_EQ(packed[24],0xdead);
    EXPECT_FALSE(transform.Pack(rgb,sizeof(rgb)-1,layout,packed.data(),48));
    EXPECT_FALSE(transform.Pack(rgb,sizeof(rgb),layout,packed.data(),47));
}
TEST(RknnNativeInput, RejectsUnsupportedLayoutAndUnboundedNormalization) {
    using namespace humanvision::runtime::rknn;
    NativeInputLayout a;
    EXPECT_FALSE(a.Configure(512,288,511,3,512*288*6));
    EXPECT_FALSE(a.Configure(512,288,512,2,512*288*4));
    EXPECT_FALSE(a.Configure(UINT32_MAX,288,UINT32_MAX,3,UINT32_MAX));
    EXPECT_FALSE(a.Configure(512,288,512,3,1));
    NativeInputTransform transform;float mean[3]={0,0,0},norm[3]={1,1,1};
    norm[2]=std::numeric_limits<float>::infinity();EXPECT_FALSE(transform.Configure(mean,norm));
    norm[2]=10000;EXPECT_FALSE(transform.Configure(mean,norm));
    norm[2]=1;ASSERT_TRUE(transform.Configure(mean,norm));
    EXPECT_EQ(transform.Value(0,255),0x5bf8); // exact IEEE half representation of 255
}
TEST(RknnNativeInput, RoundsSubnormalTiesEvenAndRejectsUnalignedDestination) {
    using namespace humanvision::runtime::rknn;
    NativeInputTransform transform;
    const float mean[3]={0,0,0},norm[3]={0x1p-25f,-0x1p-25f,1};
    ASSERT_TRUE(transform.Configure(mean,norm));
    EXPECT_EQ(transform.Value(0,1),0);EXPECT_EQ(transform.Value(0,3),2);
    EXPECT_EQ(transform.Value(0,5),2);EXPECT_EQ(transform.Value(1,3),0x8002);
    NativeInputLayout layout;ASSERT_TRUE(layout.Configure(1,1,1,3,6));
    const uint8_t rgb[3]={1,3,5};alignas(2) uint8_t memory[8]{};
    EXPECT_FALSE(transform.Pack(rgb,3,layout,memory+1,6));
}
TEST(RknnInputQualification, SameFrameBitEqualityIncludesEveryValueAndRejectsNonfinite) {
    float a[]={0,1,2,3},b[]={0,1,2,3};
    EXPECT_TRUE(humanvision::runtime::rknn::InputQualification::Equal(a,b,4));
    b[3]=3.00001f;EXPECT_FALSE(humanvision::runtime::rknn::InputQualification::Equal(a,b,4));
    b[3]=std::numeric_limits<float>::quiet_NaN();EXPECT_FALSE(humanvision::runtime::rknn::InputQualification::Equal(a,b,4));
    a[3]=b[3];EXPECT_FALSE(humanvision::runtime::rknn::InputQualification::Equal(a,b,4));
    a[3]=0.f;b[3]=-0.f;EXPECT_FALSE(humanvision::runtime::rknn::InputQualification::Equal(a,b,4));
    EXPECT_FALSE(humanvision::runtime::rknn::InputQualification::Equal(a,b,0));
}
TEST(RknnInputQualification, TwoWarmupsAndFiveComparisonsRequireAtLeastFivePercentGain) {
    using Q=humanvision::runtime::rknn::InputQualification;
    Q fast,slow;
    for(int i=0;i<6;++i){fast.Observe(true,50,40);slow.Observe(true,50,49);}
    EXPECT_EQ(fast.state,Q::State::Pending);EXPECT_EQ(slow.state,Q::State::Pending);
    fast.Observe(true,50,40);slow.Observe(true,50,49);
    EXPECT_EQ(fast.state,Q::State::Accepted);EXPECT_EQ(slow.state,Q::State::Rejected);
    EXPECT_EQ(fast.measured,5u);EXPECT_DOUBLE_EQ(fast.baseline_ms,250);
}
TEST(RknnInputQualification, WarmupMismatchOrInvalidTimingRejectsImmediately) {
    using Q=humanvision::runtime::rknn::InputQualification;
    Q q;q.Observe(false,50,20);EXPECT_EQ(q.state,Q::State::Rejected);
    Q bad;bad.Observe(true,0,20);EXPECT_EQ(bad.state,Q::State::Rejected);
    Q nan;nan.Observe(true,50,std::numeric_limits<double>::quiet_NaN());EXPECT_EQ(nan.state,Q::State::Rejected);
}
using namespace humanvision::runtime::rknn;
struct State {
    std::vector<std::string> events;
    std::string fail;
    std::string throw_stage;
    bool partial_init = false, nan = false, changed_buffer = false, changed_index=false, changed_bytes=false;
    uint32_t mask = 0, runs = 0, input_count = 1;
    bool candidate=false, candidate_mismatch=false, slow_baseline=false;
    unsigned candidate_runs=0,candidate_drops=0,candidate_retries=0;
    const void* model = nullptr;
    uint32_t model_bytes = 0;
    Attribute input{0, 4, {1, 3, 2, 4}, 24, "in0", Layout::Nchw, Type::Float32};
    std::vector<Attribute> outputs{{0, 3, {1, 2, 3}, 6, "raw_boxes", Layout::Unknown, Type::Float16},
                                   {1, 2, {2, 2}, 4, "raw_points", Layout::Unknown, Type::Float32}};
    int Step(const char* s) {
        events.emplace_back(s);
        if (throw_stage == s) throw std::runtime_error("injected vendor exception");
        return fail == s ? -9 : 0;
    }
};
class FixtureVendor final : public Vendor {
    State& s;
public:
    explicit FixtureVendor(State& x) : s(x) {}
    ~FixtureVendor() override { s.Step("unload"); }
    int Initialize(Context& c, const void* model, uint32_t bytes) override {
        s.model=model;s.model_bytes=bytes;
        const auto r=s.Step("init"); if (!r || s.partial_init) c=17; return r;
    }
    int Versions(Context, char* a, char* b, uint32_t) override { std::strcpy(a,"test-runtime");std::strcpy(b,"test-driver");return s.Step("versions"); }
    int SetCoreMask(Context, uint32_t v) override { s.mask=v;return s.Step("core"); }
    int IoCount(Context, uint32_t& i, uint32_t& o) override { i=s.input_count;o=uint32_t(s.outputs.size());return s.Step("io"); }
    int InputAttribute(Context, Attribute& a) override { a=s.input;return s.Step("input_query"); }
    int OutputAttribute(Context, Attribute& a) override { a=s.outputs.at(a.index);return s.Step("output_query"); }
    int PrepareOutputs(const OutputBuffer*, uint32_t) override { return s.Step("prepare"); }
    int InputsSet(Context, const void* rgb, uint32_t bytes) override {
        if(s.slow_baseline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        EXPECT_EQ(bytes,24u);EXPECT_EQ(static_cast<const unsigned char*>(rgb)[0],255u);
        EXPECT_EQ(s.model_bytes,4u);EXPECT_EQ(static_cast<const unsigned char*>(s.model)[0],1u);
        return s.Step("inputs_set");
    }
    int Run(Context) override { ++s.runs;return s.Step("run"); }
    int OutputsGet(Context, OutputBuffer* out, uint32_t n) override {
        const int r=s.Step("get");if(r)return r;
        for(uint32_t i=0;i<n;++i)for(uint32_t j=0;j<out[i].bytes/4;++j)out[i].data[j]=float(10*i+j+s.runs);
        if(s.nan)out[0].data[0]=std::numeric_limits<float>::quiet_NaN();
        if(s.changed_buffer)out[0].data=nullptr;
        if(s.changed_index)out[0].index=1;
        if(s.changed_bytes)--out[0].bytes;
        return 0;
    }
    int OutputsRelease(Context, uint32_t) noexcept override { return s.Step("release"); }
    bool PrepareInputCandidate(const void*,uint32_t,const Attribute&,const Attribute*,const OutputBuffer*,uint32_t,uint32_t) override {
        return s.candidate;
    }
    int CandidateInputsSet(const void*,uint32_t) override {return s.Step("candidate_set");}
    int CandidateRun() override {++s.candidate_runs;return s.Step("candidate_run");}
    int CandidateOutputsGet(OutputBuffer* out,uint32_t n) override {
        for(uint32_t i=0;i<n;++i)for(uint32_t j=0;j<out[i].bytes/4;++j)
            out[i].data[j]=float(10*i+j+s.runs)+(s.candidate_mismatch?0.01f:0);
        return s.Step("candidate_get");
    }
    int CandidateOutputsRelease(uint32_t) noexcept override {return s.Step("candidate_release");}
    void DropInputCandidate() noexcept override {++s.candidate_drops;}
    bool NextInputCandidate() override {
        if(!s.candidate_retries)return false;
        --s.candidate_retries;s.candidate_mismatch=false;return true;
    }
    void Destroy(Context) noexcept override {
        EXPECT_EQ(s.model_bytes,4u);
        EXPECT_EQ(static_cast<const unsigned char*>(s.model)[0],1u);
        s.Step("destroy");
    }
};
class FixtureLoader final : public Loader {
public:
    State state;
    std::unique_ptr<Vendor> Load(const char*, std::string& error) override {
        if(state.Step("load")){error="missing library";return {};}
        return std::make_unique<FixtureVendor>(state);
    }
};
class RknnBackend : public ::testing::Test {
protected:
    FixtureLoader loader;
    void* session=nullptr;
    HV_PluginApiV1 plugin{};
    std::filesystem::path path;
    std::string model_path;
    char detail[512]{};
    HV_ErrorBufferV1 error{sizeof(error),1,detail,sizeof(detail)};
    HV_BackendConfigV1 config{sizeof(config),1,nullptr,nullptr,"backend.rknn"};
    std::array<unsigned char,24> rgb{};
    HV_TensorViewV1 input{sizeof(input),1,"in0",3,4,{1,2,4,3},rgb.data(),24};
    HV_TensorViewV1 outputs[2]{};
    uint32_t count=0;
    void SetUp() override {
        path=std::filesystem::temp_directory_path()/std::filesystem::path("hv-rknn-backend-test-"+std::to_string(reinterpret_cast<uintptr_t>(this))+".rknn");
        std::ofstream f(path,std::ios::binary);const char model[]={1,2,3,4};f.write(model,4);f.close();
        model_path=path.u8string();config.model_path_utf8=model_path.c_str();rgb[0]=255;
        plugin.struct_size=sizeof(plugin);ASSERT_EQ(HV_QueryRknnPluginV1(1,&plugin),HV_OK);
    }
    void TearDown() override { if(session)plugin.backend->destroy(session);std::error_code ec;std::filesystem::remove(path,ec); }
    HV_Result Create() { return CreateRknnSession(&config,&session,&error,loader); }
    HV_Result Run(uint32_t capacity=2) { return plugin.backend->run(session,&input,1,outputs,capacity,&count,&error); }
};

TEST_F(RknnBackend, PublishesQueriedBorrowedShapesAndReusesBuffers) {
    ASSERT_EQ(Create(),HV_OK)<<detail;ASSERT_EQ(Run(),HV_OK)<<detail;
    ASSERT_EQ(count,2u);EXPECT_STREQ(outputs[0].name,"raw_boxes");EXPECT_EQ(outputs[0].rank,3u);EXPECT_EQ(outputs[0].dimensions[2],3);
    EXPECT_EQ(outputs[0].byte_count,24u);EXPECT_EQ(outputs[0].element_type,1u);EXPECT_FLOAT_EQ(static_cast<const float*>(outputs[0].data)[0],1);
    auto* data=outputs[0].data;ASSERT_EQ(Run(),HV_OK);EXPECT_EQ(data,outputs[0].data);EXPECT_FLOAT_EQ(static_cast<const float*>(data)[0],2);
    EXPECT_EQ(std::count(loader.state.events.begin(),loader.state.events.end(),"release"),2);
    HV_BackendSessionInfoV1 info{sizeof(info),1};ASSERT_EQ(plugin.backend->session_info(session,&info),HV_OK);
    EXPECT_STREQ(info.actual,"backend.rknn");EXPECT_STREQ(info.requested,"backend.rknn");EXPECT_EQ(info.accelerated,1u);EXPECT_EQ(info.fallback_reason[0],0);
    Diagnostics diag;ASSERT_TRUE(GetRknnDiagnostics(session,diag));EXPECT_STREQ(diag.driver_version,"test-driver");EXPECT_EQ(diag.core_mask,7u);EXPECT_EQ(diag.completed_runs,2u);
}
TEST_F(RknnBackend, InputCandidateMismatchNeverReplacesBaselineAndDisablesCandidate) {
    loader.state.candidate=true;loader.state.candidate_mismatch=true;
    config.options_utf8="{\"input_memory\":\"auto\"}";
    ASSERT_EQ(Create(),HV_OK);ASSERT_EQ(Run(),HV_OK);
    EXPECT_EQ(loader.state.candidate_runs,1u);
    EXPECT_EQ(loader.state.candidate_drops,1u);
    EXPECT_FLOAT_EQ(static_cast<const float*>(outputs[0].data)[0],1);
    ASSERT_EQ(Run(),HV_OK);EXPECT_EQ(loader.state.candidate_runs,1u);
    EXPECT_FLOAT_EQ(static_cast<const float*>(outputs[0].data)[0],2);
}
TEST_F(RknnBackend, RejectedNativeCandidateTriesConservativeCandidateWithoutPublishingMismatch) {
    loader.state.candidate=true;loader.state.candidate_mismatch=true;loader.state.candidate_retries=1;
    config.options_utf8="{\"input_memory\":\"auto\"}";
    ASSERT_EQ(Create(),HV_OK);ASSERT_EQ(Run(),HV_OK);
    EXPECT_FLOAT_EQ(static_cast<const float*>(outputs[0].data)[0],1);
    EXPECT_EQ(loader.state.candidate_retries,0u);
    humanvision::runtime::BackendStageDiagnostics diagnostic;
    ASSERT_TRUE(humanvision::runtime::CopyBackendDiagnostics(session,diagnostic));
    EXPECT_NE(std::string(diagnostic.input_path).find("previous=shared-input-qualified"),std::string::npos);
    EXPECT_NE(std::string(diagnostic.input_path).find("qualification-error-or-mismatch"),std::string::npos);
    ASSERT_EQ(Run(),HV_OK);EXPECT_EQ(loader.state.candidate_runs,2u);
    EXPECT_FLOAT_EQ(static_cast<const float*>(outputs[0].data)[0],2);
}
TEST_F(RknnBackend, ExplicitInputTransformIsValidatedWithoutChangingLegacyInputContract) {
    config.options_utf8="{\"input_transform\":{\"mean\":[0,0,0],\"norm\":[0.003921568627,0.003921568627,0.003921568627]}}";
    ASSERT_EQ(Create(),HV_OK)<<detail;ASSERT_EQ(Run(),HV_OK);
    EXPECT_EQ(loader.state.runs,1u);
}
TEST_F(RknnBackend, RejectsInvalidOrUnboundedExplicitTransform) {
    for(const char* text:{"{\"input_transform\":{\"mean\":[0,0],\"norm\":[1,1,1]}}",
        "{\"input_transform\":{\"mean\":[0,0,0],\"norm\":[1,1,10000]}}",
        "{\"input_transform\":{\"mean\":[0,0,0],\"norm\":[1,1,1],\"unknown\":true}}"}) {
        config.options_utf8=text;EXPECT_EQ(Create(),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(session,nullptr);
    }
}
TEST_F(RknnBackend, InputCandidateFailuresDoNotInvalidateTheCompletedBaselineFrame) {
    loader.state.candidate=true;loader.state.fail="candidate_run";
    config.options_utf8="{\"input_memory\":\"auto\"}";
    ASSERT_EQ(Create(),HV_OK);ASSERT_EQ(Run(),HV_OK);
    EXPECT_EQ(loader.state.candidate_runs,1u);EXPECT_EQ(loader.state.candidate_drops,1u);
    EXPECT_EQ(count,2u);EXPECT_FLOAT_EQ(static_cast<const float*>(outputs[0].data)[0],1);
    ASSERT_EQ(Run(),HV_OK);EXPECT_EQ(loader.state.candidate_runs,1u);
}
TEST_F(RknnBackend, QualifiedInputFailureRerunsSameFrameOnOriginalContext) {
    for(const char* stage:{"candidate_set","candidate_run","candidate_get","candidate_release"}) {
        loader.state=State{};loader.state.candidate=true;loader.state.slow_baseline=true;
        config.options_utf8="{\"input_memory\":\"auto\"}";
        ASSERT_EQ(Create(),HV_OK);
        for(int i=0;i<7;i++)ASSERT_EQ(Run(),HV_OK)<<detail;
        ASSERT_EQ(loader.state.candidate_drops,0u);
        ASSERT_EQ(Run(),HV_OK)<<detail;
        EXPECT_EQ(loader.state.runs,7u)<<"Qualified frame must use candidate only";
        loader.state.fail=stage;
        ASSERT_EQ(Run(),HV_OK)<<stage<<": "<<detail;
        EXPECT_EQ(loader.state.runs,8u)<<"Failure must rerun original context on the same supplied input";
        EXPECT_EQ(count,2u);EXPECT_FLOAT_EQ(static_cast<const float*>(outputs[0].data)[0],8);
        EXPECT_EQ(loader.state.candidate_drops,1u);
        Diagnostics diagnostics;ASSERT_TRUE(GetRknnDiagnostics(session,diagnostics));EXPECT_EQ(diagnostics.completed_runs,9u);
        plugin.backend->destroy(session);session=nullptr;
    }
}
TEST_F(RknnBackend, InsufficientCapacityAndInvalidInputNeverRunVendor) {
    ASSERT_EQ(Create(),HV_OK);EXPECT_EQ(Run(1),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(count,0u);EXPECT_EQ(loader.state.runs,0u);
    for(int field=0;field<7;++field){auto original=input;
        if(field==0)input.element_type=1;if(field==1)input.rank=9;if(field==2)input.dimensions[1]=4;
        if(field==3)input.byte_count=23;if(field==4)input.api_version=2;if(field==5)input.struct_size=8;if(field==6)input.data=nullptr;
        EXPECT_EQ(Run(),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(count,0u);input=original;
    }
    EXPECT_EQ(loader.state.runs,0u);
}
TEST_F(RknnBackend, FailedStagesCleanContextBeforeUnloadAndKeepNullSession) {
    for(const auto* stage: {"load","init","versions","core","io","input_query","output_query","prepare"}){
        loader.state.events.clear();loader.state.fail=stage;loader.state.partial_init=true;
        EXPECT_NE(Create(),HV_OK)<<stage;EXPECT_EQ(session,nullptr)<<stage;EXPECT_NE(detail[0],0);
        if(std::string(stage)!="load"){
            const auto& e=loader.state.events;ASSERT_GE(e.size(),2u);EXPECT_EQ(e[e.size()-2],"destroy");EXPECT_EQ(e.back(),"unload");
        }
    }
}
TEST_F(RknnBackend, RejectsIncompatibleAttributesAndOverflowBeforeRun) {
    auto original=loader.state.input;
    for(int field=0;field<7;++field){loader.state.input=original;
        auto& a=loader.state.input;if(field==0)a.rank=9;if(field==1)a.dimensions[0]=2;if(field==2)a.elements=23;
        if(field==3)a.layout=Layout::Unknown;if(field==4)a.type=Type::Unknown;if(field==5)a.dimensions[1]=4;if(field==6)a.dimensions[3]=UINT64_MAX;
        EXPECT_NE(Create(),HV_OK);EXPECT_EQ(session,nullptr);
    }
    loader.state.input=original;auto out=loader.state.outputs;
    for(int field=0;field<5;++field){loader.state.outputs=out;auto& a=loader.state.outputs[0];
        if(field==0)a.rank=9;if(field==1)a.dimensions[1]=UINT64_MAX;if(field==2)a.elements=7;if(field==3)a.type=Type::Unknown;if(field==4)a.name[0]=0;
        EXPECT_NE(Create(),HV_OK);EXPECT_EQ(session,nullptr);
    }
}
TEST_F(RknnBackend, RunStageErrorsPublishNoOutputAndReleaseSuccessfulBorrowExactlyOnce) {
    ASSERT_EQ(Create(),HV_OK);
    for(const auto* stage:{"inputs_set","run","get","release"}){
        loader.state.events.clear();loader.state.fail=stage;count=99;outputs[0].data=rgb.data();
        EXPECT_NE(Run(),HV_OK)<<stage;EXPECT_EQ(count,0u);EXPECT_EQ(outputs[0].data,nullptr);
        EXPECT_EQ(std::count(loader.state.events.begin(),loader.state.events.end(),"release"),std::string(stage)=="release"?1:0);
    }
    loader.state.fail.clear();plugin.backend->destroy(session);session=nullptr;ASSERT_EQ(Create(),HV_OK);
    loader.state.nan=true;loader.state.events.clear();EXPECT_NE(Run(),HV_OK);EXPECT_EQ(count,0u);
    EXPECT_EQ(std::count(loader.state.events.begin(),loader.state.events.end(),"release"),1);
    loader.state.nan=false;loader.state.changed_buffer=true;loader.state.events.clear();EXPECT_NE(Run(),HV_OK);EXPECT_EQ(count,0u);
    EXPECT_EQ(std::count(loader.state.events.begin(),loader.state.events.end(),"release"),1);
}
TEST_F(RknnBackend, ValidatesOptionsAndReadsNhwcAttributesWithoutGuessing) {
    for(const char* bad:{"[]","{","{\"core_mask\":0}","{\"core_mask\":6}","{\"core_mask\":1.5}","{\"input_layout\":\"nchw\"}","{\"input_type\":\"float32\"}","{\"runtime_library\":\"\"}","{\"unknown\":1}"}) {
        config.options_utf8=bad;EXPECT_EQ(Create(),HV_ERR_INVALID_ARGUMENT)<<bad;EXPECT_EQ(session,nullptr);
    }
    config.options_utf8="{\"core_mask\":1,\"input_layout\":\"nhwc\",\"input_type\":\"uint8\"}";
    loader.state.input.layout=Layout::Nhwc;loader.state.input.dimensions[1]=2;loader.state.input.dimensions[2]=4;loader.state.input.dimensions[3]=3;
    ASSERT_EQ(Create(),HV_OK)<<detail;ASSERT_EQ(Run(),HV_OK);EXPECT_EQ(loader.state.mask,1u);
}
TEST_F(RknnBackend, IndependentInstancesCanSelectEachSingleCoreOrAllCores) {
    for(unsigned mask:{1u,2u,4u,7u}) {
        const auto options=std::string("{\"core_mask\":")+std::to_string(mask)+"}";
        config.options_utf8=options.c_str();ASSERT_EQ(Create(),HV_OK)<<detail;
        ASSERT_EQ(Run(),HV_OK);EXPECT_EQ(loader.state.mask,mask);
        plugin.backend->destroy(session);session=nullptr;
    }
}
TEST_F(RknnBackend, RejectsMissingModelWrongProviderAndTruncatedContracts) {
    config.model_path_utf8="missing-rknn-model";EXPECT_EQ(Create(),HV_ERR_MODEL_LOAD);EXPECT_EQ(session,nullptr);
    config.model_path_utf8=model_path.c_str();config.requested_provider_utf8="CPU";EXPECT_EQ(Create(),HV_ERR_INVALID_ARGUMENT);
    config.requested_provider_utf8="backend.rknn";config.struct_size=8;EXPECT_EQ(Create(),HV_ERR_INVALID_ARGUMENT);
    config.struct_size=sizeof(config);config.api_version=2;EXPECT_EQ(Create(),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(CreateRknnSession(nullptr,&session,&error,loader),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(CreateRknnSession(&config,nullptr,&error,loader),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(HV_QueryRknnPluginV1(2,&plugin),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(HV_QueryRknnPluginV1(1,nullptr),HV_ERR_INVALID_ARGUMENT);
}
TEST_F(RknnBackend, DisabledHostNeverPretendsToSupportHardware) {
#if !defined(__ANDROID__) || !defined(HV_ENABLE_RKNN)
    EXPECT_NE(plugin.backend->create(&config,&session,&error),HV_OK);EXPECT_EQ(session,nullptr);
    EXPECT_NE(std::string(detail).find("RKNN"),std::string::npos);
    HV_BackendSessionInfoV1 info{sizeof(info),1};EXPECT_EQ(plugin.backend->session_info(nullptr,&info),HV_ERR_INVALID_ARGUMENT);
#endif
}
TEST_F(RknnBackend, ReleaseFailureStopsFurtherRunsAndDestroyDoesNotDoubleRelease) {
    ASSERT_EQ(Create(),HV_OK);loader.state.fail="release";EXPECT_NE(Run(),HV_OK);const auto runs=loader.state.runs;
    loader.state.fail.clear();EXPECT_NE(Run(),HV_OK);EXPECT_EQ(loader.state.runs,runs);
    plugin.backend->destroy(session);session=nullptr;
    const auto& e=loader.state.events;EXPECT_EQ(std::count(e.begin(),e.end(),"release"),1);
    ASSERT_GE(e.size(),3u);EXPECT_EQ(e[e.size()-3],"release");EXPECT_EQ(e[e.size()-2],"destroy");EXPECT_EQ(e.back(),"unload");
}
TEST_F(RknnBackend, PublicCreateAndCallbacksValidateNullAndVersionsEvenWhenDisabled) {
    EXPECT_EQ(plugin.backend->create(nullptr,&session,&error),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(plugin.backend->create(&config,nullptr,&error),HV_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(Create(),HV_OK);
    EXPECT_EQ(plugin.backend->run(session,nullptr,1,outputs,2,&count,&error),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(plugin.backend->run(session,&input,0,outputs,2,&count,&error),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(plugin.backend->run(session,&input,1,nullptr,2,&count,&error),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(plugin.backend->run(session,&input,1,outputs,2,nullptr,&error),HV_ERR_INVALID_ARGUMENT);
    HV_BackendSessionInfoV1 info{sizeof(info),2};EXPECT_EQ(plugin.backend->session_info(session,&info),HV_ERR_INVALID_ARGUMENT);
    info.api_version=1;info.struct_size=8;EXPECT_EQ(plugin.backend->session_info(session,&info),HV_ERR_INVALID_ARGUMENT);
    Diagnostics diagnostic;EXPECT_FALSE(GetRknnDiagnostics(nullptr,diagnostic));
}
TEST_F(RknnBackend, PublishesGenericDiagnosticsAndUnregistersOnDestroy) {
    ASSERT_EQ(Create(),HV_OK);humanvision::runtime::BackendStageDiagnostics stages;
    ASSERT_TRUE(humanvision::runtime::CopyBackendDiagnostics(session,stages));EXPECT_STREQ(stages.runtime_version,"test-runtime");EXPECT_EQ(stages.core_mask,7u);
    ASSERT_EQ(Run(),HV_OK);ASSERT_TRUE(humanvision::runtime::CopyBackendDiagnostics(session,stages));EXPECT_GE(stages.execute_ms,0.0f);
    const auto* identity=session;plugin.backend->destroy(session);session=nullptr;
    EXPECT_FALSE(humanvision::runtime::CopyBackendDiagnostics(identity,stages));
}
TEST_F(RknnBackend, RejectsReorderedOutputDescriptorsBeforePublicationAndReleasesThem) {
    ASSERT_EQ(Create(),HV_OK);loader.state.changed_index=true;
    EXPECT_NE(Run(),HV_OK);EXPECT_EQ(count,0u);EXPECT_EQ(outputs[0].data,nullptr);
    EXPECT_EQ(std::count(loader.state.events.begin(),loader.state.events.end(),"release"),1);
}
TEST_F(RknnBackend, RejectsChangedOutputByteCountThenResetsReusableDescriptors) {
    ASSERT_EQ(Create(),HV_OK);loader.state.changed_bytes=true;
    EXPECT_EQ(Run(),HV_ERR_INTERNAL);EXPECT_EQ(count,0u);EXPECT_EQ(outputs[0].data,nullptr);
    EXPECT_EQ(std::count(loader.state.events.begin(),loader.state.events.end(),"release"),1);
    loader.state.changed_bytes=false;
    ASSERT_EQ(Run(),HV_OK)<<detail;EXPECT_EQ(count,2u);EXPECT_EQ(outputs[0].byte_count,24u);
}
TEST_F(RknnBackend, RejectsInvalidIoCountsAndOutputNamesIndicesOrZeroDimensions) {
    loader.state.input_count=0;EXPECT_EQ(Create(),HV_ERR_MODEL_LOAD);EXPECT_EQ(session,nullptr);
    loader.state.input_count=2;EXPECT_EQ(Create(),HV_ERR_MODEL_LOAD);EXPECT_EQ(session,nullptr);
    loader.state.input_count=1;const auto originals=loader.state.outputs;
    loader.state.outputs.clear();EXPECT_EQ(Create(),HV_ERR_MODEL_LOAD);EXPECT_EQ(session,nullptr);
    loader.state.outputs.resize(65);EXPECT_EQ(Create(),HV_ERR_MODEL_LOAD);EXPECT_EQ(session,nullptr);
    for(int field=0;field<5;++field) {
        loader.state.outputs=originals;
        if(field==0)loader.state.outputs[0].index=1;
        if(field==1)std::strcpy(loader.state.outputs[1].name,"raw_boxes");
        if(field==2)std::memset(loader.state.outputs[0].name,'x',sizeof(loader.state.outputs[0].name));
        if(field==3)loader.state.outputs[0].rank=0;
        if(field==4)loader.state.outputs[0].dimensions[1]=0;
        EXPECT_EQ(Create(),HV_ERR_MODEL_LOAD)<<field;EXPECT_EQ(session,nullptr);
    }
}
TEST_F(RknnBackend, VendorExceptionsCannotEscapeCreateOrRunAndStillRetireContext) {
    for(const auto* stage:{"versions","input_query","output_query","prepare"}) {
        loader.state.events.clear();loader.state.throw_stage=stage;
        EXPECT_EQ(Create(),HV_ERR_INTERNAL)<<stage;EXPECT_EQ(session,nullptr);
        const auto& events=loader.state.events;ASSERT_GE(events.size(),2u);
        EXPECT_EQ(events[events.size()-2],"destroy");EXPECT_EQ(events.back(),"unload");
    }
    loader.state.throw_stage.clear();ASSERT_EQ(Create(),HV_OK);
    for(const auto* stage:{"inputs_set","run","get"}) {
        loader.state.throw_stage=stage;count=99;
        EXPECT_EQ(Run(),HV_ERR_INTERNAL)<<stage;EXPECT_EQ(count,0u);EXPECT_EQ(outputs[0].data,nullptr);
    }
    loader.state.throw_stage.clear();ASSERT_EQ(Run(),HV_OK);
}
TEST_F(RknnBackend, RetainsModelBytesAfterSourceRemovalAndReusesOutputStorageAcrossRuns) {
    ASSERT_EQ(Create(),HV_OK);ASSERT_TRUE(std::filesystem::remove(path));
    const void* addresses[2]{};
    for(int iteration=0;iteration<16;++iteration) {
        ASSERT_EQ(Run(),HV_OK)<<detail;
        for(int index=0;index<2;++index) {
            if(iteration==0)addresses[index]=outputs[index].data;
            EXPECT_EQ(outputs[index].data,addresses[index]);
        }
    }
    Diagnostics diagnostic;ASSERT_TRUE(GetRknnDiagnostics(session,diagnostic));EXPECT_EQ(diagnostic.completed_runs,16u);
    EXPECT_EQ(std::count(loader.state.events.begin(),loader.state.events.end(),"release"),16);
}
TEST_F(RknnBackend, InvalidErrorBuffersStayUntouchedAndSingleByteErrorsAreTerminated) {
    ASSERT_EQ(Create(),HV_OK);input.byte_count=23;
    std::memset(detail,'x',sizeof(detail));error.struct_size=8;
    EXPECT_EQ(Run(),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(detail[0],'x');
    error.struct_size=sizeof(error);error.api_version=2;
    EXPECT_EQ(Run(),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(detail[0],'x');
    error.api_version=1;error.capacity=0;
    EXPECT_EQ(Run(),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(detail[0],'x');
    error.capacity=1;
    EXPECT_EQ(Run(),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(detail[0],0);EXPECT_EQ(detail[1],'x');
    EXPECT_EQ(plugin.backend->run(session,&input,1,outputs,2,&count,nullptr),HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(count,0u);EXPECT_EQ(loader.state.runs,0u);
}
class FixtureLibrary final : public Library {
public:
    bool missing_library=false;std::string missing_symbol;int closes=0;
    bool Open(const char*, std::string& error) override { if(missing_library){error="library missing";return false;}return true; }
    void* Symbol(const char* name) override { return missing_symbol==name?nullptr:this; }
    void Close() noexcept override { ++closes; }
};
TEST(RknnLibrary, RejectsMissingLibraryAndEveryRequiredSymbolWithOneClose) {
    FixtureLibrary lib;Symbols symbols;std::string error;lib.missing_library=true;
    EXPECT_FALSE(ResolveSymbols(lib,"fake.so",symbols,error));EXPECT_EQ(lib.closes,0);
    lib.missing_library=false;
    for(const char* name:{"rknn_init","rknn_destroy","rknn_query","rknn_set_core_mask","rknn_inputs_set","rknn_run","rknn_outputs_get","rknn_outputs_release"}){
        lib.missing_symbol=name;lib.closes=0;EXPECT_FALSE(ResolveSymbols(lib,"fake.so",symbols,error));EXPECT_EQ(lib.closes,1);EXPECT_NE(error.find(name),std::string::npos);
    }
}
}
