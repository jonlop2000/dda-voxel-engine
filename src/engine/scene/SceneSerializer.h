#pragma once

#include <filesystem>
#include <string>

struct SceneConfig;

enum class SceneSerializerLogMode
{
    Default,
    Quiet,
};

struct SceneLoadDiagnostics
{
    bool loadedLegacyVersion = false;
    std::string errorMessage;
};

bool loadSceneConfigFromFile(const std::filesystem::path& path, SceneConfig& outConfig,
                             SceneLoadDiagnostics* diagnostics = nullptr,
                             SceneSerializerLogMode logMode = SceneSerializerLogMode::Default);

bool saveSceneConfigToFile(const std::filesystem::path& path, const SceneConfig& config,
                           std::string* outError = nullptr,
                           SceneSerializerLogMode logMode = SceneSerializerLogMode::Default);
