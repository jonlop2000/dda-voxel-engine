#pragma once

#include <array>
#include <cstdint>
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

#include "engine/render/HemisphereAmbient.h"
#include "engine/render/PaintedClouds.h"
#include "engine/render/RendererConfig.h"
#include "engine/render/SceneAtmosphere.h"
#include "engine/render/PaintedSky.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;
struct FrameContext;
struct PassCreateInfo;
struct GBufferPass;
struct LightsBuffer;
struct ShadowMap;
class ShadowBuffer;

struct LightingPass
{
    struct LitTarget
    {
        // hdr lighting output per frame.
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
        // used to reconstruct world position from depth.
        glm::mat4 invViewProj{1.0f};
        // unjittered projection used to anchor stylized shadow chunks to world/surface cells.
        glm::mat4 viewProj{1.0f};
        glm::mat4 view{1.0f};
        std::array<glm::mat4, kShadowCascades> lightViewProj{};
        glm::vec4 camPos{0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec4 lightDir{0.0f, -1.0f, 0.0f, 0.0f};
        glm::vec4 lightColor{1.0f};
        glm::vec4 cascadeSplits{10.0f, 30.0f, 100.0f, 0.0f};
        glm::vec4 shadowMapSize{2048.0f, 2048.0f, 1.0f / 2048.0f, 1.0f / 2048.0f};
        // x=waterLevel, y=time, z=scale, w=speed
        glm::vec4 caustics0{0.0f, 0.0f, 0.12f, 0.28f};
        // x=intensity, y=banding, z=depthFade, w=enabled
        glm::vec4 caustics1{0.52f, 0.34f, 18.0f, 1.0f};
        // x=density, y=heightFalloff, z=baseHeight, w=sunPhaseStrength
        glm::vec4 atmosphere0{0.0f};
        // x=sunPhaseExponent, yzw=direction from sample toward sun
        glm::vec4 atmosphere1{4.0f, 0.0f, 1.0f, 0.0f};
        glm::vec4 paintedSky0{1.0f, 1.0f, 1.0f, 0.0f};
        glm::vec4 paintedSky1{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 paintedSky2{1.0f, 1.0f, 1.0f, 0.0f};
        glm::vec4 paintedSky3{0.0105f, 0.0020f, 0.0f, 0.0f};
        glm::vec4 paintedSky4{48.0f, 4.0f, 0.0f, 0.0f};
        glm::vec4 paintedCloud0{1.08f, 1.06f, 1.00f, 0.0f};
        glm::vec4 paintedCloud1{0.64f, 0.72f, 0.82f, 0.46f};
        glm::vec4 paintedCloud2{90.0f, 105.0f, 0.76f, 0.085f};
        glm::vec4 paintedCloud3{0.38f, 0.16f, 0.035f, 0.18f};
        glm::vec4 paintedCloud4{0.0f};
    };

    struct FrameUboInputs
    {
        glm::mat4 invViewProj{1.0f};
        glm::mat4 viewProj{1.0f};
        glm::mat4 view{1.0f};
        std::array<glm::mat4, kShadowCascades> lightViewProj{};
        glm::vec3 cameraPosition{0.0f};
        glm::vec3 lightDirection{0.0f, -1.0f, 0.0f};
        float sunAngularRadius = 0.0f;
        glm::vec3 sunColor{1.0f};
        float sunIntensity = 1.0f;
        glm::vec4 cascadeSplits{10.0f, 30.0f, 100.0f, 0.0f};
        VkExtent2D shadowExtent{2048u, 2048u};
        float waterLevel = 0.0f;
        float animationTime = 0.0f;
        float waterCausticsScale = 0.12f;
        float waterCausticsSpeed = 0.28f;
        float waterCausticsIntensity = 0.52f;
        float waterCausticsBanding = 0.34f;
        float waterCausticsDepthFade = 18.0f;
        bool waterCausticsEnabled = true;
        engine::render::SceneAtmosphereSettings sceneAtmosphere{};
        engine::render::PaintedSkySettings paintedSky{};
        engine::render::PaintedCloudSettings paintedClouds{};
        engine::render::PaintedCloudFrameInputs paintedCloudFrame{};
    };

    [[nodiscard]] static FrameUbo buildFrameUbo(const FrameUboInputs& in)
    {
        FrameUbo ubo{};
        ubo.invViewProj = in.invViewProj;
        ubo.viewProj = in.viewProj;
        ubo.view = in.view;
        ubo.lightViewProj = in.lightViewProj;
        ubo.camPos = glm::vec4(in.cameraPosition, 1.0f);
        ubo.lightDir = glm::vec4(in.lightDirection, in.sunAngularRadius);
        ubo.lightColor = glm::vec4(in.sunColor, in.sunIntensity);
        ubo.cascadeSplits = in.cascadeSplits;
        ubo.shadowMapSize =
            glm::vec4(static_cast<float>(in.shadowExtent.width),
                      static_cast<float>(in.shadowExtent.height),
                      1.0f / static_cast<float>(in.shadowExtent.width),
                      1.0f / static_cast<float>(in.shadowExtent.height));
        ubo.caustics0 = glm::vec4(in.waterLevel, in.animationTime,
                                  in.waterCausticsScale, in.waterCausticsSpeed);
        ubo.caustics1 = glm::vec4(in.waterCausticsIntensity,
                                  in.waterCausticsBanding,
                                  in.waterCausticsDepthFade,
                                  in.waterCausticsEnabled ? 1.0f : 0.0f);
        ubo.atmosphere0 =
            engine::render::packSceneAtmosphereParameters(in.sceneAtmosphere);
        ubo.atmosphere1 = engine::render::packSceneAtmosphereSun(
            in.sceneAtmosphere, in.lightDirection);
        const engine::render::PaintedSkyGpuData paintedSky =
            engine::render::packPaintedSkyGpuData(in.paintedSky);
        ubo.paintedSky0 = paintedSky.sky0;
        ubo.paintedSky1 = paintedSky.sky1;
        ubo.paintedSky2 = paintedSky.sky2;
        ubo.paintedSky3 = paintedSky.sky3;
        ubo.paintedSky4 = paintedSky.sky4;
        const engine::render::PaintedCloudGpuData paintedClouds =
            engine::render::packPaintedCloudGpuData(in.paintedClouds,
                                                     in.paintedCloudFrame);
        ubo.paintedCloud0 = paintedClouds.cloud0;
        ubo.paintedCloud1 = paintedClouds.cloud1;
        ubo.paintedCloud2 = paintedClouds.cloud2;
        ubo.paintedCloud3 = paintedClouds.cloud3;
        ubo.paintedCloud4 = paintedClouds.cloud4;
        return ubo;
    }

    // converts the halton offset from render pixels to the uv correction consumed by
    // the procedural background branch. a zero extent must remain a safe no-op.
    [[nodiscard]] static glm::vec2 normalizedProjectionJitterUv(
        const glm::vec2& jitterPixels, VkExtent2D renderExtent)
    {
        return {
            renderExtent.width > 0
                ? jitterPixels.x / static_cast<float>(renderExtent.width)
                : 0.0f,
            renderExtent.height > 0
                ? jitterPixels.y / static_cast<float>(renderExtent.height)
                : 0.0f};
    }

    void setShaderPaths(const std::string& fullscreenVert, const std::string& fragPath);
    void setLightParams(uint32_t lightCount, int debugMode);
    void setWaterVolumes(VkBuffer buffer, uint32_t count);
    void setWaterVolumeCount(uint32_t count);

    void create(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                const LightsBuffer& lights, const ShadowMap& shadowMap, ShadowBuffer& shadowBuffer,
                VkImageView shadowResolvedView, VkSampler shadowResolvedSampler,
                VkImageView aoResolvedView, VkSampler aoResolvedSampler,
                VkImageView aoRawView, VkSampler aoRawSampler,
                VkImageView localShadowResolvedView, VkSampler localShadowResolvedSampler);
    void destroy(VulkanContext& ctx);
    void onResize(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                  const ShadowMap& shadowMap, ShadowBuffer& shadowBuffer,
                  VkImageView shadowResolvedView, VkSampler shadowResolvedSampler,
                  VkImageView aoResolvedView, VkSampler aoResolvedSampler,
                  VkImageView aoRawView, VkSampler aoRawSampler,
                  VkImageView localShadowResolvedView, VkSampler localShadowResolvedSampler);
    void record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo,
                int useDDAShadows, int debugShadowMode, int csmDitherEnabled, int csmEnabled,
                int aoEnabled, float aoContribution, float terminatorSoftness, int terminatorMode,
                int localShadowSlotCount,
                const std::array<int32_t, 4>& localShadowLightIndices,
                float pixelShadowStyleStrength,
                int voxelCellVariationEnabled,
                const engine::render::HemisphereAmbientSettings& hemisphereAmbient,
                const glm::vec3& skyColor,
                const glm::vec2& projectionJitterUv = glm::vec2(0.0f));
    void updateShadowBindings(VulkanContext& ctx, uint32_t frameIndex, ShadowBuffer& shadowBuffer,
                              VkImageView shadowResolvedView, VkSampler shadowResolvedSampler,
                              VkImageView aoResolvedView, VkSampler aoResolvedSampler,
                              VkImageView aoRawView, VkSampler aoRawSampler,
                              VkImageView localShadowResolvedView,
                              VkSampler localShadowResolvedSampler);

    const LitTarget& lit(uint32_t frameIndex) const;
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
    void updateUbo(uint32_t frameIndex, const FrameUbo& frameUbo);
    void updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                              const LightsBuffer& lights, const ShadowMap& shadowMap,
                              ShadowBuffer& shadowBuffer,
                              VkImageView shadowResolvedView, VkSampler shadowResolvedSampler,
                              VkImageView aoResolvedView, VkSampler aoResolvedSampler,
                              VkImageView aoRawView, VkSampler aoRawSampler,
                              VkImageView localShadowResolvedView,
                              VkSampler localShadowResolvedSampler);

    std::vector<LitTarget> targets_;
    VkExtent2D extent_{};

    // set 0: g-buffer textures; set 1: ubo + light buffer + water volumes.
    VkDescriptorSetLayout gbufferSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout uboSetLayout_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout pipelineLayout_{};
    engine::render::UniquePipeline pipeline_{};
    std::vector<VkDescriptorSet> gbufferSets_;
    std::vector<VkDescriptorSet> lightSets_;
    std::vector<PerFrameUbo> ubos_;

    std::string fullscreenVertPath_;
    std::string fragPath_;

    int lightCount_ = 0;
    int debugMode_ = 0;
    VkBuffer waterVolumeBuffer_ = VK_NULL_HANDLE;
    uint32_t waterVolumeCount_ = 0;
    const LightsBuffer* lights_ = nullptr;
    const ShadowMap* shadowMap_ = nullptr;
    ShadowBuffer* shadowBuffer_ = nullptr;  // day 11
};
