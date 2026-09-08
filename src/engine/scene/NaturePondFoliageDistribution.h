#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "engine/game/FoliageArchetype.h"

namespace engine::scene
{

inline constexpr std::string_view kNaturePondFoliageDistributionId =
    "nature-pond-ecology-v2-floral-life";
inline constexpr std::string_view kNaturePondMeadowCarpetDistributionId =
    "nature-pond-meadow-carpet-v1";
inline constexpr std::string_view kNaturePondWaterFloraDistributionId =
    "nature-pond-water-flora-v1";

// renderer-independent placement selected from the nature pond's canonical
// 0.30 m candidate lattice. patch anchors are expressed in that same lattice.
struct NaturePondFoliagePlacement
{
    engine::game::FoliagePatchKind kind =
        engine::game::FoliagePatchKind::GrassTuft;
    uint64_t stablePatchId = 0;
    int patchAnchorCandidateX = 0;
    int patchAnchorCandidateZ = 0;
    int patchRadiusCandidateX = 1;
    int patchRadiusCandidateZ = 1;
    int plantCandidateX = 0;
    int plantCandidateZ = 0;
    uint32_t patchSeed = 0;
    uint32_t plantSeed = 0;

    bool operator==(const NaturePondFoliagePlacement&) const = default;
};

// pure and stateless: a candidate's result does not depend on iteration order or
// generation bounds. scatterDensityPermille=1000 is the authored baseline; larger
// values can only add placements and never alter an existing placement.
[[nodiscard]] std::optional<NaturePondFoliagePlacement>
sampleNaturePondFoliageDistribution(uint32_t worldSeed, int candidateX,
                                    int candidateZ,
                                    uint32_t scatterDensityPermille = 1000u) noexcept;

// dense low-cover companion to the accent sampler. it uses the same canonical
// 0.30 m lattice and stable land-patch identity, but fills the active patch
// footprint rather than only its species ellipse. inactive patches, the pond
// and shore regions, a scene-authored access corridor, and deterministic small
// openings remain clear. increasing density is additive.
[[nodiscard]] std::optional<NaturePondFoliagePlacement>
sampleNaturePondMeadowCarpetDistribution(
    uint32_t worldSeed, int candidateX, int candidateZ,
    uint32_t scatterDensityPermille = 1000u) noexcept;

// sparse floating colonies inside the pond. the center remains open for a
// readable water/fish view and the outer water ring remains open so lilies do
// not collide with the authored reed shore. increasing density is additive.
[[nodiscard]] std::optional<NaturePondFoliagePlacement>
sampleNaturePondWaterFloraDistribution(
    uint32_t worldSeed, int candidateX, int candidateZ,
    uint32_t scatterDensityPermille = 1000u) noexcept;

} // namespace engine::scene
