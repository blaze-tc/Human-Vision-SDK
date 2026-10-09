#include "plugins/backend/rknn/rknn_backend.h"
#include "plugins/backend/rknn/rknn_vendor_boundary.h"
#include "common/backend_diagnostics.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace humanvision::runtime::rknn;
struct State {
    std::vector<std::string> events;
    std::string fail;
    std::string throw_stage;
    bool partial_init = false, nan = false, changed_buffer = false, changed_index=false, changed_bytes=false;
    uint32_t mask = 0, runs = 0, input_count = 1;
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
    for(const char* bad:{"[]","{","{\"core_mask\":0}","{\"core_mask\":2}","{\"core_mask\":1.5}","{\"input_layout\":\"nchw\"}","{\"input_type\":\"float32\"}","{\"runtime_library\":\"\"}","{\"unknown\":1}"}) {
        config.options_utf8=bad;EXPECT_EQ(Create(),HV_ERR_INVALID_ARGUMENT)<<bad;EXPECT_EQ(session,nullptr);
    }
    config.options_utf8="{\"core_mask\":1,\"input_layout\":\"nhwc\",\"input_type\":\"uint8\"}";
    loader.state.input.layout=Layout::Nhwc;loader.state.input.dimensions[1]=2;loader.state.input.dimensions[2]=4;loader.state.input.dimensions[3]=3;
    ASSERT_EQ(Create(),HV_OK)<<detail;ASSERT_EQ(Run(),HV_OK);EXPECT_EQ(loader.state.mask,1u);
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
