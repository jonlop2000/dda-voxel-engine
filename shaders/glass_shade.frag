#version 450
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vViewPos;

layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uBackDepth;
layout(set = 0, binding = 2) uniform sampler2D uSceneDepth;

layout(set = 1, binding = 0) uniform UBO {
    mat4 view;
    mat4 viewProj;
    vec4 camPos;
    vec4 tint;
    vec4 params0;
    vec4 params1;
    vec4 params2;
    vec4 params3;  // x=bubbleScale, y=bubbleIntensity, z=thicknessGate, w=chromaticSplit
    vec4 reflectionColor;
    vec4 waterWave0;  // x=waterLevel, y=time, z=waveScale, w=waveAmplitude
    vec4 waterWave1;  // x=rippleStrength, y=waterlineFalloff, z=verticalGateExp, w=enabled
    vec4 waterVolume0; // xyz=boundsMin, w=surfaceHeight
    vec4 waterVolume1; // xyz=boundsMax, w=shape enum
    vec4 sunDirToSun; // xyz=direction from glass toward sun
    vec4 sunColor;    // rgb=sun color multiplied by intensity
    vec4 atmosphere0; // density, height falloff, base height, sun phase strength
    vec4 atmosphere1; // x=active water-volume count
} u;

struct WaterVolumeGPU {
    vec4 boundsMin;       // xyz=min, w=surfaceHeight
    vec4 boundsMax;       // xyz=max, w=fogDensity
    vec4 absorptionCoeff; // xyz=absorption, w=shape enum
    vec4 deepColor;       // xyz=deep color, w=flags
};

layout(std430, set = 2, binding = 0) readonly buffer WaterVolumes {
    WaterVolumeGPU waterVolumes[];
};

#define SCENE_ATMOSPHERE_WATER_VOLUME_COUNT int(u.atmosphere1.x + 0.5)
#define SCENE_ATMOSPHERE_WATER_VOLUME(INDEX) waterVolumes[INDEX]

#include "water_volume_shape.glsl"
#include "water_waves.glsl"
#include "atmosphere_water_path.glsl"

// ============================================
// procedural bubble inclusions
// ============================================

vec3 hash33(vec3 p) {
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract(vec3(p.x * p.y, p.y * p.z, p.z * p.x));
}

float bubbleField(vec3 worldPos, float scale) {
    vec3 scaledPos = worldPos * scale;
    vec3 cellPos = floor(scaledPos);
    float minDist = 1.0;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            for (int z = -1; z <= 1; z++) {
                vec3 neighbor = cellPos + vec3(float(x), float(y), float(z));
                vec3 bubbleCenter = neighbor + hash33(neighbor) * 0.8 + 0.1;
                float dist = length(scaledPos - bubbleCenter);
                minDist = min(minDist, dist);
            }
        }
    }

    float bubble = 1.0 - smoothstep(0.05, 0.18, minDist);
    vec3 sparseSeed = hash33(cellPos + vec3(91.7, 23.4, 67.1));
    float sparseMask = step(0.7, sparseSeed.x);

    return bubble * sparseMask;
}

float fishbowlGlassContactGate(vec3 worldPos, vec3 boundsMin, vec3 boundsMax,
                               float surfaceHeight, float waterlineDelta, float falloff)
{
    vec3 center;
    vec3 radii;
    waterVolumeFishbowlEllipsoid(boundsMin, boundsMax, surfaceHeight, center, radii);
    float bowlRadius = max(min(radii.x, radii.z), 0.001);

    float lateralShellDist =
        abs(waterVolumeLateralBoundaryDistance(WATER_VOLUME_SHAPE_FISHBOWL, worldPos,
                                               boundsMin, boundsMax, surfaceHeight));
    float shellTolerance = max(0.36, bowlRadius * 0.026);
    float shellGate = 1.0 - smoothstep(shellTolerance, shellTolerance * 2.10,
                                       lateralShellDist);

    float surfaceSideDist =
        waterVolumeSurfaceEdgeDistance(WATER_VOLUME_SHAPE_FISHBOWL, worldPos,
                                       boundsMin, boundsMax, surfaceHeight);
    float surfaceRingGate =
        1.0 - smoothstep(bowlRadius * 0.010, bowlRadius * 0.085, surfaceSideDist);
    float lineYGate = 1.0 - smoothstep(0.0, max(falloff * 0.65, 0.16),
                                       abs(waterlineDelta));

    return clamp(max(shellGate, surfaceRingGate * lineYGate), 0.0, 1.0);
}

// ============================================
// thin-film interference (iridescence)
// ============================================
// simulates spectral color shift from constructive/destructive interference
// in a thin transparent film on the glass surface.

vec3 thinFilmIridescence(float cosTheta, float filmThickness, float frequency) {
    // optical path difference approximation
    float opd = filmThickness * (1.0 - cosTheta) * frequency;

    // rgb cosine waves at different frequencies for each wavelength
    // red: longest wavelength, blue: shortest wavelength
    vec3 color;
    color.r = 0.5 + 0.5 * cos(2.0 * 3.14159 * (opd * 1.0 + 0.00));
    color.g = 0.5 + 0.5 * cos(2.0 * 3.14159 * (opd * 1.2 + 0.33));
    color.b = 0.5 + 0.5 * cos(2.0 * 3.14159 * (opd * 1.4 + 0.67));

    return color;
}

void main() {
    vec2 uv = clamp(gl_FragCoord.xy * u.params1.xy, 0.0, 1.0);
    vec3 rawScene = texture(uSceneColor, uv).rgb;

    float frontDepth = max(-vViewPos.z, 0.0);
    float backDepth = texture(uBackDepth, uv).r;
    float thicknessScale = max(u.params2.x, 0.0);
    float thickness = max(backDepth - frontDepth, 0.0) * thicknessScale;
    float opticalThickness = thickness / (1.0 + thickness * 0.42);

    vec3 N = normalize(vNormal);
    vec3 V = normalize(u.camPos.xyz - vWorldPos);
    if (dot(N, V) < 0.0) {
        N = -N;
    }

    float ior = max(u.params0.z, 1.0001);
    float refractStrength = u.params0.y;
    float eta = 1.0 / ior;
    vec3 refractDir = refract(-V, N, eta);
    // project refracted direction difference to screen space
    // scale by thickness for physically plausible offset (thicker = more bend)
    vec2 refractOffset =
        (refractDir.xy + V.xy) * refractStrength * (1.0 + min(thickness, 2.25));
    float waterlineBand = 0.0;
    float waterlineRippleMask = 0.0;
    float dryPaneMask = 0.0;

    // waterline ripple distortion: sample the surface wave field and apply a falloff
    // that is strongest at the waterline and decays a few units below. only fires on
    // vertical-ish glass (side walls), so floor/ceiling glass is unaffected.
    if (u.waterWave1.w > 0.5) {
        float waterLevel = u.waterWave0.x;
        float waveTime = u.waterWave0.y;
        float waveScale = u.waterWave0.z;
        float waveAmpUi = max(u.waterWave0.w, 0.0);
        float waveAmp = min(waveAmpUi, 0.70);
        float waterlineDelta = vWorldPos.y - waterLevel;
        float belowWaterline = -waterlineDelta;
        float falloff = max(u.waterWave1.y, 0.001);
        // 1.0 right at the waterline, fading to 0 at `falloff` units below.
        float submergedGate = clamp(1.0 - max(belowWaterline, 0.0) / falloff, 0.0, 1.0);
        // skip pixels above the water surface entirely.
        submergedGate *= step(0.0, belowWaterline);
        // restrict to vertical glass — floor/ceiling have N.y near ±1, side walls near 0.
        float verticalGate = pow(clamp(1.0 - abs(N.y), 0.0, 1.0), max(u.waterWave1.z, 0.001));
        vec3 volumeMin = u.waterVolume0.xyz;
        vec3 volumeMax = u.waterVolume1.xyz;
        float volumeSurface = u.waterVolume0.w;
        int volumeShape = int(u.waterVolume1.w + 0.5);
        float shapeContactGate = 1.0;
        if (waterVolumeHasUsableBounds(volumeMin, volumeMax, volumeSurface) &&
            volumeShape == WATER_VOLUME_SHAPE_FISHBOWL) {
            shapeContactGate = fishbowlGlassContactGate(vWorldPos, volumeMin, volumeMax,
                                                        volumeSurface, waterlineDelta, falloff);
        }
        float lineWidth = max(0.045, falloff * 0.055);
        waterlineBand = (1.0 - smoothstep(0.0, lineWidth, abs(waterlineDelta))) *
                        verticalGate * shapeContactGate;
        float signedLineGate = 1.0 - smoothstep(lineWidth * 0.75, falloff * 0.42,
                                                abs(waterlineDelta));
        waterlineRippleMask =
            max(submergedGate * signedLineGate, waterlineBand * 0.42) *
            verticalGate * shapeContactGate;
        dryPaneMask = smoothstep(0.35, 2.35, waterlineDelta) * verticalGate;
        if (waterlineRippleMask > 0.001 && waveAmp > 1e-4) {
            vec3 waveNormal = computeWaveNormal(vWorldPos.xz, waveTime, waveScale, waveAmp);
            // the wave slope is ~0.01 at fishbowl wave params and this glass uses
            // refractStrength=0, so the raw warp (slope * rippleStrength 0.011) is sub-pixel.
            // apply an internal gain so the waterline visibly bends the refraction; this is
            // the only distortion the contact gets, so it must fire even when refractStrength=0.
            bool fishbowlRipple = waterVolumeHasUsableBounds(volumeMin, volumeMax,
                                                             volumeSurface) &&
                                   volumeShape == WATER_VOLUME_SHAPE_FISHBOWL;
            float waveEnergy = clamp(waveAmpUi / 1.35, 0.0, 1.0);
            float rippleGain = u.waterWave1.x * mix(34.0, fishbowlRipple ? 52.0 : 58.0,
                                                    waveEnergy);
            vec2 ripple = waveNormal.xz * rippleGain * waterlineRippleMask;
            refractOffset += ripple;
        }
    }

    vec2 refractUv = clamp(uv + refractOffset, 0.0, 1.0);
    float refractedDepth = texture(uSceneDepth, refractUv).r;
    float depthConflict = smoothstep(1e-4, 0.018, gl_FragCoord.z - refractedDepth);
    refractUv = mix(refractUv, uv, depthConflict * 0.95);
    vec3 sceneColor = texture(uSceneColor, refractUv).rgb;

    float NoV = max(dot(N, V), 0.0);
    float absorption = max(u.params0.x, 0.0);
    float trans = exp(-absorption * opticalThickness);
    float dryCenterMask = dryPaneMask * smoothstep(0.36, 0.82, NoV);
    trans = mix(trans, max(trans, 0.992), dryCenterMask * 0.86);
    vec3 tinted = mix(u.tint.rgb, sceneColor, trans);

    float r0 = (1.0 - ior) / (1.0 + ior);
    float F0 = r0 * r0;
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - NoV, 5.0);
    float rimVisibility = pow(clamp(1.0 - NoV, 0.0, 1.0), 1.85);
    fresnel = clamp(fresnel + rimVisibility * 0.030 + waterlineBand * 0.052, 0.0, 0.86);
    fresnel = mix(fresnel, fresnel * 0.38, dryCenterMask);

    // ── thin-FILM iridescence ──
    vec3 reflection = u.reflectionColor.rgb;
    float iridescentStrength = u.params2.y;
    if (iridescentStrength > 0.001) {
        float iridescentFilmThickness = u.params2.z;
        float iridescentFrequency = u.params2.w;

        vec3 iridescentColor = thinFilmIridescence(NoV, iridescentFilmThickness, iridescentFrequency);
        reflection = mix(reflection, iridescentColor, iridescentStrength);

        // slight fresnel boost so iridescence is visible at normal angles
        float iridescentFresnelBoost = iridescentStrength * 0.08;
        fresnel = max(fresnel, iridescentFresnelBoost);
    }

    vec3 color = mix(tinted, reflection, fresnel);
    color = mix(color, sceneColor, dryCenterMask * 0.68);
    vec3 edgeColor = mix(u.tint.rgb, vec3(0.78, 0.94, 1.0), 0.62);
    color += edgeColor * (rimVisibility * 0.024 + waterlineBand * 0.055 +
                          waterlineRippleMask * waterlineBand * 0.030);

    vec3 sunDir = normalize(u.sunDirToSun.xyz);
    vec3 sunRadiance = max(u.sunColor.rgb, vec3(0.0));
    vec3 halfVec = normalize(V + sunDir);
    float NoL = max(dot(N, sunDir), 0.0);
    float NoH = max(dot(N, halfVec), 0.0);
    float sunSpecPower = mix(96.0, 420.0, clamp(rimVisibility + waterlineBand * 0.35, 0.0, 1.0));
    float sunGlint = pow(NoH, sunSpecPower) * (0.20 + 0.80 * NoL);
    float sunRim = pow(NoL, 0.65) * rimVisibility;
    float sunWaterline = waterlineBand * (0.40 + 0.60 * NoL);
    float sunBackRim = pow(max(dot(-sunDir, V), 0.0), 3.0) * rimVisibility;
    color += sunRadiance * (sunGlint * 0.040 + sunRim * 0.014 +
                            sunWaterline * 0.018 + sunBackRim * 0.010);

    // ── bubble inclusions ──
    float bubbleIntensity = u.params3.y;
    if (bubbleIntensity > 0.001) {
        float bubbleScale = u.params3.x;
        float bubbleThicknessGate = u.params3.z;
        float bubbleChromaticSplit = u.params3.w;

        float b1 = bubbleField(vWorldPos, bubbleScale);
        float b2 = bubbleField(vWorldPos + vec3(7.13, 3.71, 11.51), bubbleScale * 1.7);
        float bubbles = max(b1, b2 * 0.6);

        // use unscaled thickness for gating (more intuitive control)
        float rawThickness = max(backDepth - frontDepth, 0.0);
        float bubbleMask = smoothstep(bubbleThicknessGate, bubbleThicknessGate + 0.1, rawThickness);
        bubbles *= bubbleMask;

        // slight view-angle enhancement: bubbles slightly brighter at grazing angles
        float viewAngle = 1.0 - NoV;  // NoV already computed for fresnel
        bubbles *= 0.7 + 0.3 * viewAngle;

        vec3 bubbleContribution;
        if (bubbleChromaticSplit > 0.001) {
            bubbleContribution.r = bubbles * (1.0 + bubbleChromaticSplit);
            bubbleContribution.g = bubbles * (1.0 - bubbleChromaticSplit * 0.5);
            bubbleContribution.b = bubbles * (1.0 + bubbleChromaticSplit * 1.5);
        } else {
            bubbleContribution = vec3(bubbles);
        }

        color += bubbleContribution * bubbleIntensity;
    }

    int debugMode = int(u.params0.w + 0.5);
    if (debugMode == 1) {
        float scale = max(u.params1.z, 0.0001);
        float vis = clamp(thickness / scale, 0.0, 1.0);
        color = vec3(vis);
    } else if (debugMode == 2) {
        float scale = max(u.params1.w, 0.0001);
        float vis = clamp(length(refractOffset) / scale, 0.0, 1.0);
        color = vec3(vis);
    } else if (debugMode == 3) {
        color = vec3(fresnel);
    } else if (debugMode == 4) {
        color = rawScene;
    } else if (debugMode == 5) {
        color = vec3(clamp(trans, 0.0, 1.0));
    } else if (debugMode == 6) {
        color = reflection * fresnel;
    } else if (debugMode == 7) {
        color = vec3(waterlineBand, waterlineRippleMask, rimVisibility);
    } else if (debugMode == 8) {
        // bubble field visualization
        float bubbleScale = u.params3.x;
        float b1 = bubbleField(vWorldPos, bubbleScale);
        float b2 = bubbleField(vWorldPos + vec3(7.13, 3.71, 11.51), bubbleScale * 1.7);
        float bubbles = max(b1, b2 * 0.6);
        color = vec3(bubbles);
    } else if (debugMode == 9) {
        // iridescence visualization
        float iridescentFilmThickness = u.params2.z;
        float iridescentFrequency = u.params2.w;
        color = thinFilmIridescence(NoV, iridescentFilmThickness, iridescentFrequency);
    }

    if (debugMode == 0 && u.atmosphere0.x > 0.0) {
        vec3 cameraToGlass = vWorldPos - u.camPos.xyz;
        float cameraToGlassDistance = length(cameraToGlass);
        float airOpticalDepth = sceneAtmosphereFiniteAirOpticalDepthWithWater(
            u.atmosphere0.x, u.atmosphere0.y, u.atmosphere0.z,
            u.camPos.xyz, cameraToGlass, cameraToGlassDistance);
        color = attenuateSceneAtmosphereSurfaceContribution(
            u.atmosphere0.x, airOpticalDepth, sceneColor, color);
    }

    oColor = vec4(color, 1.0);
}
