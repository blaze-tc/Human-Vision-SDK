#include "plugins/backend/rknn/rknn_vendor_boundary.h"
#include "plugins/backend/rknn/rknn_native_input.h"
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
    decltype(&rknn_create_mem) create_mem=nullptr;
    decltype(&rknn_destroy_mem) destroy_mem=nullptr;
    decltype(&rknn_set_io_mem) set_io_mem=nullptr;
    decltype(&rknn_mem_sync) mem_sync=nullptr;
    rknn_context candidate_context=0;
    rknn_tensor_mem* candidate_input=nullptr;
    uint32_t candidate_bytes=0;
    std::vector<rknn_output> candidate_outputs;
    bool candidate_borrowed=false;
    NativeInputTransform input_transform;
    NativeInputLayout input_layout;
    bool transform_ready=false;
    int candidate_step=-1;
    const void* candidate_model=nullptr;
    uint32_t candidate_model_bytes=0,candidate_mask=0,candidate_output_count=0;
    Attribute candidate_attribute;
    const Attribute* candidate_output_attributes=nullptr;
    std::vector<rknn_tensor_mem*> candidate_output_mems;
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
    ~AndroidVendor() override {DropInputCandidate();}
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
    bool PrepareInputCandidate(const void* model,uint32_t bytes,const Attribute& input_attr,const Attribute* outputs,
        const OutputBuffer*,uint32_t count,uint32_t mask) override {
        DropInputCandidate();
        candidate_model=model;candidate_model_bytes=bytes;candidate_attribute=input_attr;
        candidate_output_attributes=outputs;candidate_output_count=count;candidate_mask=mask;candidate_step=-1;
        create_mem=reinterpret_cast<decltype(create_mem)>(library->Symbol("rknn_create_mem"));
        destroy_mem=reinterpret_cast<decltype(destroy_mem)>(library->Symbol("rknn_destroy_mem"));
        set_io_mem=reinterpret_cast<decltype(set_io_mem)>(library->Symbol("rknn_set_io_mem"));
        mem_sync=reinterpret_cast<decltype(mem_sync)>(library->Symbol("rknn_mem_sync"));
        if(!create_mem||!destroy_mem||!set_io_mem||!mem_sync)return false;
        return NextInputCandidate();
    }
    void ConfigureInputTransform(const float* mean,const float* norm) override {
        transform_ready=input_transform.Configure(mean,norm);
    }
    const char* InputCandidateName() const override {
        return candidate_step==0?"native-fp16-io-qualified":candidate_step==1?"shared-io-qualified":"shared-input-qualified";
    }
    bool NextInputCandidate() override {
        DropInputCandidate();
        // Try the direct native input + shared outputs, then shared uint8 I/O,
        // then the earlier input-only candidate. Each needs a new numerical gate.
        while(++candidate_step<3) {
            if(candidate_step==0&&!transform_ready)continue;
            if(PrepareCandidateStep())return true;
        }
        return false;
    }
    bool PrepareCandidateStep() {
        const auto* model=candidate_model;const auto bytes=candidate_model_bytes;
        const auto& input_attr=candidate_attribute;const auto* outputs=candidate_output_attributes;
        const auto count=candidate_output_count,mask=candidate_mask;
        const auto reject=[this](){DropInputCandidate();return false;};
        // This context is independent: failure cannot poison the baseline input binding.
        if(init(&candidate_context,const_cast<void*>(model),bytes,0,nullptr)||!candidate_context)return reject();
        if(core(candidate_context,static_cast<rknn_core_mask>(mask)))return reject();
        uint32_t inputs=0,output_count=0;
        if(IoCount(Context(candidate_context),inputs,output_count)||inputs!=1||output_count!=count)return reject();
        for(uint32_t i=0;i<count;++i) {
            Attribute a{};a.index=i;
            if(OutputAttribute(Context(candidate_context),a)||a.index!=outputs[i].index||a.rank!=outputs[i].rank||
                a.elements!=outputs[i].elements||a.type!=outputs[i].type||a.layout!=outputs[i].layout||
                std::strcmp(a.name,outputs[i].name))return reject();
            for(uint32_t d=0;d<a.rank;++d)if(a.dimensions[d]!=outputs[i].dimensions[d])return reject();
        }
        rknn_tensor_attr attr{};
        if(query(candidate_context,candidate_step==0?RKNN_QUERY_NATIVE_NHWC_INPUT_ATTR:RKNN_QUERY_INPUT_ATTR,&attr,sizeof(attr)))return reject();
        const uint32_t height=uint32_t(input_attr.dimensions[input_attr.layout==Layout::Nchw?2:1]);
        const uint32_t width=uint32_t(input_attr.dimensions[input_attr.layout==Layout::Nchw?3:2]);
        const uint64_t rgb_bytes=uint64_t(width)*height*3;
        if(attr.n_dims!=4||std::strcmp(attr.name,input_attr.name))return reject();
        uint64_t allocation=std::max(rgb_bytes,uint64_t(attr.size_with_stride));
        if(candidate_step==0) {
            allocation=attr.size_with_stride?attr.size_with_stride:attr.size;
            if(attr.type!=RKNN_TENSOR_FLOAT16||attr.fmt!=RKNN_TENSOR_NHWC||attr.qnt_type!=RKNN_TENSOR_QNT_NONE||
                attr.dims[0]!=1||attr.dims[1]!=height||attr.dims[2]!=width||
                uint64_t(attr.n_elems)!=uint64_t(height)*width*attr.dims[3]||
                !input_layout.Configure(width,height,attr.w_stride?attr.w_stride:width,attr.dims[3],uint32_t(allocation)))return reject();
        } else if(attr.n_elems!=input_attr.elements||(attr.w_stride&&attr.w_stride!=width))return reject();
        if(!rgb_bytes||allocation>4ull*1024*1024)return reject();
        candidate_bytes=uint32_t(rgb_bytes);
        candidate_input=create_mem(candidate_context,uint32_t(allocation));
        if(!candidate_input||!candidate_input->virt_addr||candidate_input->size<allocation)return reject();
        if(candidate_step==0) {
            // Exact queried native layout plus explicit model transform. Runtime
            // must not normalize/transpose a second time; equality is checked later.
            attr.pass_through=1;
        } else {
            attr.type=RKNN_TENSOR_UINT8;attr.fmt=RKNN_TENSOR_NHWC;attr.pass_through=0;
            attr.dims[0]=1;attr.dims[1]=height;attr.dims[2]=width;attr.dims[3]=3;
            attr.size=candidate_bytes;attr.size_with_stride=candidate_bytes;attr.h_stride=0;
        }
        if(set_io_mem(candidate_context,candidate_input,&attr))return reject();
        candidate_outputs.resize(count);
        if(candidate_step<2) {
            candidate_output_mems.assign(count,nullptr);
            uint64_t total=0;
            for(uint32_t i=0;i<count;++i) {
                rknn_tensor_attr output{};output.index=i;
                if(query(candidate_context,RKNN_QUERY_OUTPUT_ATTR,&output,sizeof(output))||
                    output.n_elems!=outputs[i].elements||output.fmt==RKNN_TENSOR_NC1HWC2)return reject();
                const uint64_t bytes=uint64_t(output.n_elems)*sizeof(float);total+=bytes;
                if(!bytes||total>4ull*1024*1024)return reject();
                // Official RKNN memory API supports requested dense float outputs.
                // Keep queried dimensions/order; unsupported drivers fail binding
                // or the subsequent full output comparison, never get guessed padding.
                output.type=RKNN_TENSOR_FLOAT32;
                auto*& memory=candidate_output_mems[i];memory=create_mem(candidate_context,uint32_t(bytes));
                if(!memory||!memory->virt_addr||memory->size<bytes||set_io_mem(candidate_context,memory,&output))return reject();
            }
        }
        return true;
    }
    int CandidateInputsSet(const void* rgb,uint32_t bytes) override {
        if(!candidate_context||!candidate_input||bytes!=candidate_bytes||!rgb)return -1;
        if(candidate_step==0) {
            if(!input_transform.Pack(rgb,bytes,input_layout,candidate_input->virt_addr,candidate_input->size))return -1;
        } else std::memcpy(candidate_input->virt_addr,rgb,bytes);
        // Explicit CPU-to-device cache synchronization; timing includes this copy/sync.
        return mem_sync(candidate_context,candidate_input,RKNN_MEMORY_SYNC_TO_DEVICE);
    }
    int CandidateRun() override {return run(candidate_context,nullptr);}
    int CandidateOutputsGet(OutputBuffer* out,uint32_t count) override {
        if(count!=candidate_outputs.size())return -1;
        if(candidate_step<2) {
            for(uint32_t i=0;i<count;++i) {
                auto* memory=candidate_output_mems[i];
                if(!out[i].data||out[i].index!=i||out[i].bytes!=candidate_output_attributes[i].elements*sizeof(float)||
                    memory->size<out[i].bytes||mem_sync(candidate_context,memory,RKNN_MEMORY_SYNC_FROM_DEVICE))return -1;
                std::memcpy(out[i].data,memory->virt_addr,out[i].bytes);
            }
            return 0;
        }
        for(uint32_t i=0;i<count;++i) {
            auto& n=candidate_outputs[i];n={};n.index=out[i].index;n.want_float=1;n.is_prealloc=1;
            n.buf=out[i].data;n.size=out[i].bytes;
        }
        int status=get(candidate_context,count,candidate_outputs.data(),nullptr);
        if(!status) {
            candidate_borrowed=true;
            for(uint32_t i=0;i<count;++i)out[i]={candidate_outputs[i].index,
                static_cast<float*>(candidate_outputs[i].buf),candidate_outputs[i].size};
        }
        return status;
    }
    int CandidateOutputsRelease(uint32_t count) noexcept override {
        if(!candidate_borrowed)return 0;
        candidate_borrowed=false;
        return release(candidate_context,count,candidate_outputs.data());
    }
    void DropInputCandidate() noexcept override {
        if(candidate_context) {
            CandidateOutputsRelease(uint32_t(candidate_outputs.size()));
            for(auto* memory:candidate_output_mems)if(memory&&destroy_mem)destroy_mem(candidate_context,memory);
            candidate_output_mems.clear();
            if(candidate_input&&destroy_mem)destroy_mem(candidate_context,candidate_input);
            candidate_input=nullptr;
            destroy(candidate_context);candidate_context=0;
        }
        candidate_bytes=0;
    }
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
