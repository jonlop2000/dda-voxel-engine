#pragma once

#include <string>
#include <vector>

#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>

#include <vulkan/vulkan.h>

#include "engine/render/RendererConfig.h"
#include "engine/render/SceneAtmosphere.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;
struct FrameContext;
struct PassCreateInfo;
struct GBufferPass;
struct LightingPass;

// aaa-quality voxel glass refraction pass.
// applies snell's law refraction, beer-Lambert absorption, and fresnel reflection
// to voxel glass pixels detected via g-buffer data.
struct VoxelGlassRefractPass
{
    struct Target
    {
        // hdr output per frame (same format as lighting).
        VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    struct FrameUbo
    {
        // world position reconstruction.
        glm::mat4 invViewProj{1.0f};
        glm::vec4 camPos{0.0f, 0.0f, 0.0f, 1.0f};
        // glass tint color (rgb, a unused).
        glm::vec4 tint{0.85f, 0.95f, 0.98f, 1.0f};
        // x=absorption, y=refractStrength, z=ior, w=debugMode
        glm::vec4 params0{0.6f, 0.02f, 1.5f, 0.0f};
        // x=invResX, y=invResY, z=maxThickness, w=reflectionStrength
        glm::vec4 params1{1.0f, 1.0f, 16.0f, 0.5f};
        // Sky/environment reflection color.
        glm::vec4 reflectionColor{0.09f, 0.29f, 0.88f, 1.0f};
        // shared scene-atmosphere parameters.
        glm::vec4 atmosphere0{0.0f};
        // x=active water-volume count, yzw=reserved.
        glm::vec4 atmosphere1{0.0f};
    };

    struct FrameUboInputs
    {
        glm::mat4 invViewProj{1.0f};
        glm::vec3 cameraPosition{0.0f};
        glm::vec3 tint{0.85f, 0.95f, 0.98f};
        float absorption = 0.6f;
        float refractStrength = 0.02f;
        float ior = 1.5f;
        int debugMode = 0;
        VkExtent2D renderExtent{};
        float maxThickness = 16.0f;
        float reflectionStrength = 0.5f;
        glm::vec3 reflectionColor{0.09f, 0.29f, 0.88f};
        engine::render::SceneAtmosphereSettings sceneAtmosphere{};
        uint32_t activeWaterVolumeCount = 0;
    };

    [[nodiscard]] static FrameUbo buildFrameUbo(const FrameUboInputs& in)
    {
        FrameUbo ubo{};
        ubo.invViewProj = in.invViewProj;
        ubo.camPos = glm::vec4(in.cameraPosition, 1.0f);
        ubo.tint = glm::vec4(in.tint, 1.0f);
        ubo.params0 = glm::vec4(in.absorption, in.refractStrength, in.ior,
                                static_cast<float>(in.debugMode));
        ubo.params1 = glm::vec4(
            in.renderExtent.width > 0 ? 1.0f / static_cast<float>(in.renderExtent.width)
                                      : 1.0f,
            in.renderExtent.height > 0 ? 1.0f / static_cast<float>(in.renderExtent.height)
                                       : 1.0f,
            in.maxThickness, in.reflectionStrength);
        ubo.reflectionColor = glm::vec4(in.reflectionColor, 1.0f);
        ubo.atmosphere0 =
            engine::render::packSceneAtmosphereParameters(in.sceneAtmosphere);
        ubo.atmosphere1 =
            glm::vec4(static_cast<float>(in.activeWaterVolumeCount), 0.0f, 0.0f, 0.0f);
        return ubo;
    }

    void setShaderPaths(const std::string& fullscreenVert, const std::string& fragPath);
    void setWaterVolumeBuffer(VkBuffer buffer);

    void create(VulkanContext& ctx, const PassCreateInfo& ci,
                const GBufferPass& gbuffer, const LightingPass& lighting);
    void destroy(VulkanContext& ctx);
    void onResize(VulkanContext& ctx, const PassCreateInfo& ci,
                  const GBufferPass& gbuffer, const LightingPass& lighting);
    void record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& ubo);

    // update descriptor bindings after lighting pass resize.
    void updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                              const LightingPass& lighting);
    void updateWaterDescriptor(VulkanContext& ctx);

    const Target& target(uint32_t frameIndex) const;
    VkRenderPass renderPass() const;
    VkExtent2D extent() const;

private:
    struct PerFrameUbo
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    void createPipeline(VulkanContext& ctx);
    void destroyPipeline(VulkanContext& ctx);
    void createTargets(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroyTargets(VulkanContext& ctx);
    void updateUbo(uint32_t frameIndex, const FrameUbo& ubo);

    std::vector<Target> targets_;
    VkExtent2D extent_{};

    // set 0: g-buffer textures + lit scene; set 1: ubo.
    VkDescriptorSetLayout texSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout uboSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout waterSetLayout_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout pipelineLayout_{};
    engine::render::UniquePipeline pipeline_{};
    std::vector<VkDescriptorSet> texSets_;
    std::vector<VkDescriptorSet> uboSets_;
    VkDescriptorSet waterSet_ = VK_NULL_HANDLE;
    VkBuffer waterVolumeBuffer_ = VK_NULL_HANDLE;
    std::vector<PerFrameUbo> ubos_;

    std::string fullscreenVertPath_;
    std::string fragPath_;

    const GBufferPass* gbuffer_ = nullptr;
    const LightingPass* lighting_ = nullptr;
};
