#pragma once

#include <mutex>
#include <shared_mutex>
#include <vector>

#include <glm/glm.hpp>

#include "engine/voxel/Chunk.h"

class ChunkGrid
{
public:
    void create(const glm::ivec3& dims, const glm::ivec3& origin);
    void createPreservingBlocks(const glm::ivec3& dims, const glm::ivec3& origin);

    const glm::ivec3& dims() const;
    const glm::ivec3& origin() const;

    bool inBounds(const glm::ivec3& coord) const;
    Chunk* getChunk(const glm::ivec3& coord);
    const Chunk* getChunk(const glm::ivec3& coord) const;

    BlockId getVoxelWorld(int wx, int wy, int wz) const;
    bool setVoxelWorld(int wx, int wy, int wz, BlockId id);
    void clear();

    std::shared_lock<std::shared_mutex> lockShared() const;
    std::unique_lock<std::shared_mutex> lockUnique();

    std::vector<Chunk>& chunks();
    const std::vector<Chunk>& chunks() const;

private:
    int indexFor(const glm::ivec3& coord) const;

    glm::ivec3 dims_{0};
    glm::ivec3 origin_{0};
    std::vector<Chunk> chunks_{};
    mutable std::shared_mutex blocksMutex_{};
};
