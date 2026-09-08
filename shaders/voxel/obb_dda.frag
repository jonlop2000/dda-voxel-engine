#version 450
#extension GL_GOOGLE_include_directive : enable

#ifndef DDA_NEAR_CLIP_SAFE_OPAQUE
#define DDA_NEAR_CLIP_SAFE_OPAQUE 0
#endif

#ifndef DDA_SHARED_ALIGNED_LAYERS
#define DDA_SHARED_ALIGNED_LAYERS 0
#endif

#ifndef DDA_SHARED_ALIGNED_LAYERS_NEAR_CLIP
#define DDA_SHARED_ALIGNED_LAYERS_NEAR_CLIP 0
#endif

#if DDA_SHARED_ALIGNED_LAYERS_NEAR_CLIP
#undef DDA_SHARED_ALIGNED_LAYERS
#define DDA_SHARED_ALIGNED_LAYERS 1
#undef DDA_NEAR_CLIP_SAFE_OPAQUE
#define DDA_NEAR_CLIP_SAFE_OPAQUE 1
#endif

#if DDA_SHARED_ALIGNED_LAYERS
#ifndef DDA_UNWRAPPED_OPAQUE
#define DDA_UNWRAPPED_OPAQUE 1
#endif
#else
#ifndef DDA_UNWRAPPED_OPAQUE
#define DDA_UNWRAPPED_OPAQUE DDA_NEAR_CLIP_SAFE_OPAQUE
#endif
#endif

#ifndef DDA_OPAQUE_ONLY
#define DDA_OPAQUE_ONLY DDA_UNWRAPPED_OPAQUE
#endif

#ifndef DDA_POST_HIT_CACHE
#define DDA_POST_HIT_CACHE DDA_OPAQUE_ONLY
#endif

#ifndef DDA_OPAQUE_PALETTE_FAST_PATH
#define DDA_OPAQUE_PALETTE_FAST_PATH DDA_POST_HIT_CACHE
#endif

#ifndef DDA_NORMAL_ONLY
#if DDA_POST_HIT_CACHE
#define DDA_NORMAL_ONLY 1
#else
#define DDA_NORMAL_ONLY 0
#endif
#endif

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 3) in vec3 vLocalPos;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outVelocity;
layout(location = 4) out vec2 outWaterMeta;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 viewProjUnjittered;
    mat4 prevViewProjUnjittered;
    mat4 prevViewProj;
    mat4 invViewProjUnjittered;
    vec4 cameraWorld;
    vec2 renderSize;
    vec2 invRenderSize;
} uFrame;

#define VOXEL_SET 1
#include "voxel/palette.glsl"
#include "voxel/hierarchy.glsl"
#include "voxel_volume.glsl"
#include "voxel/water_common.glsl"

layout(set = VOXEL_SET, binding = 2) uniform usampler3D uVoxelTex;
layout(set = VOXEL_SET, binding = 3) uniform usampler3D uOccTex;
layout(set = VOXEL_SET, binding = 4) uniform sampler2D uMaterialAtlas;

#if DDA_SHARED_ALIGNED_LAYERS
layout(set = 4, binding = 2) uniform usampler3D uSecondaryVoxelTex;
layout(set = 4, binding = 3) uniform usampler3D uSecondaryOccTex;
#endif

layout(push_constant) uniform PC {
    uint volumeIndex;
    uint debugMode;
    uint skipEnabled;
    uint skipMip;
    uint heatmapMode;         // 0 = iterations, 1 = skip jumps
    float heatmapMax;         // normalization cap for heatmap
    float heatmapGamma;       // gamma correction for heatmap
    uint metricsEnabled;      // day 8.5: enable metrics recording
    uint metricsSampleStride; // sample every nth pixel
    uint metricsSampleOffsetX; // sample offset x (0..stride-1)
    uint metricsSampleOffsetY; // sample offset y (0..stride-1)
    float normalEdgeSmoothing; // day 12D: 0.0 = off, 0.1-0.2 = subtle, 0.3+ = very rounded
    float pixelEdgeShadowStrength; // pixel-art shadow/edge darkening strength
    uint waterVolumeCount;     // day 18: active water volumes
    float reflectionClipY;     // reflection pass: start dda rays at/above this water plane
    uint reflectionClipEnabled;
    float timeSeconds;
    float cellVariationStrength;
    float cellVariationGeneric;
    float cellVariationGravel;
    float cellVariationPlant;
    float cellVariationStone;
    float cellVariationWood;
    float cellVariationHueSpread;
    float cellVariationSaturationSpread;
    float cellVariationValueSpread;
    float cellVariationPaletteFamilyStrength;
    float cavityStrength;
    float paintedMaterialStrength;
#if DDA_SHARED_ALIGNED_LAYERS
    uint secondaryVolumeIndex;
#endif
} pc;

// day 8.5: metrics ssbo for sampled atomics
#if !DDA_NORMAL_ONLY
#define METRICS_SET 2
layout(std430, set = METRICS_SET, binding = 0) buffer DDAMetrics {
    uint sampleCount;
    uint sumIters;
    uint sumSkipJumps;
    uint hitCount;
    uint missCount;
    uint sumHierarchyDescents;
    uint sumFineIters;
    uint sumHierarchyProbes;
    uint hitSignatureXor;
    uint hitSignatureSum;
    uint surfaceSignatureXor;
    uint surfaceSignatureSum;
} metrics;
#endif

#define WATER_SET 3
layout(std430, set = WATER_SET, binding = 0) readonly buffer WaterBuf {
    WaterVolumeGPU waterVolumes[MAX_WATER_VOLUMES];
};

bool insideDims(ivec3 v, ivec3 dims)
{
    return (v.x >= 0 && v.y >= 0 && v.z >= 0 &&
            v.x < dims.x &&
            v.y < dims.y &&
            v.z < dims.z);
}

bool insideInnerDims(ivec3 v, ivec3 dims)
{
    return (v.x > 0 && v.y > 0 && v.z > 0 &&
            v.x < dims.x - 1 &&
            v.y < dims.y - 1 &&
            v.z < dims.z - 1);
}

bool insideTraceDims(ivec3 v, ivec3 dims, bool wrapEnabled)
{
    return wrapEnabled ? insideDims(v, dims) : insideInnerDims(v, dims);
}

vec3 debugColorFromId(uint id)
{
    float f = fract(sin(float(id) * 12.9898) * 43758.5453);
    return vec3(0.25 + 0.75 * f, 0.2 + 0.8 * fract(f * 7.0),
                0.25 + 0.75 * fract(f * 13.0));
}

uint mixVoxelCellHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

uint voxelCellHash(ivec3 cell, uint stableVolumeKey, uint salt)
{
    uint hash = 0x9e3779b9u ^ (stableVolumeKey * 0x85ebca6bu);
    hash ^= uint(cell.x) * 0x8da6b343u;
    hash ^= uint(cell.y) * 0xd8163841u;
    hash ^= uint(cell.z) * 0xcb1ab31fu;
    hash ^= salt * 0x27d4eb2du;
    return mixVoxelCellHash(hash);
}

float signedVoxelCellHash(ivec3 cell, uint stableVolumeKey, uint salt)
{
    float unit = float(voxelCellHash(cell, stableVolumeKey, salt) & 0x00ffffffu) /
                 16777215.0;
    return unit * 2.0 - 1.0;
}

vec3 voxelCellVariationSignal(ivec3 cell, uint stableVolumeKey)
{
    return vec3(
        signedVoxelCellHash(cell, stableVolumeKey, 0x68bc21ebu),
        signedVoxelCellHash(cell, stableVolumeKey, 0x02e5be93u),
        signedVoxelCellHash(cell, stableVolumeKey, 0x967a889bu));
}

float voxelCellVariationCategoryAmplitude(uint materialCategory)
{
    if (materialCategory == MATERIAL_GENERIC) return pc.cellVariationGeneric;
    if (materialCategory == MATERIAL_GRAVEL) return pc.cellVariationGravel;
    if (materialCategory == MATERIAL_PLANT) return pc.cellVariationPlant;
    if (materialCategory == MATERIAL_STONE) return pc.cellVariationStone;
    if (materialCategory == MATERIAL_WOOD) return pc.cellVariationWood;
    return 0.0;
}

float voxelCellVariationEffectiveStrength(uint materialCategory)
{
    return clamp(pc.cellVariationStrength, 0.0, 1.0) *
           clamp(voxelCellVariationCategoryAmplitude(materialCategory), 0.0, 1.0);
}

vec3 voxelCellRgbToHsv(vec3 color)
{
    float maxChannel = max(color.r, max(color.g, color.b));
    float minChannel = min(color.r, min(color.g, color.b));
    float delta = maxChannel - minChannel;
    float hue = 0.0;
    if (delta > 1e-6)
    {
        if (maxChannel == color.r)
        {
            hue = mod((color.g - color.b) / delta, 6.0);
        }
        else if (maxChannel == color.g)
        {
            hue = (color.b - color.r) / delta + 2.0;
        }
        else
        {
            hue = (color.r - color.g) / delta + 4.0;
        }
        hue = fract(hue / 6.0);
    }
    float saturation = maxChannel > 1e-6 ? delta / maxChannel : 0.0;
    return vec3(hue, saturation, maxChannel);
}

vec3 voxelCellHsvToRgb(vec3 hsv)
{
    float hue = fract(hsv.x) * 6.0;
    float chroma = hsv.z * hsv.y;
    float x = chroma * (1.0 - abs(mod(hue, 2.0) - 1.0));
    vec3 rgb;
    if (hue < 1.0) rgb = vec3(chroma, x, 0.0);
    else if (hue < 2.0) rgb = vec3(x, chroma, 0.0);
    else if (hue < 3.0) rgb = vec3(0.0, chroma, x);
    else if (hue < 4.0) rgb = vec3(0.0, x, chroma);
    else if (hue < 5.0) rgb = vec3(x, 0.0, chroma);
    else rgb = vec3(chroma, 0.0, x);
    return rgb + vec3(hsv.z - chroma);
}

vec3 applyVoxelCellAlbedoVariation(vec3 authoredColor, uint materialCategory,
                                   ivec3 cell, uint stableVolumeKey)
{
    float strength = voxelCellVariationEffectiveStrength(materialCategory);
    if (strength <= 0.0)
    {
        return authoredColor;
    }

    vec3 signal = voxelCellVariationSignal(cell, stableVolumeKey);
    ivec3 familyCell = ivec3(floor(vec3(cell) / 4.0));
    vec3 family = voxelCellVariationSignal(
        familyCell, stableVolumeKey ^ 0x6a09e667u);
    family = round(family * 2.0) * 0.5;
    ivec3 fineFamilyCell = ivec3(floor(vec3(cell) / 2.0));
    vec3 fineFamily = voxelCellVariationSignal(
        fineFamilyCell, stableVolumeKey ^ 0xbb67ae85u);
    fineFamily = round(fineFamily * 4.0) * 0.25;
    vec3 layeredFamily = mix(family, fineFamily, 0.38);
    vec3 paintedSignal = mix(
        signal, layeredFamily,
        clamp(pc.cellVariationPaletteFamilyStrength, 0.0, 1.0));
    vec3 hsv = voxelCellRgbToHsv(authoredColor);
    hsv.x = fract(hsv.x + paintedSignal.x *
                              clamp(pc.cellVariationHueSpread, 0.0, 0.25) *
                              strength);
    hsv.y = clamp(hsv.y * (1.0 + paintedSignal.y *
                                      clamp(pc.cellVariationSaturationSpread, 0.0, 1.0) *
                                      strength),
                  0.0, 1.0);
    hsv.z = max(0.0, hsv.z * (1.0 + paintedSignal.z *
                                        clamp(pc.cellVariationValueSpread, 0.0, 1.0) *
                                        strength));
    return clamp(voxelCellHsvToRgb(hsv), vec3(0.0), vec3(1.35));
}

int materialAtlasTile(uint materialCategory)
{
    if (materialCategory == MATERIAL_FRAME || materialCategory == MATERIAL_ROOM) return 0;
    if (materialCategory == MATERIAL_WOOD) return 1;
    if (materialCategory == MATERIAL_STONE) return 2;
    if (materialCategory == MATERIAL_GRAVEL) return 3;
    if (materialCategory == MATERIAL_PLANT) return 4;
    if (materialCategory == MATERIAL_CORAL) return 5;
    return -1;
}

bool materialHasDetailSignal(uint materialCategory)
{
    return materialAtlasTile(materialCategory) >= 0 || materialCategory == MATERIAL_FISH;
}

float materialAtlasStrength(uint materialCategory)
{
    if (materialCategory == MATERIAL_FRAME || materialCategory == MATERIAL_ROOM) return 0.48;
    if (materialCategory == MATERIAL_WOOD) return 0.82;
    if (materialCategory == MATERIAL_STONE) return 0.66;
    if (materialCategory == MATERIAL_GRAVEL) return 0.40;
    if (materialCategory == MATERIAL_PLANT) return 0.28;
    if (materialCategory == MATERIAL_CORAL) return 0.34;
    if (materialCategory == MATERIAL_FISH) return 0.30;
    return 0.0;
}

float materialAtlasPixelsPerVoxel(uint materialCategory)
{
    if (materialCategory == MATERIAL_WOOD) return 6.5;
    if (materialCategory == MATERIAL_STONE) return 5.8;
    if (materialCategory == MATERIAL_GRAVEL) return 6.0;
    if (materialCategory == MATERIAL_PLANT) return 4.4;
    if (materialCategory == MATERIAL_CORAL) return 5.0;
    return 5.2;
}

float materialAtlasMacroPixelsPerVoxel(uint materialCategory)
{
    if (materialCategory == MATERIAL_WOOD) return 1.4;
    if (materialCategory == MATERIAL_STONE) return 1.25;
    if (materialCategory == MATERIAL_GRAVEL) return 1.3;
    if (materialCategory == MATERIAL_PLANT) return 0.85;
    if (materialCategory == MATERIAL_CORAL) return 1.05;
    return 1.15;
}

float materialAtlasMacroStrength(uint materialCategory)
{
    if (materialCategory == MATERIAL_FRAME || materialCategory == MATERIAL_ROOM) return 0.46;
    if (materialCategory == MATERIAL_WOOD) return 0.52;
    if (materialCategory == MATERIAL_STONE) return 0.56;
    if (materialCategory == MATERIAL_GRAVEL) return 0.38;
    if (materialCategory == MATERIAL_PLANT) return 0.34;
    if (materialCategory == MATERIAL_CORAL) return 0.38;
    return 0.0;
}

vec2 materialFaceCoord(vec3 hitPointLocal, vec3 normalLocal)
{
    vec3 an = abs(normalLocal);
    if (an.x >= an.y && an.x >= an.z) return hitPointLocal.zy;
    if (an.y >= an.z) return hitPointLocal.xz;
    return hitPointLocal.xy;
}

float atlasHash13(vec3 p)
{
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

vec2 materialAtlasCoord(uint materialCategory, vec3 hitPointLocal, vec3 normalLocal)
{
    vec3 an = abs(normalLocal);

    if (materialCategory == MATERIAL_WOOD)
    {
        // the authored aquarium driftwood runs mostly along local +x with a smaller +z drift.
        // keep grain longitudinal across all visible faces instead of rotating per face normal.
        vec2 branchDir = normalize(vec2(1.0, 0.48));
        vec2 branchPerp = vec2(-branchDir.y, branchDir.x);
        float alongBranch = dot(hitPointLocal.xz, branchDir);
        float acrossBranch = dot(hitPointLocal.xz, branchPerp);
        if (an.y >= an.x && an.y >= an.z)
        {
            return vec2(alongBranch, acrossBranch);
        }
        return vec2(alongBranch, hitPointLocal.y + acrossBranch * 0.18);
    }

    if (materialCategory == MATERIAL_GRAVEL)
    {
        vec2 coord = (an.y >= an.x && an.y >= an.z) ? hitPointLocal.xz
                                                    : materialFaceCoord(hitPointLocal, normalLocal);
        vec3 cell = floor(hitPointLocal);
        float h = atlasHash13(cell);
        coord += (vec2(fract(h * 5.31), fract(h * 11.73)) - vec2(0.5)) * 0.22;
        return coord;
    }

    if (materialCategory == MATERIAL_STONE)
    {
        if (an.y >= an.x && an.y >= an.z)
        {
            return hitPointLocal.xz;
        }
        vec2 coord = materialFaceCoord(hitPointLocal, normalLocal);
        coord.x += floor(hitPointLocal.y * 0.5) * 0.35;
        return coord;
    }

    if (materialCategory == MATERIAL_FRAME || materialCategory == MATERIAL_ROOM)
    {
        if (an.x >= an.y && an.x >= an.z) return vec2(hitPointLocal.z, hitPointLocal.y);
        if (an.z >= an.y) return vec2(hitPointLocal.x, hitPointLocal.y);
        return hitPointLocal.xz;
    }

    if (materialCategory == MATERIAL_PLANT)
    {
        if (an.y >= an.x && an.y >= an.z)
        {
            return hitPointLocal.xz * vec2(0.75, 1.15);
        }
        vec2 coord = materialFaceCoord(hitPointLocal, normalLocal);
        coord.x += sin(hitPointLocal.y * 0.55) * 0.18;
        return coord;
    }

    if (materialCategory == MATERIAL_CORAL)
    {
        vec2 coord = materialFaceCoord(hitPointLocal, normalLocal);
        coord += vec2(floor(hitPointLocal.y * 0.33) * 0.21,
                      sin((hitPointLocal.x + hitPointLocal.z) * 0.33) * 0.15);
        return coord;
    }

    if (materialCategory == MATERIAL_FISH)
    {
        if (an.z >= an.x && an.z >= an.y)
        {
            return vec2(hitPointLocal.x, hitPointLocal.y);
        }
        if (an.y >= an.x && an.y >= an.z)
        {
            return vec2(hitPointLocal.x, hitPointLocal.z);
        }
        return vec2(hitPointLocal.z, hitPointLocal.y);
    }

    return materialFaceCoord(hitPointLocal, normalLocal);
}

vec3 fishMaterialDetailSignal(vec3 hitPointLocal, vec3 normalLocal)
{
    vec2 coord = materialAtlasCoord(MATERIAL_FISH, hitPointLocal, normalLocal);
    vec2 scaleCoord = vec2(coord.x * 1.30, coord.y * 1.18);
    float row = floor(scaleCoord.y);
    float rowOffset = mod(row, 2.0) * 0.48;
    vec2 scaleUv = vec2(fract(scaleCoord.x + rowOffset), fract(scaleCoord.y));
    vec2 cell = floor(vec2(scaleCoord.x + rowOffset, scaleCoord.y));
    float h = atlasHash13(vec3(cell, floor(hitPointLocal.z * 0.45)));

    float arcCenter = 0.50 + 0.25 * cos((scaleUv.x - 0.5) * 3.14159265);
    float scaleLine = 1.0 - smoothstep(0.030, 0.115, abs(scaleUv.y - arcCenter));
    float scaleFace = smoothstep(0.08, 0.28, scaleUv.y) *
                      (1.0 - smoothstep(0.74, 0.96, scaleUv.y));
    float rowBand = (floor(fract(row * 0.5) * 2.0) * 2.0 - 1.0) * 0.018;
    float alongBand = sin(hitPointLocal.x * 0.74 + row * 0.37) * 0.020;
    float sparkle = step(0.88, h) * smoothstep(0.28, 0.62, scaleUv.x) *
                    (1.0 - smoothstep(0.62, 0.92, scaleUv.y));

    float detail = 0.50 + scaleFace * 0.040 + sparkle * 0.060 + rowBand + alongBand -
                   scaleLine * 0.055;
    float coolEdge = (scaleFace - scaleLine) * 0.012;
    return clamp(vec3(detail - coolEdge * 0.25, detail + coolEdge * 0.10,
                      detail + coolEdge * 0.45),
                 vec3(0.0), vec3(1.0));
}

vec3 sampleMaterialAtlasTile(int tile, vec2 faceCoord, float pixelsPerVoxel);

vec3 plantMaterialDetailSignal(vec3 hitPointLocal, vec3 normalLocal)
{
    vec2 coord = materialAtlasCoord(MATERIAL_PLANT, hitPointLocal, normalLocal);
    vec3 fine = sampleMaterialAtlasTile(materialAtlasTile(MATERIAL_PLANT), coord,
                                        materialAtlasPixelsPerVoxel(MATERIAL_PLANT));
    vec3 macro = sampleMaterialAtlasTile(materialAtlasTile(MATERIAL_PLANT), coord,
                                         materialAtlasMacroPixelsPerVoxel(MATERIAL_PLANT));
    vec3 detail = mix(fine, macro, 0.24);

    vec3 cell = floor(hitPointLocal * vec3(0.42, 1.0, 0.42));
    float phase = atlasHash13(cell) * 6.2831853;
    float heightMask = smoothstep(0.75, 4.5, hitPointLocal.y);
    float rootMask = 1.0 - smoothstep(0.0, 1.65, hitPointLocal.y);
    float slowSway = sin(pc.timeSeconds * 1.15 + phase + hitPointLocal.y * 0.58);
    float counterSway = sin(pc.timeSeconds * 0.62 + phase * 1.71 +
                            (hitPointLocal.x - hitPointLocal.z) * 0.18);
    float livingLift = heightMask * (0.5 + 0.5 * slowSway);
    float rib = (0.5 + 0.5 * counterSway) * heightMask * 0.018;

    detail += vec3(-0.012, 0.034, 0.008) * livingLift;
    detail += vec3(-0.020, 0.032, -0.004) * rib;
    detail += vec3(-0.018, -0.006, -0.014) * rootMask;
    return clamp(detail, vec3(0.0), vec3(1.0));
}

vec3 sampleMaterialAtlasTile(int tile, vec2 faceCoord, float pixelsPerVoxel)
{
    const float tileSize = 32.0;
    const float tileCount = 6.0;
    vec2 tileUv = fract(faceCoord * (pixelsPerVoxel / tileSize));
    vec2 atlasUv = vec2((float(tile) + tileUv.x) / tileCount, tileUv.y);
    return texture(uMaterialAtlas, atlasUv).rgb;
}

vec3 voxelMaterialAtlasSignal(uint materialCategory, vec3 hitPointLocal, vec3 normalLocal)
{
    if (materialCategory == MATERIAL_FISH)
    {
        return fishMaterialDetailSignal(hitPointLocal, normalLocal);
    }
    if (materialCategory == MATERIAL_PLANT)
    {
        return plantMaterialDetailSignal(hitPointLocal, normalLocal);
    }

    int tile = materialAtlasTile(materialCategory);
    if (tile < 0)
    {
        return vec3(0.5);
    }

    vec2 faceCoord = materialAtlasCoord(materialCategory, hitPointLocal, normalLocal);
    vec3 fine = sampleMaterialAtlasTile(tile, faceCoord,
                                        materialAtlasPixelsPerVoxel(materialCategory));
    vec3 macro = sampleMaterialAtlasTile(tile, faceCoord,
                                         materialAtlasMacroPixelsPerVoxel(materialCategory));
    return mix(fine, macro, materialAtlasMacroStrength(materialCategory));
}

vec3 materialAtlasAlbedoSignal(uint materialCategory, vec3 detail)
{
    if (materialCategory != MATERIAL_GRAVEL)
    {
        return detail;
    }

    float value = dot(detail, vec3(0.299, 0.587, 0.114));
    float pebbleFace = smoothstep(0.50, 0.74, value);
    float shellFleck = smoothstep(0.76, 0.92, value);

    vec3 warmLift = vec3(0.070, 0.058, 0.032) * pebbleFace +
                    vec3(0.030, 0.026, 0.016) * shellFleck;
    return clamp(detail + warmLift, vec3(0.0), vec3(1.0));
}

vec3 materialAtlasDetailFactor(uint materialCategory, vec3 detail)
{
    vec3 albedoDetail = materialAtlasAlbedoSignal(materialCategory, detail);
    return vec3(0.52) + albedoDetail * 0.98;
}

float voxelMaterialAtlasMask(uint materialCategory)
{
    if (!materialHasDetailSignal(materialCategory))
    {
        return 0.0;
    }
    return clamp(materialAtlasStrength(materialCategory), 0.0, 1.0);
}

vec3 applyVoxelMaterialAtlas(vec3 baseColor, uint materialCategory, vec3 hitPointLocal,
                             vec3 normalLocal)
{
    if (!materialHasDetailSignal(materialCategory))
    {
        return baseColor;
    }

    vec3 detail = voxelMaterialAtlasSignal(materialCategory, hitPointLocal, normalLocal);

    // the generated atlas encodes a neutral midpoint around 0.5. remap it into a
    // multiplicative factor so palettes keep authorial color while gaining pixel detail.
    vec3 detailFactor = materialAtlasDetailFactor(materialCategory, detail);
    float strength = materialAtlasStrength(materialCategory);
    return clamp(baseColor * mix(vec3(1.0), detailFactor, strength), vec3(0.0), vec3(1.0));
}

vec3 applyVoxelMaterialAtlasSignal(vec3 baseColor, uint materialCategory, vec3 detail)
{
    if (!materialHasDetailSignal(materialCategory))
    {
        return baseColor;
    }

    vec3 detailFactor = materialAtlasDetailFactor(materialCategory, detail);
    float strength = materialAtlasStrength(materialCategory);
    return clamp(baseColor * mix(vec3(1.0), detailFactor, strength), vec3(0.0), vec3(1.0));
}

float materialAtlasRoughnessDelta(uint materialCategory, vec3 detail)
{
    if (!materialHasDetailSignal(materialCategory))
    {
        return 0.0;
    }

    float value = dot(detail, vec3(0.299, 0.587, 0.114));
    float darkGroove = clamp((0.50 - value) * 2.0, 0.0, 1.0);
    float brightWear = clamp((value - 0.54) * 2.0, 0.0, 1.0);
    float contrast = abs(value - 0.5) * 2.0;

    if (materialCategory == MATERIAL_FRAME || materialCategory == MATERIAL_ROOM)
    {
        return darkGroove * 0.035 - brightWear * 0.025;
    }
    if (materialCategory == MATERIAL_WOOD)
    {
        return darkGroove * 0.045 - brightWear * 0.020;
    }
    if (materialCategory == MATERIAL_STONE)
    {
        return darkGroove * 0.040 + contrast * 0.018;
    }
    if (materialCategory == MATERIAL_GRAVEL)
    {
        return contrast * 0.018 + darkGroove * 0.008;
    }
    if (materialCategory == MATERIAL_PLANT)
    {
        return darkGroove * 0.020 + contrast * 0.010;
    }
    if (materialCategory == MATERIAL_CORAL)
    {
        return darkGroove * 0.026 + contrast * 0.014;
    }
    if (materialCategory == MATERIAL_FISH)
    {
        return darkGroove * 0.010 - brightWear * 0.018;
    }
    return 0.0;
}

float materialAtlasAoFactor(uint materialCategory, vec3 detail)
{
    if (!materialHasDetailSignal(materialCategory))
    {
        return 1.0;
    }

    float value = dot(detail, vec3(0.299, 0.587, 0.114));
    float darkGroove = clamp((0.52 - value) * 2.2, 0.0, 1.0);

    if (materialCategory == MATERIAL_FRAME || materialCategory == MATERIAL_ROOM)
    {
        return 1.0 - darkGroove * 0.055;
    }
    if (materialCategory == MATERIAL_WOOD)
    {
        return 1.0 - darkGroove * 0.045;
    }
    if (materialCategory == MATERIAL_STONE)
    {
        return 1.0 - darkGroove * 0.065;
    }
    if (materialCategory == MATERIAL_GRAVEL)
    {
        return 1.0 - darkGroove * 0.014;
    }
    if (materialCategory == MATERIAL_PLANT)
    {
        return 1.0 - darkGroove * 0.025;
    }
    if (materialCategory == MATERIAL_CORAL)
    {
        return 1.0 - darkGroove * 0.035;
    }
    if (materialCategory == MATERIAL_FISH)
    {
        return 1.0 - darkGroove * 0.006;
    }
    return 1.0;
}

float schlickFresnelFromIor(float cosTheta, float ior)
{
    float clampedIor = max(ior, 1.01);
    float r0 = (1.0 - clampedIor) / (1.0 + clampedIor);
    r0 *= r0;
    float m = clamp(1.0 - cosTheta, 0.0, 1.0);
    float m2 = m * m;
    float m4 = m2 * m2;
    return r0 + (1.0 - r0) * m4 * m;
}

// deterministic axis from hit position inside the voxel (avoids axis flicker at edges)
ivec3 axisFromVoxelFrac(vec3 signDir, vec3 voxelFrac)
{
    vec3 distFromMin = voxelFrac;
    vec3 distFromMax = 1.0 - voxelFrac;

    float dx = (signDir.x > 0.0) ? distFromMin.x : distFromMax.x;
    float dy = (signDir.y > 0.0) ? distFromMin.y : distFromMax.y;
    float dz = (signDir.z > 0.0) ? distFromMin.z : distFromMax.z;

    float eps = 1e-3;
    float minD = min(min(dx, dy), dz);
    if (dx - minD < eps && dx <= dy && dx <= dz) return ivec3(1, 0, 0);
    if (dy - minD < eps && dy <= dz) return ivec3(0, 1, 0);
    return ivec3(0, 0, 1);
}

// stable hard normal based on hit position inside the voxel (avoids axis flicker at edges)
vec3 computeHardNormalFromVoxelPos(vec3 signDir, vec3 voxelFrac)
{
    ivec3 axis = axisFromVoxelFrac(signDir, voxelFrac);
    return vec3(float(axis.x) * -signDir.x,
                float(axis.y) * -signDir.y,
                float(axis.z) * -signDir.z);
}

vec3 normalFromHitAxis(vec3 signDir, ivec3 hitAxis)
{
    return vec3(float(hitAxis.x) * -signDir.x,
                float(hitAxis.y) * -signDir.y,
                float(hitAxis.z) * -signDir.z);
}

ivec3 axisFromBoxEntryPoint(vec3 p, vec3 bmin, vec3 bmax)
{
    vec3 dMin = abs(p - bmin);
    vec3 dMax = abs(p - bmax);
    vec3 d = min(dMin, dMax);

    if (d.x <= d.y && d.x <= d.z) return ivec3(1, 0, 0);
    if (d.y <= d.z) return ivec3(0, 1, 0);
    return ivec3(0, 0, 1);
}

#if DDA_SHARED_ALIGNED_LAYERS
bool gSurfaceUsesSecondaryLayer = false;

uint occAt(ivec3 c, int mip)
{
    uint first = texelFetch(uOccTex, c, mip).r;
    uint second = texelFetch(uSecondaryOccTex, c, mip).r;
    uint materialMask = (first | second) & VOXEL_HIERARCHY_ANY;
    if (materialMask != 0u)
    {
        return materialMask;
    }
    uint sharedJumpMip = min(first >> VOXEL_HIERARCHY_JUMP_MIP_SHIFT,
                             second >> VOXEL_HIERARCHY_JUMP_MIP_SHIFT);
    return sharedJumpMip << VOXEL_HIERARCHY_JUMP_MIP_SHIFT;
}

uint surfaceVoxelAt(ivec3 voxel)
{
    return gSurfaceUsesSecondaryLayer
               ? texelFetch(uSecondaryVoxelTex, voxel, 0).r
               : texelFetch(uVoxelTex, voxel, 0).r;
}
#else
uint occAt(ivec3 c, int mip)
{
    return texelFetch(uOccTex, c, mip).r;
}
#endif

ivec3 occLogicalDimsAtMip(ivec3 dims, int mip)
{
    int add = (1 << mip) - 1;
    return ivec3(max(1, (dims.x + add) >> mip),
                 max(1, (dims.y + add) >> mip),
                 max(1, (dims.z + add) >> mip));
}

bool isVoxelEmpty(ivec3 v, ivec3 dims)
{
    if (!insideDims(v, dims))
    {
        return true;
    }
#if DDA_SHARED_ALIGNED_LAYERS
    return surfaceVoxelAt(v) == 0u;
#else
    return texelFetch(uVoxelTex, v, 0).r == 0u;
#endif
}

#if DDA_POST_HIT_CACHE
const uint SURFACE_CACHE_FACE_NEG_X = 0u;
const uint SURFACE_CACHE_FACE_POS_X = 1u;
const uint SURFACE_CACHE_FACE_NEG_Y = 2u;
const uint SURFACE_CACHE_FACE_POS_Y = 3u;
const uint SURFACE_CACHE_FACE_NEG_Z = 4u;
const uint SURFACE_CACHE_FACE_POS_Z = 5u;
const uint SURFACE_CACHE_CORNER_NEG_A_NEG_B = 6u;
const uint SURFACE_CACHE_CORNER_NEG_A_POS_B = 7u;
const uint SURFACE_CACHE_CORNER_POS_A_NEG_B = 8u;
const uint SURFACE_CACHE_CORNER_POS_A_POS_B = 9u;

// x stores valid bits and y stores the corresponding empty-voxel bits. this cache is
// created only after traversal has produced a hit, so it cannot lengthen traversal state.
bool cachedSurfaceVoxelEmpty(ivec3 sampleVoxel, ivec3 dims, uint cacheBit,
                             inout uvec2 occupancyCache)
{
    uint mask = 1u << cacheBit;
    if ((occupancyCache.x & mask) == 0u)
    {
        if (isVoxelEmpty(sampleVoxel, dims))
        {
            occupancyCache.y |= mask;
        }
        occupancyCache.x |= mask;
    }
    return (occupancyCache.y & mask) != 0u;
}

uint surfaceFaceCacheBit(ivec3 faceStep)
{
    if (faceStep.x < 0) return SURFACE_CACHE_FACE_NEG_X;
    if (faceStep.x > 0) return SURFACE_CACHE_FACE_POS_X;
    if (faceStep.y < 0) return SURFACE_CACHE_FACE_NEG_Y;
    if (faceStep.y > 0) return SURFACE_CACHE_FACE_POS_Y;
    if (faceStep.z < 0) return SURFACE_CACHE_FACE_NEG_Z;
    return SURFACE_CACHE_FACE_POS_Z;
}
#endif

vec3 computeHardNormalFromOccupancy(vec3 signDir, vec3 rayDir, ivec3 voxel, ivec3 dims, vec3 voxelFrac, ivec3 hitAxis)
{
    vec3 dirAbs = abs(rayDir);
    bool useX = dirAbs.x > 1e-6;
    bool useY = dirAbs.y > 1e-6;
    bool useZ = dirAbs.z > 1e-6;

    bool openX = false;
    bool openY = false;
    bool openZ = false;

    if (useX)
    {
        openX = isVoxelEmpty(voxel + ivec3(int(-signDir.x), 0, 0), dims);
    }
    if (useY)
    {
        openY = isVoxelEmpty(voxel + ivec3(0, int(-signDir.y), 0), dims);
    }
    if (useZ)
    {
        openZ = isVoxelEmpty(voxel + ivec3(0, 0, int(-signDir.z)), dims);
    }

    int openCount = int(openX) + int(openY) + int(openZ);
    if (openCount == 1)
    {
        if (openX) return vec3(-signDir.x, 0.0, 0.0);
        if (openY) return vec3(0.0, -signDir.y, 0.0);
        return vec3(0.0, 0.0, -signDir.z);
    }

    if (openCount > 1)
    {
        // prefer the dda entry face (hitAxis) when it's an exposed face.
        // this eliminates per-pixel flicker at voxel edges where face-distance
        // comparisons are within float precision of each other and taa jitter
        // would otherwise flip the chosen axis frame-to-frame.
        if (hitAxis.x != 0 && openX) return vec3(-signDir.x, 0.0, 0.0);
        if (hitAxis.y != 0 && openY) return vec3(0.0, -signDir.y, 0.0);
        if (hitAxis.z != 0 && openZ) return vec3(0.0, 0.0, -signDir.z);

        vec3 distFromMin = voxelFrac;
        vec3 distFromMax = 1.0 - voxelFrac;

        float dx = (signDir.x > 0.0) ? distFromMin.x : distFromMax.x;
        float dy = (signDir.y > 0.0) ? distFromMin.y : distFromMax.y;
        float dz = (signDir.z > 0.0) ? distFromMin.z : distFromMax.z;

        float best = 1e20;
        ivec3 axis = ivec3(0);
        if (openX && dx < best) { best = dx; axis = ivec3(1, 0, 0); }
        if (openY && dy < best) { best = dy; axis = ivec3(0, 1, 0); }
        if (openZ && dz < best) { best = dz; axis = ivec3(0, 0, 1); }

        if (axis.x + axis.y + axis.z > 0)
        {
            return vec3(float(axis.x) * -signDir.x,
                        float(axis.y) * -signDir.y,
                        float(axis.z) * -signDir.z);
        }
    }

    return computeHardNormalFromVoxelPos(signDir, voxelFrac);
}

#if DDA_POST_HIT_CACHE
vec3 computeHardNormalFromOccupancyCached(vec3 signDir, vec3 rayDir, ivec3 voxel,
                                          ivec3 dims, vec3 voxelFrac, ivec3 hitAxis,
                                          inout uvec2 occupancyCache)
{
    vec3 dirAbs = abs(rayDir);
    bool useX = dirAbs.x > 1e-6;
    bool useY = dirAbs.y > 1e-6;
    bool useZ = dirAbs.z > 1e-6;

    bool openX = false;
    bool openY = false;
    bool openZ = false;

    if (useX)
    {
        ivec3 faceStep = ivec3(int(-signDir.x), 0, 0);
        openX = cachedSurfaceVoxelEmpty(voxel + faceStep, dims,
                                        surfaceFaceCacheBit(faceStep), occupancyCache);
    }
    if (useY)
    {
        ivec3 faceStep = ivec3(0, int(-signDir.y), 0);
        openY = cachedSurfaceVoxelEmpty(voxel + faceStep, dims,
                                        surfaceFaceCacheBit(faceStep), occupancyCache);
    }
    if (useZ)
    {
        ivec3 faceStep = ivec3(0, 0, int(-signDir.z));
        openZ = cachedSurfaceVoxelEmpty(voxel + faceStep, dims,
                                        surfaceFaceCacheBit(faceStep), occupancyCache);
    }

    int openCount = int(openX) + int(openY) + int(openZ);
    if (openCount == 1)
    {
        if (openX) return vec3(-signDir.x, 0.0, 0.0);
        if (openY) return vec3(0.0, -signDir.y, 0.0);
        return vec3(0.0, 0.0, -signDir.z);
    }

    if (openCount > 1)
    {
        if (hitAxis.x != 0 && openX) return vec3(-signDir.x, 0.0, 0.0);
        if (hitAxis.y != 0 && openY) return vec3(0.0, -signDir.y, 0.0);
        if (hitAxis.z != 0 && openZ) return vec3(0.0, 0.0, -signDir.z);

        vec3 distFromMin = voxelFrac;
        vec3 distFromMax = 1.0 - voxelFrac;

        float dx = (signDir.x > 0.0) ? distFromMin.x : distFromMax.x;
        float dy = (signDir.y > 0.0) ? distFromMin.y : distFromMax.y;
        float dz = (signDir.z > 0.0) ? distFromMin.z : distFromMax.z;

        float best = 1e20;
        ivec3 axis = ivec3(0);
        if (openX && dx < best) { best = dx; axis = ivec3(1, 0, 0); }
        if (openY && dy < best) { best = dy; axis = ivec3(0, 1, 0); }
        if (openZ && dz < best) { best = dz; axis = ivec3(0, 0, 1); }

        if (axis.x + axis.y + axis.z > 0)
        {
            return vec3(float(axis.x) * -signDir.x,
                        float(axis.y) * -signDir.y,
                        float(axis.z) * -signDir.z);
        }
    }

    return computeHardNormalFromVoxelPos(signDir, voxelFrac);
}
#endif

float materialBevelAmount(float categoryValue)
{
    uint category = uint(categoryValue + 0.5);
    if (category == MATERIAL_FRAME) return 0.18;
    if (category == MATERIAL_GRAVEL) return 0.30;
    if (category == MATERIAL_PLANT) return 0.24;
    if (category == MATERIAL_FISH) return 0.34;
    if (category == MATERIAL_STONE) return 0.24;
    if (category == MATERIAL_WOOD) return 0.20;
    if (category == MATERIAL_CORAL) return 0.26;
    if (category == MATERIAL_GLASS) return 0.08;
    if (category == MATERIAL_ROOM) return 0.08;
    return 0.0;
}

float materialBevelWidthScale(float categoryValue)
{
    uint category = uint(categoryValue + 0.5);
    if (category == MATERIAL_FRAME) return 0.70;
    if (category == MATERIAL_GRAVEL) return 1.10;
    if (category == MATERIAL_PLANT) return 0.90;
    if (category == MATERIAL_FISH) return 1.00;
    if (category == MATERIAL_STONE) return 1.00;
    if (category == MATERIAL_WOOD) return 0.85;
    if (category == MATERIAL_CORAL) return 0.95;
    if (category == MATERIAL_GLASS) return 0.55;
    if (category == MATERIAL_ROOM) return 0.45;
    return 0.0;
}

float bevelFaceWeight(float distanceToFace, float width)
{
    if (width <= 0.0)
    {
        return 0.0;
    }
    return 1.0 - smoothstep(width * 0.25, width, distanceToFace);
}

void accumulateBevelFace(inout vec3 normalSum, inout float adjacentWeight, vec3 hardNormal,
                         vec3 faceNormal, float distanceToFace, float width)
{
    float w = bevelFaceWeight(distanceToFace, width);
    if (w <= 0.0)
    {
        return;
    }

    normalSum += faceNormal * w;
    if (dot(faceNormal, hardNormal) < 0.999)
    {
        adjacentWeight = max(adjacentWeight, w);
    }
}

vec3 applyMaterialBevelNormal(vec3 hardNormal, ivec3 voxel, ivec3 dims, vec3 voxelFrac,
                              float materialCategory, float globalWidth,
                              out float bevelMask)
{
    bevelMask = 0.0;
    float amount = materialBevelAmount(materialCategory);
    float width = clamp(globalWidth * materialBevelWidthScale(materialCategory), 0.0, 0.32);
    if (amount <= 0.0 || width <= 0.0)
    {
        return hardNormal;
    }

    vec3 normalSum = hardNormal;
    float adjacentWeight = 0.0;

    if (isVoxelEmpty(voxel + ivec3(-1, 0, 0), dims))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(-1.0, 0.0, 0.0),
                            voxelFrac.x, width);
    }
    if (isVoxelEmpty(voxel + ivec3(1, 0, 0), dims))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(1.0, 0.0, 0.0),
                            1.0 - voxelFrac.x, width);
    }
    if (isVoxelEmpty(voxel + ivec3(0, -1, 0), dims))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(0.0, -1.0, 0.0),
                            voxelFrac.y, width);
    }
    if (isVoxelEmpty(voxel + ivec3(0, 1, 0), dims))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(0.0, 1.0, 0.0),
                            1.0 - voxelFrac.y, width);
    }
    if (isVoxelEmpty(voxel + ivec3(0, 0, -1), dims))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(0.0, 0.0, -1.0),
                            voxelFrac.z, width);
    }
    if (isVoxelEmpty(voxel + ivec3(0, 0, 1), dims))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(0.0, 0.0, 1.0),
                            1.0 - voxelFrac.z, width);
    }

    if (length(normalSum) <= 1e-4 || adjacentWeight <= 0.0)
    {
        return hardNormal;
    }

    vec3 roundedNormal = normalize(normalSum);
    bevelMask = clamp(adjacentWeight * amount, 0.0, 1.0);
    return normalize(mix(hardNormal, roundedNormal, bevelMask));
}

#if DDA_POST_HIT_CACHE
vec3 applyMaterialBevelNormalCached(vec3 hardNormal, ivec3 voxel, ivec3 dims,
                                    vec3 voxelFrac, float materialCategory,
                                    float globalWidth, out float bevelMask,
                                    inout uvec2 occupancyCache)
{
    bevelMask = 0.0;
    float amount = materialBevelAmount(materialCategory);
    float width = clamp(globalWidth * materialBevelWidthScale(materialCategory), 0.0, 0.32);
    if (amount <= 0.0 || width <= 0.0)
    {
        return hardNormal;
    }

    vec3 normalSum = hardNormal;
    float adjacentWeight = 0.0;

    if (cachedSurfaceVoxelEmpty(voxel + ivec3(-1, 0, 0), dims,
                                SURFACE_CACHE_FACE_NEG_X, occupancyCache))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(-1.0, 0.0, 0.0),
                            voxelFrac.x, width);
    }
    if (cachedSurfaceVoxelEmpty(voxel + ivec3(1, 0, 0), dims,
                                SURFACE_CACHE_FACE_POS_X, occupancyCache))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(1.0, 0.0, 0.0),
                            1.0 - voxelFrac.x, width);
    }
    if (cachedSurfaceVoxelEmpty(voxel + ivec3(0, -1, 0), dims,
                                SURFACE_CACHE_FACE_NEG_Y, occupancyCache))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(0.0, -1.0, 0.0),
                            voxelFrac.y, width);
    }
    if (cachedSurfaceVoxelEmpty(voxel + ivec3(0, 1, 0), dims,
                                SURFACE_CACHE_FACE_POS_Y, occupancyCache))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(0.0, 1.0, 0.0),
                            1.0 - voxelFrac.y, width);
    }
    if (cachedSurfaceVoxelEmpty(voxel + ivec3(0, 0, -1), dims,
                                SURFACE_CACHE_FACE_NEG_Z, occupancyCache))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(0.0, 0.0, -1.0),
                            voxelFrac.z, width);
    }
    if (cachedSurfaceVoxelEmpty(voxel + ivec3(0, 0, 1), dims,
                                SURFACE_CACHE_FACE_POS_Z, occupancyCache))
    {
        accumulateBevelFace(normalSum, adjacentWeight, hardNormal, vec3(0.0, 0.0, 1.0),
                            1.0 - voxelFrac.z, width);
    }

    if (length(normalSum) <= 1e-4 || adjacentWeight <= 0.0)
    {
        return hardNormal;
    }

    vec3 roundedNormal = normalize(normalSum);
    bevelMask = clamp(adjacentWeight * amount, 0.0, 1.0);
    return normalize(mix(hardNormal, roundedNormal, bevelMask));
}
#endif

float materialCavityStrength(uint category)
{
    if (category == MATERIAL_FRAME) return 0.12;
    if (category == MATERIAL_ROOM) return 0.08;
    if (category == MATERIAL_GRAVEL) return 0.16;
    if (category == MATERIAL_STONE) return 0.18;
    if (category == MATERIAL_WOOD) return 0.16;
    return 0.0;
}

float materialCavityWidth(uint category)
{
    if (category == MATERIAL_FRAME || category == MATERIAL_ROOM) return 0.14;
    if (category == MATERIAL_GRAVEL) return 0.18;
    if (category == MATERIAL_STONE || category == MATERIAL_WOOD) return 0.16;
    return 0.0;
}

float cavityEdgeWeight(float distanceToEdge, float width)
{
    if (width <= 0.0)
    {
        return 0.0;
    }
    return 1.0 - smoothstep(0.0, width, distanceToEdge);
}

float solidVoxelWeight(ivec3 voxel, ivec3 dims)
{
    return isVoxelEmpty(voxel, dims) ? 0.0 : 1.0;
}

float emptyVoxelWeight(ivec3 voxel, ivec3 dims)
{
    return isVoxelEmpty(voxel, dims) ? 1.0 : 0.0;
}

ivec3 normalStepFromHardNormal(vec3 hardNormal)
{
    vec3 an = abs(hardNormal);
    if (an.x >= an.y && an.x >= an.z) return ivec3(hardNormal.x > 0.0 ? 1 : -1, 0, 0);
    if (an.y >= an.z) return ivec3(0, hardNormal.y > 0.0 ? 1 : -1, 0);
    return ivec3(0, 0, hardNormal.z > 0.0 ? 1 : -1);
}

float computeVoxelCavityMask(uint materialCategory, ivec3 voxel, ivec3 dims, vec3 voxelFrac,
                             vec3 hardNormal)
{
    float strength = materialCavityStrength(materialCategory);
    float width = materialCavityWidth(materialCategory);
    if (strength <= 0.0 || width <= 0.0)
    {
        return 0.0;
    }

    ivec3 tangentA = ivec3(1, 0, 0);
    ivec3 tangentB = ivec3(0, 1, 0);
    float coordA = voxelFrac.x;
    float coordB = voxelFrac.y;
    vec3 an = abs(hardNormal);
    if (an.x >= an.y && an.x >= an.z)
    {
        tangentA = ivec3(0, 1, 0);
        tangentB = ivec3(0, 0, 1);
        coordA = voxelFrac.y;
        coordB = voxelFrac.z;
    }
    else if (an.y >= an.z)
    {
        tangentA = ivec3(1, 0, 0);
        tangentB = ivec3(0, 0, 1);
        coordA = voxelFrac.x;
        coordB = voxelFrac.z;
    }
    else
    {
        tangentA = ivec3(1, 0, 0);
        tangentB = ivec3(0, 1, 0);
        coordA = voxelFrac.x;
        coordB = voxelFrac.y;
    }

    float aMin = cavityEdgeWeight(coordA, width);
    float aMax = cavityEdgeWeight(1.0 - coordA, width);
    float bMin = cavityEdgeWeight(coordB, width);
    float bMax = cavityEdgeWeight(1.0 - coordB, width);
    ivec3 faceStep = normalStepFromHardNormal(hardNormal);

    // horizontal faces are common floor/terrain surfaces. sampling same-plane tangent
    // neighbors there turns every coplanar voxel boundary into a dark grid, so require
    // an occluder that rises through the visible face plane instead.
    bool horizontalFace = an.y >= an.x && an.y >= an.z;

    float side = 0.0;
    float corner = 0.0;
    if (horizontalFace)
    {
        side += solidVoxelWeight(voxel - tangentA + faceStep, dims) * aMin;
        side += solidVoxelWeight(voxel + tangentA + faceStep, dims) * aMax;
        side += solidVoxelWeight(voxel - tangentB + faceStep, dims) * bMin;
        side += solidVoxelWeight(voxel + tangentB + faceStep, dims) * bMax;

        corner += solidVoxelWeight(voxel - tangentA - tangentB + faceStep, dims) * aMin * bMin;
        corner += solidVoxelWeight(voxel - tangentA + tangentB + faceStep, dims) * aMin * bMax;
        corner += solidVoxelWeight(voxel + tangentA - tangentB + faceStep, dims) * aMax * bMin;
        corner += solidVoxelWeight(voxel + tangentA + tangentB + faceStep, dims) * aMax * bMax;
    }
    else
    {
        side += solidVoxelWeight(voxel - tangentA, dims) * aMin;
        side += solidVoxelWeight(voxel + tangentA, dims) * aMax;
        side += solidVoxelWeight(voxel - tangentB, dims) * bMin;
        side += solidVoxelWeight(voxel + tangentB, dims) * bMax;

        corner += solidVoxelWeight(voxel - tangentA - tangentB, dims) * aMin * bMin;
        corner += solidVoxelWeight(voxel - tangentA + tangentB, dims) * aMin * bMax;
        corner += solidVoxelWeight(voxel + tangentA - tangentB, dims) * aMax * bMin;
        corner += solidVoxelWeight(voxel + tangentA + tangentB, dims) * aMax * bMax;
    }

    float sideMask = clamp(side * (horizontalFace ? 0.62 : 0.34), 0.0, 1.0);
    float cornerMask = clamp(corner * (horizontalFace ? 0.82 : 0.52), 0.0, 1.0);
    return clamp(max(sideMask, cornerMask) * strength, 0.0, 1.0);
}

#if DDA_POST_HIT_CACHE
float computeVoxelCavityMaskCached(uint materialCategory, ivec3 voxel, ivec3 dims,
                                   vec3 voxelFrac, vec3 hardNormal,
                                   inout uvec2 occupancyCache)
{
    float strength = materialCavityStrength(materialCategory);
    float width = materialCavityWidth(materialCategory);
    if (strength <= 0.0 || width <= 0.0)
    {
        return 0.0;
    }

    ivec3 tangentA = ivec3(1, 0, 0);
    ivec3 tangentB = ivec3(0, 1, 0);
    float coordA = voxelFrac.x;
    float coordB = voxelFrac.y;
    vec3 an = abs(hardNormal);
    if (an.x >= an.y && an.x >= an.z)
    {
        tangentA = ivec3(0, 1, 0);
        tangentB = ivec3(0, 0, 1);
        coordA = voxelFrac.y;
        coordB = voxelFrac.z;
    }
    else if (an.y >= an.z)
    {
        tangentA = ivec3(1, 0, 0);
        tangentB = ivec3(0, 0, 1);
        coordA = voxelFrac.x;
        coordB = voxelFrac.z;
    }
    else
    {
        tangentA = ivec3(1, 0, 0);
        tangentB = ivec3(0, 1, 0);
        coordA = voxelFrac.x;
        coordB = voxelFrac.y;
    }

    float aMin = cavityEdgeWeight(coordA, width);
    float aMax = cavityEdgeWeight(1.0 - coordA, width);
    float bMin = cavityEdgeWeight(coordB, width);
    float bMax = cavityEdgeWeight(1.0 - coordB, width);
    ivec3 faceStep = normalStepFromHardNormal(hardNormal);
    bool horizontalFace = an.y >= an.x && an.y >= an.z;

    float side = 0.0;
    float corner = 0.0;
    if (horizontalFace)
    {
        side += solidVoxelWeight(voxel - tangentA + faceStep, dims) * aMin;
        side += solidVoxelWeight(voxel + tangentA + faceStep, dims) * aMax;
        side += solidVoxelWeight(voxel - tangentB + faceStep, dims) * bMin;
        side += solidVoxelWeight(voxel + tangentB + faceStep, dims) * bMax;

        corner += solidVoxelWeight(voxel - tangentA - tangentB + faceStep, dims) * aMin * bMin;
        corner += solidVoxelWeight(voxel - tangentA + tangentB + faceStep, dims) * aMin * bMax;
        corner += solidVoxelWeight(voxel + tangentA - tangentB + faceStep, dims) * aMax * bMin;
        corner += solidVoxelWeight(voxel + tangentA + tangentB + faceStep, dims) * aMax * bMax;
    }
    else
    {
        ivec3 negA = -tangentA;
        ivec3 posA = tangentA;
        ivec3 negB = -tangentB;
        ivec3 posB = tangentB;
        side += (cachedSurfaceVoxelEmpty(voxel + negA, dims, surfaceFaceCacheBit(negA),
                                         occupancyCache) ? 0.0 : 1.0) * aMin;
        side += (cachedSurfaceVoxelEmpty(voxel + posA, dims, surfaceFaceCacheBit(posA),
                                         occupancyCache) ? 0.0 : 1.0) * aMax;
        side += (cachedSurfaceVoxelEmpty(voxel + negB, dims, surfaceFaceCacheBit(negB),
                                         occupancyCache) ? 0.0 : 1.0) * bMin;
        side += (cachedSurfaceVoxelEmpty(voxel + posB, dims, surfaceFaceCacheBit(posB),
                                         occupancyCache) ? 0.0 : 1.0) * bMax;

        corner +=
            (cachedSurfaceVoxelEmpty(voxel - tangentA - tangentB, dims,
                                     SURFACE_CACHE_CORNER_NEG_A_NEG_B,
                                     occupancyCache) ? 0.0 : 1.0) * aMin * bMin;
        corner +=
            (cachedSurfaceVoxelEmpty(voxel - tangentA + tangentB, dims,
                                     SURFACE_CACHE_CORNER_NEG_A_POS_B,
                                     occupancyCache) ? 0.0 : 1.0) * aMin * bMax;
        corner +=
            (cachedSurfaceVoxelEmpty(voxel + tangentA - tangentB, dims,
                                     SURFACE_CACHE_CORNER_POS_A_NEG_B,
                                     occupancyCache) ? 0.0 : 1.0) * aMax * bMin;
        corner +=
            (cachedSurfaceVoxelEmpty(voxel + tangentA + tangentB, dims,
                                     SURFACE_CACHE_CORNER_POS_A_POS_B,
                                     occupancyCache) ? 0.0 : 1.0) * aMax * bMax;
    }

    float sideMask = clamp(side * (horizontalFace ? 0.62 : 0.34), 0.0, 1.0);
    float cornerMask = clamp(corner * (horizontalFace ? 0.82 : 0.52), 0.0, 1.0);
    return clamp(max(sideMask, cornerMask) * strength, 0.0, 1.0);
}
#endif

float materialPixelEdgeShadowScale(uint category)
{
    if (category == MATERIAL_GLASS || category == MATERIAL_WATER) return 0.0;
    if (category == MATERIAL_FRAME || category == MATERIAL_ROOM) return 0.34;
    if (category == MATERIAL_GRAVEL) return 0.68;
    if (category == MATERIAL_STONE) return 0.86;
    if (category == MATERIAL_WOOD) return 0.82;
    if (category == MATERIAL_PLANT) return 0.54;
    if (category == MATERIAL_CORAL) return 0.62;
    if (category == MATERIAL_FISH) return 0.48;
    return 0.0;
}

float materialPixelEdgeContactScale(uint category)
{
    if (category == MATERIAL_GLASS || category == MATERIAL_WATER) return 0.0;
    if (category == MATERIAL_FRAME || category == MATERIAL_ROOM) return 0.0;
    if (category == MATERIAL_GRAVEL) return 0.34;
    if (category == MATERIAL_STONE) return 0.42;
    if (category == MATERIAL_WOOD) return 0.38;
    if (category == MATERIAL_PLANT) return 0.26;
    if (category == MATERIAL_CORAL) return 0.32;
    if (category == MATERIAL_FISH) return 0.20;
    return 0.0;
}

float materialPixelEdgeBandWidth(uint category)
{
    if (category == MATERIAL_FRAME || category == MATERIAL_ROOM) return 0.16;
    if (category == MATERIAL_GRAVEL) return 0.22;
    if (category == MATERIAL_PLANT || category == MATERIAL_FISH) return 0.20;
    return 0.24;
}

float pixelEdgeBand(float distanceToEdge, float width)
{
    if (width <= 0.0)
    {
        return 0.0;
    }

    float edge = 1.0 - smoothstep(width * 0.35, width, distanceToEdge);
    return floor(clamp(edge, 0.0, 1.0) * 3.0 + 0.001) / 3.0;
}

float pixelStyleResponse(float rawStrength)
{
    float s = clamp(rawStrength, 0.0, 1.0);
    return clamp(max(s, smoothstep(0.0, 0.55, s) * 0.92), 0.0, 1.0);
}

float stablePixelHash21(vec2 cell)
{
    return fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
}

float materialSurfacePixelStrength(uint category)
{
    if (category == MATERIAL_GLASS || category == MATERIAL_WATER) return 0.0;
    if (category == MATERIAL_FRAME || category == MATERIAL_ROOM) return 0.26;
    if (category == MATERIAL_GRAVEL) return 0.38;
    if (category == MATERIAL_STONE) return 0.36;
    if (category == MATERIAL_WOOD) return 0.34;
    if (category == MATERIAL_PLANT) return 0.32;
    if (category == MATERIAL_CORAL) return 0.34;
    if (category == MATERIAL_FISH) return 0.38;
    return 0.0;
}

float materialSurfacePixelDensity(uint category)
{
    if (category == MATERIAL_FISH) return 2.2;
    if (category == MATERIAL_PLANT || category == MATERIAL_CORAL) return 1.55;
    if (category == MATERIAL_GRAVEL) return 0.95;
    if (category == MATERIAL_STONE) return 0.90;
    if (category == MATERIAL_WOOD) return 0.80;
    return 0.75;
}

vec3 applyVoxelSurfacePixelStyle(vec3 albedo, uint materialCategory, vec3 hitPointLocal,
                                 vec3 normalLocal)
{
    float styleStrength = pixelStyleResponse(pc.pixelEdgeShadowStrength);
    float materialStrength = materialSurfacePixelStrength(materialCategory);
    if (styleStrength <= 0.0 || materialStrength <= 0.0)
    {
        return albedo;
    }

    vec2 coord = materialAtlasCoord(materialCategory, hitPointLocal, normalLocal);
    float density = materialSurfacePixelDensity(materialCategory);
    vec2 cell = floor(coord * density);
    float h = stablePixelHash21(cell + vec2(float(materialCategory) * 13.0,
                                            float(materialCategory) * 29.0));
    float band = floor(h * 3.0) / 2.0;
    float shade = mix(-0.22, 0.12, band);
    float strength = styleStrength * materialStrength;
    return clamp(albedo * vec3(1.0 + shade * strength), vec3(0.0), vec3(1.0));
}

float computeVoxelPixelEdgeShadowMask(uint materialCategory, ivec3 voxel, ivec3 dims,
                                      vec3 voxelFrac, vec3 hardNormal, float cavityMask)
{
    float styleStrength = pixelStyleResponse(pc.pixelEdgeShadowStrength);
    float materialScale = materialPixelEdgeShadowScale(materialCategory);
    if (styleStrength <= 0.0 || materialScale <= 0.0)
    {
        return 0.0;
    }

    ivec3 tangentA = ivec3(1, 0, 0);
    ivec3 tangentB = ivec3(0, 1, 0);
    float coordA = voxelFrac.x;
    float coordB = voxelFrac.y;
    vec3 an = abs(hardNormal);
    if (an.x >= an.y && an.x >= an.z)
    {
        tangentA = ivec3(0, 1, 0);
        tangentB = ivec3(0, 0, 1);
        coordA = voxelFrac.y;
        coordB = voxelFrac.z;
    }
    else if (an.y >= an.z)
    {
        tangentA = ivec3(1, 0, 0);
        tangentB = ivec3(0, 0, 1);
        coordA = voxelFrac.x;
        coordB = voxelFrac.z;
    }
    else
    {
        tangentA = ivec3(1, 0, 0);
        tangentB = ivec3(0, 1, 0);
        coordA = voxelFrac.x;
        coordB = voxelFrac.y;
    }

    float width = materialPixelEdgeBandWidth(materialCategory);
    float aMin = pixelEdgeBand(coordA, width);
    float aMax = pixelEdgeBand(1.0 - coordA, width);
    float bMin = pixelEdgeBand(coordB, width);
    float bMax = pixelEdgeBand(1.0 - coordB, width);

    float side = 0.0;
    side += emptyVoxelWeight(voxel - tangentA, dims) * aMin;
    side += emptyVoxelWeight(voxel + tangentA, dims) * aMax;
    side += emptyVoxelWeight(voxel - tangentB, dims) * bMin;
    side += emptyVoxelWeight(voxel + tangentB, dims) * bMax;

    float corner = 0.0;
    corner += emptyVoxelWeight(voxel - tangentA - tangentB, dims) * aMin * bMin;
    corner += emptyVoxelWeight(voxel - tangentA + tangentB, dims) * aMin * bMax;
    corner += emptyVoxelWeight(voxel + tangentA - tangentB, dims) * aMax * bMin;
    corner += emptyVoxelWeight(voxel + tangentA + tangentB, dims) * aMax * bMax;

    float exposedEdge = clamp(max(side * 0.45, corner * 0.95), 0.0, 1.0);
    float contactScale = max(materialCavityStrength(materialCategory), 1e-4);
    float contactEdge =
        clamp(cavityMask / contactScale, 0.0, 1.0) *
        materialPixelEdgeContactScale(materialCategory);
    float pixelMask = max(exposedEdge, contactEdge);
    return clamp(pixelMask * materialScale * styleStrength, 0.0, 1.0);
}

#if DDA_POST_HIT_CACHE
float computeVoxelPixelEdgeShadowMaskCached(uint materialCategory, ivec3 voxel, ivec3 dims,
                                            vec3 voxelFrac, vec3 hardNormal,
                                            float cavityMask, inout uvec2 occupancyCache)
{
    float styleStrength = pixelStyleResponse(pc.pixelEdgeShadowStrength);
    float materialScale = materialPixelEdgeShadowScale(materialCategory);
    if (styleStrength <= 0.0 || materialScale <= 0.0)
    {
        return 0.0;
    }

    ivec3 tangentA = ivec3(1, 0, 0);
    ivec3 tangentB = ivec3(0, 1, 0);
    float coordA = voxelFrac.x;
    float coordB = voxelFrac.y;
    vec3 an = abs(hardNormal);
    if (an.x >= an.y && an.x >= an.z)
    {
        tangentA = ivec3(0, 1, 0);
        tangentB = ivec3(0, 0, 1);
        coordA = voxelFrac.y;
        coordB = voxelFrac.z;
    }
    else if (an.y >= an.z)
    {
        tangentA = ivec3(1, 0, 0);
        tangentB = ivec3(0, 0, 1);
        coordA = voxelFrac.x;
        coordB = voxelFrac.z;
    }
    else
    {
        tangentA = ivec3(1, 0, 0);
        tangentB = ivec3(0, 1, 0);
        coordA = voxelFrac.x;
        coordB = voxelFrac.y;
    }

    float width = materialPixelEdgeBandWidth(materialCategory);
    float aMin = pixelEdgeBand(coordA, width);
    float aMax = pixelEdgeBand(1.0 - coordA, width);
    float bMin = pixelEdgeBand(coordB, width);
    float bMax = pixelEdgeBand(1.0 - coordB, width);

    ivec3 negA = -tangentA;
    ivec3 posA = tangentA;
    ivec3 negB = -tangentB;
    ivec3 posB = tangentB;
    float side = 0.0;
    side += (cachedSurfaceVoxelEmpty(voxel + negA, dims, surfaceFaceCacheBit(negA),
                                     occupancyCache) ? 1.0 : 0.0) * aMin;
    side += (cachedSurfaceVoxelEmpty(voxel + posA, dims, surfaceFaceCacheBit(posA),
                                     occupancyCache) ? 1.0 : 0.0) * aMax;
    side += (cachedSurfaceVoxelEmpty(voxel + negB, dims, surfaceFaceCacheBit(negB),
                                     occupancyCache) ? 1.0 : 0.0) * bMin;
    side += (cachedSurfaceVoxelEmpty(voxel + posB, dims, surfaceFaceCacheBit(posB),
                                     occupancyCache) ? 1.0 : 0.0) * bMax;

    float corner = 0.0;
    corner +=
        (cachedSurfaceVoxelEmpty(voxel - tangentA - tangentB, dims,
                                 SURFACE_CACHE_CORNER_NEG_A_NEG_B,
                                 occupancyCache) ? 1.0 : 0.0) * aMin * bMin;
    corner +=
        (cachedSurfaceVoxelEmpty(voxel - tangentA + tangentB, dims,
                                 SURFACE_CACHE_CORNER_NEG_A_POS_B,
                                 occupancyCache) ? 1.0 : 0.0) * aMin * bMax;
    corner +=
        (cachedSurfaceVoxelEmpty(voxel + tangentA - tangentB, dims,
                                 SURFACE_CACHE_CORNER_POS_A_NEG_B,
                                 occupancyCache) ? 1.0 : 0.0) * aMax * bMin;
    corner +=
        (cachedSurfaceVoxelEmpty(voxel + tangentA + tangentB, dims,
                                 SURFACE_CACHE_CORNER_POS_A_POS_B,
                                 occupancyCache) ? 1.0 : 0.0) * aMax * bMax;

    float exposedEdge = clamp(max(side * 0.45, corner * 0.95), 0.0, 1.0);
    float contactScale = max(materialCavityStrength(materialCategory), 1e-4);
    float contactEdge =
        clamp(cavityMask / contactScale, 0.0, 1.0) *
        materialPixelEdgeContactScale(materialCategory);
    float pixelMask = max(exposedEdge, contactEdge);
    return clamp(pixelMask * materialScale * styleStrength, 0.0, 1.0);
}
#endif

bool shouldSamplePixel(ivec2 pix, int stride, ivec2 offset)
{
    if (stride <= 1)
    {
        return true;
    }
    int s = stride;
    int ox = offset.x % s;
    int oy = offset.y % s;
    int px = (pix.x + s - ox) % s;
    int py = (pix.y + s - oy) % s;
    return (px == 0) && (py == 0);
}

bool rayBoxIntersect(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, out float tMin, out float tMax)
{
    const float eps = 1e-6;
    if (abs(rd.x) < eps && (ro.x < bmin.x || ro.x > bmax.x)) return false;
    if (abs(rd.y) < eps && (ro.y < bmin.y || ro.y > bmax.y)) return false;
    if (abs(rd.z) < eps && (ro.z < bmin.z || ro.z > bmax.z)) return false;

    vec3 invDir = vec3(
        abs(rd.x) > eps ? 1.0 / rd.x : 1e20,
        abs(rd.y) > eps ? 1.0 / rd.y : 1e20,
        abs(rd.z) > eps ? 1.0 / rd.z : 1e20);

    vec3 t0 = (bmin - ro) * invDir;
    vec3 t1 = (bmax - ro) * invDir;
    vec3 tsmaller = min(t0, t1);
    vec3 tbigger = max(t0, t1);
    tMin = max(max(tsmaller.x, tsmaller.y), tsmaller.z);
    tMax = min(min(tbigger.x, tbigger.y), tbigger.z);
    return tMax >= max(tMin, 0.0);
}

void computeDdaAdvance(vec3 tMax, out float minT, out ivec3 hitMask)
{
    minT = min(tMax.x, min(tMax.y, tMax.z));
    hitMask = ivec3(abs(tMax.x - minT) <= 1e-4 ? 1 : 0,
                    abs(tMax.y - minT) <= 1e-4 ? 1 : 0,
                    abs(tMax.z - minT) <= 1e-4 ? 1 : 0);
}

bool advanceVoxelDda(inout ivec3 voxel, inout vec3 tMax, vec3 tDelta, ivec3 step, float minT,
                     ivec3 hitMask, inout float t, inout ivec3 hitAxis, ivec3 dimsI
#if !DDA_UNWRAPPED_OPAQUE
                     , bool wrapEnabled, VolumeGpu v
#endif
                     )
{
    t = minT;
    if (hitMask.x != 0)
    {
        voxel.x += step.x;
        tMax.x += tDelta.x;
    }
    if (hitMask.y != 0)
    {
        voxel.y += step.y;
        tMax.y += tDelta.y;
    }
    if (hitMask.z != 0)
    {
        voxel.z += step.z;
        tMax.z += tDelta.z;
    }

    if (hitMask.x != 0) hitAxis = ivec3(1, 0, 0);
    else if (hitMask.y != 0) hitAxis = ivec3(0, 1, 0);
    else hitAxis = ivec3(0, 0, 1);

#if DDA_UNWRAPPED_OPAQUE
    if (!insideInnerDims(voxel, dimsI))
    {
        return false;
    }
#else
    if (!insideTraceDims(voxel, dimsI, wrapEnabled))
    {
        if (wrapEnabled)
        {
            if (voxel.y < 0 || voxel.y >= dimsI.y)
            {
                return false;
            }
            voxel = wrapVoxelCoord(voxel, v);
        }
        else
        {
            return false;
        }
    }
#endif

    return true;
}

bool advanceBrickDda(inout ivec3 brickCoord, inout vec3 brickTMax, vec3 brickTDelta, ivec3 step,
                     float minT, ivec3 hitMask, inout float t, ivec3 brickDims)
{
    t = minT;
    if (hitMask.x != 0)
    {
        brickCoord.x += step.x;
        brickTMax.x += brickTDelta.x;
    }
    if (hitMask.y != 0)
    {
        brickCoord.y += step.y;
        brickTMax.y += brickTDelta.y;
    }
    if (hitMask.z != 0)
    {
        brickCoord.z += step.z;
        brickTMax.z += brickTDelta.z;
    }

    return all(greaterThanEqual(brickCoord, ivec3(0))) &&
           all(lessThan(brickCoord, brickDims));
}

void main()
{
    VolumeGpu v = gVolumes[pc.volumeIndex];
#if DDA_SHARED_ALIGNED_LAYERS
    VolumeGpu secondaryV = gVolumes[pc.secondaryVolumeIndex];
#endif
    ivec3 dimsI = ivec3(v.dims_palette_flags.xyz);
    vec3 dims = vec3(dimsI);
    uint volFlags = v.misc.x;
#if !DDA_UNWRAPPED_OPAQUE
    bool wrapEnabled = (volFlags & FLAG_WRAP_XZ) != 0u;
#endif
    outWaterMeta = vec2(0.0);
    if (any(lessThanEqual(dimsI, ivec3(2))))
    {
        discard;
    }

    vec3 traceMin = vec3(0.0);
    vec3 traceMax = dims;
#if !DDA_UNWRAPPED_OPAQUE
    if (!wrapEnabled)
#endif
    {
#if DDA_SHARED_ALIGNED_LAYERS
        ivec3 occupiedMinI = max(
            min(ivec3(v.occupiedMin.xyz),
                ivec3(secondaryV.occupiedMin.xyz)),
            ivec3(1));
        ivec3 occupiedMaxI = min(
            max(ivec3(v.occupiedMaxExclusive.xyz),
                ivec3(secondaryV.occupiedMaxExclusive.xyz)),
            dimsI - ivec3(1));
#else
        ivec3 occupiedMinI = max(ivec3(v.occupiedMin.xyz), ivec3(1));
        ivec3 occupiedMaxI = min(ivec3(v.occupiedMaxExclusive.xyz), dimsI - ivec3(1));
#endif
        if (any(lessThanEqual(occupiedMaxI, occupiedMinI)))
        {
            discard;
        }

        traceMin = vec3(occupiedMinI);
        traceMax = vec3(occupiedMaxI);
    }

    vec3 camWorld = uFrame.cameraWorld.xyz;
    vec3 camLocal = (v.localFromWorld * vec4(camWorld, 1.0)).xyz;

    // compute a stable per-pixel ray direction from screen space to avoid seams at obb edges.
    // using vLocalPos (interpolated from the box face) causes rayDir jumps when rasterization
    // switches faces at volume boundaries, which showed up as cut bands.
    vec2 uv = (gl_FragCoord.xy + vec2(0.5)) * uFrame.invRenderSize;
    vec2 ndcXY = uv * 2.0 - 1.0;
    vec4 worldFar = uFrame.invViewProjUnjittered * vec4(ndcXY, 1.0, 1.0);
    worldFar /= worldFar.w;
    vec3 rdWorld = normalize(worldFar.xyz - camWorld);
    vec3 rd = normalize((v.localFromWorld * vec4(rdWorld, 0.0)).xyz);

    float tNear = 0.0;
    float tFar = 0.0;
    const float boxEps = 1e-4;
    if (!rayBoxIntersect(camLocal, rd, traceMin - vec3(boxEps),
                         traceMax + vec3(boxEps), tNear, tFar))
    {
        discard;
    }

    if (pc.reflectionClipEnabled != 0u && camWorld.y < pc.reflectionClipY)
    {
        if (rdWorld.y <= 1e-5)
        {
            discard;
        }

        float clipTWorld = (pc.reflectionClipY - camWorld.y) / rdWorld.y;
        if (clipTWorld > 0.0)
        {
            vec3 clipWorld = camWorld + rdWorld * clipTWorld;
            vec3 clipLocal = (v.localFromWorld * vec4(clipWorld, 1.0)).xyz;
            float clipTLocal = dot(clipLocal - camLocal, rd);
            tNear = max(tNear, clipTLocal);
            if (tNear > tFar)
            {
                discard;
            }
        }
    }

#if DDA_NEAR_CLIP_SAFE_OPAQUE || !DDA_NORMAL_ONLY
    float traceStartT = max(tNear, 0.0);
    // proxy geometry can be clipped before rasterization when the camera or finite
    // near plane intersects a volume. the conservative full-screen path starts dda
    // at the real per-pixel near plane, so it never accepts a hit that depth validity
    // would later reject. if that plane cuts through a solid, traversal ignores only
    // the connected clipped run and resumes after reaching empty space.
    vec4 worldNearH =
        uFrame.invViewProjUnjittered * vec4(ndcXY, 0.0, 1.0);
    float nearClipTLocal = traceStartT;
    if (abs(worldNearH.w) > 1e-8)
    {
        vec3 worldNear = worldNearH.xyz / worldNearH.w;
        vec3 nearLocal =
            (v.localFromWorld * vec4(worldNear, 1.0)).xyz;
        nearClipTLocal = dot(nearLocal - camLocal, rd);
    }
    const bool startAdvancedToNearClip = nearClipTLocal > traceStartT + 1e-5;
    traceStartT = max(traceStartT, nearClipTLocal);
    if (traceStartT > tFar)
    {
        discard;
    }
    vec3 entrySurface = camLocal + rd * traceStartT;
    ivec3 initialHitAxis =
        (tNear >= 0.0 && traceStartT <= tNear + 1e-5)
            ? axisFromBoxEntryPoint(entrySurface, traceMin, traceMax)
            : ivec3(0);
#else
    vec3 entrySurface = camLocal + rd * max(tNear, 0.0);
    ivec3 initialHitAxis =
        (tNear >= 0.0) ? axisFromBoxEntryPoint(entrySurface, traceMin, traceMax) : ivec3(0);
#endif

    vec3 entry = entrySurface;
    vec3 exitP = camLocal + rd * tFar;

    // bias entry/exit to stay inside [0, dims) and avoid boundary precision seams.
    const float enterBias = 1e-3;
    entry += rd * enterBias;
    exitP -= rd * enterBias;

    const vec3 maxInside = max(traceMax - vec3(enterBias), traceMin);
#if !DDA_NORMAL_ONLY
    vec3 entryBeforeClamp = entry;
    vec3 exitBeforeClamp = exitP;
#endif
    entry = clamp(entry, traceMin, maxInside);
    exitP = clamp(exitP, traceMin, maxInside);

    vec3 dir = exitP - entry;
    float len = length(dir);
    if (len < 1e-5)
    {
        discard;
    }

    vec3 rayDir = dir / len;
    // geometric start on the true camera ray (used for world hit/depth).
    vec3 pGeom = entry + rayDir * 1e-3;
    vec3 maxP = max(traceMax - vec3(1e-3), traceMin);
#if !DDA_NORMAL_ONLY
    vec3 pBeforeClamp = pGeom;
#endif
    pGeom = clamp(pGeom, traceMin, maxP);

    // sample-space start used for voxel lookup. wrapped volumes shift this point.
    vec3 pSample = pGeom;
#if !DDA_UNWRAPPED_OPAQUE
    if (wrapEnabled)
    {
        pSample = applyWrapOffset(pSample, v);
    }
#endif

#if !DDA_NORMAL_ONLY
    // debug: visualize where entry/exit/p were clamped into the volume.
    if (pc.debugMode == 16u)
    {
        bool entryClamped = any(greaterThan(abs(entry - entryBeforeClamp), vec3(0.0)));
        bool exitClamped = any(greaterThan(abs(exitP - exitBeforeClamp), vec3(0.0)));
        bool pClamped = any(greaterThan(abs(pGeom - pBeforeClamp), vec3(0.0)));
        vec3 color = vec3(entryClamped ? 1.0 : 0.0,
                          exitClamped ? 1.0 : 0.0,
                          pClamped ? 1.0 : 0.0);
        outAlbedo = vec4(color, 1.0);
        outNormal = vec4(0.5, 0.5, 1.0, 1.0);
        outMaterial = vec4(0.8, 0.0, 1.0, 0.0);
        outVelocity = vec4(0.0);
        gl_FragDepth = gl_FragCoord.z;
        return;
    }
#endif

    ivec3 voxel = ivec3(floor(pSample));
#if DDA_UNWRAPPED_OPAQUE
    if (!insideInnerDims(voxel, dimsI))
    {
        discard;
    }
#else
    if (!insideTraceDims(voxel, dimsI, wrapEnabled))
    {
        // wrapped volumes render the full X/Z range and only clip once they leave y bounds.
        if (!wrapEnabled || voxel.y < 0 || voxel.y >= dimsI.y)
        {
            discard;
        }
        voxel = wrapVoxelCoord(voxel, v);
    }
#endif

    ivec3 step = ivec3(sign(rayDir));

    const float kHuge = 1e20;
    vec3 tDelta;
    tDelta.x = rayDir.x != 0.0 ? abs(1.0 / rayDir.x) : kHuge;
    tDelta.y = rayDir.y != 0.0 ? abs(1.0 / rayDir.y) : kHuge;
    tDelta.z = rayDir.z != 0.0 ? abs(1.0 / rayDir.z) : kHuge;

    vec3 nextBoundary = vec3(voxel) + vec3(step.x > 0 ? 1.0 : 0.0,
                                          step.y > 0 ? 1.0 : 0.0,
                                          step.z > 0 ? 1.0 : 0.0);

    vec3 tMax;
    tMax.x = rayDir.x != 0.0 ? (nextBoundary.x - pSample.x) / rayDir.x : kHuge;
    tMax.y = rayDir.y != 0.0 ? (nextBoundary.y - pSample.y) / rayDir.y : kHuge;
    tMax.z = rayDir.z != 0.0 ? (nextBoundary.z - pSample.z) / rayDir.z : kHuge;
    ivec3 initialVoxel = voxel;
    vec3 initialTMax = tMax;

#if DDA_NEAR_CLIP_SAFE_OPAQUE || !DDA_NORMAL_ONLY
    bool skipNearClipOpaqueRun = false;
#if DDA_SHARED_ALIGNED_LAYERS
    bool skipNearClipFirstRun = false;
    bool skipNearClipSecondRun = false;
#endif
    const uint nearClipRunExcludedFlags =
        FLAG_GLASS | FLAG_WATER | FLAG_WRAP_XZ | FLAG_CLOUD;
    if (startAdvancedToNearClip &&
        (volFlags & nearClipRunExcludedFlags) == 0u)
    {
#if DDA_SHARED_ALIGNED_LAYERS
        skipNearClipFirstRun = texelFetch(uVoxelTex, voxel, 0).r != 0u;
        skipNearClipSecondRun =
            texelFetch(uSecondaryVoxelTex, voxel, 0).r != 0u;
        skipNearClipOpaqueRun =
            skipNearClipFirstRun || skipNearClipSecondRun;
#else
        skipNearClipOpaqueRun = texelFetch(uVoxelTex, voxel, 0).r != 0u;
#endif
    }
#endif

    float t = 0.0;
    ivec3 hitAxis = initialHitAxis;
    uint hitId = 0u;
#if DDA_SHARED_ALIGNED_LAYERS
    uint firstLayerHitId = 0u;
    uint secondLayerHitId = 0u;
    bool hitUsesSecondaryLayer = false;
#endif
#if !DDA_OPAQUE_ONLY
    uint hitShadingModel = SHADING_OPAQUE;
#endif
    uint ddaIters = 0u;     // total dda loop iterations (measures real work)
#if !DDA_NORMAL_ONLY
    uint skipJumps = 0u;    // times we jumped over empty bricks
    uint hierarchyDescents = 0u;
    uint hierarchyProbes = 0u;
    uint fineIters = 0u;
#endif

#if !DDA_OPAQUE_ONLY
    // stage 1 transmissive accumulation while walking through glass/water voxels.
    vec3 glassTint = vec3(1.0);
    uint glassLayerCount = 0u;
    float glassDistance = 0.0;  // continuous distance through glass (world units)
#if !DDA_NORMAL_ONLY
    uint transmissiveLayerCount = 0u;
#endif
    float glassFresnel = 0.0;
    float firstGlassT = -1.0;
    vec3 firstGlassNormalLocal = vec3(0.0);  // normal of first glass surface hit
    float voxelWaterDist = 0.0;
    float rayWorldPerLocal = length((v.worldFromLocal * vec4(rayDir, 0.0)).xyz);
    rayWorldPerLocal = max(rayWorldPerLocal, 1e-4);
#endif

    int maxSteps = dimsI.x + dimsI.y + dimsI.z + 64;
#if !DDA_UNWRAPPED_OPAQUE
    if (wrapEnabled)
    {
        // wrapped cloud rays can traverse longer xz paths before a hit.
        maxSteps = max(maxSteps, (dimsI.x + dimsI.z) * 2 + dimsI.y + 128);
    }
#endif
    int requestedSkipMip = int(pc.skipMip);
    int maxDim = max(dimsI.x, max(dimsI.y, dimsI.z));
    int maxMip = maxDim > 0 ? findMSB(maxDim) : 0;
    requestedSkipMip = clamp(requestedSkipMip, 0, maxMip);
    // wrapped sparse volumes retain their established fixed-mip path. dense non-wrapped
    // hierarchy traversal remains explicit per-volume opt-in while the exact path is
    // staged through synthetic and real-scene correctness gates.
#if DDA_UNWRAPPED_OPAQUE
    bool denseHierarchyEnabled =
        (pc.skipEnabled != 0u && requestedSkipMip > 0 &&
         (volFlags & FLAG_ALLOW_DENSE_SKIP) != 0u);
    int skipMip = denseHierarchyEnabled ? requestedSkipMip : 0;
    bool skipEnabled = denseHierarchyEnabled;
#else
    bool wrappedSkipEnabled = (pc.skipEnabled != 0u && wrapEnabled && requestedSkipMip > 0);
    bool denseHierarchyEnabled =
        (pc.skipEnabled != 0u && !wrapEnabled && requestedSkipMip > 0 &&
         (volFlags & FLAG_ALLOW_DENSE_SKIP) != 0u);
    int skipMip = (wrappedSkipEnabled || denseHierarchyEnabled) ? requestedSkipMip : 0;
    bool skipEnabled = wrappedSkipEnabled || denseHierarchyEnabled;
#endif

#if !DDA_NORMAL_ONLY
    if (pc.debugMode == 4u)
    {
        ivec3 cell = clamp(ivec3(floor(entry)), ivec3(0), dimsI - ivec3(1));
        ivec3 brickDims = occLogicalDimsAtMip(dimsI, skipMip);
        ivec3 brick = clamp(cell >> skipMip, ivec3(0), brickDims - ivec3(1));
        uint occ = occAt(brick, skipMip);
        float v = ((occ & VOXEL_HIERARCHY_ANY) != 0u) ? 1.0 : 0.0;
        float grid = ((brick.x + brick.y + brick.z) & 1) == 0 ? 0.85 : 1.0;

        outAlbedo = vec4(v * grid);
        outNormal = vec4(0.5, 0.5, 1.0, 1.0);
        outMaterial = vec4(0.8, 0.0, 1.0, 0.0);
        outVelocity = vec4(0.0);
        gl_FragDepth = gl_FragCoord.z;
        return;
    }
#endif

    int guard = (skipEnabled && skipMip > 0) ? (maxSteps * 8) : maxSteps;
    ivec3 cachedOccupiedLeafCoord = ivec3(-1);

    while (ddaIters < uint(maxSteps) && t <= len && guard-- > 0)
    {
        ddaIters++;
#if !DDA_UNWRAPPED_OPAQUE
        if (wrappedSkipEnabled)
        {
            ivec3 brickCoord = voxel >> skipMip;
#if !DDA_NORMAL_ONLY
            hierarchyProbes++;
#endif
            if ((occAt(brickCoord, skipMip) & VOXEL_HIERARCHY_ANY) == 0u)
            {
#if !DDA_NORMAL_ONLY
                skipJumps++;
#endif
                float brickSize = float(1 << skipMip);
                vec3 curPos = pSample + rayDir * t;
                vec3 brickMin = vec3(brickCoord << skipMip);
                vec3 brickMax = brickMin + vec3(brickSize);

                vec3 tExit = vec3(kHuge);
                if (rayDir.x > 0.0)
                {
                    tExit.x = (brickMax.x - curPos.x) / rayDir.x;
                }
                else if (rayDir.x < 0.0)
                {
                    tExit.x = (brickMin.x - curPos.x) / rayDir.x;
                }

                if (rayDir.y > 0.0)
                {
                    tExit.y = (brickMax.y - curPos.y) / rayDir.y;
                }
                else if (rayDir.y < 0.0)
                {
                    tExit.y = (brickMin.y - curPos.y) / rayDir.y;
                }

                if (rayDir.z > 0.0)
                {
                    tExit.z = (brickMax.z - curPos.z) / rayDir.z;
                }
                else if (rayDir.z < 0.0)
                {
                    tExit.z = (brickMin.z - curPos.z) / rayDir.z;
                }

                float tJump = min(tExit.x, min(tExit.y, tExit.z));
                if (tJump >= kHuge)
                {
                    hitId = 0u;
                    break;
                }

                bool exitX = abs(tExit.x - tJump) <= 1e-4;
                bool exitY = abs(tExit.y - tJump) <= 1e-4;
                bool exitZ = abs(tExit.z - tJump) <= 1e-4;
                float boundaryT = t + max(tJump, 0.0);
                if (boundaryT > len)
                {
                    hitId = 0u;
                    break;
                }

                vec3 boundaryPos = pSample + rayDir * boundaryT;
                ivec3 brickMinVoxel = ivec3(floor(brickMin));
                ivec3 brickMaxVoxel = ivec3(ceil(brickMax)) - ivec3(1);
                voxel = clamp(ivec3(floor(boundaryPos)), brickMinVoxel, brickMaxVoxel);
                if (exitX) voxel.x = (step.x > 0) ? brickMaxVoxel.x + 1 : brickMinVoxel.x - 1;
                if (exitY) voxel.y = (step.y > 0) ? brickMaxVoxel.y + 1 : brickMinVoxel.y - 1;
                if (exitZ) voxel.z = (step.z > 0) ? brickMaxVoxel.z + 1 : brickMinVoxel.z - 1;
                if (voxel.y < 0 || voxel.y >= dimsI.y)
                {
                    hitId = 0u;
                    break;
                }
                voxel = wrapVoxelCoord(voxel, v);

                t = boundaryT;
                vec3 nextBoundary2 = vec3(voxel) + vec3(step.x > 0 ? 1.0 : 0.0,
                                                       step.y > 0 ? 1.0 : 0.0,
                                                       step.z > 0 ? 1.0 : 0.0);
                tMax.x = rayDir.x != 0.0 ? t + (nextBoundary2.x - boundaryPos.x) / rayDir.x
                                         : kHuge;
                tMax.y = rayDir.y != 0.0 ? t + (nextBoundary2.y - boundaryPos.y) / rayDir.y
                                         : kHuge;
                tMax.z = rayDir.z != 0.0 ? t + (nextBoundary2.z - boundaryPos.z) / rayDir.z
                                         : kHuge;
                continue;
            }
        }
        else if (denseHierarchyEnabled
#if DDA_NEAR_CLIP_SAFE_OPAQUE || !DDA_NORMAL_ONLY
                 && !skipNearClipOpaqueRun
#endif
                 )
#else
        if (denseHierarchyEnabled
#if DDA_NEAR_CLIP_SAFE_OPAQUE || !DDA_NORMAL_ONLY
            && !skipNearClipOpaqueRun
#endif
            )
#endif
        {
            int emptyMip = 0;
            ivec3 leafCoord = voxel >> 1;
            bool occupiedLeafCached =
                all(equal(cachedOccupiedLeafCoord, leafCoord));
            if (!occupiedLeafCached)
            {
                uint leafValue = occAt(leafCoord, 1);
#if !DDA_NORMAL_ONLY
                hierarchyProbes++;
#endif
                if ((leafValue & VOXEL_HIERARCHY_ANY) == 0u)
                {
                    int precomputedMip =
                        int(leafValue >> VOXEL_HIERARCHY_JUMP_MIP_SHIFT);
                    // a zero metadata field remains a safe mip-1 proof and
                    // keeps the path robust for freshly-created tiny volumes.
                    emptyMip = min(skipMip, max(precomputedMip, 1));
                }
                else
                {
                    cachedOccupiedLeafCoord = leafCoord;
                }
            }

            if (!occupiedLeafCached && emptyMip > 0)
            {
#if !DDA_NORMAL_ONLY
                skipJumps++;
#endif
                int emptyBrickSize = 1 << emptyMip;
                ivec3 brickCoord = voxel >> emptyMip;
                ivec3 brickMinVoxel = brickCoord << emptyMip;
                ivec3 brickMaxVoxel =
                    min(brickMinVoxel + ivec3(emptyBrickSize), dimsI) - ivec3(1);
                vec3 brickMin = vec3(brickMinVoxel);
                vec3 brickMax = vec3(brickMaxVoxel + ivec3(1));

                vec3 boundary = vec3(
                    step.x > 0 ? brickMax.x : brickMin.x,
                    step.y > 0 ? brickMax.y : brickMin.y,
                    step.z > 0 ? brickMax.z : brickMin.z);
                vec3 exitT = vec3(
                    rayDir.x != 0.0 ? (boundary.x - pSample.x) / rayDir.x : kHuge,
                    rayDir.y != 0.0 ? (boundary.y - pSample.y) / rayDir.y : kHuge,
                    rayDir.z != 0.0 ? (boundary.z - pSample.z) / rayDir.z : kHuge);
                float boundaryT = min(exitT.x, min(exitT.y, exitT.z));
                if (boundaryT >= kHuge || boundaryT > len)
                {
                    hitId = 0u;
                    break;
                }

                // reconstruct the exact fine-DDA event at this brick boundary. starting from
                // the original per-axis crossing schedule avoids accumulating a spatial bias
                // across jumps and preserves the fine path's 1e-4 simultaneous-axis rule.
                const float ddaTieEpsilon = 1e-4;
                ivec3 candidateIndex = ivec3(0);
                vec3 candidateT = vec3(kHuge);
                for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
                {
                    if (tDelta[axisIndex] >= kHuge)
                    {
                        continue;
                    }

                    int crossingsAtOrBefore =
                        max(int(floor((boundaryT - initialTMax[axisIndex]) /
                                      tDelta[axisIndex])) + 1,
                            0);
                    int eventIndex = crossingsAtOrBefore;
                    if (crossingsAtOrBefore > 0)
                    {
                        int lastIndex = crossingsAtOrBefore - 1;
                        float lastT = initialTMax[axisIndex] +
                                      float(lastIndex) * tDelta[axisIndex];
                        if (boundaryT - lastT <= ddaTieEpsilon)
                        {
                            eventIndex = lastIndex;
                        }
                    }
                    candidateIndex[axisIndex] = eventIndex;
                    candidateT[axisIndex] =
                        initialTMax[axisIndex] +
                        float(eventIndex) * tDelta[axisIndex];
                }

                float resumeT = min(candidateT.x, min(candidateT.y, candidateT.z));
                ivec3 landingMask =
                    ivec3(abs(candidateT.x - resumeT) <= ddaTieEpsilon ? 1 : 0,
                          abs(candidateT.y - resumeT) <= ddaTieEpsilon ? 1 : 0,
                          abs(candidateT.z - resumeT) <= ddaTieEpsilon ? 1 : 0);
                ivec3 processedCrossings = candidateIndex + landingMask;
                voxel = initialVoxel + step * processedCrossings;

                if (landingMask.x != 0) hitAxis = ivec3(1, 0, 0);
                else if (landingMask.y != 0) hitAxis = ivec3(0, 1, 0);
                else hitAxis = ivec3(0, 0, 1);

#if DDA_UNWRAPPED_OPAQUE
                if (!insideInnerDims(voxel, dimsI))
#else
                if (!insideTraceDims(voxel, dimsI, false))
#endif
                {
                    hitId = 0u;
                    break;
                }

                t = resumeT;
                tMax = initialTMax + vec3(processedCrossings) * tDelta;
                continue;
            }
        }

        // voxel is already wrapped via position offset and dda step wrapping
#if !DDA_NORMAL_ONLY
        fineIters++;
#endif
        hitId = texelFetch(uVoxelTex, voxel, 0).r;
#if DDA_SHARED_ALIGNED_LAYERS
        firstLayerHitId = hitId;
        secondLayerHitId = texelFetch(uSecondaryVoxelTex, voxel, 0).r;
#endif
#if DDA_NEAR_CLIP_SAFE_OPAQUE || !DDA_NORMAL_ONLY
#if DDA_SHARED_ALIGNED_LAYERS
        if (skipNearClipFirstRun && firstLayerHitId == 0u)
        {
            skipNearClipFirstRun = false;
        }
        if (skipNearClipSecondRun && secondLayerHitId == 0u)
        {
            skipNearClipSecondRun = false;
        }
        skipNearClipOpaqueRun =
            skipNearClipFirstRun || skipNearClipSecondRun;
        firstLayerHitId = skipNearClipFirstRun ? 0u : firstLayerHitId;
        secondLayerHitId = skipNearClipSecondRun ? 0u : secondLayerHitId;
        hitUsesSecondaryLayer = secondLayerHitId != 0u;
        hitId = hitUsesSecondaryLayer ? secondLayerHitId : firstLayerHitId;
        if (skipNearClipOpaqueRun && hitId == 0u)
        {
            float containingRunMinT;
            ivec3 containingRunHitMask;
            computeDdaAdvance(tMax, containingRunMinT, containingRunHitMask);
            if (!advanceVoxelDda(voxel, tMax, tDelta, step,
                                 containingRunMinT, containingRunHitMask, t,
                                 hitAxis, dimsI))
            {
                hitId = 0u;
                break;
            }
            continue;
        }
#else
        if (skipNearClipOpaqueRun)
        {
            if (hitId == 0u)
            {
                skipNearClipOpaqueRun = false;
            }
            else
            {
                float containingRunMinT;
                ivec3 containingRunHitMask;
                computeDdaAdvance(tMax, containingRunMinT, containingRunHitMask);
                if (!advanceVoxelDda(voxel, tMax, tDelta, step,
                                     containingRunMinT, containingRunHitMask,
                                     t, hitAxis, dimsI
#if !DDA_UNWRAPPED_OPAQUE
                                     , wrapEnabled, v
#endif
                                     ))
                {
                    hitId = 0u;
                    break;
                }

                hitId = 0u;
                continue;
            }
        }
#endif
#endif
#if DDA_SHARED_ALIGNED_LAYERS && !(DDA_NEAR_CLIP_SAFE_OPAQUE || !DDA_NORMAL_ONLY)
        hitUsesSecondaryLayer = secondLayerHitId != 0u;
        hitId = hitUsesSecondaryLayer ? secondLayerHitId : firstLayerHitId;
#endif
        if (hitId != 0u)
        {
#if DDA_OPAQUE_ONLY
            // this module is selected only for volumes whose conservative material flags
            // declare no glass or water. their first occupied voxel is therefore opaque.
            break;
#else
#if DDA_OPAQUE_PALETTE_FAST_PATH
            // FLAG_GLASS/FLAG_WATER are conservative per-volume declarations. opaque-only
            // volumes can accept the first occupied voxel immediately and defer their sole
            // palette lookup to post-hit shading. transmissive volumes retain the exact
            // traversal-time classification and accumulation path below.
            if ((volFlags & (FLAG_GLASS | FLAG_WATER)) == 0u)
            {
                break;
            }
#endif
            uint paletteId = v.dims_palette_flags.w;
            PaletteEntry samplePe = GetPaletteEntry(paletteId, hitId);
            uint sampleShadingModel = uint(samplePe.pbr0.w + 0.5);
            bool isGlassVoxel = (sampleShadingModel == SHADING_GLASS);
            bool isWaterVoxel = (sampleShadingModel == SHADING_WATER);

            if (isGlassVoxel || isWaterVoxel)
            {
                float minT;
                ivec3 hitMask;
                computeDdaAdvance(tMax, minT, hitMask);
#if !DDA_NORMAL_ONLY
                transmissiveLayerCount++;
#endif

                float segLocal = max(minT - t, 0.0);
                if (isGlassVoxel)
                {
                    // only calculate surface effects at exterior boundary (first glass hit)
                    if (firstGlassT < 0.0)
                    {
                        firstGlassT = minT;

                        // store entry-face normal from dda hit axis/sign for real Fresnel/refraction.
                        if (hitMask.x != 0)
                        {
                            firstGlassNormalLocal = vec3(-float(step.x), 0.0, 0.0);
                        }
                        else if (hitMask.y != 0)
                        {
                            firstGlassNormalLocal = vec3(0.0, -float(step.y), 0.0);
                        }
                        else
                        {
                            firstGlassNormalLocal = vec3(0.0, 0.0, -float(step.z));
                        }

                        // use constant F0 for glass (0.04)
                        glassFresnel = 0.04;

                        // capture tint from first glass voxel only
                        glassTint = samplePe.baseColor_alpha.rgb;
                    }
                    // accumulate continuous distance for smooth thickness
                    glassDistance += segLocal * rayWorldPerLocal;
                    glassLayerCount++;
                }
                if (isWaterVoxel)
                {
                    voxelWaterDist += segLocal * rayWorldPerLocal;
                }

                if (!advanceVoxelDda(voxel, tMax, tDelta, step, minT, hitMask, t, hitAxis, dimsI
#if !DDA_UNWRAPPED_OPAQUE
                                     , wrapEnabled, v
#endif
                                     ))
                {
                    hitId = 0u;
                    break;
                }

                hitId = 0u;
                continue;
            }

            hitShadingModel = sampleShadingModel;
            break;
#endif
        }

        float minT;
        ivec3 hitMask;
        computeDdaAdvance(tMax, minT, hitMask);
        if (!advanceVoxelDda(voxel, tMax, tDelta, step, minT, hitMask, t, hitAxis, dimsI
#if !DDA_UNWRAPPED_OPAQUE
                             , wrapEnabled, v
#endif
                             ))
        {
            hitId = 0u;
            break;
        }
    }

#if !DDA_NORMAL_ONLY
    // day 8.5: record sampled metrics (before discard to capture both hits and misses)
    if (pc.metricsEnabled != 0u)
    {
        uint stride = max(pc.metricsSampleStride, 1u);
        ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
        ivec2 offset = ivec2(int(pc.metricsSampleOffsetX), int(pc.metricsSampleOffsetY));
        if (shouldSamplePixel(pixelCoord, int(stride), offset))
        {
            uint raySignature =
                mixVoxelCellHash(uint(pixelCoord.x) * 0x8da6b343u ^
                                 uint(pixelCoord.y) * 0xd8163841u ^
                                 pc.volumeIndex * 0xcb1ab31fu);
            uint hitSignature = mixVoxelCellHash(raySignature ^ 0xa511e9b3u);
            uint surfaceSignature = hitSignature;
            if (hitId != 0u)
            {
                uint axisCode = hitAxis.x != 0 ? (step.x > 0 ? 1u : 2u)
                                               : (hitAxis.y != 0 ? (step.y > 0 ? 3u : 4u)
                                                                 : (step.z > 0 ? 5u : 6u));
                hitSignature =
                    mixVoxelCellHash(raySignature ^
                                     voxelCellHash(voxel, pc.volumeIndex, hitId));
                surfaceSignature =
                    mixVoxelCellHash(raySignature ^
                                     voxelCellHash(voxel, pc.volumeIndex,
                                                   hitId ^ (axisCode << 8u)));
            }
            atomicAdd(metrics.sampleCount, 1u);
            atomicAdd(metrics.sumIters, ddaIters);
            atomicAdd(metrics.sumSkipJumps, skipJumps);
            atomicAdd(metrics.sumHierarchyDescents, hierarchyDescents);
            atomicAdd(metrics.sumFineIters, fineIters);
            atomicAdd(metrics.sumHierarchyProbes, hierarchyProbes);
            atomicXor(metrics.hitSignatureXor, hitSignature);
            atomicAdd(metrics.hitSignatureSum, hitSignature);
            atomicXor(metrics.surfaceSignatureXor, surfaceSignature);
            atomicAdd(metrics.surfaceSignatureSum, surfaceSignature);
            if (hitId != 0u)
            {
                atomicAdd(metrics.hitCount, 1u);
            }
            else
            {
                atomicAdd(metrics.missCount, 1u);
            }
        }
    }
#endif

    if (hitId == 0u)
    {
#if DDA_OPAQUE_ONLY
        discard;
#else
#if !DDA_NORMAL_ONLY
        if (pc.debugMode == 17u || pc.debugMode == 18u)
        {
            vec3 dbg = vec3(0.0);
            if (pc.debugMode == 17u)
            {
                float tGlass = clamp(float(transmissiveLayerCount) / 6.0, 0.0, 1.0);
                if (transmissiveLayerCount > 0u)
                {
                    dbg = mix(vec3(0.0, 0.1, 0.7), vec3(1.0, 0.6, 0.0), tGlass);
                }
            }
            else
            {
                float tWater = clamp(voxelWaterDist / 20.0, 0.0, 1.0);
                dbg = mix(vec3(0.0), vec3(0.0, 0.75, 1.0), tWater);
            }

            outAlbedo = vec4(dbg, 1.0);
            outNormal = vec4(0.5, 0.5, 1.0, 1.0);
            outMaterial = vec4(0.8, 0.0, 1.0, 0.0);
            outVelocity = vec4(0.0);
            outWaterMeta = vec2(voxelWaterDist, float(MATERIAL_GENERIC));
            gl_FragDepth = gl_FragCoord.z;
            return;
        }
#endif

        if (glassLayerCount > 0u)
        {
            float glassSurfaceT = (firstGlassT >= 0.0) ? firstGlassT : t;
            vec3 glassPointLocal = pGeom + rayDir * clamp(glassSurfaceT, 0.0, len);
            vec3 glassPointWorld = (v.worldFromLocal * vec4(glassPointLocal, 1.0)).xyz;

            vec3 nWorld = vec3(0.0, 1.0, 0.0);
            if (length(firstGlassNormalLocal) > 0.5)
            {
                nWorld = normalize((v.normalFromLocal * vec4(firstGlassNormalLocal, 0.0)).xyz);
            }

            // glass mask (1.0) and thickness for aaa glass refraction pass
            // use continuous distance for smooth thickness (no visible voxel grid)
            float thicknessNorm = clamp(glassDistance / 16.0, 0.0, 1.0);

            outAlbedo = vec4(0.0, 0.0, 0.0, 1.0);  // a=1.0 marks glass pixel
            outNormal = vec4(nWorld * 0.5 + 0.5, 1.0);
            outMaterial = vec4(0.02, 0.0, 1.0, 0.0);
            outWaterMeta = vec2(voxelWaterDist, float(MATERIAL_GLASS));

            vec4 currentClip = uFrame.viewProjUnjittered * vec4(glassPointWorld, 1.0);
            vec4 prevClip = uFrame.prevViewProjUnjittered * vec4(glassPointWorld, 1.0);
            vec2 currentNdc = currentClip.xy / max(currentClip.w, 0.0001);
            vec2 prevNdc = prevClip.xy / max(prevClip.w, 0.0001);
            vec2 velocity = (currentNdc - prevNdc) * 0.5;
            outVelocity = vec4(velocity, 1.0, thicknessNorm);  // b=reactive, a=thickness
            gl_FragDepth = 1.0;
            return;
        }
        discard;
#endif
    }

    vec3 signDir = vec3(rayDir.x >= 0.0 ? 1.0 : -1.0,
                        rayDir.y >= 0.0 ? 1.0 : -1.0,
                        rayDir.z >= 0.0 ? 1.0 : -1.0);

    // sample-space hit point for voxel-relative operations (normals/voxel fraction).
    vec3 hitPointSample = pSample + rayDir * t;
    vec3 voxelMin = vec3(voxel);  // use actual hit voxel coordinates from dda
    vec3 posInVoxel = hitPointSample - voxelMin;
    vec3 voxelFrac = clamp(posInVoxel, vec3(1e-4), vec3(1.0 - 1e-4));

#if DDA_SHARED_ALIGNED_LAYERS
    if (firstLayerHitId != 0u && secondLayerHitId != 0u)
    {
        vec3 firstHardNormal;
        vec3 secondHardNormal;
        if (t <= 1e-5 &&
            (hitAxis.x != 0 || hitAxis.y != 0 || hitAxis.z != 0))
        {
            firstHardNormal = normalFromHitAxis(signDir, hitAxis);
            secondHardNormal = firstHardNormal;
        }
        else
        {
            gSurfaceUsesSecondaryLayer = false;
            firstHardNormal = computeHardNormalFromOccupancy(
                signDir, rayDir, voxel, dimsI, voxelFrac, hitAxis);
            gSurfaceUsesSecondaryLayer = true;
            secondHardNormal = computeHardNormalFromOccupancy(
                signDir, rayDir, voxel, dimsI, voxelFrac, hitAxis);
        }

        const float collisionDepthBiasWorld = 5e-4;
        vec3 collisionHitLocal = pGeom + rayDir * t;
        vec3 collisionDepthLocal =
            (hitAxis.x == 0 && hitAxis.y == 0 && hitAxis.z == 0)
                ? entry
                : collisionHitLocal;
        VolumeGpu firstV = gVolumes[pc.volumeIndex];
        vec3 firstHitWorld =
            (firstV.worldFromLocal *
             vec4(collisionDepthLocal + firstHardNormal * 1e-3, 1.0)).xyz;
        vec3 secondHitWorld =
            (secondaryV.worldFromLocal *
             vec4(collisionDepthLocal + secondHardNormal * 1e-3, 1.0)).xyz;
        vec3 firstNormalWorld = normalize(
            (firstV.normalFromLocal * vec4(firstHardNormal, 0.0)).xyz);
        vec3 secondNormalWorld = normalize(
            (secondaryV.normalFromLocal * vec4(secondHardNormal, 0.0)).xyz);
        vec4 firstClip = uFrame.viewProj *
                         vec4(firstHitWorld +
                                  firstNormalWorld * collisionDepthBiasWorld,
                              1.0);
        vec4 secondClip = uFrame.viewProj *
                          vec4(secondHitWorld +
                                   secondNormalWorld * collisionDepthBiasWorld,
                               1.0);
        float firstDepth = firstClip.z / firstClip.w;
        float secondDepth = secondClip.z / secondClip.w;
        hitUsesSecondaryLayer = secondDepth <= firstDepth;
        hitId = hitUsesSecondaryLayer ? secondLayerHitId : firstLayerHitId;
    }

    uint surfaceVolumeIndex =
        hitUsesSecondaryLayer ? pc.secondaryVolumeIndex : pc.volumeIndex;
    gSurfaceUsesSecondaryLayer = hitUsesSecondaryLayer;
    v = gVolumes[surfaceVolumeIndex];
#endif

#if !DDA_NORMAL_ONLY
    ivec3 axisRaw = hitAxis;
    ivec3 axis = axisRaw;
    if (axis.x == 0 && axis.y == 0 && axis.z == 0)
    {
        axis = axisFromVoxelFrac(signDir, voxelFrac);
    }
#endif

    uint paletteId = v.dims_palette_flags.w;
    PaletteEntry pe = GetPaletteEntry(paletteId, hitId);
    float materialCategory = pe.extra.w;

#if DDA_POST_HIT_CACHE
    // the material cache is post-hit only. the cache is initialized after traversal and
    // palette lookup, then shared by the four surface-classification consumers below.
    uvec2 surfaceOccupancyCache = uvec2(0u);
#endif

    // compute a stable hard normal, then apply material-aware beveling for shading only.
    // the trace-entry face is trusted for clipped volume boundaries. interior dda hits still
    // need occupancy validation because edge/corner crossings can advance on multiple axes while
    // advanceVoxelDda records only one axis as the hit face.
    vec3 nHardLocal;
    if (t <= 1e-5 && (hitAxis.x != 0 || hitAxis.y != 0 || hitAxis.z != 0)) {
        // the dda entry face is the actual visible surface. the voxelFrac fallback is only
        // stable away from grid boundaries; using it at immediate first hits produces dotted
        // row/column artifacts on clipped shell faces.
        nHardLocal = normalFromHitAxis(signDir, hitAxis);
    } else {
        // use occupancy to pick the exposed face and avoid boundary ambiguity.
        // hitAxis seeds a stable tie-break using the dda entry face.
#if DDA_POST_HIT_CACHE
        nHardLocal = computeHardNormalFromOccupancyCached(
            signDir, rayDir, voxel, dimsI, voxelFrac, hitAxis, surfaceOccupancyCache);
#else
        nHardLocal =
            computeHardNormalFromOccupancy(signDir, rayDir, voxel, dimsI, voxelFrac, hitAxis);
#endif
    }

    float bevelMask = 0.0;
#if DDA_POST_HIT_CACHE
    vec3 nShadeLocal = applyMaterialBevelNormalCached(
        nHardLocal, voxel, dimsI, voxelFrac, materialCategory, pc.normalEdgeSmoothing,
        bevelMask, surfaceOccupancyCache);
#else
    vec3 nShadeLocal = applyMaterialBevelNormal(nHardLocal, voxel, dimsI, voxelFrac,
                                                materialCategory, pc.normalEdgeSmoothing,
                                                bevelMask);
#endif

    // geometric hit point on the true camera ray (unwrapped sample space).
    vec3 hitPointLocal = pGeom + rayDir * t;
    vec3 hitPointForWater = (hitAxis.x == 0 && hitAxis.y == 0 && hitAxis.z == 0)
                                ? entry
                                : hitPointLocal;
    vec3 hitPointWorld = (v.worldFromLocal * vec4(hitPointForWater, 1.0)).xyz;
    float hitTWorld = dot(hitPointWorld - camWorld, rdWorld);
    float waterDistFromVolumes = 0.0;
    // this array is consumed only for post-hit water distance. keeping it out
    // of the traversal live range also avoids computing it for discarded rays.
    WaterSegment waterSegs[MAX_WATER_VOLUMES];
    int numWaterSegs = 0;
    if (pc.waterVolumeCount > 0u)
    {
        numWaterSegs = computeWaterSegments(camWorld, rdWorld, waterVolumes, pc.waterVolumeCount,
                                            waterSegs);
    }
    if (numWaterSegs > 0 && hitTWorld > 0.0)
    {
        waterDistFromVolumes = calcWaterDistance(waterSegs, numWaterSegs, 0.0, hitTWorld);
    }
#if DDA_OPAQUE_ONLY
    float hitWaterDist = waterDistFromVolumes;
#else
    float hitWaterDist = (voxelWaterDist > 0.0) ? voxelWaterDist : waterDistFromVolumes;
#endif

    vec3 hitLocal = hitPointForWater + nHardLocal * 1e-3;

    vec3 nWorld = normalize((v.normalFromLocal * vec4(nShadeLocal, 0.0)).xyz);
    vec3 nGeomWorld = normalize((v.normalFromLocal * vec4(nHardLocal, 0.0)).xyz);

    vec3 hitWorld = (v.worldFromLocal * vec4(hitLocal, 1.0)).xyz;
    outWaterMeta = vec2(
        hitWaterDist,
        materialCategory + clamp(pc.paintedMaterialStrength, 0.0, 1.0) * 0.25);

    uint materialCategoryId = uint(materialCategory + 0.5);
    vec3 atlasSignal = voxelMaterialAtlasSignal(materialCategoryId, hitPointSample, nHardLocal);
    vec3 variedPaletteColor =
        applyVoxelCellAlbedoVariation(pe.baseColor_alpha.rgb, materialCategoryId,
                                      voxel,
#if DDA_SHARED_ALIGNED_LAYERS
                                      surfaceVolumeIndex);
#else
                                      pc.volumeIndex);
#endif
    vec3 albedo =
        applyVoxelMaterialAtlasSignal(variedPaletteColor, materialCategoryId, atlasSignal);
#if DDA_POST_HIT_CACHE
    float cavityMask = computeVoxelCavityMaskCached(
        materialCategoryId, voxel, dimsI, voxelFrac, nHardLocal, surfaceOccupancyCache);
    float pixelEdgeShadowMask = computeVoxelPixelEdgeShadowMaskCached(
        materialCategoryId, voxel, dimsI, voxelFrac, nHardLocal, cavityMask,
        surfaceOccupancyCache);
#else
    float cavityMask =
        computeVoxelCavityMask(materialCategoryId, voxel, dimsI, voxelFrac, nHardLocal);
    float pixelEdgeShadowMask =
        computeVoxelPixelEdgeShadowMask(materialCategoryId, voxel, dimsI, voxelFrac, nHardLocal,
                                        cavityMask);
#endif
    cavityMask *= clamp(pc.cavityStrength, 0.0, 1.0);
#if DDA_NORMAL_ONLY
    albedo = applyVoxelSurfacePixelStyle(albedo, materialCategoryId, hitPointSample,
                                         nHardLocal);
    albedo *= vec3(1.0 - cavityMask * 0.70);
    albedo *= vec3(1.0 - pixelEdgeShadowMask * 0.46);
#else
    uint debugMode = pc.debugMode;
    if (debugMode == 0u)
    {
        albedo = applyVoxelSurfacePixelStyle(albedo, materialCategoryId, hitPointSample,
                                             nHardLocal);
        albedo *= vec3(1.0 - cavityMask * 0.70);
        albedo *= vec3(1.0 - pixelEdgeShadowMask * 0.46);
    }
    else if (debugMode == 1u)
    {
        if (axis.x == 1) albedo = vec3(1.0, 0.0, 0.0);
        else if (axis.y == 1) albedo = vec3(0.0, 1.0, 0.0);
        else if (axis.z == 1) albedo = vec3(0.0, 0.0, 1.0);
        else albedo = vec3(1.0, 1.0, 1.0);
    }
    else if (debugMode == 2u)
    {
        // step heatmap: visualize dda iterations or skip jumps
        uint heatValue = (pc.heatmapMode == 0u) ? ddaIters : skipJumps;
        float heatMax = max(pc.heatmapMax, 1.0);
        float tHeat = clamp(float(heatValue) / heatMax, 0.0, 1.0);

        // apply gamma correction for better readability
        tHeat = pow(tHeat, pc.heatmapGamma);

        // color ramp: cold blue (0) -> hot red (1)
        // low: (0.0, 0.1, 0.8) -> mid: (0.0, 0.8, 0.3) -> high: (1.0, 0.2, 0.0)
        if (tHeat < 0.5)
        {
            float t2 = tHeat * 2.0;
            albedo = mix(vec3(0.0, 0.1, 0.8), vec3(0.0, 0.8, 0.3), t2);
        }
        else
        {
            float t2 = (tHeat - 0.5) * 2.0;
            albedo = mix(vec3(0.0, 0.8, 0.3), vec3(1.0, 0.2, 0.0), t2);
        }
    }
    else if (debugMode == 3u)
    {
        albedo = debugColorFromId(hitId);
    }
    else if (debugMode == 17u)
    {
        float tGlass = clamp(float(transmissiveLayerCount) / 6.0, 0.0, 1.0);
        if (transmissiveLayerCount == 0u)
        {
            albedo = vec3(0.0);
        }
        else
        {
            albedo = mix(vec3(0.0, 0.1, 0.7), vec3(1.0, 0.6, 0.0), tGlass);
        }
    }
    else if (debugMode == 18u)
    {
        float tWater = clamp(voxelWaterDist / 20.0, 0.0, 1.0);
        albedo = mix(vec3(0.0), vec3(0.0, 0.75, 1.0), tWater);
    }
    else if (debugMode == 19u)
    {
        albedo = vec3(bevelMask);
    }
    else if (debugMode == 20u)
    {
        albedo = vec3(voxelMaterialAtlasMask(materialCategoryId));
    }
    else if (debugMode == 21u)
    {
        albedo = materialAtlasAlbedoSignal(materialCategoryId, atlasSignal);
    }
    else if (debugMode == 22u)
    {
        float strength = max(materialCavityStrength(materialCategoryId), 1e-4);
        albedo = vec3(clamp(cavityMask / strength, 0.0, 1.0));
    }
    else if (debugMode == 23u)
    {
        albedo = vec3(pixelEdgeShadowMask);
    }
    else if (debugMode == 24u)
    {
        vec3 signal = voxelCellVariationSignal(
            voxel,
#if DDA_SHARED_ALIGNED_LAYERS
            surfaceVolumeIndex);
#else
            pc.volumeIndex);
#endif
        float strength = voxelCellVariationEffectiveStrength(materialCategoryId);
        albedo = clamp(vec3(0.5) + signal * (0.5 * strength), vec3(0.0), vec3(1.0));
    }
#endif

#if DDA_OPAQUE_ONLY
    outAlbedo = vec4(albedo, 0.0);
    outNormal = vec4(nWorld * 0.5 + 0.5, 1.0);
#else
    // glass mask: 1.0 if any glass layers traversed, 0.0 otherwise
    float glassMask = (glassLayerCount > 0u) ? 1.0 : 0.0;
    outAlbedo = vec4(albedo, glassMask);

#if DDA_NORMAL_ONLY
    // when glass is in front of opaque surface, use glass normal for refraction.
    if (glassLayerCount > 0u && length(firstGlassNormalLocal) > 0.5)
    {
        vec3 glassNWorld =
            normalize((v.normalFromLocal * vec4(firstGlassNormalLocal, 0.0)).xyz);
        outNormal = vec4(glassNWorld * 0.5 + 0.5, 1.0);
    }
    else
    {
        outNormal = vec4(nWorld * 0.5 + 0.5, 1.0);
    }
#else
    // day 12 debug: dda diagnostic modes (override normal output)
    if (debugMode == 10u) {
        // DEBUG_EDGE_DIAGNOSTIC: voxelFrac.xy in rg, hitAxis index in b
        float axisIndex = float(axisRaw.x * 1 + axisRaw.y * 2 + axisRaw.z * 3) / 3.0;
        outNormal = vec4(voxelFrac.xy, axisIndex, 1.0);
    }
    else if (debugMode == 11u) {
        // DEBUG_VOXEL_POSITION: show position within voxel
        outNormal = vec4(voxelFrac, 1.0);
    }
    else if (debugMode == 12u) {
        // DEBUG_HIT_AXIS: r=x, g=y, b=z
        outNormal = vec4(float(axisRaw.x), float(axisRaw.y), float(axisRaw.z), 1.0);
    }
    else if (debugMode == 13u) {
        // DEBUG_CLOSEST_FACE: hard normal based on hit position inside voxel
        vec3 hardN = computeHardNormalFromVoxelPos(signDir, voxelFrac);
        outNormal = vec4(hardN * 0.5 + 0.5, 1.0);
    }
    else if (debugMode == 14u) {
        // DEBUG_CLOSEST_FACE_DIFF: highlight where hitAxis normal differs from closest-face normal
        ivec3 axisForDiff = axisRaw;
        if (axisForDiff.x == 0 && axisForDiff.y == 0 && axisForDiff.z == 0)
        {
            axisForDiff = axisFromVoxelFrac(signDir, voxelFrac);
        }
        vec3 hardAxis = vec3(0.0);
        if (axisForDiff.x == 1) hardAxis = vec3(-signDir.x, 0.0, 0.0);
        else if (axisForDiff.y == 1) hardAxis = vec3(0.0, -signDir.y, 0.0);
        else hardAxis = vec3(0.0, 0.0, -signDir.z);

        vec3 hardClosest = computeHardNormalFromVoxelPos(signDir, voxelFrac);
        float diff = (dot(hardAxis, hardClosest) < 0.999) ? 1.0 : 0.0;
        outNormal = vec4(diff, 0.0, diff, 1.0); // magenta where they differ
    }
    else {
        // when glass is in front of opaque surface, use glass normal for refraction
        if (glassLayerCount > 0u && length(firstGlassNormalLocal) > 0.5)
        {
            vec3 glassNWorld =
                normalize((v.normalFromLocal * vec4(firstGlassNormalLocal, 0.0)).xyz);
            outNormal = vec4(glassNWorld * 0.5 + 0.5, 1.0);
        }
        else
        {
            outNormal = vec4(nWorld * 0.5 + 0.5, 1.0);
        }
    }
#endif
#endif

    float metallic = pe.pbr0.x;
    float roughness =
        clamp(pe.pbr0.y + materialAtlasRoughnessDelta(materialCategoryId, atlasSignal), 0.04, 1.0);
    float emissive = pe.pbr0.z;
    float ao = clamp((1.0 - cavityMask * 1.20) *
                         (1.0 - pixelEdgeShadowMask * 0.35) *
                         materialAtlasAoFactor(materialCategoryId, atlasSignal),
                     0.0, 1.0);
    outMaterial = vec4(roughness, metallic, ao, clamp(emissive, 0.0, 1.0));

    vec4 currentClip = uFrame.viewProjUnjittered * vec4(hitWorld, 1.0);
    vec4 prevClip = uFrame.prevViewProjUnjittered * vec4(hitWorld, 1.0);
    vec2 currentNdc = currentClip.xy / max(currentClip.w, 0.0001);
    vec2 prevNdc = prevClip.xy / max(prevClip.w, 0.0001);
    vec2 velocity = (currentNdc - prevNdc) * 0.5;
#if DDA_OPAQUE_ONLY
    float reactiveMask = 0.0;
#else
    uint shadingModel = hitShadingModel;
    // flag water and glass for reactive taa rejection.
    float reactiveMask =
        (shadingModel == SHADING_WATER || shadingModel == SHADING_GLASS || glassLayerCount > 0u)
            ? 1.0
            : 0.0;
#endif
    if (materialCategoryId == MATERIAL_PLANT)
    {
        reactiveMask = max(reactiveMask, 0.08);
    }
    // glass thickness for aaa refraction pass - use continuous distance for smooth results
#if DDA_OPAQUE_ONLY
    float thicknessNorm = 0.0;
#else
    float thicknessNorm = clamp(glassDistance / 16.0, 0.0, 1.0);
#endif
    outVelocity = vec4(velocity, reactiveMask, thicknessNorm);

    // depth bias along the shading normal to break ties at shared chunk faces.
    const float kDepthBiasWorld = 5e-4;
    vec4 clip = uFrame.viewProj * vec4(hitWorld + nGeomWorld * kDepthBiasWorld, 1.0);

#if !DDA_NORMAL_ONLY
    // debug: visualize depth validity without writing potentially wrong depth.
    if (debugMode == 15u)
    {
        vec3 color;
        if (clip.w <= 0.0)
        {
            color = vec3(1.0, 0.0, 1.0); // magenta: behind camera
        }
        else
        {
            float ndcZ = clip.z / clip.w;
            if (ndcZ < 0.0)
            {
                color = vec3(1.0, 0.0, 0.0); // red: before near
            }
            else if (ndcZ > 1.0)
            {
                color = vec3(1.0, 1.0, 0.0); // yellow: beyond far
            }
            else
            {
                color = vec3(0.0, 1.0, 0.0); // green: valid
            }
        }

        outAlbedo = vec4(color, 1.0);
        outNormal = vec4(0.5, 0.5, 1.0, 1.0);
        outMaterial = vec4(0.8, 0.0, 1.0, 0.0);
        outVelocity = vec4(0.0);
        outWaterMeta = vec2(0.0);
        gl_FragDepth = gl_FragCoord.z;
        return;
    }
#endif

    if (clip.w <= 0.0) discard;

    float ndcZ = clip.z / clip.w;
    if (ndcZ < 0.0 || ndcZ > 1.0) discard;

    gl_FragDepth = ndcZ;
}
