#version 450
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 vWorldPos;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uRefractionSource;
layout(set = 0, binding = 1) uniform sampler2D uSceneDepth;
layout(set = 0, binding = 2) uniform sampler2D uWaterDist;
layout(set = 0, binding = 3) uniform sampler2D uNoiseTex;
layout(set = 0, binding = 4) uniform sampler2D uPlanarReflection;

layout(set = 1, binding = 0) uniform UBO {
    mat4 view;
    mat4 viewProj;
    mat4 invProj;
    mat4 reflectedViewProj;
    vec4 camPos;
    vec4 sunDirToSun;
    vec4 sunColor;
    vec4 waterTint;
    vec4 params0;  // x=waterLevel, y=time, z=waveScale, w=waveAmplitude
    vec4 params1;  // x=refractionStrength, y=specularPower, z=specularIntensity, w=fresnelBias/fresnelF0
    vec4 params2;  // x=crestThreshold, y=crestSoftness, z=crestIntensity, w=distortionDepthScale
    vec4 params3;  // x=edgeFadeDepth, y=bandHardness, z=reflectionStrength, w=gradientStrength
    vec4 params4;  // x=debugMode, y=waterVolumeCount, z=styleMode, w=planarReflectionStrength
    vec4 v2Optics0;     // x=surfacePathCap, y=surfaceAbsorptionScale, z=surfaceFogPathScale, w=surfaceFogMixScale
    vec4 v2Optics1;     // x=surfaceFogMixMax, y=refractionDepthScale, z/w=reserved
    vec4 v2Body0;       // x=bodyBase, y=bodyPathRate, z=bodyPathWeight, w=bodyViewWeight
    vec4 v2Body1;       // x=bodyMax, y=transmissionBase, z=transmissionTintScale, w=transmissionTintMix
    vec4 v2Body2;       // x=postReflectionDamping, y=postReflectionBodyMix, z=specularScale, w=rippleStrength
    vec4 v2Reflection0; // x=localUvOffset, y=localBase, z=localFacingWeight, w=localPathWeight
    vec4 v2Reflection1; // x=localPathMax, y=localWeightMin, z=localWeightMax, w=fallbackSkyLeak
    vec4 v2Planar0;     // x=distortionMin, y=distortionMax, z=guardBand, w=borderFadeWidth
    vec4 v2Planar1;     // x=viewGateMin, y=viewGateStart, z=viewGateEnd, w=planarEnvBase
    vec4 v2Planar2;     // x=planarEnvGrazing, y=reflectionGrazingBoost, z=reflectionPlanarBoost, w=reflectionMixMax
    vec4 v2Contact0;    // x=sideContactStart, y=sideContactEnd, z=waterlineStart, w=waterlineEnd
    vec4 v2Contact1;    // x=depthTintMix, y=depthBlend, z=contactBase, w=contactView
    vec4 v2Contact2;    // x=glintStrength, y=sideOpacity, z=waterlineOpacity, w=waterlineFresnelBoost
    vec4 v2Opacity0;    // x=opacityMin, y=opacityMax, z=pathOpacityScale, w=pathOpacityMax
    vec4 v2Opacity1;    // x=bodyOpacity, y=waveOpacity, z=alphaMin, w=alphaMax
    vec4 v2Vfx0;        // x=enabled, y=density, z=drift, w=scale
    vec4 v2Vfx1;        // x=foamEnabled, y=foamIntensity, z=foamRadius, w=foamRise
    vec4 v2Vfx2;        // x=foamCenterX, y=foamCenterZ, z=foamScale, w=foamSpread
    vec4 v2BodyDetail0; // x=strength, y=pathRate, z=scatterStrength, w=noiseStrength
    vec4 atmosphere0;   // x=density, y=heightFalloff, z=baseHeight, w=sunPhaseStrength
} u;

struct WaterVolumeGPU
{
    vec4 boundsMin;
    vec4 boundsMax;
    vec4 absorptionCoeff;
    vec4 deepColor;
};

layout(std430, set = 2, binding = 0) readonly buffer WaterBuf {
    WaterVolumeGPU waterVolumes[];
};

#define SCENE_ATMOSPHERE_WATER_VOLUME_COUNT int(u.params4.y + 0.5)
#define SCENE_ATMOSPHERE_WATER_VOLUME(INDEX) waterVolumes[INDEX]
#include "atmosphere_water_path.glsl"
#undef SCENE_ATMOSPHERE_WATER_VOLUME
#undef SCENE_ATMOSPHERE_WATER_VOLUME_COUNT
#include "water_waves.glsl"

float eyeDepth(vec2 uv, float depth01)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth01, 1.0);
    vec4 viewPos = u.invProj * ndc;
    return -viewPos.z / max(viewPos.w, 1e-6);
}

float schlickFresnel(float cosTheta, float f0)
{
    float clamped = clamp(f0, 0.0, 1.0);
    return clamped + (1.0 - clamped) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}

float waterV2BandedRamp(float value, float hardness)
{
    float t = clamp(value, 0.0, 1.0);
    float h = clamp(hardness, 0.0, 1.0);
    if (h <= 0.001)
    {
        return t;
    }

    float bands = mix(18.0, 5.0, h);
    float invBands = 1.0 / bands;
    float scaled = t * bands;
    float lower = floor(scaled) * invBands;
    float upper = min(lower + invBands, 1.0);
    float local = fract(scaled);
    float edge = mix(0.48, 0.045, h);
    float banded = mix(lower, upper, smoothstep(0.5 - edge, 0.5 + edge, local));
    return mix(t, banded, h);
}

float waterV2HardenMask(float value, float hardness)
{
    float t = clamp(value, 0.0, 1.0);
    float h = clamp(hardness, 0.0, 1.0);
    float edge = mix(0.34, 0.065, h);
    float hardened = smoothstep(0.5 - edge, 0.5 + edge, t);
    return mix(t, hardened, h);
}

vec3 sampleSkyReflection(vec3 reflectDir)
{
    float y = reflectDir.y;
    vec3 horizonColor = vec3(0.34, 0.46, 0.68);
    vec3 zenithColor = vec3(0.14, 0.30, 0.61);
    vec3 nadirColor = vec3(0.08, 0.12, 0.20);

    vec3 sky = (y > 0.0) ? mix(horizonColor, zenithColor, pow(y, 0.5))
                         : mix(horizonColor, nadirColor, pow(-y, 0.8));

    vec3 sunDir = normalize(u.sunDirToSun.xyz);
    float sunDot = max(dot(reflectDir, sunDir), 0.0);
    float sunDisc = pow(sunDot, 320.0) * 10.0;
    float sunGlow = pow(sunDot, 20.0) * 0.55;
    sky += u.sunColor.rgb * (sunDisc + sunGlow);
    return sky;
}

vec3 sampleContainedEnvironment(vec3 reflectDir, vec3 bodyTint, vec3 deepColor)
{
    vec3 sky = sampleSkyReflection(reflectDir);
    float upFacing = smoothstep(-0.20, 0.65, reflectDir.y);
    vec3 ceilingTint = vec3(0.50, 0.65, 0.72) * (0.72 + u.sunColor.rgb * 0.16);
    vec3 roomTint = max(deepColor * vec3(0.85, 1.18, 1.28), vec3(0.025, 0.10, 0.13));
    vec3 waterRoomTint = max(bodyTint * vec3(0.55, 0.72, 0.82), vec3(0.020, 0.12, 0.15));
    vec3 contained = mix(mix(roomTint, waterRoomTint, 0.45), ceilingTint, upFacing);

    vec3 sunDir = normalize(u.sunDirToSun.xyz);
    float sunDot = max(dot(reflectDir, sunDir), 0.0);
    contained += u.sunColor.rgb * pow(sunDot, 90.0) * 0.55;
    contained += u.sunColor.rgb * pow(sunDot, 16.0) * 0.06;

    // keep the fallback cheap and shape-agnostic. bias it toward an indoor ceiling/room
    // read; open-sky reflection should come primarily from the planar source.
    float skyLeak = smoothstep(0.82, 1.0, reflectDir.y) * max(u.v2Reflection1.w, 0.0);
    return mix(contained, sky, skyLeak);
}

float hash13(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.11369, 0.13787));
    p += vec3(dot(p, p.yzx + vec3(19.19)));
    return fract((p.x + p.y) * p.z);
}

vec3 hash33(vec3 p)
{
    return vec3(hash13(p + vec3(17.0, 59.4, 15.0)),
                hash13(p + vec3(41.0, 11.0, 83.0)),
                hash13(p + vec3(23.0, 73.0, 29.0)));
}

bool waterV2SampleInsideVolume(int shape, vec3 p, vec3 bMin, vec3 bMax, float surfaceHeight)
{
    if (shape == WATER_VOLUME_SHAPE_FISHBOWL)
    {
        return waterVolumeContainsPoint(shape, p, bMin, bMax, surfaceHeight);
    }

    return !(any(lessThan(p, bMin)) ||
             any(greaterThan(p, vec3(bMax.x, surfaceHeight + 0.04, bMax.z))));
}

float waterV2SampleBoundaryClearance(int shape, vec3 p, vec3 bMin, vec3 bMax,
                                     float surfaceHeight)
{
    if (shape == WATER_VOLUME_SHAPE_FISHBOWL)
    {
        return max(waterVolumeLateralBoundaryDistance(shape, p, bMin, bMax, surfaceHeight),
                   0.0);
    }

    return min(min(p.x - bMin.x, bMax.x - p.x), min(p.z - bMin.z, bMax.z - p.z));
}

float voxelWaterVfxMask(vec3 surfaceWorldPos, vec3 viewDir, float surfacePath, vec3 bMin,
                        vec3 bMax, float surfaceHeight, int shape, float time)
{
    if (u.v2Vfx0.x < 0.5 || surfacePath <= 0.08)
    {
        return 0.0;
    }

    float density = clamp(u.v2Vfx0.y, 0.0, 1.0);
    float drift = clamp(u.v2Vfx0.z, 0.0, 1.0);
    float scale = clamp(u.v2Vfx0.w, 0.0, 1.0);
    if (density <= 0.001)
    {
        return 0.0;
    }

    vec3 intoWater = -normalize(viewDir);
    float tracePath = min(surfacePath, max(u.v2Optics0.x, 0.001));
    float cellSize = mix(1.70, 0.66, density);
    float particleSize = mix(0.042, 0.115, scale);
    float occupancyThreshold = mix(0.986, 0.928, density);
    vec3 margin = vec3(0.035);
    float mask = 0.0;

    for (int i = 0; i < 5; ++i)
    {
        float sampleT = (float(i) + 0.42) / 5.0;
        float sampleDist = sampleT * tracePath;
        vec3 p = surfaceWorldPos + intoWater * sampleDist;
        if (!waterV2SampleInsideVolume(shape, p, bMin - margin, bMax + margin,
                                       surfaceHeight))
        {
            continue;
        }

        vec3 driftOffset =
            vec3(time * 0.045, sin(time * 0.23 + p.z * 0.31) * 0.12, -time * 0.028) *
            drift;
        vec3 grid = (p + driftOffset) / cellSize;
        vec3 cell = floor(grid);
        vec3 center = mix(vec3(0.20), vec3(0.80), hash33(cell + vec3(11.0)));
        vec3 local = abs(fract(grid) - center);
        float boxDist = max(local.x, local.z);
        float layerFade = 1.0 - smoothstep(0.30, 0.48, local.y);
        float occupied = step(occupancyThreshold, hash13(cell + vec3(3.0)));
        float cubeMask =
            occupied * layerFade * (1.0 - smoothstep(particleSize, particleSize + 0.055, boxDist));

        float sideClearance =
            waterV2SampleBoundaryClearance(shape, p, bMin, bMax, surfaceHeight);
        float bottomClearance = p.y - bMin.y;
        float boundaryFade = smoothstep(0.04, 0.34, min(sideClearance, bottomClearance));
        float depthFade = smoothstep(0.10, 0.70, sampleDist) * exp(-sampleDist * 0.22);
        float phase = 0.65 + 0.35 * hash13(cell + vec3(37.0));
        mask += cubeMask * boundaryFade * depthFade * phase;
    }

    return clamp(mask * 0.44, 0.0, 1.0);
}

float localizedVoxelFoamSurfaceMask(vec3 surfaceWorldPos, float surfacePath, float time)
{
    if (u.v2Vfx1.x < 0.5 || surfacePath <= 0.04)
    {
        return 0.0;
    }

    float intensity = clamp(u.v2Vfx1.y, 0.0, 1.0);
    float radius = max(u.v2Vfx1.z, 0.05);
    float rise = clamp(u.v2Vfx1.w, 0.0, 1.0);
    float scale = clamp(u.v2Vfx2.z, 0.0, 1.0);
    float spread = clamp(u.v2Vfx2.w, 0.0, 1.0);
    if (intensity <= 0.001)
    {
        return 0.0;
    }

    vec2 center = u.v2Vfx2.xy;
    vec2 rel = surfaceWorldPos.xz - center;
    float dist = length(rel);
    float normDist = dist / radius;
    float depthGate = smoothstep(0.035, 0.35, surfacePath);

    float lobeNoise =
        valueNoise(rel * (0.18 + spread * 0.18) + vec2(time * 0.018, -time * 0.013) + 17.0);
    float lobeBreak = smoothstep(0.28, 0.82, lobeNoise);
    float coreMask = 1.0 - smoothstep(0.06, 0.82, normDist);
    float outerMask = 1.0 - smoothstep(0.62, 1.08, normDist);
    float brokenBand =
        (1.0 - smoothstep(0.035, 0.16 + spread * 0.08,
                          abs(normDist - mix(0.36, 0.58, spread)))) *
        lobeBreak;
    float emitterMask =
        clamp(coreMask * (0.62 + 0.38 * lobeBreak) + outerMask * lobeBreak * 0.32 +
                  brokenBand * 0.20,
              0.0, 1.0) *
        intensity;
    if (emitterMask <= 0.001)
    {
        return 0.0;
    }

    float cellSize = mix(0.22, 0.56, scale);
    vec2 drift = vec2(time * mix(0.015, 0.095, spread),
                      -time * mix(0.011, 0.070, spread));
    vec2 grid = (surfaceWorldPos.xz + drift) / cellSize;
    vec2 cell = floor(grid);
    vec2 jitter = mix(vec2(0.26), vec2(0.74), hash33(vec3(cell, 47.0)).xy);
    vec2 local = abs(fract(grid) - jitter);
    float boxDist = max(local.x, local.y);
    float tileMask = 1.0 - smoothstep(0.18, 0.31 + scale * 0.08, boxDist);
    float cellRand = hash13(vec3(cell, 91.0));
    float localDensity = clamp(emitterMask * (0.68 + 0.32 * lobeBreak), 0.0, 1.0);
    float occupied = step(mix(0.88, 0.42, localDensity), cellRand);
    float fizz = 0.78 + 0.22 * sin(time * (1.4 + spread * 2.6) + cellRand * 6.28318);

    float foam = tileMask * occupied * emitterMask * fizz * depthGate;
    foam += brokenBand * intensity * lobeBreak * depthGate * 0.035;
    foam *= 0.80 + rise * 0.35;
    return clamp(foam, 0.0, 1.0);
}

float localizedVoxelFoamVolumeMask(vec3 surfaceWorldPos, vec3 viewDir, float surfacePath, vec3 bMin,
                                   vec3 bMax, float surfaceHeight, int shape, float time)
{
    if (u.v2Vfx1.x < 0.5 || surfacePath <= 0.05)
    {
        return 0.0;
    }

    float intensity = clamp(u.v2Vfx1.y, 0.0, 1.0);
    float radius = max(u.v2Vfx1.z, 0.05);
    float rise = clamp(u.v2Vfx1.w, 0.0, 1.0);
    float scale = clamp(u.v2Vfx2.z, 0.0, 1.0);
    float spread = clamp(u.v2Vfx2.w, 0.0, 1.0);
    if (intensity <= 0.001)
    {
        return 0.0;
    }

    vec3 intoWater = -normalize(viewDir);
    float tracePath = min(surfacePath, min(max(u.v2Optics0.x, 0.001), radius * 0.75 + 2.0));
    float waterDepth = max(surfaceHeight - bMin.y, 0.05);
    float verticalSpan = min(waterDepth, mix(0.65, 2.40, spread) + radius * 0.10);
    float cellSize = mix(0.18, 0.46, scale);
    float mask = 0.0;

    for (int i = 0; i < 6; ++i)
    {
        float sampleT = (float(i) + 0.35) / 6.0;
        float sampleDist = sampleT * tracePath;
        vec3 p = surfaceWorldPos + intoWater * sampleDist;
        if (!waterV2SampleInsideVolume(shape, p, bMin, bMax, surfaceHeight))
        {
            continue;
        }

        vec2 rel = p.xz - u.v2Vfx2.xy;
        float radialNorm = length(rel) / radius;
        float belowSurface = max(surfaceHeight - p.y, 0.0);
        float heightNorm = belowSurface / max(verticalSpan, 0.001);
        float radialMask = 1.0 - smoothstep(0.10, 1.0, radialNorm);
        float heightMask = smoothstep(0.02, 0.18, heightNorm) * (1.0 - smoothstep(0.72, 1.12, heightNorm));
        float plumeNoise =
            valueNoise(rel * (0.22 + spread * 0.18) + vec2(time * 0.020, time * 0.014) +
                       vec2(23.0, 9.0));
        float plumeBreak = smoothstep(0.24, 0.78, plumeNoise);
        float emitterMask = radialMask * heightMask * mix(0.48, 1.0, plumeBreak) * intensity;
        if (emitterMask <= 0.001)
        {
            continue;
        }

        vec3 drift = vec3(time * mix(0.010, 0.080, spread), -time * mix(0.045, 0.180, rise),
                          time * mix(-0.012, -0.070, spread));
        vec3 grid = (p + drift) / cellSize;
        vec3 cell = floor(grid);
        vec3 jitter = mix(vec3(0.22), vec3(0.78), hash33(cell + vec3(71.0)));
        vec3 local = abs(fract(grid) - jitter);
        float cubeDist = max(local.x, max(local.y, local.z));
        float cubeMask = 1.0 - smoothstep(0.14, 0.29 + scale * 0.08, cubeDist);
        float cellRand = hash13(cell + vec3(137.0));
        float localDensity = clamp(emitterMask * (0.70 + 0.30 * plumeBreak), 0.0, 1.0);
        float occupied = step(mix(0.94, 0.48, localDensity), cellRand);
        float depthFade = exp(-sampleDist * mix(0.18, 0.08, intensity));
        float fizz = 0.72 + 0.28 * sin(time * (1.2 + spread * 2.2) + cellRand * 6.28318);

        mask += cubeMask * occupied * emitterMask * depthFade * fizz;
    }

    return clamp(mask * (0.42 + intensity * 0.28), 0.0, 1.0);
}

float waterBodyDetailMask(vec3 surfaceWorldPos, vec3 viewDir, float surfacePath, vec3 bMin,
                          vec3 bMax, float surfaceHeight, vec3 bodyTint, vec3 deepColor,
                          int shape, float time, out vec3 detailColor)
{
    detailColor = vec3(0.0);
    float strength = max(u.v2BodyDetail0.x, 0.0);
    if (strength <= 0.001 || surfacePath <= 0.08)
    {
        return 0.0;
    }

    vec3 intoWater = -normalize(viewDir);
    float tracePath = min(surfacePath, max(u.v2Optics0.x, 0.001));
    float waterDepth = max(surfaceHeight - bMin.y, 0.05);
    float pathRate = max(u.v2BodyDetail0.y, 0.0);
    float pathMask =
        (1.0 - exp(-tracePath * (0.14 + pathRate * 1.05))) * smoothstep(0.06, 0.68, tracePath);
    float noiseStrength = clamp(u.v2BodyDetail0.w, 0.0, 1.0);
    float scatterStrength = max(u.v2BodyDetail0.z, 0.0);

    float densityAccum = 0.0;
    float variationAccum = 0.0;
    float sampleWeight = 0.0;
    vec3 sampleMean = vec3(0.0);
    float midColumnAccum = 0.0;
    float lowerColumnAccum = 0.0;
    for (int i = 0; i < 5; ++i)
    {
        float t = (float(i) + 0.35) / 5.0;
        float sampleDist = t * tracePath;
        vec3 p = surfaceWorldPos + intoWater * sampleDist;
        if (!waterV2SampleInsideVolume(shape, p, bMin, bMax, surfaceHeight))
        {
            continue;
        }

        float belowSurface = max(surfaceHeight - p.y, 0.0);
        float depthNorm = clamp(belowSurface / waterDepth, 0.0, 1.0);
        float midColumn =
            smoothstep(0.08, 0.24, depthNorm) * (1.0 - smoothstep(0.68, 0.94, depthNorm));
        float lowerColumn = smoothstep(0.46, 0.88, depthNorm);

        vec2 horizP =
            p.xz * 0.090 + vec2(p.y * 0.050 + time * 0.006, -p.y * 0.031 - time * 0.004);
        vec2 columnP = p.xy * 0.145 +
                       vec2(-p.z * 0.038 - time * 0.010, p.z * 0.024 + time * 0.008) +
                       vec2(17.2, 5.7);
        float horizNoise = valueNoise(horizP);
        float columnNoise = valueNoise(columnP);
        float layerWave = 0.5 + 0.5 *
                                    sin(p.y * 2.35 + dot(p.xz, vec2(0.18, -0.14)) +
                                        sampleDist * 0.42 + time * 0.20);
        float structure = mix(mix(horizNoise, columnNoise, 0.44), layerWave, 0.36);
        float structureField = smoothstep(0.24, 0.78, structure);
        float sampleDensity = mix(0.42, 1.12, structureField);
        sampleDensity *= mix(0.78, 1.36, midColumn);
        sampleDensity *= mix(0.92, 1.10, lowerColumn);

        float distT = sampleDist / max(tracePath, 0.001);
        float distWeight = smoothstep(0.05, 0.88, distT);
        float sampleFade = mix(0.62, 1.0, distWeight) * exp(-sampleDist * 0.035);
        float weight = sampleFade * mix(0.74, 1.26, midColumn);

        densityAccum += sampleDensity * weight;
        variationAccum += ((structure - 0.5) * 2.0) * mix(0.70, 1.30, midColumn) * weight;
        sampleWeight += weight;
        sampleMean += p * weight;
        midColumnAccum += midColumn * weight;
        lowerColumnAccum += lowerColumn * weight;
    }

    if (sampleWeight <= 0.001)
    {
        return 0.0;
    }

    float densityField = densityAccum / sampleWeight;
    float variationField = variationAccum / sampleWeight;
    sampleMean /= sampleWeight;
    float midColumnMean = midColumnAccum / sampleWeight;
    float lowerColumnMean = lowerColumnAccum / sampleWeight;
    float variationAbs =
        clamp(abs(variationField) * (1.05 + noiseStrength * 1.10), 0.0, 1.0);
    float variationMask =
        clamp(0.5 + variationField * (0.90 + noiseStrength * 0.95), 0.0, 1.0);
    float densityMod =
        mix(0.84, 1.08, clamp((densityField - 0.56) * (0.72 + noiseStrength * 0.25), 0.0, 1.0));

    float depthT = clamp(tracePath / max(u.v2Optics0.x, 0.001), 0.0, 1.0);
    float bottomDepth = max(surfaceHeight - sampleMean.y, 0.0);
    float bottomMask = smoothstep(0.22, 0.86, bottomDepth / waterDepth);
    float farMask = smoothstep(0.22, 0.92, depthT);
    float columnBias = mix(0.26, 0.52, clamp(midColumnMean * 0.88 + farMask * 0.18, 0.0, 1.0));
    float structureMask =
        clamp(pathMask * densityMod *
                  (columnBias + variationAbs * (0.18 + noiseStrength * 0.18)) *
                  mix(0.94, 1.08, lowerColumnMean * 0.24) * strength,
              0.0, 1.0);

    vec3 sunDir = normalize(u.sunDirToSun.xyz);
    float forwardScatter = pow(max(dot(viewDir, sunDir), 0.0), 4.5) *
                           smoothstep(0.22, 2.4, tracePath) * scatterStrength *
                           mix(0.68, 1.08, midColumnMean);
    vec3 shallowMedium = bodyTint * vec3(0.60, 0.92, 1.06);
    vec3 columnMedium = max(mix(shallowMedium, deepColor * vec3(0.86, 1.22, 1.34),
                                clamp(depthT * 0.62 + midColumnMean * 0.24 +
                                          bottomMask * 0.12,
                                      0.0, 1.0)),
                            bodyTint * 0.50);
    vec3 densityTint = mix(vec3(0.98, 1.00, 1.02), vec3(0.84, 0.95, 1.10),
                           clamp(densityField * 0.46 + variationAbs * 0.24, 0.0, 1.0));
    vec3 variationTint =
        mix(vec3(0.92, 0.97, 1.00), vec3(1.02, 1.04, 1.08), variationMask);
    detailColor = columnMedium * densityTint * variationTint;
    detailColor += u.sunColor.rgb * forwardScatter * vec3(0.76, 0.90, 1.0);

    return clamp(structureMask + forwardScatter * 0.40, 0.0, 1.0);
}

void main()
{
    int waterVolumeCount = int(u.params4.y + 0.5);
    bool hasVolume = waterVolumeCount > 0;
    vec3 bMin = vec3(-1e6);
    vec3 bMax = vec3(1e6);
    float surfaceHeight = u.params0.x;
    vec3 absorption = vec3(0.028, 0.010, 0.006);
    vec3 deepColor = vec3(0.05, 0.19, 0.30);
    float fogDensity = 0.02;
    int volumeShape = WATER_VOLUME_SHAPE_BOX;

    if (hasVolume)
    {
        bMin = waterVolumes[0].boundsMin.xyz;
        bMax = waterVolumes[0].boundsMax.xyz;
        surfaceHeight = waterVolumes[0].boundsMin.w;
        absorption = waterVolumes[0].absorptionCoeff.xyz;
        deepColor = waterVolumes[0].deepColor.xyz;
        fogDensity = max(waterVolumes[0].boundsMax.w, 0.0);
        volumeShape = int(waterVolumes[0].absorptionCoeff.w + 0.5);

        bool outsideVolume =
            volumeShape == WATER_VOLUME_SHAPE_FISHBOWL
                ? (!waterVolumeContainsSurfacePoint(volumeShape, vWorldPos, bMin, bMax,
                                                    surfaceHeight) ||
                   vWorldPos.y < bMin.y || vWorldPos.y > surfaceHeight + 0.04)
                : (vWorldPos.x < bMin.x || vWorldPos.x > bMax.x ||
                   vWorldPos.z < bMin.z || vWorldPos.z > bMax.z ||
                   vWorldPos.y < bMin.y || vWorldPos.y > surfaceHeight + 0.04);
        if (outsideVolume)
        {
            discard;
        }
    }

    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uRefractionSource, 0));
    float sceneDepthRaw = texture(uSceneDepth, uv).r;
    float waterDepthRaw = gl_FragCoord.z;
    bool hasSceneGeometry = sceneDepthRaw < 0.9999;

    float waterEye = eyeDepth(uv, waterDepthRaw);
    float sceneEye = eyeDepth(uv, sceneDepthRaw);
    float depthToGeometry = max(sceneEye - waterEye, 0.0);
    if (!hasSceneGeometry)
    {
        depthToGeometry = 0.0;
    }

    float waterPath = texture(uWaterDist, uv).r;
    if (waterPath <= 0.0)
    {
        waterPath = depthToGeometry;
    }
    if (!hasSceneGeometry)
    {
        waterPath = 0.0;
    }
    // lighting owns the heavy underwater volume term. the surface shader uses a capped path
    // for optical weighting so it can stay clear while still reacting to depth.
    float surfacePathCap = max(u.v2Optics0.x, 0.001);
    float surfacePath = min(max(waterPath, 0.0), surfacePathCap);
    float edgeFadeDepth = max(u.params3.x, 0.001);
    float bandHardness = clamp(u.params3.y, 0.0, 1.0);
    float gradientStrength = clamp(u.params3.w, 0.0, 1.0);
    float surfaceDepthT = clamp(surfacePath / surfacePathCap, 0.0, 1.0);
    float bandedSurfaceDepthT = waterV2BandedRamp(surfaceDepthT, bandHardness);
    float styledSurfacePath =
        mix(surfacePath, bandedSurfaceDepthT * surfacePathCap, bandHardness * 0.65);

    float time = u.params0.y;
    float waveScale = max(u.params0.z, 0.001);
    float waveAmpUi = max(u.params0.w, 0.0);
    float waveEnergy = clamp(waveAmpUi / 1.35, 0.0, 1.0);
    float waveAmp = min(waveAmpUi, 0.70);
    vec3 primaryNormal = computeWaveNormal(vWorldPos.xz, time, waveScale, waveAmp);
    vec3 detailNormal = computeDetailWaveNormal(vWorldPos.xz, time, waveScale, waveAmp);
    float detailMix = mix(0.28, 0.60, waveEnergy);
    vec3 shapedNormal = normalize(mix(primaryNormal, detailNormal, detailMix));
    float surfaceBreakup = mix(0.84, 1.05,
                               valueNoise(vWorldPos.xz * (0.018 + waveScale * 0.11) +
                                          vec2(time * 0.006, -time * 0.004) + vec2(5.2, 8.7)));
    float normalAuthority = clamp(mix(0.20, 0.80, waveEnergy) * surfaceBreakup, 0.0, 0.84);
    vec3 waveNormal = normalize(mix(vec3(0.0, 1.0, 0.0), shapedNormal, normalAuthority));

    vec3 viewDir = normalize(u.camPos.xyz - vWorldPos);
    float NdotV = max(dot(waveNormal, viewDir), 0.001);
    float fresnel = schlickFresnel(NdotV, max(u.params1.w, 0.02));

    float sideDist = 1.0;
    float volRadius = 1.0;
    bool fishbowlContact = hasVolume && (volumeShape == WATER_VOLUME_SHAPE_FISHBOWL);
    if (hasVolume)
    {
        volRadius = max(min(bMax.x - bMin.x, bMax.z - bMin.z) * 0.5, 0.001);
        if (fishbowlContact)
        {
            sideDist = waterVolumeSurfaceEdgeDistance(volumeShape, vWorldPos, bMin, bMax,
                                                      surfaceHeight);
        }
        else
        {
            float dx = min(vWorldPos.x - bMin.x, bMax.x - vWorldPos.x);
            float dz = min(vWorldPos.z - bMin.z, bMax.z - vWorldPos.z);
            sideDist = min(dx, dz);
        }
    }

    // fishbowl contact is art-directed in radius-relative bands so the curved rim
    // remains readable without becoming a broad bright outline.
    float sideStart = u.v2Contact0.x;
    float sideEnd = u.v2Contact0.y;
    float lineStart = u.v2Contact0.z;
    float lineEnd = u.v2Contact0.w;
    if (fishbowlContact)
    {
        sideStart = volRadius * 0.010;
        sideEnd = volRadius * 0.095;
        lineStart = 0.0;
        lineEnd = volRadius * 0.024;
    }

    float contactHardness = fishbowlContact ? bandHardness * 0.55 : bandHardness;
    float sideContactMask = hasVolume ? 1.0 - smoothstep(sideStart, sideEnd, sideDist) : 0.0;
    float waterlineMask = hasVolume ? 1.0 - smoothstep(lineStart, lineEnd, sideDist) : 0.0;
    sideContactMask = waterV2HardenMask(sideContactMask, contactHardness);
    waterlineMask = waterV2HardenMask(waterlineMask, contactHardness);
    float contactMask = clamp(max(sideContactMask, waterlineMask), 0.0, 1.0);
    float contactView = pow(1.0 - clamp(NdotV, 0.0, 1.0), 1.25);
    float fishbowlReflectionDamp = fishbowlContact ? mix(1.0, 0.77, contactMask) : 1.0;

    float refractionAuthority = mix(0.50, 1.22, waveEnergy);
    vec2 refractionOffset = waveNormal.xz * u.params1.x * refractionAuthority;
    refractionOffset *=
        clamp(surfacePath * max(u.params2.w, 0.0) * max(u.v2Optics1.y, 0.0), 0.0, 1.0);
    vec2 refractedUV = clamp(uv + refractionOffset, vec2(0.002), vec2(0.998));
    vec3 sceneColor = texture(uRefractionSource, refractedUV).rgb;

    vec3 transmission = exp(-absorption * styledSurfacePath * max(u.v2Optics0.y, 0.0));
    vec3 refractedColor = sceneColor * transmission;
    float fogFactor = 1.0 - exp(-fogDensity * styledSurfacePath * max(u.v2Optics0.z, 0.0));
    fogFactor = waterV2BandedRamp(fogFactor, bandHardness * 0.75);
    refractedColor =
        mix(refractedColor, deepColor,
            clamp(fogFactor * max(u.v2Optics0.w, 0.0), 0.0, max(u.v2Optics1.x, 0.0)));
    vec3 shallowBodyTint =
        max(u.waterTint.rgb * vec3(1.20, 2.00, 2.20), vec3(0.025, 0.22, 0.28));
    vec3 deepBodyTint =
        max(deepColor * vec3(0.90, 1.22, 1.34), vec3(0.014, 0.14, 0.18));
    vec3 bodyTint = mix(shallowBodyTint, deepBodyTint, bandedSurfaceDepthT);
    float bodyPathPresence =
        waterV2BandedRamp(1.0 - exp(-surfacePath * max(u.v2Body0.y, 0.0)),
                          bandHardness);
    float bodyPresence =
        clamp(max(u.v2Body0.x, 0.0) +
                  bodyPathPresence * max(u.v2Body0.z, 0.0) +
                  pow(1.0 - clamp(NdotV, 0.0, 1.0), 1.8) * max(u.v2Body0.w, 0.0),
              0.0, max(u.v2Body1.x, 0.0));
    vec3 tintedTransmission =
        mix(refractedColor * (u.v2Body1.y + bodyTint * u.v2Body1.z), bodyTint,
            clamp(u.v2Body1.w, 0.0, 1.0));
    refractedColor = mix(refractedColor, tintedTransmission, bodyPresence);

    vec3 reflectDir = reflect(-viewDir, waveNormal);
    vec3 envReflection = sampleContainedEnvironment(reflectDir, bodyTint, deepColor);
    vec3 localReflection =
        texture(uRefractionSource, clamp(uv + reflectDir.xz * u.v2Reflection0.x, 0.0, 1.0)).rgb;
    float localReflectionWeight =
        clamp(u.v2Reflection0.y + (1.0 - abs(reflectDir.y)) * u.v2Reflection0.z +
                  clamp(surfacePath * max(u.v2Reflection0.w, 0.0), 0.0,
                        max(u.v2Reflection1.x, 0.0)),
              u.v2Reflection1.y, u.v2Reflection1.z);
    vec3 fallbackReflection = mix(envReflection, localReflection, localReflectionWeight);

    vec4 planarWorldPos = vec4(vWorldPos.x, u.params0.x, vWorldPos.z, 1.0);
    vec4 projectedClipR = u.reflectedViewProj * planarWorldPos;
    bool projectedClipValid = projectedClipR.w > 1e-5;
    vec2 projectedUvBase = projectedClipR.xy / max(projectedClipR.w, 1e-5) * 0.5 + 0.5;
    float projectedEdgeDist = min(min(projectedUvBase.x, projectedUvBase.y),
                                  min(1.0 - projectedUvBase.x, 1.0 - projectedUvBase.y));
    float distortionEdgeFade = smoothstep(0.0, 0.08, projectedEdgeDist);
    float planarDistortion = mix(u.v2Planar0.x, u.v2Planar0.y, waveEnergy) * distortionEdgeFade;
    vec2 planarUv = projectedUvBase + waveNormal.xz * planarDistortion;
    vec2 planarUvClamped = clamp(planarUv, vec2(0.001), vec2(0.999));
    vec2 projectedUvClamped = clamp(projectedUvBase, vec2(0.001), vec2(0.999));
    float planarGuardBand = max(u.v2Planar0.z, 0.001);
    float planarInRange = float(all(greaterThanEqual(projectedUvBase, vec2(-planarGuardBand))) &&
                                all(lessThanEqual(projectedUvBase, vec2(1.0 + planarGuardBand))));
    float planarOutside = max(max(-projectedUvBase.x, projectedUvBase.x - 1.0),
                              max(-projectedUvBase.y, projectedUvBase.y - 1.0));
    float guardFade = 1.0 - smoothstep(0.0, planarGuardBand, max(planarOutside, 0.0));
    float edgeDist = min(min(planarUv.x, planarUv.y), min(1.0 - planarUv.x, 1.0 - planarUv.y));
    float borderFade = smoothstep(0.0, max(u.v2Planar0.w, 0.001), edgeDist);
    float planarMask = (projectedClipValid ? 1.0 : 0.0) * planarInRange * guardFade * borderFade;
    vec3 planarSample = texture(uPlanarReflection, planarUvClamped).rgb;
    float planarStrength = clamp(u.params4.w, 0.0, 1.0);
    float grazing = pow(1.0 - clamp(NdotV, 0.0, 1.0), 1.35);
    float planarViewGate =
        mix(u.v2Planar1.x, 1.0, smoothstep(u.v2Planar1.y, u.v2Planar1.z, grazing));
    float planarWeight = planarMask * planarStrength * planarViewGate * fishbowlReflectionDamp;
    vec3 planarStyled =
        mix(planarSample, envReflection, u.v2Planar1.w + grazing * u.v2Planar2.x);
    vec3 reflectionColor = mix(fallbackReflection, planarStyled, planarWeight);

    float reflectionMix =
        clamp(pow(fresnel, 1.08) * max(u.params3.z, 0.0) +
                  grazing * max(u.params3.z, 0.20) * max(u.v2Planar2.y, 0.0) +
                  planarWeight * max(u.params3.z, 0.35) * max(u.v2Planar2.z, 0.0) +
                  clamp(surfacePath * 0.006, 0.0, 0.06),
              0.0, max(u.v2Planar2.w, 0.0));
    if (planarStrength > 0.001)
    {
        reflectionMix *= fishbowlReflectionDamp;
    }
    vec3 waterColor = mix(refractedColor, reflectionColor, reflectionMix);
    waterColor =
        mix(waterColor, bodyTint,
            bodyPresence * (1.0 - reflectionMix * u.v2Body2.x) * u.v2Body2.y);

    if (gradientStrength > 0.001)
    {
        vec2 gradUv0 = vWorldPos.xz * (0.032 + waveScale * 0.11) +
                       vec2(time * 0.008, -time * 0.006);
        vec2 gradUv1 = vWorldPos.zx * (0.052 + waveScale * 0.08) +
                       vec2(-time * 0.005, time * 0.007) + vec2(9.4, 2.7);
        float gradNoise = mix(valueNoise(gradUv0), valueNoise(gradUv1), 0.42);
        float gradientT =
            clamp(bandedSurfaceDepthT * 0.54 + grazing * 0.34 +
                      (gradNoise - 0.5) * 0.30 + waveEnergy * 0.06,
                  0.0, 1.0);
        gradientT = waterV2BandedRamp(gradientT, bandHardness * 0.85);
        vec3 gradientTint = mix(vec3(1.08, 1.13, 1.07), vec3(0.82, 0.96, 1.16),
                                gradientT);
        waterColor = mix(waterColor, waterColor * gradientTint, gradientStrength);
    }

    vec3 sunDir = normalize(u.sunDirToSun.xyz);
    vec3 halfVec = normalize(viewDir + sunDir);
    float NdotH = max(dot(waveNormal, halfVec), 0.0);
    float NdotL = max(dot(waveNormal, sunDir), 0.0);
    float specPower = max(u.params1.y, 1.0);
    float spec = pow(NdotH, specPower) * max(u.params1.z, 0.0) * NdotL;
    waterColor += u.sunColor.rgb * spec * max(u.v2Body2.z, 0.0);

    vec2 rippleWarp =
        (vec2(valueNoise(vWorldPos.xz * (0.026 + waveScale * 0.055) +
                         vec2(time * 0.010, -time * 0.007)),
              valueNoise(vWorldPos.zx * (0.030 + waveScale * 0.060) +
                         vec2(-time * 0.008, time * 0.009) + vec2(12.4, 3.6))) -
         0.5) *
        (1.25 + waveScale * 3.0);
    vec2 rippleWorld = vWorldPos.xz + rippleWarp;
    vec2 rippleUv0 = rippleWorld * (0.030 + waveScale * 0.10) +
                     vec2(time * 0.008, -time * 0.006);
    vec2 rippleUv1 = (rippleWorld + vec2(rippleWarp.y, -rippleWarp.x) * 0.45) *
                         (0.052 + waveScale * 0.13) +
                     vec2(-time * 0.006, time * 0.009) + vec2(11.3, 4.7);
    float ripple0 = valueNoise(rippleUv0);
    float ripple1 = valueNoise(rippleUv1);
    float broadBreak = valueNoise(vWorldPos.xz * (0.018 + waveScale * 0.035) +
                                  vec2(time * 0.004, time * 0.003) + vec2(19.7, 2.1));
    float fineBreak = valueNoise(vWorldPos.xz * (0.110 + waveScale * 0.19) +
                                 vec2(-time * 0.011, time * 0.013) + vec2(4.9, 21.3));
    float rippleCandidate = abs(ripple0 - ripple1) * mix(0.90, 1.18, broadBreak);
    float rippleLines = smoothstep(0.66, 0.96, rippleCandidate * 1.18);
    rippleLines *= mix(0.48, 1.0, smoothstep(0.24, 0.88, fineBreak));
    float grazingDetail = pow(1.0 - clamp(NdotV, 0.0, 1.0), 1.6);
    waterColor += u.sunColor.rgb * rippleLines * grazingDetail * waveEnergy *
                  max(u.v2Body2.w, 0.0) * 0.32;

    float sparkleMask = 0.0;
    float crestDetail = max(u.params2.z, 0.0);
    if (surfacePath > 0.35 && crestDetail > 0.001)
    {
        vec2 sparkleUv = vWorldPos.xz * 0.045 + vec2(time * 0.006, -time * 0.004) +
                         vec2(23.1, 9.4);
        float sparkleNoise = valueNoise(sparkleUv);
        float sparkleThreshold = smoothstep(0.965, 0.995, sparkleNoise);
        float scatter = pow(max(dot(viewDir, normalize(u.sunDirToSun.xyz)), 0.0), 14.0);
        float depthMask = clamp(1.0 - exp(-fogDensity * surfacePath * 0.75), 0.0, 1.0);
        sparkleMask = sparkleThreshold * scatter * depthMask * clamp(crestDetail, 0.0, 1.5);
        waterColor += u.sunColor.rgb * sparkleMask * 0.012;
    }

    float voxelVfxMask = 0.0;
    float voxelFoamMask = 0.0;
    float bodyDetailMask = 0.0;
    vec3 bodyDetailColor = vec3(0.0);
    if (hasVolume)
    {
        bodyDetailMask = waterBodyDetailMask(vWorldPos, viewDir, surfacePath, bMin, bMax,
                                             surfaceHeight, bodyTint, deepColor, volumeShape, time,
                                             bodyDetailColor);
        float bodyDetailVisibility = bodyDetailMask * (1.0 - reflectionMix * 0.45);
        float bodyDetailBlend =
            bodyDetailVisibility * (0.16 + bodyDetailVisibility * 0.12);
        vec3 bodyDetailShadow =
            mix(waterColor, waterColor * vec3(0.965, 0.975, 0.990), bodyDetailVisibility * 0.10);
        vec3 bodyDetailBase =
            mix(bodyDetailShadow, max(bodyDetailShadow, bodyDetailColor),
                bodyDetailVisibility * 0.58);
        waterColor = mix(waterColor, bodyDetailBase, bodyDetailBlend);
        waterColor += bodyDetailColor * bodyDetailVisibility * 0.032;
    }

    if (hasVolume)
    {
        voxelVfxMask = voxelWaterVfxMask(vWorldPos, viewDir, surfacePath, bMin, bMax,
                                         surfaceHeight, volumeShape, time);
        float voxelVfxPresence =
            clamp(voxelVfxMask *
                      (1.65 + clamp(u.v2Vfx0.y, 0.0, 1.0) * 1.60 +
                       clamp(u.v2Vfx0.w, 0.0, 1.0) * 0.45),
                  0.0, 1.0);
        vec3 voxelVfxColor =
            mix(bodyTint * vec3(0.62, 0.95, 1.12), vec3(0.86, 0.98, 1.0), 0.68);
        vec3 voxelVfxShadow = mix(waterColor, bodyTint * vec3(0.36, 0.62, 0.72),
                                  voxelVfxPresence * 0.07);
        waterColor = mix(voxelVfxShadow, max(voxelVfxShadow, voxelVfxColor),
                         voxelVfxPresence * 0.16);
        waterColor += voxelVfxColor * voxelVfxPresence * 0.13;

        float voxelFoamSurface = localizedVoxelFoamSurfaceMask(vWorldPos, surfacePath, time);
        float voxelFoamVolume =
            localizedVoxelFoamVolumeMask(vWorldPos, viewDir, surfacePath, bMin, bMax,
                                         surfaceHeight, volumeShape, time);
        voxelFoamMask = clamp(voxelFoamVolume * 0.35 + voxelFoamSurface * 0.18, 0.0, 1.0);
        vec3 foamColor =
            mix(bodyTint * vec3(0.72, 1.05, 1.18), vec3(0.96, 1.0, 1.0), 0.84);
        float foamIntensity = clamp(u.v2Vfx1.y, 0.0, 1.0);
        waterColor = mix(waterColor, max(waterColor, foamColor),
                         voxelFoamMask * (0.18 + foamIntensity * 0.18));
        waterColor += foamColor * voxelFoamMask * 0.055;
    }

    vec3 contactTint = mix(bodyTint * vec3(0.78, 1.05, 1.15), vec3(0.84, 0.95, 1.0),
                           clamp(fresnel + waterlineMask * u.v2Contact2.w, 0.0, 1.0));
    vec3 contactDepthTint =
        mix(waterColor * 0.88, bodyTint * vec3(0.58, 0.84, 0.95), u.v2Contact1.x);
    waterColor = mix(waterColor, contactDepthTint, sideContactMask * u.v2Contact1.y);
    waterColor +=
        contactTint * sideContactMask * (u.v2Contact1.z + u.v2Contact1.w * contactView);

    // concave meniscus (fishbowl): water climbs and thickens against the glass. the band
    // just inside the contact line reads as a darker, more saturated "wet" dip; the line
    // itself gets a thin bright highlight.
    float meniscusBoost = fishbowlContact ? 1.0 : 0.0;
    float wetDip = clamp(sideContactMask - waterlineMask, 0.0, 1.0);
    vec3 wetTone = bodyTint * vec3(0.42, 0.70, 0.86);
    waterColor = mix(waterColor, wetTone, wetDip * meniscusBoost * 0.20);

    float contactGlint = pow(waterlineMask, 2.0) * (0.35 + 0.65 * fresnel) *
                         (0.55 + 0.45 * max(NdotL, 0.0));
    float contactGlintScale = fishbowlContact ? 0.78 : 1.0;
    waterColor +=
        u.sunColor.rgb * contactGlint * max(u.v2Contact2.x, 0.0) * contactGlintScale;

    vec3 meniscusColor = mix(vec3(0.86, 0.96, 1.0), u.sunColor.rgb, 0.35 * NdotL);
    waterColor += meniscusColor * waterlineMask * meniscusBoost *
                  (0.055 + 0.11 * fresnel + 0.060 * contactView);

    float alpha = 1.0;
    if (hasSceneGeometry)
    {
        alpha = smoothstep(0.0, edgeFadeDepth, depthToGeometry);
    }
    float volumeEdgeAlpha =
        hasVolume ? smoothstep(0.0, edgeFadeDepth, max(sideDist, 0.0)) : 1.0;
    alpha = min(alpha, volumeEdgeAlpha);
    float surfaceOpacity = mix(u.v2Opacity0.x, u.v2Opacity0.y, clamp(fresnel, 0.0, 1.0));
    surfaceOpacity +=
        clamp(styledSurfacePath * max(u.v2Opacity0.z, 0.0), 0.0,
              max(u.v2Opacity0.w, 0.0));
    surfaceOpacity += bodyPresence * max(u.v2Opacity1.x, 0.0);
    surfaceOpacity +=
        sideContactMask * max(u.v2Contact2.y, 0.0) + waterlineMask * max(u.v2Contact2.z, 0.0);
    surfaceOpacity += waveEnergy * max(u.v2Opacity1.y, 0.0);
    surfaceOpacity += bodyDetailMask * 0.010;
    surfaceOpacity += voxelVfxMask * 0.055;
    surfaceOpacity += voxelFoamMask * 0.055;
    alpha = min(alpha, clamp(surfaceOpacity, u.v2Opacity1.z, u.v2Opacity1.w));

    int debugMode = int(u.params4.x + 0.5);
    if (debugMode == 1)
    {
        float vis = clamp(waterPath / 10.0, 0.0, 1.0);
        oColor = vec4(vec3(vis), alpha);
        return;
    }
    if (debugMode == 2)
    {
        oColor = vec4(vec3(fresnel), alpha);
        return;
    }
    if (debugMode == 3)
    {
        oColor = vec4(vec3(reflectionMix), alpha);
        return;
    }
    if (debugMode == 4)
    {
        oColor = vec4(vec3(alpha), alpha);
        return;
    }
    if (debugMode == 5)
    {
        oColor = vec4(texture(uRefractionSource, uv).rgb, 1.0);
        return;
    }
    if (debugMode == 6)
    {
        vec3 depthVis = vec3(clamp(waterPath / 12.0, 0.0, 1.0), clamp(contactMask, 0.0, 1.0),
                             clamp(sparkleMask * 6.0, 0.0, 1.0));
        oColor = vec4(depthVis, alpha);
        return;
    }
    if (debugMode == 7)
    {
        oColor = vec4(mix(reflectionColor, vec3(planarWeight), 0.15), 1.0);
        return;
    }
    if (debugMode == 8)
    {
        oColor = vec4(reflectionColor, 1.0);
        return;
    }
    if (debugMode == 9)
    {
        oColor = vec4(refractedColor, 1.0);
        return;
    }
    if (debugMode == 10)
    {
        oColor = vec4(planarMask, planarWeight, planarViewGate, 1.0);
        return;
    }
    if (debugMode == 11)
    {
        oColor = vec4(bodyTint * clamp(bodyPresence + 0.20, 0.0, 1.0), 1.0);
        return;
    }
    if (debugMode == 12)
    {
        oColor = vec4(waveNormal * 0.5 + 0.5, 1.0);
        return;
    }
    if (debugMode == 13)
    {
        oColor = vec4(planarSample, 1.0);
        return;
    }
    if (debugMode == 14)
    {
        oColor = vec4(fallbackReflection, 1.0);
        return;
    }
    if (debugMode == 15)
    {
        oColor = vec4(texture(uPlanarReflection, clamp(uv, vec2(0.001), vec2(0.999))).rgb, 1.0);
        return;
    }
    if (debugMode == 16)
    {
        oColor = vec4(projectedUvBase, projectedClipValid ? 1.0 : 0.0, 1.0);
        return;
    }
    if (debugMode == 17)
    {
        oColor = vec4(texture(uPlanarReflection, projectedUvClamped).rgb, 1.0);
        return;
    }
    if (debugMode == 18)
    {
        vec3 vfxDebug = mix(vec3(0.015, 0.045, 0.055), vec3(0.78, 0.96, 1.0),
                            clamp(voxelVfxMask * 7.0, 0.0, 1.0));
        oColor = vec4(vfxDebug, 1.0);
        return;
    }
    if (debugMode == 19)
    {
        vec3 foamDebug = mix(vec3(0.025, 0.040, 0.045), vec3(0.92, 1.0, 1.0),
                             clamp(voxelFoamMask * 3.5, 0.0, 1.0));
        oColor = vec4(foamDebug, 1.0);
        return;
    }
    if (debugMode == 20)
    {
        vec3 bodyDebug = mix(vec3(0.015, 0.035, 0.045), bodyDetailColor,
                             clamp(bodyDetailMask * 2.8, 0.0, 1.0));
        oColor = vec4(bodyDebug, 1.0);
        return;
    }

    // the refraction source has already received lighting's atmosphere. preserve
    // that base and attenuate only terms authored by this transparent surface.
    vec3 cameraToSurface = vWorldPos - u.camPos.xyz;
    float atmosphereViewDistance = length(cameraToSurface);
    if (u.atmosphere0.x > 0.0 && atmosphereViewDistance > 0.0)
    {
        float opticalDepth = sceneAtmosphereFiniteAirOpticalDepthWithWater(
            u.atmosphere0.x, u.atmosphere0.y, u.atmosphere0.z,
            u.camPos.xyz, cameraToSurface / atmosphereViewDistance,
            atmosphereViewDistance);
        waterColor = attenuateSceneAtmosphereSurfaceContribution(
            u.atmosphere0.x, opticalDepth, refractedColor, waterColor);
    }

    oColor = vec4(waterColor, alpha);
}
