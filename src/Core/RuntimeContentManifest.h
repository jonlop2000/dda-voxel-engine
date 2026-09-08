#pragma once

#include <filesystem>
#include <string>
#include <vector>

enum class RuntimeAssetCheckType
{
    File,
    ExtensionSearch,
};

struct RuntimeShaderArtifact
{
    std::filesystem::path sourcePath;
    std::string outputFile;
    bool required = true;
};

struct RuntimeAssetCheck
{
    RuntimeAssetCheckType type = RuntimeAssetCheckType::File;
    std::filesystem::path path;
    std::filesystem::path directory;
    std::string extension;
    bool required = false;
    std::string description;
};

struct RuntimeContentManifest
{
    std::vector<std::filesystem::path> shaderIncludes;
    std::vector<RuntimeShaderArtifact> shaders;
    std::vector<RuntimeAssetCheck> assetChecks;
};

struct RuntimeContentValidationReport
{
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    bool ok() const { return errors.empty(); }
};

std::filesystem::path resolveRuntimeContentManifestPath(const char* argv0);

bool loadRuntimeContentManifest(const std::filesystem::path& path, RuntimeContentManifest& outManifest,
                                std::string* outError = nullptr);

RuntimeContentValidationReport validateRuntimeContentManifest(
    const RuntimeContentManifest& manifest, const std::filesystem::path& assetRoot,
    const char* shaderSearchRootArgv0);
