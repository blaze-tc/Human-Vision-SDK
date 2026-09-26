#include "host/profile_manager.h"
#include "common/config_io.h"
#include "picosha2/picosha2.h"
#include <array>
#include <fstream>
#include <set>

namespace humanvision::runtime {
namespace {
std::string ProfileHash(const std::filesystem::path& path){
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("Cannot read Android GPU profile for SHA-256");
    picosha2::hash256_one_by_one hash;std::array<char,65536> buffer{};
    while(stream){stream.read(buffer.data(),buffer.size());
        hash.process(buffer.begin(),buffer.begin()+stream.gcount());}
    if(!stream.eof())throw std::runtime_error("Cannot finish Android GPU profile SHA-256");
    hash.finish();return picosha2::get_hash_hex_string(hash);
}
}
std::shared_ptr<const RuntimeProfile> ProfileManager::Resolve(const std::string& id, int max_people,
        const PluginRegistry& plugins, const ModelPackManager& packs, std::string& error,
        const BackendFactory* gpu_plugins) const {
    try {
        if (!ValidComponentId(id) || max_people < 1 || max_people > HV_MAX_PEOPLE) throw std::runtime_error("Invalid profile id or MaxBodies (1-8)");
        const auto profile_path=ConfinedPath(root_, id + ".json");
        const auto json = ReadConfig(profile_path);
        if (json.at("schema_version").get<int>() != 1 || json.at("profile").get<std::string>() != id)
            throw std::runtime_error("Unsupported profile schema or identity");
        auto result = std::make_shared<RuntimeProfile>();
        result->id = id; result->json = json.dump(); result->max_people = max_people;
        if (id == "android-ncnn-vulkan") {
            const auto& detector = json.at("detector");
            if (!detector.is_object() || !detector.contains("cadence_interval_frames") ||
                !detector.contains("max_capture_gap_us") ||
                !detector.at("cadence_interval_frames").is_number_integer() ||
                !detector.at("max_capture_gap_us").is_number_integer())
                throw std::runtime_error("Android GPU detector cadence_interval_frames and max_capture_gap_us are required integers");
            result->detector_cadence_interval_frames = detector.at("cadence_interval_frames").get<int>();
            result->detector_max_capture_gap_us = detector.at("max_capture_gap_us").get<int64_t>();
            if (result->detector_cadence_interval_frames < 2 || result->detector_cadence_interval_frames > 6)
                throw std::runtime_error("detector.cadence_interval_frames must be 2-6");
            if (result->detector_max_capture_gap_us < 1 || result->detector_max_capture_gap_us > 200000)
                throw std::runtime_error("detector.max_capture_gap_us must be <=200000");
            // The GPU route is a separate V3 contract. Never resolve this profile
            // through a V1 CPU pipeline or a fallback backend.
            if (!gpu_plugins || json.at("hands").value("enabled", false) ||
                json.at("backend").value("allow_fallback", true))
                throw std::runtime_error("Android NCNN GPU route requires V3 registration, hands disabled and allow_fallback false");
            const auto backend_ids = json.at("backend").at("preference").get<std::vector<std::string>>();
            if (backend_ids.size() != 1 || backend_ids[0] != "backend.ncnn.vulkan")
                throw std::runtime_error("Android NCNN GPU route requires exactly backend.ncnn.vulkan");
            const auto pipeline_id = json.at("body").at("pipeline").get<std::string>();
            result->gpu_pack = packs.Resolve(json.at("body").at("modelPack").get<std::string>(), error);
            if (!result->gpu_pack) throw std::runtime_error(error);
            const auto pack_json=nlohmann::json::parse(result->gpu_pack->manifest_json);
            if(!pack_json.contains("profile_sha256")||!pack_json.at("profile_sha256").is_string()||
                pack_json.at("profile_sha256").get<std::string>()!=ProfileHash(profile_path))
                throw std::runtime_error("Android GPU ModelPack profile SHA-256 mismatch");
            if (pack_json.at("schema_version").get<int>() != 2 ||
                result->gpu_pack->pipeline_id != pipeline_id || result->gpu_pack->max_people < max_people)
                throw std::runtime_error("Android GPU route requires a matching schema-2 ModelPack and capacity");
            result->gpu_body = gpu_plugins->FindV3(pipeline_id, HV_CAP_BODY_POSE | HV_CAP_GPU_INPUT, error);
            if (!result->gpu_body || !result->gpu_body->api.gpu_pipeline)
                throw std::runtime_error("V3 GPU pipeline unavailable: " + pipeline_id + (error.empty() ? "" : ": " + error));
            auto backend = gpu_plugins->FindV3(backend_ids[0], HV_CAP_TENSOR_INFERENCE | HV_CAP_GPU_INPUT, error);
            if (!backend || !backend->api.gpu_backend) throw std::runtime_error("V3 GPU backend unavailable: " + error);
            if (result->gpu_body->api.v1.max_people < static_cast<uint32_t>(max_people))
                throw std::runtime_error("V3 GPU pipeline capacity is insufficient");
            const auto& requirements = json.at("required_capabilities");
            if (!requirements.is_array() || requirements.empty())
                throw std::runtime_error("Android GPU profile must declare required capabilities");
            for (const auto& value : requirements) {
                const auto name = value.get<std::string>();
                const auto bit = CapabilityBit(name);
                const auto pipeline_has = (result->gpu_body->api.v1.capabilities & bit) == bit;
                const auto pack_has = (result->gpu_pack->capabilities & bit) == bit;
                const auto backend_has = (backend->api.v1.capabilities & bit) == bit;
                const bool available = bit == HV_CAP_BODY_POSE || bit == HV_CAP_MULTI_PERSON
                    ? pipeline_has && pack_has : bit == HV_CAP_GPU_INPUT
                    ? pipeline_has && pack_has && backend_has : backend_has;
                if (!available) throw std::runtime_error("Missing required GPU capability: " + name);
            }
            result->gpu_route = true; result->allow_backend_fallback = false;
            result->body_fps = json.value("body_fps",30);
            result->output_hz = json.value("output",nlohmann::json::object()).value("hz",60);
            error.clear(); return result;
        }
        if (json.contains("required_capabilities")) {
            const auto& requirements = json.at("required_capabilities");
            if (!requirements.is_array() || requirements.empty())
                throw std::runtime_error("required_capabilities must be a nonempty array");
            std::set<std::string> seen_requirements;
            for (const auto& value : requirements) {
                const auto name = value.get<std::string>();
                if (!seen_requirements.insert(name).second)
                    throw std::runtime_error("Duplicate required capability: " + name);
                result->required_capabilities |= CapabilityBit(name);
                result->required_capability_names.push_back(name);
            }
        }
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
        result->allow_backend_fallback=json.at("backend").value("allow_fallback",true);
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
        if (!result->required_capability_names.empty() &&
            (result->allow_backend_fallback || ids.size() != 1 || ids[0] == "auto"))
            throw std::runtime_error("A strict profile requires allow_fallback false and exactly one explicit backend");
        if(!result->allow_backend_fallback&&result->backends.size()!=1)
            throw std::runtime_error("A forced backend profile must resolve exactly one backend");
        uint64_t backend_capabilities = 0;
        for (const auto& backend : result->backends) backend_capabilities |= backend->api.capabilities;
        const auto has = [](uint64_t capabilities, uint64_t bit) { return (capabilities & bit) == bit; };
        const auto requirement_available = [&](uint64_t bit) {
            if (bit == HV_CAP_BODY_POSE || bit == HV_CAP_MULTI_PERSON)
                return has(result->body.plugin->api.capabilities, bit) && has(result->body.pack->capabilities, bit);
            if (bit == HV_CAP_HAND_POSE)
                return result->hands_enabled && has(result->hands.plugin->api.capabilities, bit) && has(result->hands.pack->capabilities, bit);
            if (bit == HV_CAP_GPU_INPUT)
                return has(result->body.plugin->api.capabilities, bit) && has(result->body.pack->capabilities, bit) && has(backend_capabilities, bit);
            if (bit == HV_CAP_TENSOR_INFERENCE || bit == HV_CAP_VULKAN || bit == HV_CAP_FP16_STORAGE ||
                bit == HV_CAP_FP16_ARITHMETIC || bit == HV_CAP_ANDROID_HARDWARE_BUFFER || bit == HV_CAP_EXTERNAL_SYNC_FD)
                return has(backend_capabilities, bit);
            return has(result->body.plugin->api.capabilities | result->body.pack->capabilities | backend_capabilities, bit);
        };
        for (const auto& requirement : result->required_capability_names) {
            const auto bit = CapabilityBit(requirement);
            if (!requirement_available(bit)) throw std::runtime_error("Missing required capability: " + requirement);
        }
        error.clear(); return result;
    } catch (const std::exception& exception) { error = "Profile " + id + ": " + exception.what(); return {}; }
}
}
