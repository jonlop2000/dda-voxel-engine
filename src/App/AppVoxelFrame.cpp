#include "App/App.h"
#include "App/AppRenderHelpers.h"
#include "App/AppSceneVolume.h"
#include "App/AppVoxelDraw.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/vec2.hpp>

#include "Core/Logger.h"
#include "engine/render/AmbientOcclusionQualityTier.h"
#include "engine/render/AuxiliaryRayResolution.h"
#include "engine/render/FrameInputs.h"
#include "engine/render/PaintedClouds.h"
#include "engine/render/PassRegistry.h"
#include "engine/render/RendererPassResources.h"
#include "engine/render/voxel/VoxelRenderResources.h"
#include "engine/scene/ScenePresentationProfile.h"
#include "engine/scene/WorldStateView.h"

namespace
{

uint32_t metricsHashU32(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352d;
    v ^= v >> 15;
    v *= 0x846ca68b;
    v ^= v >> 16;
    return v;
}

glm::ivec2 computeMetricsSampleOffset(uint32_t stride, int pattern, uint64_t frameIndex)
{
    if (stride <= 1)
    {
        return glm::ivec2(0);
    }

    const uint32_t s = stride;
    switch (pattern)
    {
    case 1:
    {
        const uint64_t idx = frameIndex % (static_cast<uint64_t>(s) * s);
        const uint32_t ox = static_cast<uint32_t>(idx % s);
        const uint32_t oy = static_cast<uint32_t>((idx / s) % s);
        return glm::ivec2(static_cast<int>(ox), static_cast<int>(oy));
    }
    case 2:
    {
        const uint32_t h = metricsHashU32(static_cast<uint32_t>(frameIndex));
        const uint32_t ox = h % s;
        const uint32_t oy = (h >> 16) % s;
        return glm::ivec2(static_cast<int>(ox), static_cast<int>(oy));
    }
    default:
        return glm::ivec2(0);
    }
}

}  // namespace

void App::recordShadowMapPass(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    engine::render::FrameInputs& frameInputs,
    const ShadowPass::FrameUbo& shadowUbo)
{
    if (!frameInputs.renderCascadedShadows)
    {
        return;
    }

    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    const uint32_t gpuScopeShadowMap = gpuProfiler_.beginScope(cmd, "Shadow Map");
    recordFramePass("shadow-map", [&]() { passes.shadow.record(ctx_, fc, shadowUbo, shadowObjects_); });
    app::render::recordShadowToLightingBarrier(cmd, passes.shadow.map());
    gpuProfiler_.endScope(cmd, gpuScopeShadowMap);
}

void App::recordGBufferMeshPass(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    engine::render::FrameInputs& frameInputs,
    const GBufferPass::FrameUbo& gbufferUbo)
{
    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    const uint32_t gpuScopeGbufferMesh = gpuProfiler_.beginScope(cmd, "GBuffer Mesh");
    struct FoliageDrawContext
    {
        App* app = nullptr;
        engine::render::FoliagePass* pass = nullptr;
    } foliageDrawContext{this, &passes.foliage};
    GBufferPass::ExtraDrawCall foliageDraw{};
    foliageDraw.user = &foliageDrawContext;
    foliageDraw.fn = [](VkCommandBuffer drawCmd, VkDescriptorSet frameSet, void* user) {
        auto& draw = *static_cast<FoliageDrawContext*>(user);
        if (!draw.pass->drawable())
        {
            return;
        }
        const uint32_t scope = draw.app->gpuProfiler_.beginScope(drawCmd, "Foliage");
        draw.pass->record(drawCmd, frameSet,
                          engine::render::FoliagePass::DrawSettings{});
        draw.app->gpuProfiler_.endScope(drawCmd, scope);
    };
    recordFramePass("gbuffer", [&]() {
        passes.gbuffer.record(ctx_, fc, gbufferUbo, sceneObjects_, foliageDraw);
    });
    gpuProfiler_.endScope(cmd, gpuScopeGbufferMesh);
}

void App::recordWaterVolumePrepass(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    engine::render::FrameInputs& frameInputs,
    const glm::mat4& invViewProjJittered)
{
    if (!(waterSettings_.useWaterV2_ && input_.waterEnabled() && waterContainerMgr_.count() > 0))
    {
        return;
    }

    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    WaterVolumePrepass::FrameUbo waterVolumeUbo{};
    waterVolumeUbo.invViewProj = invViewProjJittered;
    waterVolumeUbo.camPos = glm::vec4(camera_.position, 1.0f);
    waterVolumeUbo.params = glm::vec4(static_cast<float>(waterContainerMgr_.count()),
                                      0.0f, 0.0f, 0.0f);

    const uint32_t gpuScopeWaterVolume = gpuProfiler_.beginScope(cmd, "Water Volume Prepass");
    if (input_.gbufferMode() == 7)
    {
        passes.waterVolumePrepass.recordLegacyCopy(cmd, fc.frameIndex, passes.gbuffer);
    }
    recordFramePass("water-volume-prepass", [&]() {
        passes.waterVolumePrepass.record(ctx_, fc, waterVolumeUbo);
    });
    gpuProfiler_.endScope(cmd, gpuScopeWaterVolume);
}

void App::recordPrimaryVoxelDdaPasses(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    const engine::render::VoxelRenderResources::VolumeDrawPlan& voxelDrawPlan,
    engine::render::FrameInputs& frameInputs,
    float animationTimeSeconds)
{
    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    const uint32_t obbDebugMode =
        voxelDebugSettings_.voxelDdaAdvancedDebug_ > 0
            ? static_cast<uint32_t>(9 + voxelDebugSettings_.voxelDdaAdvancedDebug_)
            : static_cast<uint32_t>(voxelDebugSettings_.voxelDdaDebugMode_);
    passes.obbPass.setDebugMode(obbDebugMode);

    const bool effectiveVoxelDdaSkipEnabled =
        voxelDebugSettings_.voxelDdaSkipEnabled_ && !automationDisableEmptySkip_;
    passes.obbPass.setSkipSettings(effectiveVoxelDdaSkipEnabled,
                                   static_cast<uint32_t>(voxelDebugSettings_.voxelDdaSkipMip_));
    passes.obbPass.setHeatmapSettings(static_cast<uint32_t>(voxelDebugSettings_.voxelHeatmapMode_),
                                      voxelDebugSettings_.voxelHeatmapMax_,
                                      voxelDebugSettings_.voxelHeatmapGamma_);
    passes.obbPass.setNormalEdgeSmoothing(voxelDebugSettings_.voxelNormalEdgeSmoothing_);
    passes.obbPass.setPixelEdgeShadowStrength(voxelDebugSettings_.voxelPixelEdgeShadowStrength_);
    passes.obbPass.setPaintedSurfaceSettings(
        voxelDebugSettings_.voxelCavityStrength_,
        voxelDebugSettings_.voxelPaintedMaterialStrength_);
    passes.obbPass.setCellVariationSettings(voxelDebugSettings_.voxelCellVariation_);
    passes.obbPass.setTime(animationTimeSeconds);

    const uint32_t metricsStride =
        static_cast<uint32_t>(std::max(1, diagnosticsSettings_.voxelMetricsSampleStride_));
    if (diagnosticsSettings_.voxelMetricsEnabled_)
    {
        voxelMetricsSampleOffset_ = computeMetricsSampleOffset(metricsStride,
                                                               voxelMetricsSamplePattern_,
                                                               voxelMetricsFrameIndex_);
    }
    else
    {
        voxelMetricsSampleOffset_ = glm::ivec2(0);
    }
    passes.obbPass.setMetricsSettings(diagnosticsSettings_.voxelMetricsEnabled_, metricsStride,
                                      static_cast<uint32_t>(voxelMetricsSampleOffset_.x),
                                      static_cast<uint32_t>(voxelMetricsSampleOffset_.y));

    if (diagnosticsSettings_.voxelMetricsEnabled_)
    {
        passes.obbPass.clearMetrics(cmd);
    }

    const uint32_t gpuScopeVoxelDda = gpuProfiler_.beginScope(cmd, "Voxel DDA");
    if (!voxelDrawPlan.mainDdaVolumes.empty())
    {
        app::render::VoxelDrawBatchData voxelBatchData{};
        voxelBatchData.pass = &passes.obbPass;
        voxelBatchData.volumes = voxelDrawPlan.mainDdaVolumes;

        VoxelGBufferPass::ExtraDrawCall voxelDraw{};
        voxelDraw.fn = app::render::drawVoxelBatchCallback;
        voxelDraw.user = &voxelBatchData;

        recordFramePass("voxel-gbuffer", [&]() {
            passes.voxelGbuffer.record(ctx_, fc, passes.gbuffer.frameSet(fc.frameIndex),
                                       voxelDraw);
        });
    }
    gpuProfiler_.endScope(cmd, gpuScopeVoxelDda);

    if (diagnosticsSettings_.voxelMetricsEnabled_)
    {
        passes.obbPass.recordMetricsCopy(cmd, fc.frameIndex);
    }

    recordPixelInspectCopy(cmd, fc.frameIndex, obbDebugMode);
}

void App::recordReflectionVoxelDdaPasses(
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    const std::vector<engine::render::VoxelRenderResources::VolumeDrawPlan::Entry>& staticVolumes,
    engine::render::FrameInputs& frameInputs,
    bool effectiveVoxelDdaSkipEnabled,
    float animationTimeSeconds)
{
    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    passes.reflectionObbPass.setDebugMode(0);
    passes.reflectionObbPass.setSkipSettings(effectiveVoxelDdaSkipEnabled,
                                             static_cast<uint32_t>(voxelDebugSettings_.voxelDdaSkipMip_));
    passes.reflectionObbPass.setHeatmapSettings(0u, voxelDebugSettings_.voxelHeatmapMax_,
                                                voxelDebugSettings_.voxelHeatmapGamma_);
    passes.reflectionObbPass.setMetricsSettings(false, 1u, 0u, 0u);
    passes.reflectionObbPass.setNormalEdgeSmoothing(voxelDebugSettings_.voxelNormalEdgeSmoothing_);
    passes.reflectionObbPass.setPixelEdgeShadowStrength(voxelDebugSettings_.voxelPixelEdgeShadowStrength_);
    passes.reflectionObbPass.setPaintedSurfaceSettings(
        voxelDebugSettings_.voxelCavityStrength_,
        voxelDebugSettings_.voxelPaintedMaterialStrength_);
    passes.reflectionObbPass.setCellVariationSettings(voxelDebugSettings_.voxelCellVariation_);
    passes.reflectionObbPass.setTime(animationTimeSeconds);
    const float reflectionDdaClipBias = 0.05f;
    passes.reflectionObbPass.setReflectionClip(waterSettings_.waterLevel_ + reflectionDdaClipBias, true);
    passes.reflectionObbPass.setWaterVolumeCount(0);

    if (staticVolumes.empty())
    {
        return;
    }

    app::render::VoxelDrawBatchData reflectionVoxelBatchData{};
    reflectionVoxelBatchData.pass = &passes.reflectionObbPass;
    reflectionVoxelBatchData.volumes = staticVolumes;

    VoxelGBufferPass::ExtraDrawCall reflectionVoxelDraw{};
    reflectionVoxelDraw.fn = app::render::drawVoxelBatchCallback;
    reflectionVoxelDraw.user = &reflectionVoxelBatchData;

    recordFramePass("reflection-voxel-gbuffer", [&]() {
        passes.reflectionVoxelGbuffer.record(
            ctx_, fc, passes.reflectionGbuffer.frameSet(fc.frameIndex), reflectionVoxelDraw);
    });
}

void App::recordPlanarReflectionPasses(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    const std::vector<engine::render::VoxelRenderResources::VolumeDrawPlan::Entry>& staticVolumes,
    engine::render::FrameInputs& frameInputs,
    const glm::vec3& reflectedCamPos,
    const glm::mat4& reflectedView,
    const glm::mat4& reflectedProj,
    const glm::mat4& reflectedViewProj,
    const std::array<glm::mat4, kShadowCascades>& lightViewProjs,
    const glm::vec4& lightDir,
    const glm::vec4& lightColor,
    const glm::vec4& cascadeSplits,
    const glm::vec4& shadowMapSize,
    const glm::vec4& caustics0,
    const glm::vec4& caustics1,
    bool effectiveVoxelDdaSkipEnabled,
    float animationTimeSeconds)
{
    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    const uint32_t gpuScopePlanar = gpuProfiler_.beginScope(cmd, "Planar Reflection");

    GBufferPass::FrameUbo reflectionGbufferUbo{};
    reflectionGbufferUbo.view = reflectedView;
    reflectionGbufferUbo.proj = reflectedProj;
    reflectionGbufferUbo.viewProj = reflectedViewProj;
    reflectionGbufferUbo.viewProjUnjittered = reflectedViewProj;
    reflectionGbufferUbo.prevViewProjUnjittered = reflectedViewProj;
    reflectionGbufferUbo.prevViewProj = reflectedViewProj;
    reflectionGbufferUbo.invViewProjUnjittered = glm::inverse(reflectedViewProj);
    reflectionGbufferUbo.cameraWorld = glm::vec4(reflectedCamPos, 1.0f);
    reflectionGbufferUbo.renderSize =
        glm::vec2(static_cast<float>(passes.reflectionExtent.width),
                  static_cast<float>(passes.reflectionExtent.height));
    reflectionGbufferUbo.invRenderSize =
        glm::vec2(passes.reflectionExtent.width > 0
                      ? 1.0f / static_cast<float>(passes.reflectionExtent.width)
                      : 1.0f,
                  passes.reflectionExtent.height > 0
                      ? 1.0f / static_cast<float>(passes.reflectionExtent.height)
                      : 1.0f);
    struct ReflectionFoliageDrawContext
    {
        engine::render::FoliagePass* pass = nullptr;
        float clipY = 0.0f;
    } reflectionFoliageDrawContext{&passes.foliage,
                                   waterSettings_.waterLevel_ + 0.05f};
    GBufferPass::ExtraDrawCall reflectionFoliageDraw{};
    reflectionFoliageDraw.user = &reflectionFoliageDrawContext;
    reflectionFoliageDraw.fn = [](VkCommandBuffer drawCmd, VkDescriptorSet frameSet,
                                  void* user) {
        const auto& draw = *static_cast<ReflectionFoliageDrawContext*>(user);
        engine::render::FoliagePass::DrawSettings settings{};
        settings.reflectionClipEnabled = true;
        settings.reflectionClipY = draw.clipY;
        draw.pass->record(drawCmd, frameSet, settings);
    };
    recordFramePass("reflection-gbuffer", [&]() {
        passes.reflectionGbuffer.record(ctx_, fc, reflectionGbufferUbo, objects_,
                                        reflectionFoliageDraw);
    });

    recordReflectionVoxelDdaPasses(fc, passes, staticVolumes, frameInputs,
                                   effectiveVoxelDdaSkipEnabled, animationTimeSeconds);
    app::render::recordGBufferToLightingBarrier(cmd, passes.reflectionGbuffer, fc.frameIndex);

    LightingPass::FrameUbo reflectionLightingUbo{};
    reflectionLightingUbo.invViewProj = glm::inverse(reflectedViewProj);
    reflectionLightingUbo.viewProj = reflectedViewProj;
    reflectionLightingUbo.view = reflectedView;
    for (uint32_t cascade = 0; cascade < kShadowCascades; ++cascade)
    {
        reflectionLightingUbo.lightViewProj[cascade] = lightViewProjs[cascade];
    }
    reflectionLightingUbo.camPos = glm::vec4(reflectedCamPos, 1.0f);
    reflectionLightingUbo.lightDir = lightDir;
    reflectionLightingUbo.lightColor = lightColor;
    reflectionLightingUbo.cascadeSplits = cascadeSplits;
    reflectionLightingUbo.shadowMapSize = shadowMapSize;
    reflectionLightingUbo.caustics0 = caustics0;
    reflectionLightingUbo.caustics1 = caustics1;
    reflectionLightingUbo.caustics1.w = 0.0f;
    const engine::render::SceneAtmosphereSettings& atmosphere =
        environmentTimeSample_.presentation.sceneAtmosphere;
    reflectionLightingUbo.atmosphere0 =
        engine::render::packSceneAtmosphereParameters(atmosphere);
    reflectionLightingUbo.atmosphere1 =
        engine::render::packSceneAtmosphereSun(atmosphere, glm::vec3(lightDir));
    const engine::render::PaintedSkyGpuData paintedSky =
        engine::render::packPaintedSkyGpuData(
            environmentTimeSample_.presentation.paintedSky);
    reflectionLightingUbo.paintedSky0 = paintedSky.sky0;
    reflectionLightingUbo.paintedSky1 = paintedSky.sky1;
    reflectionLightingUbo.paintedSky2 = paintedSky.sky2;
    reflectionLightingUbo.paintedSky3 = paintedSky.sky3;
    reflectionLightingUbo.paintedSky4 = paintedSky.sky4;
    engine::render::PaintedCloudFrameInputs paintedCloudFrame{};
    paintedCloudFrame.windDirection = sceneConfig().environmentWind.direction;
    paintedCloudFrame.windSpeed = sceneConfig().environmentWind.speed;
    paintedCloudFrame.windStrength = sceneConfig().environmentWind.strength;
    paintedCloudFrame.animationTime = animationTimeSeconds;
    paintedCloudFrame.worldSeed = sceneConfig().worldSeed;
    const engine::render::PaintedCloudGpuData paintedClouds =
        engine::render::packPaintedCloudGpuData(
            environmentTimeSample_.presentation.paintedClouds,
            paintedCloudFrame);
    reflectionLightingUbo.paintedCloud0 = paintedClouds.cloud0;
    reflectionLightingUbo.paintedCloud1 = paintedClouds.cloud1;
    reflectionLightingUbo.paintedCloud2 = paintedClouds.cloud2;
    reflectionLightingUbo.paintedCloud3 = paintedClouds.cloud3;
    reflectionLightingUbo.paintedCloud4 = paintedClouds.cloud4;

    passes.updateReflectionLightingShadowBindings(ctx_, fc.frameIndex);
    passes.reflectionLighting.setLightParams(0u, 0);
    passes.reflectionLighting.setWaterVolumeCount(0);
    const std::array<int32_t, 4> noLocalShadowLightIndices = {-1, -1, -1, -1};
    const int voxelCellVariationEnabled =
        voxelDebugSettings_.voxelCellVariation_.enabled() ? 1 : 0;
    recordFramePass("reflection-lighting", [&]() {
        passes.reflectionLighting.record(ctx_, fc, reflectionLightingUbo, 0, 0, 0, 0, 0,
                                         aoSettings_.aoContribution_,
                                         shadowSettings_.terminatorSoftness_,
                                         shadowSettings_.terminatorMode_, 0,
                                         noLocalShadowLightIndices, 0.0f,
                                         voxelCellVariationEnabled,
                                         environmentTimeSample_.presentation.hemisphereAmbient,
                                         environmentTimeSample_.presentation.skyColor);
    });
    app::render::recordLightingToWaterBarrier(cmd, passes.reflectionLighting, fc.frameIndex);

    gpuProfiler_.endScope(cmd, gpuScopePlanar);
}

void App::recordDdaShadowPasses(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    const std::vector<engine::render::VoxelRenderResources::VolumeDrawPlan::Entry>& sunShadowOccluderVolumes,
    engine::render::FrameInputs& frameInputs,
    const glm::mat4& invViewProjUnjittered,
    const glm::vec3& lightDir,
    VkExtent2D activeExtent,
    float activeResolutionScale)
{
    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    const auto& depthTarget = passes.gbuffer.depth(fc.frameIndex);
    const auto& normalTarget = passes.gbuffer.color(fc.frameIndex, GBufferPass::Slot::Normal);
    const auto& velocityTarget = passes.gbuffer.color(fc.frameIndex, GBufferPass::Slot::Velocity);

    const uint32_t gpuScopeShadowRays = gpuProfiler_.beginScope(cmd, "Shadow Rays");

    ShadowRayPass::PushConstants shadowPC{};
    shadowPC.invViewProj = invViewProjUnjittered;
    const float effectiveSunAngularRadius =
        engine::scene::effectiveDdaSunAngularRadius(
            shadowSettings_.sunAngularRadius_,
            shadowSettings_.shadowDdaSunSampleCount_);
    shadowPC.sunDir = glm::vec4(-lightDir, effectiveSunAngularRadius);
    shadowPC.camPos = glm::vec4(camera_.position, 1.0f);
    shadowPC.resolution = glm::ivec2(activeExtent.width, activeExtent.height);
    shadowPC.volumeCount = static_cast<uint32_t>(voxelWorld_.instances().size());
    shadowPC.frameIndex = taaFrameIndex_;
    shadowPC.maxShadowDist = shadowSettings_.maxShadowDist_;
    shadowPC.normalBias = shadowSettings_.shadowNormalBias_;
    shadowPC.maxStepsPerVolume = shadowSettings_.maxShadowSteps_;
    shadowPC.shadowEnabled = 1;
    shadowPC.volumeIndex = 0;
    shadowPC.cloudShadowStrength = sceneConfig().cloudShadowStrength;
    const engine::render::SunShadowSamplingDecision shadowSampling =
        passes.shadowRayPass.samplingDecision(
            shadowSettings_.shadowDdaSunSampleCount_);
    shadowPC.sunSampleCount = engine::render::effectiveSunShadowSampleCount(
        shadowSampling.effectiveSampleCount, activeResolutionScale,
        effectiveSunAngularRadius, shadowSampling.compensateReducedResolution);
    shadowPC.foliageShadowOpacityScale = shadowSettings_.foliageShadowOpacity_;

    VkClearColorValue clearColor{};
    clearColor.float32[0] = 1.0f;
    clearColor.float32[1] = 1.0f;
    clearColor.float32[2] = 1.0f;
    clearColor.float32[3] = 1.0f;

    VkImageSubresourceRange shadowRange{};
    shadowRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    shadowRange.baseMipLevel = 0;
    shadowRange.levelCount = 1;
    shadowRange.baseArrayLayer = 0;
    shadowRange.layerCount = 1;

    vkCmdClearColorImage(cmd, passes.shadowBuffer.getCurrentImage(), VK_IMAGE_LAYOUT_GENERAL,
                         &clearColor, 1, &shadowRange);

    VkImageMemoryBarrier clearBarrier{};
    clearBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    clearBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    clearBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    clearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearBarrier.image = passes.shadowBuffer.getCurrentImage();
    clearBarrier.subresourceRange = shadowRange;
    clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    clearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &clearBarrier);

    passes.shadowRayPass.prepareFrame(
        ctx_, depthTarget.view, depthTarget.sampler, normalTarget.view, normalTarget.sampler,
        passes.blueNoiseTexture, passes.shadowBuffer,
        passes.auxiliaryTileListPass.buffer(fc.frameIndex),
        passes.auxiliaryTileListPass.bufferSize(), fc.frameIndex);

    for (size_t i = 0; i < sunShadowOccluderVolumes.size(); ++i)
    {
        const auto& volume = sunShadowOccluderVolumes[i];
        const uint32_t volumeIndex = volume.volumeIndex;
        shadowPC.volumeIndex = volumeIndex;
        const auto occlusionMode =
            voxelWorld_.instances()[volumeIndex].volume.lightingOcclusionMode();
        shadowPC.foliageShadowOpacityScale =
            shadowSettings_.foliageShadowOpacity_ *
            engine::render::voxelSunShadowFoliageOpacityScale(occlusionMode);

        uint32_t gpuScopeShadowVolume = 0xffffffffu;
        if (diagnosticsSettings_.shadowRayAuditEnabled_ &&
            volumeIndex < voxelWorld_.instances().size())
        {
            gpuScopeShadowVolume = gpuProfiler_.beginScope(
                cmd,
                voxelWorld_.instances()[volumeIndex].volume.debugName().c_str());
        }
        recordFramePass("shadow-rays", [&]() {
            passes.shadowRayPass.dispatch(
                cmd, shadowPC, volume.descriptorSet,
                passes.auxiliaryTileListPass.buffer(fc.frameIndex), fc.frameIndex);
        });
        if (diagnosticsSettings_.shadowRayAuditEnabled_)
        {
            gpuProfiler_.endScope(cmd, gpuScopeShadowVolume);
        }

        if (i + 1 < sunShadowOccluderVolumes.size())
        {
            VkImageMemoryBarrier volumeBarrier{};
            volumeBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            volumeBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            volumeBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            volumeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            volumeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            volumeBarrier.image = passes.shadowBuffer.getCurrentImage();
            volumeBarrier.subresourceRange = shadowRange;
            volumeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            volumeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                                 nullptr, 1, &volumeBarrier);
        }
    }

    passes.shadowRayPass.finishFrame(cmd, fc.frameIndex);

    VkImageMemoryBarrier currentBarrier{};
    currentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    currentBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    currentBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    currentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    currentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    currentBarrier.image = passes.shadowBuffer.getCurrentImage();
    currentBarrier.subresourceRange = shadowRange;
    currentBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    currentBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &currentBarrier);

    gpuProfiler_.endScope(cmd, gpuScopeShadowRays);

    const uint32_t gpuScopeShadowResolve = gpuProfiler_.beginScope(cmd, "Shadow Resolve");
    const int historyReadIndex = 1 - passes.depthHistory.index;
    TemporalResolveConfig shadowConfig{};
    shadowConfig.blendAlpha = shadowSettings_.shadowBlendAlpha_;
    shadowConfig.blendAlphaMax = 1.0f;
    shadowConfig.depthRejectThreshold = shadowSettings_.shadowDepthReject_;
    shadowConfig.useNormalReject = true;
    shadowConfig.normalRejectDot = shadowSettings_.shadowNormalRejectDot_;
    const bool reconstructShadowCurrent =
        activeExtent.width != passCreateInfo_.extent.width ||
        activeExtent.height != passCreateInfo_.extent.height;
    shadowConfig.useNeighborhoodClamp =
        engine::render::useSunShadowNeighborhoodClamp(
            reconstructShadowCurrent, effectiveSunAngularRadius);
    shadowConfig.clampSharpness = shadowSettings_.shadowClampSharpness_;
    shadowConfig.useSpatialFilter = shadowSettings_.shadowSpatialFilterRadius_ > 0;
    shadowConfig.spatialFilterReconstructedCurrent = shadowConfig.useSpatialFilter;
    shadowConfig.spatialFilterRadius =
        static_cast<uint32_t>(std::max(shadowSettings_.shadowSpatialFilterRadius_, 0));
    shadowConfig.spatialDepthSigma = shadowSettings_.shadowSpatialDepthSigma_;
    shadowConfig.spatialValueSigma = shadowSettings_.shadowSpatialValueSigma_;
    shadowConfig.spatialNormalPower = shadowSettings_.shadowSpatialNormalPower_;
    shadowConfig.format = VK_FORMAT_R16_SFLOAT;
    passes.shadowTemporalResolve.setConfig(shadowConfig);
    recordFramePass("shadow-resolve", [&]() {
        passes.shadowTemporalResolve.resolve(cmd, ctx_,
                                             passes.shadowBuffer.getCurrentImageView(),
                                             passes.shadowBuffer.getSampler(),
                                             activeExtent,
                                             velocityTarget.view, velocityTarget.sampler,
                                             depthTarget.view, depthTarget.sampler,
                                             normalTarget.view, normalTarget.sampler,
                                             passes.depthHistory.views[historyReadIndex],
                                             passes.depthHistory.sampler,
                                             shadowSettings_.shadowResetHistory_,
                                             shadowSettings_.shadowDebugMode_, fc.frameIndex);
    });
    shadowSettings_.shadowResetHistory_ = false;

    VkImageMemoryBarrier resolvedBarrier{};
    resolvedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    resolvedBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    resolvedBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    resolvedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resolvedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resolvedBarrier.image = passes.shadowTemporalResolve.resolvedImage();
    resolvedBarrier.subresourceRange = shadowRange;
    resolvedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    const bool useShadowPostDenoise = passes.availability.shadowDenoisePass;
    resolvedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         useShadowPostDenoise ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                              : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &resolvedBarrier);

    if (useShadowPostDenoise)
    {
        recordFramePass("shadow-denoise", [&]() {
            passes.shadowDenoisePass.dispatch(
                cmd, ctx_, passes.shadowTemporalResolve.resolvedView(),
                passes.shadowTemporalResolve.sampler(), depthTarget.view, depthTarget.sampler,
                normalTarget.view, normalTarget.sampler,
                static_cast<uint32_t>(std::max(shadowSettings_.shadowPostDenoiseRadius_, 0)),
                shadowSettings_.shadowPostDenoiseDepthSigma_, shadowSettings_.shadowPostDenoiseValueSigma_,
                shadowSettings_.shadowPostDenoiseNormalPower_, fc.frameIndex);
        });

        VkImageMemoryBarrier denoisedBarrier{};
        denoisedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        denoisedBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        denoisedBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        denoisedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        denoisedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        denoisedBarrier.image = passes.shadowDenoisePass.outputImage();
        denoisedBarrier.subresourceRange = shadowRange;
        denoisedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        denoisedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &denoisedBarrier);
    }
    gpuProfiler_.endScope(cmd, gpuScopeShadowResolve);
}

void App::recordLocalLightShadowPasses(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    const std::vector<engine::render::VoxelRenderResources::VolumeDrawPlan::Entry>& localLightingOccluderVolumes,
    engine::render::FrameInputs& frameInputs,
    bool useLocalLightShadows,
    uint32_t activeLights,
    uint32_t maxShadowedLocalLights,
    const glm::mat4& invViewProjUnjittered,
    VkExtent2D activeExtent)
{
    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    localLightShadowSlotCount_ = 0u;
    if (useLocalLightShadows)
    {
        for (uint32_t i = 0; i < activeLights && localLightShadowSlotCount_ < maxShadowedLocalLights; ++i)
        {
            if (i < lightingSettings_.areaLights_.size() && lightingSettings_.areaLights_[i].castsShadows)
            {
                localLightShadowSlots_[localLightShadowSlotCount_] = i;
                ++localLightShadowSlotCount_;
            }
        }
    }

    if (useLocalLightShadows && localLightShadowSlotCount_ > 0u)
    {
        localShadowNeutralClearPending_ = true;

        VkImageSubresourceRange localRange{};
        localRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        localRange.baseMipLevel = 0;
        localRange.levelCount = 1;
        localRange.baseArrayLayer = 0;
        localRange.layerCount = 1;

        VkClearColorValue localClear{};
        localClear.float32[0] = 1.0f;
        localClear.float32[1] = 1.0f;
        localClear.float32[2] = 1.0f;
        localClear.float32[3] = 1.0f;

        vkCmdClearColorImage(cmd, passes.localLightShadowBuffer.getCurrentImage(),
                             VK_IMAGE_LAYOUT_GENERAL, &localClear, 1, &localRange);

        VkImageMemoryBarrier localClearBarrier{};
        localClearBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        localClearBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        localClearBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        localClearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        localClearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        localClearBarrier.image = passes.localLightShadowBuffer.getCurrentImage();
        localClearBarrier.subresourceRange = localRange;
        localClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        localClearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &localClearBarrier);

        const uint32_t gpuScopeLocalShadow = gpuProfiler_.beginScope(cmd, "Local Shadows");

        LocalLightShadowPass::PushConstants localPC{};
        localPC.invViewProj = invViewProjUnjittered;
        localPC.camPos = glm::vec4(camera_.position, 1.0f);
        localPC.resolution = glm::ivec2(activeExtent.width, activeExtent.height);
        localPC.volumeCount = static_cast<uint32_t>(voxelWorld_.instances().size());
        localPC.frameIndex = taaFrameIndex_;
        localPC.maxShadowDist = shadowSettings_.maxShadowDist_;
        localPC.normalBias = shadowSettings_.shadowNormalBias_;
        localPC.maxStepsPerVolume = shadowSettings_.maxShadowSteps_;
        localPC.shadowEnabled = 1;
        localPC.volumeIndex = 0u;
        localPC.lightCount = activeLights;
        localPC.slotCount = localLightShadowSlotCount_;
        localPC._pad0 = 0u;
        for (uint32_t slot = 0; slot < 4; ++slot)
        {
            localPC.lightSlots[slot] = localLightShadowSlots_[slot];
        }

        const auto& depthTarget = passes.gbuffer.depth(fc.frameIndex);
        const auto& normalTarget = passes.gbuffer.color(fc.frameIndex, GBufferPass::Slot::Normal);
        const auto& velocityTarget = passes.gbuffer.color(fc.frameIndex, GBufferPass::Slot::Velocity);

        passes.localLightShadowPass.prepareFrame(
            ctx_, depthTarget.view, depthTarget.sampler, normalTarget.view,
            normalTarget.sampler, passes.blueNoiseTexture, lights_,
            passes.localLightShadowBuffer,
            passes.auxiliaryTileListPass.buffer(fc.frameIndex),
            passes.auxiliaryTileListPass.bufferSize(), fc.frameIndex);

        for (size_t i = 0; i < localLightingOccluderVolumes.size(); ++i)
        {
            const auto& volume = localLightingOccluderVolumes[i];
            const uint32_t volumeIndex = volume.volumeIndex;
            localPC.volumeIndex = volumeIndex;

            recordFramePass("local-light-shadows", [&]() {
                passes.localLightShadowPass.dispatch(
                    cmd, localPC, volume.descriptorSet,
                    passes.auxiliaryTileListPass.buffer(fc.frameIndex), fc.frameIndex);
            });

            if (i + 1 < localLightingOccluderVolumes.size())
            {
                VkImageMemoryBarrier localVolumeBarrier{};
                localVolumeBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                localVolumeBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                localVolumeBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                localVolumeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                localVolumeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                localVolumeBarrier.image = passes.localLightShadowBuffer.getCurrentImage();
                localVolumeBarrier.subresourceRange = localRange;
                localVolumeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                localVolumeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                                     nullptr, 1, &localVolumeBarrier);
            }
        }

        gpuProfiler_.endScope(cmd, gpuScopeLocalShadow);

        VkImageMemoryBarrier localCurrentBarrier{};
        localCurrentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        localCurrentBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        localCurrentBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        localCurrentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        localCurrentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        localCurrentBarrier.image = passes.localLightShadowBuffer.getCurrentImage();
        localCurrentBarrier.subresourceRange = localRange;
        localCurrentBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        localCurrentBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &localCurrentBarrier);

        VkImageView shadowInputView = passes.localLightShadowBuffer.getCurrentImageView();
        if (canUseLocalShadowBlur())
        {
            const uint32_t gpuScopeLocalBlur = gpuProfiler_.beginScope(cmd, "Local Shadow Blur");
            recordFramePass("local-shadow-blur", [&]() {
                passes.localShadowBlur.dispatch(cmd, ctx_,
                                                passes.localLightShadowBuffer.getCurrentImageView(),
                                                passes.localLightShadowBuffer.getSampler(),
                                                passes.localLightShadowBuffer.getBlurredStorageView(),
                                                activeExtent,
                                                shadowSettings_.localShadowBlurSigma_, fc.frameIndex);
            });

            VkImageMemoryBarrier blurBarrier{};
            blurBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            blurBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            blurBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            blurBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            blurBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            blurBarrier.image = passes.localLightShadowBuffer.getBlurredImage();
            blurBarrier.subresourceRange = localRange;
            blurBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            blurBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                                 nullptr, 1, &blurBarrier);
            gpuProfiler_.endScope(cmd, gpuScopeLocalBlur);

            shadowInputView = passes.localLightShadowBuffer.getBlurredImageView();
        }

        const uint32_t gpuScopeLocalResolve = gpuProfiler_.beginScope(cmd, "Local Shadow Resolve");
        const int historyReadIndex = 1 - passes.depthHistory.index;
        LocalLightTemporalResolveConfig localConfig{};
        localConfig.blendAlpha = shadowSettings_.localShadowBlendAlpha_;
        localConfig.blendAlphaMax = 1.0f;
        localConfig.depthRejectThreshold = shadowSettings_.localShadowDepthReject_;
        localConfig.useNormalReject = false;
        localConfig.normalRejectDot = aoSettings_.aoNormalRejectDot_;
        localConfig.useNeighborhoodClamp = false;
        localConfig.clampSharpness = 1.0f;
        passes.localLightShadowTemporalResolve.setConfig(localConfig);
        recordFramePass("local-shadow-resolve", [&]() {
            passes.localLightShadowTemporalResolve.resolve(
                cmd, ctx_, shadowInputView,
                passes.localLightShadowBuffer.getSampler(), activeExtent,
                velocityTarget.view,
                velocityTarget.sampler, depthTarget.view, depthTarget.sampler,
                normalTarget.view, normalTarget.sampler,
                passes.depthHistory.views[historyReadIndex], passes.depthHistory.sampler,
                shadowSettings_.localShadowResetHistory_, 0, fc.frameIndex);
        });
        shadowSettings_.localShadowResetHistory_ = false;

        VkImageMemoryBarrier localResolvedBarrier{};
        localResolvedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        localResolvedBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        localResolvedBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        localResolvedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        localResolvedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        localResolvedBarrier.image = passes.localLightShadowTemporalResolve.resolvedImage();
        localResolvedBarrier.subresourceRange = localRange;
        localResolvedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        localResolvedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &localResolvedBarrier);
        gpuProfiler_.endScope(cmd, gpuScopeLocalResolve);
    }
    else
    {
        if (localShadowNeutralClearPending_)
        {
            app::render::clearGeneralColorImage(cmd,
                                                passes.localLightShadowTemporalResolve.resolvedImage(),
                                                1.0f, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                                VK_ACCESS_SHADER_READ_BIT);
            localShadowNeutralClearPending_ = false;
        }
        shadowSettings_.localShadowResetHistory_ = true;
    }
}

void App::recordAmbientOcclusionPasses(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    const std::vector<engine::render::VoxelRenderResources::VolumeDrawPlan::Entry>& localLightingOccluderVolumes,
    engine::render::FrameInputs& frameInputs,
    bool useAmbientOcclusion,
    const glm::mat4& invViewProjJittered,
    VkExtent2D activeExtent,
    float activeResolutionScale)
{
    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    if (useAmbientOcclusion)
    {
        aoNeutralClearPending_ = true;

        VkClearColorValue aoClear{};
        aoClear.float32[0] = 1.0f;
        aoClear.float32[1] = 1.0f;
        aoClear.float32[2] = 1.0f;
        aoClear.float32[3] = 1.0f;

        VkImageSubresourceRange aoRange{};
        aoRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        aoRange.baseMipLevel = 0;
        aoRange.levelCount = 1;
        aoRange.baseArrayLayer = 0;
        aoRange.layerCount = 1;

        vkCmdClearColorImage(cmd, passes.aoPass.aoImage(), VK_IMAGE_LAYOUT_GENERAL,
                             &aoClear, 1, &aoRange);

        VkImageMemoryBarrier aoClearBarrier{};
        aoClearBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        aoClearBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        aoClearBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        aoClearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        aoClearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        aoClearBarrier.image = passes.aoPass.aoImage();
        aoClearBarrier.subresourceRange = aoRange;
        aoClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        aoClearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &aoClearBarrier);

        const uint32_t gpuScopeAORays = gpuProfiler_.beginScope(cmd, "AO Rays");
        const VkExtent2D aoRayExtent = activeExtent;
        // freeze debug is the fixed-input visual qa mode. keep ao's stochastic
        // hemisphere direction fixed there so raw-AO captures are repeatable.
        const engine::render::ProjectedAoDistanceTier configuredAoTier =
            engine::render::projectedAoDistanceTier(
                aoSettings_.aoDistanceMode_, aoSettings_.aoProjectedRadiusPixels_,
                aoSettings_.aoProjectedMinDistance_);
        const engine::render::ProjectedAoDistanceTier projectedAoTier =
            engine::render::resolveProjectedAoDistanceTier(
                configuredAoTier,
                {automationAoProjectedRadiusPixels_,
                 automationAoProjectedMinDistance_});
        AOPass::FrameInputs aoInputs{};
        aoInputs.invViewProj = invViewProjJittered;
        aoInputs.cameraPosition = camera_.position;
        aoInputs.maxDistance = aoSettings_.aoMaxDistance_;
        aoInputs.stepSize = aoSettings_.aoStepSize_;
        aoInputs.intensity = aoSettings_.aoIntensity_;
        aoInputs.bias = aoSettings_.aoBias_;
        aoInputs.extent = aoRayExtent;
        aoInputs.volumeCount = static_cast<uint32_t>(voxelWorld_.instances().size());
        aoInputs.frameIndex = voxelDebugSettings_.voxelFreezeDebug_ ? 0u : taaFrameIndex_;
        aoInputs.rayCount = engine::render::effectiveAmbientOcclusionRayCount(
            static_cast<uint32_t>(std::max(1, aoSettings_.aoRayCount_)),
            activeResolutionScale);
        aoInputs.projectedDistanceScale = engine::render::projectedAoDistanceScale(
            projectedAoTier, camera_.fovY,
            static_cast<float>(passCreateInfo_.extent.height));
        aoInputs.projectedMinDistance = projectedAoTier.minimumWorldDistance;
        AOPass::PushConstants aoPC = AOPass::buildPushConstants(aoInputs);

        const auto& depthTarget = passes.gbuffer.depth(fc.frameIndex);
        const auto& normalTarget = passes.gbuffer.color(fc.frameIndex, GBufferPass::Slot::Normal);
        const auto& velocityTarget = passes.gbuffer.color(fc.frameIndex, GBufferPass::Slot::Velocity);

        passes.aoPass.prepareFrame(ctx_, depthTarget.view, depthTarget.sampler,
                                   normalTarget.view, normalTarget.sampler,
                                   passes.blueNoiseTexture,
                                   passes.auxiliaryTileListPass.buffer(fc.frameIndex),
                                   passes.auxiliaryTileListPass.bufferSize(),
                                   fc.frameIndex);

        for (size_t i = 0; i < localLightingOccluderVolumes.size(); ++i)
        {
            const auto& volume = localLightingOccluderVolumes[i];
            const uint32_t volumeIndex = volume.volumeIndex;
            aoPC.volumeIndex = volumeIndex;

            recordFramePass("ao-rays", [&]() {
                passes.aoPass.dispatch(
                    cmd, aoPC, volume.descriptorSet,
                    passes.auxiliaryTileListPass.buffer(fc.frameIndex), fc.frameIndex);
            });

            if (i + 1 < localLightingOccluderVolumes.size())
            {
                VkImageMemoryBarrier aoVolumeBarrier{};
                aoVolumeBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                aoVolumeBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                aoVolumeBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                aoVolumeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                aoVolumeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                aoVolumeBarrier.image = passes.aoPass.aoImage();
                aoVolumeBarrier.subresourceRange = aoRange;
                aoVolumeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                aoVolumeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                                     nullptr, 1, &aoVolumeBarrier);
            }
        }

        VkImageMemoryBarrier aoBarrier{};
        aoBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        aoBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        aoBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        aoBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        aoBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        aoBarrier.image = passes.aoPass.aoImage();
        aoBarrier.subresourceRange = aoRange;
        aoBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        aoBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &aoBarrier);
        gpuProfiler_.endScope(cmd, gpuScopeAORays);

        const uint32_t gpuScopeAOResolve = gpuProfiler_.beginScope(cmd, "AO Resolve");
        TemporalResolveConfig aoConfig{};
        aoConfig.blendAlpha =
            engine::render::effectiveAmbientOcclusionTemporalBlendAlpha(
                aoSettings_.aoBlendAlpha_, activeResolutionScale);
        aoConfig.blendAlphaMax = 1.0f;
        aoConfig.depthRejectThreshold = aoSettings_.aoDepthReject_;
        aoConfig.useNormalReject = true;
        aoConfig.normalRejectDot = aoSettings_.aoNormalRejectDot_;
        aoConfig.useNeighborhoodClamp = true;
        aoConfig.clampSharpness = 0.8f;
        const bool useHalfResolutionSpatialFilter =
            engine::render::useAmbientOcclusionSpatialFilter(activeResolutionScale);
        aoConfig.useSpatialFilter = useHalfResolutionSpatialFilter;
        aoConfig.spatialFilterReconstructedCurrent = useHalfResolutionSpatialFilter;
        aoConfig.spatialFilterRadius = 1;
        aoConfig.spatialDepthSigma = 0.01f;
        aoConfig.spatialValueSigma = 0.25f;
        aoConfig.spatialNormalPower = 32.0f;
        aoConfig.format = VK_FORMAT_R16_SFLOAT;
        passes.aoTemporalResolve.setConfig(aoConfig);

        recordFramePass("ao-resolve", [&]() {
            passes.aoTemporalResolve.resolve(cmd, ctx_,
                                             passes.aoPass.aoView(), passes.aoPass.aoSampler(),
                                             activeExtent,
                                             velocityTarget.view, velocityTarget.sampler,
                                             depthTarget.view, depthTarget.sampler,
                                             normalTarget.view, normalTarget.sampler,
                                             passes.depthHistory.views[1 - passes.depthHistory.index],
                                             passes.depthHistory.sampler,
                                             aoSettings_.aoResetHistory_, 0, fc.frameIndex);
        });
        aoSettings_.aoResetHistory_ = false;

        VkImageMemoryBarrier aoResolvedBarrier{};
        aoResolvedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        aoResolvedBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        aoResolvedBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        aoResolvedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        aoResolvedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        aoResolvedBarrier.image = passes.aoTemporalResolve.resolvedImage();
        aoResolvedBarrier.subresourceRange = aoRange;
        aoResolvedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        aoResolvedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &aoResolvedBarrier);
        gpuProfiler_.endScope(cmd, gpuScopeAOResolve);
    }
    else
    {
        if (aoNeutralClearPending_)
        {
            app::render::clearGeneralColorImage(cmd, passes.aoTemporalResolve.resolvedImage(), 1.0f,
                                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                                VK_ACCESS_SHADER_READ_BIT);
            aoNeutralClearPending_ = false;
        }
        aoSettings_.aoResetHistory_ = true;
    }
}

void App::recordMainLightingPass(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    engine::render::FrameInputs& frameInputs,
    const LightingPass::FrameUbo& lightingUbo,
    uint32_t activeLights,
    bool useDdaShadows,
    bool useAmbientOcclusion,
    const glm::vec2& projectionJitterUv)
{
    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    int debugMode = 0;
    if (renderPipelineShowcase_.enabled())
    {
        const int showcaseLightingMode =
            renderPipelineShowcase_.currentStage().lightingDebugMode;
        if (showcaseLightingMode > 0)
        {
            debugMode = 2 + showcaseLightingMode;
        }
    }
    else if (input_.cascadeDebug())
    {
        debugMode = 2;
    }
    else if (input_.heatmap())
    {
        debugMode = 1;
    }
    else if (diagnosticsSettings_.edgeFlickerDebugMode_ > 0)
    {
        debugMode = 19 + diagnosticsSettings_.edgeFlickerDebugMode_;
    }
    else if (lightingSettings_.lightingDebugMode_ > 0)
    {
        debugMode = 2 + lightingSettings_.lightingDebugMode_;
    }
    passes.lighting.setLightParams(activeLights, debugMode);

    const int csmDitherEnabled =
        (shadowSettings_.csmEnabled_ && shadowSettings_.csmDitherEnabled_ && postFxSettings_.taaEnabled_) ? 1 : 0;
    const int csmEnabled = shadowSettings_.csmEnabled_ ? 1 : 0;
    const int shadowDebugMode =
        renderPipelineShowcase_.enabled()
            ? renderPipelineShowcase_.currentStage().shadowDebugMode
            : shadowSettings_.shadowDebugMode_;

    std::array<int32_t, 4> localShadowLightIndices = {-1, -1, -1, -1};
    for (uint32_t slot = 0; slot < localLightShadowSlotCount_ && slot < 4u; ++slot)
    {
        localShadowLightIndices[slot] = static_cast<int32_t>(localLightShadowSlots_[slot]);
    }

    const uint32_t gpuScopeLighting = gpuProfiler_.beginScope(cmd, "Lighting");
    const bool variationSignalDebug =
        voxelDebugSettings_.voxelDdaAdvancedDebug_ == 15;
    const int voxelCellVariationEnabled =
        (voxelDebugSettings_.voxelCellVariation_.enabled() || variationSignalDebug)
            ? 1
            : 0;
    recordFramePass("lighting", [&]() {
        passes.lighting.record(ctx_, fc, lightingUbo, useDdaShadows ? 1 : 0,
                               shadowDebugMode, csmDitherEnabled, csmEnabled,
                               useAmbientOcclusion ? 1 : 0, aoSettings_.aoContribution_,
                               shadowSettings_.terminatorSoftness_,
                               shadowSettings_.terminatorMode_,
                               static_cast<int>(localLightShadowSlotCount_),
                               localShadowLightIndices, 0.0f,
                               voxelCellVariationEnabled,
                               environmentTimeSample_.presentation.hemisphereAmbient,
                               environmentTimeSample_.presentation.skyColor,
                               projectionJitterUv);
    });
    app::render::recordLightingToWaterBarrier(cmd, passes.lighting, fc.frameIndex);
    gpuProfiler_.endScope(cmd, gpuScopeLighting);
}

void App::recordVoxelGlassRefractionPass(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    engine::render::FrameInputs& frameInputs,
    bool shouldRecordVoxelGlassRefraction,
    const glm::mat4& invViewProjUnjittered,
    uint32_t activeWaterVolumeCount)
{
    if (!shouldRecordVoxelGlassRefraction)
    {
        return;
    }

    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    const uint32_t gpuScopeVoxelGlass = gpuProfiler_.beginScope(cmd, "Voxel Glass Refract");

    VoxelGlassRefractPass::FrameUboInputs voxelGlassInputs{};
    voxelGlassInputs.invViewProj = invViewProjUnjittered;
    voxelGlassInputs.cameraPosition = camera_.position;
    voxelGlassInputs.tint = glassSettings_.voxelGlassTint_;
    voxelGlassInputs.absorption = glassSettings_.voxelGlassAbsorption_;
    voxelGlassInputs.refractStrength = glassSettings_.voxelGlassRefractStrength_;
    voxelGlassInputs.ior = glassSettings_.voxelGlassIOR_;
    voxelGlassInputs.debugMode = glassSettings_.voxelGlassDebugMode_;
    voxelGlassInputs.renderExtent = passCreateInfo_.extent;
    voxelGlassInputs.maxThickness = 16.0f;
    voxelGlassInputs.reflectionStrength = glassSettings_.voxelGlassReflectStrength_;
    voxelGlassInputs.reflectionColor = glassSettings_.voxelGlassReflectionColor_;
    voxelGlassInputs.sceneAtmosphere =
        environmentTimeSample_.presentation.sceneAtmosphere;
    voxelGlassInputs.activeWaterVolumeCount = activeWaterVolumeCount;
    const VoxelGlassRefractPass::FrameUbo voxelGlassUbo =
        VoxelGlassRefractPass::buildFrameUbo(voxelGlassInputs);

    recordFramePass("voxel-glass-refract", [&]() {
        passes.voxelGlassRefract.record(ctx_, fc, voxelGlassUbo);
    });

    const auto& vgTarget = passes.voxelGlassRefract.target(fc.frameIndex);
    const auto& litTarget = passes.lighting.lit(fc.frameIndex);

    VkImageMemoryBarrier vgToSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    vgToSrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vgToSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vgToSrc.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vgToSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vgToSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vgToSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vgToSrc.image = vgTarget.image;
    vgToSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier litToDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    litToDst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    litToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    litToDst.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    litToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    litToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    litToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    litToDst.image = litTarget.image;
    litToDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier toTransfer[] = {vgToSrc, litToDst};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                         toTransfer);

    VkImageBlit blitRegion{};
    blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitRegion.srcOffsets[0] = {0, 0, 0};
    blitRegion.srcOffsets[1] = {static_cast<int32_t>(passes.voxelGlassRefract.extent().width),
                                static_cast<int32_t>(passes.voxelGlassRefract.extent().height),
                                1};
    blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitRegion.dstOffsets[0] = {0, 0, 0};
    blitRegion.dstOffsets[1] = {static_cast<int32_t>(passes.lighting.extent().width),
                                static_cast<int32_t>(passes.lighting.extent().height), 1};

    vkCmdBlitImage(cmd, vgTarget.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, litTarget.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blitRegion, VK_FILTER_NEAREST);

    VkImageMemoryBarrier vgBack{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    vgBack.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vgBack.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vgBack.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vgBack.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vgBack.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vgBack.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vgBack.image = vgTarget.image;
    vgBack.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier litBack{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    litBack.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    litBack.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    litBack.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    litBack.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    litBack.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    litBack.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    litBack.image = litTarget.image;
    litBack.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier toShader[] = {vgBack, litBack};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                         toShader);

    gpuProfiler_.endScope(cmd, gpuScopeVoxelGlass);
}

void App::recordWaterBodyCompositePass(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    engine::render::FrameInputs& frameInputs,
    const char* scopeLabel,
    const glm::mat4& invViewProjJittered,
    const glm::vec4& sunDirToSun,
    const glm::vec4& waterTint,
    const glm::vec4& waterBodyDetail,
    const glm::vec4& waterBodyShaft,
    uint32_t activeWaterCount,
    int waterDebugMode,
    float animationTimeSeconds)
{
    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    WaterBodyPass::FrameUbo waterBodyUbo{};
    waterBodyUbo.invViewProj = invViewProjJittered;
    waterBodyUbo.camPos = glm::vec4(camera_.position, 1.0f);
    waterBodyUbo.sunDirToSun = sunDirToSun;
    waterBodyUbo.waterTint = waterTint;
    const bool sunroofScene = isSunroofAquariumScene(sceneConfig());
    const engine::render::SunroofLivingWaterVfxSettings& livingWater =
        waterSettings_.sunroofLivingWaterVfx_;
    const float illuminatedMoteIntensity =
        sunroofScene
            ? (livingWater.enabled
                   ? std::clamp(livingWater.illuminatedMoteIntensity, 0.0f, 2.0f)
                   : 0.0f)
            : 1.0f;
    waterBodyUbo.params0 =
        glm::vec4(animationTimeSeconds, static_cast<float>(waterDebugMode),
                  static_cast<float>(activeWaterCount), illuminatedMoteIntensity);
    waterBodyUbo.params1 = waterBodyDetail;
    waterBodyUbo.params2 = waterBodyShaft;

    const uint32_t gpuScopeWaterBody = gpuProfiler_.beginScope(cmd, scopeLabel);
    recordFramePass(scopeLabel, [&]() { passes.waterBody.record(ctx_, fc, waterBodyUbo); });
    passes.waterBody.copyToLighting(cmd, fc.frameIndex, passes.lighting);
    gpuProfiler_.endScope(cmd, gpuScopeWaterBody);
}

void App::recordWaterGlassSurfacePasses(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    engine::render::FrameInputs& frameInputs,
    bool drawWater,
    bool drawGlass,
    bool shouldRecordPlanarReflection,
    const WaterPass::FrameUbo& waterUbo,
    const GlassPass::FrameUbo& glassUbo)
{
    auto recordFramePass = [&](std::string_view name, auto&& record) {
        engine::render::PassRegistry registry;
        registry.add(name, [&](const engine::render::RenderPassContext&) { record(); });
        renderer_.recordPasses(registry, frameInputs, fc);
    };

    if (drawWater)
    {
        WaterPass& activeWaterPass = waterSettings_.useWaterV2_ ? passes.waterV2 : passes.water;
        const uint32_t gpuScopeWater = gpuProfiler_.beginScope(cmd, "Water");
        const bool drawWaterSurface =
            renderPipelineShowcase_.enabled() || input_.waterDebugMode() < 21;
        recordFramePass(waterSettings_.useWaterV2_ ? "water-v2" : "water", [&]() {
            activeWaterPass.record(ctx_, fc, waterUbo, passes.lighting,
                                   &passes.reflectionLighting, shouldRecordPlanarReflection,
                                   waterMesh_, drawWaterSurface);
        });
        passes.glass.updateSceneColorSource(ctx_, fc.frameIndex,
                                            activeWaterPass.target(fc.frameIndex).view,
                                            activeWaterPass.target(fc.frameIndex).sampler);
        app::render::recordWaterToGlassBarrier(cmd, activeWaterPass, passes.lighting,
                                               fc.frameIndex);
        gpuProfiler_.endScope(cmd, gpuScopeWater);
    }
    else if (drawGlass)
    {
        WaterPass& activeWaterPass = waterSettings_.useWaterV2_ ? passes.waterV2 : passes.water;
        const uint32_t gpuScopeGlassSource = gpuProfiler_.beginScope(cmd, "Glass Source Copy");
        recordFramePass("glass-source-copy", [&]() {
            activeWaterPass.recordSceneColorCopy(fc, passes.lighting);
        });
        passes.glass.updateSceneColorSource(ctx_, fc.frameIndex,
                                            activeWaterPass.refractionSource(fc.frameIndex).view,
                                            activeWaterPass.refractionSource(fc.frameIndex).sampler);
        app::render::recordLightingReadToColorAttachmentBarrier(cmd, passes.lighting,
                                                                fc.frameIndex);
        gpuProfiler_.endScope(cmd, gpuScopeGlassSource);
    }

    if (drawWater || drawGlass)
    {
        const uint32_t gpuScopeGlass = gpuProfiler_.beginScope(cmd, "Glass");
        if (drawGlass)
        {
            recordFramePass("glass-back-depth", [&]() {
                passes.glass.recordBackDepth(ctx_, fc, glassUbo, glassObjects_);
            });
            app::render::recordGlassBackDepthToShadeBarrier(cmd, passes.glass, fc.frameIndex);
        }
        recordFramePass("glass-shade", [&]() {
            passes.glass.recordShade(ctx_, fc, glassUbo, glassObjects_, drawGlass);
        });
        app::render::recordGlassToPostBarrier(cmd, passes.lighting, fc.frameIndex);
        gpuProfiler_.endScope(cmd, gpuScopeGlass);
    }
}

void App::recordBloomTaaPasses(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    engine::render::FrameInputs& frameInputs,
    const engine::scene::WorldStateView& frameWorldState,
    const glm::vec2& jitter,
    int postDebugMode)
{
    const float taaBloomIntensity = frameWorldState.postFx.bloomEnabled_
                                        ? frameWorldState.postFx.bloomIntensity_
                                        : 0.0f;
    frameInputs.runBloom = frameWorldState.postFx.bloomEnabled_ || postDebugMode != 0;
    const int taaDebugMode =
        renderPipelineShowcase_.enabled() ? 0 : frameWorldState.postFx.taaDebugMode_;
    frameInputs.runTaa = frameWorldState.postFx.taaEnabled_ || taaDebugMode != 0;
    VkImageView resolvedSceneView = passes.lighting.lit(fc.frameIndex).view;
    VkSampler resolvedSceneSampler = passes.lighting.lit(fc.frameIndex).sampler;

    engine::render::PassRegistry postProcessPasses;
    postProcessPasses.add("bloom", [&](const engine::render::RenderPassContext& context) {
        const auto& postFx = context.frame.worldState->postFx;
        const BloomPass::Settings bloomSettings{postFx.bloomThreshold_, postFx.bloomKnee_,
                                                postFx.bloomSigma_};
        const uint32_t gpuScopeBloom = gpuProfiler_.beginScope(cmd, "Bloom");
        passes.bloom.record(context.ctx, context.frame, bloomSettings, context.inputs.runBloom);
        gpuProfiler_.endScope(cmd, gpuScopeBloom);
    });
    postProcessPasses.add("taa", [&](const engine::render::RenderPassContext& context) {
        const auto& postFx = context.frame.worldState->postFx;
        if (context.inputs.runTaa)
        {
            const bool resetHistory =
                postFx.taaResetHistory_ || taaFrameIndex_ == 0 || !postFx.taaEnabled_;
            const glm::vec2 jitterForTaa = jitter;
            const uint32_t gpuScopeTaa = gpuProfiler_.beginScope(cmd, "TAA");
            passes.taa.record(ctx_, fc, jitterForTaa, prevJitter_,
                              postFx.taaSimilarityThreshold_, postFx.taaVelocityScale_,
                              postFx.taaBlendMin_, postFx.taaBlendMax_, taaBloomIntensity,
                              resetHistory, taaDebugMode, postFx.taaDepthEdgeThreshold_,
                              camera_.nearZ, postFx.taaCrossFrameDepthThreshold_, camera_.farZ,
                              postFx.taaColorVarianceThreshold_,
                              postFx.taaSoftEdgeStrength_);
            postFxSettings_.taaResetHistory_ = false;
            resolvedSceneView = passes.taa.outputView();
            resolvedSceneSampler = passes.taa.sampler();

            if (postFx.taaEnabled_ && postFx.taaCrossFrameDepthThreshold_ > 0.0f)
            {
                const uint32_t taaHistoryWriteIndex = passes.taa.outputHistoryIndex();
                VkImageMemoryBarrier taaDepthBarriers[2]{};

                taaDepthBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                taaDepthBarriers[0].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                taaDepthBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                taaDepthBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                taaDepthBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                taaDepthBarriers[0].image = passes.gbuffer.depth(fc.frameIndex).image;
                taaDepthBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                taaDepthBarriers[0].subresourceRange.baseMipLevel = 0;
                taaDepthBarriers[0].subresourceRange.levelCount = 1;
                taaDepthBarriers[0].subresourceRange.baseArrayLayer = 0;
                taaDepthBarriers[0].subresourceRange.layerCount = 1;
                taaDepthBarriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                taaDepthBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                taaDepthBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                taaDepthBarriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                taaDepthBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                taaDepthBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                taaDepthBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                taaDepthBarriers[1].image = passes.taa.depthHistoryImage(taaHistoryWriteIndex);
                taaDepthBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                taaDepthBarriers[1].subresourceRange.baseMipLevel = 0;
                taaDepthBarriers[1].subresourceRange.levelCount = 1;
                taaDepthBarriers[1].subresourceRange.baseArrayLayer = 0;
                taaDepthBarriers[1].subresourceRange.layerCount = 1;
                taaDepthBarriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                taaDepthBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                                     2, taaDepthBarriers);

                VkImageCopy taaCopy{};
                taaCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                taaCopy.srcSubresource.mipLevel = 0;
                taaCopy.srcSubresource.baseArrayLayer = 0;
                taaCopy.srcSubresource.layerCount = 1;
                taaCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                taaCopy.dstSubresource.mipLevel = 0;
                taaCopy.dstSubresource.baseArrayLayer = 0;
                taaCopy.dstSubresource.layerCount = 1;
                taaCopy.extent = {passCreateInfo_.extent.width, passCreateInfo_.extent.height, 1};

                vkCmdCopyImage(cmd, passes.gbuffer.depth(fc.frameIndex).image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               passes.taa.depthHistoryImage(taaHistoryWriteIndex),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &taaCopy);

                taaDepthBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                taaDepthBarriers[0].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                taaDepthBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                taaDepthBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                taaDepthBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                taaDepthBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                taaDepthBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                taaDepthBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                                     nullptr, 2, taaDepthBarriers);
            }

            gpuProfiler_.endScope(cmd, gpuScopeTaa);
        }
        else
        {
            postFxSettings_.taaResetHistory_ = true;
        }
    });
    postProcessPasses.add(
        "spatial-upscale",
        [&](const engine::render::RenderPassContext& context) {
            if (passes.spatialUpscale.active())
            {
                const uint32_t gpuScopeUpscale =
                    gpuProfiler_.beginScope(cmd, "Spatial Upscale");
                passes.spatialUpscale.updateInput(ctx_, fc.frameIndex,
                                                  resolvedSceneView);
                passes.spatialUpscale.record(
                    ctx_, fc, context.frame.worldState->postFx.renderResolution_);
                passes.composite.updateTaaView(
                    ctx_, fc.frameIndex,
                    passes.spatialUpscale.outputView(fc.frameIndex),
                    passes.spatialUpscale.sampler());
                gpuProfiler_.endScope(cmd, gpuScopeUpscale);
            }
            else
            {
                passes.composite.updateTaaView(ctx_, fc.frameIndex,
                                               resolvedSceneView,
                                               resolvedSceneSampler);
            }
        });
    renderer_.recordPasses(postProcessPasses, frameInputs, fc);
}

void App::recordTemporalDepthHistoryCopy(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes)
{
    const int historyWriteIndex = passes.depthHistory.index;
    VkImageMemoryBarrier depthBarriers[2]{};

    depthBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depthBarriers[0].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    depthBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarriers[0].image = passes.gbuffer.depth(fc.frameIndex).image;
    depthBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthBarriers[0].subresourceRange.baseMipLevel = 0;
    depthBarriers[0].subresourceRange.levelCount = 1;
    depthBarriers[0].subresourceRange.baseArrayLayer = 0;
    depthBarriers[0].subresourceRange.layerCount = 1;
    depthBarriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depthBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    depthBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depthBarriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    depthBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarriers[1].image = passes.depthHistory.images[historyWriteIndex];
    depthBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthBarriers[1].subresourceRange.baseMipLevel = 0;
    depthBarriers[1].subresourceRange.levelCount = 1;
    depthBarriers[1].subresourceRange.baseArrayLayer = 0;
    depthBarriers[1].subresourceRange.layerCount = 1;
    depthBarriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depthBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                         depthBarriers);

    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    copy.srcSubresource.mipLevel = 0;
    copy.srcSubresource.baseArrayLayer = 0;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    copy.dstSubresource.mipLevel = 0;
    copy.dstSubresource.baseArrayLayer = 0;
    copy.dstSubresource.layerCount = 1;
    copy.extent = {passCreateInfo_.extent.width, passCreateInfo_.extent.height, 1};

    vkCmdCopyImage(cmd, passes.gbuffer.depth(fc.frameIndex).image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   passes.depthHistory.images[historyWriteIndex],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    depthBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    depthBarriers[0].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    depthBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    depthBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    depthBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    depthBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, depthBarriers);
}

void App::recordCompositeUiPass(
    VkCommandBuffer cmd,
    const FrameContext& fc,
    engine::render::RendererPassResources& passes,
    engine::render::FrameInputs& frameInputs,
    const engine::scene::WorldStateView& frameWorldState,
    int compositeMode,
    const glm::mat4& invViewProjUnjittered,
    float animationTimeSeconds,
    uint32_t activeWaterVolumeCount)
{
    passes.composite.setViewMode(compositeMode);
    const auto& compositePostFx = frameWorldState.postFx;
    const bool debugViewsAllowTonemap =
        renderPipelineShowcase_.enabled() ||
        (!input_.cascadeDebug() && input_.waterDebugMode() == 0 &&
         input_.glassDebugMode() == 0 && compositePostFx.taaDebugMode_ == 0);
    const bool tonemapEnabled =
        compositePostFx.tonemapEnabled_ && debugViewsAllowTonemap;
    passes.composite.setTonemapEnabled(tonemapEnabled);
    passes.composite.setExposure(compositePostFx.exposure_);
    passes.composite.setHighlightRecovery(compositePostFx.highlightRecovery_);
    passes.composite.setBloomEnabled(compositePostFx.bloomEnabled_);
    passes.composite.setBloomIntensity(compositePostFx.bloomIntensity_);
    passes.composite.setApplyBloomInComposite(compositePostFx.bloomEnabled_ &&
                                              !frameInputs.runTaa);
    passes.composite.setVignetteStrength(compositePostFx.vignetteStrength_);
    passes.composite.setGrainStrength(compositePostFx.grainStrength_);
    passes.composite.setSharpenIntensity(
        passes.spatialUpscale.active() ? 0.0f : compositePostFx.taaSharpen_);
    passes.composite.setFxaaEnabled(compositePostFx.fxaaEnabled_);
    passes.composite.setColorGradeEnabled(compositePostFx.colorGradeEnabled_);
    passes.composite.setColorGradeStrength(compositePostFx.colorGradeStrength_);
    passes.composite.setColorGradeSaturation(compositePostFx.colorGradeSaturation_);
    passes.composite.setColorGradeContrast(compositePostFx.colorGradeContrast_);
    passes.composite.setColorGradeTemperature(compositePostFx.colorGradeTemperature_);
    passes.composite.setPixelizationEnabled(compositePostFx.postPixelizationEnabled_);
    passes.composite.setPixelizationBlockSize(compositePostFx.postPixelizationBlockSize_);
    passes.composite.setPixelizationStrength(compositePostFx.postPixelizationStrength_);
    passes.composite.setPixelizationEdgeFocus(compositePostFx.postPixelizationEdgeFocus_);
    passes.composite.setMaterialDetailStrength(compositePostFx.postMaterialDetailStrength_);
    passes.composite.setTime(animationTimeSeconds);
    passes.composite.setFrameIndex(taaFrameIndex_);
    passes.composite.setStarsEnabled(sceneConfig().loadStarScene);
    passes.composite.setStarSeed(sceneConfig().starSeed);
    passes.composite.setStarDensity(sceneConfig().starDensity);
    passes.composite.setStarTwinkleSpeed(1.0f);
    passes.composite.setInvViewProj(invViewProjUnjittered);
    const bool fishFocusDofActive = isFishFocusActive() && !isFishFocusReturning();
    passes.composite.setDofEnabled(compositePostFx.dofEnabled_ || fishFocusDofActive);
    passes.composite.setDofFocusDistance(fishFocusDofActive ? fishFocusCurrentDistance_
                                                            : compositePostFx.dofFocusDistance_);
    passes.composite.setDofFocusRange(compositePostFx.dofFocusRange_);
    passes.composite.setDofBlurStrength(compositePostFx.dofBlurStrength_);
    passes.composite.setCameraPosition(camera_.position);
    passes.composite.setSceneAtmosphere(
        environmentTimeSample_.presentation.sceneAtmosphere);
    passes.composite.setActiveWaterVolumeCount(activeWaterVolumeCount);

    engine::render::PassRegistry compositePasses;
    compositePasses.add("composite-ui", [&](const engine::render::RenderPassContext&) {
        const uint32_t gpuScopeComposite = gpuProfiler_.beginScope(cmd, "Composite+UI");
        passes.composite.record(ctx_, fc);

#if VOXEL_WITH_RUNTIME_UI
        buildRuntimeUiDrawList(fc.extent);
        const UiOverlayDrawStats runtimeUiStats =
            passes.uiOverlay.record(ctx_, fc, runtimeUiDrawList_);
        if (runtimeUiContext_.activeScreenId() == ui::kOverlaySmokeScreenId &&
            !runtimeUiOverlaySmokeLogged_ && runtimeUiStats.commandCount > 0)
        {
            logInfo("RuntimeUI",
                    makeLogMessage("Overlay smoke draw list rendered commands=",
                                   runtimeUiStats.commandCount,
                                   " vertices=", runtimeUiStats.vertexCount,
                                   " indices=", runtimeUiStats.indexCount));
            runtimeUiOverlaySmokeLogged_ = true;
        }
#endif

#if VOXEL_WITH_EDITOR
        // render ImGui within the composite pass's render pass.
        imgui_.render(cmd, fc.extent);
#endif
        vkCmdEndRenderPass(cmd);
#if VOXEL_WITH_RUNTIME_UI
        recordRuntimeUiPixelInspectCopy(cmd, fc);
#endif
        gpuProfiler_.endScope(cmd, gpuScopeComposite);
    });
    renderer_.recordPasses(compositePasses, frameInputs, fc);
}
