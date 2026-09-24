#pragma once
struct GLFWwindow; namespace engine::render { struct FrameInputs; struct RendererPassResources; class PassRegistry; class VoxelRenderResources; }
namespace engine::scene { struct ScenePresentationProfileReadout; struct WorldStateView; }
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "App/AppFrameShell.h"
#include "App/Camera.h"
#include "App/Transform.h"
#include "Assets/Material.h"
#include "Assets/Texture.h"
#include "Core/JobSystem.h"
#include "engine/render/passes/PassCreateInfo.h"
#include "engine/render/FrameContext.h"
#include "engine/render/Frustum.h"
#include "engine/render/GpuProfiler.h"
#include "engine/performance/AutomationProfileSession.h"
#include "engine/render/RenderObject.h"
#include "engine/render/RendererConfig.h"
#include "engine/render/Swapchain.h"
#include "engine/render/VulkanContext.h"
#include "engine/physics/BallPhysics.h"
#include "Resources/LightsBuffer.h"
#include "Resources/MeshUploadQueue.h"
#include "UI/Runtime/UiActions.h"
#include "UI/Runtime/UiContext.h"
#include "UI/Runtime/UiDebugFont.h"
#include "UI/Runtime/UiElement.h"
#include "UI/Runtime/UiScreenControllers.h"
#include "UI/Runtime/UiTypes.h"
#if VOXEL_WITH_EDITOR
#include "UI/EditorState.h"
#include "UI/ImGuiLayer.h"
#endif
#include "Water/WaterContainer.h"
#include "engine/game/AxolotlModel.h"
#include "engine/game/FishTypes.h"
#include "engine/voxel/WaterVolume.h"
#include "engine/voxel/ModularGlassMeshManager.h"
#include "engine/voxel/VoxelMaterialAtlas.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelWorld.h"
#include "engine/voxel/ChunkGrid.h"
#include "engine/voxel/ProceduralWorldSettings.h"
#include "engine/voxel/Raycast.h"
#include "engine/voxel/VoxelTypes.h"
#include "engine/editor/SceneDocument.h"
#include "engine/scene/SceneConfig.h"
#include "engine/game/GameRuntime.h"
#include "engine/input/InputController.h"
#include "engine/render/Renderer.h"
#include "engine/render/RenderPipelineShowcase.h"
#include "engine/render/RenderQualityPreset.h"
#include "engine/render/RenderSettings.h"
#include "engine/render/voxel/VoxelRenderResources.h"
#include "engine/scene/SceneManager.h"
class App : private engine::input::InputController::Delegate
{
public:
    int run(int argc, char** argv);
    // =========================================================================
    // public ui state - accessible by Editor panels
    // =========================================================================
    // --- performance ---
    engine::render::FramePacingSettings framePacingSettings_{};
    engine::render::FramePacingSettings& framePacingSettings() { return framePacingSettings_; }
    const engine::render::FramePacingSettings& framePacingSettings() const { return framePacingSettings_; }
    SwapPresentMode presentMode() const { return presentMode_; }
    const char* activePresentModeLabel() const;
    void setPresentMode(SwapPresentMode mode);
    void applyRenderQualityPreset(engine::render::RenderQualityPreset preset);
    void setRenderScale(float scale);
    void setAuxiliaryRayScales(float ddaShadowScale, float aoScale);
    void setGpuProfilerEnabled(bool enabled);
    float cpuFrameTimeMs() const { return lastCpuFrameMs_; }
    float gpuFrameTimeMs() const { return lastGpuFrameMs_; }
    float effectiveFpsLimit() const { return effectiveFpsLimit_; }
    bool isWindowFocused() const { return windowFocused_; }
    bool isFocusedIdleThrottleActive() const { return focusedIdleThrottleActive_; }
    bool isBackgroundThrottleActive() const { return backgroundThrottleActive_; }
    const std::vector<float>& cpuFrameHistoryMs() const { return cpuFrameHistoryMs_; }
    const std::vector<float>& gpuFrameHistoryMs() const { return gpuFrameHistoryMs_; }
    bool isDdaShadowsAvailable() const;
    bool isAmbientOcclusionAvailable() const;
    bool isLocalLightShadowsAvailable() const;
    bool isLocalShadowBlurAvailable() const;
    using EditableAreaLight = engine::render::EditableAreaLight;
    engine::render::LightingSettings& lightingSettings() { return lightingSettings_; }
    const engine::render::LightingSettings& lightingSettings() const { return lightingSettings_; }
    engine::render::ShadowSettings& shadowSettings() { return shadowSettings_; }
    const engine::render::ShadowSettings& shadowSettings() const { return shadowSettings_; }
    engine::render::AmbientOcclusionSettings& ambientOcclusionSettings() { return aoSettings_; }
    const engine::render::AmbientOcclusionSettings& ambientOcclusionSettings() const { return aoSettings_; }
    engine::render::PostFxSettings& postFxSettings() { return postFxSettings_; }
    const engine::render::PostFxSettings& postFxSettings() const { return postFxSettings_; }
    engine::render::WaterSettings& waterSettings() { return waterSettings_; }
    const engine::render::WaterSettings& waterSettings() const { return waterSettings_; }
    engine::render::GlassSettings& glassSettings() { return glassSettings_; }
    const engine::render::GlassSettings& glassSettings() const { return glassSettings_; }
    // --- Camera ---
    Camera& camera() { return camera_; }
    const Camera& camera() const { return camera_; }
    void resetCamera();
    // --- scene ---
    const SceneConfig& sceneConfig() const { return sceneManager_.sceneConfig(); }
    SceneConfig& sceneConfigMutable() { return sceneManager_.sceneConfigMutable(); }
    // decision #6 shared read view (SceneConfig + settings buckets); AppSceneSettings.cpp.
    engine::scene::WorldStateView worldStateView() const; engine::scene::ScenePresentationProfileReadout presentationProfileReadout() const;
    const std::vector<SceneCatalogEntry>& availableScenes() const
    {
        return sceneManager_.availableScenes();
    }
    const std::filesystem::path& currentScenePath() const
    {
        return sceneManager_.currentScenePath();
    }
    void refreshSceneCatalog(bool logResults = false);
    bool loadSceneFromFile(const std::filesystem::path& path);
    void reloadScene(const SceneConfig& newConfig);
    void applyCloudSettings(const SceneConfig& config);
    bool isCloudBuildInProgress() const;
#if VOXEL_WITH_EDITOR
    bool resolveEditorSelectedPlaceable(PlaceableInstance& instance) const; std::vector<PlaceableInstance> editorPlaceables() const; bool editorPlaceableAuthoringAvailable() const; std::vector<PlaceablePrototype> editorPlaceablePrototypes() const; bool editorCreatePlaceable(const std::string& prototypeSlug, uint32_t prototypeVersion, const glm::vec2& positionXZ); bool editorDuplicateSelectedPlaceable(const glm::vec2& offsetXZ); bool editorDeleteSelectedPlaceable(); bool commitEditorPlaceableEdit(const PlaceableEditCommand& command); bool editorUndoPlaceableEdit(); bool editorRedoPlaceableEdit(); bool editorSaveCurrentScene(); bool editorHasPlaceableUndo() const; bool editorHasPlaceableRedo() const; bool editorPlaceableSceneDirty() const; const std::string& editorPlaceableEditStatus() const; uint64_t editorVoxelStructureRevision() const;
#endif
    void markWaterParametersDirty() { waterParametersDirty_ = true; } void setUseWaterV2(bool enabled);
    // live production and compatibility wind controls (no scene reload)
    void setEnvironmentWindSettings(const engine::scene::EnvironmentWindSettings& wind); void setEnvironmentTimeSettings(const engine::scene::EnvironmentTimeSettings& time); void setEnvironmentTimePreset(engine::scene::EnvironmentTimePreset preset); const engine::scene::EnvironmentTimeSample& environmentTimeSample() const { return environmentTimeSample_; } void setWindborneParticleSettings(const engine::scene::WindborneParticleSettings& particles); void setCloudWindSpeed(float speed); void setCloudWindDirection(const glm::vec2& dir);
    void setSceneCloudShadowStrength(float strength); void applySceneSkyPreset(int preset);
    const glm::vec3& skyColor() const { return skyColor_; } void setSceneSkyColor(const glm::vec3& color);
    // --- voxel world ---
    engine::render::VoxelDebugSettings voxelDebugSettings_{};
    engine::render::VoxelDebugSettings& voxelDebugSettings() { return voxelDebugSettings_; }
    const engine::render::VoxelDebugSettings& voxelDebugSettings() const { return voxelDebugSettings_; }
    // --- lighting ---
    engine::render::LightingSettings lightingSettings_{};
    void resetAreaLightsToScenePreset();
    // --- sun light ---
    glm::vec3 sunDirection_ = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.3f));
    void updateSunDirection();
    // --- sky ---
    int skyPreset_ = 0;  // 0=day, 1=sunset, 2=night, 3=dawn, 4=custom
    glm::vec3 skyColor_ = glm::vec3(0.09f, 0.29f, 0.88f);  // default blue sky
    void applySkyPreset(int preset);
    // --- shadows / ambient occlusion / post-Processing ---
    engine::render::ShadowSettings shadowSettings_{};
    engine::render::AmbientOcclusionSettings aoSettings_{};
    engine::render::PostFxSettings postFxSettings_{};
    // --- water / glass ---
    engine::render::WaterSettings waterSettings_{};
    engine::render::GlassSettings glassSettings_{};
    engine::WaterVolumeManager& waterVolumeMgr() { return waterVolumeMgr_; }
    // --- debug / diagnostics (view mode, profiler, pixel inspect, dda metrics) ---
    engine::render::DiagnosticsSettings diagnosticsSettings_{};
    engine::render::DiagnosticsSettings& diagnosticsSettings() { return diagnosticsSettings_; }
    const engine::render::DiagnosticsSettings& diagnosticsSettings() const { return diagnosticsSettings_; }
    engine::DDAMetrics& cachedDdaMetrics() { return cachedDdaMetrics_; }
    // --- gpu profiler ---
    GpuProfiler& gpuProfiler() { return gpuProfiler_; }
    // --- actions ---
    void regenerateVoxelWorld();
    void setProceduralFishEnabled(bool enabled);
    void setProceduralFishCount(int count);
    bool axolotlEnabled() const { return voxelDebugSettings_.axolotlEnabled_; }
    void setAxolotlEnabled(bool enabled);
    void enterRuntimeUiPlayPreview(const char* reason = "Play Runtime UI",
                                   bool startAtMainMenu = false) override;
    void exitRuntimeUiPlayPreview(const char* reason = "F5") override;
    bool isRuntimeUiPlayPreviewActive() const { return appMode_ == AppMode::PlayPreview; } void startRenderPipelineShowcaseFromEditor(double secondsPerStage, bool loop, bool startPaused);
    // --- fish focus (cinematic fish-focus camera; enumeration + selection) ---
    using FishHandle = engine::game::FishHandle;
    std::vector<FishHandle> fishList() const;
    bool selectFish(uint64_t fishId);
    void clearFishSelection();
    uint64_t selectedFishId() const { return selectedFishId_; }
    // cinematic focus mode. focusOnFish glides the camera to frame the fish
    // and tracks it; releaseFishFocus eases back to the pose saved at focus entry.
    bool focusOnFish(uint64_t fishId);
    bool releaseFishFocus() override;
    bool isFishFocusActive() const { return fishFocus_.fishId != 0; }
    bool isFishFocusReturning() const { return fishFocus_.returning; }
    uint64_t focusedFishId() const
    {
        return fishFocus_.returning ? 0 : fishFocus_.fishId;
    }
    // fish-focus framing tunables (ui-editable, like the other settings buckets).
    engine::game::FishFocusSettings fishFocusSettings_{};
    engine::game::FishFocusSettings& fishFocusSettings() { return fishFocusSettings_; }
    const engine::game::FishFocusSettings& fishFocusSettings() const
    {
        return fishFocusSettings_;
    }
    bool fishCelebrationEnabled() const { return gameRuntime_.fishCelebration().enabled(); }
    void setFishCelebrationEnabled(bool enabled) { gameRuntime_.setFishCelebrationEnabled(enabled); }
    bool axolotlBellyFloatEnabled() const { return gameRuntime_.axolotlBellyFloat().enabled(); }
    void setAxolotlBellyFloatEnabled(bool enabled) { gameRuntime_.setAxolotlBellyFloatEnabled(enabled); }

private:
    struct DdaMetricsSample
    {
        float hitRate = 0.0f;
        float avgIterations = 0.0f;
        float avgSkipJumps = 0.0f;
        float sampleCount = 0.0f;
    };
    enum class AutomationUiInputKind
    {
        PointerMove,
        PointerDown,
        PointerUp,
        Wheel,
        KeyDown,
    };
    struct AutomationUiInputEvent
    {
        uint64_t frame = 0;
        AutomationUiInputKind kind = AutomationUiInputKind::PointerMove;
        double x = 0.0;
        double y = 0.0;
        int key = 0;
        int button = 0;
        double scrollY = 0.0;
    };
#if VOXEL_WITH_EDITOR
    struct EditorVisibilitySnapshot
    {
        bool valid = false;
        bool enabled = true;
        bool leftPanelVisible = true;
        bool rightPanelVisible = true;
        bool statusBarVisible = true;
        bool profilerOverlayVisible = true;
    };
#endif
    static constexpr size_t kFishPaletteCount = 3;
    struct ProceduralFishInstance
    {
        uint64_t id = 0;  // stable fish-focus identity, assigned at spawn; 0 = invalid
        uint32_t stableSpeciesOrdinal = 0;
        glm::vec3 centerOffsetFrac{0.0f};
        glm::vec3 orbitRadiusFrac{0.0f};
        glm::vec3 wobbleAmplitudeFrac{0.0f};
        float speed = 1.0f;
        float pathPhase = 0.0f;
        float wagPhase = 0.0f;
        float scale = 1.0f;
        size_t paletteIndex = 0;
        bool isAxolotl = false;
        int bodyVolumeIndex = -1;
        int tailVolumeIndex = -1;
        std::array<int, engine::game::kAxolotlLimbCount> axolotlLimbVolumeIndices{
            -1, -1, -1, -1};
    };
    struct HeroFoliageInstance
    {
        std::string placeableUuid{};
        std::string prototypeSlug{};
        uint32_t prototypeVersion = 1;
        uint32_t seed = 0;
        glm::vec3 rootWorld{0.0f};
        glm::vec3 scale{1.0f};
        glm::vec2 swayDirection{1.0f, 0.0f};
        glm::quat restRotation{1.0f, 0.0f, 0.0f, 0.0f};
        float amplitude = 0.04f;
        float speed = 0.5f;
        float twistScale = 0.28f;
        float phase = 0.0f;
        int volumeIndex = -1;
    };
    void init(int argc, char** argv); void mainLoop();
    // frame shell: update half -> seam struct -> record half
    // shared state is carried by FrameShellInputs.
    void shutdown(); void drawFrame();
    FrameShellInputs updateFrame(); void recordFrame(const FrameShellInputs& shell); void recordFrameCommands(const FrameShellInputs& shell, FrameContext& fc, FrameCompletionState& completion);
    uint32_t prepareCompletedFrameResources(); void readCompletedDdaMetrics(engine::render::RendererPassResources& passes, uint32_t frameIndex);
    void recordShadowMapPass(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, engine::render::FrameInputs& frameInputs, const ShadowPass::FrameUbo& shadowUbo); void recordGBufferMeshPass(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, engine::render::FrameInputs& frameInputs, const GBufferPass::FrameUbo& gbufferUbo); void recordWaterVolumePrepass(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, engine::render::FrameInputs& frameInputs, const glm::mat4& invViewProjJittered);
    void recordPrimaryVoxelDdaPasses(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, const engine::render::VoxelRenderResources::VolumeDrawPlan& voxelDrawPlan, engine::render::FrameInputs& frameInputs, float animationTimeSeconds);
    void recordReflectionVoxelDdaPasses(const FrameContext& fc, engine::render::RendererPassResources& passes, const std::vector<engine::render::VoxelRenderResources::VolumeDrawPlan::Entry>& staticVolumes, engine::render::FrameInputs& frameInputs, bool effectiveVoxelDdaSkipEnabled, float animationTimeSeconds);
    void recordDdaShadowPasses(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, const std::vector<engine::render::VoxelRenderResources::VolumeDrawPlan::Entry>& sunShadowOccluderVolumes, engine::render::FrameInputs& frameInputs, const glm::mat4& invViewProjUnjittered, const glm::vec3& lightDir, VkExtent2D activeExtent, float activeResolutionScale);
    void recordLocalLightShadowPasses(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, const std::vector<engine::render::VoxelRenderResources::VolumeDrawPlan::Entry>& localLightingOccluderVolumes, engine::render::FrameInputs& frameInputs, bool useLocalLightShadows, uint32_t activeLights, uint32_t maxShadowedLocalLights, const glm::mat4& invViewProjUnjittered, VkExtent2D activeExtent);
    void recordAmbientOcclusionPasses(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, const std::vector<engine::render::VoxelRenderResources::VolumeDrawPlan::Entry>& localLightingOccluderVolumes, engine::render::FrameInputs& frameInputs, bool useAmbientOcclusion, const glm::mat4& invViewProjJittered, VkExtent2D activeExtent, float activeResolutionScale);
    void recordPlanarReflectionPasses(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, const std::vector<engine::render::VoxelRenderResources::VolumeDrawPlan::Entry>& staticVolumes, engine::render::FrameInputs& frameInputs, const glm::vec3& reflectedCamPos, const glm::mat4& reflectedView, const glm::mat4& reflectedProj, const glm::mat4& reflectedViewProj, const std::array<glm::mat4, kShadowCascades>& lightViewProjs, const glm::vec4& lightDir, const glm::vec4& lightColor, const glm::vec4& cascadeSplits, const glm::vec4& shadowMapSize, const glm::vec4& caustics0, const glm::vec4& caustics1, bool effectiveVoxelDdaSkipEnabled, float animationTimeSeconds);
    void recordMainLightingPass(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, engine::render::FrameInputs& frameInputs, const LightingPass::FrameUbo& lightingUbo, uint32_t activeLights, bool useDdaShadows, bool useAmbientOcclusion, const glm::vec2& projectionJitterUv);
    void recordVoxelGlassRefractionPass(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, engine::render::FrameInputs& frameInputs, bool shouldRecordVoxelGlassRefraction, const glm::mat4& invViewProjUnjittered, uint32_t activeWaterVolumeCount);
    void recordWaterBodyCompositePass(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, engine::render::FrameInputs& frameInputs, const char* scopeLabel, const glm::mat4& invViewProjJittered, const glm::vec4& sunDirToSun, const glm::vec4& waterTint, const glm::vec4& waterBodyDetail, const glm::vec4& waterBodyShaft, uint32_t activeWaterCount, int waterDebugMode, float animationTimeSeconds);
    void recordWaterGlassSurfacePasses(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, engine::render::FrameInputs& frameInputs, bool drawWater, bool drawGlass, bool shouldRecordPlanarReflection, const WaterPass::FrameUbo& waterUbo, const GlassPass::FrameUbo& glassUbo);
    void recordBloomTaaPasses(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, engine::render::FrameInputs& frameInputs, const engine::scene::WorldStateView& frameWorldState, const glm::vec2& jitter, int postDebugMode);
    void recordTemporalDepthHistoryCopy(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes);
    void recordCompositeUiPass(VkCommandBuffer cmd, const FrameContext& fc, engine::render::RendererPassResources& passes, engine::render::FrameInputs& frameInputs, const engine::scene::WorldStateView& frameWorldState, int compositeMode, const glm::mat4& invViewProjUnjittered, float animationTimeSeconds, uint32_t activeWaterVolumeCount);
    void finishFrameRecording(VkCommandBuffer cmd, uint32_t gpuScopeFrame); void finishFrameAccounting(const FrameShellInputs& shell, const FrameCompletionState& completion); bool bindRendererFrameLifecycle(); bool bindRendererPassLifecycle();
    void recreateSwapchainResources(); void createPassResources(); void destroyPassResources();
    void updateLights(float timeSeconds); void updateCamera(float dt);
    void detectCameraCut(); void updateGameStateHeartbeat(float dt);
    void updateFishFocus(float dt); void updateFishFocusAutomation(); void synchronizePrimaryCreatureBinding(bool bindLiveCreatures = true); bool isCareGameplayActive() const;
    bool toggleRuntimeUiPauseScreen() override;
    const ProceduralFishInstance* findFishById(uint64_t fishId) const;
    glm::vec3 fishWorldPosition(const ProceduralFishInstance& fish) const;
    void loadAssets(); void destroyAssets();
    bool initVolumeScene();
    bool executeVolumeScenePlan(const engine::scene::VolumeSceneEffectPlan& plan);
    bool rebuildVolumeScene();
    bool rebuildProceduralWorld();
    void applyProceduralPresetDefaultsForScene();
    void waitForInFlightFrameWork(const char* reason);
    void waitForRenderQueuesIdle(const char* reason);
    bool canUseDdaShadows() const; bool canUseAmbientOcclusion() const;
    bool canUseLocalLightShadowTracing() const;
    bool canUseLocalShadowBlur() const;
    engine::render::Renderer::CapabilityState rendererCapabilityState() const;
    engine::render::RendererPassResources& renderPasses() { return renderer_.passes(); }
    const engine::render::RendererPassResources& renderPasses() const { return renderer_.passes(); }
    void createVoxelWorld();
    void initializeVoxelWorldMaterials();
    void destroyVoxelWorld();
    void updateVoxelMeshing(const glm::vec3& cameraPos);
    void applyPendingVoxelMeshes(uint32_t frameIndex, VkCommandBuffer cmd);
    void rebuildVoxelObjects(const Frustum& frustum, const glm::vec3& cameraPos);
    void rebuildVolumeBoundsObjects(const Frustum& frustum);
    void rebuildFishbowlGlassMesh();
    bool shouldUseModularVoxelGlass() const;
    bool shouldDrawVoxelVolumeWithDda(uint32_t volumeIndex) const;
    void rebuildModularGlassMeshes();
    void rebuildGlassObjectsForCurrentScene();
    void rebuildStaticObjects();
    void rebuildAnimatedObjects();
    void updateAnimatedObjects(float timeSeconds);
    void rebuildAxolotlRigDebugObjects();
    void appendAxolotlCreaturePolishObjects(float timeSeconds);
    void setProceduralFishVisibility(bool visible);
    void rebuildSceneObjects();
    void buildChunkBoundsMesh();
    void rebuildChunkBoundsObjects();
    bool commitPlaceablePreview();
    bool removePlaceableAtPreview();
    bool undoLastPlaceableEdit();
    bool findPlaceableAtPreview(PlaceableInstance& outInstance) const;
    bool applyPlaceableEditCommand(const PlaceableEditCommand& command, bool undo, bool redo = false); bool completeEditorPlaceableCommand(const PlaceableEditCommand& command, std::string successStatus);
#if VOXEL_WITH_EDITOR
    void drawDebugUI(); bool handleEditorWorldPointerPress(double x, double y, int mods) override;
    void syncEditorStateFromInput();
    void syncInputFromEditorState();
#endif
    void dumpDdaMetricsCapture(float timeSeconds);
    void requestCloudBuildFromSceneConfig(const char* reason); void invalidatePendingCloudBuild(const char* reason); void consumePendingCloudBuild();
    void updateCloudDriftAnimation(double animationNow); void resetCloudRuntimeState(); void updateEnvironmentTime(float deltaSeconds, bool frozen); void resetEnvironmentTimeRuntimeState();
    void dumpAutomationGpuProfileSummary() const; void dumpAutomationDdaMetricsSummary() const; void configureRenderPipelineShowcase(int argc, char** argv); void updateRenderPipelineShowcase(double dtSeconds); void stopRenderPipelineShowcaseFromEditor();
    void onInputFramebufferResized() override;
    void onInputUserInteraction(double timeSeconds) override;
    void setAppMode(AppMode mode, const char* reason);
    void applyAppModeVisibility(const char* reason);
    bool shouldDrawEditorUi() const;
    void refreshInputRouterEditorCapture();
    void updateAutomationUiScript();
    void logRuntimeUiProbe();
#if VOXEL_WITH_RUNTIME_UI
    bool runtimeUiModalInputActive() const;
    bool resetFishFocusForSessionTransition();
    void resetRuntimeUiTransientSessionState(const char* reason);
    ui::UiTree* ensureRuntimeUiActiveTree();
    ui::UiHitResult refreshRuntimeUiPointerCapture(double x, double y) override;
    bool handleRuntimeUiScroll(double x, double y, double yOffset) override;
    void registerRuntimeUiActions(); void registerCareRuntimeUiActions(); void syncRuntimeUiCareHud(ui::UiTree& tree, uint32_t fishCount, const char* timeOfDayLabel) const;
    void syncRuntimeUiOptionsState(ui::UiTree& tree) const;
    void activateRuntimeUiShowcaseCamera(const char* reason);
    void deactivateRuntimeUiShowcaseCamera(const char* reason);
    void syncRuntimeUiShowcaseCameraForActiveScreen(const char* reason);
    void dispatchRuntimeUiAction(const ui::UiActionEvent& event, const char* source,
                                 uint64_t automationFrame = 0);
    bool cancelRuntimeUiPendingPlacement(const char* source,
                                         const char* automationScript = nullptr,
                                         uint64_t automationFrame = 0) override;
    bool rotateRuntimeUiPendingPlacement(const char* source,
                                         const char* automationScript = nullptr,
                                         uint64_t automationFrame = 0) override;
    bool confirmRuntimeUiPendingPlacement(const char* source,
                                          const char* automationScript = nullptr,
                                          uint64_t automationFrame = 0) override;
    void handleRuntimeUiPointerDown(const ui::UiHitResult& hit) override;
    void handleRuntimeUiPointerUp(const ui::UiHitResult& hit) override;
    void configureRuntimeUiSnappedCommitSmokeTarget();
    void configureRuntimeUiRemovalSmokeTarget();
    void configureRuntimeUiMultiFootprintSmokeTarget(bool blocked,
                                                     int rotationSteps = 0);
    bool configureRuntimeUiManualPlacementQaCandidate(
        ui::BuildPlacementProbe& probe,
        const glm::vec3& forward);
    void updateRuntimeUiPlacementProbe(const glm::vec3& forward);
    void buildRuntimeUiDrawList(VkExtent2D extent); void appendRenderPipelineShowcaseOverlay(VkExtent2D extent);
#endif
    void sanitizeWaterParameters();
    void syncWaterContainersFromVolumes();
    void rebuildWaterFoamObjects(float timeSeconds);
    void createPixelInspectResources();
    void destroyPixelInspectResources();
    void configureAutomationAoProjectedDistanceTier(const std::string& radiusPixels,
                                                    const std::string& minimumDistance);
    void configureAutomationFixedSceneTime(const std::string& value);
    void configureAutomationUncapped(bool enabled);
    void configureAutomationRenderQualityPreset(const std::string& value);
    void configureAutomationRenderScale(const std::string& value);
    void configureAutomationAuxiliaryRayScales(const std::string& shadowScale, const std::string& aoScale);
    void updatePixelInspectReadback(uint32_t frameIndex);
    void recordPixelInspectCopy(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t debugMode);
#if VOXEL_WITH_RUNTIME_UI
    void updateRuntimeUiPixelInspectReadback(uint32_t frameIndex);
    void recordRuntimeUiPixelInspectCopy(VkCommandBuffer cmd, const FrameContext& fc);
#endif
    GLFWwindow* window_ = nullptr;
    engine::game::GameRuntime gameRuntime_{};
    engine::input::InputController inputController_{};
    Input& input_ = inputController_.input();
    InputRouter& inputRouter_ = inputController_.router();
#if VOXEL_WITH_EDITOR
    AppMode appMode_ = AppMode::Editor;
    EditorVisibilitySnapshot editorVisibilityBeforePlayPreview_{};
#else
    AppMode appMode_ = AppMode::Game;
#endif
    ui::UiContext runtimeUiContext_{};
    bool framebufferResized_ = false;
    double lastFrameTime_ = 0.0;
    bool swapchainRecreateRequested_ = false;
    VulkanContext ctx_{};
    engine::render::Renderer renderer_{};
#if VOXEL_WITH_EDITOR
    ImGuiLayer imgui_{};
#endif
    GpuProfiler gpuProfiler_{};
#if VOXEL_WITH_RUNTIME_UI
    ui::UiActionDispatcher runtimeUiActionDispatcher_{};
    ui::UiDrawList runtimeUiDrawList_{};
    ui::DebugFontAsset runtimeUiDebugFont_{};
    CpuImage runtimeUiFontAtlasImage_{};
    ui::OverlaySmokeScreenController runtimeUiOverlaySmokeController_{};
    bool runtimeUiFishWorldLabelsVisible_ = true;
    bool runtimeUiOverlaySmokeLogged_ = false;
    bool runtimeUiOverlayTextLogged_ = false;
    bool runtimeUiWorldLabelsLogged_ = false;
    bool runtimeUiMainMenuLogged_ = false;
    bool runtimeUiShowcaseCameraActive_ = false;
    Camera runtimeUiShowcaseSavedCamera_{};
#endif
    std::array<uint32_t, 4> localLightShadowSlots_{0u, 1u, 2u, 3u};
    uint32_t localLightShadowSlotCount_ = 0;
    engine::VoxelMaterialAtlas voxelMaterialAtlas_{};
    engine::VoxelPalette voxelPalette_{};
    engine::VoxelWorld voxelWorld_{};
    // equal-size balls start at the same height and move toward each other.
    engine::physics::Ball physicsSandboxBall_{{-0.5, 2.0, 0.0}, {1.0, 0.0, 0.0}, 0.1, false};
    engine::physics::Ball physicsSandboxBallB_{{0.5, 2.0, 0.0}, {-1.0, 0.0, 0.0}, 0.1, false};
    double physicsSandboxAccumulator_ = 0;
    engine::WaterVolumeManager waterVolumeMgr_{};
    engine::WaterContainerManager waterContainerMgr_{};
    int cloudVolumeIndex_ = -1;       // index of cloud volume in VoxelWorld (-1 = none)
    glm::vec3 cloudBasePosition_{0.0f};  // base position for cloud drift animation
    glm::vec3 cloudWrapRecenterOffset_{0.0f};  // keeps cloud pattern stable when volume recenters
    bool cloudPaletteUploaded_ = false; double environmentTimeElapsedSeconds_ = 0.0; engine::scene::EnvironmentTimeSample environmentTimeSample_{};
    // stars are now rendered procedurally in the composite shader (no voxel volume needed)
    LightsBuffer lights_{};
    MaterialPool materialPool_{};

    PassCreateInfo passCreateInfo_{};
    Camera camera_{};
    glm::mat4 lastInvViewProjUnjittered_{1.0f};
    std::vector<RenderObject> objects_{};
    std::vector<RenderObject> voxelObjects_{};
    std::vector<RenderObject> voxelShadowObjects_{};
    std::vector<RenderObject> chunkBoundsObjects_{};
    std::vector<RenderObject> chunkBoundsVisibleObjects_{};
    std::vector<RenderObject> volumeBoundsVisibleObjects_{};
    std::vector<RenderObject> sceneObjects_{};
    std::vector<RenderObject> shadowObjects_{};
    std::vector<RenderObject> glassObjects_{};
    std::vector<RenderObject> animatedObjects_{};
    std::vector<RenderObject> waterFoamObjects_{};
    std::vector<RenderObject> axolotlRigDebugObjects_{};

    MeshGpu cubeMesh_{};
    MeshGpu assetMesh_{};
    MeshGpu groundMesh_{};
    MeshGpu waterMesh_{};
    MeshGpu fishbowlGlassMesh_{};
    engine::ModularGlassMeshManager modularGlassMeshes_{};
    Texture2D defaultBaseColor_{};
    Texture2D defaultNormal_{};
    Texture2D defaultOrm_{};
    Texture2D assetBaseColor_{};
    Material cubeMaterial_{};
    Material assetMaterial_{};
    Material groundMaterial_{};
    Material waterFoamMaterial_{};
    std::array<Material, kBlockTypeCount> voxelMaterials_{};
    ChunkGrid voxelGrid_{};
    engine::render::VoxelRenderResources voxelRenderResources_{};
    bool sceneObjectsDirty_ = true;
    bool voxelObjectsDirty_ = true;
    bool animatedObjectsDirty_ = true;
    std::vector<ProceduralFishInstance> proceduralFish_{};
    std::vector<HeroFoliageInstance> heroFoliage_{};
    bool proceduralFishPoseValid_ = false;
    float proceduralFishPoseTime_ = 0.0f;
    uint64_t nextFishId_ = 1;
    uint64_t selectedFishId_ = 0;
    float fishFocusCurrentDistance_ = 0.0f;  // camera(base)->fish, drives DoF focus
    struct FishFocusState
    {
        uint64_t fishId = 0;     // fish being focused (or returned from); 0 = inactive
        bool returning = false;  // easing back to the saved free-cam pose
        bool settled = false;    // settle log emitted for this focus
        glm::vec3 framingDirHoriz{0.0f, 0.0f, 1.0f};  // horizontal fish->camera framing dir
        glm::vec3 savedPosition{0.0f};
        float savedYaw = 0.0f;
        float savedPitch = 0.0f;
        // base cinematic pose: the damped tracking operates on this, and the rendered
        // camera pose is base + shake each frame, so shake never feeds back into the
        // damping loop and the release restore stays exact.
        glm::vec3 basePosition{0.0f};
        float baseYaw = 0.0f;
        float basePitch = 0.0f;
        float shakeTime = 0.0f;         // accumulated focus time (dt-driven)
        float timeSinceSettled = 0.0f;  // for the settle-decay envelope
        float timeSinceReturning = 0.0f;
    };
    FishFocusState fishFocus_{};
    float runtimeUiFrameDtSeconds_ = 0.0f;
    uint64_t runtimeUiTweenFrame_ = ~0ull;
    uint64_t runtimeUiLayoutRevision_ = ~0ull;
    uint32_t runtimeUiLayoutComputedCount_ = 0;
    uint32_t runtimeUiLayoutSkippedCount_ = 0;

    // pixel inspector internal state (some exposed publicly)
    glm::vec3 lastInspectVoxelFrac_{0.0f};
    int lastInspectHitAxis_ = 0;
    glm::ivec3 lastInspectVoxel_{0};
    int lastInspectVolumeIndex_ = -1;
    float inspectNormalDelta_ = 0.0f;
    float inspectNdotlDelta_ = 0.0f;
    bool inspectAxisChanged_ = false;
    struct PixelInspectReadback
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        glm::ivec2 pixel{-1, -1};
        uint32_t debugMode = 0;
        bool pending = false;
    };
    std::array<PixelInspectReadback, kMaxFramesInFlight> pixelInspectReadback_{};
#if VOXEL_WITH_RUNTIME_UI
    struct RuntimeUiPixelInspectReadback
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        glm::ivec2 pixel{-1, -1};
        bool pending = false;
    };
    std::array<RuntimeUiPixelInspectReadback, kMaxFramesInFlight> runtimeUiPixelReadback_{};
#endif

    double frozenTimeSeconds_ = 0.0;
    bool freezeTimeWasEnabled_ = false;

    // internal metrics state
    int voxelMetricsSamplePattern_ = 0;
    glm::ivec2 voxelMetricsSampleOffset_{0};
    bool voxelMetricsWriteJson_ = true;
    bool voxelMetricsCapturePending_ = false;
    bool voxelMetricsEnabledPrev_ = false;
    uint64_t voxelMetricsFrameIndex_ = 0;
    engine::DDAMetrics cachedDdaMetrics_{};
    std::vector<DdaMetricsSample> ddaMetricsHistory_{};
    MeshGpu chunkBoundsMesh_{};
    Material chunkBoundsMaterial_{};
    Material volumeBoundsMaterial_{};
    Material voxelCursorMaterial_{};
    Material axolotlRigJointMaterial_{};
    Material axolotlRigBoneMaterial_{};
    Material axolotlRigPawMaterial_{};
    Material placeablePreviewValidMaterial_{};
    Material placeablePreviewInvalidMaterial_{};
    Material runtimeUiPlacementValidMaterial_{};
    Material runtimeUiPlacementInvalidMaterial_{};
    Material runtimeUiRemovalTargetMaterial_{};
    bool voxelCursorVisible_ = false;
    bool axolotlRigDebugVisibleLast_ = false;
    glm::ivec3 voxelCursorVoxel_{0};
    PlaceablePreview placeablePreview_{};
    bool placeablePreviewEnabled_ = true;
    int selectedFoliagePreviewPrototype_ = 0;
    uint32_t voxelRebuildsSinceTitle_ = 0;
    uint32_t worldSeed_ = 1337;
    uint32_t worldTrees_ = 0;
    uint32_t worldFoliage_ = 0;

    // procedural world settings (internal)
    engine::ProceduralWorldSettings proceduralWorldSettings_{};

    // voxel import settings (internal)
    int voxelImportResolution_ = 64;
    bool voxelImportSplitChunks_ = false;
    bool voxelImportDirty_ = false;
    double voxelImportVoxelizeMs_ = 0.0;
    uint32_t voxelImportTriangleCount_ = 0;
    uint32_t voxelImportFilledCount_ = 0;
    std::filesystem::path voxelImportMeshPath_{};

    // internal taa state
    uint32_t taaFrameIndex_ = 0;
    glm::vec2 currentJitter_{0.0f};
    glm::vec2 prevJitter_{0.0f};
    float taaDebugDisplayTimer_ = 0.0f;
    uint32_t taaDebugSeqIndex_ = 0;
    glm::vec2 taaDebugJitter_{0.0f};
    uint32_t taaDebugFrameCount_ = 0;
    glm::mat4 prevViewProjUnjittered_{1.0f};
    glm::mat4 prevViewProjJittered_{1.0f};
    glm::vec3 prevCameraPosition_{0.0f};
    glm::vec3 prevCameraForward_{0.0f, 0.0f, -1.0f};
    glm::vec3 lastVisibilityBuildCameraPos_{0.0f};
    glm::vec3 lastVisibilityBuildCameraForward_{0.0f, 0.0f, -1.0f};
    bool visibilityBuildStateValid_ = false;
    float cameraCutThreshold_ = 2.0f;
    SwapPresentMode presentMode_ = SwapPresentMode::Fifo;
    bool aoNeutralClearPending_ = true;
    bool localShadowNeutralClearPending_ = true;
    float lastCpuFrameMs_ = 0.0f;
    float lastGpuFrameMs_ = 0.0f;
    float pendingCpuFrameMs_ = 0.0f;
    float effectiveFpsLimit_ = 60.0f;
    bool pendingProfilerSample_ = false;
    bool windowFocused_ = true;
    bool focusedIdleThrottleActive_ = false;
    bool backgroundThrottleActive_ = false;
    std::vector<float> cpuFrameHistoryMs_{};
    std::vector<float> gpuFrameHistoryMs_{};
#if VOXEL_WITH_EDITOR
    EditorState editorState_{};
#endif
    JobSystem jobSystem_{};
    MeshUploadQueue meshUploadQueue_{};
    std::filesystem::path assetRoot_{};
    engine::scene::SceneManager sceneManager_{};
    engine::editor::SceneDocument editorSceneDocument_{};
    double lastInteractionTime_ = 0.0;
    bool waterParametersDirty_ = true;
    int automationAutoExitMs_ = 0;
    uint64_t automationAutoExitFrames_ = 0;
    bool automationSkipLastUsedSave_ = false;
    int automationPixelInspectMode_ = 0;
    bool automationPixelInspectValidated_ = false;
    glm::ivec2 automationPixelInspectPoint_{-1, -1};
    bool automationWaitForCloudCommit_ = false;
    bool automationCloudCommitObserved_ = false;
    bool automationLogGpuProfile_ = false;
    bool automationProfileWaitForSceneReady_ = false;
    bool automationLogDdaMetrics_ = false;
    bool automationDisableEmptySkip_ = false;
    bool automationDisableAquariumWater_ = false;
    bool automationDisableProceduralFish_ = false; float automationAoProjectedRadiusPixels_ = 0.0f, automationAoProjectedMinDistance_ = 2.0f;
    int automationInitialViewMode_ = -1;
    int automationInitialDdaAdvancedDebug_ = -1;
    bool automationHideUi_ = false;
    std::string automationUiScript_{};
    bool automationUiProbeLogged_ = false;
    std::vector<AutomationUiInputEvent> automationUiInputQueue_{};
    size_t automationUiInputCursor_ = 0;
    bool automationUiScriptCompleted_ = false;
#if VOXEL_WITH_RUNTIME_UI
    bool automationRuntimeUiPixelInspect_ = false;
    bool automationRuntimeUiPixelInspectValidated_ = false;
    glm::ivec2 automationRuntimeUiPixelInspectPoint_{80, 80};
#endif
    engine::performance::AutomationProfileSession automationProfileSession_{};
    engine::render::RenderPipelineShowcase renderPipelineShowcase_{}; bool renderPipelineShowcaseStartedFromEditor_ = false;
    double automationLoopStartTimeSeconds_ = 0.0;
    uint64_t renderedFrameCount_ = 0;
    const char* argv0_ = nullptr;
};
