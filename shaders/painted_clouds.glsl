#ifndef PAINTED_CLOUDS_GLSL
#define PAINTED_CLOUDS_GLSL

// cloud-001 owns exactly three value-noise octaves. it performs no raymarch,
// texture lookup, shadow sample, or loop with a scene-controlled bound.
const float PAINTED_CLOUD_FACETS_PER_WORLD_SCALE = 32.0;

float paintedCloudHash(vec2 cell, float seed)
{
    return fract(sin(dot(cell, vec2(127.1, 311.7)) + seed * 0.013) *
                 43758.5453);
}

float paintedCloudValueNoise(vec2 point, float seed)
{
    vec2 cell = floor(point);
    vec2 local = fract(point);
    local = local * local * (3.0 - 2.0 * local);
    float a = paintedCloudHash(cell, seed);
    float b = paintedCloudHash(cell + vec2(1.0, 0.0), seed);
    float c = paintedCloudHash(cell + vec2(0.0, 1.0), seed);
    float d = paintedCloudHash(cell + vec2(1.0), seed);
    return mix(mix(a, b, local.x), mix(c, d, local.x), local.y);
}

vec2 evaluateFixedPaintedCloudNoise(vec2 point, float seed,
                                    float detailStrength)
{
    float octave0 = paintedCloudValueNoise(point, seed);
    point = vec2(0.80 * point.x + 0.60 * point.y,
                 -0.60 * point.x + 0.80 * point.y) * 2.03 +
            vec2(7.13, -3.71);
    float octave1 = paintedCloudValueNoise(point, seed + 19.0);
    point = vec2(0.80 * point.x + 0.60 * point.y,
                 -0.60 * point.x + 0.80 * point.y) * 2.01 +
            vec2(-5.37, 11.21);
    float octave2 = paintedCloudValueNoise(point, seed + 47.0);
    float baseShape = octave0 * 0.72 + octave1 * 0.20 + octave2 * 0.08;
    float shape = baseShape +
                  (octave1 - 0.5) * detailStrength * 0.20 +
                  (octave2 - 0.5) * detailStrength * 0.10;
    return vec2(clamp(shape, 0.0, 1.0), octave1);
}

vec3 evaluatePaintedClouds(
    vec3 skyColor, vec3 cameraPosition, vec3 viewDirection,
    vec3 directionToSun, vec4 paintedCloud0, vec4 paintedCloud1,
    vec4 paintedCloud2, vec4 paintedCloud3, vec4 paintedCloud4)
{
    float strength = clamp(paintedCloud0.w, 0.0, 1.0);
    if (strength <= 0.0)
    {
        return skyColor;
    }

    vec3 view = normalize(viewDirection);
    float horizonStart = clamp(paintedCloud3.z, 0.0, 0.80);
    float horizonEnd = max(paintedCloud3.w, horizonStart + 0.005);
    if (view.y <= horizonStart)
    {
        return skyColor;
    }

    float altitude = max(paintedCloud2.x, 10.0);
    float worldScale = max(paintedCloud2.y, 16.0);
    float layerHeight = altitude - cameraPosition.y;
    if (layerHeight <= 1.0)
    {
        return skyColor;
    }
    float travel = layerHeight / max(view.y, 0.02);
    vec2 worldPoint = cameraPosition.xz + view.xz * travel;
    vec2 windOffset = paintedCloud4.xy * paintedCloud4.z;
    vec2 noisePoint = (worldPoint - windOffset) / worldScale;
    // snap the existing analytic domain instead of adding samples or geometry.
    // this gives the painted sheet a restrained voxel/facet character while
    // remaining world stable and preserving the fixed twelve-hash budget.
    noisePoint = floor(noisePoint * PAINTED_CLOUD_FACETS_PER_WORLD_SCALE +
                       vec2(0.5)) /
                 PAINTED_CLOUD_FACETS_PER_WORLD_SCALE;
    float detailStrength = clamp(paintedCloud3.x, 0.0, 1.0);
    vec2 noise = evaluateFixedPaintedCloudNoise(
        noisePoint, paintedCloud4.w, detailStrength);

    float coverage = clamp(paintedCloud1.w, 0.0, 1.0);
    float threshold = mix(0.74, 0.30, coverage);
    float softness = clamp(paintedCloud2.w, 0.01, 0.30);
    float mask = smoothstep(threshold - softness,
                            threshold + softness, noise.x);
    float horizon = smoothstep(horizonStart, horizonEnd, view.y);
    float opacity = clamp(mask * clamp(paintedCloud2.z, 0.0, 1.0) *
                          strength * horizon, 0.0, 1.0);

    vec3 sun = normalize(directionToSun);
    float bodyLight = clamp(0.46 + 0.34 * noise.x +
                            0.12 * (noise.y - 0.5) +
                            0.08 * max(sun.y, 0.0), 0.0, 1.0);
    vec3 lightTint = max(paintedCloud0.xyz, vec3(0.0));
    vec3 shadowTint = max(paintedCloud1.xyz, vec3(0.0));
    vec3 cloudColor = mix(shadowTint, lightTint, bodyLight);
    float edge = 4.0 * mask * (1.0 - mask);
    float sunFacing = pow(max(dot(view, sun), 0.0), 8.0);
    cloudColor += lightTint * max(paintedCloud3.y, 0.0) * edge * sunFacing;
    return mix(max(skyColor, vec3(0.0)), cloudColor, opacity);
}

#endif
