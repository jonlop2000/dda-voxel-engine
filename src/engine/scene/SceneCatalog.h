#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct SceneCatalogEntry
{
    std::filesystem::path path;
    std::string fileName;
    std::string displayName;
    std::string description;
    bool valid = false;
    std::string errorMessage;
};

std::vector<SceneCatalogEntry> scanSceneCatalog(const std::filesystem::path& scenesDir);
