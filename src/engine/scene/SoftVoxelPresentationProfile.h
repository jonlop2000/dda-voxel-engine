#pragma once

#include <optional>
#include <string_view>

#include "engine/scene/ScenePresentationProfile.h"

namespace engine::scene
{

enum class ReconstructionComparisonMode
{
    NoAa,
    Fxaa,
    Taa,
    TaaFxaa
};

std::optional<ReconstructionComparisonMode>
reconstructionComparisonModeFromName(std::string_view name);

std::string_view
reconstructionComparisonModeName(ReconstructionComparisonMode mode);

// produces the provisional outdoor soft profile v0 while preserving content-
// specific lighting, water, and glass values from the resolved scene profile.
// the four modes differ only in their reconstruction switches.
ScenePresentationProfile makeSoftOutdoorProfileV0(
    ScenePresentationProfile resolvedProfile,
    ReconstructionComparisonMode mode);

// produces the historical accepted soft profile v1 candidate: taa-only v0 reconstruction
// plus the accepted cell-variation, hemisphere-ambient, and atmosphere values.
// per-volume foliage occlusion stays scene-content owned; its profile-owned
// global shadow opacity is already part of the v0 tuning above.
ScenePresentationProfile makeSoftOutdoorProfileV1(
    ScenePresentationProfile resolvedProfile);

// builds on the accepted v1 atmosphere/color contract with an engine-wide,
// edge-local reconstruction and gentler voxel surface response. geometry stays
// square; only its presentation becomes softer.
ScenePresentationProfile makeSoftOutdoorProfileV2(
    ScenePresentationProfile resolvedProfile);

// builds on v2 geometry/reconstruction with an engine-wide, matte painted
// response and deliberately broad, stable palette families.
ScenePresentationProfile makePaintedOutdoorProfileV3(
    ScenePresentationProfile resolvedProfile);

// adds the first bounded sky-presentation slice to the accepted painted voxel
// look. it stays texture-free and preserves scene-authored sky/sun identity.
ScenePresentationProfile makePaintedOutdoorSkyProfileV1(
    ScenePresentationProfile resolvedProfile);

// layers the first fixed-budget production cloud sheet over sky-001. motion is
// not authored here: runtime drift comes exclusively from scene wind-001.
ScenePresentationProfile makePaintedOutdoorCloudProfileV1(
    ScenePresentationProfile resolvedProfile);

} // namespace engine::scene
