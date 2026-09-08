#include "Resources/ShaderModule.h"

#include <cstdint>
#include <filesystem>
#include <fstream>

#include "Core/Logger.h"

bool tryReadFile(const char* path, std::vector<char>& out, std::string* outError)
{
    out.clear();
    if (outError != nullptr)
    {
        outError->clear();
    }

    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        if (outError != nullptr)
        {
            *outError = std::string("Failed to open file: ") + path;
        }
        return false;
    }

    const size_t size = static_cast<size_t>(file.tellg());
    out.resize(size);
    file.seekg(0);
    file.read(out.data(), static_cast<std::streamsize>(size));
    if (!file)
    {
        out.clear();
        if (outError != nullptr)
        {
            *outError = std::string("Failed to read file: ") + path;
        }
        return false;
    }

    return true;
}

std::vector<char> readFile(const char* path)
{
    std::vector<char> buffer;
    std::string error;
    if (!tryReadFile(path, buffer, &error))
    {
        logAndExit("Shader", error);
    }
    return buffer;
}

namespace
{
std::string g_shaderSearchRoot{};
}

void setShaderSearchRoot(const char* argv0)
{
    g_shaderSearchRoot = argv0 == nullptr ? "" : argv0;
}

std::string resolveShaderPath(const char* argv0, const char* fileName)
{
    namespace fs = std::filesystem;
    const char* root = argv0;
    if (root == nullptr || root[0] == '\0')
    {
        root = g_shaderSearchRoot.empty() ? nullptr : g_shaderSearchRoot.c_str();
    }

    fs::path exePath = fs::absolute(root == nullptr ? "" : root);
    fs::path exeDir = exePath.parent_path();
    fs::path fromExe = exeDir / "shaders" / fileName;
    if (fs::exists(fromExe))
    {
        return fromExe.string();
    }

    fs::path fromCwd = fs::current_path() / "shaders" / fileName;
    if (fs::exists(fromCwd))
    {
        return fromCwd.string();
    }

    return fromExe.string();
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        logAndExit("Shader", error.empty() ? "Failed to create shader module." : error);
    }
    return module;
}

VkShaderModule tryCreateShaderModule(VkDevice device, const std::vector<char>& code,
                                     std::string* outError)
{
    if (outError != nullptr)
    {
        outError->clear();
    }

    if (code.empty() || (code.size() % 4) != 0)
    {
        if (outError != nullptr)
        {
            *outError = "SPIR-V shader module size is invalid.";
        }
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS)
    {
        if (outError != nullptr)
        {
            *outError = "vkCreateShaderModule failed";
        }
        return VK_NULL_HANDLE;
    }
    return module;
}
