#include "App/App.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "Core/Logger.h"
#include "engine/render/AuxiliaryRayResolution.h"
#include "engine/render/AmbientOcclusionQualityTier.h"
#include "engine/render/RenderQualityPreset.h"
#include "engine/render/RenderResolution.h"

namespace
{

std::vector<GpuProfilerSample> collectTopGpuPassSamples(
    const std::vector<GpuProfilerSample>& samples,
    size_t maxCount)
{
    std::vector<GpuProfilerSample> topSamples{};
    topSamples.reserve(samples.size());
    for (const GpuProfilerSample& sample : samples)
    {
        if (sample.label != "Frame")
        {
            topSamples.push_back(sample);
        }
    }

    std::sort(topSamples.begin(), topSamples.end(),
              [](const GpuProfilerSample& lhs, const GpuProfilerSample& rhs) {
                  return lhs.ms > rhs.ms;
              });
    if (topSamples.size() > maxCount)
    {
        topSamples.resize(maxCount);
    }
    return topSamples;
}

const GpuProfilerSample* findGpuPassSample(
    const std::vector<GpuProfilerSample>& samples,
    const char* label)
{
    if (label == nullptr)
    {
        return nullptr;
    }

    const auto it = std::find_if(
        samples.begin(), samples.end(),
        [label](const GpuProfilerSample& sample) { return sample.label == label; });
    return it == samples.end() ? nullptr : &(*it);
}

}  // namespace

void App::configureAutomationFixedSceneTime(const std::string& value)
{
    if (value.empty())
    {
        return;
    }
    const std::optional<double> parsed =
        engine::performance::parseFixedSceneTimeSeconds(value);
    if (!parsed.has_value() || !voxelDebugSettings_.voxelFreezeTime_)
    {
        logWarning("Automation",
                   makeLogMessage("Ignoring --automation-fixed-scene-time-seconds value: ",
                                  value, "; a finite non-negative value and a scene/time "
                                         "freeze are required."));
        return;
    }
    frozenTimeSeconds_ = *parsed;
    freezeTimeWasEnabled_ = true;
    logInfo("Automation",
            makeLogMessage("Fixed scene time requested: ", frozenTimeSeconds_, " seconds."));
}

void App::configureAutomationRenderQualityPreset(const std::string& value)
{
    if (value.empty())
    {
        return;
    }

    const std::optional<engine::render::RenderQualityPreset> preset =
        engine::render::parseRenderQualityPreset(value);
    if (!preset.has_value())
    {
        logWarning("Automation",
                   makeLogMessage("Ignoring invalid --automation-render-quality-preset value: ",
                                  value, "."));
        return;
    }

    const engine::render::RenderQualityPresetSettings settings =
        engine::render::renderQualityPresetSettings(*preset);
    postFxSettings_.renderResolution_.scale = settings.renderScale;
    shadowSettings_.ddaRayResolutionScale_ = settings.shadowRayScale;
    aoSettings_.aoRayResolutionScale_ = settings.aoRayScale;
    shadowSettings_.adaptiveRayResolution_ = false;
    aoSettings_.adaptiveRayResolution_ = false;
    logInfo("Automation",
            makeLogMessage("Render quality preset requested: ",
                           engine::render::renderQualityPresetId(*preset),
                           " (renderScale=", settings.renderScale,
                           ", shadowRayScale=", settings.shadowRayScale,
                           ", aoRayScale=", settings.aoRayScale, ")."));
}

void App::configureAutomationRenderScale(const std::string& value)
{
    if (value.empty())
    {
        return;
    }
    try
    {
        size_t consumed = 0;
        const float requestedScale = std::stof(value, &consumed);
        if (consumed != value.size())
        {
            throw std::invalid_argument("trailing render-scale characters");
        }
        postFxSettings_.renderResolution_.scale =
            engine::render::sanitizeRenderScale(requestedScale);
        logInfo("Automation",
                makeLogMessage("Internal render scale requested: ",
                               postFxSettings_.renderResolution_.scale, "."));
    }
    catch (const std::exception&)
    {
        logWarning("Automation",
                   std::string("Ignoring invalid --automation-render-scale value: ") + value);
    }
}

void App::configureAutomationAuxiliaryRayScales(const std::string& shadowScale,
                                                const std::string& aoScale)
{
    const auto applyScale = [](const std::string& value, float& target,
                               const char* label) {
        if (value.empty())
        {
            return;
        }
        try
        {
            size_t consumed = 0;
            const float requested = std::stof(value, &consumed);
            if (consumed != value.size() || !std::isfinite(requested))
            {
                throw std::invalid_argument("invalid auxiliary ray scale");
            }
            target = engine::render::sanitizeAuxiliaryRayScale(requested);
            logInfo("Automation", makeLogMessage(label, " ray scale requested: ", target,
                                                  "."));
        }
        catch (const std::exception&)
        {
            logWarning("Automation",
                       makeLogMessage("Ignoring invalid ", label, " ray scale: ", value));
        }
    };

    applyScale(shadowScale, shadowSettings_.ddaRayResolutionScale_, "DDA shadow");
    applyScale(aoScale, aoSettings_.aoRayResolutionScale_, "AO");
    if (!shadowScale.empty())
    {
        shadowSettings_.adaptiveRayResolution_ = false;
    }
    if (!aoScale.empty())
    {
        aoSettings_.adaptiveRayResolution_ = false;
    }
}

void App::configureAutomationUncapped(bool enabled)
{
    if (!enabled)
    {
        return;
    }

    presentMode_ = SwapPresentMode::Immediate;
    framePacingSettings_.maxFpsLimit_ = 0.0f;
    framePacingSettings_.focusedIdleThrottleEnabled_ = false;
    framePacingSettings_.backgroundThrottleEnabled_ = false;
    logInfo("Automation",
            "Uncapped profiling requested: immediate presentation, frame cap off, and "
            "idle/background throttles disabled.");
}

void App::configureAutomationAoProjectedDistanceTier(
    const std::string& radiusPixels,
    const std::string& minimumDistance)
{
    automationAoProjectedRadiusPixels_ = 0.0f;
    automationAoProjectedMinDistance_ = 2.0f;
    try
    {
        if (!minimumDistance.empty())
        {
            automationAoProjectedMinDistance_ = std::stof(minimumDistance);
        }
        if (!radiusPixels.empty())
        {
            automationAoProjectedRadiusPixels_ = std::stof(radiusPixels);
        }
    }
    catch (const std::exception&)
    {
        automationAoProjectedRadiusPixels_ = 0.0f;
    }

    const engine::render::ProjectedAoDistanceTier tier{
        automationAoProjectedRadiusPixels_, automationAoProjectedMinDistance_};
    if (!radiusPixels.empty() && tier.enabled() &&
        std::isfinite(automationAoProjectedMinDistance_) &&
        automationAoProjectedMinDistance_ >= 0.0f)
    {
        logInfo("Automation",
                makeLogMessage("Projected AO distance tier enabled: radiusPixels=",
                               automationAoProjectedRadiusPixels_,
                               " minimumDistance=", automationAoProjectedMinDistance_,
                               " m; authored max distance and ray count retained."));
        return;
    }
    if (!radiusPixels.empty() || !minimumDistance.empty())
    {
        automationAoProjectedRadiusPixels_ = 0.0f;
        automationAoProjectedMinDistance_ = 2.0f;
        logWarning("Automation", "Ignoring invalid projected AO distance tier arguments.");
    }
}

void App::dumpAutomationDdaMetricsSummary() const
{
    if (!automationLogDdaMetrics_)
    {
        return;
    }

    const uint32_t totalHitMiss = cachedDdaMetrics_.hitCount + cachedDdaMetrics_.missCount;
    const float sampleCount = static_cast<float>(cachedDdaMetrics_.sampleCount);
    const auto average = [sampleCount](uint32_t sum) {
        return sampleCount > 0.0f ? static_cast<float>(sum) / sampleCount : 0.0f;
    };
    const float hitRate =
        totalHitMiss > 0 ? static_cast<float>(cachedDdaMetrics_.hitCount) /
                               static_cast<float>(totalHitMiss)
                         : 0.0f;
    std::ostringstream summary;
    summary << std::fixed << std::setprecision(2) << "DDA metrics summary for scene '"
            << sceneConfig().name << "': samples=" << cachedDdaMetrics_.sampleCount
            << ", hitRate=" << (hitRate * 100.0f)
            << "%, avgIterations=" << average(cachedDdaMetrics_.sumIters)
            << ", avgSkipJumps=" << average(cachedDdaMetrics_.sumSkipJumps)
            << ", avgHierarchyDescents="
            << average(cachedDdaMetrics_.sumHierarchyDescents)
            << ", avgFineIterations=" << average(cachedDdaMetrics_.sumFineIters)
            << ", avgHierarchyProbes=" << average(cachedDdaMetrics_.sumHierarchyProbes)
            << ", hitSignature=(" << cachedDdaMetrics_.hitSignatureXor << ","
            << cachedDdaMetrics_.hitSignatureSum << ")"
            << ", surfaceSignature=(" << cachedDdaMetrics_.surfaceSignatureXor << ","
            << cachedDdaMetrics_.surfaceSignatureSum << ")"
            << ", skipEnabled="
            << (voxelDebugSettings_.voxelDdaSkipEnabled_ && !automationDisableEmptySkip_
                    ? "true"
                    : "false")
            << ", skipMip=" << voxelDebugSettings_.voxelDdaSkipMip_
            << ", stride=" << diagnosticsSettings_.voxelMetricsSampleStride_
            << ", offset=(" << voxelMetricsSampleOffset_.x << ", "
            << voxelMetricsSampleOffset_.y << ").";
    logInfo("Automation", summary.str());
}

void App::dumpAutomationGpuProfileSummary() const
{
    if (!automationLogGpuProfile_)
    {
        return;
    }
    if (!gpuProfiler_.isSupported())
    {
        logWarning("Automation",
                   "GPU profile dump requested but timestamp queries are unsupported.");
        return;
    }
    if (!gpuProfiler_.isEnabled())
    {
        logWarning("Automation",
                   "GPU profile dump requested but the GPU profiler is disabled.");
        return;
    }

    const engine::performance::AutomationProfileSummary profileSummary =
        automationProfileSession_.summary();
    const std::vector<GpuProfilerSample>& samples = profileSummary.averagePasses;
    if (samples.empty() || profileSummary.sampledFrames == 0)
    {
        logWarning("Automation",
                   "GPU profile dump requested but no resolved GPU samples are available yet.");
        return;
    }

    std::ostringstream summary;
    summary << std::fixed << std::setprecision(2) << "GPU profile summary for scene '"
            << sceneConfig().name << "': avg CPU " << profileSummary.cpu.averageMs
            << " ms, avg GPU " << profileSummary.gpu.averageMs
            << " ms, profiledFrames=" << profileSummary.sampledFrames
            << ", renderedFrames=" << renderedFrameCount_ << ", present="
            << activePresentModeLabel() << ", cap=" << effectiveFpsLimit_ << " FPS.";
    logInfo("Automation", summary.str());

    std::ostringstream statistics;
    statistics << std::fixed << std::setprecision(2)
               << "GPU profile statistics: CPU median "
               << profileSummary.cpu.medianMs << " ms, p95 " << profileSummary.cpu.p95Ms
               << " ms, p99 " << profileSummary.cpu.p99Ms << " ms, min "
               << profileSummary.cpu.minMs << " ms, max " << profileSummary.cpu.maxMs
               << " ms; GPU median " << profileSummary.gpu.medianMs << " ms, p95 "
               << profileSummary.gpu.p95Ms << " ms, p99 " << profileSummary.gpu.p99Ms
               << " ms, min " << profileSummary.gpu.minMs << " ms, max "
               << profileSummary.gpu.maxMs << " ms.";
    logInfo("Automation", statistics.str());

    const VkExtent2D extent = renderer_.swapchainExtent();
    VkPhysicalDeviceDriverProperties driverProperties{};
    driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 deviceProperties2{};
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &driverProperties;
    vkGetPhysicalDeviceProperties2(ctx_.gpu, &deviceProperties2);
    const VkPhysicalDeviceProperties& deviceProperties = deviceProperties2.properties;
    size_t visibleOccupiedVolumes = 0;
    size_t staticVolumes = 0;
    size_t dynamicVolumes = 0;
    for (const engine::VoxelInstance& instance : voxelWorld_.instances())
    {
        if (!instance.visible || !instance.volume.hasOccupiedVoxels())
        {
            continue;
        }
        ++visibleOccupiedVolumes;
        const uint32_t flags = instance.volume.flags();
        staticVolumes += (flags & engine::VoxelVolume::FLAG_STATIC) != 0u ? 1u : 0u;
        dynamicVolumes += (flags & engine::VoxelVolume::FLAG_DYNAMIC) != 0u ? 1u : 0u;
    }
    size_t opaqueOnlyDdaVolumes = 0;
    size_t unwrappedOpaqueDdaVolumes = 0;
    size_t cameraInsideDdaVolumes = 0;
    size_t faceCullingSafeDdaVolumes = 0;
    size_t reversedWindingDdaVolumes = 0;
    for (const auto& entry :
         voxelRenderResources_.volumeDrawPlan().mainDdaVolumes)
    {
        opaqueOnlyDdaVolumes += entry.opaqueOnlyDda ? 1u : 0u;
        unwrappedOpaqueDdaVolumes += entry.unwrappedOpaqueDda ? 1u : 0u;
        cameraInsideDdaVolumes += entry.cameraInsideRasterBounds ? 1u : 0u;
        faceCullingSafeDdaVolumes += entry.faceCullingSafe ? 1u : 0u;
        reversedWindingDdaVolumes += entry.frontFaceWindingReversed ? 1u : 0u;
    }
    const size_t wrappedOpaqueDdaVolumes =
        opaqueOnlyDdaVolumes - unwrappedOpaqueDdaVolumes;
    const size_t transmissiveDdaVolumes =
        voxelRenderResources_.volumeDrawPlan().mainDdaVolumes.size() -
        opaqueOnlyDdaVolumes;
#if defined(VOXEL_PLATFORM_MACOS)
    constexpr const char* kPlatformLabel = "macOS";
#elif defined(VOXEL_PLATFORM_WINDOWS)
    constexpr const char* kPlatformLabel = "Windows";
#else
    constexpr const char* kPlatformLabel = "Unknown";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    constexpr const char* kArchitectureLabel = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    constexpr const char* kArchitectureLabel = "x64";
#else
    constexpr const char* kArchitectureLabel = "unknown";
#endif
    std::ostringstream metadata;
    metadata << "GPU profile metadata: platform=" << kPlatformLabel
             << ", architecture=" << kArchitectureLabel
             << ", build=" << DDA_VOXEL_BUILD_CONFIGURATION
             << ", editor=" << (VOXEL_WITH_EDITOR ? 1 : 0)
             << ", gpu='" << deviceProperties.deviceName << "'"
             << ", api=" << VK_VERSION_MAJOR(deviceProperties.apiVersion) << "."
             << VK_VERSION_MINOR(deviceProperties.apiVersion) << "."
             << VK_VERSION_PATCH(deviceProperties.apiVersion)
             << ", driverName='" << driverProperties.driverName << "'"
             << ", driverInfo='" << driverProperties.driverInfo << "'"
             << ", driverVersion=" << VK_VERSION_MAJOR(deviceProperties.driverVersion) << "."
             << VK_VERSION_MINOR(deviceProperties.driverVersion) << "."
             << VK_VERSION_PATCH(deviceProperties.driverVersion)
             << ", swapchain=" << extent.width << "x" << extent.height
             << ", renderTarget=" << passCreateInfo_.extent.width << "x"
             << passCreateInfo_.extent.height
             << ", totalVolumes=" << voxelWorld_.instances().size()
             << ", visibleOccupiedVolumes=" << visibleOccupiedVolumes
             << ", staticVolumes=" << staticVolumes
             << ", dynamicVolumes=" << dynamicVolumes
             << ", opaqueOnlyDdaVolumes=" << opaqueOnlyDdaVolumes
             << ", unwrappedOpaqueDdaVolumes=" << unwrappedOpaqueDdaVolumes
             << ", wrappedOpaqueDdaVolumes=" << wrappedOpaqueDdaVolumes
             << ", transmissiveDdaVolumes=" << transmissiveDdaVolumes
             << ", cameraInsideDdaVolumes=" << cameraInsideDdaVolumes
             << ", faceCullingSafeDdaVolumes=" << faceCullingSafeDdaVolumes
             << ", reversedWindingDdaVolumes=" << reversedWindingDdaVolumes
             << ", alignedPrimaryTraversalPairs="
             << voxelRenderResources_.volumeDrawPlan()
                    .alignedPrimaryTraversalPairCount
             << ", foliageRenderer="
             << engine::render::foliageRendererModeName(
                    renderPasses().foliage.requestedRenderer())
             << ", foliageInstances=" << renderPasses().foliage.instanceCount()
             << ", foliagePatches=" << renderPasses().foliage.patchCount()
             << ", foliageTopology=" << renderPasses().foliage.topologyName()
             << ", foliagePalette="
             << renderPasses().foliage.paletteResponseName()
             << ", foliagePrimitives=" << renderPasses().foliage.primitiveCount()
             << ", foliageVerticesPerPrimitive="
             << renderPasses().foliage.verticesPerPrimitive()
             << ", foliageSubmittedVertices="
             << renderPasses().foliage.submittedVertices()
             << ", foliageBatches=" << renderPasses().foliage.batchCount()
             << ", foliageGpuBytes=" << renderPasses().foliage.gpuBytes()
             << ", foliageResidentGpuBytes="
             << renderPasses().foliage.residentGpuBytes()
             << ", windborneParticles="
             << renderPasses().foliage.activeWindborneParticleCount()
             << ", windborneResidentParticles="
             << renderPasses().foliage.residentWindborneParticleCount()
             << ", windborneSubmittedVertices="
             << renderPasses().foliage.submittedWindborneVertices()
             << ", windborneResidentGpuBytes="
             << renderPasses().foliage.residentWindborneGpuBytes()
             << ", sunShadowVolumes="
             << voxelRenderResources_.volumeDrawPlan()
                    .sunShadowOccluderVolumes.size()
             << ", terrainShadowColumnVolumes="
             << voxelRenderResources_.volumeDrawPlan()
                    .terrainShadowColumnVolumeCount
             << ", aoVolumes="
             << voxelRenderResources_.volumeDrawPlan()
                    .localLightingOccluderVolumes.size()
             << ", localShadowVolumes="
             << voxelRenderResources_.volumeDrawPlan()
                    .localShadowOccluderVolumes.size()
             << ".";
    logInfo("Automation", metadata.str());

    std::ostringstream effects;
    const engine::render::ProjectedAoDistanceTier configuredAoTier =
        engine::render::projectedAoDistanceTier(
            aoSettings_.aoDistanceMode_, aoSettings_.aoProjectedRadiusPixels_,
            aoSettings_.aoProjectedMinDistance_);
    const engine::render::ProjectedAoDistanceTier effectiveAoTier =
        engine::render::resolveProjectedAoDistanceTier(
            configuredAoTier,
            {automationAoProjectedRadiusPixels_, automationAoProjectedMinDistance_});
    const engine::render::SunShadowSamplingDecision shadowSampling =
        renderPasses().shadowRayPass.samplingDecision(
            shadowSettings_.shadowDdaSunSampleCount_);
    const ShadowRayPass::TerrainShadowColumnMode terrainShadowColumnMode =
        renderPasses().shadowRayPass.terrainShadowColumnMode();
    const char* terrainShadowColumnModeId =
        terrainShadowColumnMode ==
                ShadowRayPass::TerrainShadowColumnMode::ValidateParity
            ? "parity"
            : terrainShadowColumnMode ==
                      ShadowRayPass::TerrainShadowColumnMode::Enabled
                  ? "enabled"
                  : "disabled";
    effects << "GPU profile effects: DDA=" << (voxelDebugSettings_.voxelVisible_ ? 1 : 0)
            << ", DDAEmptySkip=" << (voxelDebugSettings_.voxelDdaSkipEnabled_ ? 1 : 0)
            << ", DDAFaceCull=1"
            << ", DDAAlignedLayerTraversal="
            << (voxelDebugSettings_.voxelAlignedLayerTraversalEnabled_ ? 1 : 0)
            << ", foliageSway=" << (renderPasses().foliage.swayEnabled() ? 1 : 0)
            << ", foliageCurrentTime=" << renderPasses().foliage.currentTimeSeconds()
            << ", foliagePreviousTime=" << renderPasses().foliage.previousTimeSeconds()
            << ", CSM=" << (shadowSettings_.csmEnabled_ ? 1 : 0)
            << ", DDAShadows=" << (shadowSettings_.useDDAShadows_ ? 1 : 0)
            << ", terrainShadowColumns="
            << (renderPasses().shadowRayPass.terrainShadowColumnsEnabled() ? 1 : 0)
            << ", terrainShadowColumnMode=" << terrainShadowColumnModeId
            << ", sunShadowSampling="
            << engine::render::sunShadowSamplingModeId(
                   renderPasses().shadowRayPass.samplingMode())
            << ", sunShadowSamplesAuthored=" << shadowSampling.authoredSampleCount
            << ", sunShadowSamplesEffective="
            << renderPasses().shadowRayPass.lastDispatchedSunSampleCount()
            << ", sunShadowNeighborhoodClamp="
            << (renderPasses().shadowTemporalResolve.config().useNeighborhoodClamp ? 1 : 0)
            << ", sceneTimeFrozen=" << (voxelDebugSettings_.voxelFreezeTime_ ? 1 : 0)
            << ", sceneTimeSeconds=" << frozenTimeSeconds_
            << ", localShadows=" << (shadowSettings_.localLightShadowsEnabled_ ? 1 : 0)
            << ", AO=" << (aoSettings_.aoEnabled_ ? 1 : 0)
            << ", AODistanceMode="
            << engine::render::ambientOcclusionDistanceModeName(
                   aoSettings_.aoDistanceMode_)
            << ", AOProjectedOverride="
            << (automationAoProjectedRadiusPixels_ > 0.0f ? 1 : 0)
            << ", AOProjectedRadiusPx=" << effectiveAoTier.radiusPixels
            << ", AOProjectedMinDistance=" << effectiveAoTier.minimumWorldDistance
            << ", shadowRayScale=" << shadowSettings_.ddaRayResolutionScale_
            << ", aoRayScale=" << aoSettings_.aoRayResolutionScale_
            << ", water=" << (waterSettings_.waterEnabled_ ? 1 : 0)
            << ", glass=" << (glassSettings_.glassEnabled_ ? 1 : 0)
            << ", atmosphere="
            << (lightingSettings_.sceneAtmosphere_.enabled() ? 1 : 0)
            << ", paintedSky=" << (lightingSettings_.paintedSky_.enabled() ? 1 : 0)
            << ", paintedClouds="
            << (lightingSettings_.paintedClouds_.enabled() ? 1 : 0)
            << ", environmentTime="
            << (environmentTimeSample_.active ? 1 : 0)
            << ", environmentHour=" << environmentTimeSample_.hour
            << ", environmentPhase="
            << engine::scene::environmentTimePhaseLabel(
                   environmentTimeSample_.phase)
            << ", environmentClockAdvancing="
            << (sceneConfig().environmentTime.enabled &&
                        sceneConfig().environmentTime.cycleEnabled &&
                        !voxelDebugSettings_.voxelFreezeTime_
                    ? 1
                    : 0)
            << ", bloom=" << (postFxSettings_.bloomEnabled_ ? 1 : 0)
            << ", TAA=" << (postFxSettings_.taaEnabled_ ? 1 : 0)
            << ", renderScale=" << postFxSettings_.renderResolution_.scale
            << ", spatialUpscaler="
            << engine::render::spatialUpscaleModeName(
                   postFxSettings_.renderResolution_.upscaleMode)
            << ", FXAA=" << (postFxSettings_.fxaaEnabled_ ? 1 : 0) << ".";
    logInfo("Automation", effects.str());

    if (terrainShadowColumnMode ==
        ShadowRayPass::TerrainShadowColumnMode::ValidateParity)
    {
        const ShadowRayPass::TerrainShadowColumnParityTotals parity =
            renderPasses().shadowRayPass.terrainShadowColumnParityTotals();
        std::ostringstream parityDetails;
        parityDetails << std::setprecision(9)
                      << "Terrain shadow-column raw parity: comparedRays="
                      << parity.comparedRays
                      << ", mismatchedRays=" << parity.mismatchedRays
                      << ", candidateOnlyRays=" << parity.candidateOnlyRays
                      << ", referenceOnlyRays=" << parity.referenceOnlyRays
                      << ", firstMismatch=" << parity.hasFirstMismatch
                      << ", firstCandidate=" << parity.firstCandidateOccluded
                      << ", firstReference=" << parity.firstReferenceOccluded
                      << ", firstVolume=" << parity.firstVolumeIndex
                      << ", firstOrigin=" << parity.firstLocalOrigin.x << ","
                      << parity.firstLocalOrigin.y << ","
                      << parity.firstLocalOrigin.z
                      << ", firstDirection="
                      << parity.firstLocalDirection.x << ","
                      << parity.firstLocalDirection.y << ","
                      << parity.firstLocalDirection.z
                      << ", firstMaxT=" << parity.firstMaxT
                      << ", firstMaxSteps=" << parity.firstMaxSteps << ".";
        logInfo("Automation", parityDetails.str());
    }

    std::ostringstream window;
    window << "GPU profile window: waitForSceneReady="
           << (automationProfileWaitForSceneReady_ ? 1 : 0)
           << ", warmupFrames=" << profileSummary.warmupFrames
           << ", sampledFrames=" << profileSummary.sampledFrames << ".";
    logInfo("Automation", window.str());

    const std::vector<GpuProfilerSample> topSamples =
        collectTopGpuPassSamples(samples, 6);
    std::ostringstream topSummary;
    topSummary << std::fixed << std::setprecision(2) << "Top GPU passes:";
    for (const GpuProfilerSample& sample : topSamples)
    {
        topSummary << ' ' << sample.label << '=' << sample.ms << " ms;";
    }
    logInfo("Automation", topSummary.str());

    constexpr std::array<const char*, 17> kDecisionPassLabels = {
        "Voxel DDA",
        "Foliage",
        "Shadow Rays",
        "Shadow Resolve",
        "AO Rays",
        "AO Resolve",
        "Local Shadows",
        "Local Shadow Resolve",
        "Water Volume Prepass",
        "Water Body",
        "Planar Water Volume Prepass",
        "Planar Reflection",
        "Water",
        "Lighting",
        "TAA",
        "Spatial Upscale",
        "Composite+UI",
    };

    std::ostringstream decisionSummary;
    decisionSummary << std::fixed << std::setprecision(2) << "Decision GPU passes:";
    for (const char* label : kDecisionPassLabels)
    {
        const GpuProfilerSample* sample = findGpuPassSample(samples, label);
        if (sample != nullptr)
        {
            decisionSummary << ' ' << label << '=' << sample->ms << " ms;";
        }
        else
        {
            decisionSummary << ' ' << label << "=n/a;";
        }
    }
    logInfo("Automation", decisionSummary.str());

    if (diagnosticsSettings_.shadowRayAuditEnabled_)
    {
        std::ostringstream shadowVolumes;
        shadowVolumes << std::fixed << std::setprecision(2)
                      << "Shadow Rays per-volume GPU scopes:";
        for (const auto& entry :
             voxelRenderResources_.volumeDrawPlan().sunShadowOccluderVolumes)
        {
            if (entry.volumeIndex >= voxelWorld_.instances().size())
            {
                continue;
            }
            const std::string& name =
                voxelWorld_.instances()[entry.volumeIndex].volume.debugName();
            const GpuProfilerSample* sample =
                findGpuPassSample(samples, name.c_str());
            shadowVolumes << ' ' << name << '[' << entry.volumeIndex << "]=";
            if (sample != nullptr)
            {
                shadowVolumes << sample->ms << " ms;";
            }
            else
            {
                shadowVolumes << "n/a;";
            }
        }
        logInfo("Automation", shadowVolumes.str());
    }
}
