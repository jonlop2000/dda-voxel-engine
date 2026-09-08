#pragma once

#include <cstdint>
#include <cmath>

inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

inline float rand01(int x, int z, uint32_t seed)
{
    const uint32_t hx = static_cast<uint32_t>(x) * 73856093u;
    const uint32_t hz = static_cast<uint32_t>(z) * 19349663u;
    const uint32_t h = hash32(hx ^ hz ^ seed);
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline float smoothstep(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

inline float valueNoise2D(int x, int z, int scale, uint32_t seed)
{
    if (scale <= 0)
    {
        return 0.0f;
    }

    const float fx = static_cast<float>(x) / static_cast<float>(scale);
    const float fz = static_cast<float>(z) / static_cast<float>(scale);

    const int x0 = static_cast<int>(std::floor(fx));
    const int z0 = static_cast<int>(std::floor(fz));
    const int x1 = x0 + 1;
    const int z1 = z0 + 1;

    const float tx = smoothstep(fx - static_cast<float>(x0));
    const float tz = smoothstep(fz - static_cast<float>(z0));

    const float v00 = rand01(x0, z0, seed);
    const float v10 = rand01(x1, z0, seed);
    const float v01 = rand01(x0, z1, seed);
    const float v11 = rand01(x1, z1, seed);

    const float vx0 = lerp(v00, v10, tx);
    const float vx1 = lerp(v01, v11, tx);
    return lerp(vx0, vx1, tz);
}
