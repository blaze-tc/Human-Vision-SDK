#pragma once

#include "json/json.hpp"
#include <string>

namespace humanvision::runtime::ncnn_backend {

struct BackendOptions {
    bool use_subgroup_ops = true;
    bool use_fp16_arithmetic = true;
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
}

} // namespace humanvision::runtime::ncnn_backend
