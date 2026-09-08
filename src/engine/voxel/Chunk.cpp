#include "engine/voxel/Chunk.h"

#include <utility>

Chunk::Chunk() : blocks(static_cast<size_t>(volume()), BLOCK_AIR) {}

Chunk::Chunk(const ChunkCoord& coordIn)
    : coord(coordIn), blocks(static_cast<size_t>(volume()), BLOCK_AIR)
{
}

Chunk::Chunk(Chunk&& other) noexcept
    : coord(other.coord),
      blocks(std::move(other.blocks)),
      dirty(other.dirty.load(std::memory_order_relaxed)),
      meshing(other.meshing.load(std::memory_order_relaxed)),
      highPriority(other.highPriority.load(std::memory_order_relaxed)),
      nextBuildId(other.nextBuildId),
      lastAppliedBuildId(other.lastAppliedBuildId),
      pending(std::move(other.pending))
{
    other.dirty.store(false, std::memory_order_relaxed);
    other.meshing.store(false, std::memory_order_relaxed);
    other.highPriority.store(false, std::memory_order_relaxed);
    other.nextBuildId = 1;
    other.lastAppliedBuildId = 0;
    other.pending.reset();
}

Chunk& Chunk::operator=(Chunk&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    coord = other.coord;
    blocks = std::move(other.blocks);
    dirty.store(other.dirty.load(std::memory_order_relaxed), std::memory_order_relaxed);
    meshing.store(other.meshing.load(std::memory_order_relaxed), std::memory_order_relaxed);
    highPriority.store(other.highPriority.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
    nextBuildId = other.nextBuildId;
    lastAppliedBuildId = other.lastAppliedBuildId;
    pending = std::move(other.pending);

    other.dirty.store(false, std::memory_order_relaxed);
    other.meshing.store(false, std::memory_order_relaxed);
    other.highPriority.store(false, std::memory_order_relaxed);
    other.nextBuildId = 1;
    other.lastAppliedBuildId = 0;
    other.pending.reset();

    return *this;
}

bool Chunk::inBounds(int x, int y, int z) const
{
    return x >= 0 && x < SX && y >= 0 && y < SY && z >= 0 && z < SZ;
}

BlockId Chunk::get(int x, int y, int z) const
{
    if (!inBounds(x, y, z))
    {
        return BLOCK_AIR;
    }
    return blocks[static_cast<size_t>(index(x, y, z))];
}

void Chunk::set(int x, int y, int z, BlockId id)
{
    if (!inBounds(x, y, z))
    {
        return;
    }
    blocks[static_cast<size_t>(index(x, y, z))] = id;
    dirty.store(true);
}
