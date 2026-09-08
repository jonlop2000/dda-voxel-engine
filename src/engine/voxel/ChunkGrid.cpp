#include "engine/voxel/ChunkGrid.h"

#include "engine/voxel/VoxelMath.h"

void ChunkGrid::create(const glm::ivec3& dims, const glm::ivec3& origin)
{
    dims_ = glm::max(dims, glm::ivec3(1));
    origin_ = origin;

    const int total = dims_.x * dims_.y * dims_.z;
    chunks_.clear();
    chunks_.reserve(static_cast<size_t>(total));

    for (int y = 0; y < dims_.y; ++y)
    {
        for (int z = 0; z < dims_.z; ++z)
        {
            for (int x = 0; x < dims_.x; ++x)
            {
                ChunkCoord coord(origin_.x + x, origin_.y + y, origin_.z + z);
                chunks_.emplace_back(coord);
            }
        }
    }
}

void ChunkGrid::createPreservingBlocks(const glm::ivec3& dims, const glm::ivec3& origin)
{
    struct PreservedBlock
    {
        glm::ivec3 world;
        BlockId id = BLOCK_AIR;
    };

    std::vector<PreservedBlock> preserved;
    for (const Chunk& chunk : chunks_)
    {
        const int baseX = chunk.coord.x * Chunk::SX;
        const int baseY = chunk.coord.y * Chunk::SY;
        const int baseZ = chunk.coord.z * Chunk::SZ;
        for (int y = 0; y < Chunk::SY; ++y)
        {
            for (int z = 0; z < Chunk::SZ; ++z)
            {
                for (int x = 0; x < Chunk::SX; ++x)
                {
                    const BlockId id = chunk.get(x, y, z);
                    if (id != BLOCK_AIR)
                    {
                        preserved.push_back(
                            {glm::ivec3(baseX + x, baseY + y, baseZ + z), id});
                    }
                }
            }
        }
    }

    create(dims, origin);

    for (const PreservedBlock& block : preserved)
    {
        setVoxelWorld(block.world.x, block.world.y, block.world.z, block.id);
    }
}

const glm::ivec3& ChunkGrid::dims() const
{
    return dims_;
}

const glm::ivec3& ChunkGrid::origin() const
{
    return origin_;
}

bool ChunkGrid::inBounds(const glm::ivec3& coord) const
{
    const glm::ivec3 local = coord - origin_;
    return local.x >= 0 && local.x < dims_.x && local.y >= 0 && local.y < dims_.y &&
           local.z >= 0 && local.z < dims_.z;
}

Chunk* ChunkGrid::getChunk(const glm::ivec3& coord)
{
    if (!inBounds(coord))
    {
        return nullptr;
    }
    return &chunks_.at(static_cast<size_t>(indexFor(coord)));
}

const Chunk* ChunkGrid::getChunk(const glm::ivec3& coord) const
{
    if (!inBounds(coord))
    {
        return nullptr;
    }
    return &chunks_.at(static_cast<size_t>(indexFor(coord)));
}

BlockId ChunkGrid::getVoxelWorld(int wx, int wy, int wz) const
{
    const int cx = floorDiv(wx, Chunk::SX);
    const int cy = floorDiv(wy, Chunk::SY);
    const int cz = floorDiv(wz, Chunk::SZ);

    const Chunk* chunk = getChunk(glm::ivec3(cx, cy, cz));
    if (chunk == nullptr)
    {
        return BLOCK_AIR;
    }

    const int lx = floorMod(wx, Chunk::SX);
    const int ly = floorMod(wy, Chunk::SY);
    const int lz = floorMod(wz, Chunk::SZ);
    return chunk->get(lx, ly, lz);
}

bool ChunkGrid::setVoxelWorld(int wx, int wy, int wz, BlockId id)
{
    const int cx = floorDiv(wx, Chunk::SX);
    const int cy = floorDiv(wy, Chunk::SY);
    const int cz = floorDiv(wz, Chunk::SZ);

    Chunk* chunk = getChunk(glm::ivec3(cx, cy, cz));
    if (chunk == nullptr)
    {
        return false;
    }

    const int lx = floorMod(wx, Chunk::SX);
    const int ly = floorMod(wy, Chunk::SY);
    const int lz = floorMod(wz, Chunk::SZ);

    if (chunk->get(lx, ly, lz) == id)
    {
        return false;
    }

    chunk->set(lx, ly, lz, id);
    return true;
}

void ChunkGrid::clear()
{
    std::unique_lock<std::shared_mutex> lock(blocksMutex_);
    dims_ = glm::ivec3(0);
    origin_ = glm::ivec3(0);
    chunks_.clear();
}

std::shared_lock<std::shared_mutex> ChunkGrid::lockShared() const
{
    return std::shared_lock<std::shared_mutex>(blocksMutex_);
}

std::unique_lock<std::shared_mutex> ChunkGrid::lockUnique()
{
    return std::unique_lock<std::shared_mutex>(blocksMutex_);
}

std::vector<Chunk>& ChunkGrid::chunks()
{
    return chunks_;
}

const std::vector<Chunk>& ChunkGrid::chunks() const
{
    return chunks_;
}

int ChunkGrid::indexFor(const glm::ivec3& coord) const
{
    const glm::ivec3 local = coord - origin_;
    return local.x + dims_.x * (local.z + dims_.z * local.y);
}
