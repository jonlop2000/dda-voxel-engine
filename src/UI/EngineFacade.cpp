#include "UI/EngineFacade.h"

#include "App/App.h"
#include "App/AppSceneVolume.h"
#include "engine/scene/WorldStateView.h"

EngineFacade::EngineFacade(App& app, EditorState& editorState)
    : app_(&app), editorState_(&editorState)
{
}

// editor reads of shared world/config state go through the decision #6
// WorldStateView; writes go through named App apis or the settings buckets.
const SceneConfig& EngineFacade::sceneConfig() const
{
    return app_->worldStateView().sceneConfig;
}

engine::scene::ScenePresentationProfileReadout
EngineFacade::presentationProfileReadout() const
{
    return app_->presentationProfileReadout();
}

const std::vector<SceneCatalogEntry>& EngineFacade::availableScenes() const
{
    return app_->availableScenes();
}

const std::filesystem::path& EngineFacade::currentScenePath() const
{
    return app_->currentScenePath();
}

uint64_t EngineFacade::sceneCatalogRevision() const
{
    return app_->worldStateView().sceneCatalogRevision;
}

void EngineFacade::refreshAvailableScenes(bool logResults)
{
    app_->refreshSceneCatalog(logResults);
}

bool EngineFacade::loadSceneFromFile(const std::filesystem::path& path)
{
    return app_->loadSceneFromFile(path);
}

void EngineFacade::reloadScene(const SceneConfig& config)
{
    app_->reloadScene(config);
}

void EngineFacade::applyCloudSettings(const SceneConfig& config)
{
    app_->applyCloudSettings(config);
}

bool EngineFacade::isCloudBuildInProgress() const
{
    return app_->isCloudBuildInProgress();
}

std::vector<EngineFacade::FishHandle> EngineFacade::fishList() const
{
    return app_->fishList();
}

bool EngineFacade::selectFish(uint64_t fishId)
{
    return app_->selectFish(fishId);
}

void EngineFacade::clearFishSelection()
{
    app_->clearFishSelection();
}

uint64_t EngineFacade::selectedFishId() const
{
    return app_->selectedFishId();
}

bool EngineFacade::focusOnFish(uint64_t fishId)
{
    return app_->focusOnFish(fishId);
}

bool EngineFacade::releaseFishFocus()
{
    return app_->releaseFishFocus();
}

bool EngineFacade::isFishFocusActive() const
{
    return app_->isFishFocusActive();
}

bool EngineFacade::isFishFocusReturning() const
{
    return app_->isFishFocusReturning();
}

uint64_t EngineFacade::focusedFishId() const
{
    return app_->focusedFishId();
}

float EngineFacade::fishFocusOrbitDistance() const
{
    return app_->fishFocusSettings().fishFocusOrbitDistance_;
}

void EngineFacade::setFishFocusOrbitDistance(float distance)
{
    app_->fishFocusSettings().fishFocusOrbitDistance_ = distance;
}

float EngineFacade::fishFocusHeightOffset() const
{
    return app_->fishFocusSettings().fishFocusHeightOffset_;
}

void EngineFacade::setFishFocusHeightOffset(float offset)
{
    app_->fishFocusSettings().fishFocusHeightOffset_ = offset;
}

float EngineFacade::fishFocusPositionDamping() const
{
    return app_->fishFocusSettings().fishFocusPositionDamping_;
}

void EngineFacade::setFishFocusPositionDamping(float damping)
{
    app_->fishFocusSettings().fishFocusPositionDamping_ = damping;
}

float EngineFacade::fishFocusRotationDamping() const
{
    return app_->fishFocusSettings().fishFocusRotationDamping_;
}

void EngineFacade::setFishFocusRotationDamping(float damping)
{
    app_->fishFocusSettings().fishFocusRotationDamping_ = damping;
}

bool EngineFacade::fishShakeEnabled() const
{
    return app_->fishFocusSettings().fishShakeEnabled_;
}

void EngineFacade::setFishShakeEnabled(bool enabled)
{
    app_->fishFocusSettings().fishShakeEnabled_ = enabled;
}

bool EngineFacade::fishCelebrationEnabled() const
{
    return app_->fishCelebrationEnabled();
}

void EngineFacade::setFishCelebrationEnabled(bool enabled)
{
    app_->setFishCelebrationEnabled(enabled);
}

bool EngineFacade::axolotlBellyFloatEnabled() const
{
    return app_->axolotlBellyFloatEnabled();
}

void EngineFacade::setAxolotlBellyFloatEnabled(bool enabled)
{
    app_->setAxolotlBellyFloatEnabled(enabled);
}

bool EngineFacade::axolotlEnabled() const
{
    return app_->axolotlEnabled();
}

void EngineFacade::setAxolotlEnabled(bool enabled)
{
    app_->setAxolotlEnabled(enabled);
}

float EngineFacade::fishShakePositionAmplitude() const
{
    return app_->fishFocusSettings().fishShakePositionAmplitude_;
}

void EngineFacade::setFishShakePositionAmplitude(float amplitude)
{
    app_->fishFocusSettings().fishShakePositionAmplitude_ = amplitude;
}

float EngineFacade::fishShakeRotationAmplitude() const
{
    return app_->fishFocusSettings().fishShakeRotationAmplitude_;
}

void EngineFacade::setFishShakeRotationAmplitude(float amplitude)
{
    app_->fishFocusSettings().fishShakeRotationAmplitude_ = amplitude;
}

float EngineFacade::fishShakeFrequency() const
{
    return app_->fishFocusSettings().fishShakeFrequency_;
}

void EngineFacade::setFishShakeFrequency(float frequency)
{
    app_->fishFocusSettings().fishShakeFrequency_ = frequency;
}

float EngineFacade::fishShakeSettleDecay() const
{
    return app_->fishFocusSettings().fishShakeSettleDecay_;
}

void EngineFacade::setFishShakeSettleDecay(float decay)
{
    app_->fishFocusSettings().fishShakeSettleDecay_ = decay;
}

void EngineFacade::setEnvironmentWindSettings(
    const engine::scene::EnvironmentWindSettings& wind)
{
    app_->setEnvironmentWindSettings(wind);
}

void EngineFacade::setEnvironmentTimeSettings(
    const engine::scene::EnvironmentTimeSettings& time)
{
    app_->setEnvironmentTimeSettings(time);
}

void EngineFacade::setEnvironmentTimePreset(
    engine::scene::EnvironmentTimePreset preset)
{
    app_->setEnvironmentTimePreset(preset);
}

const engine::scene::EnvironmentTimeSample&
EngineFacade::environmentTimeSample() const
{
    return app_->environmentTimeSample();
}

void EngineFacade::setWindborneParticleSettings(
    const engine::scene::WindborneParticleSettings& particles)
{
    app_->setWindborneParticleSettings(particles);
}

void EngineFacade::setCloudWindSpeed(float speed)
{
    app_->setCloudWindSpeed(speed);
}

void EngineFacade::setCloudWindDirection(const glm::vec2& direction)
{
    app_->setCloudWindDirection(direction);
}

void EngineFacade::setSceneCloudShadowStrength(float strength)
{
    app_->setSceneCloudShadowStrength(strength);
}

void EngineFacade::applySkyPreset(int preset)
{
    app_->applySceneSkyPreset(preset);
}

const glm::vec3& EngineFacade::skyColor() const
{
    return app_->skyColor();
}

void EngineFacade::setSkyColor(const glm::vec3& color)
{
    app_->setSceneSkyColor(color);
}

int EngineFacade::viewMode() const
{
    return app_->worldStateView().diagnostics.viewMode_;
}

void EngineFacade::setViewMode(int mode)
{
    app_->diagnosticsSettings().viewMode_ = mode;
}

glm::vec3 EngineFacade::cameraPosition() const
{
    return app_->camera().position;
}

float EngineFacade::cameraYaw() const
{
    return app_->camera().yaw;
}

float EngineFacade::cameraPitch() const
{
    return app_->camera().pitch;
}

glm::mat4 EngineFacade::cameraViewProjection(float aspect) const
{
    return app_->camera().projMatrix(aspect) * app_->camera().viewMatrix();
}

bool EngineFacade::resolveEditorSelectedPlaceable(
    PlaceableInstance& instance) const
{
    return app_->resolveEditorSelectedPlaceable(instance);
}

bool EngineFacade::commitEditorPlaceableEdit(
    const PlaceableEditCommand& command)
{
    return app_->commitEditorPlaceableEdit(command);
}

std::vector<PlaceableInstance> EngineFacade::editorPlaceables() const
{
    return app_->editorPlaceables();
}

bool EngineFacade::editorPlaceableAuthoringAvailable() const
{
    return app_->editorPlaceableAuthoringAvailable();
}

std::vector<PlaceablePrototype> EngineFacade::editorPlaceablePrototypes() const
{
    return app_->editorPlaceablePrototypes();
}

bool EngineFacade::editorCreatePlaceable(const std::string& prototypeSlug,
                                         uint32_t prototypeVersion,
                                         const glm::vec2& positionXZ)
{
    return app_->editorCreatePlaceable(prototypeSlug, prototypeVersion,
                                       positionXZ);
}

bool EngineFacade::editorDuplicateSelectedPlaceable(
    const glm::vec2& offsetXZ)
{
    return app_->editorDuplicateSelectedPlaceable(offsetXZ);
}

bool EngineFacade::editorDeleteSelectedPlaceable()
{
    return app_->editorDeleteSelectedPlaceable();
}

bool EngineFacade::editorUndoPlaceableEdit()
{
    return app_->editorUndoPlaceableEdit();
}

bool EngineFacade::editorRedoPlaceableEdit()
{
    return app_->editorRedoPlaceableEdit();
}

bool EngineFacade::editorSaveCurrentScene()
{
    return app_->editorSaveCurrentScene();
}

bool EngineFacade::editorHasPlaceableUndo() const
{
    return app_->editorHasPlaceableUndo();
}

bool EngineFacade::editorHasPlaceableRedo() const
{
    return app_->editorHasPlaceableRedo();
}

bool EngineFacade::editorPlaceableSceneDirty() const
{
    return app_->editorPlaceableSceneDirty();
}

const std::string& EngineFacade::editorPlaceableEditStatus() const
{
    return app_->editorPlaceableEditStatus();
}

uint64_t EngineFacade::editorVoxelStructureRevision() const
{
    return app_->editorVoxelStructureRevision();
}

void EngineFacade::resetCamera()
{
    app_->resetCamera();
}

void EngineFacade::enterRuntimeUiPlayPreview(bool startAtMainMenu)
{
    app_->enterRuntimeUiPlayPreview(startAtMainMenu ? "Run menu" : "Play Runtime UI",
                                    startAtMainMenu);
}

bool EngineFacade::isRuntimeUiPlayPreviewActive() const
{
    return app_->isRuntimeUiPlayPreviewActive();
}

SwapPresentMode EngineFacade::presentMode() const
{
    return app_->presentMode();
}

void EngineFacade::setPresentMode(SwapPresentMode mode)
{
    app_->setPresentMode(mode);
}

const char* EngineFacade::activePresentModeLabel() const
{
    return app_->activePresentModeLabel();
}

float EngineFacade::maxFpsLimit() const
{
    return app_->worldStateView().framePacing.maxFpsLimit_;
}

void EngineFacade::setMaxFpsLimit(float fps)
{
    app_->framePacingSettings().maxFpsLimit_ = fps;
}

bool EngineFacade::focusedIdleThrottleEnabled() const
{
    return app_->worldStateView().framePacing.focusedIdleThrottleEnabled_;
}

void EngineFacade::setFocusedIdleThrottleEnabled(bool enabled)
{
    app_->framePacingSettings().focusedIdleThrottleEnabled_ = enabled;
}

float EngineFacade::focusedIdleFpsLimit() const
{
    return app_->worldStateView().framePacing.focusedIdleFpsLimit_;
}

void EngineFacade::setFocusedIdleFpsLimit(float fps)
{
    app_->framePacingSettings().focusedIdleFpsLimit_ = fps;
}

float EngineFacade::focusedIdleDelaySeconds() const
{
    return app_->worldStateView().framePacing.focusedIdleDelaySeconds_;
}

void EngineFacade::setFocusedIdleDelaySeconds(float seconds)
{
    app_->framePacingSettings().focusedIdleDelaySeconds_ = seconds;
}

bool EngineFacade::backgroundThrottleEnabled() const
{
    return app_->worldStateView().framePacing.backgroundThrottleEnabled_;
}

void EngineFacade::setBackgroundThrottleEnabled(bool enabled)
{
    app_->framePacingSettings().backgroundThrottleEnabled_ = enabled;
}

float EngineFacade::backgroundFpsLimit() const
{
    return app_->worldStateView().framePacing.backgroundFpsLimit_;
}

void EngineFacade::setBackgroundFpsLimit(float fps)
{
    app_->framePacingSettings().backgroundFpsLimit_ = fps;
}

void EngineFacade::requestTaaHistoryReset()
{
    app_->postFxSettings().taaResetHistory_ = true;
}

void EngineFacade::requestShadowHistoryReset()
{
    app_->shadowSettings().shadowResetHistory_ = true;
}

void EngineFacade::requestAoHistoryReset()
{
    app_->ambientOcclusionSettings().aoResetHistory_ = true;
}

bool EngineFacade::tonemapEnabled() const
{
    return app_->worldStateView().postFx.tonemapEnabled_;
}

void EngineFacade::setTonemapEnabled(bool enabled)
{
    app_->postFxSettings().tonemapEnabled_ = enabled;
}

bool EngineFacade::bloomEnabled() const
{
    return app_->worldStateView().postFx.bloomEnabled_;
}

void EngineFacade::setBloomEnabled(bool enabled)
{
    app_->postFxSettings().bloomEnabled_ = enabled;
}

bool EngineFacade::gpuProfilerEnabled() const
{
    return app_->worldStateView().diagnostics.gpuProfilerEnabled_;
}

void EngineFacade::setGpuProfilerEnabled(bool enabled)
{
    app_->setGpuProfilerEnabled(enabled);
}

bool EngineFacade::pixelInspectEnabled() const
{
    return app_->worldStateView().diagnostics.pixelInspectEnabled_;
}

void EngineFacade::setPixelInspectEnabled(bool enabled)
{
    app_->diagnosticsSettings().pixelInspectEnabled_ = enabled;
}

float EngineFacade::cpuFrameTimeMs() const
{
    return app_->cpuFrameTimeMs();
}

float EngineFacade::gpuFrameTimeMs() const
{
    return app_->gpuFrameTimeMs();
}

float EngineFacade::effectiveFpsLimit() const
{
    return app_->effectiveFpsLimit();
}

bool EngineFacade::isWindowFocused() const
{
    return app_->isWindowFocused();
}

bool EngineFacade::isBackgroundThrottleActive() const
{
    return app_->isBackgroundThrottleActive();
}

bool EngineFacade::isFocusedIdleThrottleActive() const
{
    return app_->isFocusedIdleThrottleActive();
}

const std::vector<float>& EngineFacade::cpuFrameHistoryMs() const
{
    return app_->cpuFrameHistoryMs();
}

const std::vector<float>& EngineFacade::gpuFrameHistoryMs() const
{
    return app_->gpuFrameHistoryMs();
}

GpuProfiler& EngineFacade::gpuProfiler()
{
    return app_->gpuProfiler();
}

const GpuProfiler& EngineFacade::gpuProfiler() const
{
    return app_->gpuProfiler();
}

EngineFacade::RenderingPanelAccess EngineFacade::renderingPanel()
{
    return RenderingPanelAccess(*app_, *editorState_);
}

EngineFacade::RenderingPanelAccess::RenderingPanelAccess(App& app, EditorState& state)
    : editorState(state)
    , lighting(app.lightingSettings())
    , shadows(app.shadowSettings())
    , ao(app.ambientOcclusionSettings())
    , postFx(app.postFxSettings())
    , water(app.waterSettings())
    , glass(app.glassSettings())
    , voxelDebug(app.voxelDebugSettings())
    , diagnostics(app.diagnosticsSettings())
    , app_(&app)
{
}

Camera& EngineFacade::RenderingPanelAccess::camera()
{
    return app_->camera();
}

const Camera& EngineFacade::RenderingPanelAccess::camera() const
{
    return app_->camera();
}

void EngineFacade::RenderingPanelAccess::resetAreaLightsToScenePreset()
{
    app_->resetAreaLightsToScenePreset();
}

void EngineFacade::RenderingPanelAccess::updateSunDirection()
{
    app_->updateSunDirection();
}

void EngineFacade::RenderingPanelAccess::regenerateVoxelWorld()
{
    app_->regenerateVoxelWorld();
}

void EngineFacade::RenderingPanelAccess::applyRenderQualityPreset(
    engine::render::RenderQualityPreset preset)
{
    app_->applyRenderQualityPreset(preset);
}

void EngineFacade::RenderingPanelAccess::setRenderScale(float scale)
{
    app_->setRenderScale(scale);
}

void EngineFacade::RenderingPanelAccess::setAuxiliaryRayScales(float ddaShadowScale,
                                                               float aoScale)
{
    app_->setAuxiliaryRayScales(ddaShadowScale, aoScale);
}

void EngineFacade::RenderingPanelAccess::setGpuProfilerEnabled(bool enabled)
{
    app_->setGpuProfilerEnabled(enabled);
}

void EngineFacade::RenderingPanelAccess::startRenderPipelineShowcase(
    double secondsPerStage, bool loop, bool startPaused)
{
    app_->startRenderPipelineShowcaseFromEditor(secondsPerStage, loop, startPaused);
}

const char* EngineFacade::RenderingPanelAccess::activePresentModeLabel() const
{
    return app_->activePresentModeLabel();
}

float EngineFacade::RenderingPanelAccess::cpuFrameTimeMs() const
{
    return app_->cpuFrameTimeMs();
}

float EngineFacade::RenderingPanelAccess::gpuFrameTimeMs() const
{
    return app_->gpuFrameTimeMs();
}

float EngineFacade::RenderingPanelAccess::effectiveFpsLimit() const
{
    return app_->effectiveFpsLimit();
}

bool EngineFacade::RenderingPanelAccess::focusedIdleThrottleEnabled() const
{
    return app_->worldStateView().framePacing.focusedIdleThrottleEnabled_;
}

void EngineFacade::RenderingPanelAccess::setFocusedIdleThrottleEnabled(bool enabled)
{
    app_->framePacingSettings().focusedIdleThrottleEnabled_ = enabled;
}

float EngineFacade::RenderingPanelAccess::focusedIdleFpsLimit() const
{
    return app_->worldStateView().framePacing.focusedIdleFpsLimit_;
}

void EngineFacade::RenderingPanelAccess::setFocusedIdleFpsLimit(float fps)
{
    app_->framePacingSettings().focusedIdleFpsLimit_ = fps;
}

float EngineFacade::RenderingPanelAccess::focusedIdleDelaySeconds() const
{
    return app_->worldStateView().framePacing.focusedIdleDelaySeconds_;
}

void EngineFacade::RenderingPanelAccess::setFocusedIdleDelaySeconds(float seconds)
{
    app_->framePacingSettings().focusedIdleDelaySeconds_ = seconds;
}

bool EngineFacade::RenderingPanelAccess::isFocusedIdleThrottleActive() const
{
    return app_->isFocusedIdleThrottleActive();
}

bool EngineFacade::RenderingPanelAccess::isBackgroundThrottleActive() const
{
    return app_->isBackgroundThrottleActive();
}

const std::vector<float>& EngineFacade::RenderingPanelAccess::cpuFrameHistoryMs() const
{
    return app_->cpuFrameHistoryMs();
}

const std::vector<float>& EngineFacade::RenderingPanelAccess::gpuFrameHistoryMs() const
{
    return app_->gpuFrameHistoryMs();
}

bool EngineFacade::RenderingPanelAccess::isDdaShadowsAvailable() const
{
    return app_->isDdaShadowsAvailable();
}

bool EngineFacade::RenderingPanelAccess::isAmbientOcclusionAvailable() const
{
    return app_->isAmbientOcclusionAvailable();
}

bool EngineFacade::RenderingPanelAccess::isLocalLightShadowsAvailable() const
{
    return app_->isLocalLightShadowsAvailable();
}

bool EngineFacade::RenderingPanelAccess::isLocalShadowBlurAvailable() const
{
    return app_->isLocalShadowBlurAvailable();
}

bool EngineFacade::RenderingPanelAccess::isSunroofScene() const
{
    return isSunroofAquariumScene(app_->worldStateView().sceneConfig);
}

void EngineFacade::RenderingPanelAccess::markWaterParametersDirty()
{
    app_->markWaterParametersDirty();
}

void EngineFacade::RenderingPanelAccess::setUseWaterV2(bool enabled)
{
    app_->setUseWaterV2(enabled);
}

void EngineFacade::RenderingPanelAccess::setProceduralFishEnabled(bool enabled)
{
    app_->setProceduralFishEnabled(enabled);
}

void EngineFacade::RenderingPanelAccess::setProceduralFishCount(int count)
{
    app_->setProceduralFishCount(count);
}

GpuProfiler& EngineFacade::RenderingPanelAccess::gpuProfiler()
{
    return app_->gpuProfiler();
}

const GpuProfiler& EngineFacade::RenderingPanelAccess::gpuProfiler() const
{
    return app_->gpuProfiler();
}

engine::DDAMetrics& EngineFacade::RenderingPanelAccess::cachedDdaMetrics()
{
    return app_->cachedDdaMetrics();
}

const engine::DDAMetrics& EngineFacade::RenderingPanelAccess::cachedDdaMetrics() const
{
    return app_->cachedDdaMetrics();
}

engine::WaterVolumeManager& EngineFacade::RenderingPanelAccess::waterVolumeMgr()
{
    return app_->waterVolumeMgr();
}

const engine::WaterVolumeManager& EngineFacade::RenderingPanelAccess::waterVolumeMgr() const
{
    return app_->waterVolumeMgr();
}
