#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.h>

std::vector<char> readFile(const char* path);
bool tryReadFile(const char* path, std::vector<char>& out, std::string* outError = nullptr);
void setShaderSearchRoot(const char* argv0);
std::string resolveShaderPath(const char* argv0, const char* fileName);
VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code);
VkShaderModule tryCreateShaderModule(VkDevice device, const std::vector<char>& code,
                                     std::string* outError = nullptr);
