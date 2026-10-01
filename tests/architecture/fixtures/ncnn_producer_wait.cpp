// Test-only Android/ncnn boundary doubles. Production Run and role completion
// bodies are inserted below by test_ncnn_crop_submission.py.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
void check(bool value, const char* reason) { if (!value) { std::cerr << reason << '\n'; std::exit(1); } }
using HV_Result = int;
constexpr int HV_OK=0, HV_ERR_INTERNAL=-1, HV_ERR_INVALID_ARGUMENT=-2, HV_ERR_MODEL_LOAD=-3;
constexpr int HV_PLUGIN_API_V1=1, HV_GPU_TENSOR_FP16=1, HV_GPU_TENSOR_FP32=2;
constexpr int VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT=1, VK_QUEUE_FAMILY_EXTERNAL_KHR=2, VK_SUCCESS=0;
constexpr int kImportSemaphoreFdInfo=1,kTemporaryImport=1,kSyncFdHandleType=1;
struct ImportSemaphoreFdInfo { int type, semaphore=0, flags=0, handleType=0, fd=-1; };
std::string fault;
int imports=0, imported_fd=0;
int ImportFd(int, const ImportSemaphoreFdInfo* info) { ++imports; imported_fd=info->fd; return fault=="import"?-1:0; }
using ImportSemaphoreFd = int (*)(int,const ImportSemaphoreFdInfo*);
namespace ncnn {
struct Option {
    bool use_packing_layout=false, use_fp16_storage=false, use_fp16_packed=false;
};
}
struct Mat {
    std::shared_ptr<int> allocation=std::make_shared<int>(1);
    int dims=1,w=1,h=1,d=1,c=3,elempack=1,bits=16; size_t cstep=1,elemsize=4;
    float data[3]{1,2,3};
    bool empty() const { return !allocation; }
    int elembits() const { return bits; }
    size_t total() const { return 3; }
    void release() { allocation.reset(); }
};
struct Command {
    int waits=0, resets=0, semaphore_waits=0, releases=0, pending=0;
    bool completed=true, producer_ready=false, broken_reset=false;
    std::vector<std::weak_ptr<int>> references;
    void record(Mat& mat) { references.push_back(mat.allocation); ++pending; completed=false; }
    void record_release_android_hardware_buffer(Mat& image,int,int) { ++releases; record(image); }
    void record_download(Mat& gpu, Mat& cpu,const ncnn::Option&) { record(gpu); record(cpu); }
    int submit_and_wait() {
        ++waits;
        if(fault=="internal"||fault=="internal_reset")check(producer_ready,"extract internally submitted AHB commands before producer semaphore wait");
        if(broken_reset)return -1;
        for (const auto& ref:references) check(!ref.expired(), "buffer freed while commands still reference it");
        if (fault=="submit" || fault=="cleanup" || fault=="release") return -1;
        completed=true; return 0;
    }
    int submit_and_wait(int,int) { producer_ready=true; ++semaphore_waits; return submit_and_wait(); }
    int reset() { check(completed, "reset before proven GPU completion"); ++resets;
        if(fault=="reset" || (fault=="internal_reset"&&resets==2)) {broken_reset=true;return -1;}
        references.clear(); pending=0; return 0;
    }
};
namespace ncnn { void* vkGetDeviceProcAddr(int,const char*) { return reinterpret_cast<void*>(&ImportFd); } }
namespace gpu {
struct Token { uint32_t index=0; uint64_t generation=1,frame_id=1; };
struct SyncFd { int value=4; bool payload=true; bool HasPayload() const{return payload;} int Get()const{return value;}
    void Release(){payload=false;} };
struct ConsumerFrame { bool claimed=true,ncnn_role_complete=false; Token token;
    struct {uint64_t generation=1;} metadata; uintptr_t ahb_buffer=1; SyncFd producer_fd;
    void* role_owner=nullptr; bool (*complete_role)(void*,ConsumerFrame&,bool,std::string&) noexcept=nullptr; };
enum class CompletionProof {GpuQuiescent}; enum class SlotResult {Ok,Invalid};
enum class NcnnRoleStart {Invalid,WaitForProducer,Acquired};
NcnnRoleStart NextNcnnRole(ConsumerFrame& f) {return f.producer_fd.HasPayload()?NcnnRoleStart::WaitForProducer:(f.ncnn_role_complete?NcnnRoleStart::Acquired:NcnnRoleStart::Invalid);}
}
bool WaitProducerFd(gpu::SyncFd& fd) {if(fault=="producer_wait")return false;fd.Release();return true;}
struct Bridge {int retired=0,quarantined=0; Command* command=nullptr;
    void QuarantineConsumer(gpu::ConsumerFrame&){++quarantined;}
    gpu::SlotResult RetireConsumer(gpu::ConsumerFrame& f,gpu::CompletionProof){
        check(command->completed && command->pending==0,"retirement without completed reset");
        ++retired; f.claimed=false;return gpu::SlotResult::Ok;}
};
struct HV_GpuFrameRefV1 {void* opaque_slot=nullptr;}; struct HV_GpuImageTransformV1 {};
struct HV_TensorViewV1 {uint32_t struct_size=0,api_version=0;const char*name=nullptr;int element_type=0,rank=0;
    int64_t dimensions[4]{};const void*data=nullptr;size_t byte_count=0;};
struct DenseOutputLayout {size_t logical_bytes=12;int rank=1;std::array<int64_t,4>dimensions{3,0,0,0};};
bool DescribeDenseOutput(int,int,int,int,int,size_t,size_t,DenseOutputLayout&){return true;}
bool ValidateDenseDownload(const DenseOutputLayout&,size_t){return true;}
bool CompactDenseFp32(const float*src,const DenseOutputLayout&,float*dst,size_t){std::copy(src,src+3,dst);return true;}
struct Extractor {Mat input;std::vector<Mat> held;
    int extract(const char*,Mat& output,Command& cmd){
        if((fault=="internal"||fault=="internal_reset")&&held.empty()) {
            const int ret=cmd.submit_and_wait(); cmd.reset(); // Pinned net.cpp ignores reset result.
            if(ret!=0)return ret;
        }
        output=Mat{};output.bits=32;held.push_back(output);
        cmd.record(input);cmd.record(output);return fault=="extract"?-1:0;}
};
enum class InputDeliveryResult {Ok,InvalidTensor,Rejected};
InputDeliveryResult DeliverInput(Extractor& ex,const char*,Mat& input,bool){ex.input=input;
    return fault=="input"?InputDeliveryResult::Rejected:InputDeliveryResult::Ok;}
struct Device {struct {int compute_queue_family_index(){return 0;}}info;int vkdevice(){return 0;}
    void convert_packing(Mat& src,Mat& dst,int,int cast,Command& cmd,const ncnn::Option&){
        dst=Mat{};dst.bits=cast==1?32:16;cmd.record(src);cmd.record(dst);}
};
struct Preprocess {bool Record(Mat& src,const HV_GpuImageTransformV1&,Mat& dst,Command& cmd,const float*,std::string&,bool){
    cmd.record(src);cmd.record(dst);return fault!="preprocess"&&fault!="cleanup";}
};
struct InputContract {enum class CropMode {Letterbox}; CropMode crop_mode=CropMode::Letterbox;
    int width=1,height=1,output_elempack=1,cast_type_to=2,output_type=HV_GPU_TENSOR_FP16;
    std::array<float,3>pad_rgb{};std::string input_blob="in";std::vector<std::string>output_blobs{"x","y"};};
struct Slot {std::unique_ptr<Command>compute=std::make_unique<Command>();int import_semaphore=1;
    Mat image,imported_rgb,normalized,prepared_input;
    std::unique_ptr<Extractor>extractor=std::make_unique<Extractor>(),pristine_extractor=std::make_unique<Extractor>();
    std::vector<Mat>gpu_outputs{2},fp32_outputs{2},cpu_outputs{2}; std::vector<std::vector<float>>dense_outputs{2};};
struct AndroidSession {
    std::mutex run_mutex_;bool terminal_gpu_fault_=false,gate_mode_=false,detector_role_=false;gpu::ConsumerFrame*active_consumer_=nullptr;
    gpu::Token active_token_;std::array<std::unique_ptr<Slot>,1>slots_;
    struct {uint64_t generation=1;std::array<uintptr_t,1>retained_ahb{1};}generation_;
    struct { bool raw_tensor=true; } backend_options_;
    Bridge*bridge_;Device*device_;Preprocess*preprocess_;InputContract contract_;ncnn::Option option_;
    std::vector<size_t>output_byte_limits_{12,12};
    bool ValidateTransform(const HV_GpuFrameRefV1&,const HV_GpuImageTransformV1&,gpu::ConsumerFrame&,std::string&){return true;}
    bool DrainDropped(std::string&){return true;}
    bool RecordRgbImport(Slot& s,Command& cmd,std::string&){cmd.record(s.image);cmd.record(s.imported_rgb);return fault!="rgb";}
    void RetireUnsubmitted(gpu::ConsumerFrame*){check(false,"unexpected unsubmitted path");}
    HV_Result Run(const HV_GpuFrameRefV1&,const HV_GpuImageTransformV1&,HV_TensorViewV1*,uint32_t,uint32_t&,std::string&);
    static bool CompleteRoleCallback(void*,gpu::ConsumerFrame&,bool,std::string&)noexcept;
    bool ReleaseActiveRole(gpu::ConsumerFrame&,std::string&)noexcept;
    bool YieldObservation(gpu::ConsumerFrame&,std::string&)noexcept;
    bool FinishObservation(gpu::ConsumerFrame&,std::string&)noexcept;
};
// PRODUCTION_BODIES

int main(int argc,char**argv) {
    check(argc==2,"case required"); const std::string mode=argv[1];
    Bridge bridge; Device device; Preprocess preprocess; AndroidSession session;
    session.bridge_=&bridge;session.device_=&device;session.preprocess_=&preprocess;
    session.contract_.output_type=HV_GPU_TENSOR_FP32;session.contract_.cast_type_to=1;
    session.slots_[0]=std::make_unique<Slot>(); auto&cmd=*session.slots_[0]->compute;bridge.command=&cmd;
    gpu::ConsumerFrame consumer; HV_GpuFrameRefV1 frame{&consumer}; HV_GpuImageTransformV1 transform;
    HV_TensorViewV1 outputs[2];uint32_t count=0;std::string error;
    if(mode=="internal"||mode=="internal_reset")fault=mode;
    if(mode=="legacy") session.backend_options_.raw_tensor=false;
    if(mode=="fp16") {session.contract_.output_type=HV_GPU_TENSOR_FP16;session.contract_.cast_type_to=2;}
    if(mode=="signed") consumer.producer_fd.value=-1;
    if(mode=="handoff"||mode=="handoff_preprocess") {consumer.producer_fd.Release();consumer.ncnn_role_complete=true;}
    const bool fails=mode=="preprocess"||mode=="input"||mode=="extract"||mode=="submit"||mode=="reset"||mode=="cleanup"||mode=="import"||mode=="rgb"||mode=="handoff_preprocess";
    if(fails) fault=mode=="handoff_preprocess"?"preprocess":mode;
    const auto result=session.Run(frame,transform,outputs,2,count,error);
    if(mode=="internal_reset") {
        check(result!=HV_OK&&count==0,"ignored internal reset failure accepted output");
        check(bridge.quarantined==1&&bridge.retired==0&&session.terminal_gpu_fault_,"internal reset fault must quarantine without retirement");
        check(cmd.resets==2,"faulted internal reset must not be retried by session cleanup");
        return 0;
    }
    if(fails) {
        check(result!=HV_OK&&count==0,"first-run fault accepted output");
        if(mode=="submit"||mode=="reset"||mode=="cleanup") {
            check(bridge.quarantined==1&&bridge.retired==0&&session.terminal_gpu_fault_,"unproven completion must quarantine");
            check(cmd.resets==(mode=="reset"?1:0),"submit failure must not reset commands");
        } else if(mode=="import") {
            check(cmd.waits==0&&cmd.releases==0&&bridge.retired==1,"failed import must retain and CPU-wait producer fd before retirement");
            check(!consumer.producer_fd.HasPayload(),"failed import producer fd was not drained");
        } else {
            check(cmd.waits==((mode=="input"||mode=="extract")?2:1)&&cmd.semaphore_waits==(mode=="handoff_preprocess"?0:1)&&cmd.releases==1&&bridge.retired==1,"partial first recording must drain producer and release together");
        }
        return 0;
    }
    check(result==HV_OK&&count==2,"first run failed");
    const int first_waits=mode=="internal"?3:2;
    check(cmd.waits==first_waits,"first preprocessing must complete before inference extraction");
    check(cmd.semaphore_waits==(mode=="handoff"?0:1)&&imports==(mode=="handoff"?0:1),"producer semaphore must be consumed exactly once including signed -1");
    if(mode=="signed")check(imported_fd==-1,"signed -1 producer payload altered");
    check(session.active_consumer_==&consumer&&consumer.role_owner==&session,"active role must be published only after completion");
    check(cmd.releases==0&&bridge.retired==0,"role released before completion callback");
    for(int i=1;i<3;++i)check(session.Run(frame,transform,outputs,2,count,error)==HV_OK,"repeated raw run failed");
    check(cmd.semaphore_waits==(mode=="handoff"?0:1),"repeated run duplicated producer wait");
    const int before_release_waits=cmd.waits;
    if(mode=="release") fault=mode;
    if(mode=="release") {
        check(!session.FinishObservation(consumer,error),"release failure accepted");
        check(bridge.quarantined==1&&bridge.retired==0,"release failure must quarantine");
    } else {
        check(session.FinishObservation(consumer,error),"final role failed");
        check(cmd.waits==before_release_waits+1&&cmd.releases==1&&bridge.retired==1,"final ownership release and retirement missing");
    }
    return 0;
}
