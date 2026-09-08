#include "engine/scene/NaturePondGenerator.h"

#include "engine/scene/NaturePondFoliageDistribution.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace
{
constexpr glm::ivec3 kTerrainDims{96, 30, 80};
constexpr glm::vec3 kTerrainPosition{-12.0f, -2.0f, -10.0f};
constexpr glm::vec3 kTerrainScale{0.25f};
constexpr glm::vec3 kDetailPosition{-12.0f, 2.0f, -10.0f};
constexpr glm::vec3 kDetailWorldExtent{24.0f, 5.0f, 20.0f};
constexpr glm::vec3 kHeroCanopyExtent{6.0f, 4.4f, 6.0f};
constexpr glm::vec3 kHeroCanopyProxyExtent{5.0f, 3.4f, 5.0f};
constexpr float kHeroCanopyProxyPitch = 0.5f;
constexpr float kHeroCanopyCenterHeight = 5.15f;
constexpr float kFoliageCandidateSpacing = 0.3f;

struct HeroCanopyLobe
{
    glm::vec3 offset{0.0f};
    glm::vec3 radii{1.0f};
};

constexpr std::array<HeroCanopyLobe, 8> kHeroCanopyLobes{
    HeroCanopyLobe{{-0.10f, 0.64f, -0.08f}, {1.62f, 1.42f, 1.52f}},
    HeroCanopyLobe{{-1.52f, 0.08f, 0.18f}, {1.42f, 1.30f, 1.48f}},
    HeroCanopyLobe{{1.50f, 0.04f, 0.12f}, {1.43f, 1.28f, 1.44f}},
    HeroCanopyLobe{{-0.78f, -0.34f, 1.34f}, {1.34f, 1.08f, 1.30f}},
    HeroCanopyLobe{{0.82f, -0.30f, 1.38f}, {1.36f, 1.10f, 1.26f}},
    HeroCanopyLobe{{-0.84f, 0.02f, -1.34f}, {1.28f, 1.20f, 1.25f}},
    HeroCanopyLobe{{0.90f, 0.00f, -1.30f}, {1.30f, 1.18f, 1.24f}},
    HeroCanopyLobe{{0.02f, -0.46f, 0.08f}, {1.30f, 1.00f, 1.20f}},
};

constexpr std::array<HeroCanopyLobe, 4> kHeroCanopySilhouettePockets{
    HeroCanopyLobe{{-0.88f, -1.32f, 1.66f}, {0.56f, 0.72f, 0.64f}},
    HeroCanopyLobe{{0.94f, -1.28f, 1.58f}, {0.52f, 0.70f, 0.60f}},
    HeroCanopyLobe{{-1.12f, 1.72f, 0.48f}, {0.34f, 0.64f, 0.56f}},
    HeroCanopyLobe{{1.02f, 1.70f, -0.24f}, {0.34f, 0.62f, 0.54f}},
};

// Outward/tangent/vertical directions distribute nine sparse moving sprigs on
// each of the seven visible crown lobes. the low connector lobe stays static.
constexpr std::array<glm::vec3, 9> kHeroCanopySprigDirections{
    glm::vec3(1.00f, 0.00f, -0.52f),
    glm::vec3(1.00f, 0.44f, -0.24f),
    glm::vec3(1.00f, -0.44f, -0.20f),
    glm::vec3(0.88f, 0.60f, 0.14f),
    glm::vec3(0.88f, -0.60f, 0.20f),
    glm::vec3(0.62f, 0.54f, 0.64f),
    glm::vec3(0.62f, -0.54f, 0.70f),
    glm::vec3(0.28f, 0.42f, 0.92f),
    glm::vec3(0.28f, -0.42f, 0.96f),
};

struct NaturePondGenerationDomain
{
    glm::ivec3 terrainDims{kTerrainDims};
    glm::vec3 terrainPosition{kTerrainPosition};
    glm::vec3 detailPosition{kDetailPosition};
    glm::vec3 detailExtent{kDetailWorldExtent};
    int rockCandidateMinX = 1;
    int rockCandidateMaxX = 28;
    int rockCandidateMinZ = 1;
    int rockCandidateMaxZ = 23;
    int foliageCandidateMinX = 1;
    int foliageCandidateMaxX = 78;
    int foliageCandidateMinZ = 1;
    int foliageCandidateMaxZ = 65;
    float scatterDensityMultiplier = 1.0f;
};

constexpr std::array<NaturePondDetailTierInfo, 3> kDetailTiers{
    NaturePondDetailTierInfo{NaturePondDetailTier::Reference10Cm, "nature_pond_probe",
                             "0.10 m reference", 0.10f},
    NaturePondDetailTierInfo{NaturePondDetailTier::Balanced20Cm,
                             "nature_pond_20cm_probe", "0.20 m balanced", 0.20f},
    NaturePondDetailTierInfo{NaturePondDetailTier::Coarse25Cm,
                             "nature_pond_25cm_probe", "0.25 m coarse", 0.25f},
};

constexpr std::array<NaturePondDensityStepInfo, 3> kDensitySteps{
    NaturePondDensityStepInfo{
        NaturePondDensityStep::Baseline,
        "baseline",
        "nature_pond_density_base_probe",
        "nature_pond_density_base_bank_probe",
        "baseline coverage / baseline scatter",
        glm::vec2(24.0f, 20.0f),
        1.0f},
    NaturePondDensityStepInfo{
        NaturePondDensityStep::ExpandedCoverage,
        "expanded-coverage",
        "nature_pond_density_coverage_probe",
        "nature_pond_density_coverage_bank_probe",
        "1.5625x coverage / baseline scatter",
        glm::vec2(30.0f, 25.0f),
        1.0f},
    NaturePondDensityStepInfo{
        NaturePondDensityStep::ExpandedCoverageDenseScatter,
        "expanded-dense",
        "nature_pond_density_dense_probe",
        "nature_pond_density_dense_bank_probe",
        "1.5625x coverage / 1.5x scatter",
        glm::vec2(30.0f, 25.0f),
        1.5f},
};

constexpr NaturePondLayout kLayout{
    glm::vec3(-6.55f, 0.55f, -5.05f),
    glm::vec3(6.55f, 3.18f, 5.05f),
    3.18f,
    glm::vec2(-8.8f, -0.8f),
    engine::game::FishHabitat{
        glm::vec3(-3.35f, 1.05f, -2.25f),
        glm::vec3(3.35f, 2.82f, 2.25f),
        glm::vec3(0.0f, 1.94f, 0.0f),
    },
};

constexpr std::array<NaturePondCameraAnchor, 3> kCameraAnchors{
    NaturePondCameraAnchor{"meadow_overview", glm::vec3(0.0f, 8.2f, 17.5f), 0.0f, -0.27f},
    NaturePondCameraAnchor{"pond_bank_waterline", glm::vec3(8.6f, 4.7f, 8.7f), -0.68f,
                           -0.18f},
    NaturePondCameraAnchor{"underwater_fish", glm::vec3(0.0f, 2.05f, 3.15f), 0.0f, -0.02f},
};

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

NaturePondGenerationDomain generationDomain(NaturePondDensityStep step)
{
    NaturePondGenerationDomain domain{};
    if (step == NaturePondDensityStep::Baseline)
    {
        return domain;
    }

    domain.terrainDims = glm::ivec3(120, kTerrainDims.y, 100);
    domain.terrainPosition = glm::vec3(-15.0f, kTerrainPosition.y, -12.5f);
    domain.detailPosition = glm::vec3(-15.0f, kDetailPosition.y, -12.5f);
    domain.detailExtent = glm::vec3(30.0f, kDetailWorldExtent.y, 25.0f);
    domain.rockCandidateMinX = -3;
    domain.rockCandidateMaxX = 32;
    domain.rockCandidateMinZ = -2;
    domain.rockCandidateMaxZ = 26;
    domain.foliageCandidateMinX = -9;
    domain.foliageCandidateMaxX = 88;
    domain.foliageCandidateMinZ = -7;
    domain.foliageCandidateMaxZ = 73;
    if (step == NaturePondDensityStep::ExpandedCoverageDenseScatter)
    {
        domain.scatterDensityMultiplier = 1.5f;
    }
    return domain;
}

glm::ivec3 detailDims(float pitch, const NaturePondGenerationDomain& domain)
{
    return glm::ivec3(static_cast<int>(std::ceil(domain.detailExtent.x / pitch)),
                      static_cast<int>(std::ceil(domain.detailExtent.y / pitch)),
                      static_cast<int>(std::ceil(domain.detailExtent.z / pitch)));
}

glm::ivec3 dimsForExtent(const glm::vec3& extent, float pitch)
{
    return glm::ivec3(static_cast<int>(std::ceil(extent.x / pitch)),
                      static_cast<int>(std::ceil(extent.y / pitch)),
                      static_cast<int>(std::ceil(extent.z / pitch)));
}

int worldToDetailCell(float worldValue, float origin, float pitch)
{
    return static_cast<int>(std::round((worldValue - origin) / pitch));
}

int worldCenterToDetailCell(float worldValue, float origin, float pitch)
{
    return static_cast<int>(std::floor((worldValue - origin) / pitch));
}

uint64_t countOccupied(const NaturePondVolumeData& volume)
{
    uint64_t occupied = 0;
    for (uint8_t voxel : volume.voxels)
    {
        occupied += voxel != 0u ? 1u : 0u;
    }
    return occupied;
}

uint64_t countMaterialBand(const NaturePondVolumeData& volume, uint8_t base,
                           uint8_t count)
{
    uint64_t occupied = 0;
    const uint16_t end = static_cast<uint16_t>(base) + count;
    for (uint8_t voxel : volume.voxels)
    {
        occupied += voxel >= base && static_cast<uint16_t>(voxel) < end ? 1u : 0u;
    }
    return occupied;
}

size_t voxelIndex(const glm::ivec3& dims, int x, int y, int z)
{
    return static_cast<size_t>(x) + static_cast<size_t>(dims.x) *
                                        (static_cast<size_t>(y) +
                                         static_cast<size_t>(dims.y) * static_cast<size_t>(z));
}

void setVoxel(NaturePondVolumeData& volume, int x, int y, int z, uint8_t material)
{
    if (x < 0 || y < 0 || z < 0 || x >= volume.dims.x || y >= volume.dims.y ||
        z >= volume.dims.z)
    {
        return;
    }
    volume.voxels[voxelIndex(volume.dims, x, y, z)] = material;
}

float pondDistance(float worldX, float worldZ)
{
    const float nx = worldX / 6.5f;
    const float nz = worldZ / 5.0f;
    return std::sqrt(nx * nx + nz * nz);
}

int canonicalTerrainX(const NaturePondGenerationDomain& domain, int localX)
{
    const float worldCenter =
        domain.terrainPosition.x + (static_cast<float>(localX) + 0.5f) * kTerrainScale.x;
    return static_cast<int>(std::round(
        (worldCenter - kTerrainPosition.x) / kTerrainScale.x - 0.5f));
}

int canonicalTerrainZ(const NaturePondGenerationDomain& domain, int localZ)
{
    const float worldCenter =
        domain.terrainPosition.z + (static_cast<float>(localZ) + 0.5f) * kTerrainScale.z;
    return static_cast<int>(std::round(
        (worldCenter - kTerrainPosition.z) / kTerrainScale.z - 0.5f));
}

int terrainHeightCell(int x, int z, uint32_t seed,
                      const NaturePondGenerationDomain& domain)
{
    const float worldX =
        domain.terrainPosition.x + (static_cast<float>(x) + 0.5f) * kTerrainScale.x;
    const float worldZ =
        domain.terrainPosition.z + (static_cast<float>(z) + 0.5f) * kTerrainScale.z;
    const float distance = pondDistance(worldX, worldZ);
    const uint32_t h =
        spatialHash(canonicalTerrainX(domain, x), 0,
                    canonicalTerrainZ(domain, z), seed);

    if (distance < 0.72f)
    {
        return 10 + static_cast<int>(h % 2u);
    }
    if (distance < 1.08f)
    {
        const float t = (distance - 0.72f) / 0.36f;
        return 11 + static_cast<int>(std::floor(t * 12.0f));
    }

    const float broadUndulation =
        std::sin(worldX * 0.31f) * 0.7f + std::cos(worldZ * 0.27f) * 0.65f;
    const int hashJitter = static_cast<int>((h >> 8u) % 3u) - 1;
    return std::clamp(23 + static_cast<int>(std::round(broadUndulation)) + hashJitter, 21,
                      domain.terrainDims.y - 2);
}

float terrainSurfaceAt(float worldX, float worldZ, uint32_t seed,
                       const NaturePondGenerationDomain& domain)
{
    const int x = std::clamp(
        static_cast<int>((worldX - domain.terrainPosition.x) / kTerrainScale.x),
        0, domain.terrainDims.x - 1);
    const int z = std::clamp(
        static_cast<int>((worldZ - domain.terrainPosition.z) / kTerrainScale.z),
        0, domain.terrainDims.z - 1);
    return domain.terrainPosition.y +
           static_cast<float>(terrainHeightCell(x, z, seed, domain) + 1) *
               kTerrainScale.y;
}

// semantic patch anchors may sit just outside the packed volume that contains one
// of their accepted plants. resolve their height on the unbounded canonical terrain
// lattice so expanding the generation domain cannot move an existing patch.
float canonicalTerrainSurfaceAt(float worldX, float worldZ, uint32_t seed)
{
    const int canonicalX = static_cast<int>(std::floor(
        (worldX - kTerrainPosition.x) / kTerrainScale.x));
    const int canonicalZ = static_cast<int>(std::floor(
        (worldZ - kTerrainPosition.z) / kTerrainScale.z));
    const float centerX =
        kTerrainPosition.x +
        (static_cast<float>(canonicalX) + 0.5f) * kTerrainScale.x;
    const float centerZ =
        kTerrainPosition.z +
        (static_cast<float>(canonicalZ) + 0.5f) * kTerrainScale.z;
    const float distance = pondDistance(centerX, centerZ);
    const uint32_t h = spatialHash(canonicalX, 0, canonicalZ, seed);

    int height = 0;
    if (distance < 0.72f)
    {
        height = 10 + static_cast<int>(h % 2u);
    }
    else if (distance < 1.08f)
    {
        const float t = (distance - 0.72f) / 0.36f;
        height = 11 + static_cast<int>(std::floor(t * 12.0f));
    }
    else
    {
        const float broadUndulation =
            std::sin(centerX * 0.31f) * 0.7f +
            std::cos(centerZ * 0.27f) * 0.65f;
        const int hashJitter = static_cast<int>((h >> 8u) % 3u) - 1;
        height = std::clamp(
            23 + static_cast<int>(std::round(broadUndulation)) + hashJitter,
            21, kTerrainDims.y - 2);
    }
    return kTerrainPosition.y +
           static_cast<float>(height + 1) * kTerrainScale.y;
}

NaturePondVolumeData makeVolume(std::string name, const glm::ivec3& dims,
                                const glm::vec3& position, const glm::vec3& scale,
                                NaturePondVolumeRole role)
{
    NaturePondVolumeData result{};
    result.name = std::move(name);
    result.dims = dims;
    result.position = position;
    result.scale = scale;
    result.role = role;
    result.voxels.resize(static_cast<size_t>(dims.x) * static_cast<size_t>(dims.y) *
                         static_cast<size_t>(dims.z));
    return result;
}

NaturePondVolumeData buildTerrain(uint32_t seed,
                                  const NaturePondGenerationDomain& domain)
{
    NaturePondVolumeData terrain = makeVolume("NaturePondTerrain", domain.terrainDims,
                                               domain.terrainPosition, kTerrainScale,
                                               NaturePondVolumeRole::TerrainOpaque);
    for (int z = 0; z < terrain.dims.z; ++z)
    {
        for (int x = 0; x < terrain.dims.x; ++x)
        {
            const int top = terrainHeightCell(x, z, seed, domain);
            const float worldX = terrain.position.x + (static_cast<float>(x) + 0.5f) * terrain.scale.x;
            const float worldZ = terrain.position.z + (static_cast<float>(z) + 0.5f) * terrain.scale.z;
            const float distance = pondDistance(worldX, worldZ);
            const int canonicalX = canonicalTerrainX(domain, x);
            const int canonicalZ = canonicalTerrainZ(domain, z);
            for (int y = 0; y <= top; ++y)
            {
                uint8_t material = static_cast<uint8_t>(
                    NaturePondMaterial::Soil::Base +
                    spatialHash(canonicalX, y, canonicalZ, seed) %
                        NaturePondMaterial::Soil::Count);
                if (y >= top - 1)
                {
                    if (distance < 1.03f)
                    {
                        material = static_cast<uint8_t>(
                            NaturePondMaterial::Gravel::Base +
                            spatialHash(canonicalX, y, canonicalZ, seed + 31u) %
                                NaturePondMaterial::Gravel::Count);
                    }
                    else
                    {
                        material = static_cast<uint8_t>(
                            NaturePondMaterial::Meadow::Base +
                            spatialHash(canonicalX, y, canonicalZ, seed + 47u) %
                                NaturePondMaterial::Meadow::Count);
                    }
                }
                setVoxel(terrain, x, y, z, material);
            }
        }
    }
    return terrain;
}

void addLightingParityAlcove(NaturePondVolumeData& details, uint32_t seed,
                            float pitch,
                            const NaturePondGenerationDomain& domain)
{
    const glm::vec3 lightPosition =
        NaturePondLightingParity::FireLightPosition;
    const int centerX =
        worldCenterToDetailCell(lightPosition.x, details.position.x, pitch);
    const int centerZ =
        worldCenterToDetailCell(lightPosition.z, details.position.z, pitch);
    const int groundY = worldToDetailCell(
        terrainSurfaceAt(lightPosition.x, lightPosition.z, seed, domain),
        details.position.y, pitch);
    const int halfWidth =
        std::max(2, static_cast<int>(std::round(1.25f / pitch)));
    const int wallHeight =
        std::max(3, static_cast<int>(std::round(1.75f / pitch)));
    const int halfDepth =
        std::max(2, static_cast<int>(std::round(0.70f / pitch)));
    const int wallThickness =
        std::max(1, static_cast<int>(std::round(0.20f / pitch)));

    auto stoneAt = [&](int x, int y, int z) {
        const uint8_t stone = static_cast<uint8_t>(
            NaturePondMaterial::Stone::Base +
            spatialHash(x, y, z, seed + 503u) %
                NaturePondMaterial::Stone::Count);
        setVoxel(details, x, y, z, stone);
    };

    // the camera faces -Z. build a shallow stone hood behind and around the
    // emissive cells so direct colored illumination and missing bounce are both
    // readable on neutral surfaces.
    for (int x = centerX - halfWidth; x <= centerX + halfWidth; ++x)
    {
        for (int z = centerZ - halfDepth; z <= centerZ + halfDepth; ++z)
        {
            stoneAt(x, groundY, z);
        }
    }
    for (int y = groundY + 1; y <= groundY + wallHeight; ++y)
    {
        for (int x = centerX - halfWidth; x <= centerX + halfWidth; ++x)
        {
            for (int thickness = 0; thickness < wallThickness; ++thickness)
            {
                stoneAt(x, y, centerZ - halfDepth + thickness);
            }
        }
        for (int z = centerZ - halfDepth; z <= centerZ + halfDepth; ++z)
        {
            for (int thickness = 0; thickness < wallThickness; ++thickness)
            {
                stoneAt(centerX - halfWidth + thickness, y, z);
                stoneAt(centerX + halfWidth - thickness, y, z);
            }
        }
    }
    for (int x = centerX - halfWidth; x <= centerX + halfWidth; ++x)
    {
        for (int z = centerZ - halfDepth; z <= centerZ + halfDepth; ++z)
        {
            stoneAt(x, groundY + wallHeight, z);
        }
    }

    const int emberRadius =
        std::max(1, static_cast<int>(std::round(0.38f / pitch)));
    const int emberHeight =
        std::max(2, static_cast<int>(std::round(0.65f / pitch)));
    for (int y = 1; y <= emberHeight; ++y)
    {
        const int layerRadius = std::max(
            1, emberRadius - (y * emberRadius) / (emberHeight + 1));
        for (int z = -layerRadius; z <= layerRadius; ++z)
        {
            for (int x = -layerRadius; x <= layerRadius; ++x)
            {
                if (x * x + z * z > layerRadius * layerRadius + 1)
                {
                    continue;
                }
                const uint8_t ember = static_cast<uint8_t>(
                    NaturePondMaterial::Ember::Base +
                    spatialHash(x, y, z, seed + 521u) %
                        NaturePondMaterial::Ember::Count);
                setVoxel(details, centerX + x, groundY + y, centerZ + z,
                         ember);
            }
        }
    }
}

NaturePondVolumeData buildOpaqueDetails(uint32_t seed, float pitch,
                                        bool includeLightingParityProbe,
                                        const NaturePondGenerationDomain& domain)
{
    const glm::ivec3 dims = detailDims(pitch, domain);
    NaturePondVolumeData details =
        makeVolume("NaturePondOpaqueDetails", dims, domain.detailPosition,
                   glm::vec3(pitch),
                   NaturePondVolumeRole::OpaqueDetails);

    constexpr float kRockCandidateSpacing = 0.8f;
    const uint32_t rockThreshold = static_cast<uint32_t>(std::clamp(
        static_cast<int>(std::round(8.0f * domain.scatterDensityMultiplier)),
        0, 100));
    for (int candidateZ = domain.rockCandidateMinZ;
         candidateZ <= domain.rockCandidateMaxZ; ++candidateZ)
    {
        for (int candidateX = domain.rockCandidateMinX;
             candidateX <= domain.rockCandidateMaxX; ++candidateX)
        {
            const float worldX = kDetailPosition.x + 0.05f +
                                 static_cast<float>(candidateX) * kRockCandidateSpacing;
            const float worldZ = kDetailPosition.z + 0.05f +
                                 static_cast<float>(candidateZ) * kRockCandidateSpacing;
            const bool acceptedBaselineCandidate =
                candidateX >= 1 && candidateX <= 28 && candidateZ >= 1 &&
                candidateZ <= 23;
            constexpr float kMaximumRockRadiusMeters = 0.4f;
            const bool safelyOutsideBaselinePatch =
                worldX < kDetailPosition.x - kMaximumRockRadiusMeters ||
                worldX >= kDetailPosition.x + kDetailWorldExtent.x +
                              kMaximumRockRadiusMeters ||
                worldZ < kDetailPosition.z - kMaximumRockRadiusMeters ||
                worldZ >= kDetailPosition.z + kDetailWorldExtent.z +
                              kMaximumRockRadiusMeters;
            if (!acceptedBaselineCandidate && !safelyOutsideBaselinePatch)
            {
                continue;
            }
            if (pondDistance(worldX, worldZ) < 1.03f)
            {
                continue;
            }

            const uint32_t h =
                spatialHash(candidateX * 8, 7, candidateZ * 8, seed + 103u);
            if (h % 100u >= rockThreshold)
            {
                continue;
            }

            const int x = worldCenterToDetailCell(worldX, details.position.x, pitch);
            const int z = worldCenterToDetailCell(worldZ, details.position.z, pitch);
            const int baseY = worldToDetailCell(
                terrainSurfaceAt(worldX, worldZ, seed, domain),
                details.position.y, pitch);
            const float radiusMeters = 0.20f + 0.10f * static_cast<float>((h >> 8u) % 3u);
            const float heightMeters = 0.20f + 0.10f * static_cast<float>((h >> 12u) % 3u);
            const int radius = std::max(1, static_cast<int>(std::round(radiusMeters / pitch)));
            const int height = std::max(1, static_cast<int>(std::round(heightMeters / pitch)));
            for (int dz = -radius; dz <= radius; ++dz)
            {
                for (int dy = 0; dy <= height; ++dy)
                {
                    for (int dx = -radius; dx <= radius; ++dx)
                    {
                        const float normalized =
                            static_cast<float>(dx * dx + dz * dz) /
                                static_cast<float>(radius * radius + 1) +
                            static_cast<float>(dy * dy) /
                                static_cast<float>(height * height + 1);
                        if (normalized <= 1.0f)
                        {
                            setVoxel(details, x + dx, baseY + dy, z + dz,
                                     static_cast<uint8_t>(
                                         NaturePondMaterial::Stone::Base +
                                         spatialHash(candidateX * 8 + dx, dy,
                                                     candidateZ * 8 + dz,
                                                     seed + 109u) %
                                             NaturePondMaterial::Stone::Count));
                        }
                    }
                }
            }
        }
    }

    // one readable fallen branch gives every tier the same physical landmark.
    constexpr float kBranchStartX = 4.4f;
    constexpr float kBranchEndX = 8.4f;
    constexpr float kBranchStartZ = 5.1f;
    const int branchSteps = std::max(1, static_cast<int>(std::round(
                                            (kBranchEndX - kBranchStartX) / pitch)));
    const int branchThickness = std::max(1, static_cast<int>(std::round(0.20f / pitch)));
    for (int step = 0; step <= branchSteps; ++step)
    {
        const float t = static_cast<float>(step) / static_cast<float>(branchSteps);
        const float worldX = kBranchStartX + (kBranchEndX - kBranchStartX) * t;
        const float worldZ = kBranchStartZ + 0.8f * t;
        const int x = worldToDetailCell(worldX, details.position.x, pitch);
        const int z = worldToDetailCell(worldZ, details.position.z, pitch);
        const int y = worldToDetailCell(terrainSurfaceAt(worldX, worldZ, seed, domain),
                                        details.position.y, pitch);
        for (int thickness = 0; thickness < branchThickness; ++thickness)
        {
            setVoxel(
                details, x, y + thickness, z,
                static_cast<uint8_t>(
                    NaturePondMaterial::Wood::Base +
                    ((step + thickness) % NaturePondMaterial::Wood::Count)));
        }
    }
    if (includeLightingParityProbe)
    {
        addLightingParityAlcove(details, seed, pitch, domain);
    }
    return details;
}

uint32_t foliageMaterialOffset(
    const engine::scene::NaturePondFoliagePlacement& placement)
{
    switch (placement.kind)
    {
    case engine::game::FoliagePatchKind::GrassTuft:
        return placement.patchSeed % 3u;
    case engine::game::FoliagePatchKind::Shrub:
        return placement.patchSeed % 2u;
    case engine::game::FoliagePatchKind::FlowerCluster:
        return 1u + placement.patchSeed % 3u;
    case engine::game::FoliagePatchKind::ReedCluster:
        return placement.patchSeed % 3u;
    case engine::game::FoliagePatchKind::WaterLilyCluster:
        return 3u + placement.patchSeed % 5u;
    case engine::game::FoliagePatchKind::TreeCanopyCluster:
        return placement.patchSeed % NaturePondMaterial::Foliage::Count;
    case engine::game::FoliagePatchKind::Count:
        break;
    }
    return 0u;
}

uint8_t foliageBlossomMaterial(
    const engine::scene::NaturePondFoliagePlacement& placement)
{
    if (placement.kind != engine::game::FoliagePatchKind::FlowerCluster &&
        placement.kind != engine::game::FoliagePatchKind::WaterLilyCluster)
    {
        return 0u;
    }
    // patch metadata owns one accent material, keeping each colony coherent and
    // preserving the one-batch patch contract. a fully mixed patch hash spreads
    // neighboring colonies across all four two-shade color families without
    // adding geometry, material storage, or draw work.
    return static_cast<uint8_t>(
        NaturePondMaterial::Flower::Base +
        hash32(placement.patchSeed ^ 0x6a09e667u) %
            NaturePondMaterial::Flower::Count);
}

engine::game::FoliageBladeInstance makeFoliageInstance(
    uint32_t seed, float pitch, const NaturePondGenerationDomain& domain,
    const NaturePondVolumeData& foliage,
    const engine::scene::NaturePondFoliagePlacement& placement,
    uint64_t stableId, float heightMeters, int leanCells, uint8_t stem,
    uint8_t secondaryStem, uint8_t blossom, uint32_t flags)
{
    const float worldX =
        kDetailPosition.x + 0.05f +
        static_cast<float>(placement.plantCandidateX) *
            kFoliageCandidateSpacing;
    const float worldZ =
        kDetailPosition.z + 0.05f +
        static_cast<float>(placement.plantCandidateZ) *
            kFoliageCandidateSpacing;
    const int x = worldCenterToDetailCell(worldX, foliage.position.x, pitch);
    const int z = worldCenterToDetailCell(worldZ, foliage.position.z, pitch);
    const int baseY = worldToDetailCell(
        terrainSurfaceAt(worldX, worldZ, seed, domain), foliage.position.y,
        pitch);

    const float patchWorldX =
        kDetailPosition.x + 0.05f +
        static_cast<float>(placement.patchAnchorCandidateX) *
            kFoliageCandidateSpacing;
    const float patchWorldZ =
        kDetailPosition.z + 0.05f +
        static_cast<float>(placement.patchAnchorCandidateZ) *
            kFoliageCandidateSpacing;
    const int patchBaseY = worldToDetailCell(
        canonicalTerrainSurfaceAt(patchWorldX, patchWorldZ, seed),
        foliage.position.y, pitch);

    engine::game::FoliageBladeInstance instance{};
    instance.stableId = stableId;
    instance.stablePatchId = placement.stablePatchId;
    instance.rootWorld =
        foliage.position +
        glm::vec3(static_cast<float>(x) + 0.5f,
                  static_cast<float>(baseY),
                  static_cast<float>(z) + 0.5f) *
            pitch;
    instance.patchRootWorld =
        glm::vec3(patchWorldX,
                  foliage.position.y + static_cast<float>(patchBaseY) * pitch,
                  patchWorldZ);
    instance.heightMeters = heightMeters;
    instance.cellSizeMeters = pitch;
    instance.patchRadiusMeters =
        static_cast<float>(std::max(placement.patchRadiusCandidateX,
                                    placement.patchRadiusCandidateZ)) *
            kFoliageCandidateSpacing +
        0.15f;
    instance.restLeanMeters = static_cast<float>(leanCells) * pitch;
    constexpr float kHalfPi = 1.57079632679489661923f;
    instance.yawRadians =
        kHalfPi * static_cast<float>(placement.patchSeed & 0x3u);

    const engine::scene::EnvironmentWindSettings& windSettings =
        engine::scene::defaultEnvironmentWindSettings();
    const engine::scene::EnvironmentWindWaveShape& windShape =
        engine::scene::defaultEnvironmentWindWaveShape();
    const glm::vec2 wind = windSettings.direction;
    const glm::vec2 crossWind(-wind.y, wind.x);
    const glm::vec2 rootXZ(instance.patchRootWorld.x,
                           instance.patchRootWorld.z);
    const float phaseJitter =
        -0.04f + 0.08f *
                     static_cast<float>((placement.patchSeed >> 16u) &
                                        0xffffu) /
                     65535.0f;
    instance.phaseRadians =
        glm::dot(rootXZ, wind) * windShape.alongPhaseRadiansPerMeter +
        glm::dot(rootXZ, crossWind) * windShape.crossPhaseRadiansPerMeter +
        phaseJitter;
    instance.randomSeed = placement.plantSeed;
    instance.patchSeed = placement.patchSeed;
    instance.stemMaterialId = stem;
    instance.secondaryMaterialId = secondaryStem;
    instance.tipMaterialId = blossom;
    instance.flags = flags;
    instance.patchKind = placement.kind;
    instance.morphology = engine::game::foliageMorphologyForPatch(
        placement.kind, placement.patchSeed);
    return instance;
}

engine::game::FoliageBladeInstance makeAquaticFoliageInstance(
    float pitch, const engine::scene::NaturePondFoliagePlacement& placement,
    uint64_t stableId, uint8_t stem, uint8_t secondaryStem, uint8_t blossom,
    bool flowering)
{
    const float worldX =
        kDetailPosition.x + 0.05f +
        static_cast<float>(placement.plantCandidateX) *
            kFoliageCandidateSpacing;
    const float worldZ =
        kDetailPosition.z + 0.05f +
        static_cast<float>(placement.plantCandidateZ) *
            kFoliageCandidateSpacing;
    const float patchWorldX =
        kDetailPosition.x + 0.05f +
        static_cast<float>(placement.patchAnchorCandidateX) *
            kFoliageCandidateSpacing;
    const float patchWorldZ =
        kDetailPosition.z + 0.05f +
        static_cast<float>(placement.patchAnchorCandidateZ) *
            kFoliageCandidateSpacing;
    constexpr float kSurfaceClearance = 0.012f;

    engine::game::FoliageBladeInstance instance{};
    instance.stableId = stableId;
    instance.stablePatchId = placement.stablePatchId;
    instance.rootWorld = glm::vec3(
        worldX, kLayout.pondSurfaceHeight + kSurfaceClearance, worldZ);
    instance.patchRootWorld = glm::vec3(
        patchWorldX, kLayout.pondSurfaceHeight + kSurfaceClearance,
        patchWorldZ);
    instance.heightMeters = flowering ? 0.10f : 0.055f;
    instance.cellSizeMeters = pitch;
    instance.patchRadiusMeters =
        static_cast<float>(std::max(placement.patchRadiusCandidateX,
                                    placement.patchRadiusCandidateZ)) *
            kFoliageCandidateSpacing +
        0.15f;
    instance.restLeanMeters = 0.0f;
    constexpr float kQuarterPi = 0.78539816339744830962f;
    instance.yawRadians =
        kQuarterPi * static_cast<float>(placement.patchSeed & 0x7u);

    const engine::scene::EnvironmentWindSettings& windSettings =
        engine::scene::defaultEnvironmentWindSettings();
    const engine::scene::EnvironmentWindWaveShape& windShape =
        engine::scene::defaultEnvironmentWindWaveShape();
    const glm::vec2 wind = windSettings.direction;
    const glm::vec2 crossWind(-wind.y, wind.x);
    const glm::vec2 rootXZ(instance.patchRootWorld.x,
                           instance.patchRootWorld.z);
    const float phaseJitter =
        -0.04f + 0.08f *
                     static_cast<float>((placement.patchSeed >> 16u) &
                                        0xffffu) /
                     65535.0f;
    instance.phaseRadians =
        glm::dot(rootXZ, wind) * windShape.alongPhaseRadiansPerMeter +
        glm::dot(rootXZ, crossWind) * windShape.crossPhaseRadiansPerMeter +
        phaseJitter;
    instance.randomSeed = placement.plantSeed;
    instance.patchSeed = placement.patchSeed;
    instance.stemMaterialId = stem;
    instance.secondaryMaterialId = secondaryStem;
    instance.tipMaterialId = blossom;
    instance.flags = engine::game::FoliageBladeAquatic |
                     (flowering ? engine::game::FoliageBladeFlower : 0u);
    instance.patchKind = placement.kind;
    instance.morphology = engine::game::foliageMorphologyForPatch(
        placement.kind, placement.patchSeed);
    return instance;
}

NaturePondVolumeData buildFoliage(uint32_t seed, float pitch,
                                  bool includeLightingParityProbe,
                                  const NaturePondGenerationDomain& domain,
                                  std::vector<engine::game::FoliageBladeInstance>& instances)
{
    const glm::ivec3 dims = detailDims(pitch, domain);
    NaturePondVolumeData foliage =
        makeVolume("NaturePondFoliage", dims, domain.detailPosition,
                   glm::vec3(pitch),
                   NaturePondVolumeRole::FoliageTranslucentMeadow);

    const uint32_t scatterDensityPermille = static_cast<uint32_t>(
        std::clamp(std::lround(domain.scatterDensityMultiplier * 1000.0f),
                   0l, 2000l));
    instances.clear();
    instances.reserve(static_cast<size_t>(
        (domain.foliageCandidateMaxX - domain.foliageCandidateMinX + 1) *
        (domain.foliageCandidateMaxZ - domain.foliageCandidateMinZ + 1)));
    for (int candidateZ = domain.foliageCandidateMinZ;
         candidateZ <= domain.foliageCandidateMaxZ; ++candidateZ)
    {
        for (int candidateX = domain.foliageCandidateMinX;
             candidateX <= domain.foliageCandidateMaxX; ++candidateX)
        {
            const float worldX = kDetailPosition.x + 0.05f +
                                 static_cast<float>(candidateX) *
                                     kFoliageCandidateSpacing;
            const float worldZ = kDetailPosition.z + 0.05f +
                                 static_cast<float>(candidateZ) *
                                     kFoliageCandidateSpacing;
            const bool acceptedBaselineCandidate =
                candidateX >= 1 && candidateX <= 78 && candidateZ >= 1 &&
                candidateZ <= 65;
            const bool safelyOutsideBaselinePatch =
                worldX < kDetailPosition.x - pitch ||
                worldX >= kDetailPosition.x + kDetailWorldExtent.x + pitch ||
                worldZ < kDetailPosition.z - pitch ||
                worldZ >= kDetailPosition.z + kDetailWorldExtent.z + pitch;
            if (!acceptedBaselineCandidate && !safelyOutsideBaselinePatch)
            {
                continue;
            }
            const glm::vec2 parityDelta(
                worldX - NaturePondLightingParity::FireLightPosition.x,
                worldZ - NaturePondLightingParity::FireLightPosition.z);
            if (includeLightingParityProbe &&
                glm::dot(parityDelta, parityDelta) < 2.2f * 2.2f)
            {
                continue;
            }
            const std::optional<engine::scene::NaturePondFoliagePlacement>
                waterPlacement =
                    engine::scene::sampleNaturePondWaterFloraDistribution(
                        seed, candidateX, candidateZ,
                        scatterDensityPermille);
            if (waterPlacement.has_value())
            {
                const uint32_t foliageOffset =
                    foliageMaterialOffset(*waterPlacement);
                const uint8_t stem = static_cast<uint8_t>(
                    NaturePondMaterial::Foliage::Base +
                    foliageOffset % NaturePondMaterial::Foliage::Count);
                const uint8_t secondaryStem = static_cast<uint8_t>(
                    NaturePondMaterial::Foliage::Base +
                    (foliageOffset + 2u) %
                        NaturePondMaterial::Foliage::Count);
                const uint8_t blossom =
                    foliageBlossomMaterial(*waterPlacement);
                const bool flowering =
                    hash32(waterPlacement->plantSeed ^ 0x71c95d3bu) % 100u <
                    42u;

                const int x = worldCenterToDetailCell(
                    worldX, foliage.position.x, pitch);
                const int z = worldCenterToDetailCell(
                    worldZ, foliage.position.z, pitch);
                const int surfaceY = worldToDetailCell(
                    kLayout.pondSurfaceHeight, foliage.position.y, pitch);
                setVoxel(foliage, x, surfaceY, z, stem);
                if (pitch <= 0.11f)
                {
                    const bool rotate =
                        (waterPlacement->plantSeed & 1u) != 0u;
                    setVoxel(foliage, x + (rotate ? 1 : 0), surfaceY,
                             z + (rotate ? 0 : 1), secondaryStem);
                    setVoxel(foliage, x - (rotate ? 0 : 1), surfaceY,
                             z - (rotate ? 1 : 0), stem);
                }
                if (flowering)
                {
                    setVoxel(foliage, x, surfaceY + 1, z, blossom);
                }

                instances.push_back(makeAquaticFoliageInstance(
                    pitch, *waterPlacement,
                    engine::game::foliageAquaticStableId(
                        seed, candidateX, candidateZ),
                    stem, secondaryStem, blossom, flowering));
            }

            const std::optional<engine::scene::NaturePondFoliagePlacement>
                carpetPlacement =
                    engine::scene::sampleNaturePondMeadowCarpetDistribution(
                        seed, candidateX, candidateZ,
                        scatterDensityPermille);
            if (carpetPlacement.has_value())
            {
                const uint32_t foliageOffset =
                    foliageMaterialOffset(*carpetPlacement);
                const uint8_t stem = static_cast<uint8_t>(
                    NaturePondMaterial::Foliage::Base +
                    foliageOffset % NaturePondMaterial::Foliage::Count);
                const uint8_t secondaryStem = static_cast<uint8_t>(
                    NaturePondMaterial::Foliage::Base +
                    (foliageOffset + 2u +
                     ((carpetPlacement->patchSeed >> 11u) & 1u)) %
                        NaturePondMaterial::Foliage::Count);
                const uint8_t blossom =
                    foliageBlossomMaterial(*carpetPlacement);
                const int x = worldCenterToDetailCell(
                    worldX, foliage.position.x, pitch);
                const int z = worldCenterToDetailCell(
                    worldZ, foliage.position.z, pitch);
                const int baseY = worldToDetailCell(
                    terrainSurfaceAt(worldX, worldZ, seed, domain),
                    foliage.position.y, pitch);
                const float authoredHeightMeters =
                    0.14f + 0.03f * static_cast<float>(
                                      (carpetPlacement->plantSeed >> 8u) % 4u);
                const int height = std::max(
                    1, static_cast<int>(
                           std::round(authoredHeightMeters / pitch)));
                const int leanCells =
                    pitch <= 0.20f
                        ? static_cast<int>(
                              (carpetPlacement->plantSeed >> 29u) % 3u) -
                              1
                        : 0;
                for (int y = 0; y < height; ++y)
                {
                    const int lean = y + 1 == height ? leanCells : 0;
                    setVoxel(
                        foliage, x + lean, baseY + y, z,
                        (y + static_cast<int>(
                                 carpetPlacement->patchSeed & 1u)) %
                                    3 ==
                                0
                            ? secondaryStem
                            : stem);
                }
                instances.push_back(makeFoliageInstance(
                    seed, pitch, domain, foliage, *carpetPlacement,
                    engine::game::foliageCarpetStableId(
                        seed, candidateX, candidateZ),
                    static_cast<float>(height) * pitch, leanCells, stem,
                    secondaryStem, blossom,
                    engine::game::FoliageBladeGroundCoverOnly));
            }

            const std::optional<engine::scene::NaturePondFoliagePlacement>
                placement =
                    engine::scene::sampleNaturePondFoliageDistribution(
                        seed, candidateX, candidateZ,
                        scatterDensityPermille);
            if (!placement.has_value())
            {
                continue;
            }

            const engine::game::FoliagePatchKind patchKind = placement->kind;
            const engine::game::FoliageMorphology morphology =
                engine::game::foliageMorphologyForPatch(
                    patchKind, placement->patchSeed);
            const bool reeds =
                patchKind == engine::game::FoliagePatchKind::ReedCluster;
            const bool flower =
                patchKind == engine::game::FoliagePatchKind::FlowerCluster;

            const int x = worldCenterToDetailCell(worldX, foliage.position.x, pitch);
            const int z = worldCenterToDetailCell(worldZ, foliage.position.z, pitch);
            const int baseY = worldToDetailCell(
                terrainSurfaceAt(worldX, worldZ, seed, domain),
                foliage.position.y, pitch);
            float heightMeters = 0.28f +
                                 0.08f * static_cast<float>(
                                             (placement->plantSeed >> 8u) % 5u);
            switch (morphology)
            {
            case engine::game::FoliageMorphology::GrassTuft:
                break;
            case engine::game::FoliageMorphology::BranchingShrub:
                heightMeters += 0.18f;
                break;
            case engine::game::FoliageMorphology::DaisyFlower:
                heightMeters += 0.20f;
                break;
            case engine::game::FoliageMorphology::SpikeFlower:
                heightMeters += 0.42f;
                break;
            case engine::game::FoliageMorphology::Reed:
                heightMeters += 0.66f;
                break;
            case engine::game::FoliageMorphology::WaterLily:
                continue;
            case engine::game::FoliageMorphology::TreeCanopySprig:
                continue;
            case engine::game::FoliageMorphology::Count:
                continue;
            }
            const int height =
                std::max(1, static_cast<int>(std::round(heightMeters / pitch)));

            const uint32_t foliageOffset = foliageMaterialOffset(*placement);
            const uint8_t stem = static_cast<uint8_t>(
                NaturePondMaterial::Foliage::Base +
                foliageOffset % NaturePondMaterial::Foliage::Count);
            const uint8_t secondaryStem = static_cast<uint8_t>(
                NaturePondMaterial::Foliage::Base +
                (foliageOffset + 2u +
                 ((placement->patchSeed >> 11u) & 1u)) %
                    NaturePondMaterial::Foliage::Count);
            const int leanCells =
                pitch <= 0.20f
                    ? static_cast<int>((placement->plantSeed >> 29u) % 3u) - 1
                    : 0;
            for (int y = 0; y < height; ++y)
            {
                const int lean = y > height / 2 ? leanCells : 0;
                setVoxel(foliage, x + lean, baseY + y, z,
                         (y + static_cast<int>(placement->patchSeed & 1u)) % 3 == 0
                             ? secondaryStem
                             : stem);
            }

            if (patchKind == engine::game::FoliagePatchKind::Shrub &&
                pitch <= 0.20f)
            {
                const int branchY = baseY + std::max(1, height / 2);
                setVoxel(foliage, x + 1, branchY, z, secondaryStem);
                setVoxel(foliage, x - 1, branchY, z, stem);
                setVoxel(foliage, x, branchY + 1, z + 1, secondaryStem);
                setVoxel(foliage, x, branchY + 1, z - 1, stem);
            }

            const uint8_t blossom = foliageBlossomMaterial(*placement);
            if (flower)
            {
                if (morphology == engine::game::FoliageMorphology::DaisyFlower)
                {
                    setVoxel(foliage, x, baseY + height, z, blossom);
                    if (pitch <= 0.11f)
                    {
                        setVoxel(foliage, x + 1, baseY + height, z, blossom);
                        setVoxel(foliage, x - 1, baseY + height, z, blossom);
                        setVoxel(foliage, x, baseY + height, z + 1, blossom);
                        setVoxel(foliage, x, baseY + height, z - 1, blossom);
                    }
                }
                else
                {
                    const int flowerLayers = pitch <= 0.11f ? 3 : 2;
                    for (int layer = 0; layer < flowerLayers; ++layer)
                    {
                        setVoxel(foliage, x + (layer & 1),
                                 baseY + height - layer, z, blossom);
                    }
                }
            }

            uint32_t flags = reeds ? engine::game::FoliageBladeReed
                                   : engine::game::FoliageBladeNone;
            if (flower)
            {
                flags |= engine::game::FoliageBladeFlower;
            }
            instances.push_back(makeFoliageInstance(
                seed, pitch, domain, foliage, *placement,
                engine::game::foliageStableId(seed, candidateX, candidateZ),
                static_cast<float>(height) * pitch, leanCells, stem,
                secondaryStem, blossom, flags));
        }
    }
    return foliage;
}

NaturePondVolumeData buildHeroTrunk(uint32_t seed, float pitch,
                                    const NaturePondGenerationDomain& domain)
{
    struct TaperedWoodSegment
    {
        glm::vec3 startOffset{0.0f};
        glm::vec3 endOffset{0.0f};
        float startRadius = 0.0f;
        float endRadius = 0.0f;
    };
    constexpr std::array<TaperedWoodSegment, 15> kWoodSegments{
        // a subtly leaning, tapered central trunk.
        TaperedWoodSegment{{0.0f, 0.05f, 0.0f}, {-0.06f, 1.70f, 0.04f}, 0.56f, 0.47f},
        TaperedWoodSegment{{-0.06f, 1.70f, 0.04f}, {0.12f, 3.25f, -0.10f}, 0.47f, 0.36f},
        TaperedWoodSegment{{0.12f, 3.25f, -0.10f}, {-0.08f, 4.30f, -0.06f}, 0.36f, 0.28f},
        TaperedWoodSegment{{-0.08f, 4.30f, -0.06f}, {0.08f, 5.52f, 0.03f}, 0.28f, 0.17f},
        // three readable crown forks, each continuing through a narrower tip.
        TaperedWoodSegment{{0.06f, 3.42f, -0.05f}, {-0.92f, 4.12f, 0.20f}, 0.29f, 0.20f},
        TaperedWoodSegment{{-0.92f, 4.12f, 0.20f}, {-1.96f, 4.57f, 0.48f}, 0.20f, 0.08f},
        TaperedWoodSegment{{-0.02f, 3.82f, -0.06f}, {0.90f, 4.42f, -0.35f}, 0.27f, 0.18f},
        TaperedWoodSegment{{0.90f, 4.42f, -0.35f}, {1.84f, 4.76f, -0.72f}, 0.18f, 0.08f},
        TaperedWoodSegment{{-0.04f, 4.06f, 0.02f}, {0.22f, 4.57f, 0.83f}, 0.22f, 0.09f},
        // low root flare breaks the previous post-like base silhouette.
        TaperedWoodSegment{{-0.04f, 0.23f, 0.03f}, {-0.98f, 0.09f, 0.48f}, 0.34f, 0.08f},
        TaperedWoodSegment{{0.05f, 0.22f, 0.02f}, {1.02f, 0.08f, 0.40f}, 0.34f, 0.08f},
        TaperedWoodSegment{{0.02f, 0.21f, -0.04f}, {-0.48f, 0.08f, -0.92f}, 0.32f, 0.08f},
        TaperedWoodSegment{{0.02f, 0.21f, 0.05f}, {0.42f, 0.08f, 1.00f}, 0.32f, 0.08f},
        TaperedWoodSegment{{-0.03f, 0.16f, 0.02f}, {-0.72f, 0.07f, -0.58f}, 0.27f, 0.07f},
        TaperedWoodSegment{{0.04f, 0.16f, -0.02f}, {0.70f, 0.07f, -0.62f}, 0.27f, 0.07f},
    };
    constexpr glm::vec3 kTrunkExtent{4.4f, 5.7f, 3.6f};
    const float baseY =
        terrainSurfaceAt(kLayout.heroTreeCenterXZ.x, kLayout.heroTreeCenterXZ.y,
                         seed, domain);
    const glm::vec3 position(kLayout.heroTreeCenterXZ.x - kTrunkExtent.x * 0.5f, baseY,
                             kLayout.heroTreeCenterXZ.y - kTrunkExtent.z * 0.5f);
    NaturePondVolumeData trunk =
        makeVolume("NaturePondHeroTrunk", dimsForExtent(kTrunkExtent, pitch), position,
                   glm::vec3(pitch), NaturePondVolumeRole::HeroTrunkOpaque);

    for (int z = 0; z < trunk.dims.z; ++z)
    {
        for (int y = 0; y < trunk.dims.y; ++y)
        {
            for (int x = 0; x < trunk.dims.x; ++x)
            {
                const glm::vec3 worldPos =
                    trunk.position +
                    (glm::vec3(x, y, z) + glm::vec3(0.5f)) * pitch;
                const glm::vec3 localPos =
                    worldPos - glm::vec3(kLayout.heroTreeCenterXZ.x, baseY,
                                         kLayout.heroTreeCenterXZ.y);
                bool insideWood = false;
                for (const TaperedWoodSegment& segment : kWoodSegments)
                {
                    const glm::vec3 axis =
                        segment.endOffset - segment.startOffset;
                    const float axisLengthSquared = glm::dot(axis, axis);
                    const float segmentT = axisLengthSquared > 1e-6f
                                               ? std::clamp(
                                                     glm::dot(localPos - segment.startOffset,
                                                              axis) /
                                                         axisLengthSquared,
                                                     0.0f, 1.0f)
                                               : 0.0f;
                    const glm::vec3 nearest =
                        segment.startOffset + axis * segmentT;
                    const float radius =
                        segment.startRadius +
                        (segment.endRadius - segment.startRadius) * segmentT;
                    if (glm::dot(localPos - nearest, localPos - nearest) <=
                        radius * radius)
                    {
                        insideWood = true;
                        break;
                    }
                }
                if (!insideWood)
                {
                    continue;
                }

                // coarser vertical patches read as bark plates while retaining the
                // established four-material wood band and deterministic seed.
                const uint32_t barkHash =
                    spatialHash(x, y / 4, z, seed + 409u);
                setVoxel(
                    trunk, x, y, z,
                    static_cast<uint8_t>(NaturePondMaterial::Wood::Base +
                                         barkHash % NaturePondMaterial::Wood::Count));
            }
        }
    }
    return trunk;
}

NaturePondVolumeData buildHeroCanopy(uint32_t seed, float pitch,
                                     const NaturePondGenerationDomain& domain)
{
    const float baseY =
        terrainSurfaceAt(kLayout.heroTreeCenterXZ.x, kLayout.heroTreeCenterXZ.y,
                         seed, domain);
    const glm::vec3 center(kLayout.heroTreeCenterXZ.x,
                           baseY + kHeroCanopyCenterHeight,
                           kLayout.heroTreeCenterXZ.y);
    NaturePondVolumeData canopy =
        makeVolume("NaturePondHeroCanopy", dimsForExtent(kHeroCanopyExtent, pitch),
                   center - kHeroCanopyExtent * 0.5f, glm::vec3(pitch),
                   NaturePondVolumeRole::HeroCanopyTranslucent);

    for (int z = 0; z < canopy.dims.z; ++z)
    {
        for (int y = 0; y < canopy.dims.y; ++y)
        {
            for (int x = 0; x < canopy.dims.x; ++x)
            {
                const glm::vec3 worldPos =
                    canopy.position +
                    (glm::vec3(x, y, z) + glm::vec3(0.5f)) * pitch;
                const glm::vec3 localPos = worldPos - center;
                float closestLobeDistance = 2.0f;
                for (const HeroCanopyLobe& lobe : kHeroCanopyLobes)
                {
                    const glm::vec3 normalized =
                        (localPos - lobe.offset) / lobe.radii;
                    closestLobeDistance =
                        std::min(closestLobeDistance,
                                 glm::dot(normalized, normalized));
                }
                if (closestLobeDistance > 1.0f)
                {
                    continue;
                }

                bool insidePocket = false;
                for (const HeroCanopyLobe& pocket :
                     kHeroCanopySilhouettePockets)
                {
                    const glm::vec3 normalized =
                        (localPos - pocket.offset) / pocket.radii;
                    insidePocket =
                        insidePocket || glm::dot(normalized, normalized) <= 1.0f;
                }
                if (insidePocket)
                {
                    continue;
                }

                const uint32_t fineHash =
                    spatialHash(x, y, z, seed + 431u);
                const uint32_t clusterHash =
                    spatialHash(x / 3, y / 3, z / 3, seed + 433u);
                const uint32_t vacancyPercent =
                    closestLobeDistance > 0.78f ? 23u : 8u;
                if (fineHash % 100u < vacancyPercent ||
                    (clusterHash % 100u < 4u && fineHash % 4u != 0u))
                {
                    continue;
                }

                const uint32_t colorPatchHash =
                    spatialHash(x / 4, y / 3, z / 4, seed + 439u);
                const float heightT =
                    std::clamp((localPos.y + kHeroCanopyExtent.y * 0.5f) /
                                   kHeroCanopyExtent.y,
                               0.0f, 1.0f);
                const int shadeJitter =
                    static_cast<int>((colorPatchHash >> 11u) % 3u) - 1;
                int shade = std::clamp(
                    1 + static_cast<int>(heightT * 2.0f) + shadeJitter,
                    0, 3);
                if (closestLobeDistance > 0.84f)
                {
                    shade = std::min(shade + 1, 3);
                }
                const uint32_t colorFamily =
                    (colorPatchHash >> 7u) & 1u;
                setVoxel(
                    canopy, x, y, z,
                    static_cast<uint8_t>(
                        NaturePondMaterial::Foliage::Base +
                        colorFamily * 4u + static_cast<uint32_t>(shade)));
            }
        }
    }
    return canopy;
}

std::vector<engine::game::FoliageBladeInstance> buildHeroCanopyFoliage(
    uint32_t seed, float pitch, const NaturePondGenerationDomain& domain,
    const NaturePondVolumeData& canopy)
{
    constexpr float kTwoPi = 6.28318530717958647692f;
    constexpr size_t kVisibleLobeCount = 7u;
    const float baseY =
        terrainSurfaceAt(kLayout.heroTreeCenterXZ.x, kLayout.heroTreeCenterXZ.y,
                         seed, domain);
    const glm::vec3 canopyCenter(
        kLayout.heroTreeCenterXZ.x, baseY + kHeroCanopyCenterHeight,
        kLayout.heroTreeCenterXZ.y);
    const engine::scene::EnvironmentWindSettings& windSettings =
        engine::scene::defaultEnvironmentWindSettings();
    const engine::scene::EnvironmentWindWaveShape& windShape =
        engine::scene::defaultEnvironmentWindWaveShape();
    const glm::vec2 crossWind(-windSettings.direction.y,
                              windSettings.direction.x);

    std::vector<engine::game::FoliageBladeInstance> instances;
    instances.reserve(kVisibleLobeCount * kHeroCanopySprigDirections.size());
    for (size_t lobeIndex = 0; lobeIndex < kVisibleLobeCount; ++lobeIndex)
    {
        const HeroCanopyLobe& lobe = kHeroCanopyLobes[lobeIndex];
        const glm::vec3 patchRoot = canopyCenter + lobe.offset;
        const uint32_t patchSeed = spatialHash(
            static_cast<int>(lobeIndex), 0, 0, seed + 467u);
        glm::vec2 authoredOutward(lobe.offset.x, lobe.offset.z);
        if (glm::length(authoredOutward) > 0.25f)
        {
            authoredOutward = glm::normalize(authoredOutward);
        }
        const float patchYaw =
            std::atan2(authoredOutward.y, authoredOutward.x);

        const uint32_t colorFamily = (patchSeed >> 7u) & 1u;
        const uint32_t primaryShade = (patchSeed >> 11u) & 1u;
        const uint32_t secondaryShade = primaryShade == 0u ? 1u : 0u;
        const uint8_t primaryMaterial = static_cast<uint8_t>(
            NaturePondMaterial::Foliage::Base + colorFamily * 4u +
            primaryShade);
        const uint8_t secondaryMaterial = static_cast<uint8_t>(
            NaturePondMaterial::Foliage::Base + colorFamily * 4u +
            secondaryShade);
        const float phaseJitter =
            -0.04f + 0.08f *
                         static_cast<float>((patchSeed >> 16u) & 0xffffu) /
                         65535.0f;
        const glm::vec2 patchXZ(patchRoot.x, patchRoot.z);
        const float phase =
            glm::dot(patchXZ, windSettings.direction) *
                windShape.alongPhaseRadiansPerMeter +
            glm::dot(patchXZ, crossWind) *
                windShape.crossPhaseRadiansPerMeter +
            phaseJitter;

        for (size_t sprigIndex = 0;
             sprigIndex < kHeroCanopySprigDirections.size(); ++sprigIndex)
        {
            glm::vec2 outward = authoredOutward;
            if (glm::length(outward) <= 0.25f)
            {
                const float angle =
                    kTwoPi * static_cast<float>(sprigIndex) /
                    static_cast<float>(kHeroCanopySprigDirections.size());
                outward = glm::vec2(std::cos(angle), std::sin(angle));
            }
            const glm::vec2 tangent(-outward.y, outward.x);
            const glm::vec3 authoredDirection =
                kHeroCanopySprigDirections[sprigIndex];
            const glm::vec2 horizontal =
                outward * authoredDirection.x +
                tangent * authoredDirection.y;
            const glm::vec3 direction = glm::normalize(
                glm::vec3(horizontal.x, authoredDirection.z, horizontal.y));
            const glm::vec3 authoredSurface =
                patchRoot + direction * lobe.radii;
            glm::vec3 rootWorld = authoredSurface;
            const float rayStep = std::max(pitch * 0.5f, 0.04f);
            const glm::vec3 rayStart =
                authoredSurface + direction * std::max(pitch * 3.0f, 0.30f);
            const int maxRaySteps =
                static_cast<int>(std::ceil(2.0f / rayStep));
            for (int rayStepIndex = 0; rayStepIndex <= maxRaySteps;
                 ++rayStepIndex)
            {
                const glm::vec3 sampleWorld =
                    rayStart - direction *
                                   (rayStep * static_cast<float>(rayStepIndex));
                const glm::ivec3 cell = glm::ivec3(glm::floor(
                    (sampleWorld - canopy.position) / canopy.scale));
                if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
                    cell.x >= canopy.dims.x || cell.y >= canopy.dims.y ||
                    cell.z >= canopy.dims.z ||
                    canopy.voxels[voxelIndex(
                        canopy.dims, cell.x, cell.y, cell.z)] == 0u)
                {
                    continue;
                }

                const glm::vec3 occupiedCenter =
                    canopy.position +
                    (glm::vec3(cell) + glm::vec3(0.5f)) * canopy.scale;
                rootWorld = occupiedCenter + direction * pitch * 0.46f;
                break;
            }
            const uint32_t plantSeed = spatialHash(
                static_cast<int>(lobeIndex),
                static_cast<int>(sprigIndex), 0, seed + 479u);

            engine::game::FoliageBladeInstance instance{};
            instance.stableId = engine::game::foliageTreeCanopyStableId(
                seed, static_cast<uint32_t>(lobeIndex),
                static_cast<uint32_t>(sprigIndex));
            instance.stablePatchId =
                engine::game::foliageTreeCanopyPatchStableId(
                    seed, static_cast<uint32_t>(lobeIndex));
            instance.rootWorld = rootWorld;
            instance.patchRootWorld = patchRoot;
            instance.heightMeters = 0.32f;
            instance.cellSizeMeters = pitch;
            instance.patchRadiusMeters =
                std::max(lobe.radii.x, lobe.radii.z);
            instance.yawRadians = patchYaw;
            instance.phaseRadians = phase;
            instance.randomSeed = plantSeed;
            instance.patchSeed = patchSeed;
            instance.stemMaterialId = primaryMaterial;
            instance.secondaryMaterialId = secondaryMaterial;
            instance.flags = engine::game::FoliageBladeTreeCanopy;
            instance.patchKind =
                engine::game::FoliagePatchKind::TreeCanopyCluster;
            instance.morphology =
                engine::game::FoliageMorphology::TreeCanopySprig;
            instances.push_back(instance);
        }
    }
    return instances;
}

NaturePondVolumeData buildHeroCanopyOcclusionProxy(
    uint32_t seed, const NaturePondGenerationDomain& domain)
{
    const float baseY =
        terrainSurfaceAt(kLayout.heroTreeCenterXZ.x, kLayout.heroTreeCenterXZ.y,
                         seed, domain);
    const glm::vec3 center(kLayout.heroTreeCenterXZ.x, baseY + 5.05f,
                           kLayout.heroTreeCenterXZ.y);
    NaturePondVolumeData proxy =
        makeVolume("NaturePondHeroCanopyOcclusionProxy",
                   dimsForExtent(kHeroCanopyProxyExtent, kHeroCanopyProxyPitch),
                   center - kHeroCanopyProxyExtent * 0.5f,
                   glm::vec3(kHeroCanopyProxyPitch),
                   NaturePondVolumeRole::HeroCanopyOcclusionProxy);

    const glm::vec3 radii = kHeroCanopyProxyExtent * glm::vec3(0.43f);
    for (int z = 0; z < proxy.dims.z; ++z)
    {
        for (int y = 0; y < proxy.dims.y; ++y)
        {
            for (int x = 0; x < proxy.dims.x; ++x)
            {
                const glm::vec3 worldPos =
                    proxy.position +
                    (glm::vec3(x, y, z) + glm::vec3(0.5f)) *
                        kHeroCanopyProxyPitch;
                const glm::vec3 normalized = (worldPos - center) / radii;
                const uint32_t h = spatialHash(x, y, z, seed + 449u);
                if (glm::dot(normalized, normalized) > 1.0f || h % 100u >= 48u)
                {
                    continue;
                }
                setVoxel(proxy, x, y, z, NaturePondMaterial::Foliage::Base);
            }
        }
    }
    return proxy;
}
} // namespace

const NaturePondLayout& naturePondLayout()
{
    return kLayout;
}

const std::array<NaturePondCameraAnchor, 3>& naturePondCameraAnchors()
{
    return kCameraAnchors;
}

const std::array<NaturePondDetailTierInfo, 3>& naturePondDetailTiers()
{
    return kDetailTiers;
}

const NaturePondDetailTierInfo& naturePondDetailTierInfo(NaturePondDetailTier tier)
{
    for (const NaturePondDetailTierInfo& info : kDetailTiers)
    {
        if (info.tier == tier)
        {
            return info;
        }
    }
    return kDetailTiers.front();
}

const std::array<NaturePondDensityStepInfo, 3>& naturePondDensitySteps()
{
    return kDensitySteps;
}

const NaturePondDensityStepInfo& naturePondDensityStepInfo(
    NaturePondDensityStep step)
{
    for (const NaturePondDensityStepInfo& info : kDensitySteps)
    {
        if (info.step == step)
        {
            return info;
        }
    }
    return kDensitySteps.front();
}

std::optional<NaturePondDensityStep> naturePondDensityStepForSceneName(
    std::string_view sceneName)
{
    for (const NaturePondDensityStepInfo& info : kDensitySteps)
    {
        if (sceneName == info.overviewSceneName || sceneName == info.bankSceneName)
        {
            return info.step;
        }
    }
    return std::nullopt;
}

bool isNaturePondDensityProbeName(std::string_view sceneName)
{
    return naturePondDensityStepForSceneName(sceneName).has_value();
}

std::optional<NaturePondDetailTier> naturePondDetailTierForSceneName(
    std::string_view sceneName)
{
    for (const NaturePondDetailTierInfo& info : kDetailTiers)
    {
        if (info.sceneName == sceneName)
        {
            return info.tier;
        }
    }
    if (sceneName == "nature_pond_bank_probe")
    {
        return NaturePondDetailTier::Reference10Cm;
    }
    if (sceneName == "nature_pond_20cm_bank_probe")
    {
        return NaturePondDetailTier::Balanced20Cm;
    }
    if (sceneName == "nature_pond_25cm_bank_probe")
    {
        return NaturePondDetailTier::Coarse25Cm;
    }
    if (isNaturePondLightingParityProbeName(sceneName))
    {
        return NaturePondDetailTier::Reference10Cm;
    }
    if (sceneName == NaturePondAtmosphere::StarSceneName)
    {
        return NaturePondDetailTier::Reference10Cm;
    }
    if (isNaturePondSunroofProbeName(sceneName))
    {
        return NaturePondDetailTier::Reference10Cm;
    }
    if (isNaturePondDensityProbeName(sceneName))
    {
        return NaturePondDetailTier::Reference10Cm;
    }
    return std::nullopt;
}

bool isNaturePondLightingParityProbeName(std::string_view sceneName)
{
    return sceneName == NaturePondLightingParity::LitSceneName ||
           sceneName == NaturePondLightingParity::UnlitSceneName;
}

bool isNaturePondSunroofProbeName(std::string_view sceneName)
{
    return sceneName == NaturePondSunroof::SceneName;
}

float naturePondProjectedPixelsPerVoxel(float voxelPitch,
                                        const NaturePondCameraAnchor& camera,
                                        const glm::vec3& referencePoint,
                                        float viewportHeight, float verticalFovRadians)
{
    if (voxelPitch <= 0.0f || viewportHeight <= 0.0f || verticalFovRadians <= 0.0f)
    {
        return 0.0f;
    }

    const float cosPitch = std::cos(camera.pitch);
    const glm::vec3 forward(cosPitch * std::sin(camera.yaw), std::sin(camera.pitch),
                            -cosPitch * std::cos(camera.yaw));
    const float depth = glm::dot(referencePoint - camera.position, forward);
    if (depth <= 0.0f)
    {
        return 0.0f;
    }
    return voxelPitch * viewportHeight /
           (2.0f * depth * std::tan(verticalFovRadians * 0.5f));
}

NaturePondBuild buildNaturePond(uint32_t seed, NaturePondDetailTier detailTier,
                                NaturePondBuildOptions options)
{
    const NaturePondDetailTierInfo& tierInfo = naturePondDetailTierInfo(detailTier);
    const NaturePondDensityStepInfo& densityInfo =
        naturePondDensityStepInfo(options.densityStep);
    const NaturePondGenerationDomain domain = generationDomain(options.densityStep);
    NaturePondBuild result{};
    result.seed = seed;
    result.detailTier = detailTier;
    result.densityStep = options.densityStep;
    result.detailVoxelPitch = tierInfo.voxelPitch;
    result.patchExtentMeters = densityInfo.patchExtentMeters;
    result.scatterDensityMultiplier = densityInfo.scatterDensityMultiplier;
    result.layout = kLayout;
    result.volumes.reserve(6);

    NaturePondVolumeData terrain = buildTerrain(seed, domain);
    result.terrainOccupiedVoxels = countOccupied(terrain);
    result.volumes.push_back(std::move(terrain));

    NaturePondVolumeData opaqueDetails = buildOpaqueDetails(
        seed, tierInfo.voxelPitch, options.includeLightingParityProbe, domain);
    result.opaqueDetailOccupiedVoxels = countOccupied(opaqueDetails);
    result.lightingParityEmissiveOccupiedVoxels = countMaterialBand(
        opaqueDetails, NaturePondMaterial::Ember::Base,
        NaturePondMaterial::Ember::Count);
    result.volumes.push_back(std::move(opaqueDetails));

    NaturePondVolumeData foliage =
        buildFoliage(seed, tierInfo.voxelPitch, options.includeLightingParityProbe,
                     domain, result.foliageInstances);
    result.foliageOccupiedVoxels = countOccupied(foliage);
    result.volumes.push_back(std::move(foliage));

    NaturePondVolumeData heroTrunk =
        buildHeroTrunk(seed, tierInfo.voxelPitch, domain);
    result.heroTrunkOccupiedVoxels = countOccupied(heroTrunk);
    result.volumes.push_back(std::move(heroTrunk));

    NaturePondVolumeData heroCanopy =
        buildHeroCanopy(seed, tierInfo.voxelPitch, domain);
    result.heroCanopyOccupiedVoxels = countOccupied(heroCanopy);
    result.heroCanopyFoliageInstances =
        buildHeroCanopyFoliage(seed, tierInfo.voxelPitch, domain, heroCanopy);
    result.volumes.push_back(std::move(heroCanopy));

    NaturePondVolumeData heroCanopyProxy =
        buildHeroCanopyOcclusionProxy(seed, domain);
    result.heroCanopyProxyOccupiedVoxels = countOccupied(heroCanopyProxy);
    result.volumes.push_back(std::move(heroCanopyProxy));
    return result;
}
