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

std::shared_ptr<const ModelPack> ModelPackManager::Resolve(const std::string& id, std::string& error) const {
    try {
        if (!ValidComponentId(id)) throw std::runtime_error("Invalid ModelPack id: " + id);
        auto pack = std::make_shared<ModelPack>();
        pack->root = ConfinedPath(root_, id);
        auto json = ReadConfig(ConfinedPath(pack->root, "manifest.json"));
        if (json.at("schema_version").get<int>() != 1) throw std::runtime_error("Unsupported ModelPack schema version");
        pack->id = json.at("pack_id").get<std::string>();
        pack->version = json.at("pack_version").get<std::string>();
        pack->pipeline_id = json.at("pipeline_id").get<std::string>();
        pack->max_people = json.at("max_people").get<int>();
        if (pack->id != id || pack->version.empty() || !ValidComponentId(pack->pipeline_id) || pack->max_people < 1 || pack->max_people > HV_MAX_PEOPLE)
            throw std::runtime_error("Invalid ModelPack identity or capacity");
        for (const auto& cap : json.at("capabilities")) pack->capabilities |= CapabilityBit(cap.get<std::string>());
        if (!pack->capabilities) throw std::runtime_error("ModelPack capabilities cannot be empty");
        const auto& models = json.at("models");
        if (!models.is_array() || models.empty()) throw std::runtime_error("ModelPack models must be a nonempty array");
        std::set<std::string> roles;
        for (const auto& value : models) {
            ModelAsset asset;
            asset.role = value.at("role").get<std::string>();
            asset.format = value.at("format").get<std::string>();
            asset.decoder_id = value.at("decoder_id").get<std::string>();
            if (!ValidComponentId(asset.role) || !roles.insert(asset.role).second || asset.format.empty() || asset.decoder_id.empty())
                throw std::runtime_error("Duplicate or invalid model role/contract");
            const auto relative = value.at("asset_path").get<std::string>();
            if (relative.find('\\') != std::string::npos || relative.find(':') != std::string::npos)
                throw std::runtime_error("Asset paths must use relative forward-slash notation");
            asset.path = ConfinedPath(pack->root, std::filesystem::u8path(relative));
            if (!std::filesystem::is_regular_file(asset.path)) throw std::runtime_error("Model asset is not a regular file");
            asset.sha256 = value.at("sha256").get<std::string>();
            if (!std::regex_match(asset.sha256, std::regex("[0-9a-f]{64}")) || HashFile(asset.path) != asset.sha256)
                throw std::runtime_error("Model SHA-256 mismatch: " + asset.role);
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
