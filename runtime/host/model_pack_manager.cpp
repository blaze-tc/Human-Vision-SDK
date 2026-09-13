#include "host/model_pack_manager.h"
#include "common/config_io.h"
#include "picosha2/picosha2.h"
#include <array>
#include <set>
#include <regex>

namespace humanvision::runtime {
bool ValidComponentId(const std::string& id) {
    static const std::regex pattern("[a-z0-9][a-z0-9._-]{0,63}");
    return std::regex_match(id, pattern);
}

uint64_t CapabilityBit(const std::string& name) {
    if (name == "body_pose") return HV_CAP_BODY_POSE;
    if (name == "hand_pose") return HV_CAP_HAND_POSE;
    if (name == "multi_person") return HV_CAP_MULTI_PERSON;
    if (name == "tensor_inference") return HV_CAP_TENSOR_INFERENCE;
    if (name == "dynamic_input") return HV_CAP_DYNAMIC_INPUT;
    if (name == "batch") return HV_CAP_BATCH;
    if (name == "gpu_input") return HV_CAP_GPU_INPUT;
    if (name == "vulkan") return HV_CAP_VULKAN;
    if (name == "fp16-storage") return HV_CAP_FP16_STORAGE;
    if (name == "fp16-arithmetic") return HV_CAP_FP16_ARITHMETIC;
    if (name == "android-hardware-buffer") return HV_CAP_ANDROID_HARDWARE_BUFFER;
    if (name == "external-sync-fd") return HV_CAP_EXTERNAL_SYNC_FD;
    throw std::runtime_error("Unknown capability: " + name);
}

static std::string HashFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read model asset: " + path.u8string());
    picosha2::hash256_one_by_one hash;
    std::array<char, 65536> buffer;
    while (stream) { stream.read(buffer.data(), buffer.size()); hash.process(buffer.begin(), buffer.begin() + stream.gcount()); }
    if (!stream.eof()) throw std::runtime_error("Cannot finish reading model asset");
    hash.finish(); return picosha2::get_hash_hex_string(hash);
}

static std::string RequiredString(const nlohmann::json& value, const char* name) {
    if (!value.contains(name) || !value.at(name).is_string() || value.at(name).get<std::string>().empty())
        throw std::runtime_error(std::string("Missing or invalid ") + name);
    return value.at(name).get<std::string>();
}

static std::filesystem::path ModelFilePath(const std::filesystem::path& root, const std::string& relative) {
    if (relative.find('\\') != std::string::npos || relative.find(':') != std::string::npos)
        throw std::runtime_error("Asset paths must use relative forward-slash notation");
    const auto path = ConfinedPath(root, std::filesystem::u8path(relative));
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Model asset is not a regular file: " + relative);
    return path;
}

static void ValidateSha256(const std::filesystem::path& path, const std::string& sha256, const std::string& label) {
    if (!std::regex_match(sha256, std::regex("[0-9a-f]{64}")) || HashFile(path) != sha256)
        throw std::runtime_error("Model SHA-256 mismatch: " + label);
}

static void ValidateThreeNumbers(const nlohmann::json& value, const char* name) {
    if (!value.contains(name) || !value.at(name).is_array() || value.at(name).size() != 3)
        throw std::runtime_error(std::string("Missing or invalid normalization.") + name);
    for (const auto& number : value.at(name))
        if (!number.is_number()) throw std::runtime_error(std::string("Missing or invalid normalization.") + name);
}

static void ValidateSchema2Contract(const nlohmann::json& value) {
    if (RequiredString(value, "format") != "ncnn") throw std::runtime_error("Schema 2 model format must be ncnn");
    const auto& input = value.at("input_contract");
    const auto& output = value.at("output_contract");
    if (!input.is_object()) throw std::runtime_error("Missing or invalid input_contract");
    if (!output.is_object()) throw std::runtime_error("Missing or invalid output_contract");
    RequiredString(input, "image_format");
    RequiredString(input, "color_order");
    RequiredString(input, "tensor_dtype");
    RequiredString(input, "input_blob");
    if (!input.contains("width") || !input.at("width").is_number_integer() || input.at("width").get<int>() < 1)
        throw std::runtime_error("Missing or invalid width");
    if (!input.contains("height") || !input.at("height").is_number_integer() || input.at("height").get<int>() < 1)
        throw std::runtime_error("Missing or invalid height");
    if (!input.contains("elempack") || !input.at("elempack").is_number_integer() ||
        (input.at("elempack").get<int>() != 1 && input.at("elempack").get<int>() != 4 && input.at("elempack").get<int>() != 8))
        throw std::runtime_error("Missing or invalid elempack");
    if (!input.contains("normalization") || !input.at("normalization").is_object())
        throw std::runtime_error("Missing or invalid normalization");
    ValidateThreeNumbers(input.at("normalization"), "mean");
    ValidateThreeNumbers(input.at("normalization"), "norm");
    if (!output.contains("output_blobs") || !output.at("output_blobs").is_array() || output.at("output_blobs").empty())
        throw std::runtime_error("Missing or invalid output_blobs");
    std::set<std::string> blobs;
    for (const auto& blob : output.at("output_blobs")) {
        if (!blob.is_string() || blob.get<std::string>().empty() || !blobs.insert(blob.get<std::string>()).second)
            throw std::runtime_error("Missing, invalid or duplicate output_blobs");
    }
    RequiredString(value, "source");
    RequiredString(value, "license");
    RequiredString(value, "conversion_recipe");
}

std::shared_ptr<const ModelPack> ModelPackManager::Resolve(const std::string& id, std::string& error) const {
    try {
        if (!ValidComponentId(id)) throw std::runtime_error("Invalid ModelPack id: " + id);
        auto pack = std::make_shared<ModelPack>();
        pack->root = ConfinedPath(root_, id);
        auto manifest = pack->root / "manifest.json";
        if (!std::filesystem::is_regular_file(manifest)) manifest = pack->root / "modelpack.json";
        auto json = ReadConfig(manifest);
        const int schema = json.at("schema_version").get<int>();
        if (schema != 1 && schema != 2) throw std::runtime_error("Unsupported ModelPack schema version");
        pack->id = json.at("pack_id").get<std::string>();
        pack->version = json.at("pack_version").get<std::string>();
        pack->pipeline_id = json.at("pipeline_id").get<std::string>();
        pack->max_people = json.at("max_people").get<int>();
        if (pack->id != id || pack->version.empty() || !ValidComponentId(pack->pipeline_id) || pack->max_people < 1 || pack->max_people > HV_MAX_PEOPLE)
            throw std::runtime_error("Invalid ModelPack identity or capacity");
        for (const auto& cap : json.at("capabilities")) pack->capabilities |= CapabilityBit(cap.get<std::string>());
        if (!pack->capabilities) throw std::runtime_error("ModelPack capabilities cannot be empty");
        if (schema == 2) {
            for (const auto& capability : {"vulkan", "fp16-storage", "fp16-arithmetic"})
                if ((pack->capabilities & CapabilityBit(capability)) == 0)
                    throw std::runtime_error(std::string("Schema 2 ModelPack missing required capability: ") + capability);
        }
        const auto& models = json.at("models");
        if (!models.is_array() || models.empty()) throw std::runtime_error("ModelPack models must be a nonempty array");
        std::set<std::string> roles;
        for (const auto& value : models) {
            ModelAsset asset;
            asset.role = value.at("role").get<std::string>();
            asset.format = RequiredString(value, "format");
            asset.decoder_id = RequiredString(value, "decoder_id");
            if (!ValidComponentId(asset.role) || !roles.insert(asset.role).second || asset.format.empty() || asset.decoder_id.empty())
                throw std::runtime_error("Duplicate or invalid model role/contract");
            if (schema == 1) {
                const auto relative = value.at("asset_path").get<std::string>();
                asset.path = ModelFilePath(pack->root, relative);
                asset.sha256 = value.at("sha256").get<std::string>();
                ValidateSha256(asset.path, asset.sha256, asset.role);
            } else {
                ValidateSchema2Contract(value);
                for (const auto& descriptor : {std::pair<const char*, const char*>("param", "param_path"),
                                               std::pair<const char*, const char*>("bin", "bin_path")}) {
                    ModelFile file;
                    file.name = descriptor.first;
                    const auto relative = RequiredString(value, descriptor.second);
                    file.path = ModelFilePath(pack->root, relative);
                    const auto hash_name = file.name + "_sha256";
                    file.sha256 = RequiredString(value, hash_name.c_str());
                    ValidateSha256(file.path, file.sha256, asset.role + ":" + file.name);
                    asset.files.push_back(std::move(file));
                }
            }
            if (!value.at("input_contract").is_object() || !value.at("output_contract").is_object() ||
                value.at("source").get<std::string>().empty() || value.at("license").get<std::string>().empty())
                throw std::runtime_error("Missing model contract or provenance");
            asset.input_contract = value.at("input_contract").dump();
            asset.output_contract = value.at("output_contract").dump();
            pack->models.push_back(std::move(asset));
        }
        pack->manifest_json = json.dump(); error.clear(); return pack;
    } catch (const std::exception& exception) { error = "ModelPack " + id + ": " + exception.what(); return {}; }
}
}
