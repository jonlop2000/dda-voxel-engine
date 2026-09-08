#pragma once

#include <cstdint>

struct SceneConfig;

namespace engine
{
class VoxelWorld;
struct WaterVolume;
} // namespace engine

bool isAppBeachSandScene(const SceneConfig& sceneConfig);
bool isSunroofAquariumScene(const SceneConfig& sceneConfig);
bool isFishbowlAquariumScene(const SceneConfig& sceneConfig);
engine::WaterVolume makeSceneWaterVolume(const SceneConfig& sceneConfig,
                                         const engine::VoxelWorld& world,
                                         float waterLevel);

enum class AquariumLayout;

struct FoliagePreviewSpecies
{
    const char* label;
    const char* slug;
    uint32_t version;
};

inline constexpr FoliagePreviewSpecies kFoliagePreviewSpecies[] = {
    {"Eelgrass", "eelgrass", 1u},
    {"Ribbon Kelp", "ribbon_kelp", 1u},
    {"Bud Cluster", "bud_cluster", 1u},
    {"Forked Sprig", "forked_sprig", 1u},
};
inline constexpr const char* kFoliagePreviewSpeciesLabels[] = {
    "Eelgrass",
    "Ribbon Kelp",
    "Bud Cluster",
    "Forked Sprig",
};
inline constexpr int kFoliagePreviewSpeciesCount =
    static_cast<int>(sizeof(kFoliagePreviewSpecies) / sizeof(kFoliagePreviewSpecies[0]));

AquariumLayout aquariumLayoutForScene(const SceneConfig& sceneConfig);
