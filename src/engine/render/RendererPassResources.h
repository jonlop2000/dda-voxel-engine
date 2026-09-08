#pragma once

#include "engine/render/AuxiliaryRayResolution.h"
#include "engine/render/passes/AOPass.h"
#include "engine/render/passes/AuxiliaryTileListPass.h"
#include "engine/render/passes/BloomPass.h"
#include "engine/render/passes/BlueNoiseTexture.h"
#include "engine/render/passes/CompositePass.h"
#include "engine/render/passes/FoliagePass.h"
#include "engine/render/passes/GBufferPass.h"
#include "engine/render/passes/GlassPass.h"
#include "engine/render/passes/LightingPass.h"
#include "engine/render/passes/LocalLightShadowBuffer.h"
#include "engine/render/passes/LocalLightShadowPass.h"
#include "engine/render/passes/LocalLightTemporalResolve.h"
#include "engine/render/passes/LocalShadowBlur.h"
#include "engine/render/passes/ShadowBuffer.h"
#include "engine/render/passes/ShadowDenoisePass.h"
#include "engine/render/passes/ShadowPass.h"
#include "engine/render/passes/ShadowRayPass.h"
#include "engine/render/passes/SpatialUpscalePass.h"
#include "engine/render/passes/TAAPass.h"
#include "engine/render/passes/TemporalResolve.h"
#include "engine/render/passes/UiOverlayPass.h"
#include "engine/render/passes/VoxelGBufferPass.h"
#include "engine/render/passes/VoxelGlassRefractPass.h"
#include "engine/render/passes/WaterBodyPass.h"
#include "engine/render/passes/WaterPass.h"
#include "engine/render/passes/WaterVolumePrepass.h"
#include "engine/render/passes/OBBPass.h"

#include <filesystem>
#include <string>

struct Commands;
struct LightsBuffer;
struct PassCreateInfo;
struct Swapchain;
struct VulkanContext;

namespace engine::render
{

struct RendererPassAvailability
{
    bool auxiliaryTileListPass = true;
    bool shadowRayPass = true;
    bool shadowTemporalResolve = true;
    bool shadowDenoisePass = true;
    bool localLightShadowPass = true;
    bool localLightShadowResolve = true;
    bool localShadowBlur = true;
    bool aoPass = true;
    bool aoTemporalResolve = true;
    bool foliagePass = true;
};

struct DepthHistoryResources
{
    int index = 0;
    VkImage images[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory memory[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView views[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkSampler sampler = VK_NULL_HANDLE;
};

struct DepthHistoryCreateInfo
{
    VulkanContext* context = nullptr;
    Commands* commands = nullptr;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
};

struct PassDestroyInfo
{
    VulkanContext* context = nullptr;
    void (*destroyPixelInspectResources)(void*) = nullptr;
    void* user = nullptr;
};

struct PrimaryTargetResizeInfo
{
    VulkanContext* context = nullptr;
    PassCreateInfo* passCreateInfo = nullptr;
    VkDescriptorSetLayout voxelSetLayout = VK_NULL_HANDLE;
    VkBuffer waterVolumeBuffer = VK_NULL_HANDLE;
    VkBuffer waterContainerBuffer = VK_NULL_HANDLE;
    VkExtent2D shadowRayExtent{};
};

struct OptionalTargetResizeResult
{
    bool ddaShadowsDisabled = false;
    bool localLightShadowsDisabled = false;
    bool ambientOcclusionDisabled = false;
};

struct MainLightingResizeInfo
{
    VulkanContext* context = nullptr;
    PassCreateInfo* passCreateInfo = nullptr;
    VkBuffer waterVolumeBuffer = VK_NULL_HANDLE;
    uint32_t waterVolumeCount = 0;
};

struct ReflectionTargetResizeInfo
{
    VulkanContext* context = nullptr;
    PassCreateInfo* passCreateInfo = nullptr;
    VkDescriptorSetLayout materialLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout voxelSetLayout = VK_NULL_HANDLE;
    VkBuffer waterVolumeBuffer = VK_NULL_HANDLE;
    uint32_t waterVolumeCount = 0;
    VkBuffer waterContainerBuffer = VK_NULL_HANDLE;
    const LightsBuffer* lights = nullptr;
};

struct WaterPostResizeInfo
{
    VulkanContext* context = nullptr;
    PassCreateInfo* passCreateInfo = nullptr;
    const Swapchain* swapchain = nullptr;
    VkBuffer waterVolumeBuffer = VK_NULL_HANDLE;
    bool taaEnabled = false;
};

#if VOXEL_WITH_RUNTIME_UI
struct RuntimeUiOverlayResizeInfo
{
    VulkanContext* context = nullptr;
    Commands* commands = nullptr;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    const CpuImage* fontAtlasImage = nullptr;
    const std::string* fontAtlasLabel = nullptr;
};
#endif

struct PassCreateResourcesInfo
{
    VulkanContext* context = nullptr;
    Commands* commands = nullptr;
    PassCreateInfo* passCreateInfo = nullptr;
    const char* argv0 = nullptr;
    std::filesystem::path assetRoot{};
    VkDescriptorSetLayout materialLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout voxelSetLayout = VK_NULL_HANDLE;
    VkBuffer voxelPaletteBuffer = VK_NULL_HANDLE;
    VkBuffer waterVolumeBuffer = VK_NULL_HANDLE;
    uint32_t waterVolumeCount = 0;
    VkBuffer waterContainerBuffer = VK_NULL_HANDLE;
    const LightsBuffer* lights = nullptr;
    const Swapchain* swapchain = nullptr;
    bool taaEnabled = false;
    VkExtent2D shadowRayExtent{};
    VkExtent2D aoRayExtent{};
    TemporalResolveConfig shadowTemporalResolveConfig{};
    LocalLightTemporalResolveConfig localShadowResolveConfig{};
    TemporalResolveConfig aoTemporalResolveConfig{};
#if VOXEL_WITH_RUNTIME_UI
    const CpuImage* runtimeUiFontAtlasImage = nullptr;
    const std::string* runtimeUiFontAtlasLabel = nullptr;
#endif
    void (*createPixelInspectResources)(void*) = nullptr;
    void* user = nullptr;
};

struct PassCreateResourcesResult
{
    bool ok = false;
    const char* fatalComponent = nullptr;
    const char* fatalReason = nullptr;
    bool ddaShadowsDisabled = false;
    bool localLightShadowsDisabled = false;
    bool localShadowBlurDisabled = false;
    bool ambientOcclusionDisabled = false;
    bool resetShadowHistory = true;
    bool resetLocalShadowHistory = true;
    bool resetAoHistory = true;
    bool resetTaaHistory = true;
    bool localShadowNeutralClearPending = true;
    bool aoNeutralClearPending = true;
};

struct RendererPassResources
{
    void configureShaderPaths(const char* argv0);
    void resetAvailability() { availability = RendererPassAvailability{}; }
    [[nodiscard]] PassCreateResourcesResult createResources(
        const PassCreateResourcesInfo& info);
    [[nodiscard]] bool createDepthHistory(const DepthHistoryCreateInfo& info);
    void destroyDepthHistory(VulkanContext& context);
    [[nodiscard]] bool destroyResources(const PassDestroyInfo& info);
    void destroySwapchainDependentResources(VulkanContext& context);
    [[nodiscard]] bool recreatePrimaryTargets(const PrimaryTargetResizeInfo& info);
    [[nodiscard]] OptionalTargetResizeResult resizeOptionalTargets(VulkanContext& context,
                                                                   VkExtent2D extent,
                                                                   VkExtent2D aoRayExtent);
    [[nodiscard]] bool recreateMainLightingTargets(const MainLightingResizeInfo& info);
    [[nodiscard]] bool recreateReflectionTargets(const ReflectionTargetResizeInfo& info);
    [[nodiscard]] bool recreateWaterPostTargets(const WaterPostResizeInfo& info);
#if VOXEL_WITH_RUNTIME_UI
    [[nodiscard]] bool recreateRuntimeUiOverlay(
        const RuntimeUiOverlayResizeInfo& info);
#endif
    void updateLightingShadowBindings(VulkanContext& context, uint32_t frameIndex);
    void updateReflectionLightingShadowBindings(VulkanContext& context, uint32_t frameIndex);

    [[nodiscard]] VkImageView currentShadowResolvedView() const
    {
        if (availability.shadowDenoisePass)
        {
            return shadowDenoisePass.outputView();
        }
        return availability.shadowTemporalResolve ? shadowTemporalResolve.resolvedView()
                                                  : shadowBuffer.getCurrentImageView();
    }

    [[nodiscard]] VkSampler currentShadowResolvedSampler() const
    {
        if (availability.shadowDenoisePass)
        {
            return shadowDenoisePass.sampler();
        }
        return availability.shadowTemporalResolve ? shadowTemporalResolve.sampler()
                                                  : shadowBuffer.getSampler();
    }

    [[nodiscard]] VkImageView currentAoResolvedView() const
    {
        if (availability.aoTemporalResolve)
        {
            return aoTemporalResolve.resolvedView();
        }
        if (availability.aoPass)
        {
            return aoPass.aoView();
        }
        return shadowBuffer.getCurrentImageView();
    }

    [[nodiscard]] VkSampler currentAoResolvedSampler() const
    {
        if (availability.aoTemporalResolve)
        {
            return aoTemporalResolve.sampler();
        }
        if (availability.aoPass)
        {
            return aoPass.aoSampler();
        }
        return shadowBuffer.getSampler();
    }

    [[nodiscard]] VkImageView currentAoRawView() const
    {
        if (availability.aoPass)
        {
            return aoPass.aoView();
        }
        if (availability.aoTemporalResolve)
        {
            return aoTemporalResolve.resolvedView();
        }
        return shadowBuffer.getCurrentImageView();
    }

    [[nodiscard]] VkSampler currentAoRawSampler() const
    {
        if (availability.aoPass)
        {
            return aoPass.aoSampler();
        }
        if (availability.aoTemporalResolve)
        {
            return aoTemporalResolve.sampler();
        }
        return shadowBuffer.getSampler();
    }

    [[nodiscard]] VkImageView currentLocalShadowResolvedView() const
    {
        return availability.localLightShadowResolve
                   ? localLightShadowTemporalResolve.resolvedView()
                   : localLightShadowBuffer.getCurrentImageView();
    }

    [[nodiscard]] VkSampler currentLocalShadowResolvedSampler() const
    {
        return availability.localLightShadowResolve ? localLightShadowTemporalResolve.sampler()
                                                   : localLightShadowBuffer.getSampler();
    }

    ShadowPass shadow{};
    GBufferPass gbuffer{};
    FoliagePass foliage{};
    VoxelGBufferPass voxelGbuffer{};
    LightingPass lighting{};
    WaterBodyPass waterBody{};
    GBufferPass reflectionGbuffer{};
    VoxelGBufferPass reflectionVoxelGbuffer{};
    OBBPass reflectionObbPass{};
    LightingPass reflectionLighting{};
    VkExtent2D reflectionExtent{};
    WaterPass water{};
    WaterPass waterV2{};
    WaterVolumePrepass waterVolumePrepass{};
    WaterVolumePrepass reflectionWaterVolumePrepass{};
    GlassPass glass{};
    VoxelGlassRefractPass voxelGlassRefract{};
    BloomPass bloom{};
    TAAPass taa{};
    SpatialUpscalePass spatialUpscale{};
    CompositePass composite{};
#if VOXEL_WITH_RUNTIME_UI
    UiOverlayPass uiOverlay{};
#endif
    OBBPass obbPass{};
    ShadowRayPass shadowRayPass{};
    TemporalResolve shadowTemporalResolve{};
    ShadowDenoisePass shadowDenoisePass{};
    ShadowBuffer shadowBuffer{};
    LocalLightShadowPass localLightShadowPass{};
    LocalLightShadowBuffer localLightShadowBuffer{};
    LocalShadowBlur localShadowBlur{};
    LocalLightTemporalResolve localLightShadowTemporalResolve{};
    BlueNoiseTexture blueNoiseTexture{};
    AuxiliaryTileListPass auxiliaryTileListPass{};
    AOPass aoPass{};
    TemporalResolve aoTemporalResolve{};
    AdaptiveAuxiliaryRayState adaptiveSunShadowState{};
    AdaptiveAuxiliaryRayState adaptiveLocalShadowState{};
    AdaptiveAuxiliaryRayState adaptiveAoState{};
    RendererPassAvailability availability{};
    DepthHistoryResources depthHistory{};
};

}  // namespace engine::render
