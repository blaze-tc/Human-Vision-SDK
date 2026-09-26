#pragma once
#include "host/model_pack_manager.h"
#include "host/plugin_registry.h"
#include "host/backend_factory.h"

namespace humanvision::runtime {
struct PipelineSelection {
    std::shared_ptr<const PluginModule> plugin;
    std::shared_ptr<const ModelPack> pack;
};
struct RuntimeProfile {
    std::string id, json, fallback_reason;
    PipelineSelection body, hands;
    std::shared_ptr<const GpuPluginModuleV3> gpu_body;
    std::shared_ptr<const ModelPack> gpu_pack;
    bool gpu_route = false;
    std::vector<std::shared_ptr<const PluginModule>> backends;
    bool hands_enabled = false;
    bool allow_backend_fallback = true;
    uint64_t required_capabilities = 0;
    std::vector<std::string> required_capability_names;
    int max_people = 0, body_fps = 30, hand_fps = 15, output_hz = 60;
};
class ProfileManager {
public:
    explicit ProfileManager(std::filesystem::path root) : root_(std::move(root)) {}
    std::shared_ptr<const RuntimeProfile> Resolve(const std::string& id, int max_people,
        const PluginRegistry&, const ModelPackManager&, std::string& error,
        const BackendFactory* gpu_plugins = nullptr) const;
private:
    std::filesystem::path root_;
};
}
