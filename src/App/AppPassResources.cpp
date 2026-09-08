#include "App/App.h"
#include <algorithm>
#include <string>

#include "Core/Logger.h"
#include "engine/render/AuxiliaryRayResolution.h"

void App::createPassResources()
{
    auto& passes = renderPasses();

    TemporalResolveConfig shadowConfig{};
    shadowConfig.blendAlpha = shadowSettings_.shadowBlendAlpha_;
    shadowConfig.blendAlphaMax = 1.0f;
    shadowConfig.depthRejectThreshold = shadowSettings_.shadowDepthReject_;
    shadowConfig.useNormalReject = true;
    shadowConfig.normalRejectDot = shadowSettings_.shadowNormalRejectDot_;
    shadowConfig.useNeighborhoodClamp = true;
    shadowConfig.clampSharpness = shadowSettings_.shadowClampSharpness_;
    shadowConfig.useSpatialFilter = shadowSettings_.shadowSpatialFilterRadius_ > 0;
    shadowConfig.spatialFilterReconstructedCurrent = shadowConfig.useSpatialFilter;
    shadowConfig.spatialFilterRadius =
        static_cast<uint32_t>(std::max(shadowSettings_.shadowSpatialFilterRadius_, 0));
    shadowConfig.spatialDepthSigma = shadowSettings_.shadowSpatialDepthSigma_;
    shadowConfig.spatialValueSigma = shadowSettings_.shadowSpatialValueSigma_;
    shadowConfig.spatialNormalPower = shadowSettings_.shadowSpatialNormalPower_;
    shadowConfig.format = VK_FORMAT_R16_SFLOAT;

    LocalLightTemporalResolveConfig localShadowConfig{};
    localShadowConfig.blendAlpha = shadowSettings_.localShadowBlendAlpha_;
    localShadowConfig.blendAlphaMax = 1.0f;
    localShadowConfig.depthRejectThreshold = shadowSettings_.localShadowDepthReject_;
    localShadowConfig.useNormalReject = false;
    localShadowConfig.normalRejectDot = aoSettings_.aoNormalRejectDot_;
    localShadowConfig.useNeighborhoodClamp = false;
    localShadowConfig.clampSharpness = 1.0f;

    TemporalResolveConfig aoConfig{};
    aoConfig.blendAlpha = aoSettings_.aoBlendAlpha_;
    aoConfig.blendAlphaMax = 1.0f;
    aoConfig.depthRejectThreshold = aoSettings_.aoDepthReject_;
    aoConfig.useNormalReject = true;
    aoConfig.normalRejectDot = aoSettings_.aoNormalRejectDot_;
    aoConfig.useNeighborhoodClamp = true;
    aoConfig.clampSharpness = 0.8f;
    // per-frame policy enables this only when the active ao tier is half.
    aoConfig.useSpatialFilter = false;
    aoConfig.spatialFilterReconstructedCurrent = false;
    aoConfig.spatialFilterRadius = 1;
    aoConfig.spatialDepthSigma = 0.01f;
    aoConfig.spatialValueSigma = 0.25f;
    aoConfig.spatialNormalPower = 32.0f;
    aoConfig.format = VK_FORMAT_R16_SFLOAT;

    engine::render::PassCreateResourcesInfo createInfo{};
    createInfo.context = &ctx_;
    createInfo.commands = &renderer_.commands();
    createInfo.passCreateInfo = &passCreateInfo_;
    createInfo.argv0 = argv0_;
    createInfo.assetRoot = assetRoot_;
    createInfo.materialLayout = materialPool_.layout();
    createInfo.voxelSetLayout = voxelWorld_.voxelSetLayout();
    createInfo.voxelPaletteBuffer = voxelPalette_.buffer();
    createInfo.waterVolumeBuffer = waterVolumeMgr_.buffer();
    createInfo.waterVolumeCount = waterVolumeMgr_.count();
    createInfo.waterContainerBuffer = waterContainerMgr_.buffer();
    createInfo.lights = &lights_;
    createInfo.swapchain = &renderer_.swapchain();
    createInfo.taaEnabled = postFxSettings_.taaEnabled_;
    createInfo.shadowRayExtent = engine::render::auxiliaryRayExtent(
        passCreateInfo_.extent, shadowSettings_.ddaRayResolutionScale_);
    createInfo.aoRayExtent = engine::render::auxiliaryRayExtent(
        passCreateInfo_.extent, aoSettings_.aoRayResolutionScale_);
    logInfo("Renderer",
            makeLogMessage("Auxiliary ray targets: shadow=",
                           createInfo.shadowRayExtent.width, "x",
                           createInfo.shadowRayExtent.height, " (scale ",
                           shadowSettings_.ddaRayResolutionScale_, "), AO=",
                           createInfo.aoRayExtent.width, "x", createInfo.aoRayExtent.height,
                           " (scale ", aoSettings_.aoRayResolutionScale_, "), scene=",
                           passCreateInfo_.extent.width, "x", passCreateInfo_.extent.height,
                           "."));
    createInfo.shadowTemporalResolveConfig = shadowConfig;
    createInfo.localShadowResolveConfig = localShadowConfig;
    createInfo.aoTemporalResolveConfig = aoConfig;
#if VOXEL_WITH_RUNTIME_UI
    createInfo.runtimeUiFontAtlasImage = &runtimeUiFontAtlasImage_;
    createInfo.runtimeUiFontAtlasLabel = &runtimeUiDebugFont_.id;
#endif
    createInfo.createPixelInspectResources = [](void* user) {
        static_cast<App*>(user)->createPixelInspectResources();
    };
    createInfo.user = this;

    const engine::render::PassCreateResourcesResult createResult =
        passes.createResources(createInfo);
    if (!createResult.ok)
    {
        const char* component = createResult.fatalComponent != nullptr
                                    ? createResult.fatalComponent
                                    : "Renderer";
        const char* reason = createResult.fatalReason != nullptr
                                 ? createResult.fatalReason
                                 : "Pass resource creation failed.";
        logAndExit("Renderer", std::string(component) + " initialization failed. " +
                                   reason);
    }
    if (createResult.ddaShadowsDisabled)
    {
        shadowSettings_.useDDAShadows_ = false;
    }
    if (createResult.localLightShadowsDisabled)
    {
        shadowSettings_.localLightShadowsEnabled_ = false;
        shadowSettings_.localShadowCastingLightCount_ = 0;
    }
    if (createResult.localShadowBlurDisabled)
    {
        shadowSettings_.localShadowBlurEnabled_ = false;
    }
    if (createResult.ambientOcclusionDisabled)
    {
        aoSettings_.aoEnabled_ = false;
    }
    shadowSettings_.shadowResetHistory_ = createResult.resetShadowHistory;
    shadowSettings_.localShadowResetHistory_ = createResult.resetLocalShadowHistory;
    aoSettings_.aoResetHistory_ = createResult.resetAoHistory;
    postFxSettings_.taaResetHistory_ = createResult.resetTaaHistory;
    localShadowNeutralClearPending_ = createResult.localShadowNeutralClearPending;
    aoNeutralClearPending_ = createResult.aoNeutralClearPending;
}

void App::destroyPassResources()
{
    auto& passes = renderPasses();
    engine::render::PassDestroyInfo destroyInfo{};
    destroyInfo.context = &ctx_;
    destroyInfo.destroyPixelInspectResources = [](void* user) {
        static_cast<App*>(user)->destroyPixelInspectResources();
    };
    destroyInfo.user = this;
    if (!passes.destroyResources(destroyInfo))
    {
        logAndExit("Renderer", "Pass resource destruction was invoked without the required "
                               "RendererPassResources bindings.");
    }
}
