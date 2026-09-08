#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace SpatialHash
{

// fast deterministic hash - same position always returns same value
inline uint32_t hash3D(int x, int y, int z)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u;
    h ^= static_cast<uint32_t>(y) * 668265263u;
    h ^= static_cast<uint32_t>(z) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// pick index within palette band (uniform noise - legacy)
inline uint8_t bandIndex(uint8_t base, uint8_t count, int x, int y, int z)
{
    return base + static_cast<uint8_t>(hash3D(x, y, z) % count);
}

// ============================================================================
// directional pattern functions (teardown-style material variation)
// ============================================================================

// wood grain - horizontal streaks (quantize y to create bands, vary by x+z)
// creates horizontal wood grain patterns like floorboards or planks
inline uint8_t woodGrainIndex(uint8_t base, uint8_t count, int x, int y, int z)
{
    // quantize y to create horizontal bands (every 2-3 voxels share a band)
    int yBand = y / 3;
    // primary variation from x+z position, secondary from y band
    uint32_t h = hash3D(x, yBand, z);
    return base + static_cast<uint8_t>(h % count);
}

// gravel depth layers - darker at bottom, lighter at top
// yMin/yMax define the depth range for proper gradient calculation
inline uint8_t gravelDepthIndex(uint8_t base, uint8_t count, int x, int y, int z,
                                 int yMin, int yMax)
{
    // calculate depth factor (0 at bottom, 1 at top)
    float depthT = static_cast<float>(y - yMin) /
                   static_cast<float>(std::max(1, yMax - yMin));
    // bias toward darker (lower indices) at bottom, lighter at top
    int biasedIndex = static_cast<int>(depthT * (count - 1) + 0.5f);
    // add small noise to break uniformity (±1 index variation)
    uint32_t noise = hash3D(x, y, z) % 3;
    int finalIndex = std::clamp(biasedIndex + static_cast<int>(noise) - 1, 0, count - 1);
    return base + static_cast<uint8_t>(finalIndex);
}

// Coral/plant gradient - base-to-tip coloring (darker at base, brighter at tips)
// centerX/Y/Z define the base/center point, maxDist is the maximum distance
inline uint8_t gradientIndex(uint8_t base, uint8_t count, int x, int y, int z,
                              float centerX, float centerY, float centerZ, float maxDist)
{
    float dx = static_cast<float>(x) - centerX;
    float dy = static_cast<float>(y) - centerY;
    float dz = static_cast<float>(z) - centerZ;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    float t = std::min(dist / std::max(maxDist, 0.001f), 1.0f);
    int index = static_cast<int>(t * (count - 1) + 0.5f);
    // add slight noise for natural variation
    uint32_t noise = hash3D(x, y, z) % 2;
    int finalIndex = std::clamp(index + static_cast<int>(noise), 0, count - 1);
    return base + static_cast<uint8_t>(finalIndex);
}

// vertical gradient - darker at bottom, lighter at top (for plants/coral stems)
// yMin is the base y coordinate, height is the total height
inline uint8_t verticalGradientIndex(uint8_t base, uint8_t count, int x, int y, int z,
                                      int yMin, int height)
{
    float t = static_cast<float>(y - yMin) / static_cast<float>(std::max(1, height));
    t = std::min(std::max(t, 0.0f), 1.0f);
    int index = static_cast<int>(t * (count - 1) + 0.5f);
    // small noise for natural look
    uint32_t noise = hash3D(x, y, z) % 2;
    int finalIndex = std::clamp(index + static_cast<int>(noise), 0, count - 1);
    return base + static_cast<uint8_t>(finalIndex);
}

// stone weathering - clustered patches with large-scale and fine-scale variation
// creates natural-looking weathering patterns on stone surfaces
inline uint8_t stoneWeatherIndex(uint8_t base, uint8_t count, int x, int y, int z)
{
    // large-scale variation (4x4x4 cell clustering)
    int cx = x / 4;
    int cy = y / 4;
    int cz = z / 4;
    uint32_t cellHash = hash3D(cx, cy, cz);
    int cellBias = static_cast<int>(cellHash % 4) - 1;  // -1 to 2 bias per cell

    // fine-scale noise within cells
    uint32_t fineHash = hash3D(x, y, z);
    int baseIndex = static_cast<int>(fineHash % count);
    int finalIndex = std::clamp(baseIndex + cellBias, 0, count - 1);
    return base + static_cast<uint8_t>(finalIndex);
}

} // namespace SpatialHash
