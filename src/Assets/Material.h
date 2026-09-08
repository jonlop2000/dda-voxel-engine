#pragma once

#include <cstdint>

#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "Assets/Texture.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;

struct Material
{
    glm::vec4 baseColorFactor{1.0f};
    float roughness = 0.6f;
    float metallic = 0.0f;
    float ao = 1.0f;

    Texture2D* baseColorTex = nullptr;
    Texture2D* normalTex = nullptr;
    Texture2D* ormTex = nullptr;

    VkDescriptorSet set = VK_NULL_HANDLE;
};

struct MaterialPool
{
    uint32_t maxMaterials = 0;

    void create(VulkanContext& ctx, uint32_t maxMaterials);
    void destroy(VulkanContext& ctx);
    [[nodiscard]] VkDescriptorSetLayout layout() const { return layout_.get(); }
    VkDescriptorSet allocate(VulkanContext& ctx);
    void updateDescriptorSet(VulkanContext& ctx, VkDescriptorSet set, const Texture2D& baseColor,
                             const Texture2D& normal, const Texture2D& orm);

private:
    engine::render::UniqueDescriptorSetLayout layout_{};
    engine::render::UniqueDescriptorPool pool_{};
};
