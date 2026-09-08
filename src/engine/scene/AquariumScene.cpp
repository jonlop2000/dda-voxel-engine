#include "engine/scene/AquariumScene.h"

#include <algorithm>
#include <cmath>

#include "Core/Logger.h"
#include "engine/render/VulkanContext.h"
#include "engine/game/FoliageCatalog.h"
#include "engine/scene/SunroofGroundFoliage.h"
#include "engine/scene/SunroofLivingWater.h"
#include "Utils/SpatialHash.h"
#include "engine/voxel/VoxelBuilder.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelWorld.h"

namespace
{

// static volume info storage
static std::vector<AquariumVolumeInfo> s_volumeInfos;
static bool s_waterBoundsValid = false;
static glm::vec3 s_waterBoundsMin{0.0f};
static glm::vec3 s_waterBoundsMax{0.0f};

const glm::ivec3 kRoomDims{64, 32, 64};
const glm::vec3 kRoomPos{-32.0f, 0.0f, -32.0f};
const glm::ivec3 kTankDims{48, 24, 32};
const glm::vec3 kTankPos{-24.0f, 2.0f, -16.0f};
const glm::vec3 kFineFoliageScale{0.20f, 0.30f, 0.20f};
const glm::ivec3 kFineFoliageDims{220, 16, 140};
const glm::vec3 kFineFoliagePos{kTankPos + glm::vec3(2.0f, 3.03f, 2.0f)};
const glm::ivec3 kSunroofRoomDims{68, 50, 68};
const glm::vec3 kSunroofRoomPos{-34.0f, 0.0f, -34.0f};
const glm::ivec3 kSunroofTankDims{kSunroofRoomDims};
const glm::vec3 kSunroofTankPos{kSunroofRoomPos};
const glm::vec3 kSunroofFineFoliageScale{0.25f, 0.24f, 0.25f};
const glm::ivec3 kSunroofFineFoliageDims{224, 14, 224};
const glm::vec3 kSunroofFineFoliagePos{kSunroofTankPos + glm::vec3(6.0f, 2.03f, 6.0f)};
const glm::ivec3 kFishbowlRoomDims{80, 34, 72};
const glm::vec3 kFishbowlRoomPos{-40.0f, 0.0f, -36.0f};
const glm::ivec3 kFishbowlTankDims{56, 34, 56};
const glm::vec3 kFishbowlTankPos{-28.0f, 2.0f, -28.0f};
const glm::vec3 kFishbowlFineFoliageScale{0.16f, 0.28f, 0.16f};
const glm::ivec3 kFishbowlFineFoliageDims{180, 14, 180};
const glm::vec3 kFishbowlFineFoliagePos{
    -0.5f * static_cast<float>(kFishbowlFineFoliageDims.x) * kFishbowlFineFoliageScale.x,
    kFishbowlTankPos.y + 6.03f,
    -0.5f * static_cast<float>(kFishbowlFineFoliageDims.z) * kFishbowlFineFoliageScale.z};

enum class FineFoliagePatchKind
{
    GrassPatch,
    AlgaePatch,
    DenseGrassPocket,
    DenseAlgaePocket,
    AccentPatch,
    DenseAccentPocket,
    LowCluster,
};

struct FineFoliagePatch
{
    int x;
    int z;
    int radiusX;
    int radiusZ;
    uint32_t seed;
    int footprintPaddingFineCells;
    FineFoliagePatchKind kind;
};

constexpr FineFoliagePatch kFineFoliagePatches[] = {
    {34, 96, 20, 12, 1201u, 2, FineFoliagePatchKind::GrassPatch},
    {70, 122, 18, 10, 1237u, 2, FineFoliagePatchKind::GrassPatch},
    {113, 38, 24, 12, 1279u, 2, FineFoliagePatchKind::GrassPatch},
    {141, 116, 21, 11, 1301u, 2, FineFoliagePatchKind::GrassPatch},
    {190, 76, 22, 12, 1327u, 2, FineFoliagePatchKind::GrassPatch},
    {25, 28, 14, 8, 1409u, 2, FineFoliagePatchKind::AlgaePatch},
    {50, 50, 16, 10, 1423u, 2, FineFoliagePatchKind::AlgaePatch},
    {96, 74, 18, 9, 1451u, 2, FineFoliagePatchKind::AlgaePatch},
    {166, 105, 19, 11, 1483u, 2, FineFoliagePatchKind::AlgaePatch},
    {198, 112, 14, 8, 1511u, 2, FineFoliagePatchKind::AlgaePatch},
    {43, 101, 11, 7, 1601u, 3, FineFoliagePatchKind::DenseGrassPocket},
    {82, 116, 13, 8, 1627u, 3, FineFoliagePatchKind::DenseGrassPocket},
    {122, 54, 14, 9, 1657u, 3, FineFoliagePatchKind::DenseGrassPocket},
    {184, 78, 12, 8, 1693u, 3, FineFoliagePatchKind::DenseGrassPocket},
    {35, 35, 10, 7, 1721u, 3, FineFoliagePatchKind::DenseAlgaePocket},
    {100, 82, 12, 7, 1741u, 3, FineFoliagePatchKind::DenseAlgaePocket},
    {154, 110, 13, 8, 1777u, 3, FineFoliagePatchKind::DenseAlgaePocket},
    {198, 116, 9, 6, 1801u, 3, FineFoliagePatchKind::DenseAlgaePocket},
    {61, 88, 12, 8, 1847u, 3, FineFoliagePatchKind::AccentPatch},
    {132, 94, 14, 9, 1871u, 3, FineFoliagePatchKind::AccentPatch},
    {177, 43, 12, 8, 1907u, 3, FineFoliagePatchKind::AccentPatch},
    {73, 33, 9, 7, 1931u, 4, FineFoliagePatchKind::DenseAccentPocket},
    {146, 124, 10, 7, 1973u, 4, FineFoliagePatchKind::DenseAccentPocket},
    {161, 60, 8, 8, 293u, 3, FineFoliagePatchKind::LowCluster},
    {199, 33, 8, 8, 373u, 3, FineFoliagePatchKind::LowCluster},
};

bool finePatchContainsTankCell(int tankX, int tankZ, const FineFoliagePatch& patch)
{
    const float fineX =
        (static_cast<float>(tankX) + 0.5f + kTankPos.x - kFineFoliagePos.x) /
        kFineFoliageScale.x;
    const float fineZ =
        (static_cast<float>(tankZ) + 0.5f + kTankPos.z - kFineFoliagePos.z) /
        kFineFoliageScale.z;
    const float rx = static_cast<float>(
        std::max(1, patch.radiusX + patch.footprintPaddingFineCells));
    const float rz = static_cast<float>(
        std::max(1, patch.radiusZ + patch.footprintPaddingFineCells));
    const float nx = (fineX - static_cast<float>(patch.x)) / rx;
    const float nz = (fineZ - static_cast<float>(patch.z)) / rz;
    return nx * nx + nz * nz <= 1.0f;
}

bool heroFootprintContainsTankCell(int tankX, int tankZ, const PlaceableInstance& instance,
                                   float radiusWorld)
{
    const float worldX = kTankPos.x + static_cast<float>(tankX) + 0.5f;
    const float worldZ = kTankPos.z + static_cast<float>(tankZ) + 0.5f;
    const float dx = worldX - instance.position.x;
    const float dz = worldZ - instance.position.z;
    return dx * dx + dz * dz <= radiusWorld * radiusWorld;
}

bool isPlantedFootprintTankCell(int tankX, int tankZ,
                                const std::vector<PlaceableInstance>& heroFoliageInstances)
{
    for (const FineFoliagePatch& patch : kFineFoliagePatches)
    {
        if (finePatchContainsTankCell(tankX, tankZ, patch))
        {
            return true;
        }
    }

    for (const PlaceableInstance& instance : heroFoliageInstances)
    {
        const FoliagePrototype* prototype =
            FoliageCatalog::findPrototype(instance.prototypeSlug, instance.prototypeVersion);
        const float radius =
            prototype != nullptr ? prototype->placeable.placement.footprintRadius : 1.55f;
        if (heroFootprintContainsTankCell(tankX, tankZ, instance, radius))
        {
            return true;
        }
    }

    return false;
}

const char* aquariumLayoutName(AquariumLayout layout)
{
    switch (layout)
    {
    case AquariumLayout::SunroofChamber:
        return "sunroof chamber";
    case AquariumLayout::Fishbowl:
        return "traditional fishbowl";
    case AquariumLayout::Default:
    default:
        return "default tank";
    }
}

glm::ivec3 roomDimsForLayout(AquariumLayout layout)
{
    switch (layout)
    {
    case AquariumLayout::SunroofChamber:
        return kSunroofRoomDims;
    case AquariumLayout::Fishbowl:
        return kFishbowlRoomDims;
    case AquariumLayout::Default:
    default:
        return kRoomDims;
    }
}

glm::vec3 roomPosForLayout(AquariumLayout layout)
{
    switch (layout)
    {
    case AquariumLayout::SunroofChamber:
        return kSunroofRoomPos;
    case AquariumLayout::Fishbowl:
        return kFishbowlRoomPos;
    case AquariumLayout::Default:
    default:
        return kRoomPos;
    }
}

glm::ivec3 tankDimsForLayout(AquariumLayout layout)
{
    switch (layout)
    {
    case AquariumLayout::SunroofChamber:
        return kSunroofTankDims;
    case AquariumLayout::Fishbowl:
        return kFishbowlTankDims;
    case AquariumLayout::Default:
    default:
        return kTankDims;
    }
}

glm::vec3 tankPosForLayout(AquariumLayout layout)
{
    switch (layout)
    {
    case AquariumLayout::SunroofChamber:
        return kSunroofTankPos;
    case AquariumLayout::Fishbowl:
        return kFishbowlTankPos;
    case AquariumLayout::Default:
    default:
        return kTankPos;
    }
}

bool usesFineFoliageVolume(AquariumLayout layout)
{
    return layout == AquariumLayout::Default ||
           layout == AquariumLayout::SunroofChamber ||
           layout == AquariumLayout::Fishbowl;
}

bool usesHeroFoliageVolumes(AquariumLayout layout)
{
    return layout == AquariumLayout::Default || layout == AquariumLayout::Fishbowl;
}

float fishbowlProfileT(float localY, const glm::ivec3& dims)
{
    const float bottom = 3.0f;
    const float top = static_cast<float>(dims.y) - 4.0f;
    return std::clamp((localY - bottom) / std::max(1.0f, top - bottom), 0.0f, 1.0f);
}

float fishbowlOuterRadiusAtLocalY(float localY, const glm::ivec3& dims)
{
    constexpr float kPi = 3.14159265359f;
    const float t = fishbowlProfileT(localY, dims);
    const float minDim = static_cast<float>(std::min(dims.x, dims.z));
    const float bulb = std::pow(std::max(0.0f, std::sin(kPi * t)), 0.45f);
    return minDim * (0.150f + 0.160f * t + 0.160f * bulb);
}

float fishbowlInnerRadiusAtLocalY(float localY, const glm::ivec3& dims)
{
    return std::max(0.0f, fishbowlOuterRadiusAtLocalY(localY, dims) - 1.6f);
}

float fishbowlWaterTopLocalY(const glm::ivec3& dims)
{
    return std::min(static_cast<float>(dims.y) - 8.0f, 25.0f);
}

float fishbowlWaterBoundsRadius(const glm::ivec3& dims)
{
    const float waterRadius = fishbowlInnerRadiusAtLocalY(fishbowlWaterTopLocalY(dims), dims);
    return std::max(8.0f, std::min(waterRadius - 3.2f, static_cast<float>(std::min(dims.x, dims.z)) * 0.31f));
}

uint32_t materialCategoriesForPlacement(uint8_t id)
{
    if (id >= AquariumMaterial::Gravel::Base &&
        id < AquariumMaterial::Gravel::Base + AquariumMaterial::Gravel::Count)
    {
        return PlaceableMaterialCategory::Substrate;
    }
    if (id >= AquariumMaterial::Stone::Base &&
        id < AquariumMaterial::Stone::Base + AquariumMaterial::Stone::Count)
    {
        return PlaceableMaterialCategory::Rock;
    }
    if ((id >= AquariumMaterial::Coral::Base &&
         id < AquariumMaterial::Coral::Base + AquariumMaterial::Coral::Count) ||
        (id >= AquariumMaterial::Wood::Base &&
         id < AquariumMaterial::Wood::Base + AquariumMaterial::Wood::Count))
    {
        return PlaceableMaterialCategory::Decoration;
    }
    return 0u;
}

bool authoredWaterBoundsForLayout(AquariumLayout layout, glm::vec3& outMin,
                                  glm::vec3& outMax)
{
    const glm::ivec3 dims = tankDimsForLayout(layout);
    const glm::vec3 pos = tankPosForLayout(layout);
    if (layout == AquariumLayout::Fishbowl)
    {
        const float cx = static_cast<float>(dims.x) * 0.5f;
        const float cz = static_cast<float>(dims.z) * 0.5f;
        const float radius = fishbowlWaterBoundsRadius(dims);
        const float bottomY = 6.0f;
        const float topY = fishbowlWaterTopLocalY(dims);
        outMin = pos + glm::vec3(cx - radius, bottomY, cz - radius);
        outMax = pos + glm::vec3(cx + radius, topY + 1.0f, cz + radius);
        return true;
    }

    constexpr int kWall = 2;
    const int xMin = kWall;
    const int yMin = kWall;
    const int zMin = kWall;
    const int xMax = dims.x - 1 - kWall;
    const int zMax = dims.z - 1 - kWall;
    const int topY = layout == AquariumLayout::SunroofChamber
                         ? std::max(yMin, dims.y - kWall - 1)
                         : std::max(yMin, dims.y - 3);
    if (xMax < xMin || zMax < zMin || topY < yMin)
    {
        return false;
    }

    outMin =
        pos + glm::vec3(static_cast<float>(xMin), static_cast<float>(yMin),
                        static_cast<float>(zMin));
    outMax =
        pos + glm::vec3(static_cast<float>(xMax + 1), static_cast<float>(topY + 1),
                        static_cast<float>(zMax + 1));
    return true;
}

void applySunroofPaletteOverrides(engine::VoxelPalette& palette)
{
    auto SM = [](uint32_t v) { return static_cast<float>(v); };
    // generic is intentionally used for the sunroof architecture so these entries
    // bypass the shared frame/room material-atlas texture. the sunroof profile keeps
    // generic cell variation disabled, leaving a uniform matte surface while normal,
    // ao, and shadow response continue to describe the individual voxel blocks.
    constexpr float kMaterialPlain = 0.0f;
    auto setOpaque = [&](uint8_t id, const glm::vec3& color, float roughness, float category,
                         float metallic = 0.0f) {
        palette.setEntry(0, id,
                         engine::PaletteEntryCPU{glm::vec4(color, 1.0f),
                                                 glm::vec4(metallic, roughness, 0.0f, SM(0)),
                                                 glm::vec4(1.0f, 0.0f, 0.0f, category)});
    };

    // uniform neutral whites replace the old blue-gray, glossy, panel-textured shell.
    // subtle value separation keeps the skylight trim and floor legible without adding
    // a surface pattern.
    setOpaque(AquariumMaterial::TankFrame, glm::vec3(0.96f, 0.96f, 0.96f), 0.92f,
              kMaterialPlain);
    setOpaque(AquariumMaterial::RoomWall, glm::vec3(0.985f, 0.985f, 0.985f), 0.94f,
              kMaterialPlain);
    setOpaque(AquariumMaterial::RoomFloor, glm::vec3(0.92f, 0.92f, 0.92f), 0.90f,
              kMaterialPlain);
}

std::vector<engine::VolumeSpec> buildVolumeSpecs(bool useMeshTankGlass, AquariumLayout layout,
                                                 bool includeWater,
                                                 const std::vector<PlaceableInstance>&
                                                     heroFoliageInstances)
{
    std::vector<engine::VolumeSpec> specs;
    specs.reserve(AquariumScene::VOLUME_COUNT + 1 + heroFoliageInstances.size());

    specs.push_back({"Room", roomDimsForLayout(layout), roomPosForLayout(layout),
                     engine::VoxelVolume::FLAG_STATIC});

    uint32_t tankFlags = engine::VoxelVolume::FLAG_STATIC;
    if (includeWater)
    {
        tankFlags |= engine::VoxelVolume::FLAG_WATER;
    }
    if (layout == AquariumLayout::Default && !useMeshTankGlass)
    {
        tankFlags |= engine::VoxelVolume::FLAG_GLASS;
    }

    specs.push_back({"TankComposite", tankDimsForLayout(layout), tankPosForLayout(layout),
                     tankFlags});
    if (usesFineFoliageVolume(layout))
    {
        engine::VolumeSpec foliageSpec{};
        foliageSpec.name = "FineFoliage";
        foliageSpec.dims = layout == AquariumLayout::SunroofChamber
                               ? kSunroofFineFoliageDims
                               : (layout == AquariumLayout::Fishbowl
                                      ? kFishbowlFineFoliageDims
                                      : kFineFoliageDims);
        foliageSpec.position = layout == AquariumLayout::SunroofChamber
                                   ? kSunroofFineFoliagePos
                                   : (layout == AquariumLayout::Fishbowl
                                          ? kFishbowlFineFoliagePos
                                          : kFineFoliagePos);
        foliageSpec.flags = engine::VoxelVolume::FLAG_STATIC;
        foliageSpec.scale = layout == AquariumLayout::SunroofChamber
                                ? kSunroofFineFoliageScale
                                : (layout == AquariumLayout::Fishbowl
                                       ? kFishbowlFineFoliageScale
                                       : kFineFoliageScale);
        foliageSpec.lightingOcclusionMode =
            engine::VoxelVolume::LightingOcclusionMode::None;
        specs.push_back(foliageSpec);
    }
    if (usesHeroFoliageVolumes(layout))
    {
        for (size_t i = 0; i < heroFoliageInstances.size(); ++i)
        {
            const PlaceableInstance& instance = heroFoliageInstances[i];
            const FoliagePrototype* prototype =
                FoliageCatalog::findPrototype(instance.prototypeSlug, instance.prototypeVersion);
            if (prototype == nullptr)
            {
                logWarning("AquariumScene",
                           makeLogMessage("Skipping missing foliage prototype: ",
                                          instance.prototypeSlug, "@",
                                          instance.prototypeVersion));
                continue;
            }

            engine::VolumeSpec heroSpec{};
            heroSpec.name = FoliageCatalog::heroFoliageVolumeName(i);
            heroSpec.dims = prototype->placeable.voxelDims;
            heroSpec.position = instance.position;
            heroSpec.flags = engine::VoxelVolume::FLAG_DYNAMIC;
            heroSpec.scale = prototype->placeable.voxelScale * instance.scale;
            heroSpec.rotation = instance.rotation;
            heroSpec.lightingOcclusionMode =
                engine::VoxelVolume::LightingOcclusionMode::TranslucentFoliage;
            specs.push_back(heroSpec);
        }
    }
    return specs;
}

bool isTankFrameMaterial(uint8_t id)
{
    return id == AquariumMaterial::TankFrame ||
           (id >= AquariumMaterial::TankFrameBand::Base &&
            id < AquariumMaterial::TankFrameBand::Base + AquariumMaterial::TankFrameBand::Count);
}

uint8_t tankFrameVariant(int x, int y, int z, bool edgeHighlight)
{
    int idx = static_cast<int>(SpatialHash::stoneWeatherIndex(
                  AquariumMaterial::TankFrameBand::Base,
                  AquariumMaterial::TankFrameBand::Count, x, y, z)) -
              AquariumMaterial::TankFrameBand::Base;

    if (edgeHighlight)
    {
        idx += 5;
    }

    const uint32_t wear = SpatialHash::hash3D(x * 5, y * 7, z * 11);
    if ((wear & 31u) == 0u)
    {
        idx -= 2;
    }
    else if ((wear & 63u) == 1u)
    {
        idx += 2;
    }

    idx = std::clamp(idx, 0, AquariumMaterial::TankFrameBand::Count - 1);
    return static_cast<uint8_t>(AquariumMaterial::TankFrameBand::Base + idx);
}

int positiveMod(int value, int modulus)
{
    const int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

uint8_t fishbowlTableWoodVariant(int x, int y, int z, int cx, int cz, float tableRadius)
{
    const int plankWidth = 5;
    const int localX = x - cx;
    const int localZ = z - cz;
    const int tableStartZ = static_cast<int>(std::floor(static_cast<float>(cz) - tableRadius));
    const int boardZ = z - tableStartZ;
    const int plank = boardZ / plankWidth;
    const int inPlank = positiveMod(boardZ, plankWidth);
    const float radial =
        std::sqrt(static_cast<float>(localX * localX + localZ * localZ)) /
        std::max(tableRadius, 1.0f);
    const uint32_t h = SpatialHash::hash3D(x + 97, y * 31 + plank * 17, z - 53);

    int idx = 5;
    if (y >= 2)
    {
        const int plankTone =
            static_cast<int>(SpatialHash::hash3D(plank, 17, 5) % 5u) - 1;
        const int fineTone = static_cast<int>(h % 3u) - 1;
        idx = 5 + plankTone + fineTone;

        const bool longSeam = inPlank == 0 || inPlank == plankWidth - 1;
        if (longSeam && (h & 3u) != 0u)
        {
            idx = 1 + static_cast<int>((h >> 4u) % 3u);
        }

        const int endJoint = positiveMod(localX + plank * 7, 18);
        if (endJoint == 0 && inPlank > 0 && inPlank < plankWidth - 1 && (h & 7u) != 0u)
        {
            idx = 2 + static_cast<int>((h >> 6u) % 2u);
        }

        const int cellX = (x + 128) / 8;
        const int cellZ = (z + 128) / 6;
        const uint32_t knotHash = SpatialHash::hash3D(cellX, 41 + plank, cellZ);
        if ((knotHash % 13u) == 0u)
        {
            const int knotX = cellX * 8 - 128 + 2 + static_cast<int>((knotHash >> 4u) % 4u);
            const int knotZ = cellZ * 6 - 128 + 1 + static_cast<int>((knotHash >> 9u) % 4u);
            const int dx = std::abs(x - knotX);
            const int dz = std::abs(z - knotZ);
            if (dx <= 1 && dz <= 1)
            {
                idx = (dx == 0 && dz == 0) ? 0 : 11;
            }
        }

        if (radial > 0.74f && idx > 3)
        {
            idx -= 1;
        }
        if ((h & 31u) == 1u && idx < 10)
        {
            idx += 2;
        }
    }
    else if (y == 1)
    {
        idx = 3 + static_cast<int>(h % 4u);
        if (radial > 0.82f)
        {
            idx = std::max(1, idx - 2);
        }
    }
    else
    {
        idx = 1 + static_cast<int>(h % 4u);
        if (radial < 0.32f && (h & 7u) == 0u)
        {
            idx += 2;
        }
    }

    idx = std::clamp(idx, 0, AquariumMaterial::TableWood::Count - 1);
    return static_cast<uint8_t>(AquariumMaterial::TableWood::Base + idx);
}

void setFrameDetailVoxel(engine::VoxelBuilder& builder, int x, int y, int z, uint8_t id)
{
    if (isTankFrameMaterial(builder.getVoxel(x, y, z)))
    {
        builder.setVoxel(x, y, z, id);
    }
}

void applyTankFrameVariation(engine::VoxelBuilder& builder, const glm::ivec3& dims)
{
    for (int z = 0; z < dims.z; ++z)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                if (!isTankFrameMaterial(builder.getVoxel(x, y, z)))
                {
                    continue;
                }

                const bool nearOuterX = x <= 1 || x >= dims.x - 2;
                const bool nearOuterY = y <= 1 || y >= dims.y - 2;
                const bool nearOuterZ = z <= 1 || z >= dims.z - 2;
                const int edgeCount = (nearOuterX ? 1 : 0) + (nearOuterY ? 1 : 0) +
                                      (nearOuterZ ? 1 : 0);
                builder.setVoxel(x, y, z, tankFrameVariant(x, y, z, edgeCount >= 2));
            }
        }
    }
}

void addTankFrameHardware(engine::VoxelBuilder& builder, const glm::ivec3& dims)
{
    constexpr uint8_t kDark = AquariumMaterial::TankFrameBand::Base + 3;
    constexpr uint8_t kMid = AquariumMaterial::TankFrameBand::Base + 7;
    constexpr uint8_t kBright = AquariumMaterial::TankFrameBand::Base + 10;

    for (int x = 5; x <= dims.x - 6; x += 8)
    {
        setFrameDetailVoxel(builder, x, 1, 0, kBright);
        setFrameDetailVoxel(builder, x, dims.y - 2, 0, kMid);
        setFrameDetailVoxel(builder, x, 1, dims.z - 1, kMid);
        setFrameDetailVoxel(builder, x, dims.y - 2, dims.z - 1, kDark);
    }

    for (int y = 4; y <= dims.y - 5; y += 5)
    {
        setFrameDetailVoxel(builder, 1, y, 0, kBright);
        setFrameDetailVoxel(builder, dims.x - 2, y, 0, kMid);
        setFrameDetailVoxel(builder, 1, y, dims.z - 1, kMid);
        setFrameDetailVoxel(builder, dims.x - 2, y, dims.z - 1, kDark);
    }

    for (int z = 5; z <= dims.z - 6; z += 7)
    {
        setFrameDetailVoxel(builder, 0, 1, z, kMid);
        setFrameDetailVoxel(builder, 0, dims.y - 2, z, kDark);
        setFrameDetailVoxel(builder, dims.x - 1, 1, z, kBright);
        setFrameDetailVoxel(builder, dims.x - 1, dims.y - 2, z, kMid);
    }
}

void addInteriorCornerSeams(engine::VoxelBuilder& builder, int xMin, int yMin, int zMin,
                            int xMax, int yMax, int zMax)
{
    constexpr uint8_t kSeal = AquariumMaterial::TankFrameBand::Base + 3;
    constexpr uint8_t kHighlight = AquariumMaterial::TankFrameBand::Base + 8;

    for (int y = yMin + 1; y <= yMax - 1; ++y)
    {
        const uint8_t id = ((y / 3) & 1) == 0 ? kSeal : kHighlight;
        builder.setVoxel(xMin, y, zMin, id);
        builder.setVoxel(xMax, y, zMin, id);
        builder.setVoxel(xMin, y, zMax, id);
        builder.setVoxel(xMax, y, zMax, id);
    }
}

void accumulateWaterBounds(const engine::VolumeSpec& spec, const std::vector<uint8_t>& data,
                           glm::vec3& ioMin, glm::vec3& ioMax, bool& ioFound)
{
    const glm::ivec3 dims = spec.dims;
    if (dims.x <= 0 || dims.y <= 0 || dims.z <= 0)
    {
        return;
    }

    const size_t expectedSize =
        static_cast<size_t>(dims.x) * static_cast<size_t>(dims.y) * static_cast<size_t>(dims.z);
    if (data.size() != expectedSize)
    {
        return;
    }

    int minX = dims.x;
    int minY = dims.y;
    int minZ = dims.z;
    int maxX = -1;
    int maxY = -1;
    int maxZ = -1;

    for (int z = 0; z < dims.z; ++z)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                const size_t idx = static_cast<size_t>(x) +
                                   static_cast<size_t>(y) * static_cast<size_t>(dims.x) +
                                   static_cast<size_t>(z) * static_cast<size_t>(dims.x) *
                                       static_cast<size_t>(dims.y);
                if (data[idx] != AquariumMaterial::Water)
                {
                    continue;
                }

                minX = std::min(minX, x);
                minY = std::min(minY, y);
                minZ = std::min(minZ, z);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
                maxZ = std::max(maxZ, z);
            }
        }
    }

    if (maxX < minX || maxY < minY || maxZ < minZ)
    {
        return;
    }

    const glm::vec3 worldMin = spec.position +
                               glm::vec3(static_cast<float>(minX), static_cast<float>(minY),
                                         static_cast<float>(minZ));
    const glm::vec3 worldMax = spec.position +
                               glm::vec3(static_cast<float>(maxX + 1), static_cast<float>(maxY + 1),
                                         static_cast<float>(maxZ + 1));

    if (!ioFound)
    {
        ioMin = worldMin;
        ioMax = worldMax;
        ioFound = true;
        return;
    }

    ioMin = glm::min(ioMin, worldMin);
    ioMax = glm::max(ioMax, worldMax);
}

void accumulateAuthoredWaterBounds(const engine::VolumeSpec& spec, AquariumLayout layout,
                                   glm::vec3& ioMin, glm::vec3& ioMax, bool& ioFound)
{
    if (layout == AquariumLayout::Fishbowl)
    {
        glm::vec3 worldMin(0.0f);
        glm::vec3 worldMax(0.0f);
        if (!authoredWaterBoundsForLayout(layout, worldMin, worldMax))
        {
            return;
        }

        if (!ioFound)
        {
            ioMin = worldMin;
            ioMax = worldMax;
            ioFound = true;
            return;
        }

        ioMin = glm::min(ioMin, worldMin);
        ioMax = glm::max(ioMax, worldMax);
        return;
    }

    const glm::ivec3 dims = spec.dims;
    constexpr int kWall = 2;
    const int xMin = kWall;
    const int yMin = kWall;
    const int zMin = kWall;
    const int xMax = dims.x - 1 - kWall;
    const int zMax = dims.z - 1 - kWall;
    const int topY = layout == AquariumLayout::SunroofChamber
                         ? std::max(yMin, dims.y - kWall - 1)
                         : std::max(yMin, dims.y - 3);
    if (xMax < xMin || zMax < zMin || topY < yMin)
    {
        return;
    }

    const glm::vec3 worldMin =
        spec.position +
        glm::vec3(static_cast<float>(xMin), static_cast<float>(yMin), static_cast<float>(zMin));
    const glm::vec3 worldMax =
        spec.position + glm::vec3(static_cast<float>(xMax + 1),
                                  static_cast<float>(topY + 1),
                                  static_cast<float>(zMax + 1));

    if (!ioFound)
    {
        ioMin = worldMin;
        ioMax = worldMax;
        ioFound = true;
        return;
    }

    ioMin = glm::min(ioMin, worldMin);
    ioMax = glm::max(ioMax, worldMax);
}

} // anonymous namespace

bool AquariumScene::init(VulkanContext& ctx, engine::VoxelWorld& world, engine::VoxelPalette& palette,
                         bool useMeshTankGlass, AquariumLayout layout, bool includeWater,
                         const std::vector<PlaceableInstance>* placeables,
                         bool useDefaultPlaceables,
                         AquariumSceneRenderData* renderData)
{
    logInfo("AquariumScene", "Initializing Aquarium Test Scene.");
    logInfo("AquariumScene", makeLogMessage("Aquarium layout: ", aquariumLayoutName(layout)));
    logInfo("AquariumScene",
            makeLogMessage("Aquarium tank glass mode: ",
                           layout == AquariumLayout::SunroofChamber
                               ? (useMeshTankGlass ? "optional mesh-glass water shaft"
                                                   : "open water shaft")
                               : (layout == AquariumLayout::Fishbowl
                                      ? (useMeshTankGlass ? "mesh faceted glass bowl"
                                                          : "legacy voxel glass bowl")
                                      : (useMeshTankGlass
                                             ? "mesh (tank walls empty + frame voxels)"
                                             : "legacy voxel glass shell"))));
    logInfo("AquariumScene",
            layout == AquariumLayout::SunroofChamber
                ? "Aquarium room shell: bright gallery chamber with skylight."
                : (layout == AquariumLayout::Fishbowl
                       ? "Aquarium room shell: fishbowl display table and backdrop."
                       : "Aquarium outer room shell: disabled"));
    if (!includeWater)
    {
        logInfo("AquariumScene", "Aquarium voxel water disabled for this run.");
    }

    const std::vector<PlaceableInstance> heroFoliageInstances =
        FoliageCatalog::aquariumHeroFoliageInstances(placeables, useDefaultPlaceables);
    const std::vector<engine::VolumeSpec> volumeSpecs =
        buildVolumeSpecs(useMeshTankGlass, layout, includeWater, heroFoliageInstances);

    if (renderData != nullptr)
    {
        renderData->foliageInstances.clear();
        if (layout == AquariumLayout::SunroofChamber)
        {
            const engine::scene::SunroofLivingFoliagePalette livingPalette{
                AquariumMaterial::PlantBand::Base,
                AquariumMaterial::PlantBand::Count,
                AquariumMaterial::AlgaeBand::Base,
                AquariumMaterial::AlgaeBand::Count,
                AquariumMaterial::Coral::Base,
                AquariumMaterial::Coral::Count,
            };
            renderData->foliageInstances =
                engine::scene::buildSunroofLivingFoliage(
                    kSunroofFineFoliageDims, kSunroofFineFoliagePos,
                    kSunroofFineFoliageScale, livingPalette);
        }
    }

    // setup aquarium palette materials (ids 5-11)
    palette.buildAquariumPalette();
    if (layout == AquariumLayout::SunroofChamber)
    {
        applySunroofPaletteOverrides(palette);
    }
    if (!palette.upload(ctx))
    {
        logError("AquariumScene", "Failed to upload aquarium palette.");
        return false;
    }

    // clear and rebuild volume info
    s_volumeInfos.clear();
    s_volumeInfos.reserve(volumeSpecs.size());
    s_waterBoundsValid = false;
    s_waterBoundsMin = glm::vec3(0.0f);
    s_waterBoundsMax = glm::vec3(0.0f);

    // build volume data for each spec
    std::vector<std::vector<uint8_t>> volumeData;
    volumeData.reserve(volumeSpecs.size());
    PlaceableVoxelDataCache placeableVoxelDataCache{};
    for (size_t i = 0; i < volumeSpecs.size(); ++i)
    {
        const auto& spec = volumeSpecs[i];

        // store volume info for debug ui
        AquariumVolumeInfo info;
        info.name = spec.name;
        info.dimensions = spec.dims;
        info.worldPosition = spec.position;
        info.flags = spec.flags;
        s_volumeInfos.push_back(info);

        // build the voxel data
        std::vector<uint8_t> data;
        if (i >= VOLUME_HERO_FOLIAGE_BEGIN &&
            i < VOLUME_HERO_FOLIAGE_BEGIN + heroFoliageInstances.size())
        {
            const size_t heroIndex = i - VOLUME_HERO_FOLIAGE_BEGIN;
            if (heroIndex < heroFoliageInstances.size())
            {
                const PlaceableInstance& instance = heroFoliageInstances[heroIndex];
                const FoliagePrototype* prototype = FoliageCatalog::findPrototype(
                    instance.prototypeSlug, instance.prototypeVersion);
                if (prototype != nullptr)
                {
                    data = placeableVoxelDataCache.getOrBuild(prototype->placeable,
                                                              instance.seed);
                }
            }
        }
        else
        {
            switch (i)
            {
            case VOLUME_ROOM:
                data = buildRoomVolume(spec.dims, layout);
                break;
            case VOLUME_TANK_COMPOSITE:
                data =
                    buildTankCompositeVolume(spec.dims, useMeshTankGlass, layout, includeWater,
                                             heroFoliageInstances);
                break;
            case VOLUME_FINE_FOLIAGE:
                if (layout == AquariumLayout::SunroofChamber)
                {
                    const engine::scene::SunroofGroundFoliagePalette foliagePalette{
                        AquariumMaterial::GrassBand::Base,
                        AquariumMaterial::GrassBand::Count,
                        AquariumMaterial::PlantBand::Base,
                        AquariumMaterial::PlantBand::Count,
                        AquariumMaterial::AlgaeBand::Base,
                        AquariumMaterial::AlgaeBand::Count,
                        AquariumMaterial::FoliageAccentBand::Base,
                        AquariumMaterial::FoliageAccentBand::Count,
                        AquariumMaterial::Coral::Base,
                        AquariumMaterial::Coral::Count,
                    };
                    engine::scene::SunroofGroundFoliageBuild foliage =
                        engine::scene::buildSunroofGroundFoliage(spec.dims, foliagePalette);
                    logInfo("AquariumScene",
                            makeLogMessage("Sunroof underwater garden: ",
                                           foliage.stats.turfPatchCount, " turf patches, ",
                                           foliage.stats.ribbonTuftCount, " ribbon tufts, ",
                                           foliage.stats.broadLeafCount, " broad leaves, ",
                                           foliage.stats.reedFanCount, " reed fans, ",
                                           foliage.stats.flowerColonyCount,
                                           " flower colonies, ",
                                           foliage.stats.occupiedVoxelCount, " occupied voxels."));
                    data = std::move(foliage.voxels);
                }
                else
                {
                    data = layout == AquariumLayout::Fishbowl
                               ? buildFishbowlFineFoliageVolume(spec.dims)
                               : buildFineFoliageVolume(spec.dims);
                }
                break;
            default:
                // shouldn't happen
                data.resize(static_cast<size_t>(spec.dims.x) * spec.dims.y * spec.dims.z, 0);
                break;
            }
        }

        const size_t expectedDataSize =
            static_cast<size_t>(spec.dims.x) * static_cast<size_t>(spec.dims.y) *
            static_cast<size_t>(spec.dims.z);
        if (data.size() != expectedDataSize)
        {
            data.assign(expectedDataSize, 0u);
        }

        accumulateWaterBounds(spec, data, s_waterBoundsMin, s_waterBoundsMax,
                              s_waterBoundsValid);
        if (!includeWater && i == VOLUME_TANK_COMPOSITE)
        {
            accumulateAuthoredWaterBounds(spec, layout, s_waterBoundsMin, s_waterBoundsMax,
                                          s_waterBoundsValid);
        }
        volumeData.push_back(std::move(data));
    }

    // initialize the voxel world with our volumes
    if (!world.initAquariumScene(ctx, palette, volumeSpecs.size(),
                                  [&](size_t index) -> const engine::VolumeSpec& { return volumeSpecs[index]; },
                                  [&](size_t index) -> const std::vector<uint8_t>& { return volumeData[index]; }))
    {
        logError("AquariumScene", "Failed to initialize aquarium volumes in VoxelWorld.");
        return false;
    }

    logInfo("AquariumScene",
            makeLogMessage("Aquarium Test Scene initialized with ", volumeSpecs.size(),
                           " volumes"));
    if (s_waterBoundsValid)
    {
        logInfo("AquariumScene",
                makeLogMessage("Aquarium water bounds: min(", s_waterBoundsMin.x, ", ",
                               s_waterBoundsMin.y, ", ", s_waterBoundsMin.z, ") max(",
                               s_waterBoundsMax.x, ", ", s_waterBoundsMax.y, ", ",
                               s_waterBoundsMax.z, ")"));
    }
    return true;
}

const std::vector<AquariumVolumeInfo>& AquariumScene::getVolumeInfos()
{
    return s_volumeInfos;
}

bool AquariumScene::getWaterBounds(glm::vec3& outMin, glm::vec3& outMax)
{
    if (!s_waterBoundsValid)
    {
        return false;
    }

    outMin = s_waterBoundsMin;
    outMax = s_waterBoundsMax;
    return true;
}

PlacementEvaluation AquariumScene::evaluateFoliagePlacement(
    const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float maxDistance,
    std::string_view prototypeSlug, uint32_t prototypeVersion,
    const std::vector<PlaceableInstance>* placeables, bool useDefaultPlaceables,
    AquariumLayout layout)
{
    PlacementEvaluation evaluation{};

    const FoliagePrototype* prototype =
        FoliageCatalog::findPrototype(prototypeSlug, prototypeVersion);
    if (prototype == nullptr)
    {
        evaluation.rejectReason = "unknown foliage prototype";
        return evaluation;
    }

    if (layout != AquariumLayout::Default)
    {
        evaluation.rejectReason = "foliage placement supports the default tank only";
        return evaluation;
    }

    const float rayLength = glm::length(rayDirection);
    if (rayLength <= 1e-5f || maxDistance <= 0.0f)
    {
        evaluation.rejectReason = "invalid placement ray";
        return evaluation;
    }

    const glm::vec3 direction = rayDirection / rayLength;
    if (std::abs(direction.y) <= 1e-5f)
    {
        evaluation.rejectReason = "ray misses the substrate plane";
        return evaluation;
    }

    const float rootY = kFineFoliagePos.y;
    const float t = (rootY - rayOrigin.y) / direction.y;
    if (t < 0.0f || t > maxDistance)
    {
        evaluation.rejectReason = "ray misses the substrate plane";
        return evaluation;
    }

    return evaluateFoliagePlacementAtPosition(
        rayOrigin + direction * t, prototypeSlug, prototypeVersion,
        placeables, useDefaultPlaceables, layout);
}

PlacementEvaluation AquariumScene::evaluateFoliagePlacementAtPosition(
    const glm::vec3& proposedPosition, std::string_view prototypeSlug,
    uint32_t prototypeVersion,
    const std::vector<PlaceableInstance>* placeables,
    bool useDefaultPlaceables, AquariumLayout layout)
{
    PlacementEvaluation evaluation{};

    const FoliagePrototype* prototype =
        FoliageCatalog::findPrototype(prototypeSlug, prototypeVersion);
    if (prototype == nullptr)
    {
        evaluation.rejectReason = "unknown foliage prototype";
        return evaluation;
    }

    if (layout != AquariumLayout::Default)
    {
        evaluation.rejectReason =
            "foliage placement supports the default tank only";
        return evaluation;
    }
    if (!std::isfinite(proposedPosition.x) ||
        !std::isfinite(proposedPosition.z))
    {
        evaluation.rejectReason = "invalid placement position";
        return evaluation;
    }

    PlacementHit hit{};
    hit.position = {proposedPosition.x, kFineFoliagePos.y,
                    proposedPosition.z};
    hit.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    hit.materialId = AquariumMaterial::Gravel::Base;

    glm::vec3 waterMin(0.0f);
    glm::vec3 waterMax(0.0f);
    if (!getWaterBounds(waterMin, waterMax) &&
        !authoredWaterBoundsForLayout(layout, waterMin, waterMax))
    {
        evaluation.rejectReason = "water bounds unavailable";
        return evaluation;
    }

    hit.waterDepth = std::max(0.0f, waterMax.y - hit.position.y);
    evaluation.hit = hit;
    evaluation.hasHit = true;

    const PlacementRules& rules = prototype->placeable.placement;
    const float radius = std::max(0.0f, rules.footprintRadius);
    if (hit.position.x < waterMin.x + radius || hit.position.x > waterMax.x - radius ||
        hit.position.z < waterMin.z + radius || hit.position.z > waterMax.z - radius)
    {
        evaluation.rejectReason = "footprint outside tank substrate";
        return evaluation;
    }

    if ((materialCategoriesForPlacement(hit.materialId) & rules.allowedMaterialCategories) == 0u)
    {
        evaluation.rejectReason = "material does not accept this plant";
        return evaluation;
    }

    if (hit.waterDepth < rules.minWaterDepth)
    {
        evaluation.rejectReason = "not enough water above plant";
        return evaluation;
    }

    const float slopeDegrees = 0.0f;
    if (slopeDegrees > rules.maxSlopeDegrees)
    {
        evaluation.rejectReason = "surface slope too steep";
        return evaluation;
    }

    evaluation.footprintClear = true;
    const std::vector<PlaceableInstance> heroFoliageInstances =
        FoliageCatalog::aquariumHeroFoliageInstances(placeables, useDefaultPlaceables);
    for (const PlaceableInstance& instance : heroFoliageInstances)
    {
        const FoliagePrototype* otherPrototype =
            FoliageCatalog::findPrototype(instance.prototypeSlug, instance.prototypeVersion);
        const float otherRadius =
            otherPrototype != nullptr ? otherPrototype->placeable.placement.footprintRadius
                                      : radius;
        const float minDistance = radius + std::max(0.0f, otherRadius);
        const float dx = hit.position.x - instance.position.x;
        const float dz = hit.position.z - instance.position.z;
        if (dx * dx + dz * dz < minDistance * minDistance)
        {
            evaluation.footprintClear = false;
            evaluation.rejectReason = "footprint overlaps another plant";
            return evaluation;
        }
    }

    evaluation.valid = true;
    return evaluation;
}

std::vector<uint8_t> AquariumScene::buildRoomVolume(const glm::ivec3& dims, AquariumLayout layout)
{
    engine::VoxelBuilder builder(dims);
    if (layout == AquariumLayout::Fishbowl)
    {
        const int cx = dims.x / 2;
        const int cz = dims.z / 2;
        constexpr float kTableRadius = 33.0f;
        constexpr float kFrameRimInnerRadius = 30.0f;

        for (int z = 0; z < dims.z; ++z)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                const float dx = static_cast<float>(x - cx);
                const float dz = static_cast<float>(z - cz);
                const float dist2 = dx * dx + dz * dz;
                if (dist2 <= kTableRadius * kTableRadius)
                {
                    for (int y = 0; y <= 2; ++y)
                    {
                        const uint8_t id = fishbowlTableWoodVariant(
                            x, y, z, cx, cz, kTableRadius);
                        builder.setVoxel(x, y, z, id);
                    }
                }
                if (dist2 > kFrameRimInnerRadius * kFrameRimInnerRadius &&
                    dist2 <= kTableRadius * kTableRadius)
                {
                    for (int y = 2; y <= 3; ++y)
                    {
                        builder.setVoxel(x, y, z, tankFrameVariant(x, y, z, true));
                    }
                }
            }
        }

        for (int z = cz - 4; z <= cz + 4; ++z)
        {
            for (int x = cx - 4; x <= cx + 4; ++x)
            {
                const float dx = static_cast<float>(x - cx);
                const float dz = static_cast<float>(z - cz);
                if (dx * dx + dz * dz <= 4.5f * 4.5f)
                {
                    builder.setVoxel(x, 3, z, tankFrameVariant(x, 3, z, false));
                    builder.setVoxel(x, 4, z, tankFrameVariant(x, 4, z, false));
                }
            }
        }

        return std::move(builder.data());
    }

    if (layout == AquariumLayout::SunroofChamber)
    {
        const int wall = 2;
        const int openingSizeX = 24;
        const int openingSizeZ = 24;
        const int openingMinX = (dims.x - openingSizeX) / 2;
        const int openingMinZ = (dims.z - openingSizeZ) / 2;
        const int openingMaxX = openingMinX + openingSizeX - 1;
        const int openingMaxZ = openingMinZ + openingSizeZ - 1;
        const int framePad = 2;

        builder.fillShellBox({0, 0, 0}, dims - glm::ivec3(1), wall,
                             AquariumMaterial::TankFrame);
        builder.fillBox({0, 0, 0}, {dims.x - 1, wall - 1, dims.z - 1},
                        AquariumMaterial::RoomFloor);

        // keep the lower slab neutral, but turn the exposed interior surface into
        // coherent grass, algae, and gravel blocks. the contact field shares the
        // garden's broad habitat shapes, grounding dense beds without adding foliage
        // to ao or shadow traversal.
        const engine::scene::SunroofGroundSurfacePalette surfacePalette{
            AquariumMaterial::GrassBand::Base,
            AquariumMaterial::GrassBand::Count,
            AquariumMaterial::AlgaeBand::Base,
            AquariumMaterial::AlgaeBand::Count,
            AquariumMaterial::Gravel::Base,
            AquariumMaterial::Gravel::Count,
        };
        for (int z = wall; z < dims.z - wall; ++z)
        {
            for (int x = wall; x < dims.x - wall; ++x)
            {
                const uint8_t grounding =
                    engine::scene::sunroofGroundFoliageGrounding(dims, x, z);
                builder.setVoxel(
                    x, wall - 1, z,
                    engine::scene::sunroofGroundBlockMaterial(dims, x, z,
                                                              surfacePalette, grounding));
            }
        }

        builder.fillBox({std::max(0, openingMinX - framePad), dims.y - wall,
                         std::max(0, openingMinZ - framePad)},
                        {std::min(dims.x - 1, openingMaxX + framePad), dims.y - 1,
                         std::min(dims.z - 1, openingMaxZ + framePad)},
                        AquariumMaterial::RoomWall);
        builder.fillBox({openingMinX, dims.y - wall, openingMinZ},
                        {openingMaxX, dims.y - 1, openingMaxZ},
                        AquariumMaterial::Empty);
        return std::move(builder.data());
    }

    // keep room volume allocation for stable volume indexing, but leave it empty so
    // the large surrounding shell is not rendered.
    return std::move(builder.data());
}

std::vector<uint8_t> AquariumScene::buildTankCompositeVolume(const glm::ivec3& dims,
                                                             bool useMeshTankGlass,
                                                             AquariumLayout layout,
                                                             bool includeWater,
                                                             const std::vector<PlaceableInstance>&
                                                                 heroFoliageInstances)
{
    engine::VoxelBuilder builder(dims);

    if (layout == AquariumLayout::Fishbowl)
    {
        const float cx = (static_cast<float>(dims.x) - 1.0f) * 0.5f;
        const float cz = (static_cast<float>(dims.z) - 1.0f) * 0.5f;
        const int glassBottom = 3;
        const int glassTop = dims.y - 4;
        const int gravelBase = 5;
        const int waterBottom = 6;
        const int waterTop = static_cast<int>(std::floor(fishbowlWaterTopLocalY(dims)));
        const float substrateRadius =
            std::min(fishbowlInnerRadiusAtLocalY(static_cast<float>(gravelBase + 2), dims) - 2.0f,
                     static_cast<float>(std::min(dims.x, dims.z)) * 0.30f);

        auto radialDistance = [&](int x, int z) {
            const float dx = (static_cast<float>(x) + 0.5f) - cx;
            const float dz = (static_cast<float>(z) + 0.5f) - cz;
            return std::sqrt(dx * dx + dz * dz);
        };

        auto insideBowl = [&](int x, int y, int z, float inset) {
            const float radius =
                fishbowlInnerRadiusAtLocalY(static_cast<float>(y) + 0.5f, dims) - inset;
            return radialDistance(x, z) <= std::max(0.0f, radius);
        };

        if (!useMeshTankGlass)
        {
            for (int y = glassBottom; y <= glassTop; ++y)
            {
                const float outerRadius = fishbowlOuterRadiusAtLocalY(static_cast<float>(y), dims);
                const float innerRadius = fishbowlInnerRadiusAtLocalY(static_cast<float>(y), dims);
                for (int z = 0; z < dims.z; ++z)
                {
                    for (int x = 0; x < dims.x; ++x)
                    {
                        const float r = radialDistance(x, z);
                        const bool shell = r <= outerRadius && r >= innerRadius;
                        const bool heavyBase = y <= glassBottom + 1 && r <= innerRadius;
                        const bool rim = y >= glassTop - 1 && r >= innerRadius - 2.0f;
                        if (shell || heavyBase || rim)
                        {
                            builder.setVoxel(x, y, z, AquariumMaterial::Glass);
                        }
                    }
                }
            }
        }
        else
        {
            for (int z = 0; z < dims.z; ++z)
            {
                for (int x = 0; x < dims.x; ++x)
                {
                    const float r = radialDistance(x, z);
                    if (r <= 10.5f && r >= 5.0f)
                    {
                        builder.setVoxel(x, glassBottom, z, AquariumMaterial::Glass);
                    }
                    if (r <= 8.0f)
                    {
                        builder.setVoxel(x, glassBottom - 1, z, AquariumMaterial::Glass);
                    }
                }
            }
        }

        if (includeWater)
        {
            for (int y = waterBottom; y <= waterTop; ++y)
            {
                const float waterRadius =
                    std::min(fishbowlInnerRadiusAtLocalY(static_cast<float>(y), dims) - 2.2f,
                             fishbowlWaterBoundsRadius(dims) + 1.0f);
                for (int z = 0; z < dims.z; ++z)
                {
                    for (int x = 0; x < dims.x; ++x)
                    {
                        if (radialDistance(x, z) <= waterRadius)
                        {
                            builder.setVoxel(x, y, z, AquariumMaterial::Water);
                        }
                    }
                }
            }
        }

        for (int z = 0; z < dims.z; ++z)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                const float r = radialDistance(x, z);
                if (r > substrateRadius)
                {
                    continue;
                }

                const float d = r / std::max(1.0f, substrateRadius);
                const uint32_t h = SpatialHash::hash3D(x, gravelBase, z);
                const int mound = d < 0.42f ? 2 : (d < 0.74f ? 1 : 0);
                const int pebble = (h & 7u) == 0u ? 1 : 0;
                const int surfaceY = std::min(waterTop - 2, gravelBase + mound + pebble);
                for (int y = gravelBase; y <= surfaceY; ++y)
                {
                    const uint8_t id = SpatialHash::gravelDepthIndex(
                        AquariumMaterial::Gravel::Base, AquariumMaterial::Gravel::Count, x, y,
                        z, gravelBase, gravelBase + 4);
                    builder.setVoxel(x, y, z, id);
                }

                if ((h & 63u) == 3u && r < substrateRadius - 1.5f)
                {
                    const uint8_t shellId = static_cast<uint8_t>(
                        AquariumMaterial::Gravel::Base + 8 + static_cast<int>((h >> 6u) % 3u));
                    builder.setVoxel(x, surfaceY + 1, z, shellId);
                }
            }
        }

        auto setDecorVoxel = [&](int x, int y, int z, uint8_t id) {
            if (x >= 0 && x < dims.x && y >= 0 && y < dims.y && z >= 0 && z < dims.z &&
                insideBowl(x, y, z, 1.2f))
            {
                builder.setVoxel(x, y, z, id);
            }
        };

        auto fillEllipsoidWithGradient = [&](glm::vec3 center, glm::vec3 radii, uint8_t base,
                                             uint8_t count) {
            const int x0 = std::max(0, static_cast<int>(std::floor(center.x - radii.x)));
            const int x1 = std::min(dims.x - 1, static_cast<int>(std::ceil(center.x + radii.x)));
            const int y0 = std::max(0, static_cast<int>(std::floor(center.y - radii.y)));
            const int y1 = std::min(dims.y - 1, static_cast<int>(std::ceil(center.y + radii.y)));
            const int z0 = std::max(0, static_cast<int>(std::floor(center.z - radii.z)));
            const int z1 = std::min(dims.z - 1, static_cast<int>(std::ceil(center.z + radii.z)));
            const float maxRadius = std::max(radii.x, std::max(radii.y, radii.z));
            for (int z = z0; z <= z1; ++z)
            {
                for (int y = y0; y <= y1; ++y)
                {
                    for (int x = x0; x <= x1; ++x)
                    {
                        const glm::vec3 p(static_cast<float>(x) + 0.5f,
                                          static_cast<float>(y) + 0.5f,
                                          static_cast<float>(z) + 0.5f);
                        const glm::vec3 d = (p - center) / radii;
                        if (glm::dot(d, d) <= 1.0f && insideBowl(x, y, z, 1.0f))
                        {
                            const uint8_t id = SpatialHash::gradientIndex(
                                base, count, x, y, z, center.x, center.y, center.z, maxRadius);
                            builder.setVoxel(x, y, z, id);
                        }
                    }
                }
            }
        };

        fillEllipsoidWithGradient(glm::vec3(18.0f, 8.1f, 22.0f), glm::vec3(4.0f, 2.3f, 3.0f),
                                  AquariumMaterial::Stone::Base, AquariumMaterial::Stone::Count);
        fillEllipsoidWithGradient(glm::vec3(34.0f, 7.4f, 33.0f), glm::vec3(3.2f, 1.8f, 2.4f),
                                  AquariumMaterial::Stone::Base, AquariumMaterial::Stone::Count);
        fillEllipsoidWithGradient(glm::vec3(29.0f, 8.0f, 18.0f), glm::vec3(2.4f, 1.5f, 2.1f),
                                  AquariumMaterial::Stone::Base, AquariumMaterial::Stone::Count);
        fillEllipsoidWithGradient(glm::vec3(39.0f, 7.6f, 22.0f), glm::vec3(3.0f, 1.4f, 2.3f),
                                  AquariumMaterial::Stone::Base, AquariumMaterial::Stone::Count);
        fillEllipsoidWithGradient(glm::vec3(17.0f, 7.4f, 34.0f), glm::vec3(2.3f, 1.2f, 1.8f),
                                  AquariumMaterial::Stone::Base, AquariumMaterial::Stone::Count);

        // small deterministic pebble groups break up the broad gravel mound without
        // consuming additional volumes or closing the central fish corridor.
        fillEllipsoidWithGradient(glm::vec3(22.0f, 7.4f, 17.0f), glm::vec3(1.3f, 1.1f, 1.0f),
                                  AquariumMaterial::Stone::Base, AquariumMaterial::Stone::Count);
        fillEllipsoidWithGradient(glm::vec3(36.0f, 6.3f, 39.0f), glm::vec3(1.5f, 1.0f, 1.1f),
                                  AquariumMaterial::Stone::Base, AquariumMaterial::Stone::Count);
        fillEllipsoidWithGradient(glm::vec3(16.0f, 7.0f, 27.0f), glm::vec3(1.2f, 1.1f, 1.0f),
                                  AquariumMaterial::Stone::Base, AquariumMaterial::Stone::Count);

        for (int i = 0; i < 19; ++i)
        {
            const int x = 15 + i;
            const int y = 8 + (i % 6 == 0 ? 1 : 0);
            const int z = 37 - i / 2 + static_cast<int>(std::sin(static_cast<float>(i) * 0.8f));
            const uint8_t woodId = SpatialHash::woodGrainIndex(
                AquariumMaterial::Wood::Base, AquariumMaterial::Wood::Count, x, y, z);
            setDecorVoxel(x, y, z, woodId);
            if ((i % 4) == 1)
            {
                setDecorVoxel(x, y + 1, z, woodId);
                setDecorVoxel(x + 1, y, z - 1, woodId);
            }
            if ((i % 5) == 2)
            {
                setDecorVoxel(x - 1, y, z + 1, woodId);
            }
        }

        // a shorter fork gives the existing driftwood a layered silhouette while
        // keeping its mass low enough that fish remain visible through the bowl.
        for (int i = 0; i < 11; ++i)
        {
            const int x = 22 + i;
            const int y = 9 + (i >= 7 ? 1 : 0);
            const int z = 34 + i / 3;
            const uint8_t woodId = SpatialHash::woodGrainIndex(
                AquariumMaterial::Wood::Base, AquariumMaterial::Wood::Count, x, y, z);
            setDecorVoxel(x, y, z, woodId);
            if ((i % 3) == 1)
            {
                setDecorVoxel(x, y + 1, z, woodId);
            }
        }

        auto addCoralSprig = [&](int baseX, int baseZ, int height, int branchDir,
                                 uint32_t seed) {
            const int baseY = 8;
            const int topY = std::min(waterTop - 2, baseY + height);
            for (int y = baseY; y <= topY; ++y)
            {
                uint8_t id = SpatialHash::verticalGradientIndex(
                    AquariumMaterial::Coral::Base, AquariumMaterial::Coral::Count,
                    baseX + static_cast<int>(seed % 5u), y, baseZ, baseY,
                    std::max(1, topY - baseY));
                setDecorVoxel(baseX, y, baseZ, id);
                if (y > baseY + 1 && ((y + static_cast<int>(seed)) & 1) == 0)
                {
                    const int reach = 1 + (y - baseY) / 4;
                    for (int d = 1; d <= std::min(reach, 3); ++d)
                    {
                        const int x = baseX + branchDir * d;
                        const int z = baseZ + (((d + static_cast<int>(seed)) & 1) == 0 ? 1 : -1);
                        id = SpatialHash::verticalGradientIndex(
                            AquariumMaterial::Coral::Base, AquariumMaterial::Coral::Count, x, y,
                            z, baseY, std::max(1, topY - baseY));
                        setDecorVoxel(x, y, z, id);
                    }
                }
            }
        };

        addCoralSprig(38, 25, 7, -1, 317u);
        addCoralSprig(22, 31, 5, 1, 509u);
        addCoralSprig(32, 39, 4, -1, 617u);
        addCoralSprig(17, 27, 4, 1, 733u);
        addCoralSprig(36, 18, 5, -1, 827u);

        return std::move(builder.data());
    }

    if (layout == AquariumLayout::SunroofChamber)
    {
        const int wall = 2;
        const int xMin = wall;
        const int yMin = wall;
        const int zMin = wall;
        const int xMax = dims.x - 1 - wall;
        const int zMax = dims.z - 1 - wall;
        const int waterTop = std::max(yMin, dims.y - wall - 1);

        // for the sunroof scene the chamber itself is the tank. fill the full interior
        // volume with water instead of creating a smaller freestanding box in the middle.
        if (includeWater)
        {
            builder.fillBox({xMin, yMin, zMin}, {xMax, waterTop, zMax}, AquariumMaterial::Water);
        }
        return std::move(builder.data());
    }

    // wall thickness of 2 ensures glass voxels extend into dda-traversable region
    // (dda skips outermost 1-voxel shell at coords 0 and dims-1)
    const int wall = 2;
    const int xMin = wall;
    const int yMin = wall;
    const int zMin = wall;
    const int xMax = dims.x - 1 - wall;
    const int yMax = dims.y - 1 - wall;
    const int zMax = dims.z - 1 - wall;

    if (useMeshTankGlass)
    {
        // build a solid frame shell first, then carve viewing apertures where mesh glass
        // panels will be composited later in GlassPass.
        builder.fillShellBox({0, 0, 0}, dims - glm::ivec3(1), wall, AquariumMaterial::TankFrame);

        // open the full top shell thickness so the tank does not get an interior ceiling.
        builder.fillBox({xMin, dims.y - wall, zMin}, {xMax, dims.y - 1, zMax},
                        AquariumMaterial::Empty);

        // apertures leave a one-voxel frame margin on each edge to keep a visible rim.
        const int apertureXMin = xMin + 1;
        const int apertureXMax = xMax - 1;
        const int apertureYMin = yMin + 1;
        const int apertureYMax = yMax - 1;
        const int apertureZMin = zMin + 1;
        const int apertureZMax = zMax - 1;

        // front and back wall apertures (2-voxel shell thickness).
        builder.fillBox({apertureXMin, apertureYMin, 0},
                        {apertureXMax, apertureYMax, wall - 1},
                        AquariumMaterial::Empty);
        builder.fillBox({apertureXMin, apertureYMin, dims.z - wall},
                        {apertureXMax, apertureYMax, dims.z - 1},
                        AquariumMaterial::Empty);

        // left and right wall apertures.
        builder.fillBox({0, apertureYMin, apertureZMin},
                        {wall - 1, apertureYMax, apertureZMax},
                        AquariumMaterial::Empty);
        builder.fillBox({dims.x - wall, apertureYMin, apertureZMin},
                        {dims.x - 1, apertureYMax, apertureZMax},
                        AquariumMaterial::Empty);

        applyTankFrameVariation(builder, dims);
        addTankFrameHardware(builder, dims);
    }
    else
    {
        // legacy voxel-glass shell path (open top).
        builder.fillShellBox({0, 0, 0}, dims - glm::ivec3(1), wall, AquariumMaterial::Glass);
        builder.fillBox({xMin, dims.y - wall, zMin}, {xMax, dims.y - 1, zMax},
                        AquariumMaterial::Empty);
    }

    // Water volume inside the shell, leaving air gap near the rim.
    const int waterTop = std::max(yMin, dims.y - 3);
    if (includeWater)
    {
        builder.fillBox({xMin, yMin, zMin}, {xMax, waterTop, zMax}, AquariumMaterial::Water);
    }

    if (useMeshTankGlass)
    {
        addInteriorCornerSeams(builder, xMin, yMin, zMin, xMax, yMax, zMax);
    }

    // Gravel bottom with depth-layered variation (darker at bottom, lighter at top)
    // using gravelDepthIndex for teardown-quality directional patterns
    const int gravelDepth = 3;  // Gravel layer is ~3 voxels deep
    for (int z = zMin; z <= zMax; ++z)
    {
        for (int x = xMin; x <= xMax; ++x)
        {
            uint8_t idx = SpatialHash::gravelDepthIndex(
                AquariumMaterial::Gravel::Base, AquariumMaterial::Gravel::Count,
                x, yMin, z, yMin, yMin + gravelDepth);
            builder.setVoxel(x, yMin, z, idx);

            const uint32_t pebbleHash = SpatialHash::hash3D(x, yMin + 1, z);
            if ((pebbleHash & 7u) == 0u &&
                !isPlantedFootprintTankCell(x, z, heroFoliageInstances))
            {
                const int pebbleY = yMin + 1;
                idx = SpatialHash::gravelDepthIndex(AquariumMaterial::Gravel::Base,
                                                    AquariumMaterial::Gravel::Count, x, pebbleY,
                                                    z, yMin, yMin + gravelDepth);
                builder.setVoxel(x, pebbleY, z, idx);
            }
        }
    }

    // two rock clusters with center-to-edge gradient variation.
    // darker at center (base), lighter towards edges (surface weathering).
    auto fillSphereWithGradient = [&](glm::vec3 center, float radius, uint8_t base, uint8_t count)
    {
        int r = static_cast<int>(std::ceil(radius));
        int cx = static_cast<int>(center.x);
        int cy = static_cast<int>(center.y);
        int cz = static_cast<int>(center.z);
        float r2 = radius * radius;
        for (int dz = -r; dz <= r; ++dz)
        {
            for (int dy = -r; dy <= r; ++dy)
            {
                for (int dx = -r; dx <= r; ++dx)
                {
                    if (dx * dx + dy * dy + dz * dz <= r2)
                    {
                        int x = cx + dx;
                        int y = cy + dy;
                        int z = cz + dz;
                        if (x < xMin || x > xMax || y < yMin || y > waterTop || z < zMin ||
                            z > zMax)
                        {
                            continue;
                        }
                        // use gradient from center - darker inside, lighter at edges
                        uint8_t idx = SpatialHash::gradientIndex(
                            base, count, x, y, z, center.x, center.y, center.z, radius);
                        builder.setVoxel(x, y, z, idx);
                    }
                }
            }
        }
    };

    fillSphereWithGradient(glm::vec3(12.0f, 3.5f, 12.0f), 3.2f,
                           AquariumMaterial::Stone::Base, AquariumMaterial::Stone::Count);
    fillSphereWithGradient(glm::vec3(35.0f, 2.8f, 23.0f), 2.4f,
                           AquariumMaterial::Stone::Base, AquariumMaterial::Stone::Count);

    // small driftwood branch on the gravel bed. this gives the wood band a visible,
    // floor-bound use without changing the main tank silhouette.
    for (int i = 0; i < 12; ++i)
    {
        const int x = xMin + 6 + i;
        const int y = yMin + 1 + ((i % 5) == 0 ? 1 : 0);
        const int z = zMin + 6 + i / 2;
        const uint8_t woodId = SpatialHash::woodGrainIndex(
            AquariumMaterial::Wood::Base, AquariumMaterial::Wood::Count, x, y, z);
        builder.setVoxel(x, y, z, woodId);
        if ((i % 4) == 1)
        {
            builder.setVoxel(x, y, z + 1, woodId);
        }
        if ((i % 5) == 2)
        {
            builder.setVoxel(x, y + 1, z, woodId);
        }
    }

    auto addCoralSprig = [&](int baseX, int baseZ, int height, int branchDir)
    {
        const int baseY = yMin + 1;
        const int topY = std::min(waterTop - 1, baseY + height);
        const int coralHeight = std::max(1, topY - baseY);
        for (int y = baseY; y <= topY; ++y)
        {
            uint8_t id = SpatialHash::verticalGradientIndex(
                AquariumMaterial::Coral::Base, AquariumMaterial::Coral::Count,
                baseX, y, baseZ, baseY, coralHeight);
            builder.setVoxel(baseX, y, baseZ, id);

            if (y > baseY + 1 && ((y - baseY) % 2) == 0)
            {
                const int reach = 1 + ((y - baseY) / 3);
                for (int d = 1; d <= std::min(reach, 3); ++d)
                {
                    const int x = baseX + branchDir * d;
                    const int z = baseZ + ((d & 1) == 0 ? 1 : -1);
                    id = SpatialHash::verticalGradientIndex(
                        AquariumMaterial::Coral::Base, AquariumMaterial::Coral::Count,
                        x, y, z, baseY, coralHeight);
                    builder.setVoxel(x, y, z, id);
                }
            }
        }
    };

    addCoralSprig(xMin + 29, zMin + 8, 7, -1);
    addCoralSprig(xMin + 36, zMin + 18, 5, 1);

    return std::move(builder.data());
}

std::vector<uint8_t> AquariumScene::buildFineFoliageVolume(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);

    auto foliageBandId = [](uint8_t base, uint8_t count, int x, int y, int z, int baseY,
                            int height, uint32_t seed) {
        const int seedX = x + static_cast<int>((seed & 0xffu) * 17u);
        const int seedZ = z + static_cast<int>(((seed >> 8u) & 0xffu) * 19u);
        return SpatialHash::verticalGradientIndex(base, count, seedX, y, seedZ, baseY, height);
    };

    auto plantBandId = [&](int x, int y, int z, int baseY, int height, uint32_t seed) {
        return foliageBandId(AquariumMaterial::PlantBand::Base,
                             AquariumMaterial::PlantBand::Count, x, y, z, baseY, height,
                             seed);
    };

    auto setPlantVoxel = [&](int x, int y, int z, int baseY, int height, uint32_t seed) {
        if (x >= 0 && x < dims.x && y >= 0 && y < dims.y && z >= 0 && z < dims.z)
        {
            builder.setVoxel(x, y, z, plantBandId(x, y, z, baseY, height, seed));
        }
    };

    auto setConnectedPlantVoxel = [&](int fromX, int fromZ, int x, int y, int z, int baseY,
                                      int height, uint32_t seed) {
        int cx = fromX;
        int cz = fromZ;
        setPlantVoxel(cx, y, cz, baseY, height, seed);
        while (cx != x)
        {
            cx += (x > cx) ? 1 : -1;
            setPlantVoxel(cx, y, cz, baseY, height, seed);
        }
        while (cz != z)
        {
            cz += (z > cz) ? 1 : -1;
            setPlantVoxel(cx, y, cz, baseY, height, seed);
        }
    };

    auto setGroundCoverVoxel = [&](uint8_t base, uint8_t count, int x, int z, uint32_t seed,
                                   int shadeMin, int shadeMax) {
        if (x >= 0 && x < dims.x && z >= 0 && z < dims.z)
        {
            const int safeMin = std::clamp(shadeMin, 0, static_cast<int>(count) - 1);
            const int safeMax = std::clamp(shadeMax, safeMin, static_cast<int>(count) - 1);
            const uint32_t h = SpatialHash::hash3D(x + static_cast<int>(seed), 1,
                                                   z - static_cast<int>(seed));
            const int shade =
                safeMin + static_cast<int>(h % static_cast<uint32_t>(safeMax - safeMin + 1));
            builder.setVoxel(x, 0, z, static_cast<uint8_t>(base + shade));
        }
    };

    auto bladePointAt = [&](int baseX, int baseZ, int dy, int bladeHeight, uint32_t seed,
                            int leanX, int leanZ, int tipSway) {
        const int clampedHeight = std::clamp(bladeHeight, 2, dims.y - 1);
        const uint32_t curveHash =
            SpatialHash::hash3D(static_cast<int>(seed), clampedHeight * 13, tipSway * 17);
        if (leanX == 0 && leanZ == 0)
        {
            if ((curveHash & 1u) != 0u)
            {
                leanX = ((curveHash >> 1u) & 1u) != 0u ? 1 : -1;
            }
            else
            {
                leanZ = ((curveHash >> 2u) & 1u) != 0u ? 1 : -1;
            }
        }
        const int crossX = leanZ != 0 ? (((curveHash >> 4u) & 1u) != 0u ? 1 : -1) : 0;
        const int crossZ = leanX != 0 ? (((curveHash >> 5u) & 1u) != 0u ? 1 : -1) : 0;
        const int bend = (dy >= 2 ? 1 : 0) + (dy >= 4 ? 1 : 0) + (dy >= 6 ? 1 : 0);
        const int wiggle = dy >= 2 && ((dy + static_cast<int>(curveHash)) & 1) != 0 ? 1 : 0;
        const int tipBend = dy >= clampedHeight - 1 ? tipSway : 0;
        return glm::ivec3{baseX + leanX * bend + crossX * wiggle + tipBend, dy,
                          baseZ + leanZ * bend + crossZ * wiggle};
    };

    auto addBlade = [&](int baseX, int baseZ, int height, uint32_t seed, int leanX, int leanZ,
                        int tipSway) {
        const int bladeHeight = std::clamp(height, 2, dims.y - 1);
        glm::ivec3 previous{baseX, 0, baseZ};
        bool hasPrevious = false;
        for (int dy = 0; dy < bladeHeight; ++dy)
        {
            const glm::ivec3 point =
                bladePointAt(baseX, baseZ, dy, bladeHeight, seed, leanX, leanZ, tipSway);
            if (hasPrevious)
            {
                setConnectedPlantVoxel(previous.x, previous.z, point.x, point.y, point.z, 0,
                                       bladeHeight, seed);
            }
            else
            {
                setPlantVoxel(point.x, point.y, point.z, 0, bladeHeight, seed);
            }
            previous = point;
            hasPrevious = true;
        }
    };

    auto addBaseSprouts = [&](int baseX, int baseZ, int radius, int count, uint32_t seed) {
        for (int sprout = 0; sprout < count; ++sprout)
        {
            const uint32_t sproutHash =
                SpatialHash::hash3D(static_cast<int>(seed), sprout * 37, radius);
            const int range = radius * 2 + 1;
            const int offsetX = static_cast<int>((sproutHash >> 2u) % range) - radius;
            const int offsetZ = static_cast<int>((sproutHash >> 9u) % range) - radius;
            const int sproutHeight = 1 + static_cast<int>((sproutHash >> 16u) % 3u);
            const int leanX = static_cast<int>((sproutHash >> 5u) % 3u) - 1;
            const int leanZ = static_cast<int>((sproutHash >> 12u) % 3u) - 1;
            glm::ivec3 previous{baseX + offsetX, 0, baseZ + offsetZ};
            bool hasPrevious = false;
            for (int dy = 0; dy < sproutHeight; ++dy)
            {
                const int bend = dy == sproutHeight - 1 && sproutHeight > 1 ? 1 : 0;
                const glm::ivec3 point{baseX + offsetX + leanX * bend, dy,
                                       baseZ + offsetZ + leanZ * bend};
                if (hasPrevious)
                {
                    setConnectedPlantVoxel(previous.x, previous.z, point.x, point.y, point.z,
                                           0, 5, seed + sprout * 17u);
                }
                else
                {
                    setPlantVoxel(point.x, point.y, point.z, 0, 5, seed + sprout * 17u);
                }
                previous = point;
                hasPrevious = true;
            }
        }
    };

    auto addLowCluster = [&](int baseX, int baseZ, uint32_t seed) {
        addBaseSprouts(baseX, baseZ, 7, 3 + static_cast<int>(seed % 2u), seed);
        const uint32_t bladeHash = SpatialHash::hash3D(static_cast<int>(seed), baseX, baseZ);
        if ((bladeHash & 7u) == 0u)
        {
            const int leanX = static_cast<int>((bladeHash >> 3u) % 3u) - 1;
            const int leanZ = static_cast<int>((bladeHash >> 6u) % 3u) - 1;
            const int tipSway = static_cast<int>((bladeHash >> 11u) % 3u) - 1;
            addBlade(baseX, baseZ, 2 + static_cast<int>((bladeHash >> 14u) % 2u),
                     seed + 97u, leanX, leanZ, tipSway);
        }
    };

    auto addGroundCoverPatch = [&](uint8_t base, uint8_t count, int centerX, int centerZ,
                                   int radiusX, int radiusZ, uint32_t seed, int maxHeight,
                                   int holeChancePercent) {
        const int minX = std::max(0, centerX - radiusX);
        const int maxX = std::min(dims.x - 1, centerX + radiusX);
        const int minZ = std::max(0, centerZ - radiusZ);
        const int maxZ = std::min(dims.z - 1, centerZ + radiusZ);
        const int holeCellSize = maxHeight >= 2 ? 4 : 5;
        const int shadeMin = maxHeight >= 2 ? 2 : 1;
        const int shadeMax = maxHeight >= 2 ? 8 : 7;
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const float nx =
                    static_cast<float>(x - centerX) / static_cast<float>(std::max(1, radiusX));
                const float nz =
                    static_cast<float>(z - centerZ) / static_cast<float>(std::max(1, radiusZ));
                const float d2 = nx * nx + nz * nz;
                if (d2 > 1.0f)
                {
                    continue;
                }

                const uint32_t h = SpatialHash::hash3D(x + static_cast<int>(seed), 0,
                                                       z - static_cast<int>(seed));
                const int cellX = x / holeCellSize;
                const int cellZ = z / holeCellSize;
                const uint32_t holeHash =
                    SpatialHash::hash3D(cellX + static_cast<int>(seed), holeChancePercent,
                                        cellZ - static_cast<int>(seed));
                const int edgeReject =
                    d2 > 0.68f ? static_cast<int>(((d2 - 0.68f) / 0.32f) * 70.0f) : 0;
                if (static_cast<int>(h % 100u) < edgeReject)
                {
                    continue;
                }

                const int holeChance = std::max(0, holeChancePercent);
                if (d2 < 0.88f && static_cast<int>(holeHash % 100u) < holeChance)
                {
                    continue;
                }

                setGroundCoverVoxel(base, count, x, z, seed, shadeMin, shadeMax);
            }
        }
    };

    auto addDenseGroundCoverPocket = [&](uint8_t base, uint8_t count, int centerX, int centerZ,
                                         int radiusX, int radiusZ, uint32_t seed, int shadeMin,
                                         int shadeMax, int holeChancePercent) {
        const int minX = std::max(0, centerX - radiusX);
        const int maxX = std::min(dims.x - 1, centerX + radiusX);
        const int minZ = std::max(0, centerZ - radiusZ);
        const int maxZ = std::min(dims.z - 1, centerZ + radiusZ);
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const float nx =
                    static_cast<float>(x - centerX) / static_cast<float>(std::max(1, radiusX));
                const float nz =
                    static_cast<float>(z - centerZ) / static_cast<float>(std::max(1, radiusZ));
                const float d2 = nx * nx + nz * nz;
                if (d2 > 1.0f)
                {
                    continue;
                }

                const uint32_t h = SpatialHash::hash3D(x + static_cast<int>(seed), 2,
                                                       z - static_cast<int>(seed));
                const uint32_t contourHash =
                    SpatialHash::hash3D((x / 3) + static_cast<int>(seed), 7,
                                        (z / 3) - static_cast<int>(seed));
                const int edgeReject =
                    d2 > 0.58f ? static_cast<int>(((d2 - 0.58f) / 0.42f) * 46.0f) : 0;
                if (static_cast<int>(h % 100u) < edgeReject)
                {
                    continue;
                }

                const int holeChance = static_cast<int>(
                    static_cast<float>(std::max(0, holeChancePercent)) *
                    (d2 < 0.38f ? 0.25f : (d2 < 0.72f ? 0.55f : 1.0f)));
                if (static_cast<int>(contourHash % 100u) < holeChance)
                {
                    continue;
                }

                const float core = 1.0f - std::clamp(d2, 0.0f, 1.0f);
                const int centerLift = core > 0.62f ? 1 : 0;
                setGroundCoverVoxel(base, count, x, z, seed + 19u, shadeMin + centerLift,
                                    shadeMax);
            }
        }
    };

    auto addGrassPatch = [&](int centerX, int centerZ, int radiusX, int radiusZ, uint32_t seed) {
        addGroundCoverPatch(AquariumMaterial::GrassBand::Base,
                            AquariumMaterial::GrassBand::Count, centerX, centerZ,
                            radiusX, radiusZ, seed, 2, 8);
    };

    auto addAlgaePatch = [&](int centerX, int centerZ, int radiusX, int radiusZ, uint32_t seed) {
        addGroundCoverPatch(AquariumMaterial::AlgaeBand::Base,
                            AquariumMaterial::AlgaeBand::Count, centerX, centerZ,
                            radiusX, radiusZ, seed, 2, 12);
    };

    auto addDenseGrassPocket = [&](int centerX, int centerZ, int radiusX, int radiusZ,
                                   uint32_t seed) {
        addDenseGroundCoverPocket(AquariumMaterial::GrassBand::Base,
                                  AquariumMaterial::GrassBand::Count, centerX, centerZ,
                                  radiusX, radiusZ, seed, 3, 9, 4);
    };

    auto addDenseAlgaePocket = [&](int centerX, int centerZ, int radiusX, int radiusZ,
                                   uint32_t seed) {
        addDenseGroundCoverPocket(AquariumMaterial::AlgaeBand::Base,
                                  AquariumMaterial::AlgaeBand::Count, centerX, centerZ,
                                  radiusX, radiusZ, seed, 4, 9, 7);
    };

    auto addAccentPatch = [&](int centerX, int centerZ, int radiusX, int radiusZ,
                              uint32_t seed) {
        addGroundCoverPatch(AquariumMaterial::FoliageAccentBand::Base,
                            AquariumMaterial::FoliageAccentBand::Count, centerX, centerZ,
                            radiusX, radiusZ, seed, 2, 11);
    };

    auto addDenseAccentPocket = [&](int centerX, int centerZ, int radiusX, int radiusZ,
                                    uint32_t seed) {
        addDenseGroundCoverPocket(AquariumMaterial::FoliageAccentBand::Base,
                                  AquariumMaterial::FoliageAccentBand::Count, centerX,
                                  centerZ, radiusX, radiusZ, seed, 3, 11, 5);
    };

    for (const FineFoliagePatch& patch : kFineFoliagePatches)
    {
        switch (patch.kind)
        {
        case FineFoliagePatchKind::GrassPatch:
            addGrassPatch(patch.x, patch.z, patch.radiusX, patch.radiusZ, patch.seed);
            break;
        case FineFoliagePatchKind::AlgaePatch:
            addAlgaePatch(patch.x, patch.z, patch.radiusX, patch.radiusZ, patch.seed);
            break;
        case FineFoliagePatchKind::DenseGrassPocket:
            addDenseGrassPocket(patch.x, patch.z, patch.radiusX, patch.radiusZ, patch.seed);
            break;
        case FineFoliagePatchKind::DenseAlgaePocket:
            addDenseAlgaePocket(patch.x, patch.z, patch.radiusX, patch.radiusZ, patch.seed);
            break;
        case FineFoliagePatchKind::AccentPatch:
            addAccentPatch(patch.x, patch.z, patch.radiusX, patch.radiusZ, patch.seed);
            break;
        case FineFoliagePatchKind::DenseAccentPocket:
            addDenseAccentPocket(patch.x, patch.z, patch.radiusX, patch.radiusZ, patch.seed);
            break;
        case FineFoliagePatchKind::LowCluster:
            addLowCluster(patch.x, patch.z, patch.seed);
            break;
        }
    }

    return std::move(builder.data());
}

std::vector<uint8_t> AquariumScene::buildFishbowlFineFoliageVolume(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);

    const int cx = dims.x / 2;
    const int cz = dims.z / 2;
    const float bowlRadius = static_cast<float>(std::min(dims.x, dims.z)) * 0.43f;

    auto insideBowlPatch = [&](int x, int z, float inset) {
        const float dx = static_cast<float>(x - cx);
        const float dz = static_cast<float>(z - cz);
        return dx * dx + dz * dz <= (bowlRadius - inset) * (bowlRadius - inset);
    };

    auto foliageBandId = [](uint8_t base, uint8_t count, int x, int y, int z, int baseY,
                            int height, uint32_t seed) {
        return SpatialHash::verticalGradientIndex(
            base, count, x + static_cast<int>((seed & 0xffu) * 11u), y,
            z - static_cast<int>(((seed >> 8u) & 0xffu) * 13u), baseY, height);
    };

    auto plantId = [&](int x, int y, int z, int height, uint32_t seed) {
        return foliageBandId(AquariumMaterial::PlantBand::Base,
                             AquariumMaterial::PlantBand::Count, x, y, z, 0, height, seed);
    };

    auto setPlant = [&](int x, int y, int z, int height, uint32_t seed) {
        if (x >= 0 && x < dims.x && y >= 0 && y < dims.y && z >= 0 && z < dims.z &&
            insideBowlPatch(x, z, 3.0f))
        {
            builder.setVoxel(x, y, z, plantId(x, y, z, height, seed));
        }
    };

    auto connectPlant = [&](glm::ivec3 a, glm::ivec3 b, int height, uint32_t seed) {
        glm::ivec3 p = a;
        setPlant(p.x, p.y, p.z, height, seed);
        while (p.x != b.x || p.z != b.z)
        {
            if (p.x != b.x)
            {
                p.x += b.x > p.x ? 1 : -1;
            }
            if (p.z != b.z)
            {
                p.z += b.z > p.z ? 1 : -1;
            }
            p.y = b.y;
            setPlant(p.x, p.y, p.z, height, seed);
        }
    };

    auto addBlade = [&](int baseX, int baseZ, int height, int leanX, int leanZ,
                        uint32_t seed) {
        const int bladeHeight = std::clamp(height, 2, dims.y - 1);
        glm::ivec3 previous{baseX, 0, baseZ};
        bool hasPrevious = false;
        for (int y = 0; y < bladeHeight; ++y)
        {
            const int bend = (y >= 2 ? 1 : 0) + (y >= 5 ? 1 : 0) + (y >= 8 ? 1 : 0);
            const int cross = ((static_cast<int>(seed) + y) & 1) == 0 ? 0 : 1;
            glm::ivec3 point{baseX + leanX * bend + (leanZ != 0 ? cross : 0), y,
                             baseZ + leanZ * bend + (leanX != 0 ? cross : 0)};
            if (hasPrevious)
            {
                connectPlant(previous, point, bladeHeight, seed);
            }
            else
            {
                setPlant(point.x, point.y, point.z, bladeHeight, seed);
            }
            previous = point;
            hasPrevious = true;
        }
    };

    auto setGroundCover = [&](uint8_t base, uint8_t count, int x, int z, uint32_t seed,
                              int shadeMin, int shadeMax) {
        if (!insideBowlPatch(x, z, 1.5f))
        {
            return;
        }
        const int safeMin = std::clamp(shadeMin, 0, static_cast<int>(count) - 1);
        const int safeMax = std::clamp(shadeMax, safeMin, static_cast<int>(count) - 1);
        const uint32_t h =
            SpatialHash::hash3D(x + static_cast<int>(seed), 3, z - static_cast<int>(seed));
        const int shade =
            safeMin + static_cast<int>(h % static_cast<uint32_t>(safeMax - safeMin + 1));
        builder.setVoxel(x, 0, z, static_cast<uint8_t>(base + shade));
    };

    auto addPatch = [&](uint8_t base, uint8_t count, int centerX, int centerZ, int radiusX,
                        int radiusZ, uint32_t seed, int bladeDensity, bool tall) {
        const int minX = std::max(0, centerX - radiusX);
        const int maxX = std::min(dims.x - 1, centerX + radiusX);
        const int minZ = std::max(0, centerZ - radiusZ);
        const int maxZ = std::min(dims.z - 1, centerZ + radiusZ);
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const float nx =
                    static_cast<float>(x - centerX) / static_cast<float>(std::max(1, radiusX));
                const float nz =
                    static_cast<float>(z - centerZ) / static_cast<float>(std::max(1, radiusZ));
                const float d2 = nx * nx + nz * nz;
                if (d2 > 1.0f || !insideBowlPatch(x, z, 0.5f))
                {
                    continue;
                }

                const uint32_t h =
                    SpatialHash::hash3D(x + static_cast<int>(seed), 0, z - static_cast<int>(seed));
                const int edgeReject =
                    d2 > 0.70f ? static_cast<int>(((d2 - 0.70f) / 0.30f) * 55.0f) : 0;
                if (static_cast<int>(h % 100u) < edgeReject)
                {
                    continue;
                }

                setGroundCover(base, count, x, z, seed, tall ? 4 : 2, tall ? 10 : 8);
                if (static_cast<int>((h >> 8u) % 100u) < bladeDensity)
                {
                    const int leanX = static_cast<int>((h >> 13u) % 3u) - 1;
                    const int leanZ = static_cast<int>((h >> 17u) % 3u) - 1;
                    const int height = tall ? 7 + static_cast<int>((h >> 20u) % 6u)
                                            : 3 + static_cast<int>((h >> 20u) % 4u);
                    addBlade(x, z, height, leanX, leanZ, seed + h);
                }
            }
        }
    };

    // layer a planted ring around the bowl: broad low cover ties the substrate
    // together, while taller back/side pockets leave the center open for fish.
    addPatch(AquariumMaterial::GrassBand::Base, AquariumMaterial::GrassBand::Count, cx - 45,
             cz + 18, 26, 16, 211u, 15, true);
    addPatch(AquariumMaterial::AlgaeBand::Base, AquariumMaterial::AlgaeBand::Count, cx - 25,
             cz - 28, 20, 17, 337u, 11, false);
    addPatch(AquariumMaterial::GrassBand::Base, AquariumMaterial::GrassBand::Count, cx + 16,
             cz + 33, 24, 15, 461u, 14, true);
    addPatch(AquariumMaterial::AlgaeBand::Base, AquariumMaterial::AlgaeBand::Count, cx + 38,
             cz - 12, 22, 18, 593u, 10, false);
    addPatch(AquariumMaterial::GrassBand::Base, AquariumMaterial::GrassBand::Count, cx - 4,
             cz + 2, 28, 20, 719u, 17, true);
    addPatch(AquariumMaterial::AlgaeBand::Base, AquariumMaterial::AlgaeBand::Count, cx + 4,
             cz - 42, 18, 12, 887u, 9, false);
    addPatch(AquariumMaterial::FoliageAccentBand::Base,
             AquariumMaterial::FoliageAccentBand::Count, cx - 34, cz - 2, 16, 13, 997u, 8,
             false);
    addPatch(AquariumMaterial::FoliageAccentBand::Base,
             AquariumMaterial::FoliageAccentBand::Count, cx + 30, cz + 12, 15, 12, 1051u,
             9, false);
    addPatch(AquariumMaterial::GrassBand::Base, AquariumMaterial::GrassBand::Count, cx - 50,
             cz - 22, 19, 14, 1123u, 15, true);
    addPatch(AquariumMaterial::AlgaeBand::Base, AquariumMaterial::AlgaeBand::Count, cx + 51,
             cz + 24, 18, 13, 1201u, 11, false);
    addPatch(AquariumMaterial::GrassBand::Base, AquariumMaterial::GrassBand::Count, cx + 32,
             cz - 42, 20, 13, 1277u, 14, true);
    addPatch(AquariumMaterial::FoliageAccentBand::Base,
             AquariumMaterial::FoliageAccentBand::Count, cx - 18, cz + 47, 18, 12, 1327u,
             10, false);

    return std::move(builder.data());
}
