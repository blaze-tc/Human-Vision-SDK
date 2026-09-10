#pragma once
#include "humanvision_plugin.h"
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace humanvision::runtime {
struct PluginModule {
    HV_PluginApiV1 api{};
    void* library = nullptr;
    ~PluginModule();
    PluginModule() = default;
    PluginModule(const PluginModule&) = delete;
    PluginModule& operator=(const PluginModule&) = delete;
};

class PluginRegistry {
public:
    bool Register(HV_QueryPluginFn query, std::string& error);
    bool Load(const std::filesystem::path& path, std::string& error);
    std::shared_ptr<const PluginModule> Find(const std::string& id, uint64_t capabilities, std::string& error) const;
    std::vector<std::shared_ptr<const PluginModule>> List(uint64_t capabilities) const;
private:
    bool RegisterModule(HV_QueryPluginFn query, std::shared_ptr<PluginModule> module, std::string& error);
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<PluginModule>> modules_;
};
}
