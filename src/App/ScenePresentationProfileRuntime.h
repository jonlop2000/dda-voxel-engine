#pragma once

#include <cstdint>
#include <string_view>

#include "engine/scene/ScenePresentationProfileVariants.h"

class Input;
struct SceneConfig;

namespace engine::render
{
struct AmbientOcclusionSettings;
struct GlassSettings;
struct LightingSettings;
struct PostFxSettings;
struct ShadowSettings;
struct VoxelDebugSettings;
struct WaterSettings;
} // namespace engine::render

namespace app
{

// keeps the input controller as a control surface while the render-settings
// buckets remain the authoritative values consumed by the renderer.
void syncPresentationSettingsFromInput(
    Input& input, engine::render::LightingSettings& lighting,
    engine::render::PostFxSettings& postFx, uint32_t maxLights);

void syncPresentationInputFromSettings(
    Input& input, engine::render::LightingSettings& lighting,
    engine::render::PostFxSettings& postFx, uint32_t maxLights);

// shared startup/reload bridge. both app load paths call this exact function so
// profile resolution, application, and input synchronization cannot drift.
engine::scene::ScenePresentationRuntimeState resolveApplyAndSyncScenePresentationProfile(
    const SceneConfig& sceneConfig, Input& input, int& skyPreset, glm::vec3& skyColor,
    engine::render::LightingSettings& lighting,
    engine::render::ShadowSettings& shadows,
    engine::render::AmbientOcclusionSettings& ambientOcclusion,
    engine::render::PostFxSettings& postFx,
    engine::render::VoxelDebugSettings& voxel,
    engine::render::WaterSettings& water,
    engine::render::GlassSettings& glass, uint32_t maxLights);

engine::scene::ScenePresentationProfileReadout inspectScenePresentationProfileRuntimeState(
    const engine::scene::ScenePresentationRuntimeState& runtimeState,
    int skyPreset, const glm::vec3& skyColor,
    const engine::render::LightingSettings& lighting,
    const engine::render::ShadowSettings& shadows,
    const engine::render::AmbientOcclusionSettings& ambientOcclusion,
    const engine::render::PostFxSettings& postFx,
    const engine::render::VoxelDebugSettings& voxel,
    const engine::render::WaterSettings& water,
    const engine::render::GlassSettings& glass);

bool applyAutomationSoftProfileV0(
    std::string_view modeName, Input& input, int& skyPreset, glm::vec3& skyColor,
    engine::render::LightingSettings& lighting,
    engine::render::ShadowSettings& shadows,
    engine::render::AmbientOcclusionSettings& ambientOcclusion,
    engine::render::PostFxSettings& postFx,
    engine::render::VoxelDebugSettings& voxel,
    engine::render::WaterSettings& water,
    engine::render::GlassSettings& glass, uint32_t maxLights);

bool applyAutomationVoxelCellVariationPreset(
    std::string_view presetName, engine::render::VoxelDebugSettings& voxel);

bool applyAutomationHemisphereAmbientPreset(
    std::string_view presetName, engine::render::LightingSettings& lighting);

bool applyAutomationSceneAtmospherePreset(
    std::string_view presetName, engine::render::LightingSettings& lighting);

bool applyAutomationLightingDebugMode(
    std::string_view modeName, engine::render::LightingSettings& lighting);

} // namespace app
