#pragma once

#include <cmath>
#include <cstdint>

namespace CloudNoise
{

// ============================================================================
// core noise functions for cloud generation
// ============================================================================

// fast hash function for noise generation
inline uint32_t hash(int x, int y, int z, int seed)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u;
    h ^= static_cast<uint32_t>(y) * 668265263u;
    h ^= static_cast<uint32_t>(z) * 2147483647u;
    h ^= static_cast<uint32_t>(seed) * 1013904223u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// hash to float [0, 1]
inline float hashFloat(int x, int y, int z, int seed)
{
    return static_cast<float>(hash(x, y, z, seed)) / 4294967295.0f;
}

// smoothstep interpolation
inline float smoothstep(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

// quintic smoothstep for smoother gradients
inline float quintic(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// linear interpolation
inline float lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

// ============================================================================
// 3D value noise
// ============================================================================

inline float valueNoise3D(float x, float y, float z, int seed)
{
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    int iz = static_cast<int>(std::floor(z));

    float fx = x - static_cast<float>(ix);
    float fy = y - static_cast<float>(iy);
    float fz = z - static_cast<float>(iz);

    // quintic interpolation for smoother results
    float u = quintic(fx);
    float v = quintic(fy);
    float w = quintic(fz);

    // hash corners
    float c000 = hashFloat(ix, iy, iz, seed);
    float c100 = hashFloat(ix + 1, iy, iz, seed);
    float c010 = hashFloat(ix, iy + 1, iz, seed);
    float c110 = hashFloat(ix + 1, iy + 1, iz, seed);
    float c001 = hashFloat(ix, iy, iz + 1, seed);
    float c101 = hashFloat(ix + 1, iy, iz + 1, seed);
    float c011 = hashFloat(ix, iy + 1, iz + 1, seed);
    float c111 = hashFloat(ix + 1, iy + 1, iz + 1, seed);

    // trilinear interpolation
    float x00 = lerp(c000, c100, u);
    float x10 = lerp(c010, c110, u);
    float x01 = lerp(c001, c101, u);
    float x11 = lerp(c011, c111, u);

    float xy0 = lerp(x00, x10, v);
    float xy1 = lerp(x01, x11, v);

    return lerp(xy0, xy1, w);
}

inline float repeatFrac(float value, float repeat)
{
    if (repeat <= 0.0001f)
    {
        return 0.0f;
    }

    float wrapped = std::fmod(value, repeat);
    if (wrapped < 0.0f)
    {
        wrapped += repeat;
    }
    return wrapped / repeat;
}

// tile the noise seamlessly across X/Z so wrapped cloud volumes do not form seams.
inline float valueNoise3DTiledXZ(float x, float y, float z, float repeatX,
                                 float repeatZ, int seed)
{
    if (repeatX <= 0.0001f || repeatZ <= 0.0001f)
    {
        return valueNoise3D(x, y, z, seed);
    }

    float tx = repeatFrac(x, repeatX);
    float tz = repeatFrac(z, repeatZ);

    float n00 = valueNoise3D(x, y, z, seed);
    float n10 = valueNoise3D(x - repeatX, y, z, seed);
    float n01 = valueNoise3D(x, y, z - repeatZ, seed);
    float n11 = valueNoise3D(x - repeatX, y, z - repeatZ, seed);

    float nx0 = lerp(n00, n10, tx);
    float nx1 = lerp(n01, n11, tx);
    return lerp(nx0, nx1, tz);
}

// ============================================================================
// fractal brownian motion (fbm)
// ============================================================================

// multi-octave noise for natural cloud shapes
// octaves: number of noise layers (4-6 for clouds)
// lacunarity: frequency multiplier per octave (typically 2.0)
// persistence: amplitude multiplier per octave (typically 0.5)
inline float fbm3D(float x, float y, float z, int octaves, float lacunarity,
                   float persistence, int seed)
{
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i)
    {
        total += valueNoise3D(x * frequency, y * frequency, z * frequency, seed + i) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / maxValue;  // normalize to [0, 1]
}

inline float fbm3DTiledXZ(float x, float y, float z, int octaves, float lacunarity,
                          float persistence, int seed, float repeatX, float repeatZ)
{
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i)
    {
        total += valueNoise3DTiledXZ(x * frequency, y * frequency, z * frequency,
                                     repeatX * frequency, repeatZ * frequency, seed + i) *
                 amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / maxValue;
}

// ============================================================================
// billow noise (for puffy cloud edges)
// ============================================================================

// creates ridged, billowy patterns by using absolute value
inline float billowNoise3D(float x, float y, float z, int octaves,
                           float lacunarity, float persistence, int seed)
{
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i)
    {
        // absolute value creates billowy ridges
        float noise = std::abs(valueNoise3D(x * frequency, y * frequency, z * frequency, seed + i) * 2.0f - 1.0f);
        total += noise * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / maxValue;
}

inline float billowNoise3DTiledXZ(float x, float y, float z, int octaves,
                                  float lacunarity, float persistence, int seed,
                                  float repeatX, float repeatZ)
{
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i)
    {
        float noise = std::abs(valueNoise3DTiledXZ(x * frequency, y * frequency, z * frequency,
                                                   repeatX * frequency, repeatZ * frequency,
                                                   seed + i) *
                                   2.0f -
                               1.0f);
        total += noise * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / maxValue;
}

// ============================================================================
// cloud density functions
// ============================================================================

// combined cloud density function
// returns 0.0-1.0 density value suitable for voxel placement
inline float cloudDensity(float x, float y, float z, int seed,
                          float baseScale, float detailScale,
                          float cloudMinY, float cloudHeight,
                          float threshold = 0.35f,
                          float repeatX = 0.0f, float repeatZ = 0.0f)
{
    // large-scale cloud shape
    float base = fbm3DTiledXZ(x / baseScale, y / baseScale, z / baseScale,
                              4, 2.0f, 0.5f, seed,
                              repeatX / baseScale, repeatZ / baseScale);

    // detail noise for fluffy edges
    float detail = billowNoise3DTiledXZ(x / detailScale, y / detailScale, z / detailScale,
                                        3, 2.0f, 0.45f, seed + 100,
                                        repeatX / detailScale, repeatZ / detailScale);

    // vertical profile - clouds thinner at top and bottom
    float yNorm = (y - cloudMinY) / std::max(cloudHeight, 1.0f);
    yNorm = std::max(0.0f, std::min(1.0f, yNorm));
    // parabolic falloff: thick in middle, thin at edges
    float verticalFalloff = 1.0f - 4.0f * (yNorm - 0.5f) * (yNorm - 0.5f);
    verticalFalloff = std::max(0.0f, verticalFalloff);

    // combine: base shape modulated by detail, with vertical profile
    float density = (base * 0.7f + detail * 0.3f) * verticalFalloff;

    // threshold and normalize
    if (density < threshold)
    {
        return 0.0f;
    }
    return (density - threshold) / (1.0f - threshold);
}

// ============================================================================
// cloud material selection
// ============================================================================

// cloud material ids (defined in VoxelPalette)
namespace CloudMaterial
{
    constexpr uint8_t Base = 100;
    constexpr uint8_t Count = 16;

    // sub-ranges for different cloud features
    constexpr uint8_t CoreWhite = 100;      // bright white cores (3 variants)
    constexpr uint8_t MidWhite = 103;       // mid-white body (4 variants)
    constexpr uint8_t EdgeGray = 107;       // gray edges/shadows (4 variants)
    constexpr uint8_t WispyLight = 111;     // translucent wisps (3 variants)
    constexpr uint8_t DarkUnderbelly = 114; // dark undersides (2 variants)
}

// select cloud material based on density and position
// density: 0.0-1.0 cloud density at this voxel
// normalizedY: 0.0-1.0 vertical position within cloud (0=bottom, 1=top)
// edgeDist: approximate distance from cloud surface
inline uint8_t cloudMaterialIndex(float density, float normalizedY, float edgeDist,
                                   int x, int y, int z)
{
    // use position hash for variation
    uint32_t h = hash(x, y, z, 12345);
    int variation = static_cast<int>(h % 4);

    // edge wisps (translucent outer layer)
    if (edgeDist < 1.5f || density < 0.15f)
    {
        return CloudMaterial::WispyLight + static_cast<uint8_t>(h % 3);
    }

    // underbelly (darker at bottom)
    if (normalizedY < 0.25f && density > 0.4f)
    {
        return CloudMaterial::DarkUnderbelly + static_cast<uint8_t>(h % 2);
    }

    // core (bright white for high density)
    if (density > 0.75f)
    {
        return CloudMaterial::CoreWhite + static_cast<uint8_t>(h % 3);
    }

    // edge shadows (medium-low density)
    if (density < 0.4f)
    {
        return CloudMaterial::EdgeGray + static_cast<uint8_t>(h % 4);
    }

    // mid-body (default)
    return CloudMaterial::MidWhite + static_cast<uint8_t>(h % 4);
}

// estimate edge distance by checking density gradient
inline float estimateEdgeDistance(float centerDensity, float neighborDensity)
{
    float gradient = std::abs(centerDensity - neighborDensity);
    // higher gradient = closer to edge
    return gradient > 0.1f ? 1.0f : 3.0f;
}

} // namespace CloudNoise
