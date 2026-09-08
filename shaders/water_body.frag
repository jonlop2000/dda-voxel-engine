#version 450
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uSceneDepth;
layout(set = 0, binding = 2) uniform sampler2D uWaterDist;

layout(set = 1, binding = 0) uniform UBO {
    mat4 invViewProj;
    vec4 camPos;
    vec4 sunDirToSun;
    vec4 waterTint;
    vec4 params0; // x=time, y=debugMode, z=waterVolumeCount, w=authored mote intensity
    vec4 params1; // x=bodyStrength, y=pathRate, z=scatterStrength, w=noiseStrength
    vec4 params2; // x=shaftStrength, y=bandContrast, z=apertureHalfExtent, w=warmth
} u;

struct WaterVolumeGPU
{
    vec4 boundsMin;       // xyz = min, w = surfaceHeight
    vec4 boundsMax;       // xyz = max, w = fogDensity
    vec4 absorptionCoeff; // xyz = absorption
    vec4 deepColor;       // xyz = deep color
};

layout(std430, set = 2, binding = 0) readonly buffer WaterBuf {
    WaterVolumeGPU waterVolumes[];
};

#include "water_volume_shape.glsl"

float hash13(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.11369, 0.13787));
    p += vec3(dot(p, p.yzx + vec3(19.19)));
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    return mix(nxy0, nxy1, f.z);
}

vec3 reconstructWorldPos(vec2 uv, float depth01)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth01, 1.0);
    vec4 world = u.invViewProj * ndc;
    return world.xyz / max(world.w, 1e-6);
}

vec2 intersectAabb(vec3 rayOrigin, vec3 rayDir, vec3 boundsMin, vec3 boundsMax)
{
    vec3 safeDir = rayDir;
    safeDir.x = abs(safeDir.x) < 1e-7 ? (safeDir.x < 0.0 ? -1e-7 : 1e-7) : safeDir.x;
    safeDir.y = abs(safeDir.y) < 1e-7 ? (safeDir.y < 0.0 ? -1e-7 : 1e-7) : safeDir.y;
    safeDir.z = abs(safeDir.z) < 1e-7 ? (safeDir.z < 0.0 ? -1e-7 : 1e-7) : safeDir.z;
    vec3 invDir = 1.0 / safeDir;
    vec3 t0 = (boundsMin - rayOrigin) * invDir;
    vec3 t1 = (boundsMax - rayOrigin) * invDir;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);
    float entry = max(max(tMin.x, tMin.y), tMin.z);
    float exit = min(min(tMax.x, tMax.y), tMax.z);
    return vec2(entry, exit);
}

struct ShaftRayHit
{
    float path;
    float tau;
};

ShaftRayHit intersectStylizedSunroofRibbon(
    vec2 openingStart, vec2 openingDelta, float waterPath,
    vec2 center, vec2 radii, vec2 rotation,
    vec3 segmentOrigin, vec3 segmentDelta,
    float surfaceHeight, float waterDepth, float apertureHalfExtent)
{
    vec2 startDelta = openingStart - center;
    vec2 localStart = vec2(rotation.x * startDelta.x + rotation.y * startDelta.y,
                           -rotation.y * startDelta.x + rotation.x * startDelta.y) /
                      max(radii, vec2(0.001));
    vec2 localDelta = vec2(rotation.x * openingDelta.x + rotation.y * openingDelta.y,
                           -rotation.y * openingDelta.x + rotation.x * openingDelta.y) /
                      max(radii, vec2(0.001));

    float quadraticA = dot(localDelta, localDelta);
    float quadraticB = dot(localStart, localDelta);
    float outerRadiusSquared = 1.32 * 1.32;
    float quadraticC = dot(localStart, localStart) - outerRadiusSquared;
    float intervalStart = 0.0;
    float intervalEnd = 0.0;

    if (quadraticA <= 1.0e-7)
    {
        if (quadraticC <= 0.0)
        {
            intervalEnd = 1.0;
        }
    }
    else
    {
        float discriminant = quadraticB * quadraticB - quadraticA * quadraticC;
        if (discriminant > 0.0)
        {
            float root = sqrt(discriminant);
            float invA = 1.0 / quadraticA;
            intervalStart = clamp((-quadraticB - root) * invA, 0.0, 1.0);
            intervalEnd = clamp((-quadraticB + root) * invA, 0.0, 1.0);
        }
    }

    ShaftRayHit hit;
    hit.path = 0.0;
    hit.tau = clamp(quadraticA > 1.0e-7 ? -quadraticB / quadraticA : 0.5, 0.0, 1.0);

    float interval = max(intervalEnd - intervalStart, 0.0);
    if (interval <= 0.0)
    {
        return hit;
    }

    hit.tau = (intervalStart + intervalEnd) * 0.5;
    vec2 closestLocal = localStart + localDelta * hit.tau;
    float closestDistanceSquared = dot(closestLocal, closestLocal);
    float softEdge = 1.0 - smoothstep(0.30 * 0.30, outerRadiusSquared,
                                      closestDistanceSquared);

    vec3 hitPosition = segmentOrigin + segmentDelta * hit.tau;
    float depthT = clamp((surfaceHeight - hitPosition.y) / waterDepth, 0.0, 1.0);
    float depthEnvelope = smoothstep(0.01, 0.10, depthT) *
                          (1.0 - smoothstep(0.86, 1.0, depthT));
    vec2 openingAtHit = openingStart + openingDelta * hit.tau;
    float apertureDistance = max(abs(openingAtHit.x), abs(openingAtHit.y));
    float openingGate = 1.0 - smoothstep(apertureHalfExtent - 0.025,
                                         apertureHalfExtent + 0.090,
                                         apertureDistance);
    hit.path = waterPath * interval * softEdge * depthEnvelope * openingGate;
    return hit;
}

float evalStylizedSunroofShaftRay(vec3 rayOrigin, vec3 rayDir,
                                  float segmentStart, float waterPath,
                                  vec3 bMin, vec3 bMax, float surfaceHeight,
                                  vec3 sunDir, float apertureHalfExtent,
                                  float contrast, float time)
{
    vec3 segmentOrigin = rayOrigin + rayDir * segmentStart;
    vec3 segmentDelta = rayDir * waterPath;
    vec3 segmentEnd = segmentOrigin + segmentDelta;
    vec2 volumeCenter = (bMin.xz + bMax.xz) * 0.5;
    vec2 volumeHalfExtent = max((bMax.xz - bMin.xz) * 0.5, vec2(0.001));
    float invSunHeight = 1.0 / max(sunDir.y, 0.08);
    vec2 projectedStart = segmentOrigin.xz +
                          sunDir.xz * max(surfaceHeight - segmentOrigin.y, 0.0) *
                              invSunHeight;
    vec2 projectedEnd = segmentEnd.xz +
                        sunDir.xz * max(surfaceHeight - segmentEnd.y, 0.0) *
                            invSunHeight;
    vec2 openingStart = (projectedStart - volumeCenter) / volumeHalfExtent;
    vec2 openingEnd = (projectedEnd - volumeCenter) / volumeHalfExtent;
    vec2 openingDelta = openingEnd - openingStart;

    float waterDepth = max(surfaceHeight - bMin.y, 0.05);
    vec3 segmentMidpoint = segmentOrigin + segmentDelta * 0.5;
    float midpointDepth = clamp((surfaceHeight - segmentMidpoint.y) / waterDepth, 0.0, 1.0);
    float spread = 0.008 + midpointDepth * 0.016;
    float a = apertureHalfExtent;

    // V2.5 preserves V2.4's complete-segment analytic route. five separated
    // intersections replace forty per-sample ellipse evaluations and sixteen
    // added 3D-noise evaluations from V2.3.
    ShaftRayHit h0 = intersectStylizedSunroofRibbon(
        openingStart, openingDelta, waterPath, vec2(-0.46, -0.20) * a,
        vec2(0.14 * a + spread, 0.40 * a + spread), vec2(0.983844, 0.179030),
        segmentOrigin, segmentDelta, surfaceHeight, waterDepth, apertureHalfExtent);
    ShaftRayHit h1 = intersectStylizedSunroofRibbon(
        openingStart, openingDelta, waterPath, vec2(0.38, -0.28) * a,
        vec2(0.13 * a + spread, 0.36 * a + spread), vec2(0.980067, -0.198669),
        segmentOrigin, segmentDelta, surfaceHeight, waterDepth, apertureHalfExtent);
    ShaftRayHit h2 = intersectStylizedSunroofRibbon(
        openingStart, openingDelta, waterPath, vec2(-0.18, 0.36) * a,
        vec2(0.15 * a + spread, 0.38 * a + spread), vec2(0.928665, 0.370920),
        segmentOrigin, segmentDelta, surfaceHeight, waterDepth, apertureHalfExtent);
    ShaftRayHit h3 = intersectStylizedSunroofRibbon(
        openingStart, openingDelta, waterPath, vec2(0.44, 0.22) * a,
        vec2(0.12 * a + spread, 0.32 * a + spread), vec2(0.949235, -0.314567),
        segmentOrigin, segmentDelta, surfaceHeight, waterDepth, apertureHalfExtent);
    ShaftRayHit h4 = intersectStylizedSunroofRibbon(
        openingStart, openingDelta, waterPath, vec2(0.02, 0.00) * a,
        vec2(0.11 * a + spread, 0.30 * a + spread), vec2(0.999200, 0.039989),
        segmentOrigin, segmentDelta, surfaceHeight, waterDepth, apertureHalfExtent);

    float totalPath = h0.path + h1.path + h2.path + h3.path + h4.path;
    if (totalPath <= 0.0001)
    {
        return 0.0;
    }

    float dominantPath = max(max(h0.path, h1.path), max(max(h2.path, h3.path), h4.path));
    float layeredPath = dominantPath + max(totalPath - dominantPath, 0.0) * 0.035;
    float response = 1.0 - exp(-layeredPath * 0.072);
    float shaped = smoothstep(mix(0.02, 0.13, contrast),
                              mix(0.98, 0.82, contrast), response);
    response = mix(response, shaped, contrast * 0.55);

    float meanTau = (h0.path * h0.tau + h1.path * h1.tau + h2.path * h2.tau +
                     h3.path * h3.tau + h4.path * h4.tau) /
                    totalPath;
    vec3 meanHit = segmentOrigin + segmentDelta * meanTau;
    float wash0 = 0.5 + 0.5 * sin(dot(meanHit, vec3(0.040, 0.052, 0.031)) +
                                     time * 0.0025 + 1.7);
    float wash1 = 0.5 + 0.5 * sin(dot(meanHit, vec3(-0.021, 0.036, 0.047)) -
                                     time * 0.0018 + 4.1);
    float broadBreakup = smoothstep(0.30, 0.72, wash0 * 0.62 + wash1 * 0.38);
    float watercolor = mix(0.38, 1.0, broadBreakup) * mix(0.88, 1.04, wash1);
    return clamp(response * watercolor, 0.0, 1.15);
}

struct BodyEval
{
    float density;
    float structure;
    float midColumn;
    float particulate;
    float pathMask;
    vec3 mediumColor;
    vec3 meanPos;
};

BodyEval evalWaterBody(vec3 rayOrigin, vec3 rayDir, float segmentStart, float waterPath)
{
    WaterVolumeGPU volume = waterVolumes[0];
    vec3 bMin = volume.boundsMin.xyz;
    vec3 bMax = volume.boundsMax.xyz;
    float surfaceHeight = volume.boundsMin.w;
    int shape = int(volume.absorptionCoeff.w + 0.5);
    float waterDepth = max(surfaceHeight - bMin.y, 0.05);

    float tracePath = min(waterPath, 28.0);

    float strength = max(u.params1.x, 0.0);
    float pathRate = max(u.params1.y, 0.0);
    float noiseStrength = clamp(u.params1.w, 0.0, 1.0);
    float time = u.params0.x;

    float densityAccum = 0.0;
    float structureAccum = 0.0;
    float midAccum = 0.0;
    float particulateAccum = 0.0;
    float weightAccum = 0.0;
    vec3 meanPos = vec3(0.0);
    float apertureHalfExtent = clamp(u.params2.z, 0.0, 1.0);

    for (int i = 0; i < 8; ++i)
    {
        float t = (float(i) + 0.45) / 8.0;
        float sampleDist = segmentStart + t * tracePath;
        vec3 p = rayOrigin + rayDir * sampleDist;
        bool outsideVolume =
            shape == WATER_VOLUME_SHAPE_FISHBOWL
                ? !waterVolumeContainsPoint(shape, p, bMin, bMax, surfaceHeight)
                : (any(lessThan(p, bMin)) ||
                   any(greaterThan(p, vec3(bMax.x, surfaceHeight + 0.04, bMax.z))));
        if (outsideVolume)
        {
            continue;
        }

        float belowSurface = max(surfaceHeight - p.y, 0.0);
        float depthNorm = clamp(belowSurface / waterDepth, 0.0, 1.0);
        float midColumn =
            smoothstep(0.10, 0.34, depthNorm) * (1.0 - smoothstep(0.68, 0.94, depthNorm));
        float lowerColumn = smoothstep(0.56, 0.94, depthNorm);

        vec3 broadP = p * vec3(0.045, 0.060, 0.045) +
                      vec3(time * 0.003, -time * 0.004, time * 0.002);
        vec3 detailP = p * vec3(0.130, 0.165, 0.130) +
                       vec3(17.1, 5.3, time * 0.012);
        float broad = valueNoise(broadP);
        float detail = valueNoise(detailP);
        float structure = mix(broad, detail, 0.32);
        float structureMod = mix(1.0, mix(0.68, 1.38, structure), noiseStrength);

        vec3 moteP = p * vec3(3.5, 4.0, 3.5) + vec3(31.7, time * 0.040, 9.2);
        float moteNoise = valueNoise(moteP);
        float mote = smoothstep(0.92, 0.985, moteNoise) * midColumn;

        float columnWeight = 0.38 + midColumn * 0.52 + lowerColumn * 0.18;
        float localDensity = columnWeight * structureMod;

        float distT = clamp(t, 0.0, 1.0);
        float weight = smoothstep(0.0, 0.16, distT) * (1.0 - smoothstep(0.92, 1.0, distT));
        weight *= mix(0.82, 1.24, midColumn);

        densityAccum += localDensity * weight;
        structureAccum += structure * weight;
        midAccum += midColumn * weight;
        particulateAccum += mote * weight;
        meanPos += p * weight;
        weightAccum += weight;
    }

    BodyEval e;
    e.density = 0.0;
    e.structure = 0.0;
    e.midColumn = 0.0;
    e.particulate = 0.0;
    e.pathMask = 0.0;
    e.mediumColor = max(volume.deepColor.xyz, u.waterTint.rgb);
    e.meanPos = rayOrigin + rayDir * (segmentStart + tracePath * 0.5);

    if (weightAccum <= 0.001)
    {
        return e;
    }

    float densityField = densityAccum / weightAccum;
    float structureField = structureAccum / weightAccum;
    float midMean = midAccum / weightAccum;
    float particulateMean = particulateAccum / weightAccum;
    meanPos /= weightAccum;

    float pathMask = (1.0 - exp(-tracePath * (0.055 + pathRate * 0.155))) *
                     smoothstep(0.55, 5.50, tracePath);
    float structureBoost = mix(0.88, 1.18, structureField);
    float densityMask = clamp(pathMask * strength * densityField * structureBoost, 0.0, 1.0);

    float depthT = clamp((surfaceHeight - meanPos.y) / waterDepth, 0.0, 1.0);
    vec3 shallow = max(u.waterTint.rgb * vec3(1.10, 1.70, 1.95), vec3(0.018, 0.14, 0.18));
    vec3 deep = max(volume.deepColor.xyz * vec3(0.92, 1.18, 1.28), shallow * 0.45);
    vec3 medium =
        mix(shallow, deep, clamp(depthT * 0.55 + midMean * 0.22 + pathMask * 0.18, 0.0, 1.0));
    if (apertureHalfExtent > 0.0)
    {
        vec3 paintedShallow = vec3(0.115, 0.205, 0.185);
        vec3 paintedMid = vec3(0.055, 0.160, 0.175);
        vec3 paintedDeep = vec3(0.025, 0.085, 0.135);
        vec3 paintedMedium = mix(paintedShallow, paintedMid,
                                 smoothstep(0.04, 0.52, depthT));
        paintedMedium = mix(paintedMedium, paintedDeep,
                            smoothstep(0.48, 0.96, depthT));
        paintedMedium *= mix(0.92, 1.08, structureField);
        medium = mix(medium, paintedMedium, 0.62);
    }

    e.density = densityMask;
    e.structure = structureField;
    e.midColumn = midMean;
    e.particulate = clamp(particulateMean * pathMask * strength, 0.0, 1.0);
    e.pathMask = pathMask;
    e.mediumColor = medium;
    e.meanPos = meanPos;
    return e;
}

void main()
{
    vec2 uv = clamp(vUV, vec2(0.0), vec2(1.0));
    vec3 sceneColor = texture(uSceneColor, uv).rgb;
    float depth01 = texture(uSceneDepth, uv).r;
    float waterDistProducer = texture(uWaterDist, uv).r;
    int debugMode = int(u.params0.y + 0.5);
    int waterVolumeCount = int(u.params0.z + 0.5);

    if (waterVolumeCount <= 0)
    {
        if (debugMode == 21 || debugMode == 22 || debugMode == 23)
        {
            oColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        oColor = vec4(sceneColor, 1.0);
        return;
    }

    WaterVolumeGPU volume = waterVolumes[0];
    vec3 bMin = volume.boundsMin.xyz;
    vec3 bMax = volume.boundsMax.xyz;
    float surfaceHeight = volume.boundsMin.w;
    int shape = int(volume.absorptionCoeff.w + 0.5);
    vec3 cappedMax = bMax;
    cappedMax.y = min(cappedMax.y, surfaceHeight);

    vec3 rayFar = reconstructWorldPos(uv, 1.0);
    vec3 rayVec = rayFar - u.camPos.xyz;
    float rayLen = length(rayVec);
    if (rayLen <= 1e-5 || any(lessThanEqual(cappedMax, bMin)))
    {
        if (debugMode == 21 || debugMode == 22 || debugMode == 23)
        {
            oColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        oColor = vec4(sceneColor, 1.0);
        return;
    }

    vec3 rayDir = rayVec / rayLen;
    vec2 waterHit =
        shape == WATER_VOLUME_SHAPE_FISHBOWL
            ? waterVolumeIntersectShape(shape, u.camPos.xyz, rayDir, bMin, bMax, surfaceHeight)
            : intersectAabb(u.camPos.xyz, rayDir, bMin, cappedMax);
    float sceneHitDist = 1.0e6;
    if (depth01 < 0.9999)
    {
        sceneHitDist = length(reconstructWorldPos(uv, depth01) - u.camPos.xyz);
    }

    float waterStart = max(waterHit.x, 0.0);
    float waterEnd = min(waterHit.y, sceneHitDist);
    float rayBoxPath = max(waterEnd - waterStart, 0.0);
    float waterPath = max(rayBoxPath, waterDistProducer);

    if (waterPath <= 0.001)
    {
        if (debugMode == 21 || debugMode == 22 || debugMode == 23)
        {
            oColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        oColor = vec4(sceneColor, 1.0);
        return;
    }

    float sampleStart = rayBoxPath > 0.001 ? waterStart : max(sceneHitDist - waterPath, 0.0);
    vec3 viewSamplePos = u.camPos.xyz + rayDir * (sampleStart + waterPath * 0.55);
    BodyEval body = evalWaterBody(u.camPos.xyz, rayDir, sampleStart, waterPath);

    vec3 absorption = volume.absorptionCoeff.xyz;
    vec3 sunDir = normalize(u.sunDirToSun.xyz);
    vec3 viewDir = normalize(u.camPos.xyz - viewSamplePos);
    float forwardScatterView = pow(max(dot(-viewDir, sunDir), 0.0), 3.0);
    float forwardScatter = forwardScatterView *
                           smoothstep(0.65, 8.0, waterPath) * max(u.params1.z, 0.0);

    float waterDepth = max(volume.boundsMin.w - bMin.y, 0.05);
    float depthT = clamp((volume.boundsMin.w - body.meanPos.y) / waterDepth, 0.0, 1.0);
    float shaftContrast = clamp(u.params2.y, 0.0, 1.0);
    float sunHeight = smoothstep(0.08, 0.62, sunDir.y);

    float apertureHalfExtent = clamp(u.params2.z, 0.0, 1.0);
    float authoredShaft = step(0.0001, apertureHalfExtent);
    float shaftScatter = max(u.params1.z, 0.0);
    shaftScatter = mix(shaftScatter, sqrt(clamp(shaftScatter, 0.0, 1.0)),
                       authoredShaft * 0.38);
    float viewResponse = mix(0.40 + forwardScatterView * 0.60,
                             0.70 + forwardScatterView * 0.30,
                             authoredShaft);
    float shaftMask = 0.0;
    if (authoredShaft > 0.5)
    {
        // V2.5 retains the complete visible-ray intersection from V2.4 while its
        // softer art response keeps the ribbons separated and highlight-safe.
        float integratedRibbons = evalStylizedSunroofShaftRay(
            u.camPos.xyz, rayDir, sampleStart, waterPath, bMin, bMax,
            surfaceHeight, sunDir, apertureHalfExtent, shaftContrast, u.params0.x);
        float visiblePath = smoothstep(0.08, 0.45, waterPath);
        shaftMask = integratedRibbons * visiblePath *
                    (0.38 + body.pathMask * 0.62) * viewResponse * sunHeight *
                    shaftScatter * max(u.params2.x, 0.0);
        shaftMask = min(shaftMask, 0.15);
    }
    else
    {
        // preserve the historical compatibility route without paying for it in
        // the authored V2.5 sunroof branch.
        vec2 sunXz = sunDir.xz;
        float sunXzLen = length(sunXz);
        sunXz = sunXzLen > 1e-4 ? sunXz / sunXzLen : vec2(0.707, 0.707);
        vec2 shaftAcross = vec2(-sunXz.y, sunXz.x);
        float alongSun = dot(body.meanPos.xz, sunXz);
        float acrossSun = dot(body.meanPos.xz, shaftAcross);
        float midWater = smoothstep(0.04, 0.22, depthT) *
                         (1.0 - smoothstep(0.78, 0.98, depthT));
        vec3 shaftP = vec3(acrossSun * 0.135 + u.params0.x * 0.006,
                           body.meanPos.y * 0.085 + u.params0.x * 0.010,
                           alongSun * 0.026 - u.params0.x * 0.003);
        float shaftNoise = valueNoise(shaftP);
        float shaftBands =
            0.5 + 0.5 * sin((acrossSun * 0.16 + shaftNoise * 1.35 +
                             u.params0.x * 0.008) * 6.2831853);
        shaftBands = pow(clamp(shaftBands, 0.0, 1.0),
                         mix(2.4, 3.5, shaftContrast));
        shaftBands = mix(mix(0.42, 0.10, shaftContrast), 1.0, shaftBands) *
                     mix(0.84, 1.12, shaftNoise);
        float shaftPath = smoothstep(1.4, 11.0, waterPath) *
                          (1.0 - exp(-waterPath * 0.095));
        shaftMask = shaftBands * shaftPath * midWater *
                    (0.35 + body.pathMask * 0.65) * viewResponse * sunHeight *
                    shaftScatter * max(u.params2.x, 0.0);
    }

    float strength = max(u.params1.x, 0.0);
    float pathRate = max(u.params1.y, 0.0);
    float pathFade = smoothstep(0.75, 7.50, waterPath);
    // beer-Lambert extinction along the water path. decoupled from body.density so the
    // medium reads as continuous rather than splotchy where the noise field is low.
    float extinctionScale = pathFade * (0.55 + strength * 0.95) *
                            mix(1.0, 0.88, authoredShaft);
    vec3 extinction = exp(-absorption * waterPath * extinctionScale);

    float inscatterPath = 1.0 - exp(-waterPath * (0.035 + pathRate * 0.155));
    float structureMod = mix(0.82, 1.22, body.structure);
    // constant base so inscatter is continuous across all valid water rays, with structure
    // and midColumn modulating how it varies. cap raised so it can read as actual medium.
    float inscatter = clamp(inscatterPath * strength * pathFade *
                                (0.22 + body.density * 0.40 + body.midColumn * 0.18) *
                                structureMod,
                            0.0, 0.55);
    inscatter *= mix(1.0, 0.78, authoredShaft);

    vec3 color = sceneColor * extinction;
    color = mix(color, body.mediumColor, inscatter);
    color += body.mediumColor * inscatter * 0.34;
    color += vec3(0.72, 0.90, 1.0) * forwardScatter * body.pathMask * 0.28;
    vec3 shaftColor = mix(body.mediumColor, vec3(0.76, 0.93, 1.0), 0.58);
    float shaftWarmth = clamp(u.params2.w, 0.0, 0.45);
    shaftColor = mix(shaftColor, vec3(1.0, 0.92, 0.72), shaftWarmth);
    vec3 paintedShaftColor = mix(vec3(0.56, 0.78, 0.74),
                                 vec3(0.92, 0.80, 0.62), shaftWarmth);
    shaftColor = mix(shaftColor, paintedShaftColor, authoredShaft);
    float brightestSource = max(max(color.r, color.g), color.b);
    float highlightHeadroom = 1.0 - smoothstep(0.72, 1.15, brightestSource);
    float shaftHighlightGuard = mix(1.0, mix(0.30, 1.0, highlightHeadroom), authoredShaft);
    color += shaftColor * shaftMask * mix(0.75, 0.50, authoredShaft) *
             shaftHighlightGuard;

    // scene-local painted depth separation: retain source color near the camera,
    // warm the upper water gently, and cool the lower column without laying a
    // uniform teal veil over fish and foliage.
    if (authoredShaft > 0.5)
    {
        float mediumPresence = pathFade * (1.0 - exp(-waterPath * 0.040));
        float paintedDepth = smoothstep(0.08, 0.94, depthT);
        vec3 depthBalance = mix(vec3(1.08, 1.02, 0.94),
                                vec3(0.91, 0.98, 1.07), paintedDepth);
        color *= mix(vec3(1.0), depthBalance, mediumPresence * 0.30);
        vec3 washLift = mix(vec3(0.055, 0.042, 0.018),
                            vec3(0.006, 0.020, 0.042), paintedDepth);
        color += washLift * mediumPresence * mix(0.78, 1.12, body.structure);
    }

    vec3 particulateColor = mix(vec3(0.82, 0.96, 1.0),
                                 vec3(0.92, 0.96, 0.78), authoredShaft * 0.45);
    float shaftMotePresence = authoredShaft * smoothstep(0.006, 0.060, shaftMask);
    float shaftMotePulse = 0.78 + 0.22 * sin(
        u.params0.x * 0.42 + dot(body.meanPos, vec3(0.31, 0.47, -0.23)));
    // V2.6 reuses the particulate field already accumulated by the fixed
    // eight-sample medium evaluation. only motes inside a painted ribbon gain
    // the warm, suspended highlight; no extra samples or noise octaves are added.
    float particulateBaseStrength = 0.045 + strength * 0.045;
    float particulateShaftStrength =
        (0.17 + strength * 0.075) * clamp(u.params0.w, 0.0, 2.0);
    float particulateStrength = mix(particulateBaseStrength,
                                    particulateShaftStrength,
                                    shaftMotePresence);
    color += particulateColor * body.particulate * particulateStrength *
             mix(1.0, shaftMotePulse, shaftMotePresence);

    if (debugMode == 21)
    {
        float pathVis = clamp(waterPath / 24.0, 0.0, 1.0);
        float producerVis = clamp(waterDistProducer / 24.0, 0.0, 1.0);
        float repaired = clamp((rayBoxPath - waterDistProducer) / 14.0, 0.0, 1.0);
        vec3 pathDebug = vec3(pathVis * 0.28);
        pathDebug += vec3(producerVis * 0.40, producerVis * 0.55, 0.0);
        pathDebug += vec3(repaired * 0.18, 0.0, repaired * 0.45);
        oColor = vec4(clamp(pathDebug, 0.0, 1.0), 1.0);
        return;
    }
    if (debugMode == 22)
    {
        vec3 densityDebug = vec3(body.density);
        densityDebug += vec3(0.0, body.midColumn * 0.28, 0.0);
        densityDebug += vec3(body.structure * 0.18, 0.0, (1.0 - body.structure) * 0.14);
        densityDebug += vec3(0.85, 0.96, 1.0) * body.particulate * 1.8;
        oColor = vec4(clamp(densityDebug, 0.0, 1.0), 1.0);
        return;
    }
    if (debugMode == 23)
    {
        vec3 delta = max(abs(color - sceneColor) - vec3(0.002), vec3(0.0)) * 1.25;
        oColor = vec4(clamp(delta + body.mediumColor * inscatter * 0.38 +
                                shaftColor * shaftMask * 1.6 +
                                vec3(0.82, 0.96, 1.0) * body.particulate * 0.45,
                            0.0, 1.0),
                      1.0);
        return;
    }

    oColor = vec4(color, 1.0);
}
