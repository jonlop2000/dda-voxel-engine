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
#include "engine/render/RenderObject.h"
#include "engine/render/SceneAtmosphere.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;
struct FrameContext;
struct PassCreateInfo;
struct GBufferPass;
struct LightingPass;
struct WaterPass;

struct GlassPass
{
    struct BackDepthTarget
    {
        VkFormat colorFormat = VK_FORMAT_R32_SFLOAT;
        VkImage colorImage = VK_NULL_HANDLE;
        VkDeviceMemory colorMemory = VK_NULL_HANDLE;
        VkImageView colorView = VK_NULL_HANDLE;
        VkSampler colorSampler = VK_NULL_HANDLE;

        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthView = VK_NULL_HANDLE;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    struct ShadeTarget
    {
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    struct FrameUbo
    {
        glm::mat4 view{1.0f};
        glm::mat4 viewProj{1.0f};
        glm::vec4 camPos{0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec4 tint{0.8f, 0.95f, 0.9f, 1.0f};
        // x=absorption, y=refractStrength, z=ior, w=debugMode
        glm::vec4 params0{0.6f, 0.01f, 1.52f, 0.0f};
        // x=invResX, y=invResY, z=thicknessDebugScale, w=refractDebugScale
        glm::vec4 params1{1.0f, 1.0f, 0.5f, 0.02f};
        // x=thicknessScale, y=iridescentStrength, z=iridescentFilmThickness, w=iridescentFrequency
        glm::vec4 params2{1.0f, 0.0f, 1.5f, 2.0f};
        // bubble parameters: x=scale, y=intensity, z=thicknessGate, w=chromaticSplit
        glm::vec4 params3{12.0f, 0.0f, 0.1f, 0.0f};
        glm::vec4 reflectionColor{0.09f, 0.29f, 0.88f, 1.0f};
        // waterline ripple distortion (sample water surface waves on side glass).
        // x=waterLevel, y=time, z=waveScale, w=waveAmplitude
        glm::vec4 waterWave0{0.0f, 0.0f, 0.0f, 0.0f};
        // x=rippleStrength, y=waterlineFalloff, z=verticalGateExp, w=enabled
        glm::vec4 waterWave1{0.011f, 2.1f, 1.35f, 0.0f};
        // active water volume metadata for shape-aware glass/water contact.
        // xyz=boundsMin, w=surfaceHeight
        glm::vec4 waterVolume0{0.0f, 0.0f, 0.0f, 0.0f};
        // xyz=boundsMax, w=shape enum
        glm::vec4 waterVolume1{0.0f, 0.0f, 0.0f, 0.0f};
        // xyz=direction from glass toward sun, w=unused
        glm::vec4 sunDirToSun{0.0f, 1.0f, 0.0f, 0.0f};
        // rgb=sun color multiplied by intensity, w=unused
        glm::vec4 sunColor{1.0f, 0.95f, 0.85f, 1.0f};
        // shared scene-atmosphere parameters: density, height falloff, base height,
        // and sun phase strength. glass uses the first three for surface-delta attenuation.
        glm::vec4 atmosphere0{0.0f};
        // x=active water-volume count, yzw=reserved.
        glm::vec4 atmosphere1{0.0f};
    };

    struct WaterVolumeMetadata
    {
        bool active = false;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        float surfaceHeight = 0.0f;
        float shape = 0.0f;
    };

    struct FrameUboInputs
    {
        glm::mat4 view{1.0f};
        // mesh glass does not contribute depth, velocity, or a reactive mask to the
        // g-buffer. anchor its coverage to the stable pixel grid so taa projection
        // jitter cannot make a static transparent silhouette crawl.
        glm::mat4 viewProjUnjittered{1.0f};
        glm::vec3 cameraPosition{0.0f};
        glm::vec3 tint{0.8f, 0.95f, 0.9f};
        float absorption = 0.6f;
        float refractStrength = 0.01f;
        float ior = 1.52f;
        int debugMode = 0;
        VkExtent2D renderExtent{};
        float thicknessDebugScale = 0.5f;
        float refractDebugScale = 0.02f;
        float thicknessScale = 1.0f;
        float iridescentStrength = 0.0f;
        float iridescentFilmThickness = 1.5f;
        float iridescentFrequency = 2.0f;
        float bubbleScale = 12.0f;
        float bubbleIntensity = 0.0f;
        float bubbleThicknessGate = 0.1f;
        float bubbleChromaticSplit = 0.0f;
        glm::vec3 reflectionColor{0.09f, 0.29f, 0.88f};
        float waterLevel = 0.0f;
        float animationTime = 0.0f;
        float waterWaveScale = 0.0f;
        float waterWaveAmplitude = 0.0f;
        bool waterlineRippleEnabled = false;
        WaterVolumeMetadata waterVolume{};
        glm::vec3 sunDirectionToSun{0.0f, 1.0f, 0.0f};
        glm::vec3 sunColor{1.0f, 0.95f, 0.85f};
        engine::render::SceneAtmosphereSettings sceneAtmosphere{};
        uint32_t activeWaterVolumeCount = 0;
    };

    [[nodiscard]] static FrameUbo buildFrameUbo(const FrameUboInputs& in)
    {
        FrameUbo ubo{};
        ubo.view = in.view;
        ubo.viewProj = in.viewProjUnjittered;
        ubo.camPos = glm::vec4(in.cameraPosition, 1.0f);
        ubo.tint = glm::vec4(in.tint, 1.0f);
        ubo.params0 = glm::vec4(in.absorption, in.refractStrength, in.ior,
                                static_cast<float>(in.debugMode));
        ubo.params1 =
            glm::vec4(in.renderExtent.width > 0
                          ? 1.0f / static_cast<float>(in.renderExtent.width)
                          : 1.0f,
                      in.renderExtent.height > 0
                          ? 1.0f / static_cast<float>(in.renderExtent.height)
                          : 1.0f,
                      in.thicknessDebugScale, in.refractDebugScale);
        ubo.params2 = glm::vec4(in.thicknessScale, in.iridescentStrength,
                                in.iridescentFilmThickness, in.iridescentFrequency);
        ubo.params3 = glm::vec4(in.bubbleScale, in.bubbleIntensity,
                                in.bubbleThicknessGate, in.bubbleChromaticSplit);
        ubo.reflectionColor = glm::vec4(in.reflectionColor, 1.0f);
        ubo.waterWave0 = glm::vec4(in.waterLevel, in.animationTime, in.waterWaveScale,
                                   in.waterWaveAmplitude);
        ubo.waterWave1 = glm::vec4(0.011f, 2.1f, 1.35f,
                                   in.waterlineRippleEnabled ? 1.0f : 0.0f);
        if (in.waterlineRippleEnabled && in.waterVolume.active)
        {
            ubo.waterVolume0 = glm::vec4(in.waterVolume.boundsMin,
                                         in.waterVolume.surfaceHeight);
            ubo.waterVolume1 = glm::vec4(in.waterVolume.boundsMax, in.waterVolume.shape);
        }
        ubo.sunDirToSun = glm::vec4(in.sunDirectionToSun, 0.0f);
        ubo.sunColor = glm::vec4(in.sunColor, 1.0f);
        ubo.atmosphere0 =
            engine::render::packSceneAtmosphereParameters(in.sceneAtmosphere);
        ubo.atmosphere1 =
            glm::vec4(static_cast<float>(in.activeWaterVolumeCount), 0.0f, 0.0f, 0.0f);
        return ubo;
    }

    void setShaderPaths(const std::string& backDepthVert, const std::string& backDepthFrag,
                        const std::string& fullscreenVert, const std::string& copyFrag,
                        const std::string& shadeVert, const std::string& shadeFrag);
    void setWaterVolumeBuffer(VkBuffer buffer);

    void create(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                const LightingPass& lighting, const WaterPass& water);
    void destroy(VulkanContext& ctx);
    void onResize(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                  const LightingPass& lighting, const WaterPass& water);
    void updateSceneColorSource(VulkanContext& ctx, uint32_t frameIndex, VkImageView sceneColorView,
                                VkSampler sceneColorSampler);

    void recordBackDepth(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo,
                         const std::vector<RenderObject>& objects);
    void recordShade(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo,
                     const std::vector<RenderObject>& objects, bool drawGlass);

    const BackDepthTarget& backDepth(uint32_t frameIndex) const;
    VkExtent2D extent() const;

private:
    struct PerFrameUbo
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    void createBackDepthPipeline(VulkanContext& ctx);
    void destroyBackDepthPipeline(VulkanContext& ctx);
    void createShadePipeline(VulkanContext& ctx);
    void destroyShadePipeline(VulkanContext& ctx);
    void createBackDepthTargets(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroyBackDepthTargets(VulkanContext& ctx);
    void createShadeTargets(VulkanContext& ctx, const GBufferPass& gbuffer,
                            const LightingPass& lighting);
    void destroyShadeTargets(VulkanContext& ctx);
    void updateBackUbo(uint32_t frameIndex, const FrameUbo& frameUbo);
    void updateShadeUbo(uint32_t frameIndex, const FrameUbo& frameUbo);
    void updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                              const WaterPass& water);
    void updateWaterDescriptor(VulkanContext& ctx);

    std::vector<BackDepthTarget> backDepthTargets_{};
    std::vector<ShadeTarget> shadeTargets_{};
    VkExtent2D extent_{};

    VkDescriptorSetLayout backUboLayout_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout backPipelineLayout_{};
    engine::render::UniquePipeline backPipeline_{};
    std::vector<VkDescriptorSet> backUboSets_{};
    std::vector<PerFrameUbo> backUbos_{};

    VkDescriptorSetLayout texSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadeUboLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout waterSetLayout_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout shadePipelineLayout_{};
    engine::render::UniquePipeline copyPipeline_{};
    engine::render::UniquePipeline shadePipeline_{};
    std::vector<VkDescriptorSet> texSets_{};
    std::vector<VkDescriptorSet> shadeUboSets_{};
    VkDescriptorSet waterSet_ = VK_NULL_HANDLE;
    VkBuffer waterVolumeBuffer_ = VK_NULL_HANDLE;
    std::vector<PerFrameUbo> shadeUbos_{};

    std::string backVertPath_{};
    std::string backFragPath_{};
    std::string fullscreenVertPath_{};
    std::string copyFragPath_{};
    std::string shadeVertPath_{};
    std::string shadeFragPath_{};
};
