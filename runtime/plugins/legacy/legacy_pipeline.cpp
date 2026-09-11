#include "plugins/legacy/legacy_pipeline.h"
#include "common/plugin_backend.h"
#include "models/rtmdet/rtmdet_model.h"
#include "models/rtmpose/rtmpose_model.h"
#include "common/config_io.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace {
using namespace humanvision;
// Transitional recognizer only: no identity, region assignment or rendering state.
struct Instance {
    explicit Instance(HV_HostServicesV1 services):detector(std::make_unique<humanvision::runtime::PluginBackend>(services)),pose(std::make_unique<humanvision::runtime::PluginBackend>(services)){}
    RtmdetModel detector;
    RtmposeModel pose;
    FrameBuffer frame;
    std::vector<Detection> detections;
    int capacity=1;
};
void Error(HV_ErrorBufferV1* out,const std::string& message) {
    if (!out || out->struct_size<sizeof(*out) || !out->data || !out->capacity) return;
    const auto size=std::min(message.size(),static_cast<size_t>(out->capacity-1));
    std::memcpy(out->data,message.data(),size);out->data[size]=0;
}
HV_Result HV_CALL Create(const HV_PipelineConfigV1* config,const HV_HostServicesV1* services,void** out,HV_ErrorBufferV1* error) {
    if (!out) return HV_ERR_INVALID_ARGUMENT;
    *out=nullptr;
    if(!services||services->struct_size<sizeof(*services)||services->api_version!=HV_PLUGIN_API_V1||!services->create_backend||!services->release_backend)return HV_ERR_INVALID_ARGUMENT;
    if(!config || config->struct_size<sizeof(*config) || config->api_version!=HV_PLUGIN_API_V1 ||
        config->max_bodies<1 || config->max_bodies>HV_MAX_PEOPLE || !config->model_manifest_utf8 || !config->asset_root_utf8) return HV_ERR_INVALID_ARGUMENT;
    try {
        auto instance=std::make_unique<Instance>(*services);instance->capacity=config->max_bodies;
        const auto manifest=nlohmann::json::parse(config->model_manifest_utf8);
        std::filesystem::path detector,body;
        for (const auto& model:manifest.at("models")) {
            const auto role=model.at("role").get<std::string>();
            const auto path=humanvision::runtime::ConfinedPath(std::filesystem::u8path(config->asset_root_utf8),std::filesystem::u8path(model.at("asset_path").get<std::string>()));
            if(role=="detector") detector=path;
            else if(role=="body") body=path;
        }
        std::string detail;
        if(detector.empty() || body.empty()) {Error(error,"Legacy pipeline requires detector and body model roles");return HV_ERR_MODEL_LOAD;}
        if(!instance->detector.Load(detector,detail) || !instance->pose.Load(body,detail)) {Error(error,detail);return HV_ERR_MODEL_LOAD;}
        instance->detections.reserve(instance->capacity);*out=instance.release();return HV_OK;
    } catch(const std::exception& exception) {Error(error,exception.what());return HV_ERR_MODEL_LOAD;}
    catch(...) {Error(error,"Legacy pipeline initialization exception");return HV_ERR_INTERNAL;}
}
void HV_CALL Destroy(void* value) {delete static_cast<Instance*>(value);}
HV_Result HV_CALL Process(void* value,const HV_PipelineInputV1* input,HV_PipelineOutputV1* output,HV_ErrorBufferV1* error) {
    if(!value || !input || !output || input->struct_size<sizeof(*input) || input->api_version!=HV_PLUGIN_API_V1 ||
        output->struct_size<sizeof(*output) || output->api_version!=HV_PLUGIN_API_V1) return HV_ERR_INVALID_ARGUMENT;
    output->body_count=output->hand_count=0;
    output->preprocess_ms=output->inference_ms=output->postprocess_ms=0;
    if(input->roi_count) {Error(error,"Legacy pipeline does not accept ROI requests; apply the common region mask before submission");return HV_ERR_INVALID_ARGUMENT;}
    auto& instance=*static_cast<Instance*>(value);
    if(!output->bodies || output->body_capacity<static_cast<uint32_t>(instance.capacity)) return HV_ERR_INVALID_ARGUMENT;
    try {
        std::string detail;const auto valid=ValidateVideoFrame(&input->frame,detail);
        if(valid!=HV_OK){Error(error,detail);return valid;}
        const auto& source=input->frame;auto& frame=instance.frame;
        frame.width=source.width;frame.height=source.height;frame.stride_bytes=source.stride_bytes;
        frame.pixel_format=source.pixel_format;frame.frame_id=source.frame_id;frame.timestamp_us=source.timestamp_us;
        frame.bytes.resize(source.data_bytes);std::memcpy(frame.bytes.data(),source.data,source.data_bytes);
        float detector_ms=0;
        if(!instance.detector.Detect(frame,.35F,instance.capacity,instance.detections,detector_ms,detail)) {Error(error,detail);return HV_ERR_INTERNAL;}
        output->inference_ms=detector_ms;
        constexpr std::array<int,17> mapping{HV_CANONICAL_NOSE,HV_CANONICAL_EYE_LEFT,HV_CANONICAL_EYE_RIGHT,
            HV_CANONICAL_EAR_LEFT,HV_CANONICAL_EAR_RIGHT,HV_CANONICAL_SHOULDER_LEFT,HV_CANONICAL_SHOULDER_RIGHT,
            HV_CANONICAL_ELBOW_LEFT,HV_CANONICAL_ELBOW_RIGHT,HV_CANONICAL_WRIST_LEFT,HV_CANONICAL_WRIST_RIGHT,
            HV_CANONICAL_HIP_LEFT,HV_CANONICAL_HIP_RIGHT,HV_CANONICAL_KNEE_LEFT,HV_CANONICAL_KNEE_RIGHT,
            HV_CANONICAL_ANKLE_LEFT,HV_CANONICAL_ANKLE_RIGHT};
        for(const auto& detection:instance.detections) {
            std::array<HV_Joint,HV_JOINT_COUNT> joints{};float pose_ms=0;
            if(!instance.pose.Estimate(frame,detection,.3F,joints,pose_ms,detail)) {Error(error,detail);output->body_count=0;return HV_ERR_INTERNAL;}
            output->inference_ms+=pose_ms;
            auto& body=output->bodies[output->body_count++];body={};body.struct_size=sizeof(body);body.api_version=HV_PLUGIN_API_V1;
            body.confidence=detection.score;body.bbox_px={detection.x1,detection.y1,detection.x2-detection.x1,detection.y2-detection.y1};
            for(auto& joint:body.joints) {joint.struct_size=sizeof(joint);joint.api_version=HV_API_VERSION_040;}
            for(size_t i=0;i<joints.size();++i) {
                auto& joint=body.joints[mapping[i]];const auto& old=joints[i];
                joint.struct_size=sizeof(joint);joint.api_version=HV_API_VERSION_040;
                joint.x_px=old.x_px;joint.y_px=old.y_px;joint.x_norm=old.x_norm;joint.y_norm=old.y_norm;
                joint.confidence=old.confidence;joint.valid=old.valid;joint.observation_timestamp_us=source.timestamp_us;
            }
        }
        return HV_OK;
    } catch(const std::exception& exception) {output->body_count=0;Error(error,exception.what());return HV_ERR_INTERNAL;}
    catch(...) {output->body_count=0;Error(error,"Legacy pipeline processing exception");return HV_ERR_INTERNAL;}
}
const HV_PipelineApiV1 api{sizeof(api),HV_PLUGIN_API_V1,Create,Destroy,Process};
}
extern "C" HV_Result HV_CALL HV_QueryLegacyPipeline(uint32_t version,HV_PluginApiV1* out) {
    if(version!=HV_PLUGIN_API_V1 || !out || out->struct_size<sizeof(*out)) return HV_ERR_INVALID_ARGUMENT;
    *out={sizeof(*out),HV_PLUGIN_API_V1,"pipeline.legacy","0.4.0-preview.1",HV_PLUGIN_PIPELINE,HV_CAP_BODY_POSE|HV_CAP_MULTI_PERSON,HV_MAX_PEOPLE,&api,nullptr,0};return HV_OK;
}
