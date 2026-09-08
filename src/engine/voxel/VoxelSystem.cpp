#include "engine/voxel/VoxelSystem.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <utility>

#include "Core/JobSystem.h"
#include "engine/voxel/ChunkGrid.h"
#include "engine/voxel/GreedyMesher.h"
#include "engine/voxel/Raycast.h"
#include "engine/voxel/VoxelMath.h"
#include "engine/voxel/WorldGen.h"

namespace engine::voxel
{
namespace
{

glm::ivec3 voxelFromPointFloor(const glm::vec3& point)
{
    return glm::ivec3(static_cast<int>(std::floor(point.x)),
                      static_cast<int>(std::floor(point.y)),
                      static_cast<int>(std::floor(point.z)));
}

std::vector<glm::ivec3> collectChunkCoords(const ChunkGrid& grid)
{
    std::vector<glm::ivec3> coords{};
    coords.reserve(grid.chunks().size());
    for (const Chunk& chunk : grid.chunks())
    {
        coords.emplace_back(chunk.coord.x, chunk.coord.y, chunk.coord.z);
    }
    return coords;
}

ChunkBounds boundsForChunk(const Chunk& chunk)
{
    const glm::ivec3 coord(chunk.coord.x, chunk.coord.y, chunk.coord.z);
    const glm::vec3 min(static_cast<float>(coord.x * Chunk::SX),
                        static_cast<float>(coord.y * Chunk::SY),
                        static_cast<float>(coord.z * Chunk::SZ));
    const glm::vec3 max =
        min + glm::vec3(static_cast<float>(Chunk::SX),
                        static_cast<float>(Chunk::SY),
                        static_cast<float>(Chunk::SZ));
    return {coord, min, max};
}

bool voxelInOwnedChunk(const ChunkGrid& grid, const glm::ivec3& voxel)
{
    const glm::ivec3 chunk(floorDiv(voxel.x, Chunk::SX),
                           floorDiv(voxel.y, Chunk::SY),
                           floorDiv(voxel.z, Chunk::SZ));
    return grid.inBounds(chunk);
}

}  // namespace

WorldGridCreateResult createWorldGrid(ChunkGrid& grid, const glm::ivec3& dims)
{
    const glm::ivec3 origin(-dims.x / 2, 0, -dims.z / 2);
    grid.create(dims, origin);

    WorldGridCreateResult result{};
    result.chunkCoords = collectChunkCoords(grid);
    return result;
}

void clearWorldGrid(ChunkGrid& grid)
{
    grid.clear();
}

WorldGenerationResult generateWorldState(ChunkGrid& grid, uint32_t seed)
{
    const WorldGenStats stats = generateWorld(grid, seed);
    return {
        stats.treesPlaced,
        stats.foliagePlaced,
        std::clamp(stats.avgHeight - 1.0f, static_cast<float>(stats.minHeight),
                   static_cast<float>(stats.maxHeight)),
    };
}

GridExpansionResult expandGridToIncludeChunk(ChunkGrid& grid,
                                             const glm::ivec3& targetChunk,
                                             int paddingChunks)
{
    GridExpansionResult result{};
    const int padding = std::max(0, paddingChunks);

    auto lock = grid.lockUnique();
    if (grid.inBounds(targetChunk))
    {
        return result;
    }

    const bool gridWasEmpty = grid.chunks().empty();
    const glm::ivec3 currentOrigin = grid.origin();
    const glm::ivec3 currentEnd = currentOrigin + grid.dims();
    const glm::ivec3 targetOrigin = targetChunk - glm::ivec3(padding);
    const glm::ivec3 targetEnd = targetChunk + glm::ivec3(padding + 1);
    const glm::ivec3 gridOrigin =
        gridWasEmpty ? targetOrigin : glm::min(currentOrigin, targetOrigin);
    const glm::ivec3 gridEnd =
        gridWasEmpty ? targetEnd : glm::max(currentEnd, targetEnd);

    grid.createPreservingBlocks(gridEnd - gridOrigin, gridOrigin);
    result.expanded = true;
    result.chunkCoords = collectChunkCoords(grid);
    return result;
}

ChunkGridSummary summarizeChunkGrid(const ChunkGrid& grid)
{
    auto lock = grid.lockShared();
    ChunkGridSummary summary{};
    summary.chunkCount = grid.chunks().size();
    for (const Chunk& chunk : grid.chunks())
    {
        if (chunk.dirty.load())
        {
            ++summary.dirtyChunks;
        }
    }
    return summary;
}

std::vector<glm::ivec3> chunkCoords(const ChunkGrid& grid)
{
    auto lock = grid.lockShared();
    return collectChunkCoords(grid);
}

std::vector<ChunkBounds> chunkBounds(const ChunkGrid& grid)
{
    auto lock = grid.lockShared();
    std::vector<ChunkBounds> bounds{};
    bounds.reserve(grid.chunks().size());
    for (const Chunk& chunk : grid.chunks())
    {
        bounds.push_back(boundsForChunk(chunk));
    }
    return bounds;
}

bool containsChunk(const ChunkGrid& grid, const glm::ivec3& chunkCoord)
{
    auto lock = grid.lockShared();
    return grid.inBounds(chunkCoord);
}

RayHit raycastVoxels(const ChunkGrid& grid, const glm::vec3& rayOrigin,
                     const glm::vec3& rayDirection, float maxDistance)
{
    auto lock = grid.lockShared();
    return voxelRaycast(grid, rayOrigin, rayDirection, maxDistance);
}

void markChunkDirty(ChunkGrid& grid, const glm::ivec3& coord, bool highPriority)
{
    Chunk* chunk = grid.getChunk(coord);
    if (chunk == nullptr)
    {
        return;
    }

    chunk->dirty.store(true);
    if (highPriority)
    {
        chunk->highPriority.store(true);
    }
}

void markVoxelEditDirtyNeighbors(ChunkGrid& grid, const glm::ivec3& target,
                                 bool highPriority)
{
    const int cx = floorDiv(target.x, Chunk::SX);
    const int cy = floorDiv(target.y, Chunk::SY);
    const int cz = floorDiv(target.z, Chunk::SZ);
    const glm::ivec3 chunkCoord(cx, cy, cz);
    const int lx = floorMod(target.x, Chunk::SX);
    const int ly = floorMod(target.y, Chunk::SY);
    const int lz = floorMod(target.z, Chunk::SZ);

    markChunkDirty(grid, chunkCoord, highPriority);
    if (lx == 0)
    {
        markChunkDirty(grid, chunkCoord + glm::ivec3(-1, 0, 0), highPriority);
    }
    if (lx == Chunk::SX - 1)
    {
        markChunkDirty(grid, chunkCoord + glm::ivec3(1, 0, 0), highPriority);
    }
    if (ly == 0)
    {
        markChunkDirty(grid, chunkCoord + glm::ivec3(0, -1, 0), highPriority);
    }
    if (ly == Chunk::SY - 1)
    {
        markChunkDirty(grid, chunkCoord + glm::ivec3(0, 1, 0), highPriority);
    }
    if (lz == 0)
    {
        markChunkDirty(grid, chunkCoord + glm::ivec3(0, 0, -1), highPriority);
    }
    if (lz == Chunk::SZ - 1)
    {
        markChunkDirty(grid, chunkCoord + glm::ivec3(0, 0, 1), highPriority);
    }
}

bool applyVoxelEdit(ChunkGrid& grid, const RayHit& hit, bool place,
                    BlockId selectedBlock)
{
    const glm::ivec3 target = place ? hit.prevVoxel : hit.voxel;

    auto lock = grid.lockUnique();
    const BlockId current = grid.getVoxelWorld(target.x, target.y, target.z);
    if (place)
    {
        if (current != BLOCK_AIR)
        {
            return false;
        }
        BlockId id = selectedBlock;
        if (id == BLOCK_AIR)
        {
            id = BLOCK_DIRT;
        }
        if (!grid.setVoxelWorld(target.x, target.y, target.z, id))
        {
            return false;
        }
    }
    else
    {
        if (current == BLOCK_AIR)
        {
            return false;
        }
        if (!grid.setVoxelWorld(target.x, target.y, target.z, BLOCK_AIR))
        {
            return false;
        }
    }

    markVoxelEditDirtyNeighbors(grid, target, true);
    return true;
}

bool commitBlockPlacement(ChunkGrid& grid, std::span<const glm::ivec3> cells,
                          BlockId block)
{
    if (block == BLOCK_AIR || cells.empty())
    {
        return false;
    }

    auto lock = grid.lockUnique();
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
        if (!grid.setVoxelWorld(cell.x, cell.y, cell.z, block))
        {
            return false;
        }
    }

    for (const glm::ivec3& cell : cells)
    {
        markVoxelEditDirtyNeighbors(grid, cell, true);
    }
    return true;
}

bool commitBlockRemoval(ChunkGrid& grid, const glm::ivec3& target,
                        BlockId expectedBlock)
{
    if (expectedBlock == BLOCK_AIR)
    {
        return false;
    }

    auto lock = grid.lockUnique();
    if (!voxelInOwnedChunk(grid, target) ||
        grid.getVoxelWorld(target.x, target.y, target.z) != expectedBlock)
    {
        return false;
    }
    if (!grid.setVoxelWorld(target.x, target.y, target.z, BLOCK_AIR))
    {
        return false;
    }

    markVoxelEditDirtyNeighbors(grid, target, true);
    return true;
}

size_t scheduleDirtyChunkMeshing(ChunkGrid& grid, JobSystem& jobSystem,
                                 const glm::vec3& cameraPos, size_t maxJobs)
{
    struct Candidate
    {
        size_t index = 0;
        float distSq = 0.0f;
        bool priority = false;
    };

    std::vector<Candidate> candidates{};
    auto& chunks = grid.chunks();
    candidates.reserve(chunks.size());

    for (size_t i = 0; i < chunks.size(); ++i)
    {
        Chunk& chunk = chunks[i];
        if (!chunk.dirty.load() || chunk.meshing.load())
        {
            continue;
        }

        const glm::vec3 center(static_cast<float>(chunk.coord.x * Chunk::SX) +
                                   static_cast<float>(Chunk::SX) * 0.5f,
                               static_cast<float>(chunk.coord.y * Chunk::SY) +
                                   static_cast<float>(Chunk::SY) * 0.5f,
                               static_cast<float>(chunk.coord.z * Chunk::SZ) +
                                   static_cast<float>(Chunk::SZ) * 0.5f);
        const glm::vec3 delta = center - cameraPos;
        const float distSq = glm::dot(delta, delta);
        candidates.push_back({i, distSq, chunk.highPriority.load()});
    }

    if (candidates.empty() || maxJobs == 0)
    {
        return 0;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  if (a.priority != b.priority)
                  {
                      return a.priority > b.priority;
                  }
                  return a.distSq < b.distSq;
              });

    size_t scheduled = 0;
    const size_t limit = std::min(candidates.size(), maxJobs);
    for (size_t i = 0; i < limit; ++i)
    {
        Chunk& chunk = chunks[candidates[i].index];
        if (!chunk.dirty.load() || chunk.meshing.load())
        {
            continue;
        }

        chunk.meshing.store(true);
        chunk.dirty.store(false);
        chunk.highPriority.store(false);
        const uint64_t buildId = chunk.nextBuildId++;
        Chunk* chunkPtr = &chunk;

        jobSystem.enqueue([&grid, chunkPtr, buildId]() {
            VoxelAccessor accessor(grid);
            ChunkMeshCpu mesh{};
            mesh.buildId = buildId;
            buildGreedyMesh(accessor, *chunkPtr, mesh.vertices, mesh.indices);

            {
                std::lock_guard<std::mutex> lock(chunkPtr->pendingMutex);
                if (!chunkPtr->pending || mesh.buildId > chunkPtr->pending->buildId)
                {
                    chunkPtr->pending = std::move(mesh);
                }
            }

            chunkPtr->meshing.store(false);
        });
        ++scheduled;
    }

    return scheduled;
}

std::vector<PendingChunkMesh> consumePendingChunkMeshes(ChunkGrid& grid)
{
    std::vector<PendingChunkMesh> pendingMeshes{};
    auto& chunks = grid.chunks();
    pendingMeshes.reserve(chunks.size());

    for (size_t i = 0; i < chunks.size(); ++i)
    {
        Chunk& chunk = chunks[i];
        std::optional<ChunkMeshCpu> pending{};

        {
            std::lock_guard<std::mutex> lock(chunk.pendingMutex);
            if (chunk.pending && chunk.pending->buildId > chunk.lastAppliedBuildId)
            {
                pending = std::move(chunk.pending);
            }
            chunk.pending.reset();
        }

        if (!pending)
        {
            continue;
        }

        const uint64_t buildId = pending->buildId;
        pendingMeshes.push_back({i, std::move(*pending)});
        chunk.lastAppliedBuildId = buildId;
    }

    return pendingMeshes;
}

PlacementProbeResult raycastPlacementProbe(
    const ChunkGrid& grid,
    const PlacementRaycastProbeConfig& config)
{
    auto lock = grid.lockShared();
    const RayHit hit =
        voxelRaycast(grid, config.rayOrigin, config.rayDirection,
                     config.maxDistance);
    if (!hit.hit)
    {
        return {};
    }

    PlacementProbeResult result{};
    result.rayHit = true;
    result.hasPlacementCandidate = true;
    result.snappedToGrid = true;
    result.hitVoxel = hit.voxel;
    result.hitNormal =
        glm::clamp(hit.prevVoxel - hit.voxel, glm::ivec3(-1), glm::ivec3(1));
    result.placementVoxel = config.removalMode ? hit.voxel : hit.prevVoxel;
    result.ghostWorldPosition =
        glm::vec3(result.placementVoxel) + glm::vec3(0.5f);
    result.distance = hit.t;
    return result;
}

PlacementProbeResult fallbackPlacementProbe(
    const PlacementFallbackProbeConfig& config)
{
    float fallbackDistance = config.defaultDistance;
    if (std::fabs(config.rayDirection.y) > 0.0001f)
    {
        const float planeDistance =
            (config.planeY - config.rayOrigin.y) / config.rayDirection.y;
        if (planeDistance > 0.0f && planeDistance <= config.maxDistance)
        {
            fallbackDistance = planeDistance;
        }
    }

    PlacementProbeResult result{};
    result.rayHit = config.syntheticSurface;
    result.hasPlacementCandidate = true;
    result.snappedToGrid = true;
    result.hitNormal =
        config.syntheticSurface ? glm::ivec3(0, 1, 0) : glm::ivec3(0);
    const glm::vec3 fallbackPoint =
        config.rayOrigin + config.rayDirection * fallbackDistance;
    result.placementVoxel = voxelFromPointFloor(fallbackPoint);
    result.hitVoxel = config.syntheticSurface
                          ? result.placementVoxel - result.hitNormal
                          : glm::ivec3(0);
    result.ghostWorldPosition =
        glm::vec3(result.placementVoxel) + glm::vec3(0.5f);
    result.distance = fallbackDistance;
    return result;
}

}  // namespace engine::voxel
