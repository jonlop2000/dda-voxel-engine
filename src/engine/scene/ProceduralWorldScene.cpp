#include "engine/scene/ProceduralWorldScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "Core/Logger.h"
#include "engine/render/VulkanContext.h"
#include "engine/scene/ProceduralWorldMaterials.h"
#include "engine/voxel/Noise.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelWorld.h"

namespace
{
using namespace engine::scene::ProceduralWorldMaterial;

constexpr uint8_t kMatGrass = Grass;
constexpr uint8_t kMatDirt = Dirt;
constexpr uint8_t kMatStone = Stone;
constexpr uint8_t kMatWood = Wood;
constexpr uint8_t kMatLeaves = Leaves;

constexpr uint8_t kBeachDrySandBase = BeachDrySandBase;
constexpr int kBeachDrySandCount = BeachDrySandCount;
constexpr uint8_t kBeachWetSandBase = BeachWetSandBase;
constexpr int kBeachWetSandCount = BeachWetSandCount;
constexpr uint8_t kBeachShellBase = BeachShellBase;
constexpr int kBeachShellCount = BeachShellCount;
constexpr uint8_t kBeachDuneGrassBase = BeachDuneGrassBase;
constexpr int kBeachDuneGrassCount = BeachDuneGrassCount;
constexpr uint8_t kBeachRockBase = BeachRockBase;
constexpr int kBeachRockCount = BeachRockCount;
constexpr uint8_t kBeachDriftwoodBase = BeachDriftwoodBase;
constexpr int kBeachDriftwoodCount = BeachDriftwoodCount;

engine::PaletteEntryCPU makeEntry(const glm::vec3& color, float roughness, float metallic,
                                  engine::VoxelMaterialCategory category, float alpha = 1.0f)
{
    engine::PaletteEntryCPU entry{};
    entry.baseColor_alpha = glm::vec4(color, alpha);
    entry.pbr0 = glm::vec4(metallic, roughness, 0.0f, 0.0f);
    entry.extra = glm::vec4(1.0f, 0.0f, 0.0f, static_cast<float>(category));
    return entry;
}

uint32_t hash4(int a, int b, int c, int d, uint32_t seed)
{
    const uint32_t ha = static_cast<uint32_t>(a) * 73856093u;
    const uint32_t hb = static_cast<uint32_t>(b) * 19349663u;
    const uint32_t hc = static_cast<uint32_t>(c) * 83492791u;
    const uint32_t hd = static_cast<uint32_t>(d) * 2654435761u;
    return hash32(ha ^ hb ^ hc ^ hd ^ seed);
}

int randIntRange(int minInclusive, int maxInclusive, uint32_t h)
{
    if (maxInclusive < minInclusive)
    {
        std::swap(minInclusive, maxInclusive);
    }
    const uint32_t span = static_cast<uint32_t>(maxInclusive - minInclusive + 1);
    const uint32_t v = (span == 0u) ? 0u : (h % span);
    return minInclusive + static_cast<int>(v);
}

float rand013(int x, int y, int z, uint32_t seed)
{
    const uint32_t hx = static_cast<uint32_t>(x) * 73856093u;
    const uint32_t hy = static_cast<uint32_t>(y) * 19349663u;
    const uint32_t hz = static_cast<uint32_t>(z) * 83492791u;
    const uint32_t h = hash32(hx ^ hy ^ hz ^ seed);
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float valueNoise3D(int x, int y, int z, int scale, uint32_t seed)
{
    if (scale <= 0)
    {
        return 0.0f;
    }

    const float fx = static_cast<float>(x) / static_cast<float>(scale);
    const float fy = static_cast<float>(y) / static_cast<float>(scale);
    const float fz = static_cast<float>(z) / static_cast<float>(scale);

    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int z0 = static_cast<int>(std::floor(fz));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const int z1 = z0 + 1;

    const float tx = smoothstep(fx - static_cast<float>(x0));
    const float ty = smoothstep(fy - static_cast<float>(y0));
    const float tz = smoothstep(fz - static_cast<float>(z0));

    const float v000 = rand013(x0, y0, z0, seed);
    const float v100 = rand013(x1, y0, z0, seed);
    const float v010 = rand013(x0, y1, z0, seed);
    const float v110 = rand013(x1, y1, z0, seed);
    const float v001 = rand013(x0, y0, z1, seed);
    const float v101 = rand013(x1, y0, z1, seed);
    const float v011 = rand013(x0, y1, z1, seed);
    const float v111 = rand013(x1, y1, z1, seed);

    const float vx00 = lerp(v000, v100, tx);
    const float vx10 = lerp(v010, v110, tx);
    const float vx01 = lerp(v001, v101, tx);
    const float vx11 = lerp(v011, v111, tx);

    const float vy0 = lerp(vx00, vx10, ty);
    const float vy1 = lerp(vx01, vx11, ty);
    return lerp(vy0, vy1, tz);
}

size_t chunkIndexFromCoord(const glm::ivec3& coord, const glm::ivec3& gridDims)
{
    return static_cast<size_t>(coord.x) +
           static_cast<size_t>(gridDims.x) *
               (static_cast<size_t>(coord.y) +
                static_cast<size_t>(gridDims.y) * static_cast<size_t>(coord.z));
}

size_t voxelIndex(int x, int y, int z, const glm::ivec3& dims)
{
    return static_cast<size_t>(x) + static_cast<size_t>(y) * static_cast<size_t>(dims.x) +
           static_cast<size_t>(z) * static_cast<size_t>(dims.x) * static_cast<size_t>(dims.y);
}

int heightAt(int worldX, int worldZ, const ProceduralWorldSettings& settings, int noiseScale,
             int totalHeight)
{
    const int detailScale = std::max(1, noiseScale / 2);
    const float n0 = valueNoise2D(worldX, worldZ, noiseScale, settings.seed);
    const float n1 = valueNoise2D(worldX, worldZ, detailScale, settings.seed + 17u);
    const float hNoise = n0 * 0.75f + n1 * 0.25f;

    const float height = settings.baseHeight + hNoise * settings.heightAmplitude;
    return std::clamp(static_cast<int>(std::floor(height)), 0, std::max(0, totalHeight - 1));
}

float beachShoreDistance(int worldX, int worldZ, const ProceduralWorldSettings& settings,
                         const glm::ivec3& totalDims)
{
    const float baseShoreline = static_cast<float>(totalDims.z) * 0.34f;
    const float broadWiggle =
        (valueNoise2D(worldX, 0, 64, settings.seed + 901u) - 0.5f) * 14.0f;
    const float fineWiggle =
        (valueNoise2D(worldX, worldZ, 19, settings.seed + 902u) - 0.5f) * 3.0f;
    return static_cast<float>(worldZ) - baseShoreline - broadWiggle - fineWiggle;
}

int beachHeightAt(int worldX, int worldZ, const ProceduralWorldSettings& settings,
                  const glm::ivec3& totalDims)
{
    const int broadScale = std::max(1, static_cast<int>(settings.noiseScale));
    const int detailScale = std::max(1, broadScale / 4);
    const float duneScale = std::clamp(settings.heightAmplitude / 8.0f, 0.35f, 3.0f);
    const float waterY = settings.baseHeight;
    const float shore = beachShoreDistance(worldX, worldZ, settings, totalDims);
    const float broad = valueNoise2D(worldX, worldZ, broadScale, settings.seed + 911u) - 0.5f;
    const float detail = valueNoise2D(worldX, worldZ, detailScale, settings.seed + 912u) - 0.5f;
    const float ripple =
        std::sin(static_cast<float>(worldX) * 0.42f + static_cast<float>(worldZ) * 0.11f +
                 detail * 5.0f);

    float height = waterY;
    if (shore < -26.0f)
    {
        height = waterY - 6.5f + broad * 1.6f + detail * 0.8f;
    }
    else if (shore < 0.0f)
    {
        const float t = (shore + 26.0f) / 26.0f;
        height = lerp(waterY - 6.5f, waterY - 1.0f, t) + ripple * 0.12f;
    }
    else if (shore < 42.0f)
    {
        const float t = shore / 42.0f;
        height = waterY - 0.8f + t * (4.8f * duneScale) + broad * 1.2f +
                 detail * 0.55f + ripple * 0.35f * (1.0f - t);
    }
    else
    {
        const float landSpan = std::max(1.0f, static_cast<float>(totalDims.z) - 42.0f);
        const float duneT = std::clamp((shore - 42.0f) / landSpan, 0.0f, 1.0f);
        const float ridge = std::sin(static_cast<float>(worldX) * 0.09f + shore * 0.18f +
                                     broad * 4.0f);
        height = waterY + 3.8f * duneScale + duneT * (7.0f * duneScale) +
                 broad * 3.0f * duneScale + detail * 1.2f +
                 ridge * (1.0f + duneT * 1.8f);
    }

    return std::clamp(static_cast<int>(std::floor(height)), 0, std::max(0, totalDims.y - 1));
}

uint8_t materialFromBand(uint8_t base, int count, float tone, uint32_t hash)
{
    const float clampedTone = std::clamp(tone, 0.0f, 1.0f);
    int index = static_cast<int>(std::floor(clampedTone * static_cast<float>(count)));
    index = std::clamp(index, 0, std::max(0, count - 1));
    const int jitter = static_cast<int>((hash >> 8u) % 3u) - 1;
    index = std::clamp(index + jitter, 0, std::max(0, count - 1));
    return static_cast<uint8_t>(base + index);
}

uint8_t beachSurfaceMaterialAt(int worldX, int surfaceY, int worldZ,
                               const ProceduralWorldSettings& settings,
                               const glm::ivec3& totalDims)
{
    const float shore = beachShoreDistance(worldX, worldZ, settings, totalDims);
    const bool wetSand =
        shore < 9.0f || static_cast<float>(surfaceY) <= settings.baseHeight + 0.5f;

    const float coarse = valueNoise2D(worldX, worldZ, 17, settings.seed + 930u);
    const float fine = valueNoise2D(worldX, worldZ, 5, settings.seed + 931u);
    const float ripple =
        0.5f + 0.5f * std::sin(static_cast<float>(worldX) * 0.63f +
                               static_cast<float>(worldZ) * 0.18f + fine * 4.5f);
    const float tone = std::clamp(coarse * 0.36f + fine * 0.34f + ripple * 0.30f,
                                  0.0f, 1.0f);

    const float accent = rand01(worldX, worldZ, settings.seed + 932u);
    if (shore > 3.0f && shore < 48.0f && accent < 0.020f)
    {
        return materialFromBand(kBeachShellBase, kBeachShellCount, tone,
                                hash4(worldX, surfaceY, worldZ, 1, settings.seed));
    }
    if (shore > -8.0f && shore < 30.0f && accent > 0.982f)
    {
        return materialFromBand(kBeachRockBase, kBeachRockCount, tone,
                                hash4(worldX, surfaceY, worldZ, 2, settings.seed));
    }

    return wetSand ? materialFromBand(kBeachWetSandBase, kBeachWetSandCount, tone,
                                      hash4(worldX, surfaceY, worldZ, 3, settings.seed))
                   : materialFromBand(kBeachDrySandBase, kBeachDrySandCount, tone,
                                      hash4(worldX, surfaceY, worldZ, 4, settings.seed));
}

uint8_t beachSubsurfaceMaterialAt(int worldX, int worldY, int worldZ, int surfaceY,
                                  int sandDepth,
                                  const ProceduralWorldSettings& settings,
                                  const glm::ivec3& totalDims)
{
    const int depth = std::max(0, surfaceY - worldY);
    const float depthT = std::clamp(static_cast<float>(depth) /
                                        static_cast<float>(std::max(1, sandDepth)),
                                    0.0f, 1.0f);
    const float shore = beachShoreDistance(worldX, worldZ, settings, totalDims);
    const bool wetSand =
        shore < 7.0f || static_cast<float>(worldY) <= settings.baseHeight - 0.5f;
    const float tone = wetSand ? 0.22f + depthT * 0.48f : 0.18f + depthT * 0.36f;
    return wetSand ? materialFromBand(kBeachWetSandBase, kBeachWetSandCount, tone,
                                      hash4(worldX, worldY, worldZ, 5, settings.seed))
                   : materialFromBand(kBeachDrySandBase, kBeachDrySandCount, tone,
                                      hash4(worldX, worldY, worldZ, 6, settings.seed));
}

uint8_t beachRockMaterialAt(int worldX, int worldY, int worldZ,
                            const ProceduralWorldSettings& settings)
{
    const float tone = valueNoise2D(worldX, worldZ, 9, settings.seed + 940u) * 0.75f +
                       rand013(worldX, worldY, worldZ, settings.seed + 941u) * 0.25f;
    return materialFromBand(kBeachRockBase, kBeachRockCount, tone,
                            hash4(worldX, worldY, worldZ, 7, settings.seed));
}

uint8_t beachDuneGrassMaterialAt(int worldX, int worldY, int worldZ,
                                 const ProceduralWorldSettings& settings)
{
    const float tone = rand013(worldX, worldY, worldZ, settings.seed + 950u);
    return materialFromBand(kBeachDuneGrassBase, kBeachDuneGrassCount, tone,
                            hash4(worldX, worldY, worldZ, 8, settings.seed));
}

uint8_t beachDriftwoodMaterialAt(int worldX, int worldY, int worldZ,
                                 const ProceduralWorldSettings& settings)
{
    const float grain =
        0.5f + 0.5f * std::sin(static_cast<float>(worldX + worldZ) * 0.55f +
                               static_cast<float>(worldY) * 0.23f);
    return materialFromBand(kBeachDriftwoodBase, kBeachDriftwoodCount, grain,
                            hash4(worldX, worldY, worldZ, 9, settings.seed));
}

bool buildBeachWorldData(const ProceduralWorldSettings& settings, const glm::ivec3& totalDims,
                         std::vector<std::vector<uint8_t>>& outData)
{
    const glm::ivec3 gridDims = settings.gridDims;
    const glm::ivec3 chunkDims = settings.chunkDims;
    const glm::ivec3 paddedDims = chunkDims + glm::ivec3(2);
    if (gridDims.x <= 0 || gridDims.y <= 0 || gridDims.z <= 0)
    {
        return false;
    }
    if (chunkDims.x <= 0 || chunkDims.y <= 0 || chunkDims.z <= 0)
    {
        return false;
    }

    const size_t volumeCount = static_cast<size_t>(gridDims.x) * static_cast<size_t>(gridDims.y) *
                               static_cast<size_t>(gridDims.z);
    const size_t voxelCountPerChunk =
        static_cast<size_t>(paddedDims.x) * static_cast<size_t>(paddedDims.y) *
        static_cast<size_t>(paddedDims.z);

    outData.clear();
    outData.resize(volumeCount);
    for (auto& chunk : outData)
    {
        chunk.assign(voxelCountPerChunk, 0);
    }

    const size_t totalVoxelCount =
        static_cast<size_t>(totalDims.x) * static_cast<size_t>(totalDims.y) *
        static_cast<size_t>(totalDims.z);
    std::vector<uint8_t> world(totalVoxelCount, 0);

    auto inWorldBounds = [&](int wx, int wy, int wz) -> bool
    {
        return wx >= 0 && wy >= 0 && wz >= 0 && wx < totalDims.x && wy < totalDims.y &&
               wz < totalDims.z;
    };

    auto worldIndex = [&](int wx, int wy, int wz) -> size_t
    {
        return static_cast<size_t>(wx) + static_cast<size_t>(wy) * static_cast<size_t>(totalDims.x) +
               static_cast<size_t>(wz) * static_cast<size_t>(totalDims.x) *
                   static_cast<size_t>(totalDims.y);
    };

    auto setVoxelWorld = [&](int wx, int wy, int wz, uint8_t id)
    {
        if (!inWorldBounds(wx, wy, wz))
        {
            return;
        }
        world[worldIndex(wx, wy, wz)] = id;
    };

    auto getVoxelWorld = [&](int wx, int wy, int wz) -> uint8_t
    {
        if (!inWorldBounds(wx, wy, wz))
        {
            return 0;
        }
        return world[worldIndex(wx, wy, wz)];
    };

    const int sandDepth = std::max(5, settings.dirtDepth);

    // dunes, wet shoreline, and shallow ocean floor.
    for (int worldZ = 0; worldZ < totalDims.z; ++worldZ)
    {
        for (int worldX = 0; worldX < totalDims.x; ++worldX)
        {
            const int h = beachHeightAt(worldX, worldZ, settings, totalDims);

            for (int worldY = 0; worldY < totalDims.y; ++worldY)
            {
                if (worldY > h)
                {
                    continue;
                }

                uint8_t id = beachRockMaterialAt(worldX, worldY, worldZ, settings);
                if (worldY == h)
                {
                    id = beachSurfaceMaterialAt(worldX, worldY, worldZ, settings, totalDims);
                }
                else if (worldY >= h - sandDepth)
                {
                    id = beachSubsurfaceMaterialAt(worldX, worldY, worldZ, h, sandDepth, settings,
                                                   totalDims);
                }

                setVoxelWorld(worldX, worldY, worldZ, id);
            }
        }
    }

    if (settings.enableTrees)
    {
        const int clumpsPerChunk = std::max(1, settings.treesPerChunk * 4);
        for (int cz = 0; cz < gridDims.z; ++cz)
        {
            for (int cx = 0; cx < gridDims.x; ++cx)
            {
                for (int ci = 0; ci < clumpsPerChunk; ++ci)
                {
                    const uint32_t hx = hash4(cx, cz, ci, 0, settings.seed + 960u);
                    const uint32_t hz = hash4(cx, cz, ci, 1, settings.seed + 961u);
                    const int worldX = cx * chunkDims.x + static_cast<int>(hx % chunkDims.x);
                    const int worldZ = cz * chunkDims.z + static_cast<int>(hz % chunkDims.z);
                    const float shore = beachShoreDistance(worldX, worldZ, settings, totalDims);
                    if (shore < 42.0f)
                    {
                        continue;
                    }

                    const int bladeCount = 2 + static_cast<int>(hash4(cx, cz, ci, 2,
                                                                     settings.seed + 962u) %
                                                                 5u);
                    for (int bi = 0; bi < bladeCount; ++bi)
                    {
                        const int ox = randIntRange(-1, 1, hash4(cx, cz, ci, 3 + bi,
                                                                  settings.seed + 963u));
                        const int oz = randIntRange(-1, 1, hash4(cx, cz, ci, 11 + bi,
                                                                  settings.seed + 964u));
                        const int bladeX = worldX + ox;
                        const int bladeZ = worldZ + oz;
                        const int bladeGround =
                            beachHeightAt(bladeX, bladeZ, settings, totalDims);
                        const int bladeH = 1 + static_cast<int>(hash4(cx, cz, ci, 19 + bi,
                                                                      settings.seed + 965u) %
                                                                  3u);
                        for (int by = 1; by <= bladeH; ++by)
                        {
                            const int wy = bladeGround + by;
                            if (getVoxelWorld(bladeX, wy, bladeZ) == 0)
                            {
                                setVoxelWorld(bladeX, wy, bladeZ,
                                              beachDuneGrassMaterialAt(bladeX, wy, bladeZ,
                                                                       settings));
                            }
                        }
                    }
                }

                const int driftwoodPieces = std::max(1, settings.treesPerChunk / 2);
                for (int di = 0; di < driftwoodPieces; ++di)
                {
                    const uint32_t hx = hash4(cx, cz, di, 30, settings.seed + 966u);
                    const uint32_t hz = hash4(cx, cz, di, 31, settings.seed + 967u);
                    const int worldX = cx * chunkDims.x + static_cast<int>(hx % chunkDims.x);
                    const int worldZ = cz * chunkDims.z + static_cast<int>(hz % chunkDims.z);
                    const float shore = beachShoreDistance(worldX, worldZ, settings, totalDims);
                    if (shore < 12.0f || shore > 58.0f)
                    {
                        continue;
                    }

                    const bool alongX =
                        (hash4(cx, cz, di, 32, settings.seed + 968u) & 1u) == 0u;
                    const int length = randIntRange(
                        4, 8, hash4(cx, cz, di, 33, settings.seed + 969u));
                    const int groundY = beachHeightAt(worldX, worldZ, settings, totalDims);
                    for (int li = 0; li < length; ++li)
                    {
                        const int wx = worldX + (alongX ? li : 0);
                        const int wz = worldZ + (alongX ? 0 : li);
                        const int wy = beachHeightAt(wx, wz, settings, totalDims) + 1;
                        if (std::abs(wy - (groundY + 1)) > 2)
                        {
                            continue;
                        }
                        if (getVoxelWorld(wx, wy, wz) == 0)
                        {
                            setVoxelWorld(wx, wy, wz, beachDriftwoodMaterialAt(wx, wy, wz,
                                                                               settings));
                        }
                    }
                }
            }
        }
    }

    if (settings.enableRocks)
    {
        const int rockClusters = std::max(0, settings.rocksPerChunk * 2);
        for (int cz = 0; cz < gridDims.z; ++cz)
        {
            for (int cx = 0; cx < gridDims.x; ++cx)
            {
                for (int ri = 0; ri < rockClusters; ++ri)
                {
                    const uint32_t hx = hash4(cx, cz, ri, 40, settings.seed + 970u);
                    const uint32_t hz = hash4(cx, cz, ri, 41, settings.seed + 971u);
                    const int worldX = cx * chunkDims.x + static_cast<int>(hx % chunkDims.x);
                    const int worldZ = cz * chunkDims.z + static_cast<int>(hz % chunkDims.z);
                    const float shore = beachShoreDistance(worldX, worldZ, settings, totalDims);
                    if (shore < -8.0f || shore > 30.0f)
                    {
                        continue;
                    }

                    const int radius =
                        randIntRange(1, 2, hash4(cx, cz, ri, 42, settings.seed + 972u));
                    const int groundY = beachHeightAt(worldX, worldZ, settings, totalDims);
                    for (int dz = -radius; dz <= radius; ++dz)
                    {
                        for (int dx = -radius; dx <= radius; ++dx)
                        {
                            if (dx * dx + dz * dz > radius * radius + 1)
                            {
                                continue;
                            }
                            const int wx = worldX + dx;
                            const int wz = worldZ + dz;
                            const int wy = beachHeightAt(wx, wz, settings, totalDims) + 1;
                            if (std::abs(wy - (groundY + 1)) > 2)
                            {
                                continue;
                            }
                            if (getVoxelWorld(wx, wy, wz) == 0)
                            {
                                setVoxelWorld(wx, wy, wz,
                                              beachRockMaterialAt(wx, wy, wz, settings));
                            }
                        }
                    }
                }
            }
        }
    }

    for (int cz = 0; cz < gridDims.z; ++cz)
    {
        for (int cy = 0; cy < gridDims.y; ++cy)
        {
            for (int cx = 0; cx < gridDims.x; ++cx)
            {
                const size_t chunkIndex = chunkIndexFromCoord({cx, cy, cz}, gridDims);
                auto& chunk = outData[chunkIndex];

                for (int z = 0; z < paddedDims.z; ++z)
                {
                    const int worldZ = cz * chunkDims.z + (z - 1);
                    for (int y = 0; y < paddedDims.y; ++y)
                    {
                        const int worldY = cy * chunkDims.y + (y - 1);
                        for (int x = 0; x < paddedDims.x; ++x)
                        {
                            const int worldX = cx * chunkDims.x + (x - 1);
                            chunk[voxelIndex(x, y, z, paddedDims)] =
                                getVoxelWorld(worldX, worldY, worldZ);
                        }
                    }
                }
            }
        }
    }

    return true;
}

bool buildWorldData(const ProceduralWorldSettings& settings, const glm::ivec3& totalDims,
                    std::vector<std::vector<uint8_t>>& outData)
{
    if (settings.terrainStyle == ProceduralWorldTerrainStyle::Beach)
    {
        return buildBeachWorldData(settings, totalDims, outData);
    }

    const glm::ivec3 gridDims = settings.gridDims;
    const glm::ivec3 chunkDims = settings.chunkDims;
    const glm::ivec3 paddedDims = chunkDims + glm::ivec3(2);
    if (gridDims.x <= 0 || gridDims.y <= 0 || gridDims.z <= 0)
    {
        return false;
    }
    if (chunkDims.x <= 0 || chunkDims.y <= 0 || chunkDims.z <= 0)
    {
        return false;
    }

    const size_t volumeCount = static_cast<size_t>(gridDims.x) * static_cast<size_t>(gridDims.y) *
                               static_cast<size_t>(gridDims.z);
    const size_t voxelCountPerChunk =
        static_cast<size_t>(paddedDims.x) * static_cast<size_t>(paddedDims.y) *
        static_cast<size_t>(paddedDims.z);

    outData.clear();
    outData.resize(volumeCount);
    for (auto& chunk : outData)
    {
        chunk.assign(voxelCountPerChunk, 0);
    }

    const size_t totalVoxelCount =
        static_cast<size_t>(totalDims.x) * static_cast<size_t>(totalDims.y) *
        static_cast<size_t>(totalDims.z);
    std::vector<uint8_t> world(totalVoxelCount, 0);

    auto inWorldBounds = [&](int wx, int wy, int wz) -> bool
    {
        return wx >= 0 && wy >= 0 && wz >= 0 && wx < totalDims.x && wy < totalDims.y &&
               wz < totalDims.z;
    };

    auto worldIndex = [&](int wx, int wy, int wz) -> size_t
    {
        return static_cast<size_t>(wx) + static_cast<size_t>(wy) * static_cast<size_t>(totalDims.x) +
               static_cast<size_t>(wz) * static_cast<size_t>(totalDims.x) *
                   static_cast<size_t>(totalDims.y);
    };

    auto setVoxelWorld = [&](int wx, int wy, int wz, uint8_t id)
    {
        if (!inWorldBounds(wx, wy, wz))
        {
            return;
        }
        world[worldIndex(wx, wy, wz)] = id;
    };

    auto getVoxelWorld = [&](int wx, int wy, int wz) -> uint8_t
    {
        if (!inWorldBounds(wx, wy, wz))
        {
            return 0;
        }
        return world[worldIndex(wx, wy, wz)];
    };

    const int noiseScale = std::max(1, static_cast<int>(settings.noiseScale));
    const int caveScale = std::max(1, static_cast<int>(settings.caveNoiseScale));
    const int dirtDepth = std::max(1, settings.dirtDepth);

    const int maxY = std::max(0, totalDims.y - 1);
    int caveMinY = std::clamp(settings.caveMinY, 0, maxY);
    int caveMaxY = std::clamp(settings.caveMaxY, 0, maxY);
    if (caveMaxY < caveMinY)
    {
        std::swap(caveMaxY, caveMinY);
    }

    // terrain + caves.
    for (int worldZ = 0; worldZ < totalDims.z; ++worldZ)
    {
        for (int worldX = 0; worldX < totalDims.x; ++worldX)
        {
            const int h = heightAt(worldX, worldZ, settings, noiseScale, totalDims.y);

            for (int worldY = 0; worldY < totalDims.y; ++worldY)
            {
                if (worldY > h)
                {
                    continue;
                }

                uint8_t id = kMatStone;
                if (worldY == h)
                {
                    id = kMatGrass;
                }
                else if (worldY >= h - dirtDepth)
                {
                    id = kMatDirt;
                }

                if (settings.enableCaves && id == kMatStone && worldY >= caveMinY &&
                    worldY <= caveMaxY && worldY < h - dirtDepth - 1)
                {
                    const float caveNoise = valueNoise3D(
                        worldX, worldY, worldZ, caveScale, settings.seed + 77u);
                    if (caveNoise > settings.caveThreshold)
                    {
                        id = 0;
                    }
                }

                setVoxelWorld(worldX, worldY, worldZ, id);
            }
        }
    }

    // trees.
    if (settings.enableTrees)
    {
        const int treesPerChunk = std::max(0, settings.treesPerChunk);
        const int trunkMinH = std::max(1, settings.trunkMinH);
        const int trunkMaxH = std::max(trunkMinH, settings.trunkMaxH);
        const int leafR = std::max(1, settings.leafRadius);

        for (int cz = 0; cz < gridDims.z; ++cz)
        {
            for (int cx = 0; cx < gridDims.x; ++cx)
            {
                for (int ti = 0; ti < treesPerChunk; ++ti)
                {
                    const uint32_t hx = hash4(cx, cz, ti, 0, settings.seed + 500u);
                    const uint32_t hz = hash4(cx, cz, ti, 1, settings.seed + 501u);
                    const int worldX = cx * chunkDims.x + static_cast<int>(hx % chunkDims.x);
                    const int worldZ = cz * chunkDims.z + static_cast<int>(hz % chunkDims.z);

                    const int groundY = heightAt(worldX, worldZ, settings, noiseScale, totalDims.y);
                    if (!inWorldBounds(worldX, groundY, worldZ) ||
                        !inWorldBounds(worldX, groundY + 1, worldZ))
                    {
                        continue;
                    }
                    if (getVoxelWorld(worldX, groundY, worldZ) != kMatGrass)
                    {
                        continue;
                    }

                    const int trunkH = randIntRange(
                        trunkMinH, trunkMaxH, hash4(cx, cz, ti, 2, settings.seed + 600u));

                    for (int i = 1; i <= trunkH; ++i)
                    {
                        setVoxelWorld(worldX, groundY + i, worldZ, kMatWood);
                    }

                    const glm::ivec3 center(worldX, groundY + trunkH + 1, worldZ);
                    for (int dz = -leafR; dz <= leafR; ++dz)
                    {
                        for (int dy = -leafR; dy <= leafR; ++dy)
                        {
                            for (int dx = -leafR; dx <= leafR; ++dx)
                            {
                                const int dist2 = dx * dx + dy * dy + dz * dz;
                                if (dist2 > leafR * leafR + 1)
                                {
                                    continue;
                                }

                                const int wx = center.x + dx;
                                const int wy = center.y + dy;
                                const int wz = center.z + dz;
                                const uint8_t cur = getVoxelWorld(wx, wy, wz);
                                if (cur == 0)
                                {
                                    setVoxelWorld(wx, wy, wz, kMatLeaves);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // rocks.
    if (settings.enableRocks)
    {
        const int rocksPerChunk = std::max(0, settings.rocksPerChunk);
        const int rockMinR = std::max(1, settings.rockMinR);
        const int rockMaxR = std::max(rockMinR, settings.rockMaxR);

        for (int cz = 0; cz < gridDims.z; ++cz)
        {
            for (int cx = 0; cx < gridDims.x; ++cx)
            {
                for (int ri = 0; ri < rocksPerChunk; ++ri)
                {
                    const uint32_t hx = hash4(cx, cz, ri, 0, settings.seed + 700u);
                    const uint32_t hz = hash4(cx, cz, ri, 1, settings.seed + 701u);
                    const int worldX = cx * chunkDims.x + static_cast<int>(hx % chunkDims.x);
                    const int worldZ = cz * chunkDims.z + static_cast<int>(hz % chunkDims.z);

                    const int groundY = heightAt(worldX, worldZ, settings, noiseScale, totalDims.y);
                    if (!inWorldBounds(worldX, groundY, worldZ))
                    {
                        continue;
                    }

                    const int radius = randIntRange(
                        rockMinR, rockMaxR, hash4(cx, cz, ri, 2, settings.seed + 702u));
                    const glm::ivec3 center(worldX, groundY - (radius / 2), worldZ);

                    for (int dz = -radius; dz <= radius; ++dz)
                    {
                        for (int dy = -radius; dy <= radius; ++dy)
                        {
                            for (int dx = -radius; dx <= radius; ++dx)
                            {
                                const int dist2 = dx * dx + dy * dy + dz * dz;
                                if (dist2 > radius * radius)
                                {
                                    continue;
                                }

                                const int wx = center.x + dx;
                                const int wy = center.y + dy;
                                const int wz = center.z + dz;

                                const uint8_t cur = getVoxelWorld(wx, wy, wz);
                                if (cur == kMatWood)
                                {
                                    continue;
                                }
                                setVoxelWorld(wx, wy, wz, kMatStone);
                            }
                        }
                    }
                }
            }
        }
    }

    for (int cz = 0; cz < gridDims.z; ++cz)
    {
        for (int cy = 0; cy < gridDims.y; ++cy)
        {
            for (int cx = 0; cx < gridDims.x; ++cx)
            {
                const size_t chunkIndex = chunkIndexFromCoord({cx, cy, cz}, gridDims);
                auto& chunk = outData[chunkIndex];

                for (int z = 0; z < paddedDims.z; ++z)
                {
                    const int worldZ = cz * chunkDims.z + (z - 1);
                    for (int y = 0; y < paddedDims.y; ++y)
                    {
                        const int worldY = cy * chunkDims.y + (y - 1);
                        for (int x = 0; x < paddedDims.x; ++x)
                        {
                            const int worldX = cx * chunkDims.x + (x - 1);
                            chunk[voxelIndex(x, y, z, paddedDims)] =
                                getVoxelWorld(worldX, worldY, worldZ);
                        }
                    }
                }
            }
        }
    }

    return true;
}

template <size_t N>
void setPaletteBand(engine::VoxelPalette& palette, uint8_t base,
                    const std::array<glm::vec3, N>& colors, float roughness,
                    engine::VoxelMaterialCategory category)
{
    for (size_t i = 0; i < colors.size(); ++i)
    {
        palette.setEntry(0, static_cast<uint32_t>(base + i),
                         makeEntry(colors[i], roughness, 0.0f, category));
    }
}

void buildDefaultProceduralPalette(engine::VoxelPalette& palette)
{
    palette.buildDefaultPalette0();
    const auto category = [](uint8_t id) {
        return engine::scene::proceduralWorldMaterialCategory(
            engine::scene::ProceduralPaletteKind::Default, id);
    };
    palette.setEntry(0, kMatGrass,
                     makeEntry(glm::vec3(0.18f, 0.62f, 0.22f), 0.88f, 0.0f,
                               category(kMatGrass)));
    palette.setEntry(0, kMatDirt,
                     makeEntry(glm::vec3(0.50f, 0.35f, 0.22f), 0.95f, 0.0f,
                               category(kMatDirt)));
    palette.setEntry(0, kMatStone,
                     makeEntry(glm::vec3(0.48f, 0.50f, 0.55f), 0.97f, 0.0f,
                               category(kMatStone)));
    palette.setEntry(0, kMatWood,
                     makeEntry(glm::vec3(0.45f, 0.28f, 0.18f), 0.90f, 0.0f,
                               category(kMatWood)));
    palette.setEntry(0, kMatLeaves,
                     makeEntry(glm::vec3(0.16f, 0.55f, 0.18f), 0.92f, 0.0f,
                               category(kMatLeaves)));
}

void buildBeachPalette(engine::VoxelPalette& palette)
{
    palette.buildDefaultPalette0();
    const auto category = [](uint8_t id) {
        return engine::scene::proceduralWorldMaterialCategory(
            engine::scene::ProceduralPaletteKind::Beach, id);
    };
    palette.setEntry(0, kMatGrass,
                     makeEntry(glm::vec3(0.26f, 0.42f, 0.18f), 0.90f, 0.0f,
                               category(kMatGrass)));
    palette.setEntry(0, kMatDirt,
                     makeEntry(glm::vec3(0.42f, 0.34f, 0.24f), 0.96f, 0.0f,
                               category(kMatDirt)));
    palette.setEntry(0, kMatStone,
                     makeEntry(glm::vec3(0.45f, 0.47f, 0.49f), 0.97f, 0.0f,
                               category(kMatStone)));
    palette.setEntry(0, kMatWood,
                     makeEntry(glm::vec3(0.38f, 0.30f, 0.22f), 0.93f, 0.0f,
                               category(kMatWood)));
    palette.setEntry(0, kMatLeaves,
                     makeEntry(glm::vec3(0.20f, 0.44f, 0.18f), 0.91f, 0.0f,
                               category(kMatLeaves)));

    setPaletteBand(palette, kBeachDrySandBase,
                   std::array<glm::vec3, kBeachDrySandCount>{
                       glm::vec3(0.58f, 0.49f, 0.34f),
                       glm::vec3(0.64f, 0.55f, 0.38f),
                       glm::vec3(0.70f, 0.62f, 0.43f),
                       glm::vec3(0.76f, 0.68f, 0.48f),
                       glm::vec3(0.82f, 0.75f, 0.55f),
                       glm::vec3(0.88f, 0.82f, 0.64f),
                       glm::vec3(0.93f, 0.88f, 0.72f),
                       glm::vec3(0.98f, 0.94f, 0.82f),
                       glm::vec3(0.86f, 0.72f, 0.52f),
                       glm::vec3(0.78f, 0.63f, 0.45f),
                       glm::vec3(0.95f, 0.86f, 0.63f),
                       glm::vec3(0.72f, 0.58f, 0.40f),
                   },
                   0.98f, category(kBeachDrySandBase));

    setPaletteBand(palette, kBeachWetSandBase,
                   std::array<glm::vec3, kBeachWetSandCount>{
                       glm::vec3(0.25f, 0.27f, 0.24f),
                       glm::vec3(0.31f, 0.31f, 0.27f),
                       glm::vec3(0.37f, 0.35f, 0.29f),
                       glm::vec3(0.43f, 0.39f, 0.31f),
                       glm::vec3(0.49f, 0.44f, 0.34f),
                       glm::vec3(0.55f, 0.49f, 0.38f),
                       glm::vec3(0.61f, 0.54f, 0.42f),
                       glm::vec3(0.67f, 0.59f, 0.46f),
                       glm::vec3(0.36f, 0.40f, 0.39f),
                       glm::vec3(0.44f, 0.46f, 0.42f),
                       glm::vec3(0.58f, 0.56f, 0.48f),
                       glm::vec3(0.72f, 0.66f, 0.52f),
                   },
                   0.99f, category(kBeachWetSandBase));

    setPaletteBand(palette, kBeachShellBase,
                   std::array<glm::vec3, kBeachShellCount>{
                       glm::vec3(0.95f, 0.91f, 0.82f),
                       glm::vec3(1.00f, 0.82f, 0.68f),
                       glm::vec3(0.92f, 0.66f, 0.58f),
                       glm::vec3(0.86f, 0.80f, 0.88f),
                       glm::vec3(0.74f, 0.86f, 0.90f),
                       glm::vec3(0.98f, 0.96f, 0.90f),
                       glm::vec3(0.78f, 0.65f, 0.52f),
                       glm::vec3(0.55f, 0.48f, 0.40f),
                   },
                   0.86f, category(kBeachShellBase));

    setPaletteBand(palette, kBeachDuneGrassBase,
                   std::array<glm::vec3, kBeachDuneGrassCount>{
                       glm::vec3(0.17f, 0.28f, 0.11f),
                       glm::vec3(0.30f, 0.45f, 0.17f),
                       glm::vec3(0.46f, 0.56f, 0.24f),
                       glm::vec3(0.62f, 0.58f, 0.30f),
                   },
                   0.88f, category(kBeachDuneGrassBase));

    setPaletteBand(palette, kBeachRockBase,
                   std::array<glm::vec3, kBeachRockCount>{
                       glm::vec3(0.20f, 0.22f, 0.23f),
                       glm::vec3(0.28f, 0.30f, 0.30f),
                       glm::vec3(0.36f, 0.38f, 0.37f),
                       glm::vec3(0.45f, 0.46f, 0.43f),
                       glm::vec3(0.53f, 0.51f, 0.46f),
                       glm::vec3(0.38f, 0.43f, 0.39f),
                       glm::vec3(0.34f, 0.32f, 0.41f),
                       glm::vec3(0.58f, 0.52f, 0.42f),
                   },
                   0.97f, category(kBeachRockBase));

    setPaletteBand(palette, kBeachDriftwoodBase,
                   std::array<glm::vec3, kBeachDriftwoodCount>{
                       glm::vec3(0.18f, 0.15f, 0.12f),
                       glm::vec3(0.28f, 0.23f, 0.18f),
                       glm::vec3(0.39f, 0.32f, 0.24f),
                       glm::vec3(0.48f, 0.42f, 0.34f),
                       glm::vec3(0.55f, 0.52f, 0.46f),
                       glm::vec3(0.33f, 0.34f, 0.32f),
                   },
                   0.94f, category(kBeachDriftwoodBase));
}

} // namespace

bool ProceduralWorldScene::init(VulkanContext& ctx, engine::VoxelWorld& world,
                                engine::VoxelPalette& palette,
                                const ProceduralWorldSettings& settings)
{
    return regenerate(ctx, world, palette, settings, false);
}

bool ProceduralWorldScene::regenerate(VulkanContext& ctx, engine::VoxelWorld& world,
                                      engine::VoxelPalette& palette,
                                      const ProceduralWorldSettings& settings, bool reuseVolumes)
{
    logInfo("ProceduralWorldScene", "Initializing ProceduralWorldScene.");

    if (settings.terrainStyle == ProceduralWorldTerrainStyle::Beach)
    {
        buildBeachPalette(palette);
    }
    else
    {
        buildDefaultProceduralPalette(palette);
    }

    if (!palette.upload(ctx))
    {
        logError("ProceduralWorldScene", "Failed to upload procedural palette.");
        return false;
    }

    const glm::ivec3 totalDims(settings.chunkDims.x * settings.gridDims.x,
                               settings.chunkDims.y * settings.gridDims.y,
                               settings.chunkDims.z * settings.gridDims.z);
    const glm::ivec3 paddedDims = settings.chunkDims + glm::ivec3(2);

    const glm::vec3 basePos(-0.5f * static_cast<float>(totalDims.x), 0.0f,
                            -0.5f * static_cast<float>(totalDims.z));

    const size_t volumeCount = static_cast<size_t>(settings.gridDims.x) *
                               static_cast<size_t>(settings.gridDims.y) *
                               static_cast<size_t>(settings.gridDims.z);

    std::vector<std::vector<uint8_t>> data;
    if (!buildWorldData(settings, totalDims, data))
    {
        logError("ProceduralWorldScene", "Failed to build procedural world data.");
        return false;
    }

    const bool canReuse =
        reuseVolumes && world.instances().size() == volumeCount &&
        std::all_of(world.instances().begin(), world.instances().end(),
                    [&](const engine::VoxelInstance& inst)
                    {
                        return inst.volume.dimensions() == paddedDims;
                    });

    if (canReuse)
    {
        for (size_t i = 0; i < volumeCount; ++i)
        {
            if (!world.instances()[i].volume.upload(ctx, data[i], palette.buffer()))
            {
                logError("ProceduralWorldScene",
                         makeLogMessage("Failed to upload procedural chunk ", i));
                return false;
            }
        }

        logInfo("ProceduralWorldScene",
                makeLogMessage("ProceduralWorldScene updated ", volumeCount, " chunks"));
        return true;
    }

    world.clearScene(ctx);

    std::vector<engine::VolumeSpec> specs;
    specs.resize(volumeCount);
    for (int z = 0; z < settings.gridDims.z; ++z)
    {
        for (int y = 0; y < settings.gridDims.y; ++y)
        {
            for (int x = 0; x < settings.gridDims.x; ++x)
            {
                const glm::ivec3 coord(x, y, z);
                const size_t index = chunkIndexFromCoord(coord, settings.gridDims);
                const glm::vec3 worldPos =
                    basePos + glm::vec3(x * settings.chunkDims.x, y * settings.chunkDims.y,
                                        z * settings.chunkDims.z) -
                    glm::vec3(1.0f);
                engine::VolumeSpec spec{};
                spec.name = "Chunk(" + std::to_string(x) + "," + std::to_string(y) + "," +
                            std::to_string(z) + ")";
                spec.dims = paddedDims;
                spec.position = worldPos;
                spec.flags = engine::VoxelVolume::FLAG_STATIC;
                specs[index] = std::move(spec);
            }
        }
    }

    if (!world.initAquariumScene(ctx, palette, specs.size(),
                                 [&](size_t index) -> const engine::VolumeSpec& {
                                     return specs[index];
                                 },
                                 [&](size_t index) -> const std::vector<uint8_t>& {
                                     return data[index];
                                 }))
    {
        logError("ProceduralWorldScene", "Failed to initialize procedural volumes.");
        return false;
    }

    logInfo("ProceduralWorldScene",
            makeLogMessage("ProceduralWorldScene initialized with ", specs.size(), " chunks"));
    return true;
}
