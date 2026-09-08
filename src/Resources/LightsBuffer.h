#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

struct VulkanContext;

enum class LightShape : uint32_t
{
    Sphere = 0u,
    Rectangle = 1u,
    Disc = 2u,
    Capsule = 3u,
    Point = 4u,
};

struct AreaLightGpu
{
    float posRadius[4];
    float colorIntensity[4];
    float shapeParam0[4];
    float shapeParam1[4];
    uint32_t shapeInfo[4];
};

// temporary compatibility alias while migrating call-sites incrementally.
using PointLightGpu = AreaLightGpu;

struct LightsBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    uint32_t maxLights = 0;
};

void createLightsBuffer(VulkanContext& ctx, uint32_t maxLights, LightsBuffer& out);
void destroyLightsBuffer(VulkanContext& ctx, LightsBuffer& out);
