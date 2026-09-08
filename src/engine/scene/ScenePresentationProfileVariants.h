#pragma once

#include <string>
#include <vector>

#include "engine/scene/ScenePresentationProfile.h"

struct SceneConfig;

namespace engine::scene
{

// builds the named base-profile layer for a scene: the profile-v0 state the app
// scene-name default helpers are expected to produce for that scene from a fresh
// process. classified scenes get a distinct variant (aquarium-direct,
// aquarium-sunroof, aquarium-fishbowl, beach-sand, nature-pond); everything else
// gets the config-dependent "scene-base" variant. variants cover all seven
// profile-v0 buckets; authored content and runtime/debug state stay owned elsewhere.
ScenePresentationProfile namedBaseScenePresentationProfile(const SceneConfig& sceneConfig);

// field-by-field difference report between two profiles across the seven value
// buckets (name/version metadata is not compared). empty means identical.
std::vector<std::string> describeScenePresentationProfileDelta(
    const ScenePresentationProfile& expected, const ScenePresentationProfile& actual);

struct SceneProfileResolution
{
    bool serializedApplied = false;
    ScenePresentationProfileSource source = ScenePresentationProfileSource::SchemaDefaults;
    // name of the named or serialized profile that was applied.
    std::string appliedName;
    // variant name of the named base layer for this scene.
    std::string namedBaseName;
    // pre-apply live-vs-named-base field delta, computed only when no serialized
    // profile was applied. this preserves migration evidence while the named base
    // is now the authoritative writer.
    std::vector<std::string> namedBaseParityDelta;
};

// retained by SceneManager after each startup/reload application. the applied
// snapshot is the baseline used to distinguish deliberate live edits from the
// source that originally resolved the scene.
struct ScenePresentationRuntimeState
{
    SceneProfileResolution resolution{};
    ScenePresentationProfile appliedProfile{};
};

struct ScenePresentationProfileReadout
{
    std::string appliedName;
    std::string namedBaseName;
    ScenePresentationProfileSource resolvedSource =
        ScenePresentationProfileSource::SchemaDefaults;
    ScenePresentationProfileSource effectiveSource =
        ScenePresentationProfileSource::SchemaDefaults;
    std::vector<std::string> liveEditDelta;
};

// one-call scene profile resolution for the app load path: builds the
// schema/named/serialized layers and always applies the resolved profile. when
// there is no serialized profile it also reports the pre-apply parity delta
// between the named base and the legacy helper-produced state.
SceneProfileResolution resolveAndApplySceneProfile(
    const SceneConfig& sceneConfig, int& skyPreset, glm::vec3& skyColor,
    engine::render::LightingSettings& lighting, engine::render::ShadowSettings& shadows,
    engine::render::AmbientOcclusionSettings& ambientOcclusion,
    engine::render::PostFxSettings& postFx, engine::render::VoxelDebugSettings& voxel,
    engine::render::WaterSettings& water, engine::render::GlassSettings& glass);

} // namespace engine::scene
