#include "engine/scene/NaturePondSunroofProbe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>

#include "engine/scene/SceneConfig.h"

namespace
{
constexpr float kTau = 6.28318530717958647692f;
constexpr float kVolumePitch = 0.20f;
constexpr glm::vec3 kVolumePosition{-6.0f, 0.4f, -6.0f};
constexpr glm::ivec3 kVolumeDims{60, 43, 60};
constexpr float kIslandBedHeight = 0.72f;
constexpr float kIslandPlateauRadius = 0.58f;
constexpr float kEcologyClearRadius = 1.18f;
constexpr float kColumnCircleRadius = 5.05f;
constexpr float kColumnShaftRadius = 0.24f;
constexpr float kColumnBaseRadius = 0.38f;
constexpr float kColumnCapitalRadius = 0.40f;
constexpr float kColumnBottom = 0.68f;
constexpr float kColumnTop = 8.05f;

constexpr NaturePondSunroofProbeLayout kLayout{
    NaturePondSunroof::IslandCenterXZ,
    glm::vec2(2.05f, 1.62f),
    3.78f,
    3.82f,
    5.72f,
    kColumnCircleRadius,
    kColumnShaftRadius,
    kColumnBottom,
    kColumnTop,
    7.92f,
    8.62f,
    12u,
};

constexpr NaturePondSunroofGlassLayout kGlassLayout{
    1u,
    1.90f,
    3.42f,
    7.34f,
    0.10f,
};

constexpr engine::game::FishHabitat kFishHabitat{
    glm::vec3(-4.90f, 1.05f, -2.15f),
    glm::vec3(-2.35f, 2.82f, 2.15f),
    glm::vec3(-3.62f, 1.94f, 0.0f),
};

struct TaperedBough
{
    glm::vec3 startOffset{0.0f};
    glm::vec3 endOffset{0.0f};
    float startRadius = 0.0f;
    float endRadius = 0.0f;
};

// the base tree already supports the left, right/back-right, and front-center
// crown masses. these four fork pairs reach the unsupported diagonal pads and
// make a readable radial scaffold from the pavilion's low camera without
// changing the promoted nature pond generator.
constexpr std::array<TaperedBough, 8> kSunroofRadialBoughs{
    TaperedBough{{-0.04f, 3.38f, 0.02f}, {-0.58f, 4.12f, 0.54f}, 0.26f, 0.17f},
    TaperedBough{{-0.58f, 4.12f, 0.54f}, {-1.34f, 4.78f, 1.17f}, 0.17f, 0.075f},
    TaperedBough{{0.08f, 3.52f, 0.02f}, {0.62f, 4.18f, 0.58f}, 0.24f, 0.16f},
    TaperedBough{{0.62f, 4.18f, 0.58f}, {1.30f, 4.82f, 1.20f}, 0.16f, 0.075f},
    TaperedBough{{-0.02f, 3.66f, -0.04f}, {-0.56f, 4.22f, -0.60f}, 0.23f, 0.15f},
    TaperedBough{{-0.56f, 4.22f, -0.60f}, {-1.30f, 4.82f, -1.20f}, 0.15f, 0.070f},
    TaperedBough{{0.06f, 3.74f, -0.05f}, {0.60f, 4.28f, -0.62f}, 0.21f, 0.14f},
    TaperedBough{{0.60f, 4.28f, -0.62f}, {1.28f, 4.88f, -1.18f}, 0.14f, 0.065f},
};

struct CanopyWindow
{
    glm::vec3 offset{0.0f};
    glm::vec3 radii{1.0f};
};

// one central underside opening exposes the crown fork. four diagonal windows
// reveal the new boughs and keep the flowering pads from merging back into one
// spherical mass at the underside review camera.
constexpr std::array<CanopyWindow, 5> kSunroofCanopyWindows{
    CanopyWindow{{0.00f, -1.02f, 0.06f}, {0.72f, 0.54f, 0.78f}},
    CanopyWindow{{-0.94f, -0.62f, 0.94f}, {0.46f, 0.54f, 0.62f}},
    CanopyWindow{{0.94f, -0.60f, 0.96f}, {0.46f, 0.54f, 0.62f}},
    CanopyWindow{{-0.92f, -0.56f, -0.92f}, {0.44f, 0.50f, 0.60f}},
    CanopyWindow{{0.94f, -0.54f, -0.94f}, {0.44f, 0.50f, 0.60f}},
};

size_t voxelIndex(const glm::ivec3& dims, int x, int y, int z)
{
    return static_cast<size_t>(x) + static_cast<size_t>(dims.x) *
                                        (static_cast<size_t>(y) +
                                         static_cast<size_t>(dims.y) *
                                             static_cast<size_t>(z));
}

uint32_t hash32(uint32_t value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

uint32_t spatialHash(int x, int y, int z, uint32_t seed)
{
    return hash32(static_cast<uint32_t>(x) * 73856093u ^
                  static_cast<uint32_t>(y) * 19349663u ^
                  static_cast<uint32_t>(z) * 83492791u ^ seed);
}

float islandDistance(float worldX, float worldZ)
{
    const float dx = (worldX - kLayout.centerXZ.x) / kLayout.islandRadii.x;
    const float dz = (worldZ - kLayout.centerXZ.y) / kLayout.islandRadii.y;
    return std::sqrt(dx * dx + dz * dz);
}

float islandTop(float distance)
{
    if (distance <= kIslandPlateauRadius)
    {
        return kLayout.islandSurfaceHeight;
    }
    const float t = std::clamp(
        (1.0f - distance) / (1.0f - kIslandPlateauRadius), 0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    return kIslandBedHeight +
           (kLayout.islandSurfaceHeight - kIslandBedHeight) * smooth;
}

uint8_t bandMaterial(uint8_t base, uint8_t count, int x, int y, int z,
                     uint32_t seed)
{
    return static_cast<uint8_t>(
        base + spatialHash(x, y, z, seed) % static_cast<uint32_t>(count));
}

NaturePondVolumeData buildIslandAndPavilion(uint32_t seed)
{
    NaturePondVolumeData result{};
    result.name = "NaturePondSunroofIslandPavilion";
    result.dims = kVolumeDims;
    result.position = kVolumePosition;
    result.scale = glm::vec3(kVolumePitch);
    result.role = NaturePondVolumeRole::SunroofArchitectureOpaque;
    result.voxels.resize(static_cast<size_t>(result.dims.x) * result.dims.y *
                         result.dims.z);

    std::array<glm::vec2, kLayout.columnCount> columnCenters{};
    for (uint32_t column = 0; column < kLayout.columnCount; ++column)
    {
        const float angle = kTau * static_cast<float>(column) /
                            static_cast<float>(kLayout.columnCount);
        columnCenters[column] =
            kLayout.centerXZ +
            glm::vec2(std::cos(angle), std::sin(angle)) * kColumnCircleRadius;
    }

    for (int z = 0; z < result.dims.z; ++z)
    {
        for (int y = 0; y < result.dims.y; ++y)
        {
            for (int x = 0; x < result.dims.x; ++x)
            {
                const glm::vec3 world =
                    result.position +
                    (glm::vec3(x, y, z) + glm::vec3(0.5f)) * result.scale;
                const float dx = world.x - kLayout.centerXZ.x;
                const float dz = world.z - kLayout.centerXZ.y;
                const float radial = std::sqrt(dx * dx + dz * dz);

                uint8_t material = 0u;
                const float islandD = islandDistance(world.x, world.z);
                if (islandD <= 1.0f && world.y >= kVolumePosition.y &&
                    world.y <= islandTop(islandD))
                {
                    const float surfaceDepth = islandTop(islandD) - world.y;
                    if (surfaceDepth <= kVolumePitch * 1.5f)
                    {
                        material = islandD <= 0.72f
                                       ? bandMaterial(
                                             NaturePondMaterial::Meadow::Base,
                                             NaturePondMaterial::Meadow::Count,
                                             x, y, z, seed + 17u)
                                       : bandMaterial(
                                             NaturePondMaterial::Gravel::Base,
                                             NaturePondMaterial::Gravel::Count,
                                             x, y, z, seed + 23u);
                    }
                    else
                    {
                        material = bandMaterial(
                            NaturePondMaterial::Soil::Base,
                            NaturePondMaterial::Soil::Count, x, y, z,
                            seed + 29u);
                    }
                }

                const bool roofSlab =
                    world.y >= kLayout.pavilionRoofBottom &&
                    world.y <= kLayout.pavilionRoofTop &&
                    radial >= kLayout.pavilionInnerRadius &&
                    radial <= kLayout.pavilionOuterRadius;
                const bool innerOculusLip =
                    world.y >= kLayout.pavilionRoofBottom - 0.20f &&
                    world.y <= kLayout.pavilionRoofTop + 0.28f &&
                    radial >= kLayout.pavilionInnerRadius - 0.16f &&
                    radial <= kLayout.pavilionInnerRadius + 0.18f;
                const bool outerFascia =
                    world.y >= kLayout.pavilionRoofBottom - 0.12f &&
                    world.y <= kLayout.pavilionRoofTop + 0.22f &&
                    radial >= kLayout.pavilionOuterRadius - 0.18f &&
                    radial <= kLayout.pavilionOuterRadius + 0.12f;

                bool columnShaft = false;
                bool columnBase = false;
                bool columnCapital = false;
                if (world.y >= kColumnBottom && world.y <= kColumnTop)
                {
                    for (const glm::vec2& center : columnCenters)
                    {
                        const glm::vec2 delta(world.x - center.x,
                                              world.z - center.y);
                        const float distanceSquared = glm::dot(delta, delta);
                        columnShaft =
                            columnShaft ||
                            distanceSquared <=
                                kColumnShaftRadius * kColumnShaftRadius;
                        columnBase =
                            columnBase ||
                            (world.y <= kColumnBottom + 0.55f &&
                             distanceSquared <=
                                 kColumnBaseRadius * kColumnBaseRadius);
                        columnCapital =
                            columnCapital ||
                            (world.y >= kColumnTop - 0.58f &&
                             distanceSquared <=
                                 kColumnCapitalRadius * kColumnCapitalRadius);
                    }
                }

                if (roofSlab || innerOculusLip || outerFascia || columnShaft ||
                    columnBase || columnCapital)
                {
                    uint8_t shade = 0u;
                    if (world.y < kLayout.pavilionRoofBottom + 0.08f &&
                        (roofSlab || innerOculusLip || outerFascia))
                    {
                        shade = 2u;
                    }
                    else if (innerOculusLip || outerFascia || columnCapital)
                    {
                        shade = 1u;
                    }
                    else if (columnShaft && world.y <
                                                naturePondLayout().pondSurfaceHeight +
                                                    0.25f)
                    {
                        shade = 4u;
                    }
                    else if (spatialHash(x / 3, y / 3, z / 3, seed + 41u) %
                                 17u ==
                             0u)
                    {
                        shade = 5u;
                    }
                    material = static_cast<uint8_t>(
                        NaturePondMaterial::Architecture::Base + shade);
                }

                result.voxels[voxelIndex(result.dims, x, y, z)] = material;
            }
        }
    }
    return result;
}

std::optional<size_t> findVolume(const NaturePondBuild& build,
                                 NaturePondVolumeRole role)
{
    for (size_t index = 0; index < build.volumes.size(); ++index)
    {
        if (build.volumes[index].role == role)
        {
            return index;
        }
    }
    return std::nullopt;
}

uint64_t countOccupied(const NaturePondVolumeData& volume)
{
    return static_cast<uint64_t>(std::count_if(
        volume.voxels.begin(), volume.voxels.end(),
        [](uint8_t material) { return material != 0u; }));
}

glm::vec3 volumeCenter(const NaturePondVolumeData& volume)
{
    return volume.position + glm::vec3(volume.dims) * volume.scale * 0.5f;
}

void addSunroofRadialBoughs(NaturePondVolumeData& trunk, uint32_t seed)
{
    const glm::vec3 extent = glm::vec3(trunk.dims) * trunk.scale;
    const glm::vec3 treeBase(trunk.position.x + extent.x * 0.5f,
                             trunk.position.y,
                             trunk.position.z + extent.z * 0.5f);
    for (int z = 0; z < trunk.dims.z; ++z)
    {
        for (int y = 0; y < trunk.dims.y; ++y)
        {
            for (int x = 0; x < trunk.dims.x; ++x)
            {
                const size_t index = voxelIndex(trunk.dims, x, y, z);
                if (trunk.voxels[index] != 0u)
                {
                    continue;
                }

                const glm::vec3 world =
                    trunk.position +
                    (glm::vec3(x, y, z) + glm::vec3(0.5f)) * trunk.scale;
                const glm::vec3 local = world - treeBase;
                bool insideBough = false;
                for (const TaperedBough& bough : kSunroofRadialBoughs)
                {
                    const glm::vec3 axis = bough.endOffset - bough.startOffset;
                    const float lengthSquared = glm::dot(axis, axis);
                    const float t = lengthSquared > 1e-6f
                                        ? std::clamp(
                                              glm::dot(local - bough.startOffset,
                                                       axis) /
                                                  lengthSquared,
                                              0.0f, 1.0f)
                                        : 0.0f;
                    const glm::vec3 nearest = bough.startOffset + axis * t;
                    const float radius =
                        bough.startRadius +
                        (bough.endRadius - bough.startRadius) * t;
                    if (glm::dot(local - nearest, local - nearest) <=
                        radius * radius)
                    {
                        insideBough = true;
                        break;
                    }
                }
                if (!insideBough)
                {
                    continue;
                }

                const uint32_t barkHash =
                    spatialHash(x, y / 4, z, seed + 557u);
                trunk.voxels[index] = static_cast<uint8_t>(
                    NaturePondMaterial::Wood::Base +
                    barkHash % NaturePondMaterial::Wood::Count);
            }
        }
    }
}

bool insideSunroofCanopyWindow(const glm::vec3& local)
{
    for (const CanopyWindow& window : kSunroofCanopyWindows)
    {
        const glm::vec3 normalized =
            (local - window.offset) / window.radii;
        if (glm::dot(normalized, normalized) <= 1.0f)
        {
            return true;
        }
    }
    return false;
}

bool insideSunroofCanopyCleft(const glm::vec3& local, int x, int y, int z,
                              uint32_t seed)
{
    const float radial = glm::length(glm::vec2(local.x, local.z));
    if (radial <= 1.28f || local.y >= 0.92f)
    {
        return false;
    }

    const float seedPhase =
        -0.035f + 0.070f *
                      static_cast<float>(hash32(seed + 563u) & 0xffffu) /
                      65535.0f;
    const float angle = std::atan2(local.z, local.x) + seedPhase;
    const float valley = std::abs(std::cos(3.0f * angle));
    const uint32_t cluster =
        spatialHash(x / 4, y / 3, z / 4, seed + 569u);
    const float width = 0.24f +
                        0.07f * static_cast<float>((cluster >> 9u) & 3u) /
                            3.0f;
    return valley < width;
}

bool shouldCarveSunroofCanopy(const glm::vec3& local, int x, int y, int z,
                              uint32_t seed)
{
    return insideSunroofCanopyWindow(local) ||
           insideSunroofCanopyCleft(local, x, y, z, seed);
}

void carveSunroofCanopy(NaturePondVolumeData& canopy, uint32_t seed)
{
    const glm::vec3 center = volumeCenter(canopy);
    for (int z = 0; z < canopy.dims.z; ++z)
    {
        for (int y = 0; y < canopy.dims.y; ++y)
        {
            for (int x = 0; x < canopy.dims.x; ++x)
            {
                const size_t index = voxelIndex(canopy.dims, x, y, z);
                if (canopy.voxels[index] == 0u)
                {
                    continue;
                }
                const glm::vec3 world =
                    canopy.position +
                    (glm::vec3(x, y, z) + glm::vec3(0.5f)) * canopy.scale;
                if (shouldCarveSunroofCanopy(world - center, x, y, z, seed))
                {
                    canopy.voxels[index] = 0u;
                }
            }
        }
    }
}

void carveSunroofCanopyProxy(NaturePondVolumeData& proxy,
                             const glm::vec3& canopyCenter, uint32_t seed)
{
    for (int z = 0; z < proxy.dims.z; ++z)
    {
        for (int y = 0; y < proxy.dims.y; ++y)
        {
            for (int x = 0; x < proxy.dims.x; ++x)
            {
                const size_t index = voxelIndex(proxy.dims, x, y, z);
                if (proxy.voxels[index] == 0u)
                {
                    continue;
                }
                const glm::vec3 world =
                    proxy.position +
                    (glm::vec3(x, y, z) + glm::vec3(0.5f)) * proxy.scale;
                if (shouldCarveSunroofCanopy(
                        world - canopyCenter, x, y, z, seed))
                {
                    proxy.voxels[index] = 0u;
                }
            }
        }
    }
}

void resnapSunroofCanopySprigs(
    NaturePondBuild& build, const NaturePondVolumeData& canopy)
{
    constexpr int kSearchRadiusCells = 8;
    for (engine::game::FoliageBladeInstance& sprig :
         build.heroCanopyFoliageInstances)
    {
        const glm::vec3 originalRoot = sprig.rootWorld;
        const glm::ivec3 rootCell = glm::ivec3(glm::floor(
            (originalRoot - canopy.position) / canopy.scale));
        float bestDistanceSquared = std::numeric_limits<float>::max();
        glm::vec3 bestCenter = originalRoot;
        bool found = false;
        for (int z = rootCell.z - kSearchRadiusCells;
             z <= rootCell.z + kSearchRadiusCells; ++z)
        {
            for (int y = rootCell.y - kSearchRadiusCells;
                 y <= rootCell.y + kSearchRadiusCells; ++y)
            {
                for (int x = rootCell.x - kSearchRadiusCells;
                     x <= rootCell.x + kSearchRadiusCells; ++x)
                {
                    if (x < 0 || y < 0 || z < 0 || x >= canopy.dims.x ||
                        y >= canopy.dims.y || z >= canopy.dims.z ||
                        canopy.voxels[voxelIndex(canopy.dims, x, y, z)] == 0u)
                    {
                        continue;
                    }
                    const glm::vec3 center =
                        canopy.position +
                        (glm::vec3(x, y, z) + glm::vec3(0.5f)) * canopy.scale;
                    const glm::vec3 delta = center - originalRoot;
                    const float distanceSquared = glm::dot(delta, delta);
                    if (distanceSquared < bestDistanceSquared)
                    {
                        bestDistanceSquared = distanceSquared;
                        bestCenter = center;
                        found = true;
                    }
                }
            }
        }
        if (!found)
        {
            continue;
        }

        glm::vec3 outward = originalRoot - sprig.patchRootWorld;
        const float outwardLength = glm::length(outward);
        outward = outwardLength > 1e-5f
                      ? outward / outwardLength
                      : glm::vec3(0.0f, 1.0f, 0.0f);
        sprig.rootWorld = bestCenter + outward * canopy.scale.x * 0.46f;
    }
}

void clearIslandEcology(NaturePondBuild& build, size_t foliageVolumeIndex)
{
    NaturePondVolumeData& foliage = build.volumes[foliageVolumeIndex];
    uint64_t clearedVoxels = 0u;
    for (int z = 0; z < foliage.dims.z; ++z)
    {
        for (int y = 0; y < foliage.dims.y; ++y)
        {
            for (int x = 0; x < foliage.dims.x; ++x)
            {
                const size_t index = voxelIndex(foliage.dims, x, y, z);
                if (foliage.voxels[index] == 0u)
                {
                    continue;
                }
                const glm::vec3 world =
                    foliage.position +
                    (glm::vec3(x, y, z) + glm::vec3(0.5f)) * foliage.scale;
                if (islandDistance(world.x, world.z) < kEcologyClearRadius)
                {
                    foliage.voxels[index] = 0u;
                    ++clearedVoxels;
                }
            }
        }
    }
    build.foliageOccupiedVoxels -=
        std::min(build.foliageOccupiedVoxels, clearedVoxels);

    build.foliageInstances.erase(
        std::remove_if(
            build.foliageInstances.begin(), build.foliageInstances.end(),
            [](const engine::game::FoliageBladeInstance& instance) {
                return islandDistance(instance.rootWorld.x,
                                      instance.rootWorld.z) <
                       kEcologyClearRadius;
            }),
        build.foliageInstances.end());
}

void flowerCanopy(NaturePondBuild& build, size_t canopyVolumeIndex)
{
    NaturePondVolumeData& canopy = build.volumes[canopyVolumeIndex];
    const glm::vec3 center = volumeCenter(canopy);
    constexpr std::array<glm::ivec3, 6> kNeighbors{
        glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0),
        glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0),
        glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1),
    };
    const auto occupied = [&](int x, int y, int z) {
        return x >= 0 && y >= 0 && z >= 0 && x < canopy.dims.x &&
               y < canopy.dims.y && z < canopy.dims.z &&
               canopy.voxels[voxelIndex(canopy.dims, x, y, z)] != 0u;
    };
    for (int z = 0; z < canopy.dims.z; ++z)
    {
        for (int y = 0; y < canopy.dims.y; ++y)
        {
            for (int x = 0; x < canopy.dims.x; ++x)
            {
                uint8_t& material =
                    canopy.voxels[voxelIndex(canopy.dims, x, y, z)];
                if (material < NaturePondMaterial::Foliage::Base ||
                    material >= NaturePondMaterial::Foliage::Base +
                                    NaturePondMaterial::Foliage::Count)
                {
                    continue;
                }

                uint32_t exposedFaces = 0u;
                for (const glm::ivec3& neighbor : kNeighbors)
                {
                    exposedFaces +=
                        occupied(x + neighbor.x, y + neighbor.y,
                                 z + neighbor.z)
                            ? 0u
                            : 1u;
                }
                if (exposedFaces == 0u)
                {
                    continue;
                }

                const glm::vec3 world =
                    canopy.position +
                    (glm::vec3(x, y, z) + glm::vec3(0.5f)) * canopy.scale;
                const glm::vec3 local = world - center;
                const float heightT = std::clamp(
                    (local.y + 1.65f) / 3.30f, 0.0f, 1.0f);
                const bool underside = local.y < -0.28f;
                const uint32_t cluster =
                    spatialHash(x / 4, y / 3, z / 4, build.seed + 73u);
                const uint32_t detail =
                    spatialHash(x, y, z, build.seed + 79u);
                const uint32_t clusterThreshold =
                    underside
                        ? 30u
                        : static_cast<uint32_t>(
                              58.0f + 30.0f * heightT);
                const uint32_t detailThreshold =
                    std::min(96u, 82u + exposedFaces * 3u);
                if (cluster % 100u < clusterThreshold &&
                    detail % 100u < detailThreshold)
                {
                    material = static_cast<uint8_t>(
                        NaturePondMaterial::Flower::Base + 4u +
                        ((cluster >> 9u) & 1u));
                }
            }
        }
    }

    for (engine::game::FoliageBladeInstance& sprig :
         build.heroCanopyFoliageInstances)
    {
        // material metadata is patch-owned by the instanced foliage contract.
        // vary per canopy lobe, not per sprig, so every member of a patch can
        // still be expanded into one valid gpu geometry payload.
        const uint8_t shade =
            static_cast<uint8_t>(sprig.stablePatchId & 1u);
        sprig.tipMaterialId = static_cast<uint8_t>(
            NaturePondMaterial::Flower::Base + 4u + shade);
    }
}
} // namespace

const NaturePondSunroofProbeLayout& naturePondSunroofProbeLayout()
{
    return kLayout;
}

const NaturePondSunroofGlassLayout& naturePondSunroofGlassLayout()
{
    return kGlassLayout;
}

std::vector<glm::mat4>
naturePondSunroofGlassPaneModels(const SceneConfig& sceneConfig)
{
    if (!isNaturePondSunroofProbeName(sceneConfig.name) ||
        !sceneConfig.enableGlass || kLayout.columnCount < 3u ||
        kGlassLayout.entranceBayIndex >= kLayout.columnCount)
    {
        return {};
    }

    std::vector<glm::mat4> paneModels;
    paneModels.reserve(kLayout.columnCount - 1u);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const float paneHeight = kGlassLayout.paneTop - kGlassLayout.paneBottom;
    const float paneCenterY =
        (kGlassLayout.paneBottom + kGlassLayout.paneTop) * 0.5f;
    for (uint32_t bay = 0; bay < kLayout.columnCount; ++bay)
    {
        if (bay == kGlassLayout.entranceBayIndex)
        {
            continue;
        }

        const float firstAngle =
            kTau * static_cast<float>(bay) /
            static_cast<float>(kLayout.columnCount);
        const float secondAngle =
            kTau * static_cast<float>(bay + 1u) /
            static_cast<float>(kLayout.columnCount);
        const glm::vec3 firstColumn(
            kLayout.centerXZ.x + std::cos(firstAngle) * kColumnCircleRadius,
            paneCenterY,
            kLayout.centerXZ.y + std::sin(firstAngle) * kColumnCircleRadius);
        const glm::vec3 secondColumn(
            kLayout.centerXZ.x + std::cos(secondAngle) * kColumnCircleRadius,
            paneCenterY,
            kLayout.centerXZ.y + std::sin(secondAngle) * kColumnCircleRadius);
        const glm::vec3 paneCenter = (firstColumn + secondColumn) * 0.5f;
        const glm::vec3 tangent = glm::normalize(secondColumn - firstColumn);
        const glm::vec3 outward = glm::normalize(glm::cross(up, tangent));

        glm::mat4 model(1.0f);
        model[0] = glm::vec4(-tangent * kGlassLayout.paneWidth, 0.0f);
        model[1] = glm::vec4(up * paneHeight, 0.0f);
        model[2] = glm::vec4(outward * kGlassLayout.paneThickness, 0.0f);
        model[3] = glm::vec4(paneCenter, 1.0f);
        paneModels.push_back(model);
    }
    return paneModels;
}

const engine::game::FishHabitat& naturePondSunroofProbeFishHabitat()
{
    return kFishHabitat;
}

bool decorateNaturePondSunroofProbe(NaturePondBuild& build)
{
    const std::optional<size_t> trunkIndex =
        findVolume(build, NaturePondVolumeRole::HeroTrunkOpaque);
    const std::optional<size_t> canopyIndex =
        findVolume(build, NaturePondVolumeRole::HeroCanopyTranslucent);
    const std::optional<size_t> canopyProxyIndex =
        findVolume(build, NaturePondVolumeRole::HeroCanopyOcclusionProxy);
    const std::optional<size_t> foliageIndex =
        findVolume(build, NaturePondVolumeRole::FoliageTranslucentMeadow);
    if (!trunkIndex.has_value() || !canopyIndex.has_value() ||
        !canopyProxyIndex.has_value() || !foliageIndex.has_value() ||
        findVolume(build, NaturePondVolumeRole::SunroofArchitectureOpaque)
            .has_value())
    {
        return false;
    }

    clearIslandEcology(build, *foliageIndex);

    const glm::vec2 oldCenter = build.layout.heroTreeCenterXZ;
    const float oldBaseY = build.volumes[*trunkIndex].position.y;
    const glm::vec3 translation(
        kLayout.centerXZ.x - oldCenter.x,
        kLayout.islandSurfaceHeight - oldBaseY,
        kLayout.centerXZ.y - oldCenter.y);
    build.volumes[*trunkIndex].position += translation;
    build.volumes[*canopyIndex].position += translation;
    build.volumes[*canopyProxyIndex].position += translation;
    for (engine::game::FoliageBladeInstance& sprig :
         build.heroCanopyFoliageInstances)
    {
        sprig.rootWorld += translation;
        sprig.patchRootWorld += translation;
    }
    build.layout.heroTreeCenterXZ = kLayout.centerXZ;

    NaturePondVolumeData& trunk = build.volumes[*trunkIndex];
    NaturePondVolumeData& canopy = build.volumes[*canopyIndex];
    NaturePondVolumeData& canopyProxy = build.volumes[*canopyProxyIndex];
    addSunroofRadialBoughs(trunk, build.seed);
    carveSunroofCanopy(canopy, build.seed);
    carveSunroofCanopyProxy(canopyProxy, volumeCenter(canopy), build.seed);
    resnapSunroofCanopySprigs(build, canopy);
    flowerCanopy(build, *canopyIndex);
    build.heroTrunkOccupiedVoxels = countOccupied(trunk);
    build.heroCanopyOccupiedVoxels = countOccupied(canopy);
    build.heroCanopyProxyOccupiedVoxels = countOccupied(canopyProxy);

    NaturePondVolumeData architecture = buildIslandAndPavilion(build.seed);
    build.sunroofArchitectureOccupiedVoxels = countOccupied(architecture);
    build.volumes.push_back(std::move(architecture));
    return true;
}
