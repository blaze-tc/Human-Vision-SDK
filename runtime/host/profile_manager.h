#pragma once
#include "host/model_pack_manager.h"
#include "host/plugin_registry.h"

namespace humanvision::runtime {
struct PipelineSelection {
    std::shared_ptr<const PluginModule> plugin;
    std::shared_ptr<const ModelPack> pack;
};
struct RuntimeProfile {
    std::string id, json, fallback_reason;
    PipelineSelection body, hands;
    std::vector<std::shared_ptr<const PluginModule>> backends;
    bool hands_enabled = false;
    int max_people = 0, body_fps = 30, hand_fps = 15, output_hz = 60;
};
class ProfileManager {
public:
    explicit ProfileManager(std::filesystem::path root) : root_(std::move(root)) {}
    std::shared_ptr<const RuntimeProfile> Resolve(const std::string& id, int max_people,
        const PluginRegistry&, const ModelPackManager&, std::string& error) const;
private:
    std::filesystem::path root_;
};
}
