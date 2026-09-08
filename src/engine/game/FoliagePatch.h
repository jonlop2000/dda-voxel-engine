#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "engine/game/FoliageArchetype.h"

namespace engine::game
{

struct FoliagePatchInstance
{
    uint64_t stableId = 0;
    glm::vec3 rootWorld{0.0f};
    float radiusMeters = 0.0f;
    float heightMeters = 0.0f;
    float cellSizeMeters = 0.1f;
    float yawRadians = 0.0f;
    float phaseRadians = 0.0f;
    uint32_t randomSeed = 0;
    uint32_t primaryMaterialId = 0;
    uint32_t secondaryMaterialId = 0;
    uint32_t accentMaterialId = 0;
    size_t sourcePlantOffset = 0;
    uint32_t sourcePlantCount = 0;
    FoliagePatchKind kind = FoliagePatchKind::GrassTuft;
    FoliageMorphology morphology = FoliageMorphology::GrassTuft;
};

struct FoliagePatchLayout
{
    size_t sourcePlantCount = 0;
    bool valid = true;
    // stable-ID-sorted members remain explicit so morphology expansion can
    // preserve every authored terrain anchor instead of collapsing a patch to
    // one oversized center silhouette.
    std::vector<FoliageBladeInstance> sourcePlants;
    std::vector<FoliagePatchInstance> patches;
    std::array<size_t, static_cast<size_t>(FoliagePatchKind::Count)> kindCounts{};
};

[[nodiscard]] std::string_view foliagePatchKindName(FoliagePatchKind kind);
[[nodiscard]] float foliagePatchResolvedMaxTipDisplacementMeters(
    const FoliagePatchInstance& patch);

// cpu reference mirrored by foliage.vert for the active patch renderer.
// normalizedHeight=0 is an exact anchor; authored rest shape stays in geometry.
[[nodiscard]] glm::vec3 foliagePatchDisplacement(
    const FoliagePatchInstance& patch, float normalizedHeight, float timeSeconds,
    const engine::scene::EnvironmentWindSettings& wind =
        engine::scene::defaultEnvironmentWindSettings(),
    float swayStrength = 1.0f);
[[nodiscard]] float foliagePatchConservativeDisplacement(
    const FoliagePatchInstance& patch,
    const engine::scene::EnvironmentWindSettings& wind =
        engine::scene::defaultEnvironmentWindSettings(),
    float swayStrength = 1.0f);

// deterministically groups authored plants by their stable ecological patch.
// placement owns patch center, species, palette, and seed; this layer never
// re-bins or reclassifies them. stable plant ids are required to be unique,
// members are retained in stable-ID order, and inconsistent shared patch
// metadata fails closed.
[[nodiscard]] FoliagePatchLayout buildFoliagePatchLayout(
    std::span<const FoliageBladeInstance> plants);

} // namespace engine::game
