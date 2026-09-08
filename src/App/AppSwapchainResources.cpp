#include "App/App.h"

#include <cmath>

#include <GLFW/glfw3.h>

#include "Core/Logger.h"
#include "engine/render/AuxiliaryRayResolution.h"
#include "engine/render/RenderQualityPreset.h"
#include "engine/render/RenderResolution.h"

void App::applyRenderQualityPreset(engine::render::RenderQualityPreset preset)
{
    const engine::render::RenderQualityPresetSettings settings =
        engine::render::renderQualityPresetSettings(preset);
    shadowSettings_.adaptiveRayResolution_ = false;
    aoSettings_.adaptiveRayResolution_ = false;
    setRenderScale(settings.renderScale);
    setAuxiliaryRayScales(settings.shadowRayScale, settings.aoRayScale);
    logInfo("Renderer",
            makeLogMessage("Render quality preset '",
                           engine::render::renderQualityPresetId(preset),
                           "' applied: renderScale=", settings.renderScale,
                           " shadowRayScale=", settings.shadowRayScale,
                           " aoRayScale=", settings.aoRayScale, "."));
}

void App::setRenderScale(float scale)
{
    const float sanitized = engine::render::sanitizeRenderScale(scale);
    if (std::abs(postFxSettings_.renderResolution_.scale - sanitized) < 1e-4f)
    {
        return;
    }

    postFxSettings_.renderResolution_.scale = sanitized;
    swapchainRecreateRequested_ = true;
}

void App::setAuxiliaryRayScales(float ddaShadowScale, float aoScale)
{
    const float sanitizedShadow = engine::render::sanitizeAuxiliaryRayScale(ddaShadowScale);
    const float sanitizedAo = engine::render::sanitizeAuxiliaryRayScale(aoScale);
    if (std::abs(shadowSettings_.ddaRayResolutionScale_ - sanitizedShadow) < 1e-4f &&
        std::abs(aoSettings_.aoRayResolutionScale_ - sanitizedAo) < 1e-4f)
    {
        return;
    }
    shadowSettings_.ddaRayResolutionScale_ = sanitizedShadow;
    aoSettings_.aoRayResolutionScale_ = sanitizedAo;
    swapchainRecreateRequested_ = true;
}

void App::recreateSwapchainResources()
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(window_, &width, &height);
        glfwWaitEvents();
    }

    waitForRenderQueuesIdle("swapchain recreation");
    auto& passes = renderPasses();

    renderer_.destroyFrameSyncResources();
    passes.destroySwapchainDependentResources(ctx_);

    renderer_.recreateSwapchainResources(window_, presentMode_);
    passCreateInfo_.extent = engine::render::internalRenderExtent(
        renderer_.swapchainExtent(), postFxSettings_.renderResolution_.scale);
    logInfo("Renderer",
            makeLogMessage("Render targets recreated: internal=",
                           passCreateInfo_.extent.width, "x", passCreateInfo_.extent.height,
                           " presentation=", renderer_.swapchainExtent().width, "x",
                           renderer_.swapchainExtent().height, " scale=",
                           postFxSettings_.renderResolution_.scale, "."));

    engine::render::PrimaryTargetResizeInfo primaryTargetInfo{};
    primaryTargetInfo.context = &ctx_;
    primaryTargetInfo.passCreateInfo = &passCreateInfo_;
    primaryTargetInfo.voxelSetLayout = voxelWorld_.voxelSetLayout();
    primaryTargetInfo.waterVolumeBuffer = waterVolumeMgr_.buffer();
    primaryTargetInfo.waterContainerBuffer = waterContainerMgr_.buffer();
    primaryTargetInfo.shadowRayExtent = engine::render::auxiliaryRayExtent(
        passCreateInfo_.extent, shadowSettings_.ddaRayResolutionScale_);
    if (!passes.recreatePrimaryTargets(primaryTargetInfo))
    {
        logAndExit("Renderer", "Primary render-target recreation failed. Voxel ray entry/exit "
                               "buffers and depth history remain required after swapchain "
                               "recreation.");
    }
    shadowSettings_.shadowResetHistory_ = true;
    shadowSettings_.localShadowResetHistory_ = true;
    aoSettings_.aoResetHistory_ = true;
    postFxSettings_.taaResetHistory_ = true;
    localShadowNeutralClearPending_ = true;
    aoNeutralClearPending_ = true;

    const VkExtent2D aoRayExtent = engine::render::auxiliaryRayExtent(
        passCreateInfo_.extent, aoSettings_.aoRayResolutionScale_);
    logInfo("Renderer",
            makeLogMessage("Auxiliary ray targets resized: shadow=",
                           primaryTargetInfo.shadowRayExtent.width, "x",
                           primaryTargetInfo.shadowRayExtent.height, ", AO=",
                           aoRayExtent.width, "x", aoRayExtent.height, "."));
    const engine::render::OptionalTargetResizeResult optionalResize =
        passes.resizeOptionalTargets(ctx_, passCreateInfo_.extent, aoRayExtent);
    if (optionalResize.ddaShadowsDisabled)
    {
        shadowSettings_.useDDAShadows_ = false;
        shadowSettings_.shadowResetHistory_ = true;
    }
    if (optionalResize.localLightShadowsDisabled)
    {
        shadowSettings_.localLightShadowsEnabled_ = false;
        shadowSettings_.localShadowCastingLightCount_ = 0;
        shadowSettings_.localShadowBlurEnabled_ = false;
        shadowSettings_.localShadowResetHistory_ = true;
        localShadowNeutralClearPending_ = true;
    }
    if (optionalResize.ambientOcclusionDisabled)
    {
        aoSettings_.aoEnabled_ = false;
        aoSettings_.aoResetHistory_ = true;
        aoNeutralClearPending_ = true;
    }

    engine::render::MainLightingResizeInfo mainLightingInfo{};
    mainLightingInfo.context = &ctx_;
    mainLightingInfo.passCreateInfo = &passCreateInfo_;
    mainLightingInfo.waterVolumeBuffer = waterVolumeMgr_.buffer();
    mainLightingInfo.waterVolumeCount = waterVolumeMgr_.count();
    if (!passes.recreateMainLightingTargets(mainLightingInfo))
    {
        logAndExit("Renderer", "Main lighting target recreation failed after swapchain "
                               "recreation.");
    }

    engine::render::ReflectionTargetResizeInfo reflectionInfo{};
    reflectionInfo.context = &ctx_;
    reflectionInfo.passCreateInfo = &passCreateInfo_;
    reflectionInfo.materialLayout = materialPool_.layout();
    reflectionInfo.voxelSetLayout = voxelWorld_.voxelSetLayout();
    reflectionInfo.waterVolumeBuffer = waterVolumeMgr_.buffer();
    reflectionInfo.waterVolumeCount = waterVolumeMgr_.count();
    reflectionInfo.waterContainerBuffer = waterContainerMgr_.buffer();
    reflectionInfo.lights = &lights_;
    if (!passes.recreateReflectionTargets(reflectionInfo))
    {
        logAndExit("Renderer",
                   "Reflection target recreation failed. Reflection lighting requires voxel "
                   "ray entry/exit buffers after swapchain recreation.");
    }

    engine::render::WaterPostResizeInfo waterPostInfo{};
    waterPostInfo.context = &ctx_;
    waterPostInfo.passCreateInfo = &passCreateInfo_;
    waterPostInfo.swapchain = &renderer_.swapchain();
    waterPostInfo.waterVolumeBuffer = waterVolumeMgr_.buffer();
    waterPostInfo.taaEnabled = postFxSettings_.taaEnabled_;
    if (!passes.recreateWaterPostTargets(waterPostInfo))
    {
        logAndExit("Renderer", "Water and post-processing target recreation failed after "
                               "swapchain recreation.");
    }
#if VOXEL_WITH_RUNTIME_UI
    engine::render::RuntimeUiOverlayResizeInfo runtimeUiInfo{};
    runtimeUiInfo.context = &ctx_;
    runtimeUiInfo.commands = &renderer_.commands();
    runtimeUiInfo.renderPass = renderer_.swapchainRenderPass();
    runtimeUiInfo.fontAtlasImage = &runtimeUiFontAtlasImage_;
    runtimeUiInfo.fontAtlasLabel = &runtimeUiDebugFont_.id;
    if (!passes.recreateRuntimeUiOverlay(runtimeUiInfo))
    {
        logAndExit("Renderer", "Runtime UI overlay recreation failed after swapchain "
                               "recreation.");
    }
    runtimeUiOverlaySmokeLogged_ = false;
    runtimeUiOverlayTextLogged_ = false;
    runtimeUiWorldLabelsLogged_ = false;
    runtimeUiMainMenuLogged_ = false;
#endif

#if VOXEL_WITH_EDITOR
    imgui_.onResize(ctx_, renderer_.swapchainRenderPass(),
                    renderer_.swapchainImageCount());
#endif

    renderer_.createFrameSyncResources();
    framebufferResized_ = false;
    swapchainRecreateRequested_ = false;
}
