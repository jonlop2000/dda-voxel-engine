#include "engine/voxel/WorldGen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include "Core/Logger.h"
#include "engine/voxel/Noise.h"
#include "engine/voxel/VoxelMath.h"
#include "engine/voxel/VoxelTypes.h"

namespace
{
constexpr int kSurfaceDirtDepth = 3;
constexpr int kSandDepth = 2;
constexpr int kTreeCellSize = 8;

int heightAt(int wx, int wz, uint32_t seed)
{
    const float base = valueNoise2D(wx, wz, 32, seed) * 12.0f;
    const float detail = valueNoise2D(wx, wz, 8, seed + 1u) * 4.0f;
    const float height = 8.0f + base + detail;
    return static_cast<int>(std::floor(height));
}

bool isSandy(int wx, int wz, uint32_t seed)
{
    const float m = valueNoise2D(wx, wz, 16, seed + 2u);
    return m < 0.2f;
}
} // namespace

WorldGenStats generateWorld(ChunkGrid& grid, uint32_t seed)
{
    WorldGenStats stats{};
    const auto start = std::chrono::steady_clock::now();
    logInfo("WorldGen", makeLogMessage("WorldGen start seed=", seed, " dims=(",
                                       grid.dims().x, ",", grid.dims().y, ",",
                                       grid.dims().z, ")"));

    logInfo("WorldGen", "WorldGen waiting for grid lock");
    auto lock = grid.lockUnique();
    logInfo("WorldGen", "WorldGen lock acquired");
    auto& chunks = grid.chunks();

    const int minX = grid.origin().x * Chunk::SX;
    const int maxX = minX + grid.dims().x * Chunk::SX - 1;
    const int minY = grid.origin().y * Chunk::SY;
    const int maxY = minY + grid.dims().y * Chunk::SY - 1;
    const int minZ = grid.origin().z * Chunk::SZ;
    const int maxZ = minZ + grid.dims().z * Chunk::SZ - 1;

    auto getBlock = [&grid](int wx, int wy, int wz) -> BlockId {
        const int cx = floorDiv(wx, Chunk::SX);
        const int cy = floorDiv(wy, Chunk::SY);
        const int cz = floorDiv(wz, Chunk::SZ);
        const Chunk* chunk = grid.getChunk(glm::ivec3(cx, cy, cz));
        if (chunk == nullptr)
        {
            return BLOCK_AIR;
        }
        const int lx = floorMod(wx, Chunk::SX);
        const int ly = floorMod(wy, Chunk::SY);
        const int lz = floorMod(wz, Chunk::SZ);
        return chunk->blocks[static_cast<size_t>(Chunk::index(lx, ly, lz))];
    };

    auto setBlock = [&grid](int wx, int wy, int wz, BlockId id) {
        const int cx = floorDiv(wx, Chunk::SX);
        const int cy = floorDiv(wy, Chunk::SY);
        const int cz = floorDiv(wz, Chunk::SZ);
        Chunk* chunk = grid.getChunk(glm::ivec3(cx, cy, cz));
        if (chunk == nullptr)
        {
            return;
        }
        const int lx = floorMod(wx, Chunk::SX);
        const int ly = floorMod(wy, Chunk::SY);
        const int lz = floorMod(wz, Chunk::SZ);
        chunk->blocks[static_cast<size_t>(Chunk::index(lx, ly, lz))] = id;
    };

    const size_t totalChunks = chunks.size();
    int minHeight = std::numeric_limits<int>::max();
    int maxHeight = std::numeric_limits<int>::min();
    int64_t heightSum = 0;
    int64_t heightSamples = 0;
    for (size_t chunkIndex = 0; chunkIndex < totalChunks; ++chunkIndex)
    {
        auto& chunk = chunks[chunkIndex];
        if (chunkIndex == 0)
        {
            logInfo("WorldGen", "WorldGen terrain begin");
        }
        std::fill(chunk.blocks.begin(), chunk.blocks.end(), BLOCK_AIR);

        const int baseX = chunk.coord.x * Chunk::SX;
        const int baseY = chunk.coord.y * Chunk::SY;
        const int baseZ = chunk.coord.z * Chunk::SZ;

        for (int z = 0; z < Chunk::SZ; ++z)
        {
            const int wz = baseZ + z;
            for (int x = 0; x < Chunk::SX; ++x)
            {
                const int wx = baseX + x;
                const int height = heightAt(wx, wz, seed);
                const bool sand = isSandy(wx, wz, seed);
                const int h = std::clamp(height, minY, maxY);

                minHeight = std::min(minHeight, h);
                maxHeight = std::max(maxHeight, h);
                heightSum += h;
                heightSamples += 1;

                for (int y = 0; y < Chunk::SY; ++y)
                {
                    const int wy = baseY + y;
                    if (wy > h)
                    {
                        continue;
                    }

                    BlockId id = BLOCK_STONE;
                    if (wy == h)
                    {
                        id = sand ? BLOCK_SAND : BLOCK_GRASS;
                    }
                    else if (sand && wy >= h - kSandDepth)
                    {
                        id = BLOCK_SAND;
                    }
                    else if (wy >= h - kSurfaceDirtDepth)
                    {
                        id = BLOCK_DIRT;
                    }

                    chunk.blocks[static_cast<size_t>(Chunk::index(x, y, z))] = id;
                }

                if (!sand)
                {
                    const int localY = h - baseY;
                    if (localY >= 0 && localY + 1 < Chunk::SY)
                    {
                        const float foliageChance = rand01(wx, wz, seed + 400u);
                        const size_t idx =
                            static_cast<size_t>(Chunk::index(x, localY + 1, z));
                        if (foliageChance < 0.08f && chunk.blocks[idx] == BLOCK_AIR)
                        {
                            chunk.blocks[idx] = BLOCK_GRASS_TALL;
                            stats.foliagePlaced++;
                        }
                    }
                }
            }
        }

        if ((chunkIndex + 1) % 16 == 0 || (chunkIndex + 1) == totalChunks)
        {
            const auto now = std::chrono::steady_clock::now();
            const double ms =
                std::chrono::duration<double, std::milli>(now - start).count();
            logInfo("WorldGen", makeLogMessage("WorldGen terrain ", (chunkIndex + 1), "/",
                                               totalChunks, " (", ms, " ms)"));
        }
    }

    logInfo("WorldGen", "WorldGen trees begin");
    const int cellMinX = floorDiv(minX, kTreeCellSize);
    const int cellMaxX = floorDiv(maxX, kTreeCellSize);
    const int cellMinZ = floorDiv(minZ, kTreeCellSize);
    const int cellMaxZ = floorDiv(maxZ, kTreeCellSize);

    for (int cz = cellMinZ; cz <= cellMaxZ; ++cz)
    {
        for (int cx = cellMinX; cx <= cellMaxX; ++cx)
        {
            const float chance = rand01(cx, cz, seed + 500u);
            if (chance > 0.08f)
            {
                continue;
            }

            int wx = cx * kTreeCellSize + kTreeCellSize / 2;
            int wz = cz * kTreeCellSize + kTreeCellSize / 2;

            const float jitterX = rand01(cx, cz, seed + 501u) - 0.5f;
            const float jitterZ = rand01(cx, cz, seed + 502u) - 0.5f;
            wx += static_cast<int>(std::round(jitterX * (kTreeCellSize * 0.6f)));
            wz += static_cast<int>(std::round(jitterZ * (kTreeCellSize * 0.6f)));

            if (wx < minX || wx > maxX || wz < minZ || wz > maxZ)
            {
                continue;
            }

            const int ground = heightAt(wx, wz, seed);
            if (ground < minY || ground >= maxY)
            {
                continue;
            }

            if (getBlock(wx, ground, wz) == BLOCK_SAND)
            {
                continue;
            }

            const int trunkH =
                4 + static_cast<int>(rand01(wx, wz, seed + 600u) * 3.0f);

            for (int i = 1; i <= trunkH; ++i)
            {
                const int wy = ground + i;
                if (wy > maxY)
                {
                    break;
                }
                setBlock(wx, wy, wz, BLOCK_LOG);
            }

            const glm::ivec3 top(wx, ground + trunkH, wz);
            const int r = 2;
            for (int dz = -r; dz <= r; ++dz)
            {
                for (int dy = -r; dy <= r; ++dy)
                {
                    for (int dx = -r; dx <= r; ++dx)
                    {
                        const int dist2 = dx * dx + dy * dy + dz * dz;
                        if (dist2 > r * r + 1)
                        {
                            continue;
                        }

                        const int lx = top.x + dx;
                        const int ly = top.y + dy;
                        const int lz = top.z + dz;

                        if (ly < minY || ly > maxY)
                        {
                            continue;
                        }

                        if (getBlock(lx, ly, lz) == BLOCK_AIR)
                        {
                            setBlock(lx, ly, lz, BLOCK_LEAF);
                        }
                    }
                }
            }

            stats.treesPlaced++;
        }
    }

    for (auto& chunk : chunks)
    {
        chunk.dirty.store(true);
        chunk.meshing.store(false);
        chunk.highPriority.store(false);
        chunk.nextBuildId = chunk.lastAppliedBuildId + 1;
        chunk.pending.reset();
    }

    if (heightSamples > 0)
    {
        stats.minHeight = minHeight;
        stats.maxHeight = maxHeight;
        stats.avgHeight = static_cast<float>(heightSum) / static_cast<float>(heightSamples);
    }
    else
    {
        stats.minHeight = minY;
        stats.maxHeight = maxY;
        stats.avgHeight = static_cast<float>(minY);
    }

    const auto end = std::chrono::steady_clock::now();
    const double totalMs =
        std::chrono::duration<double, std::milli>(end - start).count();
    logInfo("WorldGen",
            makeLogMessage("WorldGen done trees=", stats.treesPlaced, " foliage=",
                           stats.foliagePlaced, " heights=(", stats.minHeight, ",",
                           stats.maxHeight, ",", stats.avgHeight, ") (", totalMs, " ms)"));

    return stats;
}
