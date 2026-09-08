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
#include "Resources/Mesh.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;
struct FrameContext;
struct PassCreateInfo;
struct GBufferPass;
struct LightingPass;

struct WaterPass
{
    struct RefractionSource
    {
        VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct NoiseTexture
    {
        VkFormat format = VK_FORMAT_R8_UNORM;
        uint32_t width = 256;
        uint32_t height = 256;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct WaterTarget
    {
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
        glm::mat4 view{1.0f};
        glm::mat4 viewProj{1.0f};
        glm::mat4 invProj{1.0f};
        glm::mat4 reflectedViewProj{1.0f};
        glm::vec4 camPos{0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec4 sunDirToSun{0.0f, 1.0f, 0.0f, 0.0f};
        glm::vec4 sunColor{1.4f, 1.37f, 1.24f, 1.0f};
        glm::vec4 waterTint{0.02f, 0.12f, 0.18f, 1.0f};
        // x=waterLevel, y=time, z=waveScale, w=waveAmplitude
        glm::vec4 params0{4.0f, 0.0f, 0.12f, 1.0f};
        // x=refractionStrength, y=specularPower, z=specularIntensity, w=fresnelBias/fresnelF0
        glm::vec4 params1{0.03f, 256.0f, 3.0f, 0.05f};
        // x=crestThreshold, y=crestSoftness, z=crestIntensity, w=distortionDepthScale
        glm::vec4 params2{0.62f, 0.08f, 0.38f, 0.3f};
        // x=edgeFadeDepth, y=bandHardness, z=reflectionStrength, w=gradientStrength
        glm::vec4 params3{0.5f, 0.6f, 0.4f, 0.25f};
        // x=debugMode, y=waterVolumeCount, z=styleMode, w=planarReflectionStrength
        glm::vec4 params4{0.0f, 0.0f, 1.0f, 0.0f};
        // water V2 material parameters. legacy water shaders ignore these appended fields.
        // x=surfacePathCap, y=surfaceAbsorptionScale, z=surfaceFogPathScale, w=surfaceFogMixScale
        glm::vec4 v2Optics0{8.0f, 0.24f, 0.18f, 0.22f};
        // x=surfaceFogMixMax, y=refractionDepthScale, z=reserved, w=reserved
        glm::vec4 v2Optics1{0.18f, 1.6f, 0.0f, 0.0f};
        // x=bodyBase, y=bodyPathRate, z=bodyPathWeight, w=bodyViewWeight
        glm::vec4 v2Body0{0.12f, 0.22f, 0.36f, 0.18f};
        // x=bodyMax, y=transmissionBase, z=transmissionTintScale, w=transmissionTintMix
        glm::vec4 v2Body1{0.58f, 0.72f, 1.45f, 0.24f};
        // x=postReflectionDamping, y=postReflectionBodyMix, z=specularScale, w=rippleStrength
        glm::vec4 v2Body2{0.45f, 0.26f, 0.45f, 0.040f};
        // x=localUvOffset, y=localBase, z=localFacingWeight, w=localPathWeight
        glm::vec4 v2Reflection0{0.012f, 0.10f, 0.12f, 0.004f};
        // x=localPathMax, y=localWeightMin, z=localWeightMax, w=fallbackSkyLeak
        glm::vec4 v2Reflection1{0.03f, 0.08f, 0.25f, 0.045f};
        // x=distortionMin, y=distortionMax, z=guardBand, w=borderFadeWidth
        glm::vec4 v2Planar0{0.0025f, 0.0070f, 0.10f, 0.060f};
        // x=viewGateMin, y=viewGateStart, z=viewGateEnd, w=planarEnvBase
        glm::vec4 v2Planar1{0.34f, 0.015f, 0.42f, 0.025f};
        // x=planarEnvGrazing, y=reflectionGrazingBoost, z=reflectionPlanarBoost, w=reflectionMixMax
        glm::vec4 v2Planar2{0.045f, 0.16f, 0.18f, 0.70f};
        // x=sideContactStart, y=sideContactEnd, z=waterlineStart, w=waterlineEnd
        glm::vec4 v2Contact0{0.045f, 0.34f, 0.006f, 0.070f};
        // x=depthTintMix, y=depthBlend, z=contactBase, w=contactView
        glm::vec4 v2Contact1{0.35f, 0.18f, 0.032f, 0.065f};
        // x=glintStrength, y=sideOpacity, z=waterlineOpacity, w=waterlineFresnelBoost
        glm::vec4 v2Contact2{0.085f, 0.055f, 0.085f, 0.22f};
        // x=opacityMin, y=opacityMax, z=pathOpacityScale, w=pathOpacityMax
        glm::vec4 v2Opacity0{0.16f, 0.34f, 0.012f, 0.10f};
        // x=bodyOpacity, y=waveOpacity, z=alphaMin, w=alphaMax
        glm::vec4 v2Opacity1{0.13f, 0.075f, 0.18f, 0.66f};
        // x=voxelVfxEnabled, y=voxelVfxDensity, z=voxelVfxDrift, w=voxelVfxScale
        glm::vec4 v2Vfx0{1.0f, 0.35f, 0.20f, 0.15f};
        // x=foamEnabled, y=foamIntensity, z=foamRadius, w=foamRise
        glm::vec4 v2Vfx1{0.0f, 0.65f, 3.0f, 0.55f};
        // x=foamCenterX, y=foamCenterZ, z=foamScale, w=foamSpread
        glm::vec4 v2Vfx2{0.0f, 0.0f, 0.25f, 0.35f};
        // x=bodyDetailStrength, y=bodyDetailPathRate, z=bodyScatterStrength, w=bodyNoiseStrength
        glm::vec4 v2BodyDetail0{0.56f, 0.44f, 0.145f, 0.34f};
        // x=density, y=heightFalloff, z=baseHeight, w=sunPhaseStrength
        glm::vec4 atmosphere0{0.0f};
        // WaterBodyPass-only scene policy. appended so the existing surface-shader
        // ubo offsets, including atmosphere0, remain byte-compatible.
        // x=shaftStrength, y=bandContrast, z=apertureHalfExtent, w=warmth
        glm::vec4 v2BodyShaft0{1.0f, 0.0f, 0.0f, 0.0f};
    };

    void setShaderPaths(const std::string& fullscreenVert, const std::string& copyFrag,
                        const std::string& waterVert, const std::string& waterFrag);
    void setWaterVolumeBuffer(VkBuffer buffer);

    void create(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                const LightingPass& lighting,
                const LightingPass* planarReflection = nullptr);
    void destroy(VulkanContext& ctx);
    void onResize(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                  const LightingPass& lighting,
                  const LightingPass* planarReflection = nullptr);
    void recordSceneColorCopy(const FrameContext& fc, const LightingPass& lighting);
    void record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo,
                const LightingPass& lighting, const LightingPass* planarReflection,
                bool usePlanarReflection, const MeshGpu& mesh, bool drawWater);

    const WaterTarget& target(uint32_t frameIndex) const;
    const RefractionSource& refractionSource(uint32_t frameIndex) const;
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
    void createTargets(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer);
    void destroyTargets(VulkanContext& ctx);
    void createRefractionSources(VulkanContext& ctx, const PassCreateInfo& ci,
                                 const LightingPass& lighting);
    void destroyRefractionSources(VulkanContext& ctx);
    void createNoiseTexture(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroyNoiseTexture(VulkanContext& ctx);
    void recordRefractionCopy(const FrameContext& fc, const LightingPass& lighting);
    void updateUbo(uint32_t frameIndex, const FrameUbo& frameUbo);
    void updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                              const LightingPass& lighting,
                              const LightingPass* planarReflection);
    void updateWaterDescriptor(VulkanContext& ctx);

    std::vector<WaterTarget> targets_{};
    std::vector<RefractionSource> refractionSources_{};
    NoiseTexture noiseTexture_{};
    VkExtent2D extent_{};
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;

    VkDescriptorSetLayout texSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout uboSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout waterSetLayout_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout pipelineLayout_{};
    engine::render::UniquePipeline copyPipeline_{};
    engine::render::UniquePipeline waterPipeline_{};
    std::vector<VkDescriptorSet> texSets_{};
    std::vector<VkDescriptorSet> uboSets_{};
    VkDescriptorSet waterSet_ = VK_NULL_HANDLE;
    std::vector<PerFrameUbo> ubos_{};
    VkBuffer waterVolumeBuffer_ = VK_NULL_HANDLE;

    std::string fullscreenVertPath_{};
    std::string copyFragPath_{};
    std::string waterVertPath_{};
    std::string waterFragPath_{};
};
