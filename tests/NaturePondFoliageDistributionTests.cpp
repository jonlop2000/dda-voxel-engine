#include "engine/scene/NaturePondFoliageDistribution.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

using engine::game::FoliagePatchKind;
using engine::scene::NaturePondFoliagePlacement;

struct LocatedPlacement
{
    int candidateX = 0;
    int candidateZ = 0;
    NaturePondFoliagePlacement placement{};

    bool operator==(const LocatedPlacement&) const = default;
};

struct PatchMetadata
{
    FoliagePatchKind kind = FoliagePatchKind::GrassTuft;
    int anchorCandidateX = 0;
    int anchorCandidateZ = 0;
    int radiusCandidateX = 0;
    int radiusCandidateZ = 0;
    uint32_t seed = 0;

    bool operator==(const PatchMetadata&) const = default;
};

struct PatchMembership
{
    PatchMetadata metadata{};
    std::set<std::pair<int, int>> candidates;
};

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::vector<LocatedPlacement> collectPlacements(
    uint32_t seed, int minimumX, int maximumX, int minimumZ, int maximumZ,
    uint32_t densityPermille, bool reverse)
{
    std::vector<LocatedPlacement> result;
    const int xBegin = reverse ? maximumX : minimumX;
    const int xEnd = reverse ? minimumX : maximumX;
    const int xStep = reverse ? -1 : 1;
    const int zBegin = reverse ? maximumZ : minimumZ;
    const int zEnd = reverse ? minimumZ : maximumZ;
    const int zStep = reverse ? -1 : 1;
    for (int z = zBegin;; z += zStep)
    {
        for (int x = xBegin;; x += xStep)
        {
            const std::optional<NaturePondFoliagePlacement> placement =
                engine::scene::sampleNaturePondFoliageDistribution(
                    seed, x, z, densityPermille);
            if (placement.has_value())
            {
                result.push_back(LocatedPlacement{x, z, *placement});
            }
            if (x == xEnd)
            {
                break;
            }
        }
        if (z == zEnd)
        {
            break;
        }
    }
    std::sort(result.begin(), result.end(),
              [](const LocatedPlacement& lhs, const LocatedPlacement& rhs) {
                  return std::tie(lhs.candidateZ, lhs.candidateX) <
                         std::tie(rhs.candidateZ, rhs.candidateX);
              });
    return result;
}

[[nodiscard]] std::vector<LocatedPlacement> collectCarpetPlacements(
    uint32_t seed, int minimumX, int maximumX, int minimumZ, int maximumZ,
    uint32_t densityPermille, bool reverse)
{
    std::vector<LocatedPlacement> result;
    const int xBegin = reverse ? maximumX : minimumX;
    const int xEnd = reverse ? minimumX : maximumX;
    const int xStep = reverse ? -1 : 1;
    const int zBegin = reverse ? maximumZ : minimumZ;
    const int zEnd = reverse ? minimumZ : maximumZ;
    const int zStep = reverse ? -1 : 1;
    for (int z = zBegin;; z += zStep)
    {
        for (int x = xBegin;; x += xStep)
        {
            const std::optional<NaturePondFoliagePlacement> placement =
                engine::scene::sampleNaturePondMeadowCarpetDistribution(
                    seed, x, z, densityPermille);
            if (placement.has_value())
            {
                result.push_back(LocatedPlacement{x, z, *placement});
            }
            if (x == xEnd)
            {
                break;
            }
        }
        if (z == zEnd)
        {
            break;
        }
    }
    std::sort(result.begin(), result.end(),
              [](const LocatedPlacement& lhs, const LocatedPlacement& rhs) {
                  return std::tie(lhs.candidateZ, lhs.candidateX) <
                         std::tie(rhs.candidateZ, rhs.candidateX);
              });
    return result;
}

[[nodiscard]] std::vector<LocatedPlacement> collectWaterPlacements(
    uint32_t seed, int minimumX, int maximumX, int minimumZ, int maximumZ,
    uint32_t densityPermille, bool reverse)
{
    std::vector<LocatedPlacement> result;
    const int xBegin = reverse ? maximumX : minimumX;
    const int xEnd = reverse ? minimumX : maximumX;
    const int xStep = reverse ? -1 : 1;
    const int zBegin = reverse ? maximumZ : minimumZ;
    const int zEnd = reverse ? minimumZ : maximumZ;
    const int zStep = reverse ? -1 : 1;
    for (int z = zBegin;; z += zStep)
    {
        for (int x = xBegin;; x += xStep)
        {
            const std::optional<NaturePondFoliagePlacement> placement =
                engine::scene::sampleNaturePondWaterFloraDistribution(
                    seed, x, z, densityPermille);
            if (placement.has_value())
            {
                result.push_back(LocatedPlacement{x, z, *placement});
            }
            if (x == xEnd)
            {
                break;
            }
        }
        if (z == zEnd)
        {
            break;
        }
    }
    std::sort(result.begin(), result.end(),
              [](const LocatedPlacement& lhs, const LocatedPlacement& rhs) {
                  return std::tie(lhs.candidateZ, lhs.candidateX) <
                         std::tie(rhs.candidateZ, rhs.candidateX);
              });
    return result;
}

[[nodiscard]] double pondDistance(int candidateX, int candidateZ)
{
    const double worldX = -11.95 + 0.30 * static_cast<double>(candidateX);
    const double worldZ = -9.95 + 0.30 * static_cast<double>(candidateZ);
    const double normalizedX = worldX / 6.5;
    const double normalizedZ = worldZ / 5.0;
    return std::sqrt(normalizedX * normalizedX + normalizedZ * normalizedZ);
}

[[nodiscard]] size_t kindIndex(FoliagePatchKind kind)
{
    switch (kind)
    {
    case FoliagePatchKind::GrassTuft:
        return 0u;
    case FoliagePatchKind::Shrub:
        return 1u;
    case FoliagePatchKind::FlowerCluster:
        return 2u;
    case FoliagePatchKind::ReedCluster:
        return 3u;
    case FoliagePatchKind::WaterLilyCluster:
        return 4u;
    case FoliagePatchKind::TreeCanopyCluster:
        return 5u;
    case FoliagePatchKind::Count:
        break;
    }
    return 0u;
}

[[nodiscard]] size_t uniquePatchCount(
    const std::vector<LocatedPlacement>& placements)
{
    std::map<uint64_t, bool> patchIds;
    for (const LocatedPlacement& value : placements)
    {
        patchIds.emplace(value.placement.stablePatchId, true);
    }
    return patchIds.size();
}

[[nodiscard]] PatchMetadata patchMetadata(
    const NaturePondFoliagePlacement& placement)
{
    return PatchMetadata{
        placement.kind,
        placement.patchAnchorCandidateX,
        placement.patchAnchorCandidateZ,
        placement.patchRadiusCandidateX,
        placement.patchRadiusCandidateZ,
        placement.patchSeed};
}

[[nodiscard]] std::map<uint64_t, PatchMembership> collectPatchMembership(
    const std::vector<LocatedPlacement>& placements)
{
    std::map<uint64_t, PatchMembership> result;
    for (const LocatedPlacement& value : placements)
    {
        const PatchMetadata metadata = patchMetadata(value.placement);
        const auto [found, inserted] = result.try_emplace(
            value.placement.stablePatchId,
            PatchMembership{metadata, {}});
        require(inserted || found->second.metadata == metadata,
                "One stable patch identity should retain one radius, anchor, seed, and kind");
        require(found->second.candidates
                    .emplace(value.candidateX, value.candidateZ)
                    .second,
                "A patch should contain each accepted candidate at most once");
    }
    return result;
}

void testDeterministicClassificationAndIterationOrder()
{
    constexpr uint32_t kSeed = 2701u;
    require(engine::scene::kNaturePondFoliageDistributionId ==
                std::string_view("nature-pond-ecology-v2-floral-life"),
            "Nature Pond foliage distribution should retain its versioned identity");

    const std::vector<LocatedPlacement> forward =
        collectPlacements(kSeed, -9, 88, -7, 73, 1000u, false);
    const std::vector<LocatedPlacement> repeat =
        collectPlacements(kSeed, -9, 88, -7, 73, 1000u, false);
    const std::vector<LocatedPlacement> reverse =
        collectPlacements(kSeed, -9, 88, -7, 73, 1000u, true);
    const std::vector<LocatedPlacement> alternate =
        collectPlacements(kSeed + 1u, -9, 88, -7, 73, 1000u, false);

    require(!forward.empty() && forward == repeat && forward == reverse,
            "Patch sampling should be deterministic and independent of iteration order");
    require(forward != alternate,
            "A different world seed should change the foliage distribution");
    std::array<size_t, static_cast<size_t>(FoliagePatchKind::Count)>
        forwardKindCounts{};
    for (const LocatedPlacement& value : forward)
    {
        ++forwardKindCounts[kindIndex(value.placement.kind)];
    }
    require(forward.size() == 1033u && uniquePatchCount(forward) == 89u &&
                forwardKindCounts ==
                    std::array<size_t, 6>{514u, 35u, 431u, 53u, 0u, 0u},
            "Expanded baseline ecology counts should remain frozen by semantic kind (measured=" +
                std::to_string(forward.size()) + "/" +
                std::to_string(uniquePatchCount(forward)) + "/" +
                std::to_string(forwardKindCounts[0]) + "/" +
                std::to_string(forwardKindCounts[1]) + "/" +
                std::to_string(forwardKindCounts[2]) + "/" +
                std::to_string(forwardKindCounts[3]) + ")");

    // explicit negative coordinates guard mathematical floor division at the
    // expanded-domain boundary rather than only exercising positive patch cells.
    bool foundNegativeCandidate = false;
    for (const LocatedPlacement& value : forward)
    {
        foundNegativeCandidate |=
            value.candidateX < 0 || value.candidateZ < 0;
    }
    require(foundNegativeCandidate,
            "Expanded sampling should retain deterministic negative-coordinate patches");
}

void testClusteredPlacementAndDeliberateBareSpace()
{
    constexpr uint32_t kSeed = 2701u;
    const std::vector<LocatedPlacement> placements =
        collectPlacements(kSeed, 1, 78, 1, 65, 1000u, false);

    std::array<size_t, static_cast<size_t>(FoliagePatchKind::Count)>
        kindCounts{};
    std::map<std::tuple<FoliagePatchKind, int, int>, size_t>
        patchCounts;
    std::map<uint64_t, PatchMetadata> patchIdentity;
    for (const LocatedPlacement& value : placements)
    {
        require(value.placement.plantCandidateX == value.candidateX &&
                    value.placement.plantCandidateZ == value.candidateZ,
                "A placement should retain its accepted candidate anchor");
        require((value.placement.stablePatchId >> 32u) == kSeed,
                "Stable patch identity should retain its authored world seed");
        const bool reed =
            value.placement.kind == FoliagePatchKind::ReedCluster;
        require(value.placement.patchRadiusCandidateX >= (reed ? 1 : 2) &&
                    value.placement.patchRadiusCandidateX <= (reed ? 2 : 3) &&
                    value.placement.patchRadiusCandidateZ >= (reed ? 1 : 2) &&
                    value.placement.patchRadiusCandidateZ <= (reed ? 2 : 3) &&
                    std::abs(value.candidateX -
                             value.placement.patchAnchorCandidateX) <=
                        value.placement.patchRadiusCandidateX &&
                    std::abs(value.candidateZ -
                             value.placement.patchAnchorCandidateZ) <=
                        value.placement.patchRadiusCandidateZ,
                "Accepted plants should remain inside their authored patch radii");
        ++kindCounts[kindIndex(value.placement.kind)];
        ++patchCounts[{value.placement.kind,
                       value.placement.patchAnchorCandidateX,
                       value.placement.patchAnchorCandidateZ}];
        const PatchMetadata identity = patchMetadata(value.placement);
        const auto [found, inserted] = patchIdentity.emplace(
            value.placement.stablePatchId, identity);
        require(inserted || found->second == identity,
                "One stable patch identity should map to one anchor, radius, seed, and kind");
    }
    require(std::all_of(kindCounts.begin(), kindCounts.begin() + 4,
                        [](size_t count) { return count > 0u; }),
            "The reference distribution should contain grass, shrubs, flowers, and reeds");
    require(placements.size() == 543u && patchIdentity.size() == 53u &&
                kindCounts ==
                    std::array<size_t, 6>{287u, 21u, 182u, 53u, 0u, 0u},
            "Reference ecology counts should remain frozen by semantic kind (measured=" +
                std::to_string(placements.size()) + "/" +
                std::to_string(patchIdentity.size()) + "/" +
                std::to_string(kindCounts[0]) + "/" +
                std::to_string(kindCounts[1]) + "/" +
                std::to_string(kindCounts[2]) + "/" +
                std::to_string(kindCounts[3]) + ")");

    size_t clusteredPatches = 0u;
    size_t maximumPatchSize = 0u;
    size_t maximumShrubPatchSize = 0u;
    for (const auto& [key, count] : patchCounts)
    {
        maximumPatchSize = std::max(maximumPatchSize, count);
        if (count >= 3u)
        {
            ++clusteredPatches;
        }
        if (std::get<0>(key) == FoliagePatchKind::Shrub)
        {
            maximumShrubPatchSize = std::max(maximumShrubPatchSize, count);
            require(count <= 4u,
                    "A shrub patch should retain its bounded anchor count");
        }
    }
    require(clusteredPatches >= 8u && maximumPatchSize >= 6u &&
                maximumShrubPatchSize >= 3u,
            "Accepted plants should form multiple readable multi-plant clusters");

    size_t eligibleMeadowCandidates = 0u;
    size_t eligibleReedCandidates = 0u;
    size_t meadowPlacements = 0u;
    size_t reedPlacements = 0u;
    for (int z = 1; z <= 65; ++z)
    {
        for (int x = 1; x <= 78; ++x)
        {
            const double distance = pondDistance(x, z);
            eligibleMeadowCandidates += distance >= 1.14 ? 1u : 0u;
            eligibleReedCandidates +=
                distance >= 0.91 && distance < 1.11 ? 1u : 0u;
        }
    }
    for (const LocatedPlacement& value : placements)
    {
        if (value.placement.kind == FoliagePatchKind::ReedCluster)
        {
            ++reedPlacements;
        }
        else
        {
            ++meadowPlacements;
        }
    }
    require(meadowPlacements * 2u < eligibleMeadowCandidates &&
                reedPlacements * 2u < eligibleReedCandidates,
            "Patch distribution should deliberately retain substantial bare ground");
}

void testShorelineSeparation()
{
    const std::vector<LocatedPlacement> placements =
        collectPlacements(2701u, -9, 88, -7, 73, 1000u, false);
    for (const LocatedPlacement& value : placements)
    {
        const double distance =
            pondDistance(value.candidateX, value.candidateZ);
        if (value.placement.kind == FoliagePatchKind::ReedCluster)
        {
            require(distance >= 0.91 && distance < 1.11,
                    "Reed clusters should remain inside the authored shore band");
        }
        else
        {
            require(distance >= 1.14,
                    "Land foliage should remain outside the deliberate shoreline gap");
        }
    }

    for (int z = -7; z <= 73; ++z)
    {
        for (int x = -9; x <= 88; ++x)
        {
            const double distance = pondDistance(x, z);
            if (distance >= 1.11 && distance < 1.14)
            {
                require(!engine::scene::sampleNaturePondFoliageDistribution(
                             2701u, x, z, 1500u)
                             .has_value(),
                        "The shoreline separation band should stay bare at dense scatter");
            }
        }
    }
}

void testDenseDistributionIsAStrictSuperset()
{
    const std::vector<LocatedPlacement> baseline =
        collectPlacements(2701u, -9, 88, -7, 73, 1000u, false);
    const std::vector<LocatedPlacement> dense =
        collectPlacements(2701u, -9, 88, -7, 73, 1500u, false);

    std::array<size_t, static_cast<size_t>(FoliagePatchKind::Count)>
        denseKindCounts{};
    for (const LocatedPlacement& value : dense)
    {
        ++denseKindCounts[kindIndex(value.placement.kind)];
    }

    std::map<std::pair<int, int>, NaturePondFoliagePlacement> denseByCandidate;
    for (const LocatedPlacement& value : dense)
    {
        const bool inserted =
            denseByCandidate
                .emplace(std::pair{value.candidateX, value.candidateZ},
                         value.placement)
                .second;
        require(inserted,
                "A distribution should emit at most one placement per candidate");
    }
    for (const LocatedPlacement& value : baseline)
    {
        const auto found = denseByCandidate.find(
            std::pair{value.candidateX, value.candidateZ});
        require(found != denseByCandidate.end() &&
                    found->second == value.placement,
                "Dense scatter should retain every baseline placement unchanged");
    }

    const std::map<uint64_t, PatchMembership> baselinePatches =
        collectPatchMembership(baseline);
    const std::map<uint64_t, PatchMembership> densePatches =
        collectPatchMembership(dense);
    size_t expandedBaselinePatchCount = 0u;
    for (const auto& [stablePatchId, baselinePatch] : baselinePatches)
    {
        const auto found = densePatches.find(stablePatchId);
        require(found != densePatches.end(),
                "Dense scatter should retain every baseline stable patch identity");
        const PatchMembership& densePatch = found->second;
        require(densePatch.metadata == baselinePatch.metadata,
                "Dense scatter should not reshape or relabel a baseline patch");
        require(std::includes(densePatch.candidates.begin(),
                              densePatch.candidates.end(),
                              baselinePatch.candidates.begin(),
                              baselinePatch.candidates.end()),
                "Dense scatter should retain every baseline patch member");
        expandedBaselinePatchCount +=
            densePatch.candidates.size() > baselinePatch.candidates.size()
                ? 1u
                : 0u;
    }
    require(dense.size() == 1381u && uniquePatchCount(dense) == 96u &&
                denseKindCounts ==
                    std::array<size_t, 6>{689u, 39u, 572u, 81u, 0u, 0u},
            "Expanded dense ecology counts should remain frozen by semantic kind (measured=" +
                std::to_string(dense.size()) + "/" +
                std::to_string(uniquePatchCount(dense)) + "/" +
                std::to_string(denseKindCounts[0]) + "/" +
                std::to_string(denseKindCounts[1]) + "/" +
                std::to_string(denseKindCounts[2]) + "/" +
                std::to_string(denseKindCounts[3]) + ")");
    require(densePatches.size() > baselinePatches.size() &&
                expandedBaselinePatchCount > 0u &&
                dense.size() > baseline.size(),
            "Dense scatter should add patches and members rather than reshaping or relabeling baseline ecology");
}

void testMeadowCarpetCoverageAndStableGaps()
{
    constexpr uint32_t kSeed = 2701u;
    require(engine::scene::kNaturePondMeadowCarpetDistributionId ==
                std::string_view("nature-pond-meadow-carpet-v1"),
            "Nature Pond meadow carpet should retain its versioned identity");

    const std::vector<LocatedPlacement> baseline =
        collectCarpetPlacements(kSeed, 1, 78, 1, 65, 1000u, false);
    const std::vector<LocatedPlacement> repeat =
        collectCarpetPlacements(kSeed, 1, 78, 1, 65, 1000u, false);
    const std::vector<LocatedPlacement> reverse =
        collectCarpetPlacements(kSeed, 1, 78, 1, 65, 1000u, true);
    const std::vector<LocatedPlacement> dense =
        collectCarpetPlacements(kSeed, 1, 78, 1, 65, 1500u, false);
    require(baseline == repeat && baseline == reverse,
            "Meadow carpet sampling should be deterministic and independent of iteration order");

    size_t eligibleMeadowCandidates = 0u;
    size_t corridorCandidates = 0u;
    std::set<std::pair<int, int>> baselineCandidates;
    std::set<std::pair<int, int>> denseCandidates;
    for (int z = 1; z <= 65; ++z)
    {
        for (int x = 1; x <= 78; ++x)
        {
            const double distance = pondDistance(x, z);
            eligibleMeadowCandidates += distance >= 1.14 ? 1u : 0u;
            const int worldXCentimeters = -1195 + 30 * x;
            const int worldZCentimeters = -995 + 30 * z;
            const int pathCenterXCentimeters =
                330 + (worldZCentimeters - 430) * 45 / 100;
            const bool corridor =
                worldZCentimeters >= 430 &&
                std::abs(worldXCentimeters - pathCenterXCentimeters) <= 48;
            corridorCandidates += corridor && distance >= 1.14 ? 1u : 0u;
            if (corridor)
            {
                require(!engine::scene::sampleNaturePondMeadowCarpetDistribution(
                             kSeed, x, z, 1500u)
                             .has_value(),
                        "The authored meadow access corridor should remain open at dense scatter");
            }
        }
    }
    for (const LocatedPlacement& value : baseline)
    {
        require(value.placement.kind != FoliagePatchKind::ReedCluster &&
                    pondDistance(value.candidateX, value.candidateZ) >= 1.14 &&
                    baselineCandidates
                        .emplace(value.candidateX, value.candidateZ)
                        .second,
                "Meadow carpet should contain one land placement per accepted candidate outside the shore");
    }
    for (const LocatedPlacement& value : dense)
    {
        denseCandidates.emplace(value.candidateX, value.candidateZ);
    }
    require(baseline.size() > 1000u &&
                baseline.size() < eligibleMeadowCandidates &&
                baseline.size() * 2u > eligibleMeadowCandidates &&
                corridorCandidates > 0u,
            "Baseline meadow carpet should be dense while retaining deliberate openings");
    require(dense.size() > baseline.size() &&
                std::includes(denseCandidates.begin(), denseCandidates.end(),
                              baselineCandidates.begin(),
                              baselineCandidates.end()),
            "Dense meadow carpet should add anchors without removing any baseline anchor");
}

void testWaterFloraColoniesAndOpenWater()
{
    constexpr uint32_t kSeed = 2701u;
    require(engine::scene::kNaturePondWaterFloraDistributionId ==
                std::string_view("nature-pond-water-flora-v1"),
            "Nature Pond water flora should retain its versioned identity");

    const std::vector<LocatedPlacement> baseline =
        collectWaterPlacements(kSeed, 1, 78, 1, 65, 1000u, false);
    const std::vector<LocatedPlacement> repeat =
        collectWaterPlacements(kSeed, 1, 78, 1, 65, 1000u, false);
    const std::vector<LocatedPlacement> reverse =
        collectWaterPlacements(kSeed, 1, 78, 1, 65, 1000u, true);
    const std::vector<LocatedPlacement> alternate =
        collectWaterPlacements(kSeed + 1u, 1, 78, 1, 65, 1000u, false);
    const std::vector<LocatedPlacement> dense =
        collectWaterPlacements(kSeed, 1, 78, 1, 65, 1500u, false);
    require(!baseline.empty() && baseline == repeat && baseline == reverse &&
                baseline != alternate,
            "Water flora should be deterministic, traversal-independent, and seed-authored");

    std::set<std::pair<int, int>> baselineCandidates;
    std::set<std::pair<int, int>> denseCandidates;
    std::set<uint64_t> waterPatchIds;
    for (const LocatedPlacement& value : baseline)
    {
        const double distance = pondDistance(value.candidateX,
                                             value.candidateZ);
        require(value.placement.kind ==
                        FoliagePatchKind::WaterLilyCluster &&
                    distance >= 0.27 && distance < 0.79 &&
                    value.placement.patchRadiusCandidateX >= 1 &&
                    value.placement.patchRadiusCandidateX <= 2 &&
                    value.placement.patchRadiusCandidateZ >= 1 &&
                    value.placement.patchRadiusCandidateZ <= 2 &&
                    (value.placement.stablePatchId >> 32u) == kSeed &&
                    baselineCandidates
                        .emplace(value.candidateX, value.candidateZ)
                        .second,
                "Lily pads should stay in bounded sparse colonies inside the authored water annulus");
        waterPatchIds.emplace(value.placement.stablePatchId);
    }
    for (const LocatedPlacement& value : dense)
    {
        denseCandidates.emplace(value.candidateX, value.candidateZ);
    }
    require(baseline.size() == 47u && waterPatchIds.size() == 12u &&
                dense.size() == 88u && uniquePatchCount(dense) == 14u,
            "Water-flora baseline and dense budgets should remain frozen "
            "(measured=" +
                std::to_string(baseline.size()) + "/" +
                std::to_string(waterPatchIds.size()) + "/" +
                std::to_string(dense.size()) + "/" +
                std::to_string(uniquePatchCount(dense)) + ")");
    require(dense.size() > baseline.size() &&
                std::includes(denseCandidates.begin(), denseCandidates.end(),
                              baselineCandidates.begin(),
                              baselineCandidates.end()),
            "Dense water flora should add members while preserving readable baseline colonies");

    const std::map<uint64_t, PatchMembership> baselinePatches =
        collectPatchMembership(baseline);
    const std::map<uint64_t, PatchMembership> densePatches =
        collectPatchMembership(dense);
    for (const auto& [stablePatchId, baselinePatch] : baselinePatches)
    {
        const auto found = densePatches.find(stablePatchId);
        require(found != densePatches.end() &&
                    found->second.metadata == baselinePatch.metadata &&
                    std::includes(found->second.candidates.begin(),
                                  found->second.candidates.end(),
                                  baselinePatch.candidates.begin(),
                                  baselinePatch.candidates.end()),
                "Dense water flora should preserve each baseline colony's identity, shape, and members");
    }

    const std::vector<LocatedPlacement> land =
        collectPlacements(kSeed, 1, 78, 1, 65, 1500u, false);
    std::set<uint64_t> landPatchIds;
    for (const LocatedPlacement& value : land)
    {
        landPatchIds.emplace(value.placement.stablePatchId);
    }
    for (uint64_t waterPatchId : waterPatchIds)
    {
        require(!landPatchIds.contains(waterPatchId),
                "Water and land ecology must use disjoint stable patch namespaces");
    }

    for (int z = 1; z <= 65; ++z)
    {
        for (int x = 1; x <= 78; ++x)
        {
            const double distance = pondDistance(x, z);
            if (distance < 0.27 || (distance >= 0.79 && distance < 0.91))
            {
                require(!engine::scene::sampleNaturePondWaterFloraDistribution(
                             kSeed, x, z, 1500u)
                             .has_value(),
                        "The central viewing water and outer reed separation ring should remain open");
            }
        }
    }
}

} // namespace

int main()
{
    try
    {
        testDeterministicClassificationAndIterationOrder();
        testClusteredPlacementAndDeliberateBareSpace();
        testShorelineSeparation();
        testDenseDistributionIsAStrictSuperset();
        testMeadowCarpetCoverageAndStableGaps();
        testWaterFloraColoniesAndOpenWater();
        std::cout << "Nature Pond foliage distribution tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Nature Pond foliage distribution test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
