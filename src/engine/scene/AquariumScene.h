#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "engine/game/FoliageArchetype.h"
#include "engine/game/Placeables.h"

struct VulkanContext;

namespace engine
{
class VoxelPalette;
class VoxelWorld;
} // namespace engine

// material ids used by the aquarium scene
namespace AquariumMaterial
{
// single-ID materials (for special handling or uniform appearance)
constexpr uint8_t Empty = 0;
constexpr uint8_t Glass = 3;     // existing glass material
constexpr uint8_t Water = 4;     // existing water material
constexpr uint8_t RoomWall = 5;  // secondary chamber trim / wall accent
constexpr uint8_t RoomFloor = 6; // dark presentation floor
constexpr uint8_t Rock = 7;      // Brown/gray rock (legacy single-color)
constexpr uint8_t Plant = 8;     // green plant (legacy single-color)
constexpr uint8_t FishOrange = 9;
constexpr uint8_t FishWhite = 10;
constexpr uint8_t TankFrame = 11; // dark charcoal shell / tank frame

// palette bands for texture variation (use with SpatialHash directional functions)
// expanded from 8 to 12 entries per material for teardown-quality variation
namespace Gravel
{
constexpr uint8_t Base = 20;
constexpr uint8_t Count = 12;
} // namespace Gravel
namespace Coral
{
constexpr uint8_t Base = 35;
constexpr uint8_t Count = 12;
} // namespace Coral
namespace Wood
{
constexpr uint8_t Base = 50;
constexpr uint8_t Count = 12;
} // namespace Wood
namespace TableWood
{
constexpr uint8_t Base = 162;
constexpr uint8_t Count = 12;
} // namespace TableWood
namespace Stone
{
constexpr uint8_t Base = 65;
constexpr uint8_t Count = 12;
} // namespace Stone
namespace PlantBand
{
constexpr uint8_t Base = 80;
constexpr uint8_t Count = 12;
} // namespace PlantBand
namespace GrassBand
{
constexpr uint8_t Base = 92;
constexpr uint8_t Count = 12;
} // namespace GrassBand
namespace AlgaeBand
{
constexpr uint8_t Base = 104;
constexpr uint8_t Count = 12;
} // namespace AlgaeBand
namespace FoliageAccentBand
{
constexpr uint8_t Base = 186;
constexpr uint8_t Count = 12;
} // namespace FoliageAccentBand
namespace TankFrameBand
{
constexpr uint8_t Base = 150;
constexpr uint8_t Count = 12;
} // namespace TankFrameBand

// animated fish palette bands. each palette supplies body shades, accent stripes,
// and fin tones so voxel fish can read with more detail than a single flat color.
namespace FishSunset
{
constexpr uint8_t BodyBase = 120;
constexpr uint8_t BodyCount = 4;
constexpr uint8_t AccentBase = 124;
constexpr uint8_t AccentCount = 2;
constexpr uint8_t FinBase = 126;
constexpr uint8_t FinCount = 2;
} // namespace FishSunset
namespace FishPearl
{
constexpr uint8_t BodyBase = 128;
constexpr uint8_t BodyCount = 4;
constexpr uint8_t AccentBase = 132;
constexpr uint8_t AccentCount = 2;
constexpr uint8_t FinBase = 134;
constexpr uint8_t FinCount = 2;
} // namespace FishPearl
namespace FishReef
{
constexpr uint8_t BodyBase = 136;
constexpr uint8_t BodyCount = 4;
constexpr uint8_t AccentBase = 140;
constexpr uint8_t AccentCount = 2;
constexpr uint8_t FinBase = 142;
constexpr uint8_t FinCount = 2;
} // namespace FishReef
constexpr uint8_t FishEye = 144;

// axolotl palette band (leucistic pink). ids 174-181 sit just above TableWood
// (162 + 12 = 174 exclusive max), keeping the band contiguous and collision-free.
namespace AxolotlPink
{
constexpr uint8_t BodyBase = 174;
constexpr uint8_t BodyCount = 4;
constexpr uint8_t GillBase = 178;
constexpr uint8_t GillCount = 2;
constexpr uint8_t FinBase = 180;
constexpr uint8_t FinCount = 2;
constexpr uint8_t DetailBase = 182; // freckle, cheek blush, belly cream, deep rose
constexpr uint8_t DetailCount = 4;
} // namespace AxolotlPink

} // namespace AquariumMaterial

// information about each volume for debug ui
struct AquariumVolumeInfo
{
    std::string name;
    glm::ivec3 dimensions;
    glm::vec3 worldPosition;
    uint32_t flags;
};

enum class AquariumLayout
{
    Default,
    SunroofChamber,
    Fishbowl,
};

struct AquariumSceneRenderData
{
    // optional animated presentation foliage. it supplements the dense voxel
    // garden and therefore never claims or replaces a legacy volume.
    std::vector<engine::game::FoliageBladeInstance> foliageInstances;
};

class AquariumScene
{
public:
    // initialize all aquarium volumes in the VoxelWorld.
    // this will setup the palette with aquarium materials and create volumes.
    static bool init(VulkanContext& ctx, engine::VoxelWorld& world, engine::VoxelPalette& palette,
                     bool useMeshTankGlass, AquariumLayout layout = AquariumLayout::Default,
                     bool includeWater = true,
                     const std::vector<PlaceableInstance>* placeables = nullptr,
                     bool useDefaultPlaceables = true,
                     AquariumSceneRenderData* renderData = nullptr);

    // get info about each volume for debug ui display
    static const std::vector<AquariumVolumeInfo>& getVolumeInfos();
    static bool getWaterBounds(glm::vec3& outMin, glm::vec3& outMax);
    static PlacementEvaluation evaluateFoliagePlacement(
        const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float maxDistance,
        std::string_view prototypeSlug, uint32_t prototypeVersion,
        const std::vector<PlaceableInstance>* placeables = nullptr,
        bool useDefaultPlaceables = true,
        AquariumLayout layout = AquariumLayout::Default);
    static PlacementEvaluation evaluateFoliagePlacementAtPosition(
        const glm::vec3& proposedPosition, std::string_view prototypeSlug,
        uint32_t prototypeVersion,
        const std::vector<PlaceableInstance>* placeables = nullptr,
        bool useDefaultPlaceables = true,
        AquariumLayout layout = AquariumLayout::Default);
    // volume indices for external reference
    static constexpr uint32_t VOLUME_ROOM = 0;
    static constexpr uint32_t VOLUME_TANK_COMPOSITE = 1;
    static constexpr uint32_t VOLUME_FINE_FOLIAGE = 2; // layout-specific scaled ground foliage.
    static constexpr uint32_t VOLUME_HERO_FOLIAGE_BEGIN = 3;
    static constexpr uint32_t VOLUME_COUNT = 2;

private:
    // volume builders - each returns voxel data ready for upload
    static std::vector<uint8_t> buildRoomVolume(const glm::ivec3& dims, AquariumLayout layout);
    static std::vector<uint8_t> buildTankCompositeVolume(const glm::ivec3& dims,
                                                         bool useMeshTankGlass,
                                                         AquariumLayout layout,
                                                         bool includeWater,
                                                         const std::vector<PlaceableInstance>&
                                                             heroFoliageInstances);
    static std::vector<uint8_t> buildFineFoliageVolume(const glm::ivec3& dims);
    static std::vector<uint8_t> buildFishbowlFineFoliageVolume(const glm::ivec3& dims);
};
