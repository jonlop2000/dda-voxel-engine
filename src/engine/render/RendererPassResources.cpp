#include "engine/render/RendererPassResources.h"

#include "Core/Logger.h"
#include "engine/render/Commands.h"
#include "engine/render/Swapchain.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "engine/render/passes/PassCreateInfo.h"
#include "Resources/GpuImage.h"
#include "Resources/ShaderModule.h"
#include "engine/render/FrameCharacterization.h"
#include "engine/render/gpu/SamplerCache.h"

#include <algorithm>

namespace engine::render
{

namespace
{
static constexpr const char* kShaderFullscreenVert = "fullscreen.vert.spv";
static constexpr const char* kShaderCompositeGBufferFrag = "composite_gbuffer.frag.spv";
static constexpr const char* kShaderGBufferVert = "gbuffer.vert.spv";
static constexpr const char* kShaderGBufferFrag = "gbuffer.frag.spv";
static constexpr const char* kShaderFoliageVert = "foliage.vert.spv";
static constexpr const char* kShaderFoliageFrag = "foliage.frag.spv";
static constexpr const char* kShaderLightingFrag = "lighting_multi.frag.spv";
static constexpr const char* kShaderShadowVert = "shadow.vert.spv";
static constexpr const char* kShaderShadowFrag = "shadow.frag.spv";
static constexpr const char* kShaderBloomExtractFrag = "bloom_extract.frag.spv";
static constexpr const char* kShaderBloomBlurFrag = "bloom_blur.frag.spv";
static constexpr const char* kShaderTaaVert = "taa.vert.spv";
static constexpr const char* kShaderTaaFrag = "taa.frag.spv";
static constexpr const char* kShaderSpatialUpscaleFrag = "spatial_upscale.frag.spv";
static constexpr const char* kShaderWaterVert = "water.vert.spv";
static constexpr const char* kShaderWaterFrag = "water.frag.spv";
static constexpr const char* kShaderWaterV2Frag = "water_v2.frag.spv";
static constexpr const char* kShaderWaterBodyFrag = "water_body.frag.spv";
static constexpr const char* kShaderWaterVolumePrepassFrag = "water_volume_prepass.frag.spv";
static constexpr const char* kShaderWaterCopyFrag = "water_copy.frag.spv";
static constexpr const char* kShaderGlassBackDepthVert = "glass_backdepth.vert.spv";
static constexpr const char* kShaderGlassBackDepthFrag = "glass_backdepth.frag.spv";
static constexpr const char* kShaderGlassShadeVert = "glass_shade.vert.spv";
static constexpr const char* kShaderGlassShadeFrag = "glass_shade.frag.spv";
static constexpr const char* kShaderGlassCopyFrag = "glass_copy.frag.spv";
static constexpr const char* kShaderVoxelGlassRefractFrag = "voxel_glass_refract.frag.spv";
static constexpr const char* kShaderUiVert = "ui.vert.spv";
static constexpr const char* kShaderUiQuadFrag = "ui_quad.frag.spv";
static constexpr const char* kShaderVoxelObbVert = "obb.vert.spv";
static constexpr const char* kShaderVoxelObbFullScreenVert = "obb_fullscreen.vert.spv";
static constexpr const char* kShaderVoxelObbSharedAlignedVert =
    "obb_shared_aligned.vert.spv";
static constexpr const char* kShaderVoxelObbDdaFrag = "obb_dda.frag.spv";
static constexpr const char* kShaderVoxelObbDdaHotFrag = "obb_dda_hot.frag.spv";
static constexpr const char* kShaderVoxelObbDdaOpaqueFrag = "obb_dda_opaque.frag.spv";
static constexpr const char* kShaderVoxelObbDdaUnwrappedOpaqueFrag =
    "obb_dda_unwrapped_opaque.frag.spv";
static constexpr const char* kShaderVoxelObbDdaNearClipSafeOpaqueFrag =
    "obb_dda_near_clip_safe_opaque.frag.spv";
static constexpr const char* kShaderVoxelObbDdaSharedAlignedOpaqueFrag =
    "obb_dda_shared_aligned_opaque.frag.spv";
static constexpr const char* kShaderVoxelObbDdaSharedAlignedNearClipFrag =
    "obb_dda_shared_aligned_near_clip.frag.spv";

VkExtent2D quarterExtent(VkExtent2D extent)
{
    VkExtent2D out{};
    out.width = std::max(1u, extent.width / 4u);
    out.height = std::max(1u, extent.height / 4u);
    return out;
}
}  // namespace

void RendererPassResources::configureShaderPaths(const char* argv0)
{
    shadow.setShaderPaths(resolveShaderPath(argv0, kShaderShadowVert),
                          resolveShaderPath(argv0, kShaderShadowFrag));
    gbuffer.setShaderPaths(resolveShaderPath(argv0, kShaderGBufferVert),
                           resolveShaderPath(argv0, kShaderGBufferFrag));
    reflectionGbuffer.setShaderPaths(resolveShaderPath(argv0, kShaderGBufferVert),
                                     resolveShaderPath(argv0, kShaderGBufferFrag));
    foliage.setShaderPaths(resolveShaderPath(argv0, kShaderFoliageVert),
                           resolveShaderPath(argv0, kShaderFoliageFrag));
    lighting.setShaderPaths(resolveShaderPath(argv0, kShaderFullscreenVert),
                            resolveShaderPath(argv0, kShaderLightingFrag));
    waterBody.setShaderPaths(resolveShaderPath(argv0, kShaderFullscreenVert),
                             resolveShaderPath(argv0, kShaderWaterBodyFrag));
    reflectionLighting.setShaderPaths(resolveShaderPath(argv0, kShaderFullscreenVert),
                                      resolveShaderPath(argv0, kShaderLightingFrag));
    bloom.setShaderPaths(resolveShaderPath(argv0, kShaderFullscreenVert),
                         resolveShaderPath(argv0, kShaderBloomExtractFrag),
                         resolveShaderPath(argv0, kShaderBloomBlurFrag));
    taa.setShaderPaths(resolveShaderPath(argv0, kShaderTaaVert),
                       resolveShaderPath(argv0, kShaderTaaFrag));
    spatialUpscale.setShaderPaths(resolveShaderPath(argv0, kShaderFullscreenVert),
                                  resolveShaderPath(argv0, kShaderSpatialUpscaleFrag));
    water.setShaderPaths(resolveShaderPath(argv0, kShaderFullscreenVert),
                         resolveShaderPath(argv0, kShaderWaterCopyFrag),
                         resolveShaderPath(argv0, kShaderWaterVert),
                         resolveShaderPath(argv0, kShaderWaterFrag));
    waterV2.setShaderPaths(resolveShaderPath(argv0, kShaderFullscreenVert),
                           resolveShaderPath(argv0, kShaderWaterCopyFrag),
                           resolveShaderPath(argv0, kShaderWaterVert),
                           resolveShaderPath(argv0, kShaderWaterV2Frag));
    waterVolumePrepass.setShaderPaths(resolveShaderPath(argv0, kShaderFullscreenVert),
                                      resolveShaderPath(argv0, kShaderWaterVolumePrepassFrag));
    reflectionWaterVolumePrepass.setShaderPaths(
        resolveShaderPath(argv0, kShaderFullscreenVert),
        resolveShaderPath(argv0, kShaderWaterVolumePrepassFrag));
    glass.setShaderPaths(resolveShaderPath(argv0, kShaderGlassBackDepthVert),
                         resolveShaderPath(argv0, kShaderGlassBackDepthFrag),
                         resolveShaderPath(argv0, kShaderFullscreenVert),
                         resolveShaderPath(argv0, kShaderGlassCopyFrag),
                         resolveShaderPath(argv0, kShaderGlassShadeVert),
                         resolveShaderPath(argv0, kShaderGlassShadeFrag));
    voxelGlassRefract.setShaderPaths(resolveShaderPath(argv0, kShaderFullscreenVert),
                                     resolveShaderPath(argv0, kShaderVoxelGlassRefractFrag));
    composite.setShaderPaths(resolveShaderPath(argv0, kShaderFullscreenVert),
                             resolveShaderPath(argv0, kShaderCompositeGBufferFrag));
#if VOXEL_WITH_RUNTIME_UI
    uiOverlay.setShaderPaths(resolveShaderPath(argv0, kShaderUiVert),
                             resolveShaderPath(argv0, kShaderUiQuadFrag));
#endif
    obbPass.setShaderPaths(resolveShaderPath(argv0, kShaderVoxelObbVert),
                           resolveShaderPath(argv0, kShaderVoxelObbFullScreenVert),
                           resolveShaderPath(argv0,
                                             kShaderVoxelObbSharedAlignedVert),
                           resolveShaderPath(argv0, kShaderVoxelObbDdaHotFrag),
                           resolveShaderPath(argv0, kShaderVoxelObbDdaOpaqueFrag),
                           resolveShaderPath(argv0, kShaderVoxelObbDdaUnwrappedOpaqueFrag),
                           resolveShaderPath(argv0,
                                             kShaderVoxelObbDdaNearClipSafeOpaqueFrag),
                           resolveShaderPath(
                               argv0, kShaderVoxelObbDdaSharedAlignedOpaqueFrag),
                           resolveShaderPath(
                               argv0, kShaderVoxelObbDdaSharedAlignedNearClipFrag),
                           resolveShaderPath(argv0, kShaderVoxelObbDdaFrag));
    reflectionObbPass.setShaderPaths(resolveShaderPath(argv0, kShaderVoxelObbVert),
                                     resolveShaderPath(argv0, kShaderVoxelObbFullScreenVert),
                                     resolveShaderPath(
                                         argv0, kShaderVoxelObbSharedAlignedVert),
                                     resolveShaderPath(argv0, kShaderVoxelObbDdaHotFrag),
                                     resolveShaderPath(argv0, kShaderVoxelObbDdaOpaqueFrag),
                                     resolveShaderPath(
                                         argv0, kShaderVoxelObbDdaUnwrappedOpaqueFrag),
                                     resolveShaderPath(
                                         argv0, kShaderVoxelObbDdaNearClipSafeOpaqueFrag),
                                     resolveShaderPath(
                                         argv0,
                                         kShaderVoxelObbDdaSharedAlignedOpaqueFrag),
                                     resolveShaderPath(
                                         argv0,
                                         kShaderVoxelObbDdaSharedAlignedNearClipFrag),
                                     resolveShaderPath(argv0, kShaderVoxelObbDdaFrag));
}

PassCreateResourcesResult RendererPassResources::createResources(
    const PassCreateResourcesInfo& info)
{
    PassCreateResourcesResult result{};
    if (info.context == nullptr || info.commands == nullptr ||
        info.passCreateInfo == nullptr || info.passCreateInfo->commands == nullptr ||
        info.voxelSetLayout == VK_NULL_HANDLE || info.materialLayout == VK_NULL_HANDLE ||
        info.voxelPaletteBuffer == VK_NULL_HANDLE ||
        info.lights == nullptr || info.swapchain == nullptr ||
        info.createPixelInspectResources == nullptr)
    {
        result.fatalComponent = "Renderer";
        result.fatalReason = "Pass resource creation was invoked without required bindings.";
        return result;
    }
#if VOXEL_WITH_RUNTIME_UI
    if (info.runtimeUiFontAtlasImage == nullptr ||
        info.runtimeUiFontAtlasLabel == nullptr)
    {
        result.fatalComponent = "Renderer";
        result.fatalReason =
            "Runtime UI overlay creation was invoked without required font atlas inputs.";
        return result;
    }
#endif

    VulkanContext& ctx = *info.context;
    Commands& commands = *info.commands;
    PassCreateInfo& passCreateInfo = *info.passCreateInfo;
    resetAvailability();
    configureShaderPaths(info.argv0);

    beginRuntimePassLifecycleCharacterization("pass_create");
    const auto traceCreate = [](const char* name) {
        traceRuntimePassLifecycleEvent(name);
    };
    const auto fail = [&](const char* component,
                          const char* reason) -> PassCreateResourcesResult {
        result.fatalComponent = component;
        result.fatalReason = reason;
        endRuntimePassLifecycleCharacterization();
        return result;
    };

    shadow.create(ctx, passCreateInfo);
    traceCreate("shadow-map");
    gbuffer.setMaterialLayout(info.materialLayout);
    gbuffer.create(ctx, passCreateInfo);
    traceCreate("gbuffer");
    if (!foliage.create(ctx, gbuffer.renderPass(), gbuffer.frameSetLayout(),
                        info.voxelPaletteBuffer))
    {
        availability.foliagePass = false;
    }
    else
    {
        traceCreate("foliage");
    }
    voxelGbuffer.create(ctx, passCreateInfo, gbuffer);
    traceCreate("voxel-gbuffer");
    if (!obbPass.create(ctx, voxelGbuffer.renderPass(), gbuffer.extent(),
                        gbuffer.frameSetLayout(), info.voxelSetLayout,
                        static_cast<uint32_t>(GBufferPass::kGBufferCount),
                        info.waterVolumeBuffer))
    {
        return fail("OBBPass", "Voxel DDA resources are required for scene rendering.");
    }
    traceCreate("obb");
    waterVolumePrepass.setContainerBuffer(info.waterContainerBuffer);
    waterVolumePrepass.create(ctx, passCreateInfo, gbuffer);
    traceCreate("water-volume-prepass");

    const VkExtent2D shadowRayExtent =
        info.shadowRayExtent.width > 0 && info.shadowRayExtent.height > 0
            ? info.shadowRayExtent
            : passCreateInfo.extent;
    if (!shadowBuffer.create(ctx, shadowRayExtent.width, shadowRayExtent.height))
    {
        return fail("ShadowBuffer",
                    "Resolved shadow bindings require a backing shadow image.");
    }
    traceCreate("shadow-buffer");
    if (!localLightShadowBuffer.create(ctx, passCreateInfo.extent.width,
                                       passCreateInfo.extent.height))
    {
        return fail("LocalLightShadowBuffer",
                    "Local-light shadow resolve requires a backing shadow image.");
    }
    traceCreate("local-light-shadow-buffer");
    const std::filesystem::path blueNoisePath =
        info.assetRoot / "textures" / "blue_noise_128_rg.png";
    if (!blueNoiseTexture.create(ctx, blueNoisePath))
    {
        return fail("BlueNoiseTexture",
                    "Stochastic shadow and AO sampling require the runtime blue-noise texture.");
    }
    traceCreate("blue-noise");
    if (!auxiliaryTileListPass.create(ctx, passCreateInfo.extent))
    {
        availability.auxiliaryTileListPass = false;
        availability.shadowRayPass = false;
        availability.localLightShadowPass = false;
        availability.aoPass = false;
        result.ddaShadowsDisabled = true;
        result.localLightShadowsDisabled = true;
        result.localShadowBlurDisabled = true;
        result.ambientOcclusionDisabled = true;
        logWarning(
            "Renderer",
            "AuxiliaryTileListPass initialization failed; DDA shadows, local-light shadows, "
            "and ambient occlusion are disabled for this run.");
    }
    if (availability.auxiliaryTileListPass)
    {
        traceCreate("auxiliary-tile-list");
    }
    if (availability.auxiliaryTileListPass &&
        !shadowRayPass.create(ctx, info.voxelSetLayout))
    {
        availability.shadowRayPass = false;
        result.ddaShadowsDisabled = true;
        logWarning("Renderer",
                   "ShadowRayPass initialization failed; DDA shadows are disabled for this run.");
    }
    if (availability.shadowRayPass)
    {
        traceCreate("shadow-rays");
    }
    if (availability.auxiliaryTileListPass &&
        !localLightShadowPass.create(ctx, passCreateInfo.extent, info.voxelSetLayout))
    {
        availability.localLightShadowPass = false;
        result.localLightShadowsDisabled = true;
        result.localShadowBlurDisabled = true;
        logWarning("Renderer", "LocalLightShadowPass initialization failed; local light shadows "
                               "are disabled for this run.");
    }
    if (availability.localLightShadowPass)
    {
        traceCreate("local-light-shadows");
    }
    if (!localShadowBlur.create(ctx, passCreateInfo.extent))
    {
        availability.localShadowBlur = false;
        result.localShadowBlurDisabled = true;
        logWarning("Renderer",
                   "LocalShadowBlur initialization failed; local shadow blur is disabled.");
    }
    if (availability.localShadowBlur)
    {
        traceCreate("local-shadow-blur");
    }
    if (availability.auxiliaryTileListPass &&
        !aoPass.create(ctx,
                       info.aoRayExtent.width > 0 && info.aoRayExtent.height > 0
                           ? info.aoRayExtent
                           : passCreateInfo.extent,
                       info.voxelSetLayout))
    {
        availability.aoPass = false;
        result.ambientOcclusionDisabled = true;
        logWarning("Renderer",
                   "AOPass initialization failed; ambient occlusion is disabled and lighting "
                   "will use neutral AO.");
    }
    if (availability.aoPass)
    {
        traceCreate("ao-rays");
    }
    if (!shadowTemporalResolve.create(ctx, passCreateInfo.extent,
                                      info.shadowTemporalResolveConfig))
    {
        availability.shadowTemporalResolve = false;
        result.ddaShadowsDisabled = true;
        logWarning("Renderer",
                   "ShadowTemporalResolve initialization failed; DDA shadows are disabled for "
                   "this run.");
    }
    if (availability.shadowTemporalResolve)
    {
        traceCreate("shadow-resolve");
    }
    if (!shadowDenoisePass.create(ctx, passCreateInfo.extent))
    {
        availability.shadowDenoisePass = false;
        logWarning("Renderer",
                   "ShadowDenoisePass initialization failed; post-denoise is disabled.");
    }
    if (availability.shadowDenoisePass)
    {
        traceCreate("shadow-denoise");
    }
    if (!localLightShadowTemporalResolve.create(ctx, passCreateInfo.extent,
                                                info.localShadowResolveConfig))
    {
        availability.localLightShadowResolve = false;
        result.localLightShadowsDisabled = true;
        result.localShadowBlurDisabled = true;
        logWarning("Renderer",
                   "LocalLightShadowTemporalResolve initialization failed; local light shadows "
                   "are disabled for this run.");
    }
    if (availability.localLightShadowResolve)
    {
        traceCreate("local-shadow-resolve");
    }
    if (!aoTemporalResolve.create(ctx, passCreateInfo.extent,
                                  info.aoTemporalResolveConfig))
    {
        availability.aoTemporalResolve = false;
        result.ambientOcclusionDisabled = true;
        logWarning("Renderer",
                   "AOTemporalResolve initialization failed; ambient occlusion is disabled and "
                   "lighting will use neutral AO.");
    }
    if (availability.aoTemporalResolve)
    {
        traceCreate("ao-resolve");
    }
    DepthHistoryCreateInfo depthHistoryInfo{};
    depthHistoryInfo.context = &ctx;
    depthHistoryInfo.commands = &commands;
    depthHistoryInfo.extent = passCreateInfo.extent;
    depthHistoryInfo.format = gbuffer.depthFormat();
    if (!createDepthHistory(depthHistoryInfo))
    {
        return fail("DepthHistory",
                    "Temporal resolve requires a readable depth history image.");
    }
    traceCreate("depth-history");
    depthHistory.index = 0;

    lighting.setWaterVolumes(info.waterVolumeBuffer, info.waterVolumeCount);
    lighting.create(ctx, passCreateInfo, gbuffer, *info.lights, shadow.map(), shadowBuffer,
                    currentShadowResolvedView(), currentShadowResolvedSampler(),
                    currentAoResolvedView(), currentAoResolvedSampler(), currentAoRawView(),
                    currentAoRawSampler(), currentLocalShadowResolvedView(),
                    currentLocalShadowResolvedSampler());
    traceCreate("lighting");
    waterBody.setWaterVolumeBuffer(info.waterVolumeBuffer);
    waterBody.create(ctx, passCreateInfo, gbuffer, lighting);
    traceCreate("water-body");

    reflectionExtent = quarterExtent(passCreateInfo.extent);
    PassCreateInfo reflectionCi = passCreateInfo;
    reflectionCi.extent = reflectionExtent;
    reflectionGbuffer.setMaterialLayout(info.materialLayout);
    reflectionGbuffer.create(ctx, reflectionCi);
    traceCreate("reflection-gbuffer");
    reflectionVoxelGbuffer.create(ctx, reflectionCi, reflectionGbuffer);
    traceCreate("reflection-voxel-gbuffer");
    if (!reflectionObbPass.create(ctx, reflectionVoxelGbuffer.renderPass(),
                                  reflectionGbuffer.extent(),
                                  reflectionGbuffer.frameSetLayout(), info.voxelSetLayout,
                                  static_cast<uint32_t>(GBufferPass::kGBufferCount),
                                  info.waterVolumeBuffer))
    {
        return fail("Reflection OBBPass", "Reflection lighting requires voxel DDA resources.");
    }
    traceCreate("reflection-obb");
    reflectionWaterVolumePrepass.setContainerBuffer(info.waterContainerBuffer);
    reflectionWaterVolumePrepass.create(ctx, reflectionCi, reflectionGbuffer);
    traceCreate("reflection-water-volume-prepass");
    reflectionLighting.setWaterVolumes(info.waterVolumeBuffer, info.waterVolumeCount);
    reflectionLighting.create(ctx, reflectionCi, reflectionGbuffer, *info.lights, shadow.map(),
                              shadowBuffer, currentShadowResolvedView(),
                              currentShadowResolvedSampler(), currentAoResolvedView(),
                              currentAoResolvedSampler(), currentAoRawView(),
                              currentAoRawSampler(), currentLocalShadowResolvedView(),
                              currentLocalShadowResolvedSampler());
    traceCreate("reflection-lighting");

    water.setWaterVolumeBuffer(info.waterVolumeBuffer);
    waterV2.setWaterVolumeBuffer(info.waterVolumeBuffer);
    glass.setWaterVolumeBuffer(info.waterVolumeBuffer);
    voxelGlassRefract.setWaterVolumeBuffer(info.waterVolumeBuffer);
    water.create(ctx, passCreateInfo, gbuffer, lighting, &reflectionLighting);
    traceCreate("water");
    waterV2.create(ctx, passCreateInfo, gbuffer, lighting, &reflectionLighting);
    traceCreate("water-v2");
    glass.create(ctx, passCreateInfo, gbuffer, lighting, water);
    traceCreate("glass");
    voxelGlassRefract.create(ctx, passCreateInfo, gbuffer, lighting);
    traceCreate("voxel-glass-refract");
    bloom.create(ctx, passCreateInfo, lighting);
    traceCreate("bloom");
    taa.create(ctx, passCreateInfo, gbuffer, lighting, bloom);
    traceCreate("taa");
    taa.setEnabled(info.taaEnabled);
    spatialUpscale.create(ctx, passCreateInfo, info.swapchain->extent);
    traceCreate("spatial-upscale");
    composite.setWaterVolumeBuffer(info.waterVolumeBuffer);
    composite.create(ctx, passCreateInfo, *info.swapchain);
    traceCreate("composite-ui");
    composite.updateDescriptorSets(ctx, gbuffer, lighting, shadow.map(), bloom, taa,
                                   blueNoiseTexture, &waterVolumePrepass);
#if VOXEL_WITH_RUNTIME_UI
    uiOverlay.setFontAtlasImage(*info.runtimeUiFontAtlasImage,
                                *info.runtimeUiFontAtlasLabel);
    uiOverlay.create(ctx, commands, info.swapchain->renderPass);
    traceCreate("runtime-ui-overlay");
#endif

    info.createPixelInspectResources(info.user);
    traceCreate("pixel-inspect");
    endRuntimePassLifecycleCharacterization();

    result.ok = true;
    return result;
}

bool RendererPassResources::createDepthHistory(const DepthHistoryCreateInfo& info)
{
    if (info.context == nullptr || info.commands == nullptr ||
        info.format == VK_FORMAT_UNDEFINED || info.extent.width == 0 ||
        info.extent.height == 0)
    {
        return false;
    }

    VulkanContext& ctx = *info.context;
    Commands& commands = *info.commands;
    for (int i = 0; i < 2; ++i)
    {
        createImage(ctx, info.extent.width, info.extent.height, info.format,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    depthHistory.images[i], depthHistory.memory[i]);
        depthHistory.views[i] = createImageView(ctx.device, depthHistory.images[i],
                                                info.format, VK_IMAGE_ASPECT_DEPTH_BIT);
        transitionImageLayout(ctx, commands, depthHistory.images[i], VK_IMAGE_ASPECT_DEPTH_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    return ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampNearest,
                                depthHistory.sampler);
}

void RendererPassResources::destroyDepthHistory(VulkanContext& ctx)
{
    depthHistory.sampler = VK_NULL_HANDLE;

    for (int i = 0; i < 2; ++i)
    {
        if (depthHistory.views[i] != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx.device, depthHistory.views[i], nullptr);
            depthHistory.views[i] = VK_NULL_HANDLE;
        }
        if (depthHistory.images[i] != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx.device, depthHistory.images[i], nullptr);
            depthHistory.images[i] = VK_NULL_HANDLE;
        }
        if (depthHistory.memory[i] != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, depthHistory.memory[i], nullptr);
            depthHistory.memory[i] = VK_NULL_HANDLE;
        }
    }
}

bool RendererPassResources::destroyResources(const PassDestroyInfo& info)
{
    if (info.context == nullptr || info.destroyPixelInspectResources == nullptr)
    {
        return false;
    }

    VulkanContext& ctx = *info.context;
    beginRuntimePassLifecycleCharacterization("pass_destroy");
    const auto traceDestroy = [](const char* name) {
        traceRuntimePassLifecycleEvent(name);
    };

    info.destroyPixelInspectResources(info.user);
    traceDestroy("pixel-inspect");

#if VOXEL_WITH_RUNTIME_UI
    uiOverlay.destroy(ctx);
    traceDestroy("runtime-ui-overlay");
#endif
    composite.destroy(ctx);
    traceDestroy("composite-ui");
    spatialUpscale.destroy(ctx);
    traceDestroy("spatial-upscale");
    taa.destroy(ctx);
    traceDestroy("taa");
    bloom.destroy(ctx);
    traceDestroy("bloom");
    voxelGlassRefract.destroy(ctx);
    traceDestroy("voxel-glass-refract");
    glass.destroy(ctx);
    traceDestroy("glass");
    waterV2.destroy(ctx);
    traceDestroy("water-v2");
    water.destroy(ctx);
    traceDestroy("water");
    waterBody.destroy(ctx);
    traceDestroy("water-body");
    reflectionLighting.destroy(ctx);
    traceDestroy("reflection-lighting");
    reflectionWaterVolumePrepass.destroy(ctx);
    traceDestroy("reflection-water-volume-prepass");
    reflectionObbPass.destroy(ctx);
    traceDestroy("reflection-obb");
    reflectionVoxelGbuffer.destroy(ctx);
    traceDestroy("reflection-voxel-gbuffer");
    reflectionGbuffer.destroy(ctx);
    traceDestroy("reflection-gbuffer");
    lighting.destroy(ctx);
    traceDestroy("lighting");

    shadowTemporalResolve.destroy(ctx);
    traceDestroy("shadow-resolve");
    shadowDenoisePass.destroy(ctx);
    traceDestroy("shadow-denoise");
    localLightShadowTemporalResolve.destroy(ctx);
    traceDestroy("local-shadow-resolve");
    localLightShadowPass.destroy(ctx);
    traceDestroy("local-light-shadows");
    localShadowBlur.destroy(ctx);
    traceDestroy("local-shadow-blur");
    shadowRayPass.destroy(ctx);
    traceDestroy("shadow-rays");
    blueNoiseTexture.destroy(ctx);
    traceDestroy("blue-noise");
    localLightShadowBuffer.destroy(ctx);
    traceDestroy("local-light-shadow-buffer");
    shadowBuffer.destroy(ctx);
    traceDestroy("shadow-buffer");
    aoTemporalResolve.destroy(ctx);
    traceDestroy("ao-resolve");
    aoPass.destroy(ctx);
    traceDestroy("ao-rays");
    auxiliaryTileListPass.destroy(ctx);
    traceDestroy("auxiliary-tile-list");
    destroyDepthHistory(ctx);
    traceDestroy("depth-history");

    obbPass.destroy(ctx);
    traceDestroy("obb");
    voxelGbuffer.destroy(ctx);
    traceDestroy("voxel-gbuffer");
    waterVolumePrepass.destroy(ctx);
    traceDestroy("water-volume-prepass");
    foliage.destroy(ctx);
    if (availability.foliagePass)
    {
        traceDestroy("foliage");
    }
    gbuffer.destroy(ctx);
    traceDestroy("gbuffer");
    shadow.destroy(ctx);
    traceDestroy("shadow-map");
    endRuntimePassLifecycleCharacterization();
    return true;
}

void RendererPassResources::destroySwapchainDependentResources(VulkanContext& ctx)
{
#if VOXEL_WITH_RUNTIME_UI
    uiOverlay.destroy(ctx);
#endif
    composite.destroy(ctx);
    spatialUpscale.destroy(ctx);
    taa.destroy(ctx);
    bloom.destroy(ctx);
    glass.destroy(ctx);
    waterV2.destroy(ctx);
    water.destroy(ctx);
    waterBody.destroy(ctx);
    reflectionLighting.destroy(ctx);
    reflectionWaterVolumePrepass.destroy(ctx);
    reflectionObbPass.destroy(ctx);
    reflectionVoxelGbuffer.destroy(ctx);
    reflectionGbuffer.destroy(ctx);
    waterVolumePrepass.destroy(ctx);
    obbPass.destroy(ctx);
    voxelGbuffer.destroy(ctx);
}

bool RendererPassResources::recreatePrimaryTargets(const PrimaryTargetResizeInfo& info)
{
    if (info.context == nullptr || info.passCreateInfo == nullptr ||
        info.passCreateInfo->commands == nullptr ||
        info.voxelSetLayout == VK_NULL_HANDLE)
    {
        return false;
    }

    VulkanContext& ctx = *info.context;
    PassCreateInfo& passCreateInfo = *info.passCreateInfo;
    gbuffer.onResize(ctx, passCreateInfo);
    if (availability.foliagePass &&
        !foliage.recreatePipeline(ctx, gbuffer.renderPass(),
                                  gbuffer.frameSetLayout()))
    {
        availability.foliagePass = false;
        logWarning("Renderer",
                   "FoliagePass resize failed; the voxel foliage fallback is active.");
    }
    voxelGbuffer.create(ctx, passCreateInfo, gbuffer);
    if (!obbPass.create(ctx, voxelGbuffer.renderPass(), gbuffer.extent(),
                        gbuffer.frameSetLayout(), info.voxelSetLayout,
                        static_cast<uint32_t>(GBufferPass::kGBufferCount),
                        info.waterVolumeBuffer))
    {
        return false;
    }
    waterVolumePrepass.setContainerBuffer(info.waterContainerBuffer);
    waterVolumePrepass.create(ctx, passCreateInfo, gbuffer);
    const VkExtent2D shadowRayExtent =
        info.shadowRayExtent.width > 0 && info.shadowRayExtent.height > 0
            ? info.shadowRayExtent
            : passCreateInfo.extent;
    shadowBuffer.resize(ctx, shadowRayExtent.width, shadowRayExtent.height);
    localLightShadowBuffer.resize(ctx, passCreateInfo.extent.width,
                                  passCreateInfo.extent.height);

    depthHistory.index = 0;
    destroyDepthHistory(ctx);
    DepthHistoryCreateInfo depthHistoryInfo{};
    depthHistoryInfo.context = &ctx;
    depthHistoryInfo.commands = passCreateInfo.commands;
    depthHistoryInfo.extent = passCreateInfo.extent;
    depthHistoryInfo.format = gbuffer.depthFormat();
    return createDepthHistory(depthHistoryInfo);
}

OptionalTargetResizeResult RendererPassResources::resizeOptionalTargets(VulkanContext& ctx,
                                                                        VkExtent2D extent,
                                                                        VkExtent2D aoRayExtent)
{
    OptionalTargetResizeResult result{};

    if (availability.auxiliaryTileListPass &&
        !auxiliaryTileListPass.resize(ctx, extent))
    {
        availability.auxiliaryTileListPass = false;
        availability.shadowRayPass = false;
        availability.localLightShadowPass = false;
        availability.aoPass = false;
        availability.aoTemporalResolve = false;
        result.ddaShadowsDisabled = true;
        result.localLightShadowsDisabled = true;
        result.ambientOcclusionDisabled = true;
        logWarning(
            "Renderer",
            "AuxiliaryTileListPass resize failed; DDA shadows, local-light shadows, and "
            "ambient occlusion are disabled for the rest of this run.");
    }
    if (availability.shadowTemporalResolve)
    {
        if (!shadowTemporalResolve.resize(ctx, extent))
        {
            availability.shadowTemporalResolve = false;
            result.ddaShadowsDisabled = true;
            logWarning("Renderer",
                       "ShadowTemporalResolve resize failed; DDA shadows are disabled for the "
                       "rest of this run.");
        }
    }
    if (availability.shadowDenoisePass)
    {
        if (!shadowDenoisePass.resize(ctx, extent))
        {
            availability.shadowDenoisePass = false;
            logWarning("Renderer",
                       "ShadowDenoisePass resize failed; post-denoise is disabled for the rest "
                       "of this run.");
        }
    }
    if (availability.localLightShadowPass)
    {
        localLightShadowPass.resize(ctx, extent);
    }
    if (availability.localShadowBlur)
    {
        localShadowBlur.resize(ctx, extent);
    }
    if (availability.localLightShadowResolve)
    {
        if (!localLightShadowTemporalResolve.resize(ctx, extent))
        {
            availability.localLightShadowResolve = false;
            result.localLightShadowsDisabled = true;
            logWarning("Renderer",
                       "LocalLightShadowTemporalResolve resize failed; local light shadows are "
                       "disabled for the rest of this run.");
        }
    }
    if (availability.aoPass)
    {
        const VkExtent2D targetExtent =
            aoRayExtent.width > 0 && aoRayExtent.height > 0 ? aoRayExtent : extent;
        if (!aoPass.resize(ctx, targetExtent))
        {
            availability.aoPass = false;
            availability.aoTemporalResolve = false;
            result.ambientOcclusionDisabled = true;
            logWarning("Renderer",
                       "AOPass resize failed; ambient occlusion is disabled and lighting will "
                       "use neutral AO for the rest of this run.");
        }
    }
    if (availability.aoTemporalResolve)
    {
        if (!aoTemporalResolve.resize(ctx, extent))
        {
            availability.aoTemporalResolve = false;
            result.ambientOcclusionDisabled = true;
            logWarning("Renderer",
                       "AOTemporalResolve resize failed; ambient occlusion is disabled and "
                       "lighting will use neutral AO for the rest of this run.");
        }
    }

    return result;
}

bool RendererPassResources::recreateMainLightingTargets(const MainLightingResizeInfo& info)
{
    if (info.context == nullptr || info.passCreateInfo == nullptr)
    {
        return false;
    }

    VulkanContext& ctx = *info.context;
    PassCreateInfo& passCreateInfo = *info.passCreateInfo;
    lighting.setWaterVolumes(info.waterVolumeBuffer, info.waterVolumeCount);
    lighting.onResize(ctx, passCreateInfo, gbuffer, shadow.map(), shadowBuffer,
                      currentShadowResolvedView(), currentShadowResolvedSampler(),
                      currentAoResolvedView(), currentAoResolvedSampler(), currentAoRawView(),
                      currentAoRawSampler(), currentLocalShadowResolvedView(),
                      currentLocalShadowResolvedSampler());
    waterBody.setWaterVolumeBuffer(info.waterVolumeBuffer);
    waterBody.create(ctx, passCreateInfo, gbuffer, lighting);
    return true;
}

bool RendererPassResources::recreateReflectionTargets(const ReflectionTargetResizeInfo& info)
{
    if (info.context == nullptr || info.passCreateInfo == nullptr ||
        info.passCreateInfo->commands == nullptr || info.lights == nullptr ||
        info.materialLayout == VK_NULL_HANDLE || info.voxelSetLayout == VK_NULL_HANDLE)
    {
        return false;
    }

    VulkanContext& ctx = *info.context;
    PassCreateInfo& passCreateInfo = *info.passCreateInfo;
    reflectionExtent = quarterExtent(passCreateInfo.extent);
    PassCreateInfo reflectionCi = passCreateInfo;
    reflectionCi.extent = reflectionExtent;
    reflectionGbuffer.setMaterialLayout(info.materialLayout);
    reflectionGbuffer.create(ctx, reflectionCi);
    reflectionVoxelGbuffer.create(ctx, reflectionCi, reflectionGbuffer);
    if (!reflectionObbPass.create(ctx, reflectionVoxelGbuffer.renderPass(),
                                  reflectionGbuffer.extent(),
                                  reflectionGbuffer.frameSetLayout(), info.voxelSetLayout,
                                  static_cast<uint32_t>(GBufferPass::kGBufferCount),
                                  info.waterVolumeBuffer))
    {
        return false;
    }
    reflectionWaterVolumePrepass.setContainerBuffer(info.waterContainerBuffer);
    reflectionWaterVolumePrepass.create(ctx, reflectionCi, reflectionGbuffer);
    reflectionLighting.setWaterVolumes(info.waterVolumeBuffer, info.waterVolumeCount);
    reflectionLighting.create(ctx, reflectionCi, reflectionGbuffer, *info.lights, shadow.map(),
                              shadowBuffer, currentShadowResolvedView(),
                              currentShadowResolvedSampler(), currentAoResolvedView(),
                              currentAoResolvedSampler(), currentAoRawView(),
                              currentAoRawSampler(), currentLocalShadowResolvedView(),
                              currentLocalShadowResolvedSampler());
    return true;
}

bool RendererPassResources::recreateWaterPostTargets(const WaterPostResizeInfo& info)
{
    if (info.context == nullptr || info.passCreateInfo == nullptr || info.swapchain == nullptr)
    {
        return false;
    }

    VulkanContext& ctx = *info.context;
    PassCreateInfo& passCreateInfo = *info.passCreateInfo;
    water.setWaterVolumeBuffer(info.waterVolumeBuffer);
    waterV2.setWaterVolumeBuffer(info.waterVolumeBuffer);
    glass.setWaterVolumeBuffer(info.waterVolumeBuffer);
    voxelGlassRefract.setWaterVolumeBuffer(info.waterVolumeBuffer);
    water.create(ctx, passCreateInfo, gbuffer, lighting, &reflectionLighting);
    waterV2.create(ctx, passCreateInfo, gbuffer, lighting, &reflectionLighting);
    glass.create(ctx, passCreateInfo, gbuffer, lighting, water);
    voxelGlassRefract.create(ctx, passCreateInfo, gbuffer, lighting);
    bloom.create(ctx, passCreateInfo, lighting);
    taa.create(ctx, passCreateInfo, gbuffer, lighting, bloom);
    taa.setEnabled(info.taaEnabled);
    spatialUpscale.create(ctx, passCreateInfo, info.swapchain->extent);
    composite.setWaterVolumeBuffer(info.waterVolumeBuffer);
    composite.create(ctx, passCreateInfo, *info.swapchain);
    composite.updateDescriptorSets(ctx, gbuffer, lighting, shadow.map(), bloom, taa,
                                   blueNoiseTexture, &waterVolumePrepass);
    return true;
}

#if VOXEL_WITH_RUNTIME_UI
bool RendererPassResources::recreateRuntimeUiOverlay(
    const RuntimeUiOverlayResizeInfo& info)
{
    if (info.context == nullptr || info.commands == nullptr ||
        info.renderPass == VK_NULL_HANDLE || info.fontAtlasImage == nullptr ||
        info.fontAtlasLabel == nullptr)
    {
        return false;
    }

    uiOverlay.setFontAtlasImage(*info.fontAtlasImage, *info.fontAtlasLabel);
    uiOverlay.create(*info.context, *info.commands, info.renderPass);
    return true;
}
#endif

void RendererPassResources::updateLightingShadowBindings(VulkanContext& ctx,
                                                         uint32_t frameIndex)
{
    lighting.updateShadowBindings(ctx, frameIndex, shadowBuffer, currentShadowResolvedView(),
                                  currentShadowResolvedSampler(), currentAoResolvedView(),
                                  currentAoResolvedSampler(), currentAoRawView(),
                                  currentAoRawSampler(), currentLocalShadowResolvedView(),
                                  currentLocalShadowResolvedSampler());
}

void RendererPassResources::updateReflectionLightingShadowBindings(VulkanContext& ctx,
                                                                   uint32_t frameIndex)
{
    reflectionLighting.updateShadowBindings(
        ctx, frameIndex, shadowBuffer, currentShadowResolvedView(),
        currentShadowResolvedSampler(), currentAoResolvedView(), currentAoResolvedSampler(),
        currentAoRawView(), currentAoRawSampler(), currentLocalShadowResolvedView(),
        currentLocalShadowResolvedSampler());
}

}  // namespace engine::render
