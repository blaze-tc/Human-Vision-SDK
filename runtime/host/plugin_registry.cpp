#include "host/plugin_registry.h"
#include <exception>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace humanvision::runtime {
PluginModule::~PluginModule() {
#if defined(_WIN32)
    if (library) FreeLibrary(static_cast<HMODULE>(library));
#endif
}

bool PluginRegistry::Register(HV_QueryPluginFn query, std::string& error) {
    return RegisterModule(query, std::make_shared<PluginModule>(), error);
}

bool PluginRegistry::RegisterModule(HV_QueryPluginFn query, std::shared_ptr<PluginModule> module, std::string& error) {
    error.clear();
    if (!query) { error = "Plugin query callback is missing"; return false; }
    auto& api = module->api;
    api.struct_size = sizeof(api); api.api_version = HV_PLUGIN_API_V1;
    try {
        if (query(HV_PLUGIN_API_V1, &api) != HV_OK) { error = "Plugin rejected API version"; return false; }
    } catch (...) { error = "Plugin query raised an exception across C ABI"; return false; }
    if (api.struct_size < sizeof(api) || api.api_version != HV_PLUGIN_API_V1 ||
        !api.plugin_id || !*api.plugin_id || !api.plugin_version || !*api.plugin_version) {
        error = "Plugin metadata size, version or identity is invalid"; return false;
    }
    if (api.type == HV_PLUGIN_PIPELINE) {
        const auto* p = api.pipeline;
        if (!p || p->struct_size < sizeof(*p) || p->api_version != HV_PLUGIN_API_V1 ||
            !p->create || !p->destroy || !p->process || api.max_people < 1 || api.max_people > HV_MAX_PEOPLE ||
            !(api.capabilities & (HV_CAP_BODY_POSE | HV_CAP_HAND_POSE))) {
            error = "Pipeline plugin has invalid callbacks or capabilities"; return false;
        }
    } else if (api.type == HV_PLUGIN_BACKEND) {
        const auto* p = api.backend;
        if (!p || p->struct_size < sizeof(*p) || p->api_version != HV_PLUGIN_API_V1 ||
            !p->create || !p->destroy || !p->run || !p->session_info || !(api.capabilities & HV_CAP_TENSOR_INFERENCE)) {
            error = "Backend plugin has invalid callbacks or capabilities"; return false;
        }
    } else { error = "Unknown plugin type"; return false; }
    std::lock_guard<std::mutex> lock(mutex_);
    if (modules_.count(api.plugin_id)) { error = "Duplicate plugin id: " + std::string(api.plugin_id); return false; }
    modules_.emplace(api.plugin_id, std::move(module));
    return true;
}

bool PluginRegistry::Load(const std::filesystem::path& path, std::string& error) {
#if defined(_WIN32)
    if (!path.is_absolute()) { error = "Plugin library path must be absolute"; return false; }
    auto module = std::make_shared<PluginModule>();
    module->library = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module->library) { error = "Cannot load plugin library: " + std::to_string(GetLastError()); return false; }
    auto query = reinterpret_cast<HV_QueryPluginFn>(GetProcAddress(static_cast<HMODULE>(module->library), "HV_QueryPlugin"));
    return RegisterModule(query, std::move(module), error);
#else
    (void)path; error = "Dynamic plugins are unavailable; use static registration on this platform"; return false;
#endif
}

std::shared_ptr<const PluginModule> PluginRegistry::Find(const std::string& id, uint64_t capabilities, std::string& error) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = modules_.find(id);
    if (found == modules_.end()) { error = "Plugin is not registered: " + id; return {}; }
    if ((found->second->api.capabilities & capabilities) != capabilities) {
        error = "Plugin lacks requested capabilities: " + id; return {};
    }
    error.clear(); return found->second;
}
}
