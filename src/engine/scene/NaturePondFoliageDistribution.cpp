#include "engine/scene/NaturePondFoliageDistribution.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace engine::scene
{
namespace
{
constexpr int kMaximumSupportedCandidateMagnitude = 512;

constexpr int kLandPatchStride = 9;
constexpr int kReedPatchStride = 7;
constexpr int kWaterPatchStride = 7;

constexpr uint32_t kLandPatchActivationPermille = 900u;
constexpr uint32_t kReedPatchActivationPermille = 850u;
constexpr uint32_t kWaterPatchActivationPermille = 650u;
constexpr uint32_t kMaximumThresholdPermille = 970u;

constexpr uint32_t kPatchSeedSalt = 0x243f6a88u;
constexpr uint32_t kPatchShapeSalt = 0x9e3779b9u;
constexpr uint32_t kPatchKindSalt = 0xb7e15162u;
constexpr uint32_t kPatchActivationSalt = 0x6d2b79f5u;
constexpr uint32_t kPlantSeedSalt = 0x85ebca6bu;
constexpr uint32_t kPlantAcceptanceSalt = 0xc2b2ae35u;
constexpr uint32_t kCarpetSeedSalt = 0x165667b1u;
constexpr uint32_t kCarpetAcceptanceSalt = 0xd3a2646cu;
constexpr uint32_t kCarpetAcceptancePermille = 840u;
constexpr uint32_t kWaterPatchSeedSalt = 0x4f1bbcdcu;
constexpr uint32_t kWaterPlantSeedSalt = 0xa8f3c2d1u;

// V2 keeps the same bounded patch lattice and acceptance rules while making
// flowers a first-class part of the meadow read. roughly two in five active
// land patches become flower colonies, with shrubs kept deliberately sparse.
constexpr uint32_t kFlowerPatchStartPermille = 460u;
constexpr uint32_t kShrubPatchStartPermille = 860u;

enum class FoliageRegion
{
    Excluded,
    ReedShore,
    Meadow,
};

struct Patch
{
    engine::game::FoliagePatchKind kind =
        engine::game::FoliagePatchKind::GrassTuft;
    uint64_t stableId = 0;
    int anchorX = 0;
    int anchorZ = 0;
    int radiusX = 1;
    int radiusZ = 1;
    uint32_t seed = 0;
    uint32_t shapeSeed = 0;
};

[[nodiscard]] uint32_t mixSeed(uint32_t value) noexcept
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

[[nodiscard]] uint32_t spatialHash(int x, int y, int z,
                                   uint32_t seed) noexcept
{
    return mixSeed(static_cast<uint32_t>(x) * 73856093u ^
                   static_cast<uint32_t>(y) * 19349663u ^
                   static_cast<uint32_t>(z) * 83492791u ^ seed);
}

[[nodiscard]] int floorDivide(int value, int divisor) noexcept
{
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] uint32_t scaledThreshold(uint32_t baselinePermille,
                                       uint32_t densityPermille) noexcept
{
    const uint64_t scaled =
        static_cast<uint64_t>(baselinePermille) * densityPermille / 1000u;
    return static_cast<uint32_t>(
        std::min<uint64_t>(scaled, kMaximumThresholdPermille));
}

[[nodiscard]] uint64_t stablePatchId(uint32_t worldSeed, int cellX,
                                     int cellZ, bool reed) noexcept
{
    const uint32_t packedCoordinates =
        (reed ? 0x80000000u : 0u) |
        ((static_cast<uint32_t>(cellX) & 0x7fffu) << 16u) |
        (static_cast<uint32_t>(cellZ) & 0xffffu);
    return (static_cast<uint64_t>(worldSeed) << 32u) | packedCoordinates;
}

[[nodiscard]] uint64_t stableWaterPatchId(uint32_t worldSeed, int cellX,
                                          int cellZ) noexcept
{
    constexpr uint32_t kWaterPatchNamespace = 0x40000000u;
    const uint32_t packedCoordinates =
        kWaterPatchNamespace |
        ((static_cast<uint32_t>(cellX) & 0x3fffu) << 16u) |
        (static_cast<uint32_t>(cellZ) & 0xffffu);
    return (static_cast<uint64_t>(worldSeed) << 32u) | packedCoordinates;
}

[[nodiscard]] bool distanceAtLeast(int64_t distanceNumerator,
                                   int thresholdHundredths) noexcept
{
    constexpr int64_t kPondDenominator =
        int64_t{650} * 650 * int64_t{500} * 500;
    return distanceNumerator * 10000 >=
           kPondDenominator * thresholdHundredths * thresholdHundredths;
}

[[nodiscard]] FoliageRegion foliageRegion(int candidateX,
                                          int candidateZ) noexcept
{
    if (candidateX < -kMaximumSupportedCandidateMagnitude ||
        candidateX > kMaximumSupportedCandidateMagnitude ||
        candidateZ < -kMaximumSupportedCandidateMagnitude ||
        candidateZ > kMaximumSupportedCandidateMagnitude)
    {
        return FoliageRegion::Excluded;
    }

    // exact centimeter form of the existing canonical candidate mapping:
    // (-12.0 m, -10.0 m) + 0.05 m + candidate * 0.30 m.
    const int64_t worldXCentimeters = -1195 + int64_t{30} * candidateX;
    const int64_t worldZCentimeters = -995 + int64_t{30} * candidateZ;
    const int64_t distanceNumerator =
        worldXCentimeters * worldXCentimeters * 500 * 500 +
        worldZCentimeters * worldZCentimeters * 650 * 650;

    const bool outsideWater = distanceAtLeast(distanceNumerator, 91);
    const bool outsideReedShore = distanceAtLeast(distanceNumerator, 111);
    if (outsideWater && !outsideReedShore)
    {
        return FoliageRegion::ReedShore;
    }
    if (distanceAtLeast(distanceNumerator, 114))
    {
        return FoliageRegion::Meadow;
    }
    return FoliageRegion::Excluded;
}

[[nodiscard]] bool waterFloraRegion(int candidateX, int candidateZ) noexcept
{
    if (candidateX < -kMaximumSupportedCandidateMagnitude ||
        candidateX > kMaximumSupportedCandidateMagnitude ||
        candidateZ < -kMaximumSupportedCandidateMagnitude ||
        candidateZ > kMaximumSupportedCandidateMagnitude)
    {
        return false;
    }

    const int64_t worldXCentimeters = -1195 + int64_t{30} * candidateX;
    const int64_t worldZCentimeters = -995 + int64_t{30} * candidateZ;
    const int64_t distanceNumerator =
        worldXCentimeters * worldXCentimeters * 500 * 500 +
        worldZCentimeters * worldZCentimeters * 650 * 650;
    // keep a broad central window clear for the fish/readable water and stop
    // well before the 0.91 shoreline boundary used by reeds.
    return distanceAtLeast(distanceNumerator, 27) &&
           !distanceAtLeast(distanceNumerator, 79);
}

[[nodiscard]] int jitter(uint32_t seed, uint32_t shift) noexcept
{
    return static_cast<int>((seed >> shift) % 3u) - 1;
}

[[nodiscard]] Patch landPatch(uint32_t worldSeed, int candidateX,
                              int candidateZ) noexcept
{
    const int cellX = floorDivide(candidateX, kLandPatchStride);
    const int cellZ = floorDivide(candidateZ, kLandPatchStride);
    const uint32_t patchSeed = spatialHash(
        cellX, 61, cellZ, worldSeed + kPatchSeedSalt);
    const uint32_t shapeSeed = mixSeed(patchSeed ^ kPatchShapeSalt);
    const uint32_t kindRoll =
        mixSeed(patchSeed ^ kPatchKindSalt) % 1000u;

    engine::game::FoliagePatchKind kind =
        engine::game::FoliagePatchKind::GrassTuft;
    if (kindRoll >= kShrubPatchStartPermille)
    {
        kind = engine::game::FoliagePatchKind::Shrub;
    }
    else if (kindRoll >= kFlowerPatchStartPermille)
    {
        kind = engine::game::FoliagePatchKind::FlowerCluster;
    }

    Patch patch{};
    patch.kind = kind;
    patch.stableId = stablePatchId(worldSeed, cellX, cellZ, false);
    patch.anchorX = cellX * kLandPatchStride + kLandPatchStride / 2 +
                    jitter(shapeSeed, 0u);
    patch.anchorZ = cellZ * kLandPatchStride + kLandPatchStride / 2 +
                    jitter(shapeSeed, 8u);
    patch.radiusX = 2 + static_cast<int>((shapeSeed >> 16u) & 1u);
    patch.radiusZ = 2 + static_cast<int>((shapeSeed >> 17u) & 1u);
    patch.seed = patchSeed;
    patch.shapeSeed = shapeSeed;
    return patch;
}

[[nodiscard]] Patch reedPatch(uint32_t worldSeed, int candidateX,
                              int candidateZ) noexcept
{
    const int cellX = floorDivide(candidateX, kReedPatchStride);
    const int cellZ = floorDivide(candidateZ, kReedPatchStride);
    const uint32_t patchSeed = spatialHash(
        cellX, 83, cellZ, worldSeed + kPatchSeedSalt);
    const uint32_t shapeSeed = mixSeed(patchSeed ^ kPatchShapeSalt);

    Patch patch{};
    patch.kind = engine::game::FoliagePatchKind::ReedCluster;
    patch.stableId = stablePatchId(worldSeed, cellX, cellZ, true);
    patch.anchorX = cellX * kReedPatchStride + kReedPatchStride / 2 +
                    jitter(shapeSeed, 0u);
    patch.anchorZ = cellZ * kReedPatchStride + kReedPatchStride / 2 +
                    jitter(shapeSeed, 8u);
    patch.radiusX = 1 + static_cast<int>((shapeSeed >> 16u) & 1u);
    patch.radiusZ = 1 + static_cast<int>((shapeSeed >> 17u) & 1u);
    patch.seed = patchSeed;
    patch.shapeSeed = shapeSeed;
    return patch;
}

[[nodiscard]] Patch waterPatch(uint32_t worldSeed, int candidateX,
                               int candidateZ) noexcept
{
    const int cellX = floorDivide(candidateX, kWaterPatchStride);
    const int cellZ = floorDivide(candidateZ, kWaterPatchStride);
    const uint32_t patchSeed = spatialHash(
        cellX, 149, cellZ, worldSeed + kWaterPatchSeedSalt);
    const uint32_t shapeSeed = mixSeed(patchSeed ^ kPatchShapeSalt);

    Patch patch{};
    patch.kind = engine::game::FoliagePatchKind::WaterLilyCluster;
    patch.stableId = stableWaterPatchId(worldSeed, cellX, cellZ);
    patch.anchorX = cellX * kWaterPatchStride + kWaterPatchStride / 2 +
                    jitter(shapeSeed, 0u);
    patch.anchorZ = cellZ * kWaterPatchStride + kWaterPatchStride / 2 +
                    jitter(shapeSeed, 8u);
    patch.radiusX = 1 + static_cast<int>((shapeSeed >> 16u) & 1u);
    patch.radiusZ = 1 + static_cast<int>((shapeSeed >> 17u) & 1u);
    patch.seed = patchSeed;
    patch.shapeSeed = shapeSeed;
    return patch;
}

[[nodiscard]] bool patchActive(const Patch& patch, uint32_t worldSeed,
                               uint32_t densityPermille) noexcept
{
    const uint32_t baseline =
        patch.kind == engine::game::FoliagePatchKind::ReedCluster
            ? kReedPatchActivationPermille
        : patch.kind == engine::game::FoliagePatchKind::WaterLilyCluster
            ? kWaterPatchActivationPermille
            : kLandPatchActivationPermille;
    const uint32_t roll =
        mixSeed(patch.seed ^ kPatchActivationSalt ^ worldSeed) % 1000u;
    return roll < scaledThreshold(baseline, densityPermille);
}

[[nodiscard]] std::array<int, 2> rotateOffset(std::array<int, 2> offset,
                                              uint32_t rotations) noexcept
{
    for (uint32_t rotation = 0; rotation < (rotations & 3u); ++rotation)
    {
        offset = {-offset[1], offset[0]};
    }
    return offset;
}

[[nodiscard]] bool shrubAnchorAccepted(const Patch& patch, int candidateX,
                                       int candidateZ) noexcept
{
    constexpr std::array<std::array<int, 2>, 4> kOffsets = {
        std::array<int, 2>{0, 0}, std::array<int, 2>{2, 1},
        std::array<int, 2>{-1, 2}, std::array<int, 2>{1, -2}};
    const size_t count = 3u + ((patch.shapeSeed >> 18u) & 1u);
    const uint32_t rotations = (patch.shapeSeed >> 20u) & 3u;
    for (size_t index = 0; index < count; ++index)
    {
        const std::array<int, 2> offset =
            rotateOffset(kOffsets[index], rotations);
        if (candidateX == patch.anchorX + offset[0] &&
            candidateZ == patch.anchorZ + offset[1])
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ellipseAccepted(const Patch& patch, int candidateX,
                                   int candidateZ, uint32_t plantSeed,
                                   uint32_t densityPermille) noexcept
{
    const int64_t dx = static_cast<int64_t>(candidateX) - patch.anchorX;
    const int64_t dz = static_cast<int64_t>(candidateZ) - patch.anchorZ;
    const int64_t radiusXSquared = patch.radiusX * patch.radiusX;
    const int64_t radiusZSquared = patch.radiusZ * patch.radiusZ;
    const int64_t metric = dx * dx * radiusZSquared +
                           dz * dz * radiusXSquared;
    const int64_t limit = radiusXSquared * radiusZSquared;
    if (metric > limit)
    {
        return false;
    }

    uint32_t core = 900u;
    uint32_t falloff = 220u;
    if (patch.kind == engine::game::FoliagePatchKind::FlowerCluster)
    {
        core = 820u;
        falloff = 260u;
    }
    else if (patch.kind == engine::game::FoliagePatchKind::ReedCluster)
    {
        core = 920u;
        falloff = 220u;
    }
    else if (patch.kind == engine::game::FoliagePatchKind::WaterLilyCluster)
    {
        core = 760u;
        falloff = 300u;
    }
    const uint32_t radialFalloff = static_cast<uint32_t>(
        static_cast<uint64_t>(falloff) * metric / limit);
    const uint32_t threshold =
        scaledThreshold(core - radialFalloff, densityPermille);
    return mixSeed(plantSeed ^ kPlantAcceptanceSalt) % 1000u < threshold;
}

[[nodiscard]] bool meadowAccessCorridorExcluded(int candidateX,
                                                int candidateZ) noexcept
{
    // preserve a narrow, gently diagonal approach from the south-east meadow
    // to the pond bank. integer centimeter math keeps this stable across hosts.
    const int worldXCentimeters = -1195 + 30 * candidateX;
    const int worldZCentimeters = -995 + 30 * candidateZ;
    if (worldZCentimeters < 430)
    {
        return false;
    }
    const int pathCenterXCentimeters =
        330 + (worldZCentimeters - 430) * 45 / 100;
    return std::abs(worldXCentimeters - pathCenterXCentimeters) <= 48;
}

[[nodiscard]] bool carpetCandidateAccepted(const Patch& patch,
                                            int candidateX,
                                            int candidateZ,
                                            uint32_t plantSeed,
                                            uint32_t densityPermille) noexcept
{
    if (meadowAccessCorridorExcluded(candidateX, candidateZ))
    {
        return false;
    }
    const uint32_t threshold =
        scaledThreshold(kCarpetAcceptancePermille, densityPermille);
    return mixSeed(plantSeed ^ patch.shapeSeed ^ kCarpetAcceptanceSalt) %
               1000u <
           threshold;
}

} // namespace

std::optional<NaturePondFoliagePlacement>
sampleNaturePondFoliageDistribution(uint32_t worldSeed, int candidateX,
                                    int candidateZ,
                                    uint32_t scatterDensityPermille) noexcept
{
    const FoliageRegion region = foliageRegion(candidateX, candidateZ);
    if (region == FoliageRegion::Excluded || scatterDensityPermille == 0u)
    {
        return std::nullopt;
    }

    const Patch patch = region == FoliageRegion::ReedShore
                            ? reedPatch(worldSeed, candidateX, candidateZ)
                            : landPatch(worldSeed, candidateX, candidateZ);
    if (foliageRegion(patch.anchorX, patch.anchorZ) != region ||
        !patchActive(patch, worldSeed, scatterDensityPermille))
    {
        return std::nullopt;
    }

    const uint32_t plantSeed = spatialHash(
        candidateX, 101, candidateZ, worldSeed + kPlantSeedSalt);
    const bool accepted = patch.kind == engine::game::FoliagePatchKind::Shrub
                              ? shrubAnchorAccepted(patch, candidateX, candidateZ)
                              : ellipseAccepted(patch, candidateX, candidateZ,
                                                plantSeed,
                                                scatterDensityPermille);
    if (!accepted)
    {
        return std::nullopt;
    }

    return NaturePondFoliagePlacement{
        patch.kind, patch.stableId, patch.anchorX, patch.anchorZ,
        patch.radiusX, patch.radiusZ, candidateX, candidateZ,
        patch.seed, plantSeed};
}

std::optional<NaturePondFoliagePlacement>
sampleNaturePondMeadowCarpetDistribution(
    uint32_t worldSeed, int candidateX, int candidateZ,
    uint32_t scatterDensityPermille) noexcept
{
    if (foliageRegion(candidateX, candidateZ) != FoliageRegion::Meadow ||
        scatterDensityPermille == 0u)
    {
        return std::nullopt;
    }

    const Patch patch = landPatch(worldSeed, candidateX, candidateZ);
    if (foliageRegion(patch.anchorX, patch.anchorZ) !=
            FoliageRegion::Meadow ||
        !patchActive(patch, worldSeed, scatterDensityPermille))
    {
        return std::nullopt;
    }

    const uint32_t plantSeed = spatialHash(
        candidateX, 131, candidateZ, worldSeed + kCarpetSeedSalt);
    if (!carpetCandidateAccepted(patch, candidateX, candidateZ, plantSeed,
                                 scatterDensityPermille))
    {
        return std::nullopt;
    }

    return NaturePondFoliagePlacement{
        patch.kind, patch.stableId, patch.anchorX, patch.anchorZ,
        patch.radiusX, patch.radiusZ, candidateX, candidateZ,
        patch.seed, plantSeed};
}

std::optional<NaturePondFoliagePlacement>
sampleNaturePondWaterFloraDistribution(
    uint32_t worldSeed, int candidateX, int candidateZ,
    uint32_t scatterDensityPermille) noexcept
{
    if (!waterFloraRegion(candidateX, candidateZ) ||
        scatterDensityPermille == 0u)
    {
        return std::nullopt;
    }

    const Patch patch = waterPatch(worldSeed, candidateX, candidateZ);
    if (!waterFloraRegion(patch.anchorX, patch.anchorZ) ||
        !patchActive(patch, worldSeed, scatterDensityPermille))
    {
        return std::nullopt;
    }

    const uint32_t plantSeed = spatialHash(
        candidateX, 157, candidateZ, worldSeed + kWaterPlantSeedSalt);
    if (!ellipseAccepted(patch, candidateX, candidateZ, plantSeed,
                         scatterDensityPermille))
    {
        return std::nullopt;
    }

    return NaturePondFoliagePlacement{
        patch.kind, patch.stableId, patch.anchorX, patch.anchorZ,
        patch.radiusX, patch.radiusZ, candidateX, candidateZ,
        patch.seed, plantSeed};
}

} // namespace engine::scene
