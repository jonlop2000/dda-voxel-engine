#include "engine/scene/SunroofGroundFoliage.h"
#include "engine/scene/SunroofLivingWater.h"
#include "engine/render/FoliageVoxelGeometry.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

size_t indexOf(const glm::ivec3& dims, int x, int y, int z)
{
    return static_cast<size_t>(x) + static_cast<size_t>(y) * dims.x +
           static_cast<size_t>(z) * dims.x * dims.y;
}

bool idInBand(unsigned int id, uint8_t base, uint8_t count)
{
    return id >= base && id < static_cast<unsigned int>(base) + count;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 1.0e-5f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

bool nearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs,
                 float epsilon = 1.0e-5f)
{
    return nearlyEqual(lhs.x, rhs.x, epsilon) &&
           nearlyEqual(lhs.y, rhs.y, epsilon) &&
           nearlyEqual(lhs.z, rhs.z, epsilon);
}

void testSunroofGroundFoliageDistribution()
{
    constexpr glm::ivec3 dims{224, 14, 224};
    constexpr engine::scene::SunroofGroundFoliagePalette palette{
        10, 8, 30, 8, 50, 8, 70, 8, 90, 8,
    };

    const engine::scene::SunroofGroundFoliageBuild first =
        engine::scene::buildSunroofGroundFoliage(dims, palette);
    const engine::scene::SunroofGroundFoliageBuild repeat =
        engine::scene::buildSunroofGroundFoliage(dims, palette);
    const engine::scene::SunroofGroundFoliageBuild alternate =
        engine::scene::buildSunroofGroundFoliage(dims, palette, 0x12345678u);
    const engine::scene::SunroofGroundFoliageBuild maximumSeed =
        engine::scene::buildSunroofGroundFoliage(dims, palette, 0xffffffffu);
    const engine::scene::SunroofGroundFoliageBuild maximumSeedRepeat =
        engine::scene::buildSunroofGroundFoliage(dims, palette, 0xffffffffu);

    std::cout << "Sunroof garden stats: turf=" << first.stats.turfPatchCount
              << " ribbon=" << first.stats.ribbonTuftCount
              << " broad=" << first.stats.broadLeafCount << " reeds=" << first.stats.reedFanCount
              << " flowers=" << first.stats.flowerColonyCount
              << " occupied=" << first.stats.occupiedVoxelCount
              << " grass=" << first.stats.grassVoxelCount << " leaf=" << first.stats.leafVoxelCount
              << " reed=" << first.stats.reedVoxelCount
              << " accent=" << first.stats.accentVoxelCount
              << " blossom=" << first.stats.flowerVoxelCount << '\n';

    const size_t expectedSize = static_cast<size_t>(dims.x) * dims.y * dims.z;
    require(first.voxels.size() == expectedSize,
            "sunroof foliage should fill the requested volume allocation");
    require(first.voxels == repeat.voxels,
            "sunroof foliage should be deterministic for a fixed seed");
    require(first.voxels != alternate.voxels,
            "sunroof foliage should respond to its authored seed");
    require(maximumSeed.voxels == maximumSeedRepeat.voxels,
            "sunroof foliage should remain deterministic across the full seed range");

    require(first.stats.turfPatchCount > 0 && first.stats.ribbonTuftCount > 0 &&
                first.stats.broadLeafCount > 0 && first.stats.reedFanCount > 0 &&
                first.stats.flowerColonyCount > 0,
            "sunroof garden should contain every authored foliage archetype");
    require(first.stats.turfPatchCount >= 2200 && first.stats.turfPatchCount <= 3200 &&
                first.stats.ribbonTuftCount >= 130 && first.stats.ribbonTuftCount <= 260 &&
                first.stats.broadLeafCount >= 110 && first.stats.broadLeafCount <= 230 &&
                first.stats.reedFanCount >= 30 && first.stats.reedFanCount <= 100 &&
                first.stats.flowerColonyCount >= 15 && first.stats.flowerColonyCount <= 60 &&
                first.stats.occupiedVoxelCount >= 40000 && first.stats.occupiedVoxelCount <= 60000,
            "sunroof garden density should remain inside the accepted V2 budget");
    require(first.stats.occupiedVoxelCount ==
                first.stats.grassVoxelCount + first.stats.leafVoxelCount +
                    first.stats.reedVoxelCount + first.stats.accentVoxelCount +
                    first.stats.flowerVoxelCount,
            "sunroof foliage statistics should account for every occupied voxel");
    require(first.stats.grassVoxelCount >= 20000 && first.stats.leafVoxelCount >= 5000 &&
                first.stats.reedVoxelCount >= 1000 && first.stats.accentVoxelCount >= 500 &&
                first.stats.flowerVoxelCount >= 100,
            "every foliage material family should contribute visible emitted volume");

    for (const uint8_t id : first.voxels)
    {
        const bool grass = idInBand(id, palette.grassBase, palette.grassCount);
        const bool leaf = idInBand(id, palette.leafBase, palette.leafCount);
        const bool reed = idInBand(id, palette.reedBase, palette.reedCount);
        const bool accent = idInBand(id, palette.accentBase, palette.accentCount);
        const bool flower = idInBand(id, palette.flowerBase, palette.flowerCount);
        require(id == 0 || grass || leaf || reed || accent || flower,
                "sunroof foliage should emit only its assigned palette bands");
        if (flower)
        {
            require(id >= palette.flowerBase + (palette.flowerCount * 2u) / 3u,
                    "sunroof blossoms should use the bright floral palette tier");
        }
    }

    size_t focalLowVoxels = 0;
    size_t focalTallVoxels = 0;
    size_t lowLayerVoxels = 0;
    size_t middleLayerVoxels = 0;
    size_t tallLayerVoxels = 0;
    size_t horizontallyGroupedVoxels = 0;
    size_t occupiedVoxels = 0;
    size_t innerThirdColumns = 0;
    size_t innerThirdOccupiedColumns = 0;
    size_t outerBandOccupiedColumns = 0;
    size_t occupiedColumns = 0;

    for (int z = 0; z < dims.z; ++z)
    {
        for (int x = 0; x < dims.x; ++x)
        {
            bool columnOccupied = false;
            for (int y = 0; y < dims.y; ++y)
            {
                if (first.voxels[indexOf(dims, x, y, z)] == 0)
                {
                    continue;
                }
                columnOccupied = true;
                ++occupiedVoxels;
                if (y <= 1)
                {
                    ++lowLayerVoxels;
                }
                else if (y <= 5)
                {
                    ++middleLayerVoxels;
                }
                else
                {
                    ++tallLayerVoxels;
                }
                if (engine::scene::isSunroofGroundFoliageWalkway(dims, x, z))
                {
                    y <= 1 ? ++focalLowVoxels : ++focalTallVoxels;
                }

                const bool hasHorizontalNeighbor =
                    (x > 0 && first.voxels[indexOf(dims, x - 1, y, z)] != 0) ||
                    (x + 1 < dims.x && first.voxels[indexOf(dims, x + 1, y, z)] != 0) ||
                    (z > 0 && first.voxels[indexOf(dims, x, y, z - 1)] != 0) ||
                    (z + 1 < dims.z && first.voxels[indexOf(dims, x, y, z + 1)] != 0);
                horizontallyGroupedVoxels += hasHorizontalNeighbor ? 1u : 0u;
            }

            const bool innerThird =
                x >= dims.x / 3 && x < (dims.x * 2) / 3 && z >= dims.z / 3 && z < (dims.z * 2) / 3;
            if (innerThird)
            {
                ++innerThirdColumns;
                innerThirdOccupiedColumns += columnOccupied ? 1u : 0u;
            }
            const bool outerBand =
                x < dims.x / 6 || x >= (dims.x * 5) / 6 || z < dims.z / 6 || z >= (dims.z * 5) / 6;
            outerBandOccupiedColumns += outerBand && columnOccupied ? 1u : 0u;
            occupiedColumns += columnOccupied ? 1u : 0u;
        }
    }

    std::cout << "Sunroof garden shape: focalLow=" << focalLowVoxels
              << " focalTall=" << focalTallVoxels << " innerCoverage=" << innerThirdOccupiedColumns
              << '/' << innerThirdColumns << " grouped=" << horizontallyGroupedVoxels << '/'
              << occupiedVoxels << " outerColumns=" << outerBandOccupiedColumns << '/'
              << occupiedColumns << '\n';

    require(focalLowVoxels > 0 && focalTallVoxels == 0,
            "the focal lens should admit low turf but reject eye-level foliage");
    require(lowLayerVoxels > 0 && middleLayerVoxels > 0 && tallLayerVoxels > 0,
            "sunroof foliage should retain low, middle, and tall depth layers");
    require(innerThirdOccupiedColumns * 8u > innerThirdColumns,
            "sunroof foliage should occupy the central field rather than form a "
            "ring");
    require(outerBandOccupiedColumns * 4u < occupiedColumns * 3u,
            "sunroof foliage should not concentrate most occupied columns at the "
            "perimeter");
    require(horizontallyGroupedVoxels * 3u > occupiedVoxels,
            "sunroof foliage should read as grouped plant silhouettes, not "
            "isolated posts");

    for (int y = 0; y < dims.y; ++y)
    {
        require(first.voxels[indexOf(dims, 0, y, dims.z / 3)] == 0 &&
                    first.voxels[indexOf(dims, dims.x - 1, y, dims.z / 3)] == 0 &&
                    first.voxels[indexOf(dims, dims.x / 3, y, 0)] == 0 &&
                    first.voxels[indexOf(dims, dims.x / 3, y, dims.z - 1)] == 0,
                "sunroof foliage should keep a clean perimeter against the white "
                "walls");
    }
}

void testSunroofGroundingAndSurfacePalette()
{
    constexpr glm::ivec3 dims{68, 1, 68};
    constexpr engine::scene::SunroofGroundSurfacePalette palette{
        20, 8, 40, 8, 60, 8,
    };
    constexpr uint32_t foliageSeed = 0x53554e46u;
    std::array<bool, 256> used{};
    std::array<size_t, 3> materialCounts{};
    size_t sameNeighborCount = 0;
    size_t neighborCount = 0;
    size_t groundingCount = 0;
    uint64_t highGroundShadeSum = 0;
    uint64_t lowGroundShadeSum = 0;
    size_t highGroundGrassCount = 0;
    size_t lowGroundGrassCount = 0;

    for (int z = 0; z < dims.z; ++z)
    {
        for (int x = 0; x < dims.x; ++x)
        {
            const uint8_t grounding =
                engine::scene::sunroofGroundFoliageGrounding(dims, x, z, foliageSeed);
            const uint8_t repeatGrounding =
                engine::scene::sunroofGroundFoliageGrounding(dims, x, z, foliageSeed);
            require(grounding == repeatGrounding,
                    "sunroof contact grounding should be deterministic");
            groundingCount += grounding > 0 ? 1u : 0u;

            const uint8_t first =
                engine::scene::sunroofGroundBlockMaterial(dims, x, z, palette, grounding);
            const uint8_t repeat =
                engine::scene::sunroofGroundBlockMaterial(dims, x, z, palette, grounding);
            require(first == repeat, "sunroof planted-surface palette should be deterministic");

            const bool grass = idInBand(first, palette.grassBase, palette.grassCount);
            const bool algae = idInBand(first, palette.algaeBase, palette.algaeCount);
            const bool gravel = idInBand(first, palette.gravelBase, palette.gravelCount);
            require(grass || algae || gravel,
                    "sunroof floor should use only its authored substrate bands");
            used[first] = true;
            materialCounts[grass ? 0u : (algae ? 1u : 2u)]++;

            if (grass && grounding >= 180)
            {
                highGroundShadeSum += first - palette.grassBase;
                ++highGroundGrassCount;
            }
            if (grass && grounding <= 40)
            {
                lowGroundShadeSum += first - palette.grassBase;
                ++lowGroundGrassCount;
            }

            if (x + 1 < dims.x)
            {
                const uint8_t neighborGrounding =
                    engine::scene::sunroofGroundFoliageGrounding(dims, x + 1, z, foliageSeed);
                const uint8_t neighbor = engine::scene::sunroofGroundBlockMaterial(
                    dims, x + 1, z, palette, neighborGrounding);
                const bool sameMaterial =
                    (grass && idInBand(neighbor, palette.grassBase, palette.grassCount)) ||
                    (algae && idInBand(neighbor, palette.algaeBase, palette.algaeCount)) ||
                    (gravel && idInBand(neighbor, palette.gravelBase, palette.gravelCount));
                sameNeighborCount += sameMaterial ? 1u : 0u;
                ++neighborCount;
            }
        }
    }

    size_t usedGrassShades = 0;
    size_t usedAlgaeShades = 0;
    size_t usedGravelShades = 0;
    for (unsigned int id = 0; id < used.size(); ++id)
    {
        usedGrassShades +=
            used[id] && idInBand(id, palette.grassBase, palette.grassCount) ? 1u : 0u;
        usedAlgaeShades +=
            used[id] && idInBand(id, palette.algaeBase, palette.algaeCount) ? 1u : 0u;
        usedGravelShades +=
            used[id] && idInBand(id, palette.gravelBase, palette.gravelCount) ? 1u : 0u;
    }

    std::cout << "Sunroof surface stats: grass=" << materialCounts[0]
              << " algae=" << materialCounts[1] << " gravel=" << materialCounts[2]
              << " grounding=" << groundingCount << " neighborCoherence=" << sameNeighborCount
              << '/' << neighborCount << " shades=" << usedGrassShades << ',' << usedAlgaeShades
              << ',' << usedGravelShades << '\n';

    require(groundingCount > 0 && groundingCount < static_cast<size_t>(dims.x * dims.z),
            "contact grounding should be feathered across planted regions only");
    require(engine::scene::sunroofGroundFoliageGrounding(dims, 0, dims.z / 2) == 0,
            "contact grounding should preserve the wall perimeter");
    require(materialCounts[0] > materialCounts[1] && materialCounts[1] > 0 && materialCounts[2] > 0,
            "grass should dominate restrained algae and gravel substrate islands");
    require(sameNeighborCount * 4u > neighborCount * 3u,
            "sunroof substrate bands should form coherent regions, not a "
            "checkerboard");
    require(usedGrassShades >= 2 && usedGrassShades <= 4 && usedAlgaeShades <= 4 &&
                usedGravelShades <= 4,
            "each substrate should use a restrained coherent four-shade palette");
    require(highGroundGrassCount > 0 && lowGroundGrassCount > 0 &&
                highGroundShadeSum * lowGroundGrassCount < lowGroundShadeSum * highGroundGrassCount,
            "dense foliage beds should darken the floor relative to open grass");

    bool seedChangedGrounding = false;
    for (int z = 0; z < dims.z && !seedChangedGrounding; ++z)
    {
        for (int x = 0; x < dims.x; ++x)
        {
            seedChangedGrounding =
                engine::scene::sunroofGroundFoliageGrounding(dims, x, z, foliageSeed) !=
                engine::scene::sunroofGroundFoliageGrounding(dims, x, z, 0x12345678u);
            if (seedChangedGrounding)
            {
                break;
            }
        }
    }
    require(seedChangedGrounding, "contact grounding should respond to the garden seed");
}

void testSunroofGroundFoliageRejectsInvalidInputs()
{
    const glm::ivec3 dims{48, 8, 48};
    const engine::scene::SunroofGroundFoliageBuild result =
        engine::scene::buildSunroofGroundFoliage(dims,
                                                 engine::scene::SunroofGroundFoliagePalette{});
    require(result.voxels.size() == static_cast<size_t>(dims.x) * dims.y * dims.z,
            "invalid palette should still return a correctly sized empty volume");
    require(result.stats.occupiedVoxelCount == 0,
            "invalid palette should not emit ambiguous material IDs");
    require(engine::scene::sunroofGroundBlockMaterial(
                {12, 1, 12}, 4, 9, engine::scene::SunroofGroundSurfacePalette{}) == 0,
            "invalid planted-surface palette should resolve to empty");
    require(engine::scene::sunroofGroundFoliageGrounding({12, 1, 12}, -1, 4) == 0,
            "out-of-bounds grounding queries should resolve to zero");
}

void testSunroofLivingWaterBudgets()
{
    constexpr glm::ivec3 dims{224, 14, 224};
    constexpr glm::vec3 position{-28.0f, 2.03f, -28.0f};
    constexpr glm::vec3 scale{0.25f, 0.24f, 0.25f};
    constexpr engine::scene::SunroofLivingFoliagePalette palette{
        80, 12, 104, 12, 35, 12,
    };

    const auto foliage = engine::scene::buildSunroofLivingFoliage(
        dims, position, scale, palette);
    const auto repeated = engine::scene::buildSunroofLivingFoliage(
        dims, position, scale, palette);
    require(foliage.size() == 24u && repeated.size() == foliage.size(),
            "living-water hero foliage should retain its fixed 24-anchor budget");

    size_t flowerCount = 0;
    size_t reedCount = 0;
    size_t shrubCount = 0;
    for (size_t index = 0; index < foliage.size(); ++index)
    {
        const auto& plant = foliage[index];
        const auto& repeat = repeated[index];
        require(plant.stableId == repeat.stableId &&
                    plant.stablePatchId == repeat.stablePatchId &&
                    nearlyEqual(plant.rootWorld, repeat.rootWorld) &&
                    plant.randomSeed == repeat.randomSeed &&
                    plant.tipMaterialId == repeat.tipMaterialId,
                "living-water foliage should be deterministic");
        require(plant.rootWorld.x > position.x &&
                    plant.rootWorld.x < position.x + dims.x * scale.x &&
                    plant.rootWorld.z > position.z &&
                    plant.rootWorld.z < position.z + dims.z * scale.z &&
                    nearlyEqual(plant.rootWorld.y, position.y),
                "living-water foliage should remain rooted in the authored garden");
        require(plant.morphology == engine::game::foliageMorphologyForPatch(
                                        plant.patchKind, plant.patchSeed),
                "living-water foliage morphology should match its semantic patch");
        if (plant.patchKind == engine::game::FoliagePatchKind::FlowerCluster)
        {
            ++flowerCount;
            require(plant.tipMaterialId >= palette.flowerBase + 8u &&
                        plant.tipMaterialId < palette.flowerBase + palette.flowerCount,
                    "hero blossoms should cycle the colorful upper coral tier");
        }
        else if (plant.patchKind == engine::game::FoliagePatchKind::ReedCluster)
        {
            ++reedCount;
        }
        else if (plant.patchKind == engine::game::FoliagePatchKind::Shrub)
        {
            ++shrubCount;
        }
    }
    require(flowerCount == 16u && reedCount == 4u && shrubCount == 4u,
            "the animated overlay should favor flowers while retaining reeds and shrubs");

    const engine::render::FoliageVoxelGeometry geometry =
        engine::render::buildFoliageVoxelGeometry(foliage);
    require(geometry.semanticInstanceCount == foliage.size() &&
                geometry.patchCount == foliage.size() &&
                !geometry.primitives.empty() && geometry.primitives.size() <= 1400u,
            "living-water hero foliage should expand into one bounded GPU batch");

    constexpr glm::vec3 boundsMin{-32.0f, 3.0f, -32.0f};
    constexpr glm::vec3 boundsMax{32.0f, 48.0f, 32.0f};
    constexpr float surfaceHeight = 46.0f;
    const auto bubbles = engine::scene::buildSunroofBubbleMotes(
        boundsMin, boundsMax, surfaceHeight, 0.0f);
    const auto bubblesRepeat = engine::scene::buildSunroofBubbleMotes(
        boundsMin, boundsMax, surfaceHeight, 0.0f);
    const auto bubblesAdvanced = engine::scene::buildSunroofBubbleMotes(
        boundsMin, boundsMax, surfaceHeight, 1.0f);
    require(bubbles.size() == 27u && bubblesRepeat.size() == bubbles.size() &&
                bubblesAdvanced.size() == bubbles.size(),
            "bubble streams should retain a fixed three-by-nine draw budget");

    bool advanced = false;
    for (size_t index = 0; index < bubbles.size(); ++index)
    {
        const auto& bubble = bubbles[index];
        require(nearlyEqual(bubble.center, bubblesRepeat[index].center) &&
                    nearlyEqual(bubble.scale, bubblesRepeat[index].scale),
                "bubble streams should be deterministic at a fixed time");
        require(bubble.center.x > boundsMin.x && bubble.center.x < boundsMax.x &&
                    bubble.center.y > boundsMin.y && bubble.center.y < surfaceHeight &&
                    bubble.center.z > boundsMin.z && bubble.center.z < boundsMax.z &&
                    bubble.scale >= 0.055f && bubble.scale <= 0.1301f,
                "bubble motes should remain small and inside the water volume");
        advanced = advanced ||
                   !nearlyEqual(bubble.center, bubblesAdvanced[index].center);
    }
    require(advanced, "bubble streams should rise and drift over time");

    const engine::scene::SunroofBubbleControls reducedControls{
        0.50f, 1.75f, 0.0f, 0.0f};
    const auto reducedBubbles = engine::scene::buildSunroofBubbleMotes(
        boundsMin, boundsMax, surfaceHeight, 0.0f, reducedControls);
    const auto reducedBubblesAdvanced = engine::scene::buildSunroofBubbleMotes(
        boundsMin, boundsMax, surfaceHeight, 40.0f, reducedControls);
    require(reducedBubbles.size() == 15u &&
                reducedBubblesAdvanced.size() == reducedBubbles.size(),
            "bubble amount should select an even three-stream subset under the 27-cube ceiling");
    for (size_t index = 0; index < reducedBubbles.size(); ++index)
    {
        require(nearlyEqual(reducedBubbles[index].center,
                            reducedBubblesAdvanced[index].center) &&
                    reducedBubbles[index].scale >= 0.0962f &&
                    reducedBubbles[index].scale <= 0.2277f,
                "zero rise/drift controls should freeze bounded, size-scaled bubble motes");
    }
    require(engine::scene::buildSunroofBubbleMotes(
                boundsMin, boundsMax, surfaceHeight, 0.0f,
                engine::scene::SunroofBubbleControls{0.0f, 1.0f, 1.0f, 1.0f})
                .empty() &&
                engine::scene::buildSunroofBubbleMotes(
                    boundsMin, boundsMax, surfaceHeight, 0.0f,
                    engine::scene::SunroofBubbleControls{2.0f, 2.0f, 2.0f, 2.0f})
                        .size() == 27u,
            "bubble controls should support zero output and clamp to the validated maximum");

    require(engine::scene::buildSunroofLivingFoliage(
                {8, 4, 8}, position, scale, palette).empty() &&
                engine::scene::buildSunroofBubbleMotes(
                    boundsMin, boundsMin, surfaceHeight, 0.0f).empty(),
            "living-water builders should reject invalid scene bounds");
}

} // namespace

int main()
{
    try
    {
        testSunroofGroundFoliageDistribution();
        testSunroofGroundingAndSurfacePalette();
        testSunroofGroundFoliageRejectsInvalidInputs();
        testSunroofLivingWaterBudgets();
        std::cout << "Sunroof ground foliage tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Sunroof ground foliage test failure: " << error.what() << '\n';
        return 1;
    }
}
