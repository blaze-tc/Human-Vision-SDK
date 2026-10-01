#pragma once

#include "json/json.hpp"
#include <string>

namespace humanvision::runtime::ncnn_backend {

struct BackendOptions {
    bool use_subgroup_ops = true;
    bool use_fp16_arithmetic = true;
    bool raw_tensor = false;
    bool use_packing_layout = false;
    bool use_winograd_convolution = true;
    bool use_sgemm_convolution = true;
};

// ModelPack role options are required and pinned by the local pack builder.
// Rejecting an absent or unexpected option prevents ncnn from silently using
// its defaults when a pose model needs the audited non-subgroup path.
inline bool ParseBackendOptions(const nlohmann::json& model,
                                BackendOptions& result, std::string& error) {
    if (!model.is_object() || !model.contains("role") || !model.at("role").is_string() ||
        !model.contains("backend_options") || !model.at("backend_options").is_object()) {
        error = "ncnn model requires role and backend_options";
        return false;
    }
    const auto role = model.at("role").get<std::string>();
    const auto& value = model.at("backend_options");
    const auto execution_contract=model.value("execution_contract", std::string{});
    const bool sgemm=execution_contract=="raw_tensor_fp32_sgemm_v1";
    if (execution_contract == "raw_tensor_fp32_v1" || sgemm) {
        const char* fields[]={"use_subgroup_ops","use_fp16_arithmetic","use_fp16_packed","use_fp16_storage","use_packing_layout",
                             "use_winograd_convolution","use_sgemm_convolution"};
        const size_t count=sgemm?7:5;
        if (value.size()!=count) {error=sgemm?"Raw tensor SGEMM execution requires all seven explicit backend options":"Raw tensor execution requires all five explicit backend options";return false;}
        for(size_t i=0;i<count;++i)if(!value.contains(fields[i])||!value.at(fields[i]).is_boolean()) {
            error="Raw tensor backend option missing or nonboolean";return false;
        }
        if (value.at("use_subgroup_ops").get<bool>() || value.at("use_fp16_arithmetic").get<bool>() ||
            value.at("use_fp16_packed").get<bool>() || value.at("use_fp16_storage").get<bool>() ||
            !value.at("use_packing_layout").get<bool>()) {
            error="Raw tensor FP32 contract requires internal packing and disables FP16 and subgroup";return false;
        }
        if(sgemm&&(value.at("use_winograd_convolution").get<bool>()||!value.at("use_sgemm_convolution").get<bool>())) {
            error="Raw tensor FP32 SGEMM contract disables Winograd and enables SGEMM";return false;
        }
        result={false,false,true,true,!sgemm,true};error.clear();return true;
    }
    if(model.contains("execution_contract")) {error="Unsupported execution_contract";return false;}
    if ((role != "body" && role != "detector") || value.size() != 2 ||
        !value.contains("use_subgroup_ops") || !value.at("use_subgroup_ops").is_boolean() ||
        !value.contains("use_fp16_arithmetic") || !value.at("use_fp16_arithmetic").is_boolean()) {
        error = "ncnn model has invalid role or backend_options";
        return false;
    }
    const bool subgroup = value.at("use_subgroup_ops").get<bool>();
    const bool arithmetic = value.at("use_fp16_arithmetic").get<bool>();
    const bool expected = role == "detector";
    if (subgroup != expected || arithmetic != expected) {
        error = "ncnn backend_options do not match the selected model role";
        return false;
    }
    result = BackendOptions{subgroup, arithmetic};
    error.clear();
    return true;
}

template <typename Option>
inline void ApplyBackendOptions(Option& option, const BackendOptions& model_options) {
    option.use_subgroup_ops = model_options.use_subgroup_ops;
    option.use_fp16_arithmetic = model_options.use_fp16_arithmetic;
    option.use_winograd_convolution = model_options.use_winograd_convolution;
    option.use_sgemm_convolution = model_options.use_sgemm_convolution;
}

} // namespace humanvision::runtime::ncnn_backend
