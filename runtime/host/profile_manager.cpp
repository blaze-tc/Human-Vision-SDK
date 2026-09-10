#include "host/profile_manager.h"
#include "host/config_io.h"
#include <set>

namespace humanvision::runtime {
std::shared_ptr<const RuntimeProfile> ProfileManager::Resolve(const std::string& id, int max_people,
        const PluginRegistry& plugins, const ModelPackManager& packs, std::string& error) const {
    try {
        if (!ValidComponentId(id) || max_people < 1 || max_people > HV_MAX_PEOPLE) throw std::runtime_error("Invalid profile id or MaxBodies (1-8)");
        const auto json = ReadConfig(ConfinedPath(root_, id + ".json"));
        if (json.at("schema_version").get<int>() != 1 || json.at("profile").get<std::string>() != id)
            throw std::runtime_error("Unsupported profile schema or identity");
        auto result = std::make_shared<RuntimeProfile>();
        result->id = id; result->json = json.dump(); result->max_people = max_people;
        auto resolve = [&](const nlohmann::json& choice, uint64_t capability) {
            PipelineSelection selected;
            const auto plugin_id = choice.at("pipeline").get<std::string>();
            selected.plugin = plugins.Find(plugin_id, capability, error);
            if (!selected.plugin) throw std::runtime_error(error);
            selected.pack = packs.Resolve(choice.at("modelPack").get<std::string>(), error);
            if (!selected.pack) throw std::runtime_error(error);
            if (selected.plugin->api.type != HV_PLUGIN_PIPELINE || selected.pack->pipeline_id != plugin_id ||
                (selected.pack->capabilities & capability) != capability ||
                selected.plugin->api.max_people < static_cast<uint32_t>(max_people) || selected.pack->max_people < max_people)
                throw std::runtime_error("Incompatible pipeline, ModelPack or capacity: " + plugin_id);
            return selected;
        };
        if (json.contains("body_by_capacity")) {
            bool found = false; int previous = 0;
            for (const auto& choice : json.at("body_by_capacity")) {
                const int capacity = choice.at("max_people").get<int>();
                if (capacity <= previous || capacity > HV_MAX_PEOPLE) throw std::runtime_error("Profile capacities must increase within 1-8");
                previous = capacity;
                if (!found && max_people <= capacity) { result->body = resolve(choice, HV_CAP_BODY_POSE); found = true; }
            }
            if (!found) throw std::runtime_error("Profile has no compatible capacity selection");
        } else result->body = resolve(json.at("body"), HV_CAP_BODY_POSE);
        if (json.contains("hands")) {
            result->hands_enabled = json.at("hands").value("enabled", false);
            if (result->hands_enabled) result->hands = resolve(json.at("hands"), HV_CAP_HAND_POSE);
            result->hand_fps = json.at("hands").value("fps", 15);
        }
        result->body_fps = json.value("body_fps", 30);
        result->output_hz = json.value("output", nlohmann::json::object()).value("hz", 60);
        if (result->body_fps < 1 || result->body_fps > 120 || result->hand_fps < 1 || result->hand_fps > 120 || result->output_hz < 1 || result->output_hz > 240)
            throw std::runtime_error("Profile target rates are outside supported bounds");
        const auto preference = json.at("backend").at("preference");
        std::vector<std::string> ids;
        if (preference.is_string()) ids.push_back(preference.get<std::string>());
        else ids = preference.get<std::vector<std::string>>();
        std::set<std::string> seen;
        for (const auto& backend : ids) {
            if (backend == "auto") {
                for (auto& module : plugins.List(HV_CAP_TENSOR_INFERENCE))
                    if (module->api.type == HV_PLUGIN_BACKEND && seen.insert(module->api.plugin_id).second)
                        result->backends.push_back(module);
                continue;
            }
            auto module = plugins.Find(backend, HV_CAP_TENSOR_INFERENCE, error);
            if (module && module->api.type != HV_PLUGIN_BACKEND) {
                error = "Plugin is not a backend: " + backend;
                module.reset();
            }
            if (module && seen.insert(backend).second) result->backends.push_back(std::move(module));
            else if (!module) result->fallback_reason += error + "; ";
        }
        if (result->backends.empty()) throw std::runtime_error("No compatible backend: " + result->fallback_reason);
        error.clear(); return result;
    } catch (const std::exception& exception) { error = "Profile " + id + ": " + exception.what(); return {}; }
}
}
