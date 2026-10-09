#include "plugins/backend/rknn/rknn_vendor_boundary.h"
#if defined(__ANDROID__) && defined(HV_ENABLE_RKNN)
#include "rknn_api.h"
#include <dlfcn.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace humanvision::runtime::rknn {
namespace {
class AndroidLibrary final : public Library {
    void* module=nullptr;
public:
    ~AndroidLibrary() override { Close(); }
    bool Open(const char* path, std::string& error) override {
        module=dlopen(path,RTLD_NOW|RTLD_LOCAL);
        if (!module) { const char* reason=dlerror();error=std::string("RKNN runtime dlopen failed: ")+(reason?reason:path);return false; }
        return true;
    }
    void* Symbol(const char* name) override { return dlsym(module,name); }
    void Close() noexcept override { if(module){dlclose(module);module=nullptr;} }
};
Type NativeType(rknn_tensor_type t) {
    switch(t) {
        case RKNN_TENSOR_FLOAT32:return Type::Float32;case RKNN_TENSOR_FLOAT16:return Type::Float16;
        case RKNN_TENSOR_UINT8:return Type::UInt8;case RKNN_TENSOR_INT8:return Type::Int8;
        case RKNN_TENSOR_INT16:return Type::Int16;case RKNN_TENSOR_UINT16:return Type::UInt16;
        case RKNN_TENSOR_INT32:return Type::Int32;case RKNN_TENSOR_UINT32:return Type::UInt32;
        case RKNN_TENSOR_INT64:return Type::Int64;default:return Type::Unknown;
    }
}
class AndroidVendor final : public Vendor {
    std::unique_ptr<AndroidLibrary> library;
    decltype(&rknn_init) init;
    decltype(&rknn_destroy) destroy;
    decltype(&rknn_query) query;
    decltype(&rknn_set_core_mask) core;
    decltype(&rknn_inputs_set) inputs;
    decltype(&rknn_run) run;
    decltype(&rknn_outputs_get) get;
    decltype(&rknn_outputs_release) release;
    std::vector<rknn_output> native_outputs;
    std::vector<OutputBuffer> expected_outputs;
    int QueryAttribute(Context context, Attribute& a, rknn_query_cmd command) {
        rknn_tensor_attr native{};native.index=a.index;
        const int result=query(rknn_context(context),command,&native,sizeof(native));if(result)return result;
        a={};a.index=native.index;a.rank=native.n_dims;a.elements=native.n_elems;a.type=NativeType(native.type);
        a.layout=native.fmt==RKNN_TENSOR_NCHW?Layout::Nchw:native.fmt==RKNN_TENSOR_NHWC?Layout::Nhwc:Layout::Unknown;
        for(uint32_t i=0;i<std::min(a.rank,8u);++i)a.dimensions[i]=native.dims[i];
        static_assert(RKNN_MAX_NAME_LEN<=sizeof(a.name),"vendor name exceeds private boundary");
        std::memcpy(a.name,native.name,RKNN_MAX_NAME_LEN);return 0;
    }
public:
    AndroidVendor(std::unique_ptr<AndroidLibrary> lib, const Symbols& s) : library(std::move(lib)),
        init(reinterpret_cast<decltype(init)>(s.values[0])),destroy(reinterpret_cast<decltype(destroy)>(s.values[1])),
        query(reinterpret_cast<decltype(query)>(s.values[2])),core(reinterpret_cast<decltype(core)>(s.values[3])),
        inputs(reinterpret_cast<decltype(inputs)>(s.values[4])),run(reinterpret_cast<decltype(run)>(s.values[5])),
        get(reinterpret_cast<decltype(get)>(s.values[6])),release(reinterpret_cast<decltype(release)>(s.values[7])) {}
    int Initialize(Context& context, const void* model, uint32_t bytes) override {
        rknn_context native=0;
        // Zero flags: synchronous result, no ASYNC_MASK or COLLECT_PERF_MASK.
        const int result=init(&native,const_cast<void*>(model),bytes,0,nullptr);context=Context(native);return result;
    }
    int Versions(Context context, char* runtime, char* driver, uint32_t capacity) override {
        rknn_sdk_version version{};const int result=query(rknn_context(context),RKNN_QUERY_SDK_VERSION,&version,sizeof(version));if(result)return result;
        const auto copy=[capacity](char* to,const char* from,size_t n){if(!capacity)return;const auto count=std::min(size_t(capacity-1),n);std::memcpy(to,from,count);to[count]=0;};
        copy(runtime,version.api_version,sizeof(version.api_version));copy(driver,version.drv_version,sizeof(version.drv_version));return 0;
    }
    int SetCoreMask(Context context, uint32_t mask) override { return core(rknn_context(context),static_cast<rknn_core_mask>(mask)); }
    int IoCount(Context context, uint32_t& in, uint32_t& out) override { rknn_input_output_num io{};const int r=query(rknn_context(context),RKNN_QUERY_IN_OUT_NUM,&io,sizeof(io));in=io.n_input;out=io.n_output;return r; }
    int InputAttribute(Context context, Attribute& a) override { return QueryAttribute(context,a,RKNN_QUERY_INPUT_ATTR); }
    int OutputAttribute(Context context, Attribute& a) override { return QueryAttribute(context,a,RKNN_QUERY_OUTPUT_ATTR); }
    int PrepareOutputs(const OutputBuffer* buffers, uint32_t count) override {
        expected_outputs.assign(buffers,buffers+count);native_outputs.resize(count);return 0;
    }
    int InputsSet(Context context, const void* rgb, uint32_t bytes) override {
        rknn_input input{};input.index=0;input.buf=const_cast<void*>(rgb);input.size=bytes;
        input.type=RKNN_TENSOR_UINT8;input.fmt=RKNN_TENSOR_NHWC;input.pass_through=0;
        return inputs(rknn_context(context),1,&input);
    }
    int Run(Context context) override { return run(rknn_context(context),nullptr); }
    int OutputsGet(Context context, OutputBuffer* out, uint32_t count) override {
        for(uint32_t i=0;i<count;++i) {
            auto& n=native_outputs[i];n={};n.index=expected_outputs[i].index;n.want_float=1;n.is_prealloc=1;
            n.buf=expected_outputs[i].data;n.size=expected_outputs[i].bytes;
        }
        const int result=get(rknn_context(context),count,native_outputs.data(),nullptr);
        if(!result)for(uint32_t i=0;i<count;++i)out[i]={native_outputs[i].index,static_cast<float*>(native_outputs[i].buf),native_outputs[i].size};
        return result;
    }
    int OutputsRelease(Context context, uint32_t count) noexcept override { return release(rknn_context(context),count,native_outputs.data()); }
    void Destroy(Context context) noexcept override { destroy(rknn_context(context)); }
};
class AndroidLoader final : public Loader {
public:
    std::unique_ptr<Vendor> Load(const char* path, std::string& error) override {
        auto library=std::make_unique<AndroidLibrary>();Symbols symbols;
        if(!ResolveSymbols(*library,path,symbols,error))return {};
        return std::make_unique<AndroidVendor>(std::move(library),symbols);
    }
};
}
std::unique_ptr<Loader> MakePlatformLoader() { return std::make_unique<AndroidLoader>(); }
}
#else
namespace humanvision::runtime::rknn {
std::unique_ptr<Loader> MakePlatformLoader() { return {}; }
}
#endif
