#include "UI/Runtime/BuildPlacement.h"

#include <algorithm>
#include <array>
#include <limits>

#include "engine/voxel/Chunk.h"
#include "engine/voxel/ChunkGrid.h"
#include "engine/voxel/VoxelMath.h"

namespace ui
{
namespace
{

constexpr uint32_t kRuntimeBuildFoliagePrototypeVersion = 1u;

const std::array<BuildCatalogItemDefinition, 8> kRuntimeBuildCatalogItems{{
    {kBuildCatalogItemAId, kBuildCatalogKeyFilter, "FILTER", "EQUIPMENT",
     332.0f, BLOCK_STONE, glm::ivec3(1), true, 80, true, "filter"},
    {kBuildCatalogItemBId, kBuildCatalogKeyEelgrass, "EELGRASS", "PLANTS",
     332.0f, BLOCK_LEAF, glm::ivec3(1), true, 45, true, "eelgrass",
     "eelgrass", kRuntimeBuildFoliagePrototypeVersion},
    {kBuildCatalogItemCId, kBuildCatalogKeyRibbonKelp, "RIBBON KELP", "PLANTS",
     352.0f, BLOCK_LEAF, glm::ivec3(1), true, 55, true, "ribbon_kelp",
     "ribbon_kelp", kRuntimeBuildFoliagePrototypeVersion},
    {kBuildCatalogItemDId, kBuildCatalogKeyBudCluster, "BUD CLUSTER", "PLANTS",
     344.0f, BLOCK_GRASS_TALL, glm::ivec3(1), true, 60, true, "bud_cluster",
     "bud_cluster", kRuntimeBuildFoliagePrototypeVersion},
    {kBuildCatalogItemEId, kBuildCatalogKeyForkedSprig, "FORKED SPRIG", "PLANTS",
     360.0f, BLOCK_GRASS_TALL, glm::ivec3(1), true, 65, true, "forked_sprig",
     "forked_sprig", kRuntimeBuildFoliagePrototypeVersion},
    {kBuildCatalogItemFId, kBuildCatalogKeyDecor, "DECOR", "DECOR",
     352.0f, BLOCK_SAND, glm::ivec3(2, 1, 1), true, 120, true, "decor"},
    {kBuildCatalogItemGId, kBuildCatalogKeyRock, "ROCK", "DECOR",
     292.0f, BLOCK_DIRT, glm::ivec3(2, 1, 2), true, 55, true, "rock"},
    {kBuildCatalogItemHId, kBuildCatalogKeyHeater, "HEATER", "EQUIPMENT",
     332.0f, BLOCK_LOG, glm::ivec3(1), true, 150, true, "heater"},
}};

bool voxelInOwnedChunk(const ChunkGrid& grid, const glm::ivec3& voxel)
{
    const glm::ivec3 chunk(floorDiv(voxel.x, Chunk::SX),
                           floorDiv(voxel.y, Chunk::SY),
                           floorDiv(voxel.z, Chunk::SZ));
    return grid.inBounds(chunk);
}

glm::ivec3 rotateOffsetAroundY(const glm::ivec3& offset, int rotationSteps)
{
    switch (normalizeBuildPlacementRotationSteps(rotationSteps))
    {
    case 1:
        return glm::ivec3(-offset.z, offset.y, offset.x);
    case 2:
        return glm::ivec3(-offset.x, offset.y, -offset.z);
    case 3:
        return glm::ivec3(offset.z, offset.y, -offset.x);
    default:
        return offset;
    }
}

std::vector<glm::ivec3> buildPlacementFootprintOffsets(
    const BuildCatalogItemDefinition& item,
    int rotationSteps)
{
    const glm::ivec3 footprint = glm::max(item.footprintVoxels, glm::ivec3(1));
    std::vector<glm::ivec3> offsets;
    offsets.reserve(static_cast<size_t>(footprint.x * footprint.y * footprint.z));

    glm::ivec3 minOffset(std::numeric_limits<int>::max());
    for (int y = 0; y < footprint.y; ++y)
    {
        for (int z = 0; z < footprint.z; ++z)
        {
            for (int x = 0; x < footprint.x; ++x)
            {
                const glm::ivec3 rotated =
                    rotateOffsetAroundY(glm::ivec3(x, y, z), rotationSteps);
                offsets.push_back(rotated);
                minOffset = glm::min(minOffset, rotated);
            }
        }
    }

    for (glm::ivec3& offset : offsets)
    {
        offset -= minOffset;
    }
    return offsets;
}

} // namespace

const char* buildPlacementInvalidReasonLabel(BuildPlacementInvalidReason reason)
{
    switch (reason)
    {
    case BuildPlacementInvalidReason::None:
        return "none";
    case BuildPlacementInvalidReason::UnknownItem:
        return "unknown_item";
    case BuildPlacementInvalidReason::NoSurface:
        return "no_surface";
    case BuildPlacementInvalidReason::OutOfBounds:
        return "out_of_bounds";
    case BuildPlacementInvalidReason::Occupied:
        return "occupied";
    }

    return "unknown";
}

const char* buildRemovalInvalidReasonLabel(BuildRemovalInvalidReason reason)
{
    switch (reason)
    {
    case BuildRemovalInvalidReason::None:
        return "none";
    case BuildRemovalInvalidReason::NoTarget:
        return "no_target";
    case BuildRemovalInvalidReason::OutOfBounds:
        return "out_of_bounds";
    case BuildRemovalInvalidReason::Empty:
        return "empty";
    }

    return "unknown";
}

int normalizeBuildPlacementRotationSteps(int rotationSteps)
{
    const int normalized = rotationSteps % 4;
    return normalized < 0 ? normalized + 4 : normalized;
}

std::span<const BuildCatalogItemDefinition> runtimeBuildCatalogItems()
{
    return kRuntimeBuildCatalogItems;
}

const BuildCatalogItemDefinition* findRuntimeBuildCatalogItem(std::string_view key)
{
    const auto items = runtimeBuildCatalogItems();
    const auto found =
        std::find_if(items.begin(), items.end(),
                     [key](const BuildCatalogItemDefinition& item) {
                         return item.key == key;
                     });
    return found != items.end() ? &*found : nullptr;
}

glm::ivec3 buildPlacementRotatedFootprintVoxels(
    const BuildCatalogItemDefinition& item,
    int rotationSteps)
{
    const std::vector<glm::ivec3> offsets =
        buildPlacementFootprintOffsets(item, rotationSteps);
    glm::ivec3 maxOffset(0);
    for (const glm::ivec3& offset : offsets)
    {
        maxOffset = glm::max(maxOffset, offset);
    }
    return maxOffset + glm::ivec3(1);
}

std::vector<glm::ivec3> buildPlacementFootprintCells(
    const BuildCatalogItemDefinition& item,
    const glm::ivec3& anchorVoxel,
    int rotationSteps)
{
    const std::vector<glm::ivec3> offsets =
        buildPlacementFootprintOffsets(item, rotationSteps);
    std::vector<glm::ivec3> cells;
    cells.reserve(offsets.size());
    for (const glm::ivec3& offset : offsets)
    {
        cells.push_back(anchorVoxel + offset);
    }
    return cells;
}

BuildPlacementEvaluation evaluateBuildPlacementCandidate(
    const ChunkGrid& grid,
    std::string_view itemKey,
    const glm::ivec3& anchorVoxel,
    bool surfaceHit,
    int rotationSteps)
{
    BuildPlacementEvaluation evaluation{};
    evaluation.anchorVoxel = anchorVoxel;
    evaluation.rotationSteps = normalizeBuildPlacementRotationSteps(rotationSteps);

    const BuildCatalogItemDefinition* item = findRuntimeBuildCatalogItem(itemKey);
    if (item == nullptr)
    {
        evaluation.invalidReason = BuildPlacementInvalidReason::UnknownItem;
        return evaluation;
    }

    evaluation.item = item;
    evaluation.block = item->block;
    evaluation.footprintVoxels =
        buildPlacementRotatedFootprintVoxels(*item, evaluation.rotationSteps);

    if (item->requiresSurface && !surfaceHit)
    {
        evaluation.invalidReason = BuildPlacementInvalidReason::NoSurface;
        return evaluation;
    }

    const std::vector<glm::ivec3> cells =
        buildPlacementFootprintCells(*item, anchorVoxel,
                                     evaluation.rotationSteps);
    for (const glm::ivec3& cell : cells)
    {
        if (!voxelInOwnedChunk(grid, cell))
        {
            evaluation.invalidReason = BuildPlacementInvalidReason::OutOfBounds;
            return evaluation;
        }
    }
    for (const glm::ivec3& cell : cells)
    {
        if (grid.getVoxelWorld(cell.x, cell.y, cell.z) != BLOCK_AIR)
        {
            evaluation.invalidReason = BuildPlacementInvalidReason::Occupied;
            return evaluation;
        }
    }

    evaluation.valid = true;
    evaluation.invalidReason = BuildPlacementInvalidReason::None;
    return evaluation;
}

bool commitBuildPlacement(ChunkGrid& grid,
                          const BuildPlacementEvaluation& evaluation)
{
    if (!evaluation.valid || evaluation.item == nullptr ||
        evaluation.block == BLOCK_AIR)
    {
        return false;
    }

    const std::vector<glm::ivec3> cells =
        buildPlacementFootprintCells(*evaluation.item, evaluation.anchorVoxel,
                                     evaluation.rotationSteps);
    for (const glm::ivec3& cell : cells)
    {
        if (!voxelInOwnedChunk(grid, cell) ||
            grid.getVoxelWorld(cell.x, cell.y, cell.z) != BLOCK_AIR)
        {
            return false;
        }
    }
    for (const glm::ivec3& cell : cells)
    {
        if (!grid.setVoxelWorld(cell.x, cell.y, cell.z, evaluation.block))
        {
            return false;
        }
    }

    return true;
}

BuildRemovalEvaluation evaluateBuildRemovalCandidate(
    const ChunkGrid& grid,
    const glm::ivec3& targetVoxel,
    bool surfaceHit)
{
    BuildRemovalEvaluation evaluation{};
    evaluation.targetVoxel = targetVoxel;

    if (!surfaceHit)
    {
        evaluation.invalidReason = BuildRemovalInvalidReason::NoTarget;
        return evaluation;
    }
    if (!voxelInOwnedChunk(grid, targetVoxel))
    {
        evaluation.invalidReason = BuildRemovalInvalidReason::OutOfBounds;
        return evaluation;
    }

    evaluation.block =
        grid.getVoxelWorld(targetVoxel.x, targetVoxel.y, targetVoxel.z);
    if (evaluation.block == BLOCK_AIR)
    {
        evaluation.invalidReason = BuildRemovalInvalidReason::Empty;
        return evaluation;
    }

    evaluation.valid = true;
    evaluation.invalidReason = BuildRemovalInvalidReason::None;
    return evaluation;
}

bool commitBuildRemoval(ChunkGrid& grid,
                        const BuildRemovalEvaluation& evaluation)
{
    if (!evaluation.valid || evaluation.block == BLOCK_AIR ||
        !voxelInOwnedChunk(grid, evaluation.targetVoxel))
    {
        return false;
    }

    const BlockId current =
        grid.getVoxelWorld(evaluation.targetVoxel.x, evaluation.targetVoxel.y,
                           evaluation.targetVoxel.z);
    if (current != evaluation.block)
    {
        return false;
    }

    return grid.setVoxelWorld(evaluation.targetVoxel.x,
                              evaluation.targetVoxel.y,
                              evaluation.targetVoxel.z, BLOCK_AIR);
}

} // namespace ui
