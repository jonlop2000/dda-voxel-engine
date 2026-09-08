#version 450
#extension GL_GOOGLE_include_directive : enable
#define SHADOW_CASCADE_COUNT 3
#include "area_light_common.glsl"
layout(location=0) in vec2 vUV;
layout(location=0) out vec4 oColor;

// g-buffer inputs.
layout(set=0, binding=0) uniform sampler2D gAlbedo;
layout(set=0, binding=1) uniform sampler2D gNormal;
layout(set=0, binding=2) uniform sampler2D gMaterial;
layout(set=0, binding=3) uniform sampler2D gDepth;
layout(set=0, binding=4) uniform sampler2DShadow shadowMap0;
layout(set=0, binding=5) uniform sampler2DShadow shadowMap1;
layout(set=0, binding=6) uniform sampler2DShadow shadowMap2;
layout(set=0, binding=7) uniform sampler2D uShadowResolved;  // day 12: temporal resolved
layout(set=0, binding=8) uniform sampler2D uShadowRaw;       // day 11: raw 1spp
layout(set=0, binding=9) uniform sampler2D uAOResolved;      // day 15: temporal resolved
layout(set=0, binding=10) uniform sampler2D uAORaw;          // day 15: raw ao
layout(set=0, binding=11) uniform sampler2D uWaterDist;      // r=water distance, g=material category
layout(set=0, binding=12) uniform sampler2D uLocalLightShadowResolved; // day 22: rgba local shadows

layout(set=1, binding=0) uniform UBO {
    mat4 invViewProj;
    mat4 viewProj;
    mat4 view;
    mat4 lightViewProj[SHADOW_CASCADE_COUNT];
    vec4 camPos;
    vec4 lightDir;
    vec4 lightColor;
    vec4 cascadeSplits;
    vec4 shadowMapSize;
    vec4 caustics0; // x=waterLevel, y=time, z=scale, w=speed
    vec4 caustics1; // x=intensity, y=banding, z=depthFade, w=enabled
    vec4 atmosphere0; // x=density, y=heightFalloff, z=baseHeight, w=sunPhaseStrength
    vec4 atmosphere1; // x=sunPhaseExponent, yzw=direction toward sun
    vec4 paintedSky0; // xyz=horizon tint, w=strength
    vec4 paintedSky1; // xyz=zenith tint, w=gradient exponent
    vec4 paintedSky2; // xyz=lower tint, w=horizon-band strength
    vec4 paintedSky3; // x=disc radius, y=softness, z=disc intensity, w=halo intensity
    vec4 paintedSky4; // x=halo exponent, y=horizon-band exponent
    vec4 paintedCloud0; // xyz=light tint, w=strength
    vec4 paintedCloud1; // xyz=shadow tint, w=coverage
    vec4 paintedCloud2; // x=altitude, y=world scale, z=opacity, w=softness
    vec4 paintedCloud3; // x=detail, y=silver lining, z/w=horizon fade
    vec4 paintedCloud4; // xy=wind-001 velocity, z=time, w=world seed
} u;

layout(std430, set=1, binding=1) readonly buffer Lights {
    AreaLightGpu lights[];
};

struct WaterVolumeGPU {
    vec4 boundsMin;       // xyz = min, w = surfaceHeight
    vec4 boundsMax;       // xyz = max, w = fogDensity
    vec4 absorptionCoeff; // xyz = coefficients
    vec4 deepColor;       // xyz = color
};

layout(std430, set=1, binding=2) readonly buffer WaterBuf {
    WaterVolumeGPU waterVolumes[];
};

layout(push_constant) uniform PC {
    int lightCount;
    int debugMode;
    int useDDAShadows;     // day 11: 0 = use csm, 1 = use dda shadow buffer
    int debugShadowMode;   // day 12: 0 = off, 1 = raw, 2 = resolved, 3 = csm, 4 = reject
    int csmDitherEnabled;  // csm dither toggle
    int csmEnabled;        // csm shadow toggle
    int aoEnabled;         // ao contribution toggle
    float aoContribution;  // hemisphere ambient strength (legacy field name)
    // day 12C: shadow terminator softening
    float terminatorSoftness;  // 0.0 = hard edge, 0.3 = soft
    int terminatorMode;        // 0 = off, 1 = wrap, 2 = smoothstep
    int waterVolumeCount;      // day 18
    int localShadowSlotCount;  // day 22: number of valid local shadow channels (0..4)
    int localShadowLightIndices[4]; // day 22: slot -> light index map (-1 means unused)
    float skyColorR;       // Sky/ambient color rgb
    float skyColorG;
    float skyColorB;
    float pixelShadowStyleStrength;
    int voxelCellVariationEnabled;
    float hemisphereAmbientStrength;
    float hemisphereSkyTintR;
    float hemisphereSkyTintG;
    float hemisphereSkyTintB;
    float hemisphereGroundTintR;
    float hemisphereGroundTintG;
    float hemisphereGroundTintB;
    float projectionJitterUvX;
    float projectionJitterUvY;
} pc;

#define SCENE_ATMOSPHERE_WATER_VOLUME_COUNT pc.waterVolumeCount
#define SCENE_ATMOSPHERE_WATER_VOLUME(INDEX) waterVolumes[INDEX]
#include "atmosphere_water_path.glsl"
#undef SCENE_ATMOSPHERE_WATER_VOLUME
#undef SCENE_ATMOSPHERE_WATER_VOLUME_COUNT
#include "painted_sky.glsl"
#include "painted_clouds.glsl"

vec3 applyResolvedSceneAtmosphere(vec3 surfaceColor, vec3 viewDirection,
                                 float opticalDepth)
{
    float transmittance;
    vec3 inscatter;
    vec3 skyTint = max(vec3(pc.skyColorR, pc.skyColorG, pc.skyColorB), vec3(0.0));
    vec3 sunTint = max(u.lightColor.rgb, vec3(0.0));
    evaluateSceneAtmosphereFromOpticalDepth(
        u.atmosphere0.x, u.atmosphere0.w, u.atmosphere1.x,
        opticalDepth, viewDirection, u.atmosphere1.yzw, skyTint, sunTint,
        transmittance, inscatter);
    return surfaceColor * transmittance + inscatter;
}

// interleaved gradient noise - deterministic noise from screen position
float interleavedGradientNoise(vec2 screenPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

// ============================================================================
// shadow terminator softening (day 12C)
// ============================================================================

float wrapNdotL(float NdotL, float wrap) {
    float w = max(wrap, 0.0);
    return max((NdotL + w) / (1.0 + w), 0.0);
}

float smoothNdotL(float NdotL, float softness) {
    float s = max(softness, 0.0001);
    return smoothstep(-s, s * 2.0, NdotL);
}

float halfLambertNdotL(float NdotL) {
    float hl = NdotL * 0.5 + 0.5;
    return hl * hl;
}

float schlickFresnel(float cosTheta, float f0) {
    float ct = clamp(cosTheta, 0.0, 1.0);
    float m = 1.0 - ct;
    float m2 = m * m;
    float m4 = m2 * m2;
    return f0 + (1.0 - f0) * m4 * m;
}

float softenTerminator(float rawNdotL, float softness, int mode) {
    float s = max(softness, 0.0);
    if (mode == 0 || s <= 0.0) {
        return max(rawNdotL, 0.0);
    }
    if (mode == 1) {
        return wrapNdotL(rawNdotL, s);
    }
    return smoothNdotL(rawNdotL, s);
}

vec3 applyWaterAbsorption(vec3 color, float waterDist, int volumeCount, float posY, float waterLevel)
{
    // skip if no water distance, no volumes, or geometry is above water level
    if (waterDist <= 0.001 || volumeCount <= 0 || posY >= waterLevel)
    {
        return color;
    }

    vec3 absorption = waterVolumes[0].absorptionCoeff.xyz;
    vec3 deepColor = waterVolumes[0].deepColor.xyz;
    float fogDensity = waterVolumes[0].boundsMax.w;

    vec3 transmission = exp(-absorption * waterDist);
    vec3 attenuated = color * transmission;
    float nearFieldClarity = 1.0 - smoothstep(0.75, 5.5, waterDist);
    attenuated = mix(attenuated, color, nearFieldClarity * 0.38);
    float fogFactor = 1.0 - exp(-fogDensity * waterDist);
    fogFactor *= smoothstep(1.25, 5.0, waterDist);
    return mix(attenuated, deepColor, clamp(fogFactor, 0.0, 1.0));
}

vec3 materialCategoryColor(float categoryValue)
{
    int category = int(floor(categoryValue + 0.5));
    if (category == 1) return vec3(0.32, 0.42, 0.54);  // frame
    if (category == 2) return vec3(0.82, 0.72, 0.46);  // gravel
    if (category == 3) return vec3(0.18, 0.72, 0.28);  // plant
    if (category == 4) return vec3(1.00, 0.46, 0.18);  // fish
    if (category == 5) return vec3(0.62, 0.64, 0.66);  // stone
    if (category == 6) return vec3(0.54, 0.34, 0.18);  // wood
    if (category == 7) return vec3(0.12, 0.82, 0.78);  // coral
    if (category == 8) return vec3(0.78, 0.92, 1.00);  // glass
    if (category == 9) return vec3(0.04, 0.42, 1.00);  // water
    if (category == 10) return vec3(0.88, 0.92, 0.98); // room
    return vec3(0.08, 0.08, 0.08);
}

int materialCategoryId(float categoryValue)
{
    return int(floor(categoryValue + 0.5));
}

float paintedMaterialStrength(float categoryValue)
{
    // opaque voxel and instanced-foliage passes use the bounded fractional
    // channel as an opt-in marker while the rounded integer remains category.
    return clamp(fract(max(categoryValue, 0.0)) / 0.25, 0.0, 1.0);
}

float softFoliageMaterialStrength(float categoryValue)
{
    if (materialCategoryId(categoryValue) != 3)
    {
        return 0.0;
    }
    // instanced V10 foliage stores a bounded opt-in marker in the fractional
    // part while retaining category 3 for every established material policy.
    return paintedMaterialStrength(categoryValue);
}

float softenPaintedShadowVisibility(float shadowVisibility, float categoryValue)
{
    float strength = paintedMaterialStrength(categoryValue);
    float shadow = clamp(shadowVisibility, 0.0, 1.0);
    float lifted = 0.30 + shadow * 0.70;
    return mix(shadow, lifted, strength * 0.68);
}

float softenFoliageShadowVisibility(float shadowVisibility, float categoryValue)
{
    float strength = softFoliageMaterialStrength(categoryValue);
    float shadow = clamp(shadowVisibility, 0.0, 1.0);
    float lifted = 0.42 + shadow * 0.58;
    return mix(shadow, lifted, strength * 0.48);
}

float materialDirectSpecularScale(float categoryValue)
{
    int category = materialCategoryId(categoryValue);
    if (category == 1) return 0.75; // frame
    if (category == 2) return 0.45; // gravel
    if (category == 3) return 0.30; // plant
    if (category == 4) return 1.20; // fish
    if (category == 5) return 0.45; // stone
    if (category == 6) return 0.35; // wood
    if (category == 7) return 0.75; // coral
    if (category == 8) return 1.00; // glass
    if (category == 9) return 0.85; // water
    if (category == 10) return 0.45; // room
    return 1.0;
}

float materialEnvironmentSpecularScale(float categoryValue)
{
    int category = materialCategoryId(categoryValue);
    if (category == 1) return 1.15; // frame
    if (category == 2) return 0.55; // gravel
    if (category == 3) return 0.35; // plant
    if (category == 4) return 1.20; // fish
    if (category == 5) return 0.50; // stone
    if (category == 6) return 0.45; // wood
    if (category == 7) return 0.75; // coral
    if (category == 8) return 1.00; // glass
    if (category == 9) return 0.90; // water
    if (category == 10) return 0.50; // room
    return 1.0;
}

float materialCausticScale(float categoryValue)
{
    int category = materialCategoryId(categoryValue);
    if (category == 1) return 0.35; // frame
    if (category == 2) return 1.30; // gravel
    if (category == 3) return 0.85; // plant
    if (category == 4) return 1.10; // fish
    if (category == 5) return 1.05; // stone
    if (category == 6) return 0.55; // wood
    if (category == 7) return 1.35; // coral
    if (category == 8) return 0.15; // glass
    if (category == 9) return 0.20; // water
    if (category == 10) return 0.45; // room
    return 1.0;
}

float materialCausticWarmth(float categoryValue)
{
    int category = materialCategoryId(categoryValue);
    if (category == 2) return 0.55; // gravel
    if (category == 3) return 0.35; // plant
    if (category == 4) return 0.42; // fish
    if (category == 5) return 0.50; // stone
    if (category == 7) return 0.40; // coral
    return 0.45;
}

float materialPixelShadowScale(float categoryValue)
{
    int category = materialCategoryId(categoryValue);
    if (category == 8 || category == 9) return 0.0;  // glass/water keep specialized paths
    if (category == 1 || category == 10) return 0.72; // frame/room
    if (category == 2) return 0.92; // gravel
    if (category == 3) return 0.66; // plant
    if (category == 4) return 0.62; // fish
    if (category == 5) return 0.96; // stone
    if (category == 6) return 0.92; // wood
    if (category == 7) return 0.72; // coral
    return 0.0;
}

float materialPixelLightScale(float categoryValue)
{
    int category = materialCategoryId(categoryValue);
    if (category == 8 || category == 9) return 0.0;
    if (category == 1 || category == 10) return 0.46;
    if (category == 2) return 0.66;
    if (category == 3) return 0.42;
    if (category == 4) return 0.44;
    if (category == 5) return 0.64;
    if (category == 6) return 0.60;
    if (category == 7) return 0.48;
    return 0.0;
}

float pixelBandValue(float value, float bandCount, float bias)
{
    float bands = max(bandCount, 1.0);
    return clamp(floor(clamp(value, 0.0, 1.0) * bands + bias) / bands, 0.0, 1.0);
}

float pixelStyleResponse(float rawStrength)
{
    float s = clamp(rawStrength, 0.0, 1.0);
    return clamp(max(s, smoothstep(0.0, 0.55, s) * 0.92), 0.0, 1.0);
}

float stablePixelHash(vec2 cell)
{
    return fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
}

float stableWorldHash(vec3 cell)
{
    return fract(sin(dot(cell, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

vec2 stableSurfacePixelCoord(vec3 pos, vec3 normal)
{
    vec3 an = abs(normal);
    if (an.y >= an.x && an.y >= an.z) return pos.xz;
    if (an.x >= an.z) return pos.zy;
    return pos.xy;
}

float materialShadowPixelDensity(float categoryValue)
{
    // cells per world unit. lower = bigger chunks. voxels are ~1 world unit,
    // so values < 1 produce multi-voxel-wide shadow blocks (the reference look).
    int category = materialCategoryId(categoryValue);
    if (category == 2) return 0.42; // gravel  (~2.4 unit cells)
    if (category == 4) return 0.80; // fish    (smaller, surface detail matters more)
    if (category == 5) return 0.40; // stone   (~2.5 unit cells)
    if (category == 6) return 0.36; // wood    (~2.8 unit cells)
    if (category == 3 || category == 7) return 0.55; // plant/coral
    return 0.40;                    // default (room/frame)  (~2.5 unit cells)
}

vec3 reconstructWorldPos(vec2 uv, float depth01);

ivec2 uvToTexel(vec2 uv, vec2 textureSizeF)
{
    ivec2 maxTexel = ivec2(max(textureSizeF - vec2(1.0), vec2(0.0)));
    return ivec2(clamp(floor(uv * textureSizeF), vec2(0.0), vec2(maxTexel)));
}

vec2 texelCenterUv(ivec2 texel, vec2 textureSizeF)
{
    return (vec2(texel) + vec2(0.5)) / max(textureSizeF, vec2(1.0));
}

float sampleResolvedShadowNearest(vec2 uv, vec2 shadowSize)
{
    ivec2 texel = uvToTexel(uv, shadowSize);
    return texelFetch(uShadowResolved, texel, 0).r;
}

vec3 quantizeShadowSurfacePoint(vec3 pos, vec3 normal, float density)
{
    float d = max(density, 0.001);
    vec3 an = abs(normal);

    if (an.y >= an.x && an.y >= an.z)
    {
        vec2 center = (floor(pos.xz * d) + vec2(0.5)) / d;
        return vec3(center.x, pos.y, center.y);
    }

    if (an.x >= an.z)
    {
        vec2 center = (floor(pos.zy * d) + vec2(0.5)) / d;
        return vec3(pos.x, center.y, center.x);
    }

    vec2 center = (floor(pos.xy * d) + vec2(0.5)) / d;
    return vec3(center.x, center.y, pos.z);
}

bool projectWorldToUv(vec3 worldPos, out vec2 projectedUv)
{
    vec4 clip = u.viewProj * vec4(worldPos, 1.0);
    if (clip.w <= 1e-5)
    {
        projectedUv = vec2(0.0);
        return false;
    }

    vec3 ndc = clip.xyz / clip.w;
    projectedUv = ndc.xy * 0.5 + vec2(0.5);
    if (ndc.z < 0.0 || ndc.z > 1.0)
    {
        return false;
    }
    return all(greaterThanEqual(projectedUv, vec2(0.0))) &&
           all(lessThanEqual(projectedUv, vec2(1.0)));
}

float projectedSurfaceCompatibility(vec2 projectedUv, vec3 projectedWorldPos, vec3 currentNormal)
{
    vec2 depthSize = vec2(textureSize(gDepth, 0));
    ivec2 texel = uvToTexel(projectedUv, depthSize);
    float projectedDepth = texelFetch(gDepth, texel, 0).r;
    if (projectedDepth >= 0.9999)
    {
        return 0.0;
    }

    vec3 sampleNormal = texelFetch(gNormal, texel, 0).rgb * 2.0 - 1.0;
    float sampleNormalLen2 = dot(sampleNormal, sampleNormal);
    if (sampleNormalLen2 <= 1e-4)
    {
        return 0.0;
    }

    sampleNormal *= inversesqrt(sampleNormalLen2);
    vec3 n = normalize(currentNormal);
    float normalCompatibility = smoothstep(0.52, 0.82, dot(sampleNormal, n));
    if (normalCompatibility <= 0.0)
    {
        return 0.0;
    }

    vec3 sampledWorldPos = reconstructWorldPos(texelCenterUv(texel, depthSize), projectedDepth);
    float planeError = abs(dot(sampledWorldPos - projectedWorldPos, n));
    float depthCompatibility = 1.0 - smoothstep(0.12, 0.45, planeError);

    return clamp(normalCompatibility * depthCompatibility, 0.0, 1.0);
}

float sampleChunkyResolvedShadow(vec2 uv, vec3 pos, vec3 normal, float baseShadow,
                                 float categoryValue)
{
    float globalStrength = pixelStyleResponse(pc.pixelShadowStyleStrength);
    float materialStrength = globalStrength * materialPixelShadowScale(categoryValue);
    if (materialStrength <= 0.0)
    {
        return baseShadow;
    }

    vec2 shadowSize = vec2(textureSize(uShadowResolved, 0));
    vec2 halfTexel = vec2(0.5) / max(shadowSize, vec2(1.0));
    float density = materialShadowPixelDensity(categoryValue);
    vec3 worldCellPos = quantizeShadowSurfacePoint(pos, normal, density);
    vec2 worldCellUv;
    if (!projectWorldToUv(worldCellPos, worldCellUv))
    {
        return baseShadow;
    }

    worldCellUv = clamp(worldCellUv, halfTexel, vec2(1.0) - halfTexel);
    float surfaceCompatibility =
        projectedSurfaceCompatibility(worldCellUv, worldCellPos, normal);
    float guardedBlend = smoothstep(0.05, 0.85, surfaceCompatibility);
    if (guardedBlend <= 0.0)
    {
        return baseShadow;
    }

    float chunkyShadow = sampleResolvedShadowNearest(worldCellUv, shadowSize);

    // ramp the chunky pass to dominate by the time global strength hits ~0.3 so
    // the slider's mid-range already reads as "voxel-block shadows" instead of a
    // soft wash. below ~0.05 we fade fully back to the smooth shadow buffer.
    float dominance = smoothstep(0.04, 0.32, globalStrength);
    float blendWeight = clamp(materialStrength * 1.55 + dominance * 0.30, 0.0, 0.99);
    blendWeight *= guardedBlend;
    return mix(baseShadow, chunkyShadow, blendWeight);
}

float applyPixelShadowStyle(float shadowFactor, vec3 pos, vec3 normal, float categoryValue)
{
    float globalStrength = pixelStyleResponse(pc.pixelShadowStyleStrength);
    float materialStrength = globalStrength * materialPixelShadowScale(categoryValue);
    if (materialStrength <= 0.0)
    {
        return shadowFactor;
    }

    float shadowAmount = 1.0 - clamp(shadowFactor, 0.0, 1.0);
    float shadowPresence = smoothstep(0.04, 0.55, shadowAmount);

    // ramp 4 -> 2 bands with strength. bias 0.5 keeps boundaries centered so the
    // banding is monotonic with the input (no "styling pushes lit pixels back
    // into light" inversions like the old hard-step mix produced).
    float bandCount = mix(4.0, 2.0, smoothstep(0.05, 0.45, globalStrength));
    float bandedAmount = pixelBandValue(shadowAmount, bandCount, 0.5);
    float styledShadow = 1.0 - bandedAmount;

    // strong blend so the banded value visibly dominates at moderate strengths.
    float dominance = smoothstep(0.04, 0.32, globalStrength);
    float blendWeight = clamp(materialStrength * (1.10 + 0.55 * shadowPresence)
                              + dominance * 0.20, 0.0, 0.96);
    return mix(shadowFactor, styledShadow, blendWeight);
}

float applyPixelLightStyle(float lightValue, float categoryValue)
{
    float globalStrength = pixelStyleResponse(pc.pixelShadowStyleStrength);
    float strength = globalStrength * materialPixelLightScale(categoryValue);
    if (strength <= 0.0)
    {
        return lightValue;
    }

    float bandCount = mix(5.0, 3.0, smoothstep(0.15, 0.85, globalStrength));
    float banded = pixelBandValue(lightValue, bandCount, 0.26);
    return mix(lightValue, banded, clamp(strength * 0.65, 0.0, 0.75));
}

float plantLeafFaceMask(vec3 normal)
{
    float horizontalFace = smoothstep(0.72, 0.96, abs(normal.y));
    return mix(1.0, 0.35, horizontalFace);
}

vec3 applyPlantAlbedoVariation(vec3 albedo, vec3 pos, vec3 normal, float categoryValue)
{
    if (materialCategoryId(categoryValue) != 3)
    {
        return albedo;
    }

    vec3 cell = floor((pos + normal * 0.045) * vec3(4.2, 3.2, 4.2));
    float h0 = stableWorldHash(cell);
    float h1 = stableWorldHash(cell + vec3(19.3, 7.1, 31.7));
    float h2 = stableWorldHash(cell + vec3(5.7, 23.9, 11.4));

    vec3 coolLeaf = vec3(0.80, 1.02, 0.92);
    vec3 warmLeaf = vec3(1.07, 1.04, 0.78);
    vec3 deepLeaf = vec3(0.72, 0.92, 1.05);
    vec3 tint = mix(coolLeaf, warmLeaf, h0);
    tint = mix(tint, deepLeaf, h1 * 0.28);

    float faceMask = plantLeafFaceMask(normal);
    float value = 1.0 + (h2 - 0.5) * 0.14;
    float strength = mix(0.05, 0.14, faceMask);
    return clamp(albedo * mix(vec3(1.0), tint * value, strength), vec3(0.0), vec3(1.35));
}

float plantWrapDiffuseExtra(float rawNdotL, vec3 normal, float categoryValue)
{
    if (materialCategoryId(categoryValue) != 3)
    {
        return 0.0;
    }

    float faceMask = plantLeafFaceMask(normal);
    float wrapped = wrapNdotL(rawNdotL, 0.54);
    float hard = max(rawNdotL, 0.0);
    float terminatorLift = max(wrapped - hard, 0.0);
    float backFacing = pow(clamp(-rawNdotL, 0.0, 1.0), 0.85);
    float soft = softFoliageMaterialStrength(categoryValue);
    float legacy = terminatorLift * 0.26 + backFacing * 0.08;
    float pastel = terminatorLift * 0.62 + backFacing * 0.18;
    return mix(legacy, pastel, soft) * faceMask;
}

vec3 plantTransmissionColor(vec3 albedo, vec3 normal, vec3 viewDir, vec3 sunColor,
                            float rawNdotL, float shadowFactor, float waterDist,
                            float categoryValue)
{
    if (materialCategoryId(categoryValue) != 3)
    {
        return vec3(0.0);
    }

    float faceMask = plantLeafFaceMask(normal);
    float backFacing = pow(clamp(-rawNdotL, 0.0, 1.0), 1.15);
    float edgeView = pow(clamp(1.0 - abs(dot(normal, viewDir)), 0.0, 1.0), 1.4);
    float wrapLift = max(wrapNdotL(rawNdotL, 0.62) - max(rawNdotL, 0.0), 0.0);
    float soft = softFoliageMaterialStrength(categoryValue);
    float softenedShadow = softenPaintedShadowVisibility(shadowFactor, categoryValue);
    softenedShadow = softenFoliageShadowVisibility(softenedShadow, categoryValue);
    float shadowVisibility = mix(0.22, 0.90, softenedShadow);
    float waterBoost = mix(0.78, 1.0, smoothstep(0.001, 0.35, waterDist));
    float transmission = (backFacing * mix(0.15, 0.28, soft) +
                          backFacing * edgeView * mix(0.16, 0.25, soft) +
                          wrapLift * mix(0.10, 0.18, soft)) *
                         faceMask * shadowVisibility * waterBoost;
    vec3 leafLight = mix(albedo, vec3(0.70, 1.00, 0.56), mix(0.45, 0.62, soft));
    return leafLight * sunColor * transmission;
}

float causticField(vec2 p, float timeSeconds, float speed)
{
    float a = sin((p.x + timeSeconds * speed) * 6.5);
    float b = sin((p.y - timeSeconds * speed * 1.3) * 7.1);
    float c = sin((p.x + p.y + timeSeconds * speed * 0.8) * 5.3);
    float d = sin((p.x - p.y - timeSeconds * speed * 0.6) * 8.2);
    return (a + b + c + d) * 0.125 + 0.5;
}

vec2 causticProjectionUv(vec3 pos, vec3 normal, vec3 sunL, float waterDist)
{
    vec2 floorUv = pos.xz;
    vec2 xSideUv = vec2(pos.z, pos.y * 0.82);
    vec2 zSideUv = vec2(pos.x, pos.y * 0.82);
    vec2 sideUv = (abs(normal.x) > abs(normal.z)) ? xSideUv : zSideUv;
    float sideBlend = smoothstep(0.28, 0.82, max(abs(normal.x), abs(normal.z)));

    vec2 sunSlide = sunL.xz * (0.28 + waterDist * 0.035);
    return mix(floorUv, sideUv, sideBlend * 0.72) + sunSlide;
}

vec3 reconstructWorldPos(vec2 uv, float depth01) {
    // reconstruct world position from depth.
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth01, 1.0);
    vec4 wpos = u.invViewProj * ndc;
    wpos /= wpos.w;
    return wpos.xyz;
}

vec2 vogelDiskSample(int sampleIndex, int sampleCount, float rotation) {
    float goldenAngle = 2.4;
    float r = sqrt(float(sampleIndex) + 0.5) / sqrt(float(sampleCount));
    float theta = float(sampleIndex) * goldenAngle + rotation;
    return vec2(r * cos(theta), r * sin(theta));
}

float sampleShadowFrom(sampler2DShadow map, vec2 suv, float sdepth, float bias, vec2 screenPos,
                       int cascadeIndex) {
    vec2 texel = u.shadowMapSize.zw;
    float rotation = (pc.csmDitherEnabled != 0)
                         ? interleavedGradientNoise(screenPos) * 6.28318
                         : 0.0;

    const int SAMPLE_COUNT = 16;
    float sum = 0.0;
    float sunSoftness = clamp(u.lightDir.w * 48.0, 0.0, 4.0);
    float diskRadius = 2.0 + sunSoftness + float(cascadeIndex) * 1.5;

    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        vec2 offset = vogelDiskSample(i, SAMPLE_COUNT, rotation) * diskRadius;
        sum += texture(map, vec3(suv + offset * texel, sdepth - bias));
    }

    return sum / float(SAMPLE_COUNT);
}

float sampleShadow(int cascadeIndex, vec3 pos, vec3 normal, float NdotL, vec2 screenPos) {
    vec3 posOffset = pos + normal * 0.002;
    vec4 lightClip = u.lightViewProj[cascadeIndex] * vec4(posOffset, 1.0);
    vec3 ndc = lightClip.xyz / max(lightClip.w, 0.0001);
    vec2 suv = ndc.xy * 0.5 + 0.5;
    float sdepth = ndc.z;

    float bias = max(0.0025 * (1.0 - NdotL), 0.0008);

    if (cascadeIndex == 0) {
        return sampleShadowFrom(shadowMap0, suv, sdepth, bias, screenPos, 0);
    }
    if (cascadeIndex == 1) {
        return sampleShadowFrom(shadowMap1, suv, sdepth, bias, screenPos, 1);
    }
    return sampleShadowFrom(shadowMap2, suv, sdepth, bias, screenPos, 2);
}

vec3 evaluateHemisphereAmbientTint(vec3 normal)
{
    float strength = clamp(pc.hemisphereAmbientStrength, 0.0, 1.0);
    if (strength <= 0.0)
    {
        return vec3(1.0);
    }

    vec3 skyTint = max(
        vec3(pc.hemisphereSkyTintR, pc.hemisphereSkyTintG, pc.hemisphereSkyTintB),
        vec3(0.0));
    vec3 groundTint = max(
        vec3(pc.hemisphereGroundTintR, pc.hemisphereGroundTintG,
             pc.hemisphereGroundTintB),
        vec3(0.0));
    float skyWeight = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 hemisphereTint = mix(groundTint, skyTint, skyWeight);
    return mix(vec3(1.0), hemisphereTint, strength);
}

vec3 evaluateHemisphereAmbientColor(vec3 legacyAmbientColor, vec3 normal)
{
    if (pc.hemisphereAmbientStrength <= 0.0)
    {
        // exact compatibility path: preserve the former flat sky-color term.
        return legacyAmbientColor;
    }
    return legacyAmbientColor * evaluateHemisphereAmbientTint(normal);
}

int chooseCascade(float depth) {
    int c = 0;
    if (depth > u.cascadeSplits.x) {
        c = 1;
    }
    if (depth > u.cascadeSplits.y) {
        c = 2;
    }
    return c;
}

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);

    float depth01 = texture(gDepth, uv).r;
    float glassMaskEarly = texture(gAlbedo, uv).a;

    if (pc.debugShadowMode == 1 || pc.debugShadowMode == 2) {
        float shadowVisibility = 1.0;
        if (pc.useDDAShadows != 0) {
            shadowVisibility = pc.debugShadowMode == 1
                                   ? texture(uShadowRaw, uv).r
                                   : texture(uShadowResolved, uv).r;
        }
        oColor = vec4(vec3(shadowVisibility), 1.0);
        return;
    }

    // sky early-out, but not for glass pixels (they have depth=1.0 but need processing)
    if (depth01 >= 0.9999 && glassMaskEarly < 0.5) {
        // lighting reconstructs geometry from the jittered depth grid. procedural sky and
        // cloud features have no g-buffer motion vector, so sampling them on that grid makes
        // their hard world-space facets follow the halton offset and defeats taa history.
        // undo only the projection jitter for the background ray; geometry reconstruction
        // below intentionally keeps using the jittered inverse view-projection.
        vec2 backgroundUv = clamp(
            uv - vec2(pc.projectionJitterUvX, pc.projectionJitterUvY),
            vec2(0.0), vec2(1.0));
        vec3 farWorld = reconstructWorldPos(backgroundUv, 1.0);
        vec3 viewDirection = normalize(farWorld - u.camPos.xyz);
        vec3 sky = evaluatePaintedSky(
            vec3(pc.skyColorR, pc.skyColorG, pc.skyColorB), viewDirection,
            u.atmosphere1.yzw, u.lightColor.rgb * max(u.lightColor.a, 0.0),
            u.paintedSky0, u.paintedSky1, u.paintedSky2, u.paintedSky3,
            u.paintedSky4);
        sky = evaluatePaintedClouds(
            sky, u.camPos.xyz, viewDirection, u.atmosphere1.yzw,
            u.paintedCloud0, u.paintedCloud1, u.paintedCloud2,
            u.paintedCloud3, u.paintedCloud4);
        if (u.atmosphere0.x > 0.0)
        {
            float opticalDepth = sceneAtmosphereSkyAirOpticalDepthWithWater(
                u.atmosphere0.x, u.atmosphere0.y, u.atmosphere0.z,
                u.camPos.xyz, viewDirection);
            sky = applyResolvedSceneAtmosphere(sky, viewDirection, opticalDepth);
        }
        oColor = vec4(sky, 1.0);
        return;
    }

    // glass-only pixels (depth=1.0, glassMask=1.0): apply fresnel fallback and return
    if (depth01 >= 0.9999 && glassMaskEarly > 0.5) {
        vec3 skyColor = vec3(pc.skyColorR, pc.skyColorG, pc.skyColorB);
        vec3 n = texture(gNormal, uv).rgb * 2.0 - 1.0;
        n = normalize(n);
        // for glass-only, we don't have proper world pos, use normal-based view approximation
        vec3 V = vec3(0.0, 0.0, 1.0);  // approximate view as forward
        float NoV = max(dot(n, V), 0.0);
        float glassF0 = 0.04;
        float glassFresnel = schlickFresnel(NoV, glassF0);
        // strong minimum visibility: at least 8% + fresnel contribution
        float reflectionMix = max(glassFresnel, 0.08);
        // use contrasting reflection color (bright white-ish) against sky
        vec3 reflectionColor = vec3(0.6, 0.7, 0.9);
        vec3 glassColor = mix(skyColor, reflectionColor, reflectionMix);
        oColor = vec4(glassColor, 1.0);
        return;
    }

    vec3 pos = reconstructWorldPos(uv, depth01);
    vec4 viewPos = u.view * vec4(pos, 1.0);
    float viewDepth = -viewPos.z;
    int cascadeIndex = chooseCascade(viewDepth);
    if (pc.debugMode == 2) {
        vec3 cascadeColor = vec3(0.0);
        if (cascadeIndex == 0) {
            cascadeColor = vec3(1.0, 0.0, 0.0);
        } else if (cascadeIndex == 1) {
            cascadeColor = vec3(0.0, 1.0, 0.0);
        } else {
            cascadeColor = vec3(0.0, 0.0, 1.0);
        }
        oColor = vec4(cascadeColor, 1.0);
        return;
    }

    vec3 n = texture(gNormal, uv).rgb * 2.0 - 1.0;
    n = normalize(n);

    vec4 mat = texture(gMaterial, uv);
    float rough = clamp(mat.r, 0.02, 1.0);
    float metallic = clamp(mat.g, 0.0, 1.0);
    float aoMat = clamp(mat.b, 0.0, 1.0);
    float emissive = max(mat.a, 0.0);
    float aoResolved = (pc.aoEnabled != 0) ? clamp(texture(uAOResolved, uv).r, 0.0, 1.0) : 1.0;
    float ao = aoMat * aoResolved;
    vec2 waterMeta = texture(uWaterDist, uv).rg;
    float waterDist = waterMeta.r;
    float materialCategory = waterMeta.g;
    float paintedMaterial = paintedMaterialStrength(materialCategory);
    float softFoliage = softFoliageMaterialStrength(materialCategory);
    vec3 albedo = texture(gAlbedo, uv).rgb;
    if (pc.voxelCellVariationEnabled == 0)
    {
        // compatibility path. the unified integer-cell path applies plant
        // variation in the obb g-buffer and must not receive this legacy
        // reconstructed-world-position variation a second time.
        albedo = applyPlantAlbedoVariation(albedo, pos, n, materialCategory);
    }
    // the painted profile deliberately keeps its texture, hue families, and
    // lighting response intact while lowering only material value. at the
    // outdoor profile's 0.82 marker this is a subtle ~9.8% albedo reduction;
    // compatibility materials remain byte-for-byte unchanged.
    albedo *= mix(1.0, 0.88, paintedMaterial);
    float greenDominance = smoothstep(
        0.015, 0.16,
        albedo.g - max(albedo.r * 0.92, albedo.b * 1.04));
    float greenSaturation = smoothstep(
        0.05, 0.22,
        max(albedo.r, max(albedo.g, albedo.b)) -
            min(albedo.r, min(albedo.g, albedo.b)));
    float paintedGreen = greenDominance * greenSaturation * paintedMaterial;
    // green terrain and vegetation receive one additional small value step;
    // neutral stone, warm wood, water, glass, and sky are unaffected.
    albedo *= mix(1.0, 0.96, paintedGreen);
    float directSpecularScale = materialDirectSpecularScale(materialCategory);
    float environmentSpecularScale = materialEnvironmentSpecularScale(materialCategory);
    directSpecularScale *= mix(1.0, 0.14, paintedMaterial);
    environmentSpecularScale *= mix(1.0, 0.10, paintedMaterial);
    float causticMaterialScale = materialCausticScale(materialCategory);
    float causticWarmth = materialCausticWarmth(materialCategory);
    float albedoLum = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    float darkMaterial = 1.0 - smoothstep(0.16, 0.36, albedoLum);
    float ambientAoStrength = mix(0.55, 0.25, darkMaterial);
    // preserve the painted profile's soft fill while leaving enough resolved
    // ao authority for its runtime controls to visibly shape contacts and
    // cavities. foliage stays slightly softer than the surrounding terrain.
    ambientAoStrength = mix(ambientAoStrength, 0.27, paintedMaterial);
    ambientAoStrength = mix(ambientAoStrength, 0.22, softFoliage);
    float ambientAo = (pc.aoEnabled != 0) ? mix(1.0, ao, ambientAoStrength) : 1.0;
    vec3 V = normalize(u.camPos.xyz - pos);

    vec3 diffuseColor = (1.0 - metallic) * albedo;
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float specPow = mix(128.0, 8.0, rough);

    vec3 skyColor = vec3(pc.skyColorR, pc.skyColorG, pc.skyColorB);
    vec3 hemisphereAmbientTintSignal = evaluateHemisphereAmbientTint(n);
    vec3 hemisphereAmbientColor = evaluateHemisphereAmbientColor(skyColor, n);
    vec3 ambientIrradiance =
        hemisphereAmbientColor * (pc.aoContribution * ambientAo);
    vec3 pastelAmbient = mix(vec3(0.68, 0.80, 0.64), skyColor, 0.38);
    vec3 paintedFill = mix(vec3(0.96, 0.76, 0.58), skyColor, 0.58);
    // these profile fills are ambient light too. route them through the same
    // bounded ao response so the ao intensity control can visibly shape
    // contacts without attenuating direct sun, specular, or emissive light.
    vec3 profileAmbientFill =
        paintedFill * (0.13 * paintedMaterial) +
        pastelAmbient * (0.10 * softFoliage);
    ambientIrradiance += profileAmbientFill * ambientAo;
    vec3 envDiffuse = diffuseColor * ambientIrradiance;
    float envSpecStrength = mix(0.02, 0.25, 1.0 - rough) * environmentSpecularScale;
    vec3 envSpec = F0 * envSpecStrength;
    vec3 emissiveColor = albedo * emissive * 3.0;
    vec3 envOnly = envDiffuse + envSpec + emissiveColor;

    vec3 color = envOnly;
    vec3 diffuseAccum = envDiffuse;
    vec3 specAccum = envSpec;
    float causticsDebug = 0.0;
    float heat = 0.0;

    vec3 sunL = normalize(-u.lightDir.xyz);
    float sunRawNdotL = dot(n, sunL);
    float sunNdotLHard = max(sunRawNdotL, 0.0);
    float sunNdotL = softenTerminator(sunRawNdotL, pc.terminatorSoftness, pc.terminatorMode);
    sunNdotL = applyPixelLightStyle(sunNdotL, materialCategory);
    vec2 screenPos = gl_FragCoord.xy;

    // day 12: toggle between dda resolved shadow and csm
    float shadowFactor;
    if (pc.useDDAShadows != 0) {
        // sample temporally resolved shadow
        shadowFactor = texture(uShadowResolved, uv).r;
        shadowFactor = sampleChunkyResolvedShadow(uv, pos, n, shadowFactor, materialCategory);
        shadowFactor = applyPixelShadowStyle(shadowFactor, pos, n, materialCategory);
    } else if (pc.csmEnabled != 0) {
        // existing csm path
        shadowFactor = sampleShadow(cascadeIndex, pos, n, sunNdotLHard, screenPos);
    } else {
        // csm disabled
        shadowFactor = 1.0;
    }
    shadowFactor = softenPaintedShadowVisibility(shadowFactor, materialCategory);
    if (softFoliage > 0.0)
    {
        shadowFactor = softenFoliageShadowVisibility(shadowFactor, materialCategory);
    }

    // day 12: debug visualization
    if (pc.debugShadowMode == 3) {
        // show csm shadow for comparison
        float csmShadow = (pc.csmEnabled != 0)
                              ? sampleShadow(cascadeIndex, pos, n, sunNdotLHard, screenPos)
                              : 1.0;
        oColor = vec4(vec3(csmShadow), 1.0);
        return;
    } else if (pc.debugShadowMode == 4) {
        float mask = texture(uShadowResolved, uv).r;
        oColor = vec4(vec3(mask), 1.0);
        return;
    }

    if (pc.debugMode == 6) {
        float aoRaw = (pc.aoEnabled != 0) ? texture(uAORaw, uv).r : 1.0;
        oColor = vec4(vec3(aoRaw), 1.0);
        return;
    } else if (pc.debugMode == 7) {
        float aoVis = (pc.aoEnabled != 0) ? texture(uAOResolved, uv).r : 1.0;
        oColor = vec4(vec3(aoVis), 1.0);
        return;
    }

    vec3 sunColor = u.lightColor.rgb * u.lightColor.w;

    float plantWrap = plantWrapDiffuseExtra(sunRawNdotL, n, materialCategory);
    float paintedWrappedDiffuse = clamp((sunRawNdotL + 0.42) / 1.42, 0.0, 1.0);
    sunNdotL = mix(sunNdotL, paintedWrappedDiffuse, paintedMaterial * 0.52);
    float foliageWrappedDiffuse = clamp((sunRawNdotL + 0.85) / 1.85, 0.0, 1.0);
    float foliageDiffuse = mix(sunNdotL, foliageWrappedDiffuse, softFoliage * 0.74);
    vec3 sunDiffuse = diffuseColor * clamp(foliageDiffuse + plantWrap, 0.0, 1.0) * sunColor * shadowFactor;
    vec3 plantTransmission =
        plantTransmissionColor(albedo, n, V, sunColor, sunRawNdotL, shadowFactor, waterDist, materialCategory);
    vec3 sunH = normalize(sunL + V);
    float sunSpec = pow(max(dot(n, sunH), 0.0), specPow);
    vec3 sunSpecCol = F0 * sunSpec * sunColor * shadowFactor * directSpecularScale;
    color += sunDiffuse + plantTransmission + sunSpecCol;
    diffuseAccum += sunDiffuse + plantTransmission;
    specAccum += sunSpecCol;

    if (u.caustics1.w > 0.5 && waterDist > 0.001) {
        // only apply caustics inside water volume xz bounds
        vec3 wBoundsMin = waterVolumes[0].boundsMin.xyz;
        vec3 wBoundsMax = waterVolumes[0].boundsMax.xyz;
        float surfaceHeight = waterVolumes[0].boundsMin.w;
        int shape = int(waterVolumes[0].absorptionCoeff.w + 0.5);
        bool inWaterBounds =
            shape == WATER_VOLUME_SHAPE_FISHBOWL
                ? waterVolumeContainsPoint(shape, pos, wBoundsMin, wBoundsMax, surfaceHeight)
                : (pos.x >= wBoundsMin.x && pos.x <= wBoundsMax.x &&
                   pos.z >= wBoundsMin.z && pos.z <= wBoundsMax.z);

        if (inWaterBounds) {
            float waterLevel = u.caustics0.x;
            float timeSeconds = u.caustics0.y;
            float scale = max(u.caustics0.z, 0.001);
            float speed = max(u.caustics0.w, 0.0);
            float intensity = max(u.caustics1.x, 0.0);
            float banding = clamp(u.caustics1.y, 0.0, 1.0);
            float depthFadeDist = max(u.caustics1.z, 0.001);

            float underwaterMask = 1.0 - smoothstep(waterLevel - 0.2, waterLevel + 0.25, pos.y);
            float upwardMask = clamp(n.y, 0.0, 1.0);
            float sideWeight = mix(0.35, 1.0, upwardMask);
            float downPenalty = smoothstep(-0.85, -0.05, n.y);
            float normalWeight = max(sideWeight * downPenalty, 0.20);
            float depthFade = exp(-waterDist / depthFadeDist);
            float readabilityTail = exp(-waterDist / (depthFadeDist * 2.8));
            float depthReadable = max(depthFade, readabilityTail * 0.40);
            float sunMask = mix(0.35, 1.0, clamp(sunNdotLHard * shadowFactor, 0.0, 1.0));

            vec2 projectedUv = causticProjectionUv(pos, n, sunL, waterDist);
            float field = causticField(projectedUv * scale, timeSeconds, speed);
            float fineField =
                causticField((projectedUv * 1.73 + vec2(7.1, -3.8)) * scale,
                             timeSeconds + 11.0, speed * 0.63);
            field = mix(field, fineField, 0.24);

            float threshold = mix(0.20, 0.78, banding);
            float bandWidth = mix(0.14, 0.035, banding);
            float coreBand = smoothstep(threshold - bandWidth, threshold + bandWidth, field);
            float softVeil =
                smoothstep(threshold - bandWidth * 2.8, threshold + bandWidth * 4.0, field) *
                (0.24 * (1.0 - banding));
            float band = clamp(max(coreBand, softVeil), 0.0, 1.0);

            float readabilityBoost = mix(1.0, 1.4, 1.0 - depthReadable);
            float brightSurfaceLimiter = mix(1.0, 0.55, smoothstep(0.54, 0.92, albedoLum));
            float causticEnergy =
                band * underwaterMask * normalWeight * depthReadable * sunMask * intensity * readabilityBoost *
                causticMaterialScale * brightSurfaceLimiter;
            vec3 causticLight = mix(vec3(0.82, 0.96, 1.0), vec3(1.0, 0.95, 0.82), causticWarmth);
            vec3 causticTint = mix(diffuseColor, causticLight, 0.56);
            vec3 causticColor = causticTint * sunColor * (0.40 + 0.60 * field) * causticEnergy;
            color += causticColor;
            diffuseAccum += causticColor;
            causticsDebug = causticEnergy;
        }
    }

    int count = clamp(pc.lightCount, 0, 2048);
    for (int i = 0; i < count; ++i) {
        AreaLightGpu light = lights[i];
        vec3 lightCenter = light.posRadius.xyz;
        float influenceRadius = max(light.posRadius.w, 0.001);
        float centerDist = length(lightCenter - pos);
        if (centerDist > influenceRadius) {
            continue;
        }

        vec3 closestPt = closestPointOnLight(light, pos);
        vec3 toClosest = closestPt - pos;
        float closestDist = length(toClosest);
        vec3 L = toClosest / max(closestDist, 0.001);

        float window = 1.0 - pow(clamp(centerDist / influenceRadius, 0.0, 1.0), 4.0);
        window = max(window * window, 0.0);
        float att = window / (closestDist * closestDist + 0.01);

        float rawNdotL = dot(n, L);
        float NdotL = softenTerminator(rawNdotL, pc.terminatorSoftness, pc.terminatorMode);
        NdotL = clamp(NdotL + plantWrapDiffuseExtra(rawNdotL, n, materialCategory) * 0.65, 0.0, 1.0);
        vec3 lightColor = light.colorIntensity.rgb * light.colorIntensity.w;

        float localWrapped = clamp((rawNdotL + 0.72) / 1.72, 0.0, 1.0);
        float paintedLocalWrapped = clamp((rawNdotL + 0.38) / 1.38, 0.0, 1.0);
        NdotL = mix(NdotL, paintedLocalWrapped, paintedMaterial * 0.46);
        NdotL = mix(NdotL, localWrapped, softFoliage * 0.58);
        vec3 diff = diffuseColor * NdotL * lightColor * att;
        float srcRadius = sourceRadius(light);
        float angularSize = srcRadius / max(closestDist, 0.01);
        float effectiveRough = clamp(rough + angularSize * 0.5, 0.02, 1.0);
        float localSpecPow = mix(128.0, 8.0, effectiveRough);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(n, H), 0.0), localSpecPow);
        vec3 specCol = F0 * spec * lightColor * att * directSpecularScale;

        float localShadow = 1.0;
        for (int slot = 0; slot < pc.localShadowSlotCount && slot < 4; ++slot) {
            if (pc.localShadowLightIndices[slot] == i) {
                localShadow = texture(uLocalLightShadowResolved, uv)[slot];
                break;
            }
        }

        localShadow = softenPaintedShadowVisibility(localShadow, materialCategory);
        if (softFoliage > 0.0)
        {
            localShadow = softenFoliageShadowVisibility(localShadow, materialCategory);
        }
        color += (diff + specCol) * localShadow;
        diffuseAccum += diff * localShadow;
        specAccum += specCol * localShadow;
        heat += att * light.colorIntensity.w;
    }

    vec3 absorbedColor = applyWaterAbsorption(color, waterDist, pc.waterVolumeCount, pos.y, u.caustics0.x);

    // =========================================
    // voxel glass fallback (when refract pass is disabled)
    // =========================================
    float glassMask = texture(gAlbedo, uv).a;
    if (glassMask > 0.5) {
        // basic fresnel reflection fallback - ensures glass is visible
        float NoV = max(dot(n, V), 0.0);
        float glassF0 = 0.04;  // standard glass F0
        float glassFresnel = schlickFresnel(NoV, glassF0);
        // strong minimum visibility: at least 8% + fresnel contribution
        float reflectionMix = max(glassFresnel, 0.08);
        // use contrasting reflection color for visibility
        vec3 glassReflectionColor = vec3(0.6, 0.7, 0.9);
        // blend: show background through glass plus reflection
        absorbedColor = mix(absorbedColor, glassReflectionColor, reflectionMix);
    }

    if (u.atmosphere0.x > 0.0)
    {
        vec3 cameraToSurface = pos - u.camPos.xyz;
        float viewDistance = length(cameraToSurface);
        if (viewDistance > 0.0)
        {
            vec3 viewDirection = cameraToSurface / viewDistance;
            float opticalDepth = sceneAtmosphereFiniteAirOpticalDepthWithWater(
                u.atmosphere0.x, u.atmosphere0.y, u.atmosphere0.z,
                u.camPos.xyz, viewDirection, viewDistance);
            absorbedColor = applyResolvedSceneAtmosphere(
                absorbedColor, viewDirection, opticalDepth);
        }
    }

    // optional heatmap debug.
    if (pc.debugMode == 1) {
        float h = clamp(heat / max(1.0, float(count)), 0.0, 1.0);
        oColor = vec4(vec3(h), shadowFactor);
    } else if (pc.debugMode == 3) {
        oColor = vec4(diffuseAccum, shadowFactor);
    } else if (pc.debugMode == 4) {
        oColor = vec4(specAccum, shadowFactor);
    } else if (pc.debugMode == 5) {
        oColor = vec4(envOnly, shadowFactor);
    } else if (pc.debugMode == 8) {
        oColor = vec4(vec3(causticsDebug), 1.0);
    }
    else if (pc.debugMode == 9) {
        float s = texture(uLocalLightShadowResolved, uv).r;
        oColor = vec4(vec3(s), 1.0);
    }
    else if (pc.debugMode == 10) {
        float s = texture(uLocalLightShadowResolved, uv).g;
        oColor = vec4(vec3(s), 1.0);
    }
    else if (pc.debugMode == 11) {
        float s = texture(uLocalLightShadowResolved, uv).b;
        oColor = vec4(vec3(s), 1.0);
    }
    else if (pc.debugMode == 12) {
        float s = texture(uLocalLightShadowResolved, uv).a;
        oColor = vec4(vec3(s), 1.0);
    }
    else if (pc.debugMode == 13) {
        oColor = vec4(vec3(rough), 1.0);
    }
    else if (pc.debugMode == 14) {
        oColor = vec4(vec3(metallic), 1.0);
    }
    else if (pc.debugMode == 15) {
        oColor = vec4(emissiveColor, 1.0);
    }
    else if (pc.debugMode == 16) {
        oColor = vec4(materialCategoryColor(materialCategory), 1.0);
    }
    else if (pc.debugMode == 17) {
        // neutral unit tint is middle gray; directional color separation remains
        // readable without being attenuated by ao or the ambient contribution.
        oColor = vec4(hemisphereAmbientTintSignal * 0.5, 1.0);
    }
    else if (pc.debugMode == 18) {
        // final material-aware multiplier applied to all ambient lighting.
        // white is unoccluded; darker values are the actual composite response.
        oColor = vec4(vec3(ambientAo), 1.0);
    }
    // day 12 debug: edge flickering diagnostic modes
    else if (pc.debugMode == 20) {
        // raw NdotL visualization (red=back-facing, green=lit, yellow=terminator)
        float rawNdotL = dot(n, sunL);
        vec3 debugColor;
        if (rawNdotL < -0.01) {
            debugColor = vec3(abs(rawNdotL), 0.0, 0.0);  // red = back-facing
        } else if (rawNdotL > 0.01) {
            debugColor = vec3(0.0, rawNdotL, 0.0);  // green = lit
        } else {
            debugColor = vec3(1.0, 1.0, 0.0);  // yellow = terminator zone
        }
        oColor = vec4(debugColor, 1.0);
    }
    else if (pc.debugMode == 21) {
        // NdotL as grayscale (easier to see subtle changes/flickering)
        float rawNdotL = dot(n, sunL);
        float vis = rawNdotL * 0.5 + 0.5;
        oColor = vec4(vis, vis, vis, 1.0);
    }
    else if (pc.debugMode == 22) {
        // normal edge detection (where normals differ from neighbors)
        vec2 texelSize = 1.0 / vec2(textureSize(gNormal, 0));
        vec3 nC = texture(gNormal, uv).rgb * 2.0 - 1.0;
        vec3 nL = texture(gNormal, uv - vec2(texelSize.x, 0)).rgb * 2.0 - 1.0;
        vec3 nR = texture(gNormal, uv + vec2(texelSize.x, 0)).rgb * 2.0 - 1.0;
        vec3 nU = texture(gNormal, uv - vec2(0, texelSize.y)).rgb * 2.0 - 1.0;
        vec3 nD = texture(gNormal, uv + vec2(0, texelSize.y)).rgb * 2.0 - 1.0;

        float edgeness = length(nC - nL) + length(nC - nR) + length(nC - nU) + length(nC - nD);
        edgeness = clamp(edgeness * 2.0, 0.0, 1.0);
        oColor = vec4(edgeness, edgeness, edgeness, 1.0);
    }
    else if (pc.debugMode == 23) {
        // NdotL sensitivity (flicker risk - red = high sensitivity)
        float baseNdotL = dot(n, sunL);

        vec3 perturbX = normalize(n + vec3(0.01, 0, 0));
        vec3 perturbY = normalize(n + vec3(0, 0.01, 0));
        vec3 perturbZ = normalize(n + vec3(0, 0, 0.01));

        float sensitivity = abs(dot(perturbX, sunL) - baseNdotL) +
                           abs(dot(perturbY, sunL) - baseNdotL) +
                           abs(dot(perturbZ, sunL) - baseNdotL);

        sensitivity = clamp(sensitivity * 50.0, 0.0, 1.0);
        oColor = vec4(sensitivity, 1.0 - sensitivity, 0.0, 1.0);
    }
    else {
        oColor = vec4(absorbedColor, shadowFactor);
    }
}
