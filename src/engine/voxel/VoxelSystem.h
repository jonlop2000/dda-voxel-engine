#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "engine/voxel/Chunk.h"
#include "engine/voxel/VoxelTypes.h"

class ChunkGrid;
class JobSystem;
struct RayHit;

namespace engine::voxel
{

struct WorldGridCreateResult
{
    std::vector<glm::ivec3> chunkCoords{};
};

struct WorldGenerationResult
{
    uint32_t treesPlaced = 0;
    uint32_t foliagePlaced = 0;
    float waterLevel = 0.0f;
};

struct PendingChunkMesh
{
    size_t chunkIndex = 0;
    ChunkMeshCpu mesh{};
};

struct ChunkGridSummary
{
    size_t chunkCount = 0;
    size_t dirtyChunks = 0;
};

struct ChunkBounds
{
    glm::ivec3 coord{0};
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

struct PlacementRaycastProbeConfig
{
    glm::vec3 rayOrigin{0.0f};
    glm::vec3 rayDirection{0.0f};
    float maxDistance = 0.0f;
    bool removalMode = false;
};

struct PlacementFallbackProbeConfig
{
    glm::vec3 rayOrigin{0.0f};
    glm::vec3 rayDirection{0.0f};
    float maxDistance = 0.0f;
    float defaultDistance = 0.0f;
    float planeY = 0.0f;
    bool syntheticSurface = false;
};

struct PlacementProbeResult
{
    bool rayHit = false;
    bool hasPlacementCandidate = false;
    bool snappedToGrid = false;
    glm::ivec3 hitVoxel{0};
    glm::ivec3 hitNormal{0};
    glm::ivec3 placementVoxel{0};
    glm::vec3 ghostWorldPosition{0.0f};
    float distance = 0.0f;
};

struct GridExpansionResult
{
    bool expanded = false;
    std::vector<glm::ivec3> chunkCoords{};
};

WorldGridCreateResult createWorldGrid(ChunkGrid& grid, const glm::ivec3& dims);
void clearWorldGrid(ChunkGrid& grid);
WorldGenerationResult generateWorldState(ChunkGrid& grid, uint32_t seed);
GridExpansionResult expandGridToIncludeChunk(ChunkGrid& grid,
                                             const glm::ivec3& targetChunk,
                                             int paddingChunks);
ChunkGridSummary summarizeChunkGrid(const ChunkGrid& grid);
std::vector<glm::ivec3> chunkCoords(const ChunkGrid& grid);
std::vector<ChunkBounds> chunkBounds(const ChunkGrid& grid);
bool containsChunk(const ChunkGrid& grid, const glm::ivec3& chunkCoord);
RayHit raycastVoxels(const ChunkGrid& grid, const glm::vec3& rayOrigin,
                     const glm::vec3& rayDirection, float maxDistance);
void markChunkDirty(ChunkGrid& grid, const glm::ivec3& coord, bool highPriority);
void markVoxelEditDirtyNeighbors(ChunkGrid& grid, const glm::ivec3& target,
                                 bool highPriority);
bool applyVoxelEdit(ChunkGrid& grid, const RayHit& hit, bool place,
                    BlockId selectedBlock);
bool commitBlockPlacement(ChunkGrid& grid, std::span<const glm::ivec3> cells,
                          BlockId block);
bool commitBlockRemoval(ChunkGrid& grid, const glm::ivec3& target,
                        BlockId expectedBlock);
size_t scheduleDirtyChunkMeshing(ChunkGrid& grid, JobSystem& jobSystem,
                                 const glm::vec3& cameraPos, size_t maxJobs);
std::vector<PendingChunkMesh> consumePendingChunkMeshes(ChunkGrid& grid);
PlacementProbeResult raycastPlacementProbe(
    const ChunkGrid& grid,
    const PlacementRaycastProbeConfig& config);
PlacementProbeResult fallbackPlacementProbe(
    const PlacementFallbackProbeConfig& config);

}  // namespace engine::voxel
