#pragma once
#include "humanvision_plugin.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace humanvision::runtime {
struct ModelAsset {
    std::string role, format, decoder_id, sha256, input_contract, output_contract;
    std::filesystem::path path;
};
struct ModelPack {
    std::string id, version, pipeline_id, manifest_json;
    std::filesystem::path root;
    uint64_t capabilities = 0;
    int max_people = 0;
    std::vector<ModelAsset> models;
};
bool ValidComponentId(const std::string& id);
uint64_t CapabilityBit(const std::string& name);
class ModelPackManager {
public:
    explicit ModelPackManager(std::filesystem::path root) : root_(std::move(root)) {}
    std::shared_ptr<const ModelPack> Resolve(const std::string& id, std::string& error) const;
private:
    std::filesystem::path root_;
};
}
