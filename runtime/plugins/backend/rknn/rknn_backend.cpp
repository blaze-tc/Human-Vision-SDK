#include "plugins/backend/rknn/rknn_vendor_boundary.h"
#include "common/backend_diagnostics.h"
#include "json/json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace humanvision::runtime::rknn {
namespace {
using Clock = std::chrono::steady_clock;
double Elapsed(Clock::time_point a) { return std::chrono::duration<double,std::milli>(Clock::now()-a).count(); }
void Error(HV_ErrorBufferV1* out, const char* text) noexcept {
    if (!out || out->struct_size<sizeof(*out) || out->api_version!=1 || !out->data || !out->capacity) return;
    const auto n=std::min(std::strlen(text),size_t(out->capacity-1));std::memcpy(out->data,text,n);out->data[n]=0;
}
void StageError(HV_ErrorBufferV1* out, const char* stage, int code) {
    Error(out,(std::string("RKNN ")+stage+" failed ("+std::to_string(code)+"); no fallback").c_str());
}
struct Options { std::string library="librknnrt.so";uint32_t mask=7; };
bool ParseOptions(const char* text, Options& out, HV_ErrorBufferV1* error) {
    if (!text || !*text) return true;
    try {
        const auto json=nlohmann::json::parse(text);
        if (!json.is_object()) throw std::invalid_argument("expected object");
        for (auto item=json.begin();item!=json.end();++item) {
            const auto& key=item.key();const auto& value=item.value();
            if (key=="runtime_library") {
                if (!value.is_string()) throw std::invalid_argument("runtime_library must be a string");
                out.library=value.get<std::string>();
                if (out.library.empty() || out.library.find('\0')!=std::string::npos) throw std::invalid_argument("runtime_library must be nonempty");
            } else if (key=="core_mask") {
                if (!value.is_number_integer() || (value!=1 && value!=7)) throw std::invalid_argument("core_mask must be 1 or 7");
                out.mask=value.get<uint32_t>();
            } else if (key=="input_layout") {
                if (!value.is_string() || value!="nhwc") throw std::invalid_argument("input_layout must be nhwc");
            } else if (key=="input_type") {
                if (!value.is_string() || value!="uint8") throw std::invalid_argument("input_type must be uint8");
            } else throw std::invalid_argument("unknown option");
        }
        return true;
    } catch (const std::exception& e) { Error(error,(std::string("RKNN options: ")+e.what()).c_str());return false; }
}
bool Elements(const Attribute& a, uint64_t& elements) {
    if (!a.rank || a.rank>8 || a.type==Type::Unknown || !a.name[0] || !std::memchr(a.name,0,sizeof(a.name))) return false;
    elements=1;
    for (uint32_t i=0;i<a.rank;++i) {
        if (!a.dimensions[i] || a.dimensions[i]>uint64_t(std::numeric_limits<int64_t>::max()) ||
            a.dimensions[i]>uint64_t(UINT32_MAX)/sizeof(float)/elements) return false;
        elements*=a.dimensions[i];
    }
    return elements==a.elements;
}
struct Session {
    std::unique_ptr<Vendor> vendor;
    Context context=0;
    std::vector<unsigned char> model;
    Attribute input;
    uint32_t height=0,width=0,input_bytes=0;
    std::vector<Attribute> attributes;
    std::vector<std::vector<float>> buffers;
    std::vector<OutputBuffer> output_buffers;
    Diagnostics diagnostic;
    bool borrowed=false,poisoned=false;
    void Publish() noexcept {
        BackendStageDiagnostics value;
        std::memcpy(value.runtime_version,diagnostic.runtime_version,sizeof(value.runtime_version));
        std::memcpy(value.driver_version,diagnostic.driver_version,sizeof(value.driver_version));
        value.core_mask=diagnostic.core_mask;value.initialize_ms=float(diagnostic.init_ms);
        value.input_set_ms=float(diagnostic.inputs_set_ms);value.execute_ms=float(diagnostic.run_ms);
        value.output_get_ms=float(diagnostic.outputs_get_ms);value.output_release_ms=float(diagnostic.outputs_release_ms);
        try { PublishBackendDiagnostics(this,value); } catch (...) {}
    }
    int Release() noexcept {
        if (!borrowed) return 0;
        borrowed=false;const auto start=Clock::now();
        const int result=vendor->OutputsRelease(context,uint32_t(output_buffers.size()));
        diagnostic.outputs_release_ms=Elapsed(start);
        if (result) poisoned=true;
        return result;
    }
    ~Session() {
        try { UnregisterBackendDiagnostics(this); } catch (...) {}
        if (vendor) { Release();if (context) vendor->Destroy(context); }
        // Vendor/library is destroyed before model memory, after native context.
        vendor.reset();
    }
};
bool ReadModel(const char* path, std::vector<unsigned char>& data, HV_ErrorBufferV1* error) {
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
    const auto size=file?file.tellg():std::streampos(-1);
    if (size<=0 || size>std::streampos(256ull*1024*1024)) { Error(error,"RKNN model is missing, empty, unreadable or exceeds 256 MiB; check model_path_utf8");return false; }
    data.resize(size_t(size));file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size()))) { Error(error,"RKNN model read failed");return false; }
    return true;
}
void HV_CALL Destroy(void* value) { delete static_cast<Session*>(value); }
HV_Result HV_CALL Run(void* value, const HV_TensorViewV1* input, uint32_t inputs,
    HV_TensorViewV1* output, uint32_t capacity, uint32_t* count, HV_ErrorBufferV1* error) {
    if (count) *count=0;
    auto* session=static_cast<Session*>(value);
    if (output && capacity) std::fill_n(output,session?std::min(capacity,uint32_t(session->attributes.size())):1u,HV_TensorViewV1{});
    if (!session || !input || inputs!=1 || !output || !count || input->struct_size<sizeof(*input) ||
        input->api_version!=1 || input->element_type!=3 || input->rank!=4 || !input->data) {
        Error(error,"RKNN expects one ABI1 UINT8 RGB NHWC tensor and caller-owned output views");return HV_ERR_INVALID_ARGUMENT;
    }
    if (capacity<session->attributes.size()) { Error(error,"RKNN output tensor capacity is too small; no inference submitted");return HV_ERR_INVALID_ARGUMENT; }
    if (input->dimensions[0]!=1 || input->dimensions[1]!=session->height || input->dimensions[2]!=session->width ||
        input->dimensions[3]!=3 || input->byte_count!=session->input_bytes ||
        (input->name && *input->name && std::strcmp(input->name,session->input.name))) {
        Error(error,"RKNN input name/shape/byte count mismatch; expected tightly packed [1,H,W,3] RGB uint8");return HV_ERR_INVALID_ARGUMENT;
    }
    if (session->poisoned) { Error(error,"RKNN session output release failed; destroy and recreate the session");return HV_ERR_INTERNAL; }
    struct PublishOnExit { Session& session;~PublishOnExit(){session.Publish();} } publication{*session};
    try {
        auto& s=*session;s.diagnostic.inputs_set_ms=s.diagnostic.run_ms=s.diagnostic.outputs_get_ms=s.diagnostic.outputs_release_ms=0;
        // Reset descriptor copies in case a vendor changed returned pointers/sizes.
        for (size_t i=0;i<s.buffers.size();++i) s.output_buffers[i]={uint32_t(i),s.buffers[i].data(),uint32_t(s.buffers[i].size()*sizeof(float))};
        auto started=Clock::now();const int input_status=s.vendor->InputsSet(s.context,input->data,s.input_bytes);s.diagnostic.inputs_set_ms=Elapsed(started);
        if (input_status) { StageError(error,"inputs_set",input_status);return HV_ERR_INTERNAL; }
        started=Clock::now();const int run_status=s.vendor->Run(s.context);s.diagnostic.run_ms=Elapsed(started);
        if (run_status) { StageError(error,"run",run_status);return HV_ERR_INTERNAL; }
        started=Clock::now();const int get_status=s.vendor->OutputsGet(s.context,s.output_buffers.data(),uint32_t(s.output_buffers.size()));s.diagnostic.outputs_get_ms=Elapsed(started);
        if (get_status) { StageError(error,"outputs_get",get_status);return HV_ERR_INTERNAL; }
        s.borrowed=true;
        const char* invalid=nullptr;
        for (size_t i=0;i<s.buffers.size();++i) {
            if (s.output_buffers[i].index!=s.attributes[i].index) { invalid="RKNN changed the queried output tensor order";break; }
            if (s.output_buffers[i].data!=s.buffers[i].data() || s.output_buffers[i].bytes!=s.buffers[i].size()*sizeof(float)) { invalid="RKNN changed a preallocated output pointer or byte count";break; }
            if (!std::all_of(s.buffers[i].begin(),s.buffers[i].end(),[](float v){return std::isfinite(v);})) { invalid="RKNN returned a nonfinite FP32 output";break; }
        }
        const int release_status=s.Release();
        if (release_status) { StageError(error,"outputs_release",release_status);return HV_ERR_INTERNAL; }
        if (invalid) { Error(error,invalid);return HV_ERR_INTERNAL; }
        for (size_t i=0;i<s.attributes.size();++i) {
            const auto& a=s.attributes[i];auto& v=output[i];v.struct_size=sizeof(v);v.api_version=1;
            v.name=a.name;v.element_type=1;v.rank=a.rank;
            for(uint32_t j=0;j<a.rank;++j)v.dimensions[j]=int64_t(a.dimensions[j]);
            v.data=s.buffers[i].data();v.byte_count=s.buffers[i].size()*sizeof(float);
        }
        ++s.diagnostic.completed_runs;*count=uint32_t(s.attributes.size());return HV_OK;
    } catch (const std::exception& e) { session->Release();Error(error,e.what());return HV_ERR_INTERNAL; }
    catch (...) { session->Release();Error(error,"RKNN inference exception");return HV_ERR_INTERNAL; }
}
HV_Result HV_CALL Info(void* value, HV_BackendSessionInfoV1* out) {
    if (!value || !out || out->struct_size<sizeof(*out) || out->api_version!=1) return HV_ERR_INVALID_ARGUMENT;
    *out={};out->struct_size=sizeof(*out);out->api_version=1;
    std::strcpy(out->requested,"backend.rknn");std::strcpy(out->actual,"backend.rknn");out->accelerated=1;return HV_OK;
}
HV_Result HV_CALL Create(const HV_BackendConfigV1* config, void** out, HV_ErrorBufferV1* error) {
    if (out) *out=nullptr;
    if (!out || !config || config->struct_size<sizeof(*config) || config->api_version!=1 ||
        !config->model_path_utf8 || !*config->model_path_utf8) {
        Error(error,"RKNN create expects an ABI1 config, output handle and model path");return HV_ERR_INVALID_ARGUMENT;
    }
    try {
        auto loader=MakePlatformLoader();
        if (!loader) { Error(error,"RKNN unavailable: requires Android arm64 and HV_ENABLE_RKNN with the private vendor header/runtime; no fallback");return HV_ERR_NOT_INITIALIZED; }
        return CreateRknnSession(config,out,error,*loader);
    } catch (const std::exception& e) { Error(error,e.what());return HV_ERR_INTERNAL; }
    catch (...) { Error(error,"RKNN loader creation exception");return HV_ERR_INTERNAL; }
}
const HV_BackendApiV1 api{sizeof(api),1,Create,Destroy,Run,Info};
}

HV_Result CreateRknnSession(const HV_BackendConfigV1* config, void** out, HV_ErrorBufferV1* error, Loader& loader) {
    if (!out) return HV_ERR_INVALID_ARGUMENT;*out=nullptr;
    if (!config || config->struct_size<sizeof(*config) || config->api_version!=1 || !config->model_path_utf8 || !*config->model_path_utf8) {
        Error(error,"RKNN create expects an ABI1 config with a model path");return HV_ERR_INVALID_ARGUMENT;
    }
    if (config->requested_provider_utf8 && *config->requested_provider_utf8 && std::strcmp(config->requested_provider_utf8,"backend.rknn")) {
        Error(error,"RKNN cannot satisfy the requested provider; select backend.rknn");return HV_ERR_INVALID_ARGUMENT;
    }
    try {
        Options options;if (!ParseOptions(config->options_utf8,options,error))return HV_ERR_INVALID_ARGUMENT;
        auto s=std::make_unique<Session>();
        if (!ReadModel(config->model_path_utf8,s->model,error))return HV_ERR_MODEL_LOAD;
        std::string detail;s->vendor=loader.Load(options.library.c_str(),detail);
        if (!s->vendor) { Error(error,detail.empty()?"RKNN runtime load failed; no fallback":detail.c_str());return HV_ERR_MODEL_LOAD; }
        auto started=Clock::now();const int init=s->vendor->Initialize(s->context,s->model.data(),uint32_t(s->model.size()));s->diagnostic.init_ms=Elapsed(started);
        if (init || !s->context) { StageError(error,"init",init?init:-1);return HV_ERR_MODEL_LOAD; }
        auto status=s->vendor->Versions(s->context,s->diagnostic.runtime_version,s->diagnostic.driver_version,256);
        s->diagnostic.runtime_version[255]=s->diagnostic.driver_version[255]=0;
        if (status) { StageError(error,"version query",status);return HV_ERR_MODEL_LOAD; }
        status=s->vendor->SetCoreMask(s->context,options.mask);
        if (status) { StageError(error,"core selection",status);return HV_ERR_MODEL_LOAD; }s->diagnostic.core_mask=options.mask;
        uint32_t inputs=0,outputs=0;status=s->vendor->IoCount(s->context,inputs,outputs);
        if (status) { StageError(error,"IO query",status);return HV_ERR_MODEL_LOAD; }
        if (inputs!=1 || !outputs || outputs>64) { Error(error,"RKNN requires one image input and 1-64 output tensors");return HV_ERR_MODEL_LOAD; }
        status=s->vendor->InputAttribute(s->context,s->input);
        if (status) { StageError(error,"input attribute query",status);return HV_ERR_MODEL_LOAD; }
        uint64_t elements=0;const auto& a=s->input;
        if (!Elements(a,elements) || a.index!=0 || a.rank!=4 || a.dimensions[0]!=1 ||
            (a.layout!=Layout::Nchw && a.layout!=Layout::Nhwc) ||
            (a.layout==Layout::Nchw?a.dimensions[1]!=3:a.dimensions[3]!=3)) {
            Error(error,"RKNN model input attributes must describe batch1 RGB in actual NCHW or NHWC format");return HV_ERR_MODEL_LOAD;
        }
        s->height=uint32_t(a.dimensions[a.layout==Layout::Nchw?2:1]);s->width=uint32_t(a.dimensions[a.layout==Layout::Nchw?3:2]);s->input_bytes=uint32_t(elements);
        s->attributes.resize(outputs);s->buffers.resize(outputs);s->output_buffers.resize(outputs);uint64_t total=0;
        for (uint32_t i=0;i<outputs;++i) {
            auto& attr=s->attributes[i];attr.index=i;status=s->vendor->OutputAttribute(s->context,attr);
            if (status) { StageError(error,"output attribute query",status);return HV_ERR_MODEL_LOAD; }
            if (!Elements(attr,elements) || attr.index!=i || elements>64ull*1024*1024/sizeof(float) ||
                total+elements>128ull*1024*1024/sizeof(float)) {
                Error(error,"RKNN output attributes are unsupported, inconsistent or exceed bounded FP32 storage");return HV_ERR_MODEL_LOAD;
            }
            for(uint32_t previous=0;previous<i;++previous)if(!std::strcmp(attr.name,s->attributes[previous].name)) {
                Error(error,"RKNN output tensor names must be unique");return HV_ERR_MODEL_LOAD;
            }
            total+=elements;s->buffers[i].resize(size_t(elements));s->output_buffers[i]={i,s->buffers[i].data(),uint32_t(elements*sizeof(float))};
        }
        status=s->vendor->PrepareOutputs(s->output_buffers.data(),outputs);
        if (status) { StageError(error,"output preparation",status);return HV_ERR_MODEL_LOAD; }
        RegisterBackendDiagnostics(s.get());s->Publish();
        *out=s.release();return HV_OK;
    } catch (const std::exception& e) { Error(error,e.what());return HV_ERR_INTERNAL; }
    catch (...) { Error(error,"RKNN session creation exception");return HV_ERR_INTERNAL; }
}
bool GetRknnDiagnostics(void* session, Diagnostics& out) noexcept {
    if (!session) return false;out=static_cast<Session*>(session)->diagnostic;return true;
}
bool ResolveSymbols(Library& library, const char* path, Symbols& symbols, std::string& error) {
    symbols={};if (!library.Open(path,error))return false;
    const char* names[]={"rknn_init","rknn_destroy","rknn_query","rknn_set_core_mask","rknn_inputs_set","rknn_run","rknn_outputs_get","rknn_outputs_release"};
    for(size_t i=0;i<8;++i) {
        symbols.values[i]=library.Symbol(names[i]);
        if (!symbols.values[i]) { library.Close();symbols={};error=std::string("RKNN runtime missing symbol: ")+names[i];return false; }
    }
    return true;
}
}
extern "C" HV_Result HV_CALL HV_QueryRknnPluginV1(uint32_t version, HV_PluginApiV1* out) {
    if (version!=1 || !out || out->struct_size<sizeof(*out))return HV_ERR_INVALID_ARGUMENT;
    *out={sizeof(*out),1,"backend.rknn","0.4.0-preview.6",HV_PLUGIN_BACKEND,HV_CAP_TENSOR_INFERENCE,0,nullptr,&humanvision::runtime::rknn::api,120};return HV_OK;
}
