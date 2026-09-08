#pragma once

#include <array>
#include <shared_mutex>
#include <vector>

#include <glm/glm.hpp>

#include "engine/voxel/Chunk.h"
#include "engine/voxel/ChunkGrid.h"
#include "engine/voxel/VoxelTypes.h"

struct VoxelAccessor
{
    const ChunkGrid* grid = nullptr;
    std::shared_lock<std::shared_mutex> lock{};

    explicit VoxelAccessor(const ChunkGrid& gridIn);
    BlockId getVoxelWorld(int wx, int wy, int wz) const;
};

void buildGreedyMesh(const VoxelAccessor& accessor, const Chunk& chunk,
                     std::array<std::vector<Vertex>, kBlockTypeCount>& vertices,
                     std::array<std::vector<uint32_t>, kBlockTypeCount>& indices);
