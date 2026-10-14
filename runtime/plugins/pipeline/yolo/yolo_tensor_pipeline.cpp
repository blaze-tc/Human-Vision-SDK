#include "plugins/pipeline/yolo/yolo_tensor_pipeline.h"
#include "plugins/pipeline/yolo/yolo_decoder.h"
#include "plugins/pipeline/yolo/yolo_rgb_preprocess.h"
#include "common/config_io.h"
#include "common/pipeline_diagnostics.h"
#include "picosha2/picosha2.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace {
using namespace humanvision::runtime;
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
constexpr int width=512,height=288,rows=3024;
constexpr char pack_id[]="yolov8n-pose-rectangle512x288-rknn-nonquantized-experimental";
constexpr char profile_id[]="android-rknn-npu-quality-low";
constexpr char contract_id[]="raw_tensor_rknn_nonquantized_v1";
constexpr char model_sha[]="1b3ba8dd5bb81e3d968cfeef019c42b1aa3bffca6f0ec1c9f89f257fe0c08030";
constexpr char onnx_sha[]="6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3";
constexpr char bank_sha[]="5ab8bb97034e957f3176162a63b471790b64514c7ba5a34d17622190167d4c34";
constexpr char conversion_sha[]="244a28f1bc9e0b1665fe27a3b35e774a732f9f62f752b5f6faed376e28bef4bb";
constexpr char simulator_sha[]="fe29f669bd5fafecba7a21797cad35f99b9f21b540c419a5f479d98cd03687f9";
constexpr char profile_sha[]="c7ef0f46de1b8e8175db3e5ae4fa7c89a93c07931f62849fc81da3f79d6628bd";

void Error(HV_ErrorBufferV1* out,const char* message) noexcept {
    if(!out||out->struct_size<sizeof(*out)||out->api_version!=HV_PLUGIN_API_V1||!out->data||!out->capacity)return;
    const auto count=std::min(std::strlen(message),size_t(out->capacity-1));
    std::memcpy(out->data,message,count);out->data[count]=0;
}
void Require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
float Milliseconds(Clock::time_point begin,Clock::time_point end) noexcept {
    return std::chrono::duration<float,std::milli>(end-begin).count();
}
std::filesystem::path Asset(const std::filesystem::path& root,const Json& path) {
    const auto name=path.get<std::string>();
    Require(name.find('\\')==std::string::npos&&name.find(':')==std::string::npos&&name.find('\0')==std::string::npos,
            "YOLO tensor assets require relative forward-slash paths");
    const auto resolved=ConfinedPath(root,std::filesystem::u8path(name));
    Require(std::filesystem::is_regular_file(resolved),"YOLO tensor asset is not a regular file");
    return resolved;
}
void CheckHash(const std::filesystem::path& path,const char* expected) {
    std::ifstream input(path,std::ios::binary);
    Require(bool(input),"Cannot open YOLO tensor asset");
    picosha2::hash256_one_by_one hash;
    std::array<char,65536> buffer;
    while(input){input.read(buffer.data(),buffer.size());hash.process(buffer.begin(),buffer.begin()+input.gcount());}
    Require(input.eof(),"Cannot finish reading YOLO tensor asset");
    hash.finish();
    Require(picosha2::get_hash_hex_string(hash)==expected,"YOLO tensor asset or evidence SHA-256 mismatch");
}
Json ReadEvidence(const std::filesystem::path& root,const Json& evidence,const char* expected) {
    Require(evidence.at("sha256")==expected,"YOLO tensor evidence must bind the pinned offline receipt");
    const auto path=Asset(root,evidence.at("path"));
    // Configuration reads are bounded; receipts, unlike model buffers, are not
    // interpreted again on the frame-processing path.
    Require(std::filesystem::file_size(path)<=2*1024*1024,"YOLO tensor evidence is oversized");
    CheckHash(path,expected);
    return ReadConfig(path);
}
std::filesystem::path Validate(const Json& manifest,const Json& profile,const std::filesystem::path& root) {
    Require(manifest.at("schema_version")==1&&manifest.at("pack_id")==pack_id&&
        manifest.at("pipeline_id")=="pipeline.yolo.tensor"&&manifest.at("profile_id")==profile_id&&
        manifest.at("profile_sha256")==profile_sha&&manifest.at("max_people")==8&&
        manifest.at("experimental")==true&&manifest.at("local_evaluation_only")==true&&
        manifest.at("execution_contract")==contract_id&&
        manifest.at("capabilities")==Json::array({"body_pose","multi_person"})&&
        manifest.at("models").is_array()&&manifest.at("models").size()==1,
        "YOLO tensor requires the pinned private schema-1 non-quantized experimental ModelPack");
    Require(profile.at("schema_version")==1&&profile.at("profile")==profile_id&&
        profile.at("experimental")==true&&profile.at("local_evaluation_only")==true&&
        profile.at("execution_contract")==contract_id&&
        profile.at("body")==Json({{"pipeline","pipeline.yolo.tensor"},{"modelPack",pack_id}})&&
        profile.at("hands")==Json({{"enabled",false},{"fps",15}})&&profile.at("body_fps")==30&&
        profile.at("output")==Json({{"hz",60}})&&
        profile.at("required_capabilities")==Json::array({"body_pose","multi_person","tensor_inference"})&&
        profile.at("backend")==Json({{"preference",Json::array({"backend.rknn"})},{"allow_fallback",false}}),
        "YOLO tensor requires the matching private RKNN-only profile without fallback");
    const auto& qualification=manifest.at("qualification");
    Require(qualification.at("offline_numerical_passed")==true&&qualification.at("deployment_ready")==false&&
        qualification.at("device_performance_verified")==false&&qualification.at("validation_index_sha256")==bank_sha&&
        qualification.at("source_onnx_sha256")==onnx_sha&&qualification.at("conversion_receipt_sha256")==conversion_sha&&
        qualification.at("simulator_comparison_sha256")==simulator_sha,
        "YOLO tensor accepts offline numerical evidence only; physical RK3588 acceptance remains open");
    const auto& model=manifest.at("models").at(0);
    Require(model.at("role")=="body"&&model.at("format")=="rknn"&&model.at("decoder_id")=="yolov8_pose_dfl17_v1"&&
        model.at("sha256")==model_sha,"YOLO tensor body model identity or precision mismatch");
    const auto& input=model.at("input_contract");
    Require(input.at("width")==width&&input.at("height")==height&&input.at("color_order")=="RGB"&&
        input.at("tensor_dtype")=="uint8"&&input.at("tensor_layout")=="NHWC"&&input.at("input_blob")=="in0"&&
        input.at("crop_mode")=="letterbox"&&input.at("pad_rgb")==Json::array({114,114,114})&&
        input.at("normalization").at("mean")==Json::array({0,0,0})&&
        input.at("normalization").at("norm")==Json::array({1./255.,1./255.,1./255.}),
        "YOLO tensor requires uint8 RGB NHWC 1x288x512x3 with compiled /255 normalization and pad114");
    const auto& output=model.at("output_contract");
    Require(output.at("decoder")=="yolov8_pose_dfl17_v1"&&output.at("output_blobs")==Json::array({"out0","out1"})&&
        output.at("tensor_dtype")=="fp32"&&output.at("rows")==rows&&output.at("columns")==Json::array({65,51})&&
        output.at("max_output_bytes")==Json({{"out0",rows*65*4},{"out1",rows*51*4}}),
        "YOLO tensor requires exact bounded FP32 out0[1,3024,65] and out1[1,3024,51]");
    Require(model.at("backend_options")==Json({{"runtime_library","librknnrt.so"},{"core_mask",7},
        {"input_layout","nhwc"},{"input_type","uint8"}}),"YOLO tensor backend options differ from the pinned non-quantized experiment");
    const auto& evidence=model.at("evidence");
    Require(evidence.is_array()&&evidence.size()==2,"YOLO tensor requires actual conversion and simulator receipts");
    const auto conversion=ReadEvidence(root,evidence.at(0),conversion_sha);
    Require(conversion.at("target")=="rk3588"&&conversion.at("toolkit_version")=="2.3.2"&&
        conversion.at("source_onnx_sha256")==onnx_sha&&conversion.at("rknn_sha256")==model_sha&&
        conversion.at("precision_request")=="non-quantized"&&conversion.at("deployment_ready")==false&&
        conversion.at("device_performance_verified")==false&&conversion.at("numerical_gate_passed")==false&&
        conversion.at("input").at("name")=="in0"&&conversion.at("input").at("shape")==Json::array({1,3,height,width})&&
        conversion.at("input").at("color_order")=="RGB"&&conversion.at("input").at("mean")==Json::array({0,0,0})&&
        conversion.at("input").at("std")==Json::array({255,255,255})&&conversion.at("calibration").empty(),
        "YOLO tensor conversion receipt is incompatible with the pinned non-quantized contract");
    const auto simulator=ReadEvidence(root,evidence.at(1),simulator_sha);
    Require(simulator.at("offline_numerical_passed")==true&&simulator.at("deployment_ready")==false&&
        simulator.at("device_performance_verified")==false&&simulator.at("identity").at("rknn_sha256")==model_sha&&
        simulator.at("identity").at("validation_index_sha256")==bank_sha&&
        simulator.at("fixtures").is_array()&&simulator.at("fixtures").size()==3,
        "YOLO tensor simulator receipt does not prove the pinned three-control offline gate");
    constexpr int people[]={7,1,0};
    for(int i=0;i<3;++i)Require(simulator.at("fixtures").at(i).at("passed")==true&&
        simulator.at("fixtures").at(i).at("expected_people")==people[i],
        "YOLO tensor seven/one/empty offline numerical controls must all pass");
    const auto path=Asset(root,model.at("asset_path"));
    CheckHash(path,model_sha);
    if(!profile.contains("private_graph_candidate"))return path;
    Require(profile.at("private_graph_candidate")=="folded-output","Unqualified private model candidate");
    // This is an additional private comparison, never a replacement of the
    // baseline pack or a waiver of its numerical admission contract. Pin the
    // graph rewrite, actual compilation and seven/one/empty simulator receipts.
    const char* files[]={"graph.json","conversion.json","comparison.json","folded-output.rknn"};
    const char* hashes[]={"31228539a990d44618f740368e811cfbc66001ff9445791a86f2d082edd40af3",
        "489c9ffc5c1c9761b89c0b6b51672c9e7c63cc4793215f5bee5a23ed0a1c6d6d",
        "f28500f793c299928de0c7e0a677d6e44d8cf2d250b82d6026c6ae3907622b10",
        "be4d785b8d4c87c9339423ec894afbb35c6baf41eb5b930e4c76f00d4301c643"};
    std::filesystem::path selected;
    for(int i=0;i<4;++i){selected=Asset(root,std::string("candidates/folded-output/")+files[i]);CheckHash(selected,hashes[i]);}
    return selected;
}

struct Axis {
    int first=0,second=0,weight0=0,weight1=0;
};
template<size_t count> void BuildAxis(int source,std::array<Axis,count>& axes,bool horizontal) noexcept {
    const double scale=double(source)/count;
    for(size_t i=0;i<count;++i){
        // Match OpenCV INTER_LINEAR uint8's float half-pixel coordinate and
        // 11-bit coefficients. Horizontal weights clamp at the edges; vertical
        // border rows repeat while retaining their fractional coefficients.
        float fraction=float((i+.5)*scale-.5);
        int first=int(std::floor(fraction));fraction-=first;
        if(horizontal&&first<0){first=0;fraction=0;}
        if(horizontal&&first>=source-1){first=source-1;fraction=0;}
        axes[i]={std::clamp(first,0,source-1),std::clamp(first+1,0,source-1),int(std::nearbyint((1.f-fraction)*2048.f)),
            int(std::nearbyint(fraction*2048.f))};
    }
}
template<size_t count> void BuildLetterboxAxis(int source,std::array<Axis,count>& axes,int extent,bool horizontal) noexcept {
    const double scale=double(source)/extent;
    for(int i=0;i<extent;++i){
        float fraction=float((i+.5)*scale-.5);
        int first=int(std::floor(fraction));fraction-=first;
        if(horizontal&&first<0){first=0;fraction=0;}
        if(horizontal&&first>=source-1){first=source-1;fraction=0;}
        axes[i]={std::clamp(first,0,source-1),std::clamp(first+1,0,source-1),int(std::nearbyint((1.f-fraction)*2048.f)),
            int(std::nearbyint(fraction*2048.f))};
    }
}
struct Instance {
    HV_HostServicesV1 host{};
    const HV_BackendApiV1* backend=nullptr;
    void* session=nullptr;
    yolo::Decoder decoder{width,height};
    std::array<uint8_t,width*height*3> pixels{};
    // CPU storage is allocated at creation and reused across all source sizes.
    std::vector<float> planar;
    bool cpu=false;
    std::array<Axis,width> horizontal{};
    std::array<Axis,height> vertical{};
    std::array<HV_BodyObservationV1,8> observations{};
    uint64_t executions=0,attempts=0,validation_failures=0;
    int capacity=0,source_width=0,source_height=0;
    bool registered=false;
    Instance(const HV_HostServicesV1& h,int limit,bool cpu_route=false)
        :host(h),decoder(width,height,cpu_route),cpu(cpu_route),capacity(limit){
        if(cpu)planar.resize(width*height*3);
    }
    ~Instance() {
        if(registered)UnregisterPipelineDiagnostics(this);
        // Even invalid tables and failing creates may hand us a partial session.
        // The host factory, rather than this plugin, owns that resource's heap.
        if(session)try{host.release_backend(host.context,backend,session);}catch(...){}
    }
    void Preprocess(const HV_VideoFrame& frame,int channels,bool rgb) noexcept {
        if(yolo::TryFastRgbPreprocess(frame,pixels.data()))return;
        if(source_width!=frame.width||source_height!=frame.height){
            BuildAxis(frame.width,horizontal,true);BuildAxis(frame.height,vertical,false);
            source_width=frame.width;source_height=frame.height;
        }
        const auto* source=static_cast<const uint8_t*>(frame.data);
        for(int y=0;y<height;++y){
            const auto& ay=vertical[y];
            const auto* top=source+size_t(ay.first)*frame.stride_bytes;
            const auto* bottom=source+size_t(ay.second)*frame.stride_bytes;
            for(int x=0;x<width;++x){
                const auto& ax=horizontal[x];
                for(int channel=0;channel<3;++channel){
                    const int offset=rgb?channel:2-channel;
                    const int a=top[size_t(ax.first)*channels+offset]*ax.weight0+
                        top[size_t(ax.second)*channels+offset]*ax.weight1;
                    const int b=bottom[size_t(ax.first)*channels+offset]*ax.weight0+
                        bottom[size_t(ax.second)*channels+offset]*ax.weight1;
                    // The staged shifts are significant for uint8 parity;
                    // floating interpolation followed by lround differs by 1.
                    const int value=(((ay.weight0*(a>>4))>>16)+((ay.weight1*(b>>4))>>16)+2)>>2;
                    pixels[(size_t(y)*width+x)*3+channel]=uint8_t(std::clamp(value,0,255));
                }
            }
        }
    }
    void PreprocessLetterbox(const HV_VideoFrame& frame,int channels,bool rgb,const yolo::Geometry& geometry) noexcept {
        if(int64_t(frame.width)*9==int64_t(frame.height)*16){Preprocess(frame,channels,rgb);return;}
        const bool width_limited=int64_t(frame.width)*height>=int64_t(frame.height)*width;
        const double scale=width_limited?double(width)/frame.width:double(height)/frame.height;
        const int rw=width_limited?width:int(frame.width*scale),rh=width_limited?int(frame.height*scale):height;
        if(source_width!=frame.width||source_height!=frame.height){
            BuildLetterboxAxis(frame.width,horizontal,rw,true);BuildLetterboxAxis(frame.height,vertical,rh,false);
            source_width=frame.width;source_height=frame.height;
        }
        pixels.fill(114);
        const auto* source=static_cast<const uint8_t*>(frame.data);
        for(int y=0;y<rh;++y){
            const auto& ay=vertical[y];
            const auto* top=source+size_t(ay.first)*frame.stride_bytes;
            const auto* bottom=source+size_t(ay.second)*frame.stride_bytes;
            for(int x=0;x<rw;++x){
                const auto& ax=horizontal[x];
                for(int channel=0;channel<3;++channel){
                    const int offset=rgb?channel:2-channel;
                    const int a=top[size_t(ax.first)*channels+offset]*ax.weight0+
                        top[size_t(ax.second)*channels+offset]*ax.weight1;
                    const int b=bottom[size_t(ax.first)*channels+offset]*ax.weight0+
                        bottom[size_t(ax.second)*channels+offset]*ax.weight1;
                    const int value=(((ay.weight0*(a>>4))>>16)+((ay.weight1*(b>>4))>>16)+2)>>2;
                    pixels[(size_t(y+geometry.top)*width+x+geometry.left)*3+channel]=uint8_t(std::clamp(value,0,255));
                }
            }
        }
    }
    void Publish(uint32_t count,float inference_ms,int64_t timestamp) noexcept {
        PipelineDiagnostics value{};
        value.detector_execution_count=executions;value.detector_attempted=attempts;
        value.pose_validation_failures=validation_failures;value.detector_keyframe=true;
        value.cadence_interval_frames=1;value.accepted_detection_count=count;value.pose_person_count=count;
        // V1 source timestamps remain on observations; they are not guaranteed
        // to share the native steady-clock epoch used for optional detector age.
        (void)timestamp;
        value.detector_inference_ms=inference_ms;value.last_detector_capture_steady_us=0;
        PublishPipelineDiagnostics(this,value);
    }
};

std::filesystem::path ValidateCpu(const Json& manifest,const Json& profile,const std::filesystem::path& root) {
    Require(manifest.at("schema_version")==1&&manifest.at("pack_id")=="yolov8n-pose-512x288-ort"&&
        manifest.at("pipeline_id")=="pipeline.yolo.cpu"&&manifest.at("max_people")==8&&
        manifest.at("capabilities")==Json::array({"body_pose","multi_person"})&&
        manifest.at("models").is_array()&&manifest.at("models").size()==1,
        "YOLO CPU requires the pinned schema-1 512x288 ONNX ModelPack");
    const auto name=profile.at("profile").get<std::string>();
    Require(profile.at("schema_version")==1&&(name=="android-cpu-nohands"||name=="comparison-yolo-cpu")&&
        profile.at("body")==Json({{"pipeline","pipeline.yolo.cpu"},{"modelPack","yolov8n-pose-512x288-ort"}})&&
        profile.at("hands").at("enabled")==false&&
        profile.at("backend")==Json({{"preference",Json::array({"backend.ort.cpu"})},{"allow_fallback",false}}),
        "YOLO CPU requires an explicit Android/comparison CPU-only profile, disabled hands and no fallback");
    const auto& model=manifest.at("models").at(0);
    Require(model.at("role")=="body"&&model.at("format")=="onnx"&&model.at("sha256")==onnx_sha&&
        model.at("decoder_id")=="yolov8_pose_dfl17_v1",
        "YOLO CPU weights or decoder differ from the qualified source ONNX");
    const auto& input=model.at("input_contract");
    Require(input.at("width")==width&&input.at("height")==height&&input.at("tensor_layout")=="NCHW"&&
        input.at("tensor_dtype")=="fp32"&&input.at("color_order")=="RGB"&&input.at("input_blob")=="in0"&&
        input.at("crop_mode")=="letterbox"&&input.at("pad_rgb")==Json::array({114,114,114})&&
        input.at("normalization")==Json({{"mean",{0,0,0}},{"norm",{1./255.,1./255.,1./255.}}}),
        "YOLO CPU requires RGB FP32 NCHW in0[1,3,288,512], centered pad114 and exactly one /255 normalization");
    const auto& output=model.at("output_contract");
    Require(output.at("decoder")=="yolov8_pose_dfl17_v1"&&output.at("output_blobs")==Json::array({"out0","out1"})&&
        output.at("tensor_dtype")=="fp32"&&output.at("rows")==rows&&output.at("columns")==Json::array({65,51})&&
        output.at("max_output_bytes")==Json({{"out0",rows*65*4},{"out1",rows*51*4}}),
        "YOLO CPU requires exact bounded FP32 out0[1,3024,65] and out1[1,3024,51]");
    Require(!model.contains("backend_options")||model.at("backend_options")==Json::object(),
        "YOLO CPU does not accept backend overrides or backend-side normalization");
    const auto path=Asset(root,model.at("asset_path"));CheckHash(path,onnx_sha);return path;
}
HV_Result CreateImpl(const HV_PipelineConfigV1* config,const HV_HostServicesV1* host,void** out,HV_ErrorBufferV1* error,bool cpu) {
    if(out)*out=nullptr;
    if(!out||!config||!host||config->struct_size<sizeof(*config)||config->api_version!=HV_PLUGIN_API_V1||
        host->struct_size<sizeof(*host)||host->api_version!=HV_PLUGIN_API_V1||!host->create_backend||!host->release_backend||
        !config->model_manifest_utf8||!config->asset_root_utf8||!config->options_utf8||config->max_bodies<1||config->max_bodies>8||
        config->reserved){Error(error,"Invalid YOLO tensor pipeline configuration or host services");return HV_ERR_INVALID_ARGUMENT;}
    try {
        const auto manifest=Json::parse(config->model_manifest_utf8),profile=Json::parse(config->options_utf8);
        const auto path=(cpu?ValidateCpu:Validate)(manifest,profile,std::filesystem::u8path(config->asset_root_utf8)).u8string();
        auto backend_options=cpu?Json::object():manifest.at("models").at(0).at("backend_options");
        // Validate() already pins the model and its compiled normalization. Pass
        // that contract explicitly; the generic backend must never guess /255.
        if(!cpu)backend_options["input_transform"]=manifest.at("models").at(0).at("input_contract").at("normalization");
#if defined(HV_RKNN_INPUT_MEMORY_EXPERIMENT)
        // Only the private comparison coordinator supplies per-instance cores.
        // Canonical profile/model bytes and the default three-core path stay pinned.
        if(!cpu&&profile.contains("private_instance_core_mask")) {
            const auto& mask=profile.at("private_instance_core_mask");
            Require(mask.is_number_integer()&&(mask==1||mask==2||mask==4),"Invalid private instance core mask");
            backend_options["core_mask"]=mask;
        }
#endif
        const auto options=backend_options.dump();
        auto self=std::make_unique<Instance>(*host,config->max_bodies,cpu);
        const HV_BackendConfigV1 backend_config{sizeof(backend_config),HV_PLUGIN_API_V1,path.c_str(),options.c_str(),cpu?"backend.ort.cpu":"backend.rknn"};
        const auto status=host->create_backend(host->context,&backend_config,&self->backend,&self->session,error);
        if(status!=HV_OK)return status;
        if(!self->backend||!self->session||self->backend->struct_size<sizeof(HV_BackendApiV1)||
            self->backend->api_version!=HV_PLUGIN_API_V1||!self->backend->run){
            Error(error,cpu?"CPU host factory returned an invalid tensor backend table or session":
                "RKNN host factory returned an invalid tensor backend table or session");return HV_ERR_MODEL_LOAD;
        }
        if(!RegisterPipelineDiagnostics(self.get())){Error(error,"Pipeline diagnostics capacity exhausted");return HV_ERR_INTERNAL;}
        self->registered=true;Error(error,"");*out=self.release();return HV_OK;
    }catch(const std::exception& ex){Error(error,ex.what());return HV_ERR_MODEL_LOAD;}
    catch(...){Error(error,"YOLO tensor creation failed");return HV_ERR_INTERNAL;}
}
HV_Result HV_CALL Create(const HV_PipelineConfigV1* c,const HV_HostServicesV1* h,void** out,HV_ErrorBufferV1* e){return CreateImpl(c,h,out,e,false);}
HV_Result HV_CALL CreateCpu(const HV_PipelineConfigV1* c,const HV_HostServicesV1* h,void** out,HV_ErrorBufferV1* e){return CreateImpl(c,h,out,e,true);}
void HV_CALL Destroy(void* instance) {delete static_cast<Instance*>(instance);}
bool OutputTensor(const HV_TensorViewV1& view,const char* name,int columns) noexcept {
    if(view.struct_size<sizeof(view)||view.api_version!=HV_PLUGIN_API_V1||!view.name||std::strcmp(view.name,name)||
        view.element_type!=1||view.rank!=3||view.dimensions[0]!=1||view.dimensions[1]!=rows||view.dimensions[2]!=columns||
        !view.data||view.byte_count!=uint64_t(rows)*columns*sizeof(float))return false;
    for(int i=3;i<8;++i)if(view.dimensions[i])return false;
    return true;
}
HV_Result HV_CALL Process(void* instance,const HV_PipelineInputV1* input,HV_PipelineOutputV1* out,HV_ErrorBufferV1* error) {
    if(!out||out->struct_size<sizeof(*out)||out->api_version!=HV_PLUGIN_API_V1)return HV_ERR_INVALID_ARGUMENT;
    out->body_count=out->hand_count=0;out->preprocess_ms=out->inference_ms=out->postprocess_ms=0;
    if(!instance||!input||input->struct_size<sizeof(*input)||input->api_version!=HV_PLUGIN_API_V1||input->reserved||input->roi_count){
        Error(error,"Invalid YOLO tensor pipeline input");return HV_ERR_INVALID_ARGUMENT;
    }
    auto& self=*static_cast<Instance*>(instance);
    const auto& frame=input->frame;
    if(frame.struct_size<sizeof(frame)||!frame.data||frame.data_bytes<1||frame.width<1||frame.height<1||
        frame.stride_bytes<1||frame.frame_id<0||frame.timestamp_us<0||!out->bodies||out->body_capacity<uint32_t(self.capacity)){
        Error(error,"Invalid YOLO tensor frame or insufficient body output capacity");return HV_ERR_INVALID_ARGUMENT;
    }
    int channels=0;bool rgb=false;
    switch(frame.pixel_format){
        case HV_PIXEL_RGB24:channels=3;rgb=true;break;
        case HV_PIXEL_BGR24:channels=3;break;
        case HV_PIXEL_RGBA32:channels=4;rgb=true;break;
        case HV_PIXEL_BGRA32:channels=4;break;
        default:Error(error,"YOLO tensor supports RGB24/BGR24/RGBA32/BGRA32 only");return HV_ERR_UNSUPPORTED_FORMAT;
    }
    yolo::Geometry geometry{};
    const bool geometry_valid=self.cpu?yolo::BuildLetterboxGeometry(frame.width,frame.height,width,height,geometry):
        yolo::BuildGeometry(frame.width,frame.height,width,height,geometry);
    if(!geometry_valid||
        int64_t(frame.width)*channels>frame.stride_bytes||
        int64_t(frame.stride_bytes)*(frame.height-1)+int64_t(frame.width)*channels>frame.data_bytes){
        Error(error,self.cpu?"YOLO CPU requires a complete stride-aware frame with nonzero letterbox dimensions":
            "YOLO tensor requires a complete stride-aware 16:9 landscape frame");return HV_ERR_INVALID_ARGUMENT;
    }
    try {
        const auto begin=Clock::now();
        if(self.cpu)self.PreprocessLetterbox(frame,channels,rgb,geometry);else self.Preprocess(frame,channels,rgb);
        if(self.cpu)for(size_t pixel=0;pixel<size_t(width)*height;++pixel)
            for(int channel=0;channel<3;++channel)
                self.planar[size_t(channel)*width*height+pixel]=self.pixels[pixel*3+channel]/255.f;
        const auto prepared=Clock::now();
        out->preprocess_ms=Milliseconds(begin,prepared);
        HV_TensorViewV1 tensor{sizeof(tensor),HV_PLUGIN_API_V1,"in0",3,4,{1,height,width,3},self.pixels.data(),self.pixels.size()};
        if(self.cpu)tensor={sizeof(tensor),HV_PLUGIN_API_V1,"in0",1,4,{1,3,height,width},self.planar.data(),self.planar.size()*sizeof(float)};
        HV_TensorViewV1 outputs[2]{};uint32_t tensor_count=0;++self.attempts;
        const auto status=self.backend->run(self.session,&tensor,1,outputs,2,&tensor_count,error);
        const auto inferred=Clock::now();out->inference_ms=Milliseconds(prepared,inferred);
        if(status!=HV_OK){self.Publish(0,out->inference_ms,frame.timestamp_us);return status;}
        ++self.executions;
        float scores[8]{};uint32_t count=0;
        if(tensor_count!=2||!OutputTensor(outputs[0],"out0",65)||!OutputTensor(outputs[1],"out1",51)||
            !self.decoder.Decode(outputs,2,geometry,frame.timestamp_us,self.observations.data(),scores,self.capacity,count)){
            ++self.validation_failures;self.Publish(0,out->inference_ms,frame.timestamp_us);
            Error(error,"YOLO tensor output shape, dtype, names, byte counts or finite values are invalid");return HV_ERR_INTERNAL;
        }
        std::copy_n(self.observations.data(),count,out->bodies);out->body_count=count;
        out->postprocess_ms=Milliseconds(inferred,Clock::now());self.Publish(count,out->inference_ms,frame.timestamp_us);
        Error(error,"");return HV_OK;
    }catch(const std::exception& ex){out->body_count=0;self.Publish(0,out->inference_ms,frame.timestamp_us);Error(error,ex.what());return HV_ERR_INTERNAL;}
    catch(...){out->body_count=0;self.Publish(0,out->inference_ms,frame.timestamp_us);Error(error,"YOLO tensor processing failed");return HV_ERR_INTERNAL;}
}
const HV_PipelineApiV1 api{sizeof(api),HV_PLUGIN_API_V1,Create,Destroy,Process};
const HV_PipelineApiV1 cpu_api{sizeof(cpu_api),HV_PLUGIN_API_V1,CreateCpu,Destroy,Process};
}
extern "C" HV_Result HV_CALL HV_QueryYoloCpuPipelineV1(uint32_t version,HV_PluginApiV1* out) {
 if(!out||version!=HV_PLUGIN_API_V1||out->struct_size<sizeof(*out)||out->api_version!=HV_PLUGIN_API_V1)return HV_ERR_INVALID_ARGUMENT;
 *out={sizeof(*out),HV_PLUGIN_API_V1,"pipeline.yolo.cpu","0.4.0-preview.7",HV_PLUGIN_PIPELINE,HV_CAP_BODY_POSE|HV_CAP_MULTI_PERSON,8,&cpu_api,nullptr,0};return HV_OK;
}
extern "C" HV_Result HV_CALL HV_QueryYoloTensorPipelineV1(uint32_t version,HV_PluginApiV1* out) {
 if(!out||version!=HV_PLUGIN_API_V1||out->struct_size<sizeof(*out)||out->api_version!=HV_PLUGIN_API_V1)return HV_ERR_INVALID_ARGUMENT;
 *out={sizeof(*out),HV_PLUGIN_API_V1,"pipeline.yolo.tensor","0.4.0-preview.6",HV_PLUGIN_PIPELINE,HV_CAP_BODY_POSE|HV_CAP_MULTI_PERSON,8,&api,nullptr,0};return HV_OK;
}
