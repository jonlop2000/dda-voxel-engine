#pragma once

#include <vulkan/vulkan.h>

struct Commands;

struct PassCreateInfo
{
    VkExtent2D extent{};
    Commands* commands = nullptr;
};
