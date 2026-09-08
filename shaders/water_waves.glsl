#ifndef WATER_WAVES_GLSL
#define WATER_WAVES_GLSL

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

vec2 waveDomainWarp(vec2 p, float time)
{
    float w0 = valueNoise(p * 0.17 + vec2(time * 0.045, -time * 0.031));
    float w1 = valueNoise(p * 0.11 + vec2(-time * 0.028, time * 0.039) + vec2(13.2, 7.1));
    return (vec2(w0, w1) - 0.5) * 1.10;
}

float waveEnvelope(vec2 p, float time)
{
    float n = valueNoise(p * 0.09 + vec2(time * 0.015, -time * 0.012));
    return mix(0.82, 1.14, n);
}

void accumulateWave(vec2 p, vec2 direction, float frequency, float speed, float time, float amplitude,
                    inout float h, inout vec2 slope)
{
    vec2 dir = normalize(direction);
    float freqJitter =
        1.0 + 0.14 * sin(dot(p, dir * 0.37 + vec2(0.21, 0.13)) - time * (0.08 + speed * 0.02));
    float phaseJitter = sin(dot(p, dir.yx * vec2(0.73, -0.49)) + time * (0.17 + speed * 0.08)) *
                        0.65;
    float localFreq = frequency * freqJitter;
    float phase = dot(p, dir) * localFreq + time * speed + phaseJitter;
    float s = sin(phase);
    float c = cos(phase);
    h += s * amplitude;
    slope += dir * (localFreq * c * amplitude);
}

float waveHeight(vec2 worldXZ, float time, float waveScale, float waveAmplitude)
{
    vec2 p = worldXZ * max(waveScale, 0.001);
    vec2 warpedP = p + waveDomainWarp(p, time);
    float envelope = waveEnvelope(warpedP, time);

    float h = 0.0;
    vec2 slope = vec2(0.0);

    // keep stylized readability but break regular tiling with mild non-uniform modulation.
    accumulateWave(warpedP, vec2(1.0, 0.2), 0.65, 0.30, time, 0.50 * envelope, h, slope);
    accumulateWave(warpedP, vec2(-0.4, 0.9), 1.05, 0.48, time, 0.30 * envelope, h, slope);
    accumulateWave(warpedP, vec2(0.75, -0.55), 1.55, 0.72, time, 0.14 * envelope, h, slope);
    accumulateWave(warpedP, vec2(-0.8, -0.35), 2.20, 0.95, time, 0.06 * envelope, h, slope);

    vec2 swirlDir = normalize(vec2(sin(time * 0.07 + 0.9), cos(time * 0.09 - 0.4)));
    accumulateWave(warpedP + waveDomainWarp(warpedP * 0.62, time * 0.6) * 0.35, swirlDir, 0.92,
                   0.40, time, 0.10 * envelope, h, slope);

    return h * waveAmplitude;
}

vec3 computeWaveNormal(vec2 worldXZ, float time, float waveScale, float waveAmplitude)
{
    if (waveAmplitude <= 1e-4 || waveScale <= 1e-4)
    {
        return vec3(0.0, 1.0, 0.0);
    }

    vec2 p = worldXZ * max(waveScale, 0.001);
    vec2 warpedP = p + waveDomainWarp(p, time);
    float envelope = waveEnvelope(warpedP, time);

    float h = 0.0;
    vec2 slope = vec2(0.0);
    accumulateWave(warpedP, vec2(1.0, 0.2), 0.65, 0.30, time, 0.50 * envelope, h, slope);
    accumulateWave(warpedP, vec2(-0.4, 0.9), 1.05, 0.48, time, 0.30 * envelope, h, slope);
    accumulateWave(warpedP, vec2(0.75, -0.55), 1.55, 0.72, time, 0.14 * envelope, h, slope);
    accumulateWave(warpedP, vec2(-0.8, -0.35), 2.20, 0.95, time, 0.06 * envelope, h, slope);

    vec2 swirlDir = normalize(vec2(sin(time * 0.07 + 0.9), cos(time * 0.09 - 0.4)));
    accumulateWave(warpedP + waveDomainWarp(warpedP * 0.62, time * 0.6) * 0.35, swirlDir, 0.92,
                   0.40, time, 0.10 * envelope, h, slope);

    vec2 worldSlope = slope * (waveScale * waveAmplitude);
    return normalize(vec3(-worldSlope.x, 1.0, -worldSlope.y));
}

vec3 computeDetailWaveNormal(vec2 worldXZ, float time, float waveScale, float waveAmplitude)
{
    if (waveAmplitude <= 1e-4)
    {
        return vec3(0.0, 1.0, 0.0);
    }

    float detailScale = max(waveScale * 4.5 + 0.08, 0.10);
    vec2 p = worldXZ * detailScale;
    vec2 warpedP =
        p + waveDomainWarp(p * 1.6 + vec2(9.7, -4.3), time * 1.35) * 0.60;
    float envelope = mix(0.72, 1.18,
                         valueNoise(warpedP * 0.23 + vec2(time * 0.025, -time * 0.019)));

    float h = 0.0;
    vec2 slope = vec2(0.0);
    accumulateWave(warpedP, vec2(0.95, 0.30), 2.80, 1.05, time, 0.075 * envelope, h, slope);
    accumulateWave(warpedP, vec2(-0.38, 0.92), 4.10, 1.48, time, 0.048 * envelope, h, slope);
    accumulateWave(warpedP, vec2(0.74, -0.61), 6.20, 2.05, time, 0.026 * envelope, h, slope);

    vec2 worldSlope = slope * (0.35 + waveAmplitude * 0.75);
    return normalize(vec3(-worldSlope.x, 1.0, -worldSlope.y));
}

#endif
