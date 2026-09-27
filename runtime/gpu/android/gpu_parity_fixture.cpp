#include "gpu/android/gpu_parity_fixture.h"
#if defined(__ANDROID__) && defined(HV_ANDROID_R4_PARITY)
#include "common/config_io.h"
#include "picosha2/picosha2.h"
#include <android/log.h>
#include <fstream>
#include <mutex>
#include <cstring>
#include <cmath>
#include <atomic>

namespace humanvision::gpu {
namespace {
std::mutex fixture_mutex;
std::shared_ptr<const ParityFixture> current;
std::atomic<int> requested_copy_path{0};
std::vector<unsigned char> Read(const std::filesystem::path& path,size_t expected) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in||size_t(in.tellg())!=expected) throw std::runtime_error("R4 fixture byte length mismatch");
    std::vector<unsigned char> bytes(expected); in.seekg(0);
    if(!in.read(reinterpret_cast<char*>(bytes.data()),expected)) throw std::runtime_error("R4 fixture read failed");
    return bytes;
}
}
std::shared_ptr<const ParityFixture> CurrentParityFixture() {
    std::lock_guard<std::mutex> lock(fixture_mutex); return current;
}
int RequestedParityCopyPath() { return requested_copy_path.load(); }
std::array<uint8_t,32> ParityHashBytes(const std::string& hash) {
    std::array<uint8_t,32> result{};
    if(hash.size()!=64) return result;
    for(int i=0;i<64;i++) {
        const char c=hash[i];int value=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;
        if(value<0) return {};
        result[i/2]|=uint8_t(value<<((1-i%2)*4));
    }
    return result;
}
extern "C" __attribute__((visibility("default"))) int HV_R4ConfigureRun(int copy_path) {
    if(copy_path<0||copy_path>2) return 0;
    requested_copy_path.store(copy_path); return 1;
}
static int ConfigureFixture(const char* root,const char* hash,int case_index) {
    try {
        if(!root||!hash||std::strlen(hash)!=64) return 0;
        const auto base=std::filesystem::u8path(root);
        const auto file=base/"manifest.json";
        auto manifest_bytes=Read(file,std::filesystem::file_size(file));
        if(picosha2::hash256_hex_string(manifest_bytes)!=hash) return 0;
        auto manifest=nlohmann::json::parse(manifest_bytes);
        auto fixture=std::make_shared<ParityFixture>(); fixture->manifest_hash=hash; fixture->root=root;
        auto artifact_base=base;
        if(manifest.contains("cases")) {
            if(case_index<0||size_t(case_index)>=manifest.at("cases").size()) return 0;
            manifest=manifest.at("cases").at(case_index).get<nlohmann::json>();
            artifact_base=runtime::ConfinedPath(base,std::filesystem::u8path(manifest.at("folder").get<std::string>()));
            fixture->case_index=case_index;
        } else if(case_index!=-1) return 0;
        fixture->width=manifest.at("width"); fixture->height=manifest.at("height");
        fixture->expected_people=manifest.contains("annotations")?uint32_t(manifest.at("annotations").size()):0;
        if(!fixture->width||!fixture->height||fixture->width>8192||fixture->height>8192) return 0;
        const char* names[]={"rgba","tensor_fp32","tensor_fp16_rtz"};
        const size_t sizes[]={size_t(fixture->width)*fixture->height*4,320*320*3*4,320*320*3*2};
        for(int i=0;i<3;i++) {
            auto artifact=manifest.at("artifacts").at(names[i]);
            const auto path=runtime::ConfinedPath(artifact_base,std::filesystem::u8path(artifact.at("file").get<std::string>()));
            fixture->bytes[i]=Read(path,sizes[i]); fixture->hashes[i]=artifact.at("sha256");
            if(picosha2::hash256_hex_string(fixture->bytes[i])!=fixture->hashes[i]) return 0;
        }
        std::lock_guard<std::mutex> lock(fixture_mutex); current=fixture;
        __android_log_print(ANDROID_LOG_INFO,"HV_R4_PARITY","fixture hash=%s width=%u height=%u",hash,fixture->width,fixture->height);
        return 1;
    } catch(const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR,"HV_R4_PARITY","fixture_error=%s",e.what()); return 0;
    }
}
extern "C" __attribute__((visibility("default"))) int HV_R4ConfigureFixture(const char* root,const char* hash) {
    return ConfigureFixture(root,hash,-1);
}
extern "C" __attribute__((visibility("default"))) int HV_R4ConfigureFixtureCase(const char* root,const char* hash,int case_index) {
    return ConfigureFixture(root,hash,case_index);
}
bool NcnnParityStages::Initialize(const ncnn::VulkanDevice* device,ncnn::VkAllocator* allocator,ncnn::VkAllocator* staging,
    const std::array<uint8_t,16>& device_uuid,std::string& error) {
    fixture_=CurrentParityFixture(); if(!fixture_) { error="R4 fixture not configured"; return false; }
    import_control_epoch_=fixture_->import_controls.Begin();
    device_uuid_=device_uuid; device_handle_=reinterpret_cast<uintptr_t>(device->vkdevice());
    ncnn::Option option; option.use_fp16_packed=false; option.use_fp16_storage=false; option.use_fp16_arithmetic=false;
    option.blob_vkallocator=allocator; option.staging_vkallocator=staging;
    ncnn::VkCompute upload(device);
    for(int i=0;i<3;i++) {
        const int w=i?320:int(fixture_->width),h=i?320:int(fixture_->height),c=3;
        const size_t bytes=i==2?2:4;
        ncnn::Mat cpu(w,h,c,bytes,1);
        for(int ch=0;ch<c;ch++) {
            if(i==0) {
                auto* plane=static_cast<float*>(cpu.channel(ch).data);
                for(size_t pixel=0;pixel<size_t(w)*h;pixel++) plane[pixel]=fixture_->bytes[0][pixel*4+ch];
            } else std::memcpy(cpu.channel(ch).data,fixture_->bytes[i].data()+size_t(ch)*w*h*bytes,size_t(w)*h*bytes);
        }
        goldens_[i].create(w,h,c,bytes,1,allocator);
        upload.record_clone(cpu,goldens_[i],option);
        if(!reducers_[i].Initialize(device,allocator,staging,error)) return false;
        contexts_[i].owner=this; contexts_[i].index=i;
        probes_[i]=std::make_unique<GpuParityProbe>(ParityDispatch{&contexts_[i],DispatchRecord,DispatchCollect});
    }
    if(upload.submit_and_wait()!=0) { error="R4 golden upload failed"; return false; }
    // Initialization-only independent allocation and actual GPU mutation validate
    // the reduction, including a mismatch beyond the first subgroup of pixels.
    const char* mutation=R"glsl(#version 450
layout(local_size_x=1,local_size_y=1,local_size_z=1) in;
layout(binding=0) buffer Actual { uint a[]; };
layout(push_constant) uniform parameter { uint half_storage; } p;
void main() {
    if(p.half_storage==0u) a[32]=floatBitsToUint(uintBitsToFloat(a[32])+1.0);
    else { vec2 v=unpackHalf2x16(a[16]); v.x+=1.0; a[16]=packHalf2x16(v); }
}
)glsl";
    std::vector<uint32_t> spirv;
    if(ncnn::compile_spirv_module(mutation,int(std::strlen(mutation)),option,spirv)!=0) {
        error="R4 negative-control shader failed"; return false;
    }
    ncnn::Pipeline mutate(device); mutate.set_local_size_xyz(1,1,1);
    if(mutate.create(spirv.data(),spirv.size()*4,{})!=0) { error="R4 negative-control pipeline failed"; return false; }
    for(int i=1;i<3;i++) {
        ncnn::VkMat actual; ncnn::VkCompute control(device);
        control.record_clone(goldens_[i],actual,option);
        if(!reducers_[i].Record(actual,goldens_[i],0.02f,control,error)||control.submit_and_wait()!=0) return false;
        ParityReduction clean{};
        if(!reducers_[i].CollectCompleted(clean)||clean.mismatch_count||clean.max_error!=0||clean.error_sum!=0) {
            error="R4 independent clean GPU control failed"; return false;
        }
        control.reset();
        std::vector<ncnn::vk_constant_type> constants(1); constants[0].i=i==2;
        ncnn::VkMat dispatcher; dispatcher.w=dispatcher.h=dispatcher.d=dispatcher.c=1;
        control.record_pipeline(&mutate,{actual},constants,dispatcher);
        if(!reducers_[i].Record(actual,goldens_[i],0.02f,control,error)||control.submit_and_wait()!=0) return false;
        ParityReduction bad{};
        const bool pass=reducers_[i].CollectCompleted(bad)&&bad.element_count==320*320*3&&
            bad.mismatch_count==1&&bad.first_mismatch==32&&bad.max_error>0.99f&&bad.max_error<1.01f&&
            std::abs(bad.error_sum-bad.max_error)<0.0001f;
        __android_log_print(ANDROID_LOG_INFO,"HV_R4_PARITY",
            "gpu_control dtype=%s pass=%d count=%u mismatches=%u first=%u max=%.9g sum=%.9g",
            i==1?"fp32":"fp16",pass,bad.element_count,bad.mismatch_count,bad.first_mismatch,bad.max_error,bad.error_sum);
        if(!pass) { error="R4 GPU single-element corruption control failed"; return false; }
    }
    // Independently rewrite a separate GPU allocation with each named fault.
    // Only the bounded reduction returns; the actual/golden tensors stay on GPU.
    const char* faults=R"glsl(#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
layout(binding=0) readonly buffer Golden { uint g[]; };
layout(binding=1) writeonly buffer Actual { uint a[]; };
layout(push_constant) uniform parameter { uint half_storage;uint mode; } p;
const uint width=320u,height=320u,plane=102400u,count=307200u;
float readValue(uint i) { return p.half_storage!=0u?unpackHalf2x16(g[i/2u])[i%2u]:uintBitsToFloat(g[i]); }
float corrupted(uint i) {
    uint c=i/plane,pixel=i%plane,y=pixel/width,x=pixel%width,j=i;
    if(p.mode==1u) j=(2u-c)*plane+pixel;
    if(p.mode==2u) j=c*plane+(height-1u-y)*width+x;
    if(p.mode==3u&&x>=width*3u/4u) return 0.0;
    if(p.mode==4u) j=(c*plane+y*(width-1u)+x)%count;
    if(p.mode==5u) j=(pixel*3u+c)%count;
    float value=readValue(j);
    if(p.mode==6u) value*=.5;
    if(p.mode==7u) value+=1.0;
    return value;
}
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(p.half_storage==0u) { if(i<count) a[i]=floatBitsToUint(corrupted(i)); }
    else if(i<count/2u) a[i]=packHalf2x16(vec2(corrupted(i*2u),corrupted(i*2u+1u)));
}
)glsl";
    std::vector<uint32_t> fault_spirv;
    if(ncnn::compile_spirv_module(faults,int(std::strlen(faults)),option,fault_spirv)!=0) {
        error="R4 corruption matrix shader failed";return false;
    }
    ncnn::Pipeline fault_pipeline(device);fault_pipeline.set_local_size_xyz(64,1,1);
    if(fault_pipeline.create(fault_spirv.data(),fault_spirv.size()*4,{})!=0) {error="R4 corruption matrix pipeline failed";return false;}
    const char* names[]={"clean","channel_swap","vertical_flip","truncated_copy","wrong_stride","wrong_packing","wrong_scale","old_slot_content"};
    for(int dtype=1;dtype<3;dtype++) {
        ncnn::VkMat actual;actual.create(320,320,3,dtype==1?size_t(4):size_t(2),1,allocator);
        std::vector<ncnn::VkMat> bindings{goldens_[dtype],actual};
        std::vector<ncnn::vk_constant_type> push(2);push[0].i=dtype==2;
        ncnn::VkMat dispatcher;dispatcher.w=dtype==1?307200:153600;dispatcher.h=dispatcher.d=dispatcher.c=1;
        for(int mode=0;mode<8;mode++) {
            ncnn::VkCompute command(device);push[1].i=mode;
            command.record_pipeline(&fault_pipeline,bindings,push,dispatcher);
            if(!reducers_[dtype].Record(actual,goldens_[dtype],.02f,command,error)||command.submit_and_wait()!=0) return false;
            ParityReduction r{};if(!reducers_[dtype].CollectCompleted(r)) {error="R4 corruption summary invalid";return false;}
            const bool pass=r.element_count==307200 && (mode==0?
                (r.mismatch_count==0&&r.max_error==0&&r.error_sum==0):
                (r.mismatch_count>0&&r.first_mismatch<r.element_count&&r.max_error>.02f));
            __android_log_print(ANDROID_LOG_INFO,"HV_R4_PARITY",
                "gpu_fault_control name=%s dtype=%s pass=%d count=%u mismatches=%u first=%u max=%.9g mean=%.9g",
                names[mode],dtype==1?"fp32":"fp16",pass,r.element_count,r.mismatch_count,r.first_mismatch,r.max_error,double(r.error_sum)/r.element_count);
            if(!pass) {error="R4 GPU corruption matrix failed";return false;}
        }
    }
    const char* import_fault_shader=R"glsl(#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
layout(binding=0) readonly buffer Source { float source_data[]; };
layout(binding=1) writeonly buffer Target { float target_data[]; };
layout(push_constant) uniform parameter { uint width;uint height;uint source_cstep;uint target_cstep;uint mode; } p;
void main() {
    uint i=gl_GlobalInvocationID.x,plane=p.width*p.height;if(i>=plane*3u)return;
    uint c=i/plane,pixel=i%plane,x=pixel%p.width,y=pixel/p.width,j=pixel,k=c;
    if(p.mode==1u)k=2u-c;
    if(p.mode==2u)j=(p.height-1u-y)*p.width+x;
    if(p.mode==4u)j=(y*(p.width-1u)+x)%plane;
    if(p.mode==5u){uint wrong=pixel*3u+c;j=wrong%plane;k=wrong/plane;}
    float value=source_data[k*p.source_cstep+j];
    if(p.mode==3u&&x>=p.width*3u/4u)value=0.0;
    if(p.mode==6u)value*=.5;
    if(p.mode==7u)value=255.0-value;
    target_data[c*p.target_cstep+pixel]=value;
}
)glsl";
    std::vector<uint32_t> import_spirv;
    if(ncnn::compile_spirv_module(import_fault_shader,int(std::strlen(import_fault_shader)),option,import_spirv)!=0){error="R4 import fault shader failed";return false;}
    import_fault_pipeline_=std::make_unique<ncnn::Pipeline>(device);import_fault_pipeline_->set_local_size_xyz(64,1,1);
    if(import_fault_pipeline_->create(import_spirv.data(),import_spirv.size()*4,{})!=0){error="R4 import fault pipeline failed";return false;}
    import_fault_scratch_.create(fixture_->width,fixture_->height,3,size_t(4),1,allocator);
    if(import_fault_scratch_.empty()){error="R4 import fault scratch allocation failed";return false;}
    for(auto& reduction:import_fault_reducers_)if(!reduction.Initialize(device,allocator,staging,error))return false;
    import_fault_bindings_.resize(2);import_fault_constants_.resize(5);
    error.clear(); return true;
}
void ReportParitySummary(const char* stage,const ParitySummary& r,const ParityFixture& fixture,
                         uint32_t slot,int golden_index,uintptr_t device) {
    char uuid[33]{}; const char* digits="0123456789abcdef";
    for(int i=0;i<16;i++) { uuid[i*2]=digits[r.device_uuid[i]>>4]; uuid[i*2+1]=digits[r.device_uuid[i]&15]; }
    __android_log_print(ANDROID_LOG_INFO,"HV_R4_PARITY",
        "{\"stage\":\"%s\",\"generation\":%llu,\"source_id\":%llu,\"slot\":%u,\"manifest_sha256\":\"%s\",\"golden_sha256\":\"%s\",\"device_uuid\":\"%s\",\"vkdevice\":%llu,\"width\":%u,\"height\":%u,\"channels\":%u,\"row_stride\":%u,\"channel_stride\":%u,\"dtype\":%u,\"elempack\":%u,\"passed\":%s,\"elements\":%u,\"mismatches\":%u,\"first\":%u,\"first_xyz\":[%u,%u,%u],\"max\":%.9g,\"mean\":%.9g,\"samples\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]}",
        stage,(unsigned long long)r.generation,(unsigned long long)r.source_id,slot,fixture.manifest_hash.c_str(),fixture.hashes[golden_index].c_str(),uuid,
        (unsigned long long)device,r.width,r.height,r.channels,r.row_stride_elements,r.channel_stride_elements,
        unsigned(r.dtype),unsigned(r.elempack),r.passed?"true":"false",r.element_count,r.mismatch_count,r.first_mismatch,
        r.first_x,r.first_y,r.first_channel,r.max_error,r.mean_error,
        r.samples[0],r.samples[1],r.samples[2],r.samples[3],r.samples[4],r.samples[5],r.samples[6],r.samples[7],r.samples[8]);
}
bool NcnnParityStages::DispatchRecord(void* opaque,const ParityGpuView& actual,const ParityGpuView& golden,
    const ParityContract& contract,uint32_t,ParityReduction&) noexcept {
    auto& c=*static_cast<DispatchContext*>(opaque);
    if(!c.actual||!c.command||!c.error || actual.resource!=reinterpret_cast<uintptr_t>(c.actual->buffer()) ||
       actual.byte_offset!=c.actual->buffer_offset() ||
       golden.resource!=reinterpret_cast<uintptr_t>(c.owner->goldens_[c.index].buffer()) ||
       golden.byte_offset!=c.owner->goldens_[c.index].buffer_offset()) return false;
    return c.owner->reducers_[c.index].Record(*c.actual,c.owner->goldens_[c.index],
        contract.element_tolerance,*c.command,*c.error);
}
bool NcnnParityStages::DispatchCollect(void* opaque,uint32_t,ParityReduction& reduction) noexcept {
    auto& c=*static_cast<DispatchContext*>(opaque);
    return c.ready&&c.owner->reducers_[c.index].CollectCompleted(reduction);
}
bool NcnnParityStages::Record(const ncnn::VkMat& imported,const ncnn::VkMat& normalized,
    const ncnn::VkMat& packed,ncnn::VkCompute& compute,uint64_t generation,uint64_t source_id,std::string& error) {
    if(completed_>=8) { error.clear(); return true; }
    if(pending_) {error="R4 GPU ticket still pending";return false;}
    const ncnn::VkMat* values[]={&imported,&normalized,&packed};
    for(int i=0;i<3;i++) {
        auto& context=contexts_[i]; context.actual=values[i]; context.command=&compute;context.error=&error;context.ready=false;
        const auto view=[&](const ncnn::VkMat& mat,bool golden) {
            ParityGpuView v{};v.resource=reinterpret_cast<uintptr_t>(mat.buffer());v.device_uuid=device_uuid_;
            v.byte_offset=mat.buffer_offset();v.byte_capacity=mat.buffer_capacity();
            v.generation=generation;v.source_id=source_id;v.width=mat.w;v.height=mat.h;v.channels=mat.c*mat.elempack;
            v.row_stride_elements=mat.w*mat.elempack;v.channel_stride_elements=uint32_t(mat.cstep)*mat.elempack;
            v.elempack=uint8_t(mat.elempack);v.dtype=mat.elembits()==16?ParityDtype::Fp16:ParityDtype::Fp32;
            v.identity=golden?ParityIdentity::IndependentGolden:ParityIdentity::Production;
            if(golden) v.content_sha256=ParityHashBytes(fixture_->hashes[i]);
            return v;
        };
        const auto a=view(*values[i],false),g=view(goldens_[i],true);
        ParityContract contract{};contract.golden_sha256=g.content_sha256;contract.generation=generation;contract.source_id=source_id;
        contract.width=g.width;contract.height=g.height;contract.channels=3;contract.dtype=g.dtype;contract.elempack=1;
        contract.element_tolerance=contract.max_error_limit=i==0?1.0f:.02f;contract.mean_error_limit=i==0?1.0f:.002f;
        if(completed_==0) {
            ParityTicket rejected{};auto bad=a;bad.source_id=source_id-1;
            bool pass=!probes_[i]->Record(static_cast<ParityStage>(i+2),bad,g,contract,rejected);
            bad=a;bad.generation=generation-1;
            pass=pass&&!probes_[i]->Record(static_cast<ParityStage>(i+2),bad,g,contract,rejected);
            bad=a;bad.row_stride_elements=a.width-1;
            pass=pass&&!probes_[i]->Record(static_cast<ParityStage>(i+2),bad,g,contract,rejected);
            bad=a;bad.elempack=2;
            pass=pass&&!probes_[i]->Record(static_cast<ParityStage>(i+2),bad,g,contract,rejected);
            __android_log_print(ANDROID_LOG_INFO,"HV_R4_PARITY",
                "gpu_resource_metadata_control stage=%d pass=%d stale_source=reject stale_generation=reject wrong_stride=reject wrong_packing=reject",i+2,pass);
            if(!pass) {error="R4 GPU resource metadata control failed";return false;}
        }
        if(!probes_[i]->Record(static_cast<ParityStage>(i+2),a,g,contract,tickets_[i])) { error="R4 GPU ticket record rejected";return false; }
        ParitySummary incomplete{};
        if(probes_[i]->TryCollect(tickets_[i],incomplete)) {error="R4 ticket collected before GPU completion proof";return false;}
    }
    if(completed_<8) {
        import_fault_bindings_[0]=imported;import_fault_bindings_[1]=import_fault_scratch_;
        import_fault_constants_[0].i=imported.w;import_fault_constants_[1].i=imported.h;
        import_fault_constants_[2].i=int(imported.cstep);import_fault_constants_[3].i=int(import_fault_scratch_.cstep);
        ncnn::VkMat dispatcher;dispatcher.w=imported.w*imported.h*3;dispatcher.h=dispatcher.d=dispatcher.c=1;
        const uint32_t mode=completed_;
            import_fault_constants_[4].i=mode;
            compute.record_pipeline(import_fault_pipeline_.get(),import_fault_bindings_,import_fault_constants_,dispatcher);
            if(!import_fault_reducers_[mode].Record(import_fault_scratch_,goldens_[0],1.f,compute,error))return false;
        import_faults_pending_=true;
    }
    pending_=true;return true;
}
void NcnnParityStages::ReportCompleted(uint64_t generation,uint64_t source_id,uint32_t slot) {
    if(!pending_) return;
    pending_=false;
    const char* stages[]={"imported_rgb","normalized","packed"};
    bool import_clean=false;
    for(int i=0;i<3;i++) {
        contexts_[i].ready=true;
        ParitySummary summary{};
        if(!probes_[i]->TryCollect(tickets_[i],summary)||summary.generation!=generation||summary.source_id!=source_id) {
            __android_log_print(ANDROID_LOG_ERROR,"HV_R4_PARITY","reduction_ticket_error stage=%s",stages[i]);continue;
        }
        ReportParitySummary(stages[i],summary,*fixture_,slot,i,device_handle_);
        if(i==0)import_clean=summary.passed;
        ParitySummary duplicate{};
        if(probes_[i]->TryCollect(tickets_[i],duplicate))
            __android_log_print(ANDROID_LOG_ERROR,"HV_R4_PARITY","reduction_ticket_error duplicate_collect stage=%s",stages[i]);
    }
    if(import_faults_pending_) {
        const char* names[]={"clean","channel_swap","vertical_flip","truncated_copy","wrong_stride","wrong_packing","wrong_scale","old_slot_content"};
        const uint32_t mode=completed_;
            ParityReduction r{};
            const bool pass=import_clean&&import_fault_reducers_[mode].CollectCompleted(r)&&r.element_count==fixture_->width*fixture_->height*3&&
                (mode==0?(r.mismatch_count==0&&r.max_error<=1.f):(r.mismatch_count>0&&r.first_mismatch<r.element_count&&r.max_error>1.f));
            __android_log_print(ANDROID_LOG_INFO,"HV_R4_PARITY",
                "gpu_boundary_control stage=imported_rgb name=%s pass=%d generation=%llu source_id=%llu slot=%u prior_clean=0 baseline_clean=%d count=%u mismatches=%u first=%u max=%.9g",
                names[mode],pass,(unsigned long long)generation,(unsigned long long)source_id,slot,import_clean,r.element_count,r.mismatch_count,r.first_mismatch,r.max_error);
        import_faults_pending_=false;
    }
    if(++completed_==8) {
        fixture_->import_controls.Complete(import_control_epoch_);
        __android_log_print(ANDROID_LOG_INFO,"HV_R4_PARITY",
            "immutable_fixture_phase stage=tensors mode=normal_inference_after_scheduled_controls");
    }
}
void NcnnParityStages::ReportDetectorOutput(uint64_t source_id,const char* name,const float* values,size_t count) {
    if(!fixture_||!source_id||!values||!name||
       (std::strcmp(name,"cls")!=0&&std::strcmp(name,"bbox")!=0)||count>8400) return;
    bool selected=false;
    for(auto& saved:detector_sources_) {
        if(saved==source_id) { selected=true; break; }
        if(saved==0) { saved=source_id; selected=true; break; }
    }
    if(!selected) return;
    // These are the ordinary compact detector outputs already downloaded by
    // inference, never source pixels or a preprocessed input tensor.
    const auto filename="detector-"+std::to_string(source_id)+"-"+name+".f32";
    std::ofstream out(std::filesystem::u8path(fixture_->root)/filename,std::ios::binary);
    out.write(reinterpret_cast<const char*>(values),count*sizeof(float));
    const auto* bytes=reinterpret_cast<const unsigned char*>(values);
    const auto hash=picosha2::hash256_hex_string(bytes,bytes+count*sizeof(float));
    __android_log_print(out?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,"HV_R4_PARITY",
        "detector_output source=%llu name=%s elements=%zu saved=%d file=%s sha256=%s",
        (unsigned long long)source_id,name,count,bool(out),filename.c_str(),hash.c_str());
}
}
#endif
