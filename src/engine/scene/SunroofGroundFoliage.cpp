#include "engine/scene/SunroofGroundFoliage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "Utils/SpatialHash.h"
#include "engine/voxel/VoxelBuilder.h"

namespace engine::scene
{
namespace
{
constexpr int kMinimumBorderCells = 8;
constexpr float kTau = 6.28318530718f;

constexpr std::array<glm::ivec3, 13> kPlantOffsets{{
    {0, 0, 0},
    {1, 0, 0},
    {-1, 0, 0},
    {0, 0, 1},
    {0, 0, -1},
    {1, 0, 1},
    {-1, 0, 1},
    {1, 0, -1},
    {-1, 0, -1},
    {2, 0, 0},
    {-2, 0, 0},
    {0, 0, 2},
    {0, 0, -2},
}};

constexpr std::array<glm::ivec3, 8> kDirections{{
    {1, 0, 0},
    {-1, 0, 0},
    {0, 0, 1},
    {0, 0, -1},
    {1, 0, 1},
    {-1, 0, 1},
    {1, 0, -1},
    {-1, 0, -1},
}};

[[nodiscard]] bool validBand(uint8_t base, uint8_t count) noexcept
{
    return count > 0 && static_cast<unsigned int>(base) + count <=
                            static_cast<unsigned int>(std::numeric_limits<uint8_t>::max()) + 1u;
}

[[nodiscard]] bool validPalette(const SunroofGroundFoliagePalette& palette) noexcept
{
    return validBand(palette.grassBase, palette.grassCount) &&
           validBand(palette.leafBase, palette.leafCount) &&
           validBand(palette.reedBase, palette.reedCount) &&
           validBand(palette.accentBase, palette.accentCount) &&
           validBand(palette.flowerBase, palette.flowerCount);
}

[[nodiscard]] bool validPalette(const SunroofGroundSurfacePalette& palette) noexcept
{
    return validBand(palette.grassBase, palette.grassCount) &&
           validBand(palette.algaeBase, palette.algaeCount) &&
           validBand(palette.gravelBase, palette.gravelCount);
}

[[nodiscard]] bool idInBand(uint8_t id, uint8_t base, uint8_t count) noexcept
{
    const unsigned int value = id;
    return value >= base && value < static_cast<unsigned int>(base) + count;
}

[[nodiscard]] uint8_t bandTierId(uint8_t base, uint8_t count, int tier, uint32_t hash) noexcept
{
    const unsigned int safeTier = static_cast<unsigned int>(std::clamp(tier, 0, 2));
    const unsigned int first = safeTier * count / 3u;
    const unsigned int end = safeTier == 2u ? count : (safeTier + 1u) * count / 3u;
    const unsigned int tierCount = std::max(1u, end - first);
    return static_cast<uint8_t>(base + first + hash % tierCount);
}

[[nodiscard]] int borderCells(const glm::ivec3& dims) noexcept
{
    return std::max(kMinimumBorderCells, std::min(dims.x, dims.z) / 28);
}

[[nodiscard]] bool insideBorder(const glm::ivec3& dims, int x, int z) noexcept
{
    const int border = borderCells(dims);
    return x >= border && x < dims.x - border && z >= border && z < dims.z - border;
}

[[nodiscard]] float smoothStep(float value) noexcept
{
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] float hashUnit(int x, int z, uint32_t seed) noexcept
{
    const uint32_t hash = SpatialHash::hash3D(x + static_cast<int>(seed & 0x7fffu), 97,
                                              z - static_cast<int>((seed >> 15u) & 0x7fffu));
    return static_cast<float>(hash & 0xffffu) / 65535.0f;
}

[[nodiscard]] int floorDiv(int value, int divisor) noexcept
{
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] float smoothNoise(int x, int z, int cellSize, uint32_t seed) noexcept
{
    const int cellX = floorDiv(x, cellSize);
    const int cellZ = floorDiv(z, cellSize);
    const int localX = x - cellX * cellSize;
    const int localZ = z - cellZ * cellSize;
    const float tx = smoothStep(static_cast<float>(localX) / static_cast<float>(cellSize));
    const float tz = smoothStep(static_cast<float>(localZ) / static_cast<float>(cellSize));
    const float n00 = hashUnit(cellX, cellZ, seed);
    const float n10 = hashUnit(cellX + 1, cellZ, seed);
    const float n01 = hashUnit(cellX, cellZ + 1, seed);
    const float n11 = hashUnit(cellX + 1, cellZ + 1, seed);
    const float nx0 = n00 + (n10 - n00) * tx;
    const float nx1 = n01 + (n11 - n01) * tx;
    return nx0 + (nx1 - nx0) * tz;
}

[[nodiscard]] float habitatWeight(const glm::ivec3& dims, int x, int z, uint32_t seed) noexcept
{
    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(dims.x);
    const float v = (static_cast<float>(z) + 0.5f) / static_cast<float>(dims.z);
    const int shortSide = std::max(1, std::min(dims.x, dims.z));
    const float broad = smoothNoise(x, z, std::max(4, shortSide / 5), seed ^ 0x8da6b343u);
    const float medium = smoothNoise(x, z, std::max(3, shortSide / 10), seed ^ 0xd8163841u);
    const float detail = smoothNoise(x, z, std::max(2, shortSide / 20), seed ^ 0x6c8e9cf5u);
    const float sweep = 0.5f + 0.5f * std::sin(u * kTau * 1.35f + v * kTau * 0.82f + 0.65f);
    return std::clamp(broad * 0.46f + medium * 0.28f + detail * 0.14f + sweep * 0.12f, 0.0f, 1.0f);
}

[[nodiscard]] bool insideNavigationChannel(const glm::ivec3& dims, int x, int z) noexcept
{
    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(dims.x);
    const float v = (static_cast<float>(z) + 0.5f) / static_cast<float>(dims.z);
    const float center = 0.50f + 0.075f * std::sin(v * kTau + 0.55f);
    const float width = 0.030f + 0.012f * (0.5f + 0.5f * std::sin(v * kTau * 1.7f));
    return v > 0.10f && v < 0.90f && std::fabs(u - center) < width;
}

[[nodiscard]] uint32_t latticeHash(int latticeX, int latticeZ, uint32_t seed, int salt) noexcept
{
    // keep all coordinate arithmetic safely inside the signed range accepted by
    // SpatialHash while still mixing both halves of the authored seed.  using a
    // single 31-bit offset here could overflow before hash3D converted it to
    // unsigned arithmetic.
    const int seedX = static_cast<int>(seed & 0xffffu);
    const int seedY = static_cast<int>((seed >> 8u) & 0xffffu);
    const int seedZ = static_cast<int>((seed >> 16u) & 0xffffu);
    return SpatialHash::hash3D(latticeX + seedX + salt, 17 + seedY + salt * 5,
                               latticeZ - seedZ - salt * 3);
}

[[nodiscard]] glm::ivec3 jitteredRoot(int latticeX, int latticeZ, int stride,
                                      uint32_t hash) noexcept
{
    return {latticeX * stride + static_cast<int>(hash % static_cast<uint32_t>(stride)), 0,
            latticeZ * stride + static_cast<int>((hash >> 5u) % static_cast<uint32_t>(stride))};
}

[[nodiscard]] uint8_t coherentSurfaceShade(uint8_t base, uint8_t count, float shadeField,
                                           uint8_t grounding) noexcept
{
    const unsigned int usable = std::min<unsigned int>(4u, count);
    const unsigned int first = count > usable ? (count - usable) / 2u : 0u;
    const float groundingStrength = static_cast<float>(grounding) / 255.0f;
    const float adjusted = std::clamp(shadeField - groundingStrength * 0.26f, 0.0f, 0.999f);
    const unsigned int shade =
        std::min<unsigned int>(usable - 1u, static_cast<unsigned int>(adjusted * usable));
    return static_cast<uint8_t>(base + first + shade);
}

} // namespace

uint8_t sunroofGroundFoliageGrounding(const glm::ivec3& dims, int x, int z, uint32_t seed) noexcept
{
    if (dims.x <= 0 || dims.z <= 0 || x < 0 || z < 0 || x >= dims.x || z >= dims.z ||
        !insideBorder(dims, x, z))
    {
        return 0;
    }

    float weight = habitatWeight(dims, x, z, seed);
    if (insideNavigationChannel(dims, x, z))
    {
        weight *= 0.20f;
    }
    else if (isSunroofGroundFoliageWalkway(dims, x, z))
    {
        weight *= 0.58f;
    }

    // feather the bottom of the range away so sparse regions remain clean and the
    // result reads as contact mass rather than a second floor texture.
    const float feathered = smoothStep((weight - 0.24f) / 0.58f);
    return static_cast<uint8_t>(std::clamp(feathered * 255.0f, 0.0f, 255.0f));
}

uint8_t sunroofGroundBlockMaterial(const glm::ivec3& dims, int x, int z,
                                   const SunroofGroundSurfacePalette& palette, uint8_t grounding,
                                   uint32_t seed) noexcept
{
    if (dims.x <= 0 || dims.z <= 0 || x < 0 || z < 0 || x >= dims.x || z >= dims.z ||
        !validPalette(palette))
    {
        return 0;
    }

    const float materialField = smoothNoise(x, z, 19, seed ^ 0xa4c1f326u) * 0.68f +
                                smoothNoise(x, z, 9, seed ^ 0x31c6d7b9u) * 0.32f;
    const float shadeField = smoothNoise(x, z, 11, seed ^ 0x7f4a7c15u);

    // grass owns most of the floor. low-frequency algae and gravel islands
    // provide substrate identity without the previous per-cell checkerboard.
    if (materialField < 0.19f)
    {
        return coherentSurfaceShade(palette.gravelBase, palette.gravelCount, shadeField, 0);
    }
    if (materialField < 0.35f)
    {
        return coherentSurfaceShade(palette.algaeBase, palette.algaeCount, shadeField, grounding);
    }
    return coherentSurfaceShade(palette.grassBase, palette.grassCount, shadeField, grounding);
}

bool isSunroofGroundFoliageWalkway(const glm::ivec3& dims, int x, int z) noexcept
{
    if (dims.x <= 0 || dims.z <= 0)
    {
        return false;
    }

    // this is now a compact, slightly offset focal lens rather than a large
    // sterile clearing. low turf may enter it; medium/tall archetypes are kept
    // below eye level.
    const float dx = static_cast<float>(x - (dims.x / 2 + dims.x / 48)) /
                     static_cast<float>(std::max(1, dims.x / 15));
    const float dz = static_cast<float>(z - (dims.z / 2 - dims.z / 56)) /
                     static_cast<float>(std::max(1, dims.z / 18));
    return dx * dx + dz * dz <= 1.0f;
}

SunroofGroundFoliageBuild buildSunroofGroundFoliage(const glm::ivec3& dims,
                                                    const SunroofGroundFoliagePalette& palette,
                                                    uint32_t seed)
{
    SunroofGroundFoliageBuild result{};
    if (dims.x <= 0 || dims.y <= 0 || dims.z <= 0)
    {
        return result;
    }

    engine::VoxelBuilder builder(dims);
    if (dims.x < 24 || dims.y < 5 || dims.z < 24 || !validPalette(palette))
    {
        result.voxels = std::move(builder.data());
        return result;
    }

    auto setFoliage = [&](int x, int y, int z, uint8_t material) {
        if (y < 0 || y >= dims.y || !insideBorder(dims, x, z))
        {
            return;
        }
        if (y > 1 &&
            (isSunroofGroundFoliageWalkway(dims, x, z) || insideNavigationChannel(dims, x, z)))
        {
            return;
        }
        builder.setVoxel(x, y, z, material);
    };

    auto addTaperedBlade = [&](int baseX, int baseZ, int height, uint8_t base, uint8_t count,
                               uint32_t bladeHash, bool wideBase) {
        const int safeHeight = std::clamp(height, 2, dims.y - 1);
        const bool bendAlongX = ((bladeHash >> 3u) & 1u) != 0u;
        const int bendDirection = ((bladeHash >> 4u) & 1u) != 0u ? 1 : -1;
        const int widthDirection = ((bladeHash >> 6u) & 1u) != 0u ? 1 : -1;
        int previousX = baseX;
        int previousZ = baseZ;
        for (int y = 0; y < safeHeight; ++y)
        {
            int bend = y >= safeHeight / 2 ? 1 : 0;
            if (safeHeight >= 8 && y >= safeHeight - 2)
            {
                ++bend;
            }
            const int x = baseX + (bendAlongX ? bendDirection * bend : 0);
            const int z = baseZ + (bendAlongX ? 0 : bendDirection * bend);
            const int tier = y * 3 / std::max(1, safeHeight);
            const uint8_t material = bandTierId(base, count, tier, bladeHash + y * 19u);
            if (x != previousX || z != previousZ)
            {
                setFoliage(previousX, y, previousZ, material);
            }
            setFoliage(x, y, z, material);
            if (wideBase && y < 2)
            {
                setFoliage(x + (bendAlongX ? 0 : widthDirection), y,
                           z + (bendAlongX ? widthDirection : 0), material);
            }
            previousX = x;
            previousZ = z;
        }
    };

    auto addTurfPatch = [&](int rootX, int rootZ, uint32_t detailHash) {
        const int radius = 1 + static_cast<int>((detailHash >> 5u) & 1u);
        for (int dz = -radius; dz <= radius; ++dz)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                const uint32_t cellHash = SpatialHash::hash3D(
                    rootX + dx * 23, static_cast<int>(detailHash & 0x7fffffffu), rootZ + dz * 29);
                if (std::abs(dx) + std::abs(dz) > radius + 1 || cellHash % 100u >= 74u)
                {
                    continue;
                }
                setFoliage(rootX + dx, 0, rootZ + dz,
                           bandTierId(palette.grassBase, palette.grassCount, 0, cellHash));
                if ((cellHash >> 8u) % 100u < 48u)
                {
                    setFoliage(rootX + dx, 1, rootZ + dz,
                               bandTierId(palette.grassBase, palette.grassCount,
                                          (cellHash & 1u) != 0u ? 1 : 2, cellHash >> 11u));
                }
            }
        }
    };

    auto addRibbonTuft = [&](int rootX, int rootZ, uint32_t detailHash) {
        const int bladeCount = 5 + static_cast<int>((detailHash >> 7u) % 4u);
        const int rotation = static_cast<int>((detailHash >> 11u) % kPlantOffsets.size());
        for (int blade = 0; blade < bladeCount; ++blade)
        {
            const glm::ivec3 offset = kPlantOffsets[(rotation + blade * 2) % kPlantOffsets.size()];
            const uint32_t bladeHash = SpatialHash::hash3D(
                rootX + blade * 31, static_cast<int>(detailHash & 0x7fffffffu), rootZ - blade * 17);
            addTaperedBlade(rootX + offset.x, rootZ + offset.z,
                            4 + static_cast<int>((bladeHash >> 15u) % 5u), palette.leafBase,
                            palette.leafCount, bladeHash, true);
        }
    };

    auto addBroadLeaf = [&](int rootX, int rootZ, uint32_t detailHash) {
        const bool accentPlant = ((detailHash >> 9u) % 100u) < 38u;
        const uint8_t base = accentPlant ? palette.accentBase : palette.leafBase;
        const uint8_t count = accentPlant ? palette.accentCount : palette.leafCount;
        const int centerHeight = 2 + static_cast<int>((detailHash >> 13u) % 3u);
        for (int y = 0; y <= centerHeight; ++y)
        {
            setFoliage(rootX, y, rootZ,
                       bandTierId(base, count, y * 3 / (centerHeight + 1), detailHash + y));
        }

        const int leafCount = 5 + static_cast<int>((detailHash >> 16u) % 3u);
        const int rotation = static_cast<int>((detailHash >> 19u) % kDirections.size());
        for (int leaf = 0; leaf < leafCount; ++leaf)
        {
            const glm::ivec3 direction = kDirections[(rotation + leaf) % kDirections.size()];
            const int length = 2 + static_cast<int>((detailHash >> (leaf % 8u)) % 3u);
            for (int step = 1; step <= length; ++step)
            {
                const int y = 1 + step / 2;
                const uint8_t material =
                    bandTierId(base, count, std::min(2, step * 3 / (length + 1)),
                               detailHash + leaf * 41u + static_cast<uint32_t>(step));
                const int x = rootX + direction.x * step;
                const int z = rootZ + direction.z * step;
                setFoliage(x, y, z, material);
                if (step <= 2 && direction.x == 0)
                {
                    setFoliage(x + 1, y, z, material);
                }
                else if (step <= 2 && direction.z == 0)
                {
                    setFoliage(x, y, z + 1, material);
                }
            }
        }
    };

    auto addReedFan = [&](int rootX, int rootZ, uint32_t detailHash) {
        const int bladeCount = 3 + static_cast<int>((detailHash >> 8u) % 3u);
        const int rotation = static_cast<int>((detailHash >> 12u) % kPlantOffsets.size());
        for (int blade = 0; blade < bladeCount; ++blade)
        {
            const glm::ivec3 offset = kPlantOffsets[(rotation + blade * 3) % kPlantOffsets.size()];
            const uint32_t bladeHash =
                detailHash ^ (0x9e3779b9u * static_cast<uint32_t>(blade + 1));
            addTaperedBlade(rootX + offset.x, rootZ + offset.z,
                            8 + static_cast<int>((bladeHash >> 17u) % 5u), palette.reedBase,
                            palette.reedCount, bladeHash, false);
        }
    };

    auto addFlowerColony = [&](int rootX, int rootZ, uint32_t detailHash) {
        const int stemCount = 3 + static_cast<int>((detailHash >> 7u) % 3u);
        const int rotation = static_cast<int>((detailHash >> 11u) % kPlantOffsets.size());
        for (int stemIndex = 0; stemIndex < stemCount; ++stemIndex)
        {
            const glm::ivec3 offset =
                kPlantOffsets[(rotation + stemIndex * 3) % kPlantOffsets.size()];
            const uint32_t stemHash =
                detailHash ^ (0x85ebca6bu * static_cast<uint32_t>(stemIndex + 1));
            // V2.6 lifts the floral accents above the dense turf so their coral,
            // lavender, cream, and gold tips remain legible in the water wash.
            const int height = 6 + static_cast<int>((stemHash >> 13u) % 6u);
            for (int y = 0; y < height; ++y)
            {
                setFoliage(rootX + offset.x, y, rootZ + offset.z,
                           bandTierId(palette.leafBase, palette.leafCount,
                                      std::min(2, y * 3 / std::max(1, height)),
                                      stemHash + static_cast<uint32_t>(y)));
            }
            const uint8_t blossom =
                bandTierId(palette.flowerBase, palette.flowerCount, 2, stemHash >> 8u);
            setFoliage(rootX + offset.x, height, rootZ + offset.z, blossom);
            const glm::ivec3 petal = kDirections[(rotation + stemIndex) % kDirections.size()];
            setFoliage(rootX + offset.x + petal.x, height, rootZ + offset.z + petal.z, blossom);
            if ((stemHash & 1u) != 0u)
            {
                setFoliage(rootX + offset.x - petal.x, height, rootZ + offset.z - petal.z, blossom);
            }
            if ((stemHash & 2u) != 0u)
            {
                const glm::ivec3 crossPetal =
                    kDirections[(rotation + stemIndex + 2) % kDirections.size()];
                setFoliage(rootX + offset.x + crossPetal.x, height,
                           rootZ + offset.z + crossPetal.z, blossom);
            }
        }
    };

    // low carpet: dense enough to unite the substrate, including a restrained
    // amount through the focal lens and navigation channel.
    constexpr int turfStride = 3;
    for (int latticeZ = 0; latticeZ * turfStride < dims.z; ++latticeZ)
    {
        for (int latticeX = 0; latticeX * turfStride < dims.x; ++latticeX)
        {
            const uint32_t rootHash = latticeHash(latticeX, latticeZ, seed, 3);
            const glm::ivec3 root = jitteredRoot(latticeX, latticeZ, turfStride, rootHash);
            if (!insideBorder(dims, root.x, root.z))
            {
                continue;
            }
            float chance = 0.34f + habitatWeight(dims, root.x, root.z, seed) * 0.46f;
            if (insideNavigationChannel(dims, root.x, root.z))
            {
                chance *= 0.24f;
            }
            else if (isSunroofGroundFoliageWalkway(dims, root.x, root.z))
            {
                chance *= 0.62f;
            }
            if (static_cast<float>((rootHash >> 9u) % 1000u) >= chance * 1000.0f)
            {
                continue;
            }
            addTurfPatch(root.x, root.z, rootHash ^ 0x4cf5ad43u);
            ++result.stats.turfPatchCount;
        }
    }

    // mid layer: fuller ribbon and broad-leaf silhouettes replace the old field
    // of single-cell posts.
    constexpr int mediumStride = 7;
    for (int latticeZ = 0; latticeZ * mediumStride < dims.z; ++latticeZ)
    {
        for (int latticeX = 0; latticeX * mediumStride < dims.x; ++latticeX)
        {
            const uint32_t rootHash = latticeHash(latticeX, latticeZ, seed, 11);
            const glm::ivec3 root = jitteredRoot(latticeX, latticeZ, mediumStride, rootHash);
            if (!insideBorder(dims, root.x, root.z) ||
                isSunroofGroundFoliageWalkway(dims, root.x, root.z) ||
                insideNavigationChannel(dims, root.x, root.z))
            {
                continue;
            }
            const float chance = 0.18f + habitatWeight(dims, root.x, root.z, seed) * 0.50f;
            if (static_cast<float>((rootHash >> 10u) % 1000u) >= chance * 1000.0f)
            {
                continue;
            }
            if ((rootHash >> 20u) % 100u < 56u)
            {
                addRibbonTuft(root.x, root.z, rootHash ^ 0xb5297a4du);
                ++result.stats.ribbonTuftCount;
            }
            else
            {
                addBroadLeaf(root.x, root.z, rootHash ^ 0x68e31da4u);
                ++result.stats.broadLeafCount;
            }
        }
    }

    // tall reeds favor the back and outer thirds, creating depth layers without a
    // continuous perimeter wall.
    constexpr int reedStride = 13;
    for (int latticeZ = 0; latticeZ * reedStride < dims.z; ++latticeZ)
    {
        for (int latticeX = 0; latticeX * reedStride < dims.x; ++latticeX)
        {
            const uint32_t rootHash = latticeHash(latticeX, latticeZ, seed, 23);
            const glm::ivec3 root = jitteredRoot(latticeX, latticeZ, reedStride, rootHash);
            if (!insideBorder(dims, root.x, root.z) ||
                isSunroofGroundFoliageWalkway(dims, root.x, root.z) ||
                insideNavigationChannel(dims, root.x, root.z))
            {
                continue;
            }
            const float u = static_cast<float>(root.x) / static_cast<float>(dims.x);
            const float v = static_cast<float>(root.z) / static_cast<float>(dims.z);
            const float outerBias = std::fabs(u - 0.5f) * 0.18f + v * 0.10f;
            const float chance =
                0.06f + habitatWeight(dims, root.x, root.z, seed) * 0.33f + outerBias;
            if (static_cast<float>((rootHash >> 8u) % 1000u) >= chance * 1000.0f)
            {
                continue;
            }
            addReedFan(root.x, root.z, rootHash ^ 0x1b56c4e9u);
            ++result.stats.reedFanCount;
        }
    }

    // floral colonies are sparse, grouped accents rather than isolated
    // cross-shaped flowers on every few lattice cells.
    constexpr int flowerStride = 14;
    for (int latticeZ = 0; latticeZ * flowerStride < dims.z; ++latticeZ)
    {
        for (int latticeX = 0; latticeX * flowerStride < dims.x; ++latticeX)
        {
            const uint32_t rootHash = latticeHash(latticeX, latticeZ, seed, 37);
            const glm::ivec3 root = jitteredRoot(latticeX, latticeZ, flowerStride, rootHash);
            if (!insideBorder(dims, root.x, root.z) ||
                isSunroofGroundFoliageWalkway(dims, root.x, root.z) ||
                insideNavigationChannel(dims, root.x, root.z))
            {
                continue;
            }
            const float chance = 0.10f + habitatWeight(dims, root.x, root.z, seed) * 0.31f;
            if (static_cast<float>((rootHash >> 12u) % 1000u) >= chance * 1000.0f)
            {
                continue;
            }
            addFlowerColony(root.x, root.z, rootHash ^ 0xc2b2ae35u);
            ++result.stats.flowerColonyCount;
        }
    }

    result.voxels = std::move(builder.data());
    for (const uint8_t id : result.voxels)
    {
        if (id == 0)
        {
            continue;
        }
        ++result.stats.occupiedVoxelCount;
        if (idInBand(id, palette.grassBase, palette.grassCount))
        {
            ++result.stats.grassVoxelCount;
        }
        else if (idInBand(id, palette.leafBase, palette.leafCount))
        {
            ++result.stats.leafVoxelCount;
        }
        else if (idInBand(id, palette.reedBase, palette.reedCount))
        {
            ++result.stats.reedVoxelCount;
        }
        else if (idInBand(id, palette.accentBase, palette.accentCount))
        {
            ++result.stats.accentVoxelCount;
        }
        else if (idInBand(id, palette.flowerBase, palette.flowerCount))
        {
            ++result.stats.flowerVoxelCount;
        }
    }
    return result;
}

} // namespace engine::scene
