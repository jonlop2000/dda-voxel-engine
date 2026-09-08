#include "Core/RuntimeContentManifest.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{

constexpr const char* kRuntimeContentManifestFileName = "runtime_content_manifest.json";

std::string lowercaseCopy(const std::string& text)
{
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

bool hasFileWithExtension(const std::filesystem::path& directory, const std::string& extension)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec))
    {
        return false;
    }

    const std::string wantedExtension = lowercaseCopy(extension);
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec))
    {
        if (ec)
        {
            return false;
        }
        if (!entry.is_regular_file(ec))
        {
            continue;
        }

        if (lowercaseCopy(entry.path().extension().string()) == wantedExtension)
        {
            return true;
        }
    }

    return false;
}

std::filesystem::path resolveCompiledShaderPath(const char* argv0, const std::string& outputFile)
{
    namespace fs = std::filesystem;

    fs::path exePath = fs::absolute(argv0 == nullptr ? "" : argv0);
    fs::path exeDir = exePath.parent_path();
    fs::path fromExe = exeDir / "shaders" / outputFile;
    if (fs::exists(fromExe))
    {
        return fromExe;
    }

    fs::path fromCwd = fs::current_path() / "shaders" / outputFile;
    if (fs::exists(fromCwd))
    {
        return fromCwd;
    }

    return fromExe;
}

bool readManifestString(const json& j, const char* key, std::string& outValue, std::string& outError)
{
    if (!j.contains(key) || !j[key].is_string())
    {
        outError = std::string("Manifest field '") + key + "' must be a string";
        return false;
    }

    outValue = j[key].get<std::string>();
    return true;
}

} // namespace

std::filesystem::path resolveRuntimeContentManifestPath(const char* argv0)
{
    namespace fs = std::filesystem;

    fs::path exePath = fs::absolute(argv0 == nullptr ? "" : argv0);
    fs::path exeDir = exePath.parent_path();
    fs::path fromExe = exeDir / "shaders" / kRuntimeContentManifestFileName;
    if (fs::exists(fromExe))
    {
        return fromExe;
    }

    fs::path fromCwd = fs::current_path() / "shaders" / kRuntimeContentManifestFileName;
    if (fs::exists(fromCwd))
    {
        return fromCwd;
    }

    return fromExe;
}

bool loadRuntimeContentManifest(const std::filesystem::path& path, RuntimeContentManifest& outManifest,
                                std::string* outError)
{
    outManifest = RuntimeContentManifest{};
    if (outError != nullptr)
    {
        outError->clear();
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        if (outError != nullptr)
        {
            *outError = "Failed to open runtime content manifest";
        }
        return false;
    }

    try
    {
        const json root = json::parse(file);
        if (!root.contains("shader_includes") || !root["shader_includes"].is_array())
        {
            if (outError != nullptr)
            {
                *outError = "Manifest field 'shader_includes' must be an array";
            }
            return false;
        }
        if (!root.contains("shaders") || !root["shaders"].is_array())
        {
            if (outError != nullptr)
            {
                *outError = "Manifest field 'shaders' must be an array";
            }
            return false;
        }
        if (!root.contains("asset_checks") || !root["asset_checks"].is_array())
        {
            if (outError != nullptr)
            {
                *outError = "Manifest field 'asset_checks' must be an array";
            }
            return false;
        }

        std::string localError;
        for (const json& includeEntry : root["shader_includes"])
        {
            if (!includeEntry.is_string())
            {
                if (outError != nullptr)
                {
                    *outError = "Manifest shader include entries must be strings";
                }
                return false;
            }
            outManifest.shaderIncludes.push_back(includeEntry.get<std::string>());
        }

        for (const json& shaderEntry : root["shaders"])
        {
            if (!shaderEntry.is_object())
            {
                if (outError != nullptr)
                {
                    *outError = "Manifest shader entries must be objects";
                }
                return false;
            }

            RuntimeShaderArtifact shader;
            std::string sourcePath;
            if (!readManifestString(shaderEntry, "source", sourcePath, localError))
            {
                if (outError != nullptr)
                {
                    *outError = localError;
                }
                return false;
            }
            if (!readManifestString(shaderEntry, "output", shader.outputFile, localError))
            {
                if (outError != nullptr)
                {
                    *outError = localError;
                }
                return false;
            }
            if (shaderEntry.contains("required"))
            {
                if (!shaderEntry["required"].is_boolean())
                {
                    if (outError != nullptr)
                    {
                        *outError = "Manifest shader field 'required' must be a boolean";
                    }
                    return false;
                }
                shader.required = shaderEntry["required"].get<bool>();
            }

            shader.sourcePath = sourcePath;
            outManifest.shaders.push_back(std::move(shader));
        }

        for (const json& assetEntry : root["asset_checks"])
        {
            if (!assetEntry.is_object())
            {
                if (outError != nullptr)
                {
                    *outError = "Manifest asset check entries must be objects";
                }
                return false;
            }

            RuntimeAssetCheck assetCheck;
            std::string typeValue;
            if (!readManifestString(assetEntry, "type", typeValue, localError))
            {
                if (outError != nullptr)
                {
                    *outError = localError;
                }
                return false;
            }
            if (!assetEntry.contains("required") || !assetEntry["required"].is_boolean())
            {
                if (outError != nullptr)
                {
                    *outError = "Manifest asset check field 'required' must be a boolean";
                }
                return false;
            }
            assetCheck.required = assetEntry["required"].get<bool>();
            if (assetEntry.contains("description"))
            {
                if (!assetEntry["description"].is_string())
                {
                    if (outError != nullptr)
                    {
                        *outError = "Manifest asset check field 'description' must be a string";
                    }
                    return false;
                }
                assetCheck.description = assetEntry["description"].get<std::string>();
            }

            if (typeValue == "file")
            {
                assetCheck.type = RuntimeAssetCheckType::File;
                std::string pathValue;
                if (!readManifestString(assetEntry, "path", pathValue, localError))
                {
                    if (outError != nullptr)
                    {
                        *outError = localError;
                    }
                    return false;
                }
                assetCheck.path = pathValue;
            }
            else if (typeValue == "extension_search")
            {
                assetCheck.type = RuntimeAssetCheckType::ExtensionSearch;
                std::string directoryValue;
                if (!readManifestString(assetEntry, "directory", directoryValue, localError))
                {
                    if (outError != nullptr)
                    {
                        *outError = localError;
                    }
                    return false;
                }
                if (!readManifestString(assetEntry, "extension", assetCheck.extension, localError))
                {
                    if (outError != nullptr)
                    {
                        *outError = localError;
                    }
                    return false;
                }
                assetCheck.directory = directoryValue;
            }
            else
            {
                if (outError != nullptr)
                {
                    *outError = "Manifest asset check field 'type' has unsupported value";
                }
                return false;
            }

            outManifest.assetChecks.push_back(std::move(assetCheck));
        }

        return true;
    }
    catch (const json::exception& e)
    {
        if (outError != nullptr)
        {
            *outError = e.what();
        }
        return false;
    }
}

RuntimeContentValidationReport validateRuntimeContentManifest(
    const RuntimeContentManifest& manifest, const std::filesystem::path& assetRoot,
    const char* shaderSearchRootArgv0)
{
    namespace fs = std::filesystem;

    RuntimeContentValidationReport report;

    for (const RuntimeShaderArtifact& shader : manifest.shaders)
    {
        const fs::path shaderPath =
            resolveCompiledShaderPath(shaderSearchRootArgv0, shader.outputFile);
        if (!fs::exists(shaderPath))
        {
            const std::string message =
                (shader.required ? "Missing compiled shader '" : "Missing optional compiled shader '") +
                shader.outputFile + "' at " + shaderPath.string();
            if (shader.required)
            {
                report.errors.push_back(message);
            }
            else
            {
                report.warnings.push_back(message);
            }
        }
    }

    for (const RuntimeAssetCheck& assetCheck : manifest.assetChecks)
    {
        bool found = false;
        std::string targetDescription;
        if (assetCheck.type == RuntimeAssetCheckType::File)
        {
            const fs::path candidate = assetRoot / assetCheck.path;
            found = fs::exists(candidate);
            targetDescription = candidate.string();
        }
        else
        {
            const fs::path directory = assetRoot / assetCheck.directory;
            found = hasFileWithExtension(directory, assetCheck.extension);
            targetDescription = (directory / ("*" + assetCheck.extension)).string();
        }

        if (found)
        {
            continue;
        }

        std::string message = assetCheck.description.empty() ? targetDescription
                                                              : assetCheck.description + ": " + targetDescription;
        if (assetCheck.required)
        {
            report.errors.push_back("Missing required runtime content: " + message);
        }
        else
        {
            report.warnings.push_back("Missing optional runtime content: " + message);
        }
    }

    return report;
}
