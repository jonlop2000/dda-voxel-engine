#pragma once

#include <cstdlib>
#include <string>

#include "Core/Logger.h"
#include <vulkan/vulkan.h>

inline void die(const char* msg)
{
    logAndExit("Runtime", msg);
}

inline void vkCheck(VkResult result, const char* expr, const char* file, int line)
{
    if (result != VK_SUCCESS)
    {
        const std::string message = std::string(expr) + " failed with VkResult " +
                                    std::to_string(static_cast<int>(result)) + " (" + file +
                                    ":" + std::to_string(line) + ")";
        logAndExit("Vulkan", message);
    }
}

#define VK_CHECK(expr) vkCheck((expr), #expr, __FILE__, __LINE__)
