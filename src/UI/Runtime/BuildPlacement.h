#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "engine/voxel/VoxelTypes.h"

class ChunkGrid;

namespace ui
{

inline constexpr std::string_view kBuildCatalogItemAId = "build_catalog_item_a";
inline constexpr std::string_view kBuildCatalogItemBId = "build_catalog_item_b";
inline constexpr std::string_view kBuildCatalogItemCId = "build_catalog_item_c";
inline constexpr std::string_view kBuildCatalogItemDId = "build_catalog_item_d";
inline constexpr std::string_view kBuildCatalogItemEId = "build_catalog_item_e";
inline constexpr std::string_view kBuildCatalogItemFId = "build_catalog_item_f";
inline constexpr std::string_view kBuildCatalogItemGId = "build_catalog_item_g";
inline constexpr std::string_view kBuildCatalogItemHId = "build_catalog_item_h";
inline constexpr std::string_view kBuildCatalogKeyFilter = "filter";
inline constexpr std::string_view kBuildCatalogKeyEelgrass = "eelgrass";
inline constexpr std::string_view kBuildCatalogKeyRibbonKelp = "ribbon_kelp";
inline constexpr std::string_view kBuildCatalogKeyBudCluster = "bud_cluster";
inline constexpr std::string_view kBuildCatalogKeyForkedSprig = "forked_sprig";
inline constexpr std::string_view kBuildCatalogKeyDecor = "decor";
inline constexpr std::string_view kBuildCatalogKeyHeater = "heater";
inline constexpr std::string_view kBuildCatalogKeyLight = "light";
inline constexpr std::string_view kBuildCatalogKeyRock = "rock";
inline constexpr std::string_view kBuildCatalogKeyBubbler = "bubbler";

struct BuildCatalogItemDefinition
{
    std::string_view id{};
    std::string_view key{};
    std::string_view label{};
    std::string_view category{};
    float rowWidth = 0.0f;
    BlockId block = BLOCK_AIR;
    glm::ivec3 footprintVoxels{1};
    bool requiresSurface = true;
    uint32_t price = 0;
    bool unlocked = true;
    std::string_view iconId{};
    std::string_view placeablePrototypeSlug{};
    uint32_t placeablePrototypeVersion = 0;

    bool hasPlaceablePrototype() const
    {
        return !placeablePrototypeSlug.empty() && placeablePrototypeVersion != 0;
    }
};

enum class BuildPlacementInvalidReason
{
    None,
    UnknownItem,
    NoSurface,
    OutOfBounds,
    Occupied,
};

enum class BuildRemovalInvalidReason
{
    None,
    NoTarget,
    OutOfBounds,
    Empty,
};

struct BuildPlacementEvaluation
{
    const BuildCatalogItemDefinition* item = nullptr;
    BlockId block = BLOCK_AIR;
    glm::ivec3 anchorVoxel{0};
    glm::ivec3 footprintVoxels{1};
    int rotationSteps = 0;
    bool valid = false;
    BuildPlacementInvalidReason invalidReason =
        BuildPlacementInvalidReason::UnknownItem;
};

struct BuildRemovalEvaluation
{
    BlockId block = BLOCK_AIR;
    glm::ivec3 targetVoxel{0};
    bool valid = false;
    BuildRemovalInvalidReason invalidReason = BuildRemovalInvalidReason::NoTarget;
};

const char* buildPlacementInvalidReasonLabel(BuildPlacementInvalidReason reason);
const char* buildRemovalInvalidReasonLabel(BuildRemovalInvalidReason reason);
int normalizeBuildPlacementRotationSteps(int rotationSteps);
std::span<const BuildCatalogItemDefinition> runtimeBuildCatalogItems();
const BuildCatalogItemDefinition* findRuntimeBuildCatalogItem(std::string_view key);
glm::ivec3 buildPlacementRotatedFootprintVoxels(
    const BuildCatalogItemDefinition& item,
    int rotationSteps);
std::vector<glm::ivec3> buildPlacementFootprintCells(
    const BuildCatalogItemDefinition& item,
    const glm::ivec3& anchorVoxel,
    int rotationSteps = 0);
BuildPlacementEvaluation evaluateBuildPlacementCandidate(
    const ChunkGrid& grid,
    std::string_view itemKey,
    const glm::ivec3& anchorVoxel,
    bool surfaceHit,
    int rotationSteps = 0);
bool commitBuildPlacement(ChunkGrid& grid,
                          const BuildPlacementEvaluation& evaluation);
BuildRemovalEvaluation evaluateBuildRemovalCandidate(
    const ChunkGrid& grid,
    const glm::ivec3& targetVoxel,
    bool surfaceHit);
bool commitBuildRemoval(ChunkGrid& grid,
                        const BuildRemovalEvaluation& evaluation);

} // namespace ui
