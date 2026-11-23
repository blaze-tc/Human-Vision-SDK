#include "host/plugin_registry.h"
#include <exception>
#include <algorithm>
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

namespace {
bool QueryImageExtension(HV_QueryPipelineImageExtensionV1Fn query,const char* id,
                        HV_PipelineImageExtensionV1& api,std::string& error) {
 if(!query){error="Pipeline image extension query is missing";return false;}
 api={};api.struct_size=sizeof(api);api.api_version=1;
 try {if(query(1,&api)!=HV_OK){error="Pipeline image extension rejected its version";return false;}}
 catch(...){error="Pipeline image extension query threw across C ABI";return false;}
 if(api.struct_size<sizeof(api)||api.api_version!=1||!api.plugin_id||std::string(api.plugin_id)!=id||
    !api.DescribeTransform||!api.ProcessPrepared){error="Pipeline image extension identity, size or callbacks are invalid";return false;}
 return true;
}
}
bool PluginRegistry::RegisterImageExtension(const std::string& id,HV_QueryPipelineImageExtensionV1Fn query,std::string& error) {
 HV_PipelineImageExtensionV1 extension{};if(!QueryImageExtension(query,id.c_str(),extension,error))return false;
 std::lock_guard<std::mutex> lock(mutex_);const auto entry=modules_.find(id);
 if(entry==modules_.end()||entry->second->api.type!=HV_PLUGIN_PIPELINE){error="Image extension requires a registered pipeline: "+id;return false;}
 if(entry->second.use_count()!=1||entry->second->has_image_extension){error="Image extension registration must precede immutable plugin use";return false;}
 entry->second->image_extension=extension;entry->second->has_image_extension=true;error.clear();return true;
}

bool PluginRegistry::RegisterModule(HV_QueryPluginFn query, std::shared_ptr<PluginModule> module, std::string& error,
                                   HV_QueryPipelineImageExtensionV1Fn image_query) {
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
    if(image_query){
        if(api.type!=HV_PLUGIN_PIPELINE||!QueryImageExtension(image_query,api.plugin_id,module->image_extension,error))return false;
        module->has_image_extension=true;
    }
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
    auto image_query=reinterpret_cast<HV_QueryPipelineImageExtensionV1Fn>(GetProcAddress(static_cast<HMODULE>(module->library),"HV_QueryPipelineImageExtensionV1"));
    return RegisterModule(query,std::move(module),error,image_query);
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

std::vector<std::shared_ptr<const PluginModule>> PluginRegistry::List(uint64_t capabilities) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<const PluginModule>> result;
    for (const auto& entry : modules_) if ((entry.second->api.capabilities & capabilities) == capabilities) result.push_back(entry.second);
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a->api.priority > b->api.priority; });
    return result;
}
}
