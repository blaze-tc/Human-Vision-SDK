#include "gpu/android/gpu_parity_ncnn.h"
#if defined(__ANDROID__) && defined(HV_ANDROID_R4_PARITY)
#include "gpu/android/gpu_parity_shader.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace humanvision::gpu {
bool CompileParityShaderSource(const char* source,std::vector<uint32_t>& spirv) {
    ncnn::Option option;option.use_fp16_packed=option.use_fp16_storage=option.use_fp16_arithmetic=false;
    return ncnn::compile_spirv_module(source,int(std::strlen(source)),option,spirv)==0;
}
bool CompileParityImageShader(std::vector<uint32_t>& spirv) {
    const std::string shader=std::string("#version 450\n#define PARITY_IMAGE\n")+kParityShader;
    ncnn::Option option; option.use_fp16_packed=option.use_fp16_storage=option.use_fp16_arithmetic=false;
    return ncnn::compile_spirv_module(shader.c_str(),int(shader.size()),option,spirv)==0;
}
bool NcnnParityReduction::Initialize(const ncnn::VulkanDevice* device,
    ncnn::VkAllocator* allocator, ncnn::VkAllocator* staging, std::string& error) {
    if (!device || !allocator || pipeline_) { error="invalid parity initialization"; return false; }
    const std::string shader=std::string("#version 450\n")+kParityShader;
    ncnn::Option option;
    option.use_fp16_packed=false; option.use_fp16_storage=false;
    option.use_fp16_arithmetic=false; option.use_subgroup_ops=false;
    std::vector<uint32_t> spirv;
    if(ncnn::compile_spirv_module(shader.c_str(), int(shader.size()), option, spirv)!=0) {
        error="parity shader compilation failed"; return false;
    }
    pipeline_=std::make_unique<ncnn::Pipeline>(device);
    pipeline_->set_local_size_xyz(64,1,1);
    if(pipeline_->create(spirv.data(),spirv.size()*4,{})!=0 ||
        pipeline_->shader_info().binding_count!=3 || pipeline_->shader_info().push_constant_count!=12) {
        error="parity pipeline layout mismatch"; return false;
    }
    summary_.create(14, size_t(4), 1, allocator);
    cpu_summary_.create(14, size_t(4), 1);
    if(summary_.empty()||cpu_summary_.empty()) { error="parity summary allocation failed"; return false; }
    bindings_.resize(3); constants_.resize(12); download_option_=option;
    download_option_.blob_vkallocator=allocator; download_option_.staging_vkallocator=staging;
    error.clear(); return true;
}
bool NcnnParityReduction::Record(const ncnn::VkMat& actual,const ncnn::VkMat& golden,
    float tolerance,ncnn::VkCompute& compute,std::string& error,bool rgba_golden) {
    if(!pipeline_||actual.empty()||golden.empty()||actual.dims!=3||golden.dims!=3||
        actual.w!=golden.w||actual.h!=golden.h||
        (!rgba_golden && actual.c*actual.elempack!=golden.c*golden.elempack)||
        (rgba_golden && (actual.c!=3||actual.elempack!=1||golden.c!=1||golden.elemsize!=4||golden.elempack!=1))||
        actual.data==golden.data||!std::isfinite(tolerance)||tolerance<0||
        (actual.elembits()!=16&&actual.elembits()!=32)||(golden.elembits()!=16&&golden.elembits()!=32)||
        actual.cstep>uint32_t(INT32_MAX)/actual.elempack||golden.cstep>uint32_t(INT32_MAX)/golden.elempack) {
        error="parity tensor shape/dtype/identity mismatch"; return false;
    }
    constants_[0].i=actual.w; constants_[1].i=actual.h; constants_[2].i=actual.c*actual.elempack;
    constants_[3].i=actual.w*actual.elempack; constants_[4].i=int(actual.cstep)*actual.elempack;
    constants_[5].i=actual.elempack; constants_[6].i=actual.elembits()==16?1:2;
    constants_[7].i=golden.w*golden.elempack; constants_[8].i=int(golden.cstep)*golden.elempack;
    constants_[9].i=golden.elempack; constants_[10].i=golden.elembits()==16?1:2;
    if(rgba_golden) { constants_[7].i=golden.w*4; constants_[8].i=1; constants_[9].i=4; constants_[10].i=0; }
    constants_[11].f=tolerance;
    bindings_[0]=actual; bindings_[1]=golden; bindings_[2]=summary_;
    ncnn::VkMat dispatcher; dispatcher.w=64; dispatcher.h=dispatcher.d=dispatcher.c=1;
    compute.record_pipeline(pipeline_.get(),bindings_,constants_,dispatcher);
    compute.record_download(summary_,cpu_summary_,download_option_);
    error.clear(); return true;
}
bool NcnnParityReduction::CollectCompleted(ParityReduction& result) const noexcept {
    static_assert(sizeof(ParityReduction)==14*4,"shader readback layout mismatch");
    // ncnn Mat::total includes 16-byte cstep alignment: fourteen logical words
    // occupy sixteen allocated words. Validate logical shape, never padding.
    if(cpu_summary_.empty()||cpu_summary_.dims!=1||cpu_summary_.w!=14||
       cpu_summary_.elempack!=1||cpu_summary_.elembits()!=32||cpu_summary_.total()<14) return false;
    std::memcpy(&result,cpu_summary_.data,sizeof(result));
    return true;
}
}
#endif
