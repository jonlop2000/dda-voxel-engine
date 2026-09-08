#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"
#include "engine/render/RendererConfig.h"
#include "engine/render/SceneAtmosphere.h"
#include "engine/render/passes/GBufferPass.h"

struct VulkanContext;
struct FrameContext;
struct PassCreateInfo;
struct Swapchain;
struct LightingPass;
struct ShadowMap;
struct BloomPass;
struct TAAPass;
struct WaterVolumePrepass;
class BlueNoiseTexture;

struct CompositePass
{
    struct AtmosphereUbo
    {
        // shared layout: density, height falloff, base height, sun phase strength.
        glm::vec4 atmosphere0{0.0f};
        // x=active water-volume count, yzw=reserved.
        glm::vec4 atmosphere1{0.0f};
    };

    [[nodiscard]] static AtmosphereUbo buildAtmosphereUbo(
        const engine::render::SceneAtmosphereSettings& settings,
        uint32_t activeWaterVolumeCount)
    {
        AtmosphereUbo ubo{};
        ubo.atmosphere0 = engine::render::packSceneAtmosphereParameters(settings);
        ubo.atmosphere1 = glm::vec4(static_cast<float>(activeWaterVolumeCount),
                                    0.0f, 0.0f, 0.0f);
        return ubo;
    }

    static constexpr size_t kSceneSlot = 0;
    static constexpr size_t kGBufferStartSlot = 1;
    static constexpr size_t kShadowSlot = kGBufferStartSlot + GBufferPass::kGBufferCount;
    static constexpr size_t kBloomExtractSlot = kShadowSlot + 1;
    static constexpr size_t kBloomBlurSlot = kBloomExtractSlot + 1;
    static constexpr size_t kTaaSlot = kBloomBlurSlot + 1;
    static constexpr size_t kBlueNoiseSlot = kTaaSlot + 1;
    static constexpr size_t kDepthSlot = kBlueNoiseSlot + 1;
    static constexpr size_t kLegacyWaterDistSlot = kDepthSlot + 1;
    static constexpr size_t kCompositeSlots = kLegacyWaterDistSlot + 1;

    void setShaderPaths(const std::string& fullscreenVert, const std::string& fragPath);
    void setViewMode(int mode);
    void setTonemapEnabled(bool enabled);
    void setExposure(float exposure);
    void setHighlightRecovery(float recovery);
    void setBloomEnabled(bool enabled);
    void setBloomIntensity(float intensity);
    void setApplyBloomInComposite(bool enabled);
    void setVignetteStrength(float strength);
    void setGrainStrength(float strength);
    void setSharpenIntensity(float intensity);
    void setFxaaEnabled(bool enabled);
    void setColorGradeEnabled(bool enabled);
    void setColorGradeStrength(float strength);
    void setColorGradeSaturation(float saturation);
    void setColorGradeContrast(float contrast);
    void setColorGradeTemperature(float temperature);
    void setPixelizationEnabled(bool enabled);
    void setPixelizationBlockSize(float blockSize);
    void setPixelizationStrength(float strength);
    void setPixelizationEdgeFocus(float edgeFocus);
    void setMaterialDetailStrength(float strength);
    void setTime(float time);
    void setFrameIndex(uint32_t frameIndex);
    void setStarsEnabled(bool enabled);
    void setStarSeed(uint32_t seed);
    void setStarDensity(float density);
    void setStarTwinkleSpeed(float speed);
    void setInvViewProj(const glm::mat4& invViewProj);
    void setDofEnabled(bool enabled);
    void setDofFocusDistance(float distance);
    void setDofFocusRange(float range);
    void setDofBlurStrength(float strength);
    void setCameraPosition(const glm::vec3& position);
    void setSceneAtmosphere(const engine::render::SceneAtmosphereSettings& settings);
    void setActiveWaterVolumeCount(uint32_t count);
    void setWaterVolumeBuffer(VkBuffer buffer);

    void create(VulkanContext& ctx, const PassCreateInfo& ci, const Swapchain& sc);
    void destroy(VulkanContext& ctx);
    void onResize(VulkanContext& ctx, const PassCreateInfo& ci, const Swapchain& sc);
    void updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                              const LightingPass& lighting, const ShadowMap& shadow,
                              const BloomPass& bloom, const TAAPass& taa,
                              const BlueNoiseTexture& blueNoise,
                              const WaterVolumePrepass* waterVolumePrepass = nullptr);
    void updateTaaView(VulkanContext& ctx, uint32_t frameIndex, VkImageView taaView,
                       VkSampler taaSampler);
    void record(VulkanContext& ctx, const FrameContext& fc);

private:
    struct PerFrameAtmosphereUbo
    {
        engine::render::UniqueBuffer buffer{};
        void* mapped = nullptr;
    };

    void createPipeline(VulkanContext& ctx);
    void destroyPipeline(VulkanContext& ctx);
    void updateAtmosphereUbo(VulkanContext& ctx, uint32_t frameIndex);

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout atmosphereSetLayout_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout pipelineLayout_{};
    engine::render::UniquePipeline pipeline_{};
    std::vector<VkDescriptorSet> descSets_;
    std::vector<VkDescriptorSet> atmosphereSets_;
    std::vector<PerFrameAtmosphereUbo> atmosphereUbos_;

    std::string fullscreenVertPath_;
    std::string fragPath_;
    // view mode selects which input to display.
    int viewMode_ = 0;
    bool tonemapEnabled_ = true;
    float exposure_ = 1.0f;
    float highlightRecovery_ = 0.0f;
    bool bloomEnabled_ = false;
    float bloomIntensity_ = 0.0f;
    bool applyBloomInComposite_ = false;
    float vignetteStrength_ = 0.0f;
    float grainStrength_ = 0.0f;
    float sharpenIntensity_ = 0.0f;
    bool fxaaEnabled_ = true;
    bool colorGradeEnabled_ = false;
    float colorGradeStrength_ = 0.0f;
    float colorGradeSaturation_ = 1.0f;
    float colorGradeContrast_ = 1.0f;
    float colorGradeTemperature_ = 0.0f;
    bool pixelizationEnabled_ = false;
    float pixelizationBlockSize_ = 4.0f;
    float pixelizationStrength_ = 0.70f;
    float pixelizationEdgeFocus_ = 0.45f;
    float materialDetailStrength_ = 0.0f;
    float time_ = 0.0f;
    uint32_t frameIndex_ = 0;
    const Swapchain* swapchain_ = nullptr;

    // procedural star parameters
    bool starsEnabled_ = false;
    uint32_t starSeed_ = 123;
    float starDensity_ = 0.02f;
    float starTwinkleSpeed_ = 1.0f;
    glm::mat4 invViewProj_{1.0f};

    // depth of field (in-composite focus blur)
    bool dofEnabled_ = false;
    float dofFocusDistance_ = 10.0f;
    float dofFocusRange_ = 6.0f;
    float dofBlurStrength_ = 0.85f;
    glm::vec3 cameraPos_{0.0f};
    engine::render::SceneAtmosphereSettings sceneAtmosphere_{};
    uint32_t activeWaterVolumeCount_ = 0;
    VkBuffer waterVolumeBuffer_ = VK_NULL_HANDLE;
};
