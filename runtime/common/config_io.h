#pragma once
#include "json/json.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace humanvision::runtime {
inline nlohmann::json ReadConfig(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 2 * 1024 * 1024)
        throw std::runtime_error("Missing or oversized configuration: " + path.u8string());
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open configuration: " + path.u8string());
    return nlohmann::json::parse(input);
}
inline std::filesystem::path ConfinedPath(const std::filesystem::path& root, const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name()) throw std::runtime_error("Asset path must be relative");
    for (const auto& part : relative) if (part == "..") throw std::runtime_error("Path traversal is forbidden");
    const auto base = std::filesystem::canonical(root);
    const auto resolved = std::filesystem::canonical(base / relative);
    const auto suffix = resolved.lexically_relative(base);
    if (suffix.empty() || suffix.is_absolute() || *suffix.begin() == "..")
        throw std::runtime_error("Resolved path escapes its root");
    return resolved;
}
}
