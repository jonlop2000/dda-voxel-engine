#include "App/App.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

#include <GLFW/glfw3.h>
#include <glm/ext/vector_float3.hpp>

#include "App/AppRenderHelpers.h"
#include "App/AppSceneVolume.h"
#include "App/ScenePresentationProfileRuntime.h"
#include "Core/Logger.h"
#include "engine/game/FoliageCatalog.h"
#include "engine/physics/BallPhysics.h"
#include "engine/render/AuxiliaryRayResolution.h"
#include "engine/render/FrameCharacterization.h"
#include "engine/render/FrameInputs.h"
#include "engine/render/PassRegistry.h"
#include "engine/render/voxel/VoxelLightingOcclusionPolicy.h"
#include "engine/scene/WorldStateView.h"
#include "engine/scene/AquariumScene.h"
#include "engine/voxel/VoxelSystem.h"
#if VOXEL_WITH_EDITOR
#include <imgui.h>

#include "UI/Editor.h"
#include "UI/EngineFacade.h"
#include "UI/Panels/ProfilerOverlay.h"
#endif

// the frame shell: App::drawFrame's update half (simulation/world mutation,
// fully pre-acquire) and record half (from renderer beginFrame to the frame
// tail).

namespace
{

float haltonSingle(uint32_t index, uint32_t base)
{
    if (base < 2)
    {
        return 0.0f;
    }

    float result = 0.0f;
    float f = 1.0f / static_cast<float>(base);
    uint32_t i = index;
    while (i > 0)
    {
        result += f * static_cast<float>(i % base);
        i /= base;
        f /= static_cast<float>(base);
    }
    return result;
}

constexpr uint32_t kTaaJitterPeriod = 16;

float signNonZero(float value)
{
    return value < 0.0f ? -1.0f : 1.0f;
}

glm::vec2 halton23(uint32_t index)
{
    return glm::vec2(haltonSingle(index, 2), haltonSingle(index, 3));
}

constexpr float kVoxelEditDistance = 8.0f;
constexpr int kViewModeBloomExtract = 6;
constexpr int kViewModeBloomBlur = 7;
constexpr int kViewModeBloomCombined = 8;
constexpr int kViewModeBloomHeatmap = 9;
constexpr int kViewModeBlueNoiseRaw = 10;
constexpr int kViewModeStochasticNoise = 11;
constexpr int kViewModeNoiseHemisphere = 12;
constexpr int kViewModeWaterDistance = 13;
constexpr int kViewModeWaterDistanceDelta = 14;

void appendHistorySample(std::vector<float>& history, float value, size_t limit)
{
    history.push_back(value);
    if (history.size() > limit)
    {
        history.erase(history.begin(), history.begin() +
                                       static_cast<std::vector<float>::difference_type>(
                                           history.size() - limit));
    }
}

glm::mat4 makeObliqueNearPlaneProjectionZeroToOne(const glm::mat4& projection,
                                                  const glm::mat4& view,
                                                  const glm::vec4& worldClipPlane,
                                                  const glm::vec4& keepPointWorld)
{
    glm::vec4 clipPlaneView = glm::transpose(glm::inverse(view)) * worldClipPlane;
    const glm::vec4 keepPointView = view * keepPointWorld;
    if (glm::dot(clipPlaneView, keepPointView) < 0.0f)
    {
        clipPlaneView = -clipPlaneView;
    }

    const glm::vec4 q = glm::inverse(projection) *
                        glm::vec4(signNonZero(clipPlaneView.x),
                                  signNonZero(clipPlaneView.y), 1.0f, 1.0f);
    const float denom = glm::dot(clipPlaneView, q);
    if (std::abs(denom) < 1e-5f)
    {
        return projection;
    }

    // Vulkan/GLM zero-to-one depth clips against z >= 0, so the new projection
    // row directly becomes the water clip plane. the OpenGL -1..1 variant uses
    // a different row replacement and should not be used here.
    const glm::vec4 c = clipPlaneView * (1.0f / denom);
    if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z) ||
        !std::isfinite(c.w))
    {
        return projection;
    }

    glm::mat4 clipped = projection;
    clipped[0][2] = c.x;
    clipped[1][2] = c.y;
    clipped[2][2] = c.z;
    clipped[3][2] = c.w;
    return clipped;
}

} // namespace

void App::drawFrame()
{
    engine::render::beginRuntimeFrameLoopCharacterization();
    engine::render::traceRuntimeFrameLoopStep("drawFrame.begin");
    const FrameShellInputs shell = updateFrame();
    recordFrame(shell);
    engine::render::traceRuntimeFrameLoopStep("drawFrame.end");
    engine::render::finishRuntimeFrameCharacterization();
}

FrameShellInputs App::updateFrame()
{
    engine::render::traceRuntimeFrameLoopStep("updateFrame.begin");
    auto& passes = renderPasses();
    const auto frameStart = std::chrono::high_resolution_clock::now();
    const double realNow = glfwGetTime();
    const float dt =
        lastFrameTime_ > 0.0 ? static_cast<float>(realNow - lastFrameTime_) : 0.0f;
    lastFrameTime_ = realNow;
    updateRenderPipelineShowcase(dt);
    const bool wasWindowFocused = windowFocused_;
    windowFocused_ = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    if (windowFocused_ && !wasWindowFocused)
    {
        lastInteractionTime_ = realNow;
    }

    inputController_.updateMouseCapture();
#if VOXEL_WITH_EDITOR
    imgui_.beginFrame(input_.mouseLookActive());
#endif

    const uint32_t currentFrame = prepareCompletedFrameResources();

    lastGpuFrameMs_ = static_cast<float>(gpuProfiler_.totalMs());
    if (pendingProfilerSample_)
    {
#if VOXEL_WITH_EDITOR
        const size_t historyLimit =
            static_cast<size_t>(std::clamp(editorState_.profilerHistoryFrames, 30, 600));
#else
        constexpr size_t historyLimit = 180;
#endif
        appendHistorySample(cpuFrameHistoryMs_, pendingCpuFrameMs_, historyLimit);
        if (gpuProfiler_.isEnabled())
        {
            appendHistorySample(gpuFrameHistoryMs_, lastGpuFrameMs_, historyLimit);
            const auto& resolvedSamples = gpuProfiler_.samples();
            if (automationLogGpuProfile_ && !resolvedSamples.empty())
            {
                const bool sceneReady =
                    !automationProfileWaitForSceneReady_ ||
                    (!isCloudBuildInProgress() &&
                     (!sceneConfig().loadCloudScene || automationCloudCommitObserved_));
                const engine::performance::AutomationProfileSampleResult profileResult =
                    automationProfileSession_.consume(
                        sceneReady, static_cast<double>(pendingCpuFrameMs_),
                        static_cast<double>(lastGpuFrameMs_), resolvedSamples);
                if (profileResult.readinessReached)
                {
                    logInfo(
                        "Automation",
                        makeLogMessage(
                            "Steady-state profile readiness reached; warmupFrames=",
                            automationProfileSession_.requestedWarmupFrames(),
                            " sampleFrames=",
                            automationProfileSession_.requestedSampleFrames(), "."));
                }
                if (profileResult.warmupCompleted)
                {
                    logInfo("Automation",
                            makeLogMessage(
                                "GPU profile warm-up complete after ",
                                automationProfileSession_.consumedWarmupFrames(),
                                " resolved frames."));
                }
                if (profileResult.samplingStarted)
                {
                    logInfo("Automation", "Steady-state GPU profile sampling started.");
                }
                if (profileResult.samplingCompleted)
                {
                    logInfo(
                        "Automation",
                        makeLogMessage(
                            "Steady-state GPU profile sampling complete after ",
                            automationProfileSession_.sampledFrameCount(), " frames."));
                }
            }
        }
        pendingProfilerSample_ = false;
    }

    if (diagnosticsSettings_.voxelMetricsEnabled_)
    {
        readCompletedDdaMetrics(passes, currentFrame);
    }

#if VOXEL_WITH_EDITOR
    if (shouldDrawEditorUi())
    {
        EngineFacade engineFacade(*this, editorState_);

        // Draw editor ui (new organized panels or legacy single window)
        if (editorState_.enabled)
        {
            syncEditorStateFromInput();
            Editor::Draw(engineFacade);
            syncInputFromEditorState();
        }
        else
        {
            drawDebugUI();
        }

        if (editorState_.profilerOverlayVisible)
        {
            ProfilerOverlay::Draw(engineFacade);
        }

        if (sceneConfig().name == "physics_sandbox")
        {
            if (ImGui::Begin("Physics Sandbox"))
            {
                // display the ball's state with three decimal places for each number.
                ImGui::Text("Center height: %.3f m", physicsSandboxBall_.position.y);
                ImGui::Text("Vertical velocity: %.3f m/s", physicsSandboxBall_.velocity.y);
                // x shows sideways motion using the ball's center position.
                ImGui::Text("Horizontal position (x): %.3f m", physicsSandboxBall_.position.x);
                ImGui::Text("Horizontal velocity (x): %.3f m/s", physicsSandboxBall_.velocity.x);
                ImGui::Text("State: %s", physicsSandboxBall_.isResting ? "Resting" : "Moving");
            }
            ImGui::End();
        }
    }
#endif
    // input is a control surface; the settings buckets are the values consumed
    // by rendering. this also captures no-editor hotkeys before pass policy.
    app::syncPresentationSettingsFromInput(
        input_, lightingSettings_, postFxSettings_,
        std::min<uint32_t>(
            lights_.maxLights,
            static_cast<uint32_t>(lightingSettings_.areaLights_.size())));
    refreshInputRouterEditorCapture();

    passes.taa.setEnabled(postFxSettings_.taaEnabled_);

    if (voxelMetricsEnabledPrev_ != diagnosticsSettings_.voxelMetricsEnabled_)
    {
        voxelMetricsEnabledPrev_ = diagnosticsSettings_.voxelMetricsEnabled_;
        cachedDdaMetrics_ = {};
        ddaMetricsHistory_.clear();
    }

    if (voxelDebugSettings_.voxelFreezeTime_ && !freezeTimeWasEnabled_)
    {
        frozenTimeSeconds_ = realNow;
    }
    freezeTimeWasEnabled_ = voxelDebugSettings_.voxelFreezeTime_;
    const double animationNow = voxelDebugSettings_.voxelFreezeTime_ ? frozenTimeSeconds_ : realNow;
    updateEnvironmentTime(dt, voxelDebugSettings_.voxelFreezeTime_);

    const float cameraDt = voxelDebugSettings_.voxelFreezeCamera_ ? 0.0f : dt;
    runtimeUiFrameDtSeconds_ = dt;
#if VOXEL_WITH_RUNTIME_UI
    gameRuntime_.tickGameplayActionFeedback(dt);
#endif
    const glm::vec3 cameraPosBeforeUpdate = camera_.position;
    const float cameraYawBeforeUpdate = camera_.yaw;
    const float cameraPitchBeforeUpdate = camera_.pitch;
    updateCamera(cameraDt);
    if (camera_.position != cameraPosBeforeUpdate || camera_.yaw != cameraYawBeforeUpdate ||
        camera_.pitch != cameraPitchBeforeUpdate)
    {
        lastInteractionTime_ = realNow;
    }
    detectCameraCut();
    consumePendingCloudBuild();

    updateCloudDriftAnimation(animationNow);

    if (sceneConfig().name == "physics_sandbox" &&
        !voxelDebugSettings_.voxelFreezeTime_)
    {
        const double physicsStep = 0.01;
        const double gravityAcceleration = -9.81;
        const double restitution = 0.8;
        physicsSandboxAccumulator_ += dt;
        while (physicsSandboxAccumulator_ >= physicsStep)
        {
            engine::physics::advanceBall(physicsSandboxBall_, gravityAcceleration, physicsStep, restitution);
            physicsSandboxAccumulator_ -= physicsStep;
        }
        // subtract the 0.12 m center offset on each axis to get the volume's origin.
        glm::vec3 volumePosition = {
            static_cast<float>(physicsSandboxBall_.position.x - 0.12),
            static_cast<float>(physicsSandboxBall_.position.y - 0.12),
            static_cast<float>(physicsSandboxBall_.position.z - 0.12)
        };
        voxelWorld_.setInstancePosition(0, volumePosition);
    }

    updateAnimatedObjects(static_cast<float>(animationNow));
    updateGameStateHeartbeat(dt);
    updateFishFocus(cameraDt);
    if (isFishFocusActive())
    {
        // the cinematic camera moves every frame; keep the idle fps throttle off so
        // the glide/tracking stays smooth.
        lastInteractionTime_ = realNow;
    }
    rebuildWaterFoamObjects(static_cast<float>(animationNow));

    if (input_.consumeMetricsCapture())
    {
        voxelMetricsCapturePending_ = true;
    }

    if (voxelMetricsCapturePending_)
    {
        if (diagnosticsSettings_.voxelMetricsEnabled_)
        {
            dumpDdaMetricsCapture(static_cast<float>(animationNow));
        }
        else
        {
            logWarning("Metrics", "Metrics capture requested but metrics are disabled.");
        }
        voxelMetricsCapturePending_ = false;
    }

    if (input_.consumeVoxelRegen())
    {
        regenerateVoxelWorld();
    }
    const bool voxelVisible = input_.voxelWorldEnabled();
    if (voxelDebugSettings_.voxelVisible_ != voxelVisible)
    {
        voxelDebugSettings_.voxelVisible_ = voxelVisible;
        sceneObjectsDirty_ = true;
        voxelObjectsDirty_ = true;
    }
    const bool chunkBoundsVisible = input_.chunkBoundsEnabled();
    if (voxelDebugSettings_.chunkBoundsVisible_ != chunkBoundsVisible)
    {
        voxelDebugSettings_.chunkBoundsVisible_ = chunkBoundsVisible;
        sceneObjectsDirty_ = true;
        voxelObjectsDirty_ = true;
    }
    const bool volumeBoundsVisible = input_.volumeBoundsEnabled();
    if (voxelDebugSettings_.volumeBoundsVisible_ != volumeBoundsVisible)
    {
        voxelDebugSettings_.volumeBoundsVisible_ = volumeBoundsVisible;
        sceneObjectsDirty_ = true;
    }
    if (voxelDebugSettings_.axolotlRigDebugVisible_ != axolotlRigDebugVisibleLast_)
    {
        axolotlRigDebugVisibleLast_ = voxelDebugSettings_.axolotlRigDebugVisible_;
        sceneObjectsDirty_ = true;
    }
    if (voxelDebugSettings_.axolotlRigDebugVisible_)
    {
        sceneObjectsDirty_ = true;
    }

    RayHit editHit{};
    bool hasEditHit = false;
    const bool editMode = input_.editModeEnabled() && voxelDebugSettings_.voxelVisible_;
    const glm::vec3 forward = cameraForward(camera_);
    if (editMode)
    {
        editHit = engine::voxel::raycastVoxels(voxelGrid_, camera_.position,
                                               forward, kVoxelEditDistance);
        hasEditHit = editHit.hit;
    }
#if VOXEL_WITH_RUNTIME_UI
    updateRuntimeUiPlacementProbe(forward);
#endif

    const bool wasPlaceablePreviewActive = placeablePreview_.active;
    const bool placeablePreviewActive =
        sceneConfig().loadAquariumTest &&
        aquariumLayoutForScene(sceneConfig()) == AquariumLayout::Default &&
        input_.editModeEnabled() && placeablePreviewEnabled_;
    placeablePreview_ = {};
    placeablePreview_.active = placeablePreviewActive;
    if (placeablePreviewActive && kFoliagePreviewSpeciesCount > 0)
    {
        selectedFoliagePreviewPrototype_ =
            std::clamp(selectedFoliagePreviewPrototype_, 0, kFoliagePreviewSpeciesCount - 1);
        const FoliagePreviewSpecies& species =
            kFoliagePreviewSpecies[selectedFoliagePreviewPrototype_];
        placeablePreview_.prototypeSlug = species.slug;
        placeablePreview_.prototypeVersion = species.version;
        placeablePreview_.seed =
            0x50000000u + static_cast<uint32_t>(selectedFoliagePreviewPrototype_);
        placeablePreview_.evaluation = AquariumScene::evaluateFoliagePlacement(
            camera_.position, forward, kVoxelEditDistance, species.slug, species.version,
            &sceneConfig().placeables, sceneConfig().useDefaultPlaceables,
            aquariumLayoutForScene(sceneConfig()));
        const FoliagePrototype* prototype =
            FoliageCatalog::findPrototype(species.slug, species.version);
        if (prototype != nullptr)
        {
            placeablePreview_.footprintRadius = prototype->placeable.placement.footprintRadius;
        }
        if (placeablePreview_.evaluation.hasHit)
        {
            placeablePreview_.position = placeablePreview_.evaluation.hit.position;
        }
    }

    const bool removeRequested = input_.consumeBlockRemove();
    const bool placeRequested = input_.consumeBlockPlace();
    const bool placeableEditMode = placeablePreview_.active;
    if (placeableEditMode)
    {
        if (placeRequested)
        {
            commitPlaceablePreview();
        }
        else if (removeRequested)
        {
            removePlaceableAtPreview();
        }
    }
    else if (hasEditHit && (removeRequested || placeRequested))
    {
        engine::voxel::applyVoxelEdit(voxelGrid_, editHit, placeRequested,
                                      input_.selectedBlock());
    }

    voxelCursorVisible_ = editMode && hasEditHit && !placeableEditMode;
    if (hasEditHit)
    {
        voxelCursorVoxel_ = placeRequested ? editHit.prevVoxel : editHit.voxel;
    }

    if (placeablePreview_.active || wasPlaceablePreviewActive)
    {
        sceneObjectsDirty_ = true;
    }
    if (placeablePreview_.active && placeablePreview_.evaluation.hasHit &&
        chunkBoundsMesh_.vbo == VK_NULL_HANDLE)
    {
        buildChunkBoundsMesh();
    }

    updateVoxelMeshing(camera_.position);


    engine::render::traceRuntimeFrameLoopStep("updateFrame.end");
    return {frameStart, realNow, animationNow, voxelVisible, chunkBoundsVisible,
            volumeBoundsVisible};
}

void App::recordFrame(const FrameShellInputs& shell)
{
    engine::render::traceRuntimeFrameLoopStep("recordFrame.begin");

    struct FrameRunState
    {
        App* app = nullptr;
        const FrameShellInputs* shell = nullptr;
        FrameCompletionState completion{};
    } state{this, &shell};

    const bool frameCompleted = renderer_.runFrameLifecycle({
        [](void* user, FrameContext& fc) {
            auto& frame = *static_cast<FrameRunState*>(user);
            frame.app->recordFrameCommands(*frame.shell, fc, frame.completion);
        },
        [](void* user, FrameContext&) {
            auto& frame = *static_cast<FrameRunState*>(user);
            frame.app->finishFrameAccounting(*frame.shell, frame.completion);
        },
        &state,
    });
    if (frameCompleted)
    {
        engine::render::traceRuntimeFrameLoopStep("recordFrame.end");
    }
}

void App::recordFrameCommands(const FrameShellInputs& shell, FrameContext& fc,
                              FrameCompletionState& completion)
{
    auto& passes = renderPasses();
    const engine::scene::WorldStateView frameWorldState = worldStateView();
    fc.worldState = &frameWorldState;
    VkCommandBuffer cmd = fc.cmd;

    gpuProfiler_.beginFrame(cmd, fc.frameIndex);
    const uint32_t gpuScopeFrame = gpuProfiler_.beginScope(cmd, "Frame");

    applyPendingVoxelMeshes(fc.frameIndex, cmd);
    engine::render::traceRuntimeFrameCharacterizationStep("applyPendingVoxelMeshes");
    engine::render::traceRuntimeFrameLoopStep("applyPendingVoxelMeshes");

    const VkExtent2D renderExtent = passCreateInfo_.extent;
    const float aspect = renderExtent.height == 0
                             ? 1.0f
                             : static_cast<float>(renderExtent.width) /
                                   static_cast<float>(renderExtent.height);
    const glm::mat4 view = camera_.viewMatrix();
    const glm::mat4 proj = camera_.projMatrix(aspect);
    const glm::mat4 viewProjUnjittered = proj * view;

    glm::vec2 jitter(0.0f);
    const bool jitterEnabled = engine::scene::effectiveTaaJitterEnabled(
        postFxSettings_.taaEnabled_, postFxSettings_.jitterEnabled_,
        voxelDebugSettings_.voxelDisableJitter_,
        voxelDebugSettings_.voxelFreezeDebug_);
    if (jitterEnabled)
    {
        const uint32_t jitterIndex = (taaFrameIndex_ % kTaaJitterPeriod) + 1;
        jitter = halton23(jitterIndex) - glm::vec2(0.5f);
    }
    currentJitter_ = jitter;
    glm::mat4 projJittered = proj;
    if (jitter.x != 0.0f || jitter.y != 0.0f)
    {
        const float invW = renderExtent.width > 0
                               ? 1.0f / static_cast<float>(renderExtent.width)
                               : 0.0f;
        const float invH =
            renderExtent.height > 0
                ? 1.0f / static_cast<float>(renderExtent.height)
                : 0.0f;
        projJittered[2][0] += 2.0f * jitter.x * invW;
        projJittered[2][1] += 2.0f * jitter.y * invH;
    }
    const glm::mat4 viewProjJittered = projJittered * view;
    if (taaFrameIndex_ == 0)
    {
        prevViewProjUnjittered_ = viewProjUnjittered;
        prevViewProjJittered_ = viewProjJittered;
        prevJitter_ = jitter;
    }
    const glm::mat4 prevViewProjUnjittered = prevViewProjUnjittered_;
    const glm::mat4 prevViewProjJittered = prevViewProjJittered_;
    const glm::mat4 invViewProjJittered = glm::inverse(viewProjJittered);
    const glm::mat4 invViewProjUnjittered = glm::inverse(viewProjUnjittered);
    float foliageAnimationTime = static_cast<float>(shell.animationNow);
    float foliageSwayStrength = 1.0f;
    if (isSunroofAquariumScene(sceneConfig()))
    {
        const engine::render::SunroofLivingWaterVfxSettings& livingWater =
            waterSettings_.sunroofLivingWaterVfx_;
        const bool animateFoliage =
            livingWater.enabled && livingWater.heroFoliageMotionEnabled;
        foliageAnimationTime *= std::clamp(livingWater.heroFoliageMotionSpeed,
                                           0.25f, 2.0f);
        foliageSwayStrength =
            animateFoliage
                ? std::clamp(livingWater.heroFoliageSwayStrength, 0.0f, 2.0f)
                : 0.0f;
    }
    passes.foliage.setEnvironmentWind(sceneConfig().environmentWind);
    passes.foliage.setWindborneParticleSettings(
        sceneConfig().windborneParticles);
    passes.foliage.setSwayStrength(foliageSwayStrength);
    passes.foliage.beginFrame(foliageAnimationTime);
    lastInvViewProjUnjittered_ = invViewProjUnjittered;
    engine::render::traceRuntimeFrameCharacterizationStep("cameraMatricesAndJitter");
    engine::render::traceRuntimeFrameLoopStep("cameraMatricesAndJitter");
    const glm::vec3 currentVisibilityForward = cameraForward(camera_);
    const bool visibilityCameraChanged =
        !visibilityBuildStateValid_ ||
        camera_.position.x != lastVisibilityBuildCameraPos_.x ||
        camera_.position.y != lastVisibilityBuildCameraPos_.y ||
        camera_.position.z != lastVisibilityBuildCameraPos_.z ||
        currentVisibilityForward.x != lastVisibilityBuildCameraForward_.x ||
        currentVisibilityForward.y != lastVisibilityBuildCameraForward_.y ||
        currentVisibilityForward.z != lastVisibilityBuildCameraForward_.z;

    const bool needsVoxelVisibilityBuild =
        (voxelDebugSettings_.voxelVisible_ || voxelDebugSettings_.chunkBoundsVisible_) && (voxelObjectsDirty_ || visibilityCameraChanged);
    const bool needsVolumeBoundsBuild =
        voxelDebugSettings_.volumeBoundsVisible_ &&
        (sceneObjectsDirty_ || visibilityCameraChanged || voxelWorld_.drawListDirty());

    std::optional<Frustum> visibilityFrustum{};
    auto getVisibilityFrustum = [&]() -> const Frustum& {
        if (!visibilityFrustum)
        {
            visibilityFrustum.emplace(makeFrustum(viewProjUnjittered));
        }
        return *visibilityFrustum;
    };

    if (voxelDebugSettings_.voxelVisible_ || voxelDebugSettings_.chunkBoundsVisible_)
    {
        if (needsVoxelVisibilityBuild)
        {
            rebuildVoxelObjects(getVisibilityFrustum(), camera_.position);
        }
    }
    else if (!voxelObjects_.empty() || !voxelShadowObjects_.empty() ||
             !chunkBoundsVisibleObjects_.empty())
    {
        voxelObjects_.clear();
        voxelShadowObjects_.clear();
        chunkBoundsVisibleObjects_.clear();
        voxelObjectsDirty_ = false;
        sceneObjectsDirty_ = true;
    }

    if (voxelDebugSettings_.volumeBoundsVisible_)
    {
        if (needsVolumeBoundsBuild)
        {
            rebuildVolumeBoundsObjects(getVisibilityFrustum());
            sceneObjectsDirty_ = true;
        }
    }
    else if (!volumeBoundsVisibleObjects_.empty())
    {
        volumeBoundsVisibleObjects_.clear();
        sceneObjectsDirty_ = true;
    }

    if (needsVoxelVisibilityBuild || needsVolumeBoundsBuild)
    {
        lastVisibilityBuildCameraPos_ = camera_.position;
        lastVisibilityBuildCameraForward_ = currentVisibilityForward;
        visibilityBuildStateValid_ = true;
    }

    if (sceneObjectsDirty_ || animatedObjectsDirty_)
    {
        rebuildSceneObjects();
    }
    engine::render::traceRuntimeFrameCharacterizationStep("visibilityAndSceneRebuilds");
    engine::render::traceRuntimeFrameLoopStep("visibilityAndSceneRebuilds");

    updateLights(static_cast<float>(shell.animationNow));
    engine::render::traceRuntimeFrameCharacterizationStep("updateLights");
    engine::render::traceRuntimeFrameLoopStep("updateLights");

    const engine::scene::EnvironmentTimePresentation& timePresentation =
        environmentTimeSample_.presentation;
    const glm::vec3 lightDir = environmentTimeSample_.lightDirection;
    const VkExtent2D shadowExtent = passes.shadow.map().extent;
    const glm::mat4 invView = glm::inverse(view);
    const app::render::ShadowCascadeSetup cascadeSetup =
        app::render::computeShadowCascadeSetup(camera_, invView, aspect, lightDir,
                                               shadowExtent);

    if (input_.gbufferMode() == 4)
    {
        static double lastShadowDebugPrint = -1.0;
        if (lastShadowDebugPrint < 0.0 || (shell.realNow - lastShadowDebugPrint) >= 0.5)
        {
            lastShadowDebugPrint = shell.realNow;
            const app::render::CascadeFit& cascadeDebug = cascadeSetup.cascade0Fit;
            logDebug("ShadowPass",
                     makeLogMessage("ShadowCascade0 near=", cascadeDebug.nearPlane,
                                    " far=", cascadeDebug.farPlane,
                                    " shadowFar=", cascadeSetup.shadowFar,
                                    " splits=(", cascadeSetup.split0, ",", cascadeSetup.split1,
                                    ",", cascadeSetup.split2, ")",
                                    " minLS=(", cascadeDebug.minLS.x, ",",
                                    cascadeDebug.minLS.y, ",", cascadeDebug.minLS.z, ")",
                                    " maxLS=(", cascadeDebug.maxLS.x, ",",
                                    cascadeDebug.maxLS.y, ",", cascadeDebug.maxLS.z, ")",
                                    " lightDistance=", cascadeDebug.lightDistance));
        }
    }

    ShadowPass::FrameUbo shadowUbo{};
    for (uint32_t c = 0; c < kShadowCascades; ++c)
    {
        shadowUbo.lightViewProj[c] = cascadeSetup.lightViewProjs[c];
    }

    GBufferPass::FrameUboInputs gbufferUboInputs{};
    gbufferUboInputs.view = view;
    gbufferUboInputs.proj = proj;
    gbufferUboInputs.viewProj = viewProjJittered;
    gbufferUboInputs.viewProjUnjittered = viewProjUnjittered;
    gbufferUboInputs.prevViewProjUnjittered = prevViewProjUnjittered;
    gbufferUboInputs.prevViewProj = prevViewProjJittered;
    gbufferUboInputs.invViewProjUnjittered = invViewProjUnjittered;
    gbufferUboInputs.cameraWorld = camera_.position;
    gbufferUboInputs.renderExtent = renderExtent;
    const GBufferPass::FrameUbo gbufferUbo =
        GBufferPass::buildFrameUbo(gbufferUboInputs);

    passes.gbuffer.updateFrameUbo(fc.frameIndex, gbufferUbo);

    sanitizeWaterParameters();

    LightingPass::FrameUboInputs lightingUboInputs{};
    lightingUboInputs.invViewProj = invViewProjJittered;
    lightingUboInputs.viewProj = viewProjUnjittered;
    lightingUboInputs.view = view;
    lightingUboInputs.lightViewProj = cascadeSetup.lightViewProjs;
    lightingUboInputs.cameraPosition = camera_.position;
    lightingUboInputs.lightDirection = lightDir;
    lightingUboInputs.sunAngularRadius = shadowSettings_.sunAngularRadius_;
    lightingUboInputs.sunColor = timePresentation.sunColor;
    lightingUboInputs.sunIntensity = timePresentation.sunIntensity;
    lightingUboInputs.cascadeSplits = glm::vec4(cascadeSetup.split0, cascadeSetup.split1,
                                                cascadeSetup.split2, cascadeSetup.shadowFar);
    lightingUboInputs.shadowExtent = shadowExtent;
    lightingUboInputs.waterLevel = waterSettings_.waterLevel_;
    lightingUboInputs.animationTime = static_cast<float>(shell.animationNow);
    lightingUboInputs.waterCausticsScale = waterSettings_.waterCausticsScale_;
    lightingUboInputs.waterCausticsSpeed = waterSettings_.waterCausticsSpeed_;
    lightingUboInputs.waterCausticsIntensity = waterSettings_.waterCausticsIntensity_;
    lightingUboInputs.waterCausticsBanding = waterSettings_.waterCausticsBanding_;
    lightingUboInputs.waterCausticsDepthFade = waterSettings_.waterCausticsDepthFade_;
    lightingUboInputs.waterCausticsEnabled = waterSettings_.waterCausticsEnabled_;
    lightingUboInputs.sceneAtmosphere = timePresentation.sceneAtmosphere;
    lightingUboInputs.paintedSky = timePresentation.paintedSky;
    lightingUboInputs.paintedClouds = timePresentation.paintedClouds;
    lightingUboInputs.paintedCloudFrame.windDirection =
        sceneConfig().environmentWind.direction;
    lightingUboInputs.paintedCloudFrame.windSpeed =
        sceneConfig().environmentWind.speed;
    lightingUboInputs.paintedCloudFrame.windStrength =
        sceneConfig().environmentWind.strength;
    lightingUboInputs.paintedCloudFrame.animationTime =
        static_cast<float>(shell.animationNow);
    lightingUboInputs.paintedCloudFrame.worldSeed = sceneConfig().worldSeed;
    const LightingPass::FrameUbo lightingUbo =
        LightingPass::buildFrameUbo(lightingUboInputs);
    sceneConfigMutable().useWaterV2 = waterSettings_.useWaterV2_;

    const int effectiveLightingDebugMode =
        renderPipelineShowcase_.enabled()
            ? renderPipelineShowcase_.currentStage().lightingDebugMode
            : lightingSettings_.lightingDebugMode_;
    const int effectiveShadowDebugMode =
        renderPipelineShowcase_.enabled()
            ? renderPipelineShowcase_.currentStage().shadowDebugMode
            : shadowSettings_.shadowDebugMode_;
    const int effectiveWaterDebugMode =
        renderPipelineShowcase_.enabled() ? 0 : input_.waterDebugMode();
    const int effectiveGlassDebugMode =
        renderPipelineShowcase_.enabled() ? 0 : input_.glassDebugMode();
    const bool isolateLightingDebugView =
        renderPipelineShowcase_.enabled()
            ? effectiveLightingDebugMode > 0 || effectiveShadowDebugMode > 0
            : input_.cascadeDebug() || input_.heatmap() ||
                  shadowSettings_.shadowDebugMode_ != 0 ||
                  diagnosticsSettings_.edgeFlickerDebugMode_ > 0 ||
                  effectiveLightingDebugMode > 0;
    const bool drawWater =
        !isolateLightingDebugView && (input_.waterEnabled() || effectiveWaterDebugMode != 0);
    const bool drawGlass = !isolateLightingDebugView &&
                           (input_.glassEnabled() || effectiveGlassDebugMode != 0) &&
                           !glassObjects_.empty();
    const bool shouldRecordPlanarReflection =
        !isolateLightingDebugView && waterSettings_.waterPlanarReflectionEnabled_ && input_.waterEnabled() &&
        (waterSettings_.useWaterV2_ || waterSettings_.waterStylizedMode_);
    glm::vec3 reflectedCamPos = camera_.position;
    glm::mat4 reflectedView = view;
    glm::mat4 reflectedProj = proj;
    glm::mat4 reflectedViewProj = viewProjUnjittered;
    if (shouldRecordPlanarReflection)
    {
        reflectedCamPos.y = 2.0f * waterSettings_.waterLevel_ - camera_.position.y;
        glm::vec3 reflectedForward = cameraForward(camera_);
        reflectedForward.y *= -1.0f;
        if (glm::dot(reflectedForward, reflectedForward) < 1e-6f)
        {
            reflectedForward = glm::vec3(0.0f, 0.0f, -1.0f);
        }
        reflectedForward = glm::normalize(reflectedForward);

        glm::vec3 reflectedUp = glm::vec3(glm::inverse(view)[1]);
        reflectedUp.y *= -1.0f;
        if (glm::dot(reflectedUp, reflectedUp) < 1e-6f ||
            std::abs(glm::dot(reflectedForward, glm::normalize(reflectedUp))) > 0.98f)
        {
            reflectedUp = glm::vec3(0.0f, -1.0f, 0.0f);
        }
        if (std::abs(glm::dot(reflectedForward, glm::normalize(reflectedUp))) > 0.98f)
        {
            reflectedUp = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        reflectedView = glm::lookAt(reflectedCamPos, reflectedCamPos + reflectedForward,
                                    glm::normalize(reflectedUp));
        if (waterSettings_.waterPlanarObliqueClipEnabled_)
        {
            const float reflectionClipBias = 0.05f;
            const float reflectionClipY = waterSettings_.waterLevel_ + reflectionClipBias;
            reflectedProj = makeObliqueNearPlaneProjectionZeroToOne(
                reflectedProj, reflectedView, glm::vec4(0.0f, 1.0f, 0.0f, -reflectionClipY),
                glm::vec4(reflectedCamPos.x, reflectionClipY + 1.0f, reflectedCamPos.z, 1.0f));
        }
        reflectedViewProj = reflectedProj * reflectedView;
    }

    const uint32_t activeWaterCount =
        input_.waterEnabled() ? waterVolumeMgr_.count() : 0;

    app::render::WaterFrameUboInputs waterUboInputs{};
    waterUboInputs.view = view;
    waterUboInputs.viewProjJittered = viewProjJittered;
    waterUboInputs.invProjJittered = glm::inverse(projJittered);
    waterUboInputs.reflectedViewProj = reflectedViewProj;
    waterUboInputs.cameraPos = camera_.position;
    waterUboInputs.lightDir = lightDir;
    waterUboInputs.sunColor =
        glm::vec4(glm::vec3(lightingUbo.lightColor) * lightingUbo.lightColor.w, 1.0f);
    waterUboInputs.animationNow = static_cast<float>(shell.animationNow);
    waterUboInputs.waterDebugMode = effectiveWaterDebugMode;
    waterUboInputs.planarStrength =
        shouldRecordPlanarReflection ? waterSettings_.waterPlanarStrength_ : 0.0f;
    waterUboInputs.sceneAtmosphere = timePresentation.sceneAtmosphere;
    const WaterPass::FrameUbo waterUbo = app::render::buildWaterFrameUbo(
        waterUboInputs, sceneConfig(), waterSettings_, waterVolumeMgr_);

    GlassPass::FrameUboInputs glassUboInputs{};
    glassUboInputs.view = view;
    glassUboInputs.viewProjUnjittered = viewProjUnjittered;
    glassUboInputs.cameraPosition = camera_.position;
    glassUboInputs.tint = glassSettings_.glassTint_;
    glassUboInputs.absorption = glassSettings_.glassAbsorption_;
    glassUboInputs.refractStrength = glassSettings_.glassRefract_;
    glassUboInputs.ior = glassSettings_.glassIor_;
    glassUboInputs.debugMode = drawGlass ? effectiveGlassDebugMode : 0;
    glassUboInputs.renderExtent = renderExtent;
    glassUboInputs.thicknessDebugScale = glassSettings_.glassThicknessDebugScale_;
    glassUboInputs.refractDebugScale = glassSettings_.glassRefractDebugScale_;
    glassUboInputs.thicknessScale = glassSettings_.glassThicknessScale_;
    glassUboInputs.iridescentStrength = glassSettings_.glassIridescentStrength_;
    glassUboInputs.iridescentFilmThickness = glassSettings_.glassIridescentFilmThickness_;
    glassUboInputs.iridescentFrequency = glassSettings_.glassIridescentFrequency_;
    glassUboInputs.bubbleScale = glassSettings_.glassBubbleScale_;
    glassUboInputs.bubbleIntensity = glassSettings_.glassBubbleIntensity_;
    glassUboInputs.bubbleThicknessGate = glassSettings_.glassBubbleThicknessGate_;
    glassUboInputs.bubbleChromaticSplit = glassSettings_.glassBubbleChromaticSplit_;
    glassUboInputs.reflectionColor = glassSettings_.glassReflection_;
    glassUboInputs.waterLevel = waterSettings_.waterLevel_;
    glassUboInputs.animationTime = static_cast<float>(shell.animationNow);
    glassUboInputs.waterWaveScale = waterSettings_.waterWaveScale_;
    glassUboInputs.waterWaveAmplitude = waterSettings_.waterWaveAmp_;
    glassUboInputs.waterlineRippleEnabled = waterSettings_.useWaterV2_ && input_.waterEnabled();
    if (glassUboInputs.waterlineRippleEnabled && !waterVolumeMgr_.volumes().empty())
    {
        const engine::WaterVolume& volume = waterVolumeMgr_.volumes().front();
        glassUboInputs.waterVolume.active = true;
        glassUboInputs.waterVolume.boundsMin = volume.boundsMin;
        glassUboInputs.waterVolume.boundsMax = volume.boundsMax;
        glassUboInputs.waterVolume.surfaceHeight = volume.surfaceHeight;
        glassUboInputs.waterVolume.shape = static_cast<float>(volume.shape);
    }
    glassUboInputs.sunDirectionToSun = -lightDir;
    glassUboInputs.sunColor =
        glm::vec3(lightingUbo.lightColor) * lightingUbo.lightColor.w;
    glassUboInputs.sceneAtmosphere = timePresentation.sceneAtmosphere;
    glassUboInputs.activeWaterVolumeCount = activeWaterCount;
    const GlassPass::FrameUbo glassUbo = GlassPass::buildFrameUbo(glassUboInputs);

    engine::render::FrameInputs frameInputs{};
    frameInputs.renderCascadedShadows =
        shadowSettings_.csmEnabled_ || input_.cascadeDebug() ||
        diagnosticsSettings_.viewMode_ == 4;
    frameInputs.renderWaterVolumePrepass =
        waterSettings_.useWaterV2_ && input_.waterEnabled() && waterContainerMgr_.count() > 0;
    frameInputs.renderVoxelGlass =
        !isolateLightingDebugView && (glassSettings_.voxelGlassRefractEnabled_ || glassSettings_.voxelGlassDebugMode_ > 0);
    frameInputs.renderPlanarReflection = shouldRecordPlanarReflection;
    frameInputs.renderWater = drawWater;
    frameInputs.renderGlass = drawGlass;
#if VOXEL_WITH_RUNTIME_UI
    frameInputs.renderRuntimeUi = true;
#endif
    recordShadowMapPass(cmd, fc, passes, frameInputs, shadowUbo);

    if (waterVolumeMgr_.dirty())
    {
        syncWaterContainersFromVolumes();
        waterVolumeMgr_.upload();
    }
    if (waterContainerMgr_.dirty())
    {
        waterContainerMgr_.upload();
    }
    passes.obbPass.setWaterVolumeCount(activeWaterCount);
    passes.obbPass.setReflectionClip(0.0f, false);
    passes.lighting.setWaterVolumeCount(activeWaterCount);

    const uint32_t availableAreaLights =
        static_cast<uint32_t>(
            std::min<size_t>(lightingSettings_.areaLights_.size(),
                             lights_.maxLights));
    const uint32_t activeLights =
        lightingSettings_.pointLightsEnabled_
            ? std::min(static_cast<uint32_t>(lightingSettings_.lightCount_),
                       availableAreaLights)
            : 0u;
    const uint32_t maxShadowedLocalLights = static_cast<uint32_t>(
        std::clamp(shadowSettings_.localShadowCastingLightCount_, 0, 4));
    std::array<engine::render::LocalShadowLightInfluence, 4>
        localShadowLightInfluences{};
    uint32_t localShadowLightInfluenceCount = 0u;
    for (uint32_t i = 0;
         i < activeLights &&
         localShadowLightInfluenceCount < maxShadowedLocalLights;
         ++i)
    {
        if (i < lightingSettings_.areaLights_.size() &&
            lightingSettings_.areaLights_[i].castsShadows)
        {
            localShadowLightInfluences[localShadowLightInfluenceCount] =
                engine::render::localShadowLightInfluence(
                    lightingSettings_.areaLights_[i],
                    shadowSettings_.shadowNormalBias_);
            ++localShadowLightInfluenceCount;
        }
    }
    const bool hasShadowCastingLocalLight =
        localShadowLightInfluenceCount > 0u;
    const bool useDdaShadows = canUseDdaShadows();
    const bool useLocalLightShadows =
        canUseLocalLightShadowTracing() && hasShadowCastingLocalLight &&
        maxShadowedLocalLights > 0u;
    const bool useAmbientOcclusion = canUseAmbientOcclusion();

    engine::render::VoxelRenderResources::VolumeFilter voxelVolumeFilter{};
    voxelVolumeFilter.user = this;
    voxelVolumeFilter.accept = [](void* user, uint32_t volumeIndex,
                                  const engine::VoxelInstance&) {
        return static_cast<App*>(user)->shouldDrawVoxelVolumeWithDda(volumeIndex);
    };
    engine::render::VoxelRenderResources::VolumeDrawPlanOptions
        voxelDrawPlanOptions{};
    voxelDrawPlanOptions.supplementalSunShadowFilter.user = this;
    voxelDrawPlanOptions.supplementalSunShadowFilter.accept =
        [](void* user, uint32_t volumeIndex,
           const engine::VoxelInstance&) {
            App& app = *static_cast<App*>(user);
            if (!app.renderPasses().foliage.replacesVoxelVolume(volumeIndex) ||
                volumeIndex >= app.voxelWorld_.instances().size())
            {
                return false;
            }
            return app.voxelWorld_.instances()[volumeIndex]
                       .volume.lightingOcclusionMode() ==
                   engine::VoxelVolume::LightingOcclusionMode::TranslucentMeadow;
        };
    voxelDrawPlanOptions.enableAlignedPrimaryTraversal =
        voxelDebugSettings_.voxelAlignedLayerTraversalEnabled_ &&
        passes.obbPass.supportsSharedAlignedTraversal();
    voxelDrawPlanOptions.viewProjection = viewProjUnjittered;
    voxelDrawPlanOptions.hasViewProjection = true;
    const float nearZ = std::max(camera_.nearZ, 0.0f);
    if (camera_.projectionMode == CameraProjectionMode::Orthographic)
    {
        const float halfHeight = std::max(camera_.orthoHeight, 0.1f) * 0.5f;
        const float halfWidth = halfHeight * std::max(aspect, 0.001f);
        voxelDrawPlanOptions.nearClipGuardRadiusWorld =
            std::sqrt(nearZ * nearZ + halfWidth * halfWidth +
                      halfHeight * halfHeight);
    }
    else
    {
        const float tanHalfFov = std::tan(camera_.fovY * 0.5f);
        const float halfHeight = nearZ * tanHalfFov;
        const float halfWidth = halfHeight * std::max(aspect, 0.001f);
        voxelDrawPlanOptions.nearClipGuardRadiusWorld =
            std::sqrt(nearZ * nearZ + halfWidth * halfWidth +
                      halfHeight * halfHeight);
    }
    if (useLocalLightShadows)
    {
        voxelDrawPlanOptions.localShadowLights = std::span(
            localShadowLightInfluences.data(), localShadowLightInfluenceCount);
    }
    const engine::render::VoxelRenderResources::VolumeDrawPlan& voxelDrawPlan =
        voxelRenderResources_.prepareVolumeDrawPlan(
            voxelWorld_, ctx_, camera_.position, voxelVolumeFilter,
            voxelDrawPlanOptions);

    recordGBufferMeshPass(cmd, fc, passes, frameInputs, gbufferUbo);

    const bool effectiveVoxelDdaSkipEnabled =
        voxelDebugSettings_.voxelDdaSkipEnabled_ && !automationDisableEmptySkip_;
    recordPrimaryVoxelDdaPasses(cmd, fc, passes, voxelDrawPlan, frameInputs,
                                static_cast<float>(shell.animationNow));
    app::render::recordGBufferToLightingBarrier(cmd, passes.gbuffer, fc.frameIndex);
    recordWaterVolumePrepass(cmd, fc, passes, frameInputs, invViewProjJittered);

    const auto& staticVolumes = voxelDrawPlan.staticVolumes;
    const auto& sunShadowOccluderVolumes = voxelDrawPlan.sunShadowOccluderVolumes;
    const auto& localLightingOccluderVolumes = voxelDrawPlan.localLightingOccluderVolumes;
    const auto& localShadowOccluderVolumes =
        voxelDrawPlan.localShadowOccluderVolumes;
    frameInputs.useDdaShadows = useDdaShadows;
    frameInputs.useLocalLightShadows = useLocalLightShadows;
    frameInputs.useAmbientOcclusion = useAmbientOcclusion;

    const bool cameraInsideVoxelVolume =
        voxelDrawPlan.cameraInsideVolumeCount > 0u;
    const engine::render::AdaptiveAuxiliaryRayConfig shadowAdaptiveConfig{
        shadowSettings_.adaptiveRayResolution_ && effectiveShadowDebugMode != 1};
    const engine::render::AdaptiveAuxiliaryRayConfig aoAdaptiveConfig =
        engine::render::ambientOcclusionAdaptiveRayConfig(
            aoSettings_.adaptiveRayResolution_ && effectiveLightingDebugMode != 4);
    const engine::render::AdaptiveAuxiliaryRayDecision sunShadowResolution =
        engine::render::resolveAdaptiveAuxiliaryRayResolution(
            passCreateInfo_.extent, shadowSettings_.ddaRayResolutionScale_,
            voxelDrawPlan.projectedScreenCoverage, cameraInsideVoxelVolume,
            shadowAdaptiveConfig, passes.adaptiveSunShadowState);
    const engine::render::AdaptiveAuxiliaryRayDecision localShadowResolution =
        engine::render::resolveAdaptiveAuxiliaryRayResolution(
            passCreateInfo_.extent, shadowSettings_.ddaRayResolutionScale_,
            voxelDrawPlan.projectedScreenCoverage, cameraInsideVoxelVolume,
            shadowAdaptiveConfig, passes.adaptiveLocalShadowState);
    const engine::render::AdaptiveAuxiliaryRayDecision aoResolution =
        engine::render::resolveAdaptiveAuxiliaryRayResolution(
            passCreateInfo_.extent, aoSettings_.aoRayResolutionScale_,
            voxelDrawPlan.projectedScreenCoverage, cameraInsideVoxelVolume,
            aoAdaptiveConfig, passes.adaptiveAoState);
    const auto logAdaptiveDecision = [&](const char* consumer,
                                         const auto& decision) {
        if (!decision.changed)
        {
            return;
        }
        logInfo(
            "Renderer",
            makeLogMessage(
                consumer, " auxiliary rays: tier=",
                engine::render::adaptiveAuxiliaryRayTierName(decision.tier),
                ", reason=",
                engine::render::adaptiveAuxiliaryRayReasonName(decision.reason),
                ", coverage=", voxelDrawPlan.projectedScreenCoverage,
                ", cameraInside=", cameraInsideVoxelVolume ? "true" : "false",
                ", scale=", decision.effectiveScale, ", extent=",
                decision.activeExtent.width, "x", decision.activeExtent.height));
    };
    logAdaptiveDecision("Sun shadow", sunShadowResolution);
    logAdaptiveDecision("Local shadow", localShadowResolution);
    logAdaptiveDecision("AO", aoResolution);
    // sun-shadow history is always full scene resolution. a ray-extent tier
    // change only changes how the current visibility estimate is sampled and
    // reconstructed, so retaining valid reprojected history avoids exposing a
    // fresh low-sample stochastic penumbra on every adaptive transition.
    if (localShadowResolution.changed)
    {
        shadowSettings_.localShadowResetHistory_ = true;
    }
    if (aoResolution.changed)
    {
        aoSettings_.aoResetHistory_ = true;
    }

    const bool useTemporalDepthHistory =
        useDdaShadows || useAmbientOcclusion || useLocalLightShadows;
    if (useTemporalDepthHistory)
    {
        passes.depthHistory.index = 1 - passes.depthHistory.index;
    }

    const auto recordAuxiliaryTileList = [&](VkExtent2D extent,
                                             AuxiliaryTileListPass::Consumer consumer,
                                             const char* scopeName,
                                             const char* passName, bool hasWork) {
        if (!passes.availability.auxiliaryTileListPass || !hasWork)
        {
            return;
        }
        const uint32_t gpuScopeTileList = gpuProfiler_.beginScope(cmd, scopeName);
        const auto& depthTarget = passes.gbuffer.depth(fc.frameIndex);
        passes.auxiliaryTileListPass.prepareFrame(
            ctx_, depthTarget.view, depthTarget.sampler, consumer, fc.frameIndex);
        engine::render::PassRegistry tileListRegistry;
        tileListRegistry.add(passName, [&](const engine::render::RenderPassContext&) {
            passes.auxiliaryTileListPass.dispatch(
                cmd, glm::ivec2(extent.width, extent.height), consumer,
                fc.frameIndex);
        });
        renderer_.recordPasses(tileListRegistry, frameInputs, fc);
        gpuProfiler_.endScope(cmd, gpuScopeTileList);
    };

    recordAuxiliaryTileList(
        sunShadowResolution.activeExtent,
        AuxiliaryTileListPass::Consumer::SunShadow,
        "Shadow Tile List", "shadow-tile-list",
        useDdaShadows && !sunShadowOccluderVolumes.empty());
    if (useDdaShadows)
    {
        recordDdaShadowPasses(cmd, fc, passes, sunShadowOccluderVolumes, frameInputs,
                              invViewProjUnjittered, lightDir,
                              sunShadowResolution.activeExtent,
                              sunShadowResolution.effectiveScale);
    }

    recordAuxiliaryTileList(
        localShadowResolution.activeExtent,
        AuxiliaryTileListPass::Consumer::LocalShadow,
        "Local Shadow Tile List", "local-shadow-tile-list",
        useLocalLightShadows && !localShadowOccluderVolumes.empty());
    recordLocalLightShadowPasses(cmd, fc, passes, localShadowOccluderVolumes, frameInputs,
                                 useLocalLightShadows, activeLights, maxShadowedLocalLights,
                                 invViewProjUnjittered,
                                 localShadowResolution.activeExtent);

    recordAuxiliaryTileList(
        aoResolution.activeExtent,
        AuxiliaryTileListPass::Consumer::AmbientOcclusion,
        "AO Tile List", "ao-tile-list",
        useAmbientOcclusion && !localLightingOccluderVolumes.empty());
    recordAmbientOcclusionPasses(cmd, fc, passes, localLightingOccluderVolumes, frameInputs,
                                 useAmbientOcclusion, invViewProjJittered,
                                 aoResolution.activeExtent,
                                 aoResolution.effectiveScale);

    passes.updateLightingShadowBindings(ctx_, fc.frameIndex);
    const glm::vec2 projectionJitterUv =
        LightingPass::normalizedProjectionJitterUv(jitter, renderExtent);
    recordMainLightingPass(cmd, fc, passes, frameInputs, lightingUbo, activeLights,
                           useDdaShadows, useAmbientOcclusion, projectionJitterUv);

    recordVoxelGlassRefractionPass(
        cmd, fc, passes, frameInputs,
        !isolateLightingDebugView &&
            (glassSettings_.voxelGlassRefractEnabled_ ||
             (!renderPipelineShowcase_.enabled() &&
              glassSettings_.voxelGlassDebugMode_ > 0)),
        invViewProjUnjittered, activeWaterCount);

    const bool waterBodyEnabled =
        !isolateLightingDebugView && waterSettings_.useWaterV2_ && input_.waterEnabled() && activeWaterCount > 0;
    const int waterBodyDebugMode = effectiveWaterDebugMode;
    const bool isolatedWaterBodyDebug =
        waterBodyDebugMode >= 21 && waterBodyDebugMode <= 23;

    if (waterBodyEnabled && isolatedWaterBodyDebug)
    {
        recordWaterBodyCompositePass(cmd, fc, passes, frameInputs, "Water Body Debug",
                                     invViewProjJittered, waterUbo.sunDirToSun,
                                     waterUbo.waterTint, waterUbo.v2BodyDetail0,
                                     waterUbo.v2BodyShaft0,
                                     activeWaterCount, waterBodyDebugMode,
                                     static_cast<float>(shell.animationNow));
    }

    if (shouldRecordPlanarReflection)
    {
        recordPlanarReflectionPasses(cmd, fc, passes, staticVolumes, frameInputs,
                                     reflectedCamPos, reflectedView, reflectedProj,
                                     reflectedViewProj, cascadeSetup.lightViewProjs,
                                     lightingUbo.lightDir,
                                     lightingUbo.lightColor, lightingUbo.cascadeSplits,
                                     lightingUbo.shadowMapSize, lightingUbo.caustics0,
                                     lightingUbo.caustics1, effectiveVoxelDdaSkipEnabled,
                                     static_cast<float>(shell.animationNow));
    }

    recordWaterGlassSurfacePasses(cmd, fc, passes, frameInputs, drawWater, drawGlass,
                                  shouldRecordPlanarReflection, waterUbo, glassUbo);

    if (waterBodyEnabled && !isolatedWaterBodyDebug)
    {
        recordWaterBodyCompositePass(cmd, fc, passes, frameInputs, "Water Body Final",
                                     invViewProjJittered, waterUbo.sunDirToSun,
                                     waterUbo.waterTint, waterUbo.v2BodyDetail0,
                                     waterUbo.v2BodyShaft0,
                                     activeWaterCount, waterBodyDebugMode,
                                     static_cast<float>(shell.animationNow));
    }

    const int gbufferMode = input_.gbufferMode();
    int postDebugMode = input_.postDebugMode();
    if (renderPipelineShowcase_.enabled())
    {
        postDebugMode = renderPipelineShowcase_.currentStage().postDebugMode;
    }
    frameInputs.renderWaterBody = waterBodyEnabled;
    recordBloomTaaPasses(cmd, fc, passes, frameInputs, frameWorldState, jitter, postDebugMode);

    if (useTemporalDepthHistory)
    {
        recordTemporalDepthHistoryCopy(cmd, fc, passes);
    }

    int compositeMode = gbufferMode;
    if (gbufferMode == 6)
    {
        compositeMode = kViewModeWaterDistance;
    }
    else if (gbufferMode == 7)
    {
        compositeMode = kViewModeWaterDistanceDelta;
    }
    if (gbufferMode == 0 && postDebugMode > 0)
    {
        const int postModes[] = {0, kViewModeBloomExtract, kViewModeBloomBlur,
                                 kViewModeBloomCombined, kViewModeBloomHeatmap,
                                 kViewModeBlueNoiseRaw, kViewModeStochasticNoise,
                                 kViewModeNoiseHemisphere};
        const int maxIndex =
            static_cast<int>(sizeof(postModes) / sizeof(postModes[0])) - 1;
        const int clamped = std::min(postDebugMode, maxIndex);
        compositeMode = postModes[clamped];
    }
    if (renderPipelineShowcase_.enabled())
    {
        compositeMode = renderPipelineShowcase_.currentStage().compositeMode;
    }
    recordCompositeUiPass(cmd, fc, passes, frameInputs, frameWorldState, compositeMode,
                          invViewProjUnjittered, static_cast<float>(shell.animationNow),
                          activeWaterCount);
    completion.viewProjUnjittered = viewProjUnjittered;
    completion.viewProjJittered = viewProjJittered;
    finishFrameRecording(cmd, gpuScopeFrame);
    fc.worldState = nullptr;
}
