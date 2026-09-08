#pragma once

#include <array>
#include <atomic>
#include <optional>
#include <mutex>
#include <vector>

#include <glm/glm.hpp>

#include "Resources/Mesh.h"
#include "engine/voxel/ChunkCoord.h"
#include "engine/voxel/VoxelTypes.h"

struct ChunkMeshCpu
{
    std::array<std::vector<Vertex>, kBlockTypeCount> vertices{};
    std::array<std::vector<uint32_t>, kBlockTypeCount> indices{};
    uint64_t buildId = 0;
};

struct Chunk
{
    static constexpr int SX = 32;
    static constexpr int SY = 32;
    static constexpr int SZ = 32;

    ChunkCoord coord{};
    std::vector<BlockId> blocks{};
    std::atomic<bool> dirty{true};
    std::atomic<bool> meshing{false};
    std::atomic<bool> highPriority{false};
    uint64_t nextBuildId = 1;
    uint64_t lastAppliedBuildId = 0;
    std::mutex pendingMutex{};
    std::optional<ChunkMeshCpu> pending{};

    Chunk();
    explicit Chunk(const ChunkCoord& coordIn);
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    Chunk(Chunk&& other) noexcept;
    Chunk& operator=(Chunk&& other) noexcept;

    static constexpr int index(int x, int y, int z)
    {
        return x + SX * (z + SZ * y);
    }

    static constexpr int volume()
    {
        return SX * SY * SZ;
    }

    bool inBounds(int x, int y, int z) const;
    BlockId get(int x, int y, int z) const;
    void set(int x, int y, int z, BlockId id);
};
