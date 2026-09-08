#version 450
#extension GL_GOOGLE_include_directive : enable
layout(location=0) in vec2 vUV;
layout(location=0) out vec4 oColor;

layout(set=0, binding=0) uniform sampler2D uTex[13];

layout(set=1, binding=0) uniform AtmosphereUBO {
    vec4 atmosphere0; // density, height falloff, base height, sun phase strength
    vec4 atmosphere1; // x=active water-volume count
} uAtmosphere;

struct WaterVolumeGPU {
    vec4 boundsMin;       // xyz=min, w=surfaceHeight
    vec4 boundsMax;       // xyz=max, w=fogDensity
    vec4 absorptionCoeff; // xyz=absorption, w=shape enum
    vec4 deepColor;       // xyz=deep color, w=flags
};

layout(std430, set=1, binding=1) readonly buffer WaterVolumes {
    WaterVolumeGPU waterVolumes[];
};

#define SCENE_ATMOSPHERE_WATER_VOLUME_COUNT int(uAtmosphere.atmosphere1.x + 0.5)
#define SCENE_ATMOSPHERE_WATER_VOLUME(INDEX) waterVolumes[INDEX]
#include "atmosphere_water_path.glsl"

const int kAlbedoSlot = 1;
const int kNormalSlot = 2;
const int kWaterDistSlot = 5;
const int kShadowSlot = 6;
const int kBloomExtractSlot = 7;
const int kBloomBlurSlot = 8;
const int kTaaSlot = 9;
const int kBlueNoiseSlot = 10;
const int kDepthSlot = 11;
const int kLegacyWaterDistSlot = 12;

#define STOCHASTIC_BN_SIZE 128
#define STOCHASTIC_BLUE_NOISE_FETCH(pixel) \
    texelFetch(uTex[kBlueNoiseSlot], (pixel) % STOCHASTIC_BN_SIZE, 0).rg
#include "stochastic_common.glsl"

layout(push_constant) uniform PC {
    // camera data for world-space star rendering
    mat4 invViewProj;
    // rendering parameters
    int mode;
    int tonemapOn;
    float exposure;
    float highlightRecovery;
    float bloomIntensity;
    int bloomEnabled;
    float vignetteStrength;
    float grainStrength;
    float time;
    float sharpenIntensity;
    float texelSizeX;
    float texelSizeY;
    float fxaaEnabled;
    uint frameIndex;
    float swapchainIsSRGB;
    // procedural star parameters
    int starsEnabled;
    uint starSeed;
    float starDensity;
    float starTwinkleSpeed;
    int applyBloomInComposite;
    int colorGradeEnabled;
    float colorGradeStrength;
    float colorGradeSaturation;
    float colorGradeContrast;
    float colorGradeTemperature;
    int pixelizeEnabled;
    float pixelizeBlockSize;
    float pixelizeStrength;
    float pixelizeEdgeFocus;
    float materialDetailStrength;
    // depth of field (in-composite focus blur). camera position as three scalars
    // to match the c++ struct packing exactly (no vec alignment rules).
    int dofEnabled;
    float dofFocusDistance;
    float dofFocusRange;
    float dofBlurStrength;
    float cameraPosX;
    float cameraPosY;
    float cameraPosZ;
} pc;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

const float EDGE_THRESHOLD_MIN = 0.0312;
const float EDGE_THRESHOLD = 0.125;
const float SUBPIXEL_QUALITY = 0.75;

vec3 applyHighlightRecoveryHdr(vec3 hdr) {
    float strength = clamp(pc.highlightRecovery, 0.0, 1.0);
    if (strength <= 0.0) {
        return hdr;
    }

    float y = max(luminance(hdr), 1e-5);
    float knee = mix(4.0, 0.85, strength);
    float shoulder = mix(0.35, 2.40, strength);
    float over = max(y - knee, 0.0);
    float compressedY = knee + over / (1.0 + shoulder * over);
    float mask = smoothstep(knee * 0.65, knee * 2.0, y);
    float targetY = mix(y, compressedY, mask);
    return hdr * (targetY / y);
}

vec3 applyHighlightRecoveryLdr(vec3 color) {
    float strength = clamp(pc.highlightRecovery, 0.0, 1.0);
    if (strength <= 0.0) {
        return color;
    }

    float y = max(luminance(color), 1e-5);
    float cap = 1.0 - 0.22 * strength;
    float over = max(y - cap, 0.0);
    float softY = cap + over * mix(0.55, 0.22, strength);
    float mask = smoothstep(0.78, 0.99, y);
    float targetY = mix(y, min(y, softY), mask);
    return clamp(color * (targetY / y), 0.0, 1.0);
}

vec3 tonemapACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 heatmap(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c = vec3(1.5 * t - 0.5,
                  1.5 - abs(2.0 * t - 1.0),
                  1.5 * (1.0 - t) - 0.5);
    return clamp(c, 0.0, 1.0);
}

vec3 waterDistanceDebug(float waterDist) {
    if (waterDist <= 0.001) {
        return vec3(0.0);
    }

    // aquarium camera rays can cross a large water volume, so linear scaling
    // saturates too quickly. log compression keeps shallow and deep regions readable.
    float t = clamp(log2(1.0 + waterDist) / log2(97.0), 0.0, 1.0);
    vec3 shallow = vec3(0.0, 0.035, 0.14);
    vec3 mid = vec3(0.0, 0.45, 1.0);
    vec3 deep = vec3(0.82, 1.0, 1.0);
    vec3 c = mix(shallow, mid, smoothstep(0.0, 0.55, t));
    c = mix(c, deep, smoothstep(0.55, 1.0, t));

    float minorPhase = fract(waterDist / 5.0);
    float minorLine = 1.0 - smoothstep(0.0, 0.025, min(minorPhase, 1.0 - minorPhase));
    float majorPhase = fract(waterDist / 20.0);
    float majorLine = 1.0 - smoothstep(0.0, 0.018, min(majorPhase, 1.0 - majorPhase));
    c = mix(c, vec3(0.98, 1.0, 1.0), minorLine * 0.22);
    c = mix(c, vec3(1.0), majorLine * 0.42);
    return c;
}

float filmGrain(vec2 uv) {
    float seed = pc.time * 12.9898;
    return fract(sin(dot(uv * vec2(127.1, 311.7), vec2(269.5, 183.3)) + seed) * 43758.5453);
}

vec3 tonemapScene(vec3 hdr) {
    if (pc.tonemapOn == 0) {
        return hdr;
    }

    vec3 c = max(hdr * max(pc.exposure, 0.0), vec3(0.0));
    c = applyHighlightRecoveryHdr(c);
    c = tonemapACES(c);
    c = pow(c, vec3(1.0 / 2.2));
    c = applyHighlightRecoveryLdr(c);
    return c;
}

float materialPresentationDetailScale(float categoryValue) {
    int category = int(floor(categoryValue + 0.5));
    if (category == 8 || category == 9) return 0.0;  // glass/water keep specialized paths
    if (category == 1 || category == 10) return 1.06; // frame/room
    if (category == 2) return 0.76; // gravel: visible, but avoid muddy gaps
    if (category == 3) return 0.44; // plant
    if (category == 4) return 0.22; // fish should keep smoother body color
    if (category == 5) return 1.12; // stone
    if (category == 6) return 0.92; // wood
    if (category == 7) return 0.52; // coral
    return 0.0;
}

float materialPresentationDarkLimit(float categoryValue) {
    int category = int(floor(categoryValue + 0.5));
    if (category == 2) return 0.016; // keep gravel from becoming dirty again
    if (category == 3 || category == 7) return 0.028;
    if (category == 4) return 0.014;
    return 0.045;
}

float materialPresentationBrightLimit(float categoryValue) {
    int category = int(floor(categoryValue + 0.5));
    if (category == 1 || category == 10) return 0.110;
    if (category == 2) return 0.095;
    if (category == 3 || category == 7) return 0.074;
    if (category == 4) return 0.042;
    if (category == 5) return 0.120;
    if (category == 6) return 0.096;
    return 0.085;
}

float materialPresentationPaleBoost(float categoryValue, float albedoLum) {
    int category = int(floor(categoryValue + 0.5));
    float pale = smoothstep(0.55, 0.86, albedoLum);
    if (category == 1 || category == 10) return mix(1.0, 1.24, pale);
    if (category == 2) return mix(1.0, 1.16, pale);
    if (category == 5) return mix(1.0, 1.30, pale);
    if (category == 6) return mix(1.0, 1.12, pale);
    if (category == 4) return mix(1.0, 0.70, pale);
    return 1.0;
}

void accumulateMaterialAlbedo(inout vec3 sum, inout float weight, vec2 sampleUv,
                              float centerCategory) {
    vec2 clampedUv = clamp(sampleUv, 0.0, 1.0);
    float sampleCategory = texture(uTex[kWaterDistSlot], clampedUv).g;
    float sameCategory = 1.0 - step(0.45, abs(sampleCategory - centerCategory));
    sum += texture(uTex[kAlbedoSlot], clampedUv).rgb * sameCategory;
    weight += sameCategory;
}

vec3 applyMaterialDetailPresentation(vec3 color, vec2 uv) {
    float strength = clamp(pc.materialDetailStrength, 0.0, 1.0);
    if (strength <= 0.0) {
        return color;
    }

    float depth = texture(uTex[kDepthSlot], uv).r;
    if (depth >= 0.9999) {
        return color;
    }

    float category = texture(uTex[kWaterDistSlot], uv).g;
    float materialScale = materialPresentationDetailScale(category);
    if (materialScale <= 0.0) {
        return color;
    }

    vec2 texel = 1.0 / vec2(textureSize(uTex[kAlbedoSlot], 0));
    vec2 stepUv = texel * 2.0;
    vec3 center = texture(uTex[kAlbedoSlot], uv).rgb;

    vec3 blur = center * 2.0;
    float weight = 2.0;
    accumulateMaterialAlbedo(blur, weight, uv + vec2(stepUv.x, 0.0), category);
    accumulateMaterialAlbedo(blur, weight, uv - vec2(stepUv.x, 0.0), category);
    accumulateMaterialAlbedo(blur, weight, uv + vec2(0.0, stepUv.y), category);
    accumulateMaterialAlbedo(blur, weight, uv - vec2(0.0, stepUv.y), category);
    accumulateMaterialAlbedo(blur, weight, uv + stepUv, category);
    accumulateMaterialAlbedo(blur, weight, uv - stepUv, category);
    blur /= max(weight, 1e-4);

    float albedoLum = luminance(center);
    vec3 detail = center - blur;
    float darkLimit = materialPresentationDarkLimit(category);
    float gravelPale = smoothstep(0.56, 0.84, albedoLum);
    darkLimit *= (int(floor(category + 0.5)) == 2) ? mix(1.0, 0.68, gravelPale) : 1.0;
    float brightLimit = materialPresentationBrightLimit(category);
    detail = clamp(detail, vec3(-darkLimit), vec3(brightLimit));

    float luma = luminance(color);
    float visibleRange = smoothstep(0.12, 0.42, luma) * (1.0 - smoothstep(0.96, 1.0, luma) * 0.35);
    float brightReadability = mix(0.82, 1.55, smoothstep(0.48, 0.88, luma));
    float paleBoost = materialPresentationPaleBoost(category, albedoLum);
    vec3 lifted =
        color + detail * (strength * materialScale * visibleRange * brightReadability * paleBoost);
    return clamp(lifted, 0.0, 1.0);
}

vec3 toLinear(vec3 c) {
    return pow(clamp(c, vec3(0.0), vec3(1.0)), vec3(2.2));
}

// ============================================================================
// depth of field (in-composite focus blur)
// ============================================================================

float dofSceneDistance(vec2 uv) {
    float depth = texture(uTex[kDepthSlot], uv).r;
    if (depth >= 0.9999) {
        return 1e6;  // sky / far plane: fully out of focus
    }
    // depth is non-linear D32; reconstruct the world position through the
    // unjittered invViewProj instead of eyeballing the raw value.
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = pc.invViewProj * clipPos;
    vec3 world = worldPos.xyz / worldPos.w;
    return length(world - vec3(pc.cameraPosX, pc.cameraPosY, pc.cameraPosZ));
}

float dofCoc(vec2 uv) {
    if (pc.dofEnabled == 0) {
        return 0.0;
    }
    float dist = dofSceneDistance(uv);
    float range = max(pc.dofFocusRange, 1e-3);
    return clamp(abs(dist - pc.dofFocusDistance) / range, 0.0, 1.0) *
           clamp(pc.dofBlurStrength, 0.0, 1.0);
}

vec3 dofTaaColor(vec2 uv) {
    vec3 center = texture(uTex[kTaaSlot], uv).rgb;
    float coc = dofCoc(uv);
    if (coc <= 0.004) {
        return center;
    }

    // small disc gather (gaussian-ish weights, like the bloom blur kernel) whose
    // radius scales with the circle of confusion. single-pass and cheap; bokeh
    // quality and depth-edge bleed limits can be tuned here.
    vec2 texel = vec2(max(pc.texelSizeX, 1e-6), max(pc.texelSizeY, 1e-6));
    float radiusPx = coc * 8.0;
    vec2 r = texel * radiusPx;

    vec3 sum = center;
    float weight = 1.0;

    const float kInnerW = 0.75;
    const vec2 kInner[4] = vec2[](vec2(0.5, 0.5), vec2(-0.5, 0.5),
                                  vec2(0.5, -0.5), vec2(-0.5, -0.5));
    for (int i = 0; i < 4; ++i) {
        sum += texture(uTex[kTaaSlot], clamp(uv + kInner[i] * r, 0.0, 1.0)).rgb * kInnerW;
        weight += kInnerW;
    }

    const float kOuterW = 0.45;
    const vec2 kOuter[8] = vec2[](vec2(1.0, 0.0), vec2(-1.0, 0.0),
                                  vec2(0.0, 1.0), vec2(0.0, -1.0),
                                  vec2(0.7071, 0.7071), vec2(-0.7071, 0.7071),
                                  vec2(0.7071, -0.7071), vec2(-0.7071, -0.7071));
    for (int i = 0; i < 8; ++i) {
        sum += texture(uTex[kTaaSlot], clamp(uv + kOuter[i] * r, 0.0, 1.0)).rgb * kOuterW;
        weight += kOuterW;
    }

    return mix(center, sum / weight, coc);
}

vec3 resolvedHDR(vec2 uv) {
    vec2 clamped = clamp(uv, 0.0, 1.0);
    vec3 hdr = dofTaaColor(clamped);
    if (pc.bloomEnabled != 0 && pc.applyBloomInComposite != 0) {
        hdr += texture(uTex[kBloomBlurSlot], clamped).rgb * pc.bloomIntensity;
    }
    return hdr;
}

vec2 presentationPixelUv(vec2 uv) {
    if (pc.pixelizeEnabled == 0) {
        return uv;
    }

    float blockSize = floor(clamp(pc.pixelizeBlockSize, 1.0, 32.0) + 0.5);
    if (blockSize <= 1.0) {
        return uv;
    }

    vec2 screenSize = vec2(1.0 / max(pc.texelSizeX, 1e-6),
                           1.0 / max(pc.texelSizeY, 1e-6));
    vec2 pixelCoord = uv * screenSize;
    vec2 blockCenter = (floor(pixelCoord / blockSize) + vec2(0.5)) * blockSize;
    return clamp(blockCenter / screenSize, vec2(0.0), vec2(1.0));
}

float presentationPixelStrength() {
    if (pc.pixelizeEnabled == 0) {
        return 0.0;
    }

    float blockSize = floor(clamp(pc.pixelizeBlockSize, 1.0, 32.0) + 0.5);
    if (blockSize <= 1.0) {
        return 0.0;
    }

    return clamp(pc.pixelizeStrength, 0.0, 1.0);
}

vec3 resolvedLDR(vec2 uv) {
    return tonemapScene(resolvedHDR(uv));
}

vec3 safeNormalAt(vec2 uv) {
    vec3 n = texture(uTex[kNormalSlot], clamp(uv, 0.0, 1.0)).xyz * 2.0 - 1.0;
    float len2 = dot(n, n);
    if (len2 <= 1e-4) {
        return vec3(0.0, 0.0, 1.0);
    }
    return n * inversesqrt(len2);
}

float depthEdgePair(float a, float b) {
    bool aSky = a >= 0.9999;
    bool bSky = b >= 0.9999;
    if (aSky && bSky) {
        return 0.0;
    }
    if (aSky || bSky) {
        return 1.0;
    }
    return smoothstep(0.00025, 0.004, abs(a - b));
}

float pixelPresentationEdgeMask(vec2 uv) {
    vec2 texel = 1.0 / vec2(textureSize(uTex[kAlbedoSlot], 0));
    vec2 uvN = clamp(uv + vec2(0.0, texel.y), 0.0, 1.0);
    vec2 uvS = clamp(uv - vec2(0.0, texel.y), 0.0, 1.0);
    vec2 uvE = clamp(uv + vec2(texel.x, 0.0), 0.0, 1.0);
    vec2 uvW = clamp(uv - vec2(texel.x, 0.0), 0.0, 1.0);

    float dM = texture(uTex[kDepthSlot], uv).r;
    float depthEdge = max(max(depthEdgePair(dM, texture(uTex[kDepthSlot], uvN).r),
                              depthEdgePair(dM, texture(uTex[kDepthSlot], uvS).r)),
                          max(depthEdgePair(dM, texture(uTex[kDepthSlot], uvE).r),
                              depthEdgePair(dM, texture(uTex[kDepthSlot], uvW).r)));

    vec3 nM = safeNormalAt(uv);
    float normalDelta = 0.0;
    if (dM < 0.9999) {
        normalDelta = max(max(1.0 - max(dot(nM, safeNormalAt(uvN)), 0.0),
                              1.0 - max(dot(nM, safeNormalAt(uvS)), 0.0)),
                          max(1.0 - max(dot(nM, safeNormalAt(uvE)), 0.0),
                              1.0 - max(dot(nM, safeNormalAt(uvW)), 0.0)));
    }
    float normalEdge = smoothstep(0.08, 0.42, normalDelta);

    float lM = luminance(resolvedLDR(uv));
    float lumaDelta = max(max(abs(lM - luminance(resolvedLDR(uvN))),
                              abs(lM - luminance(resolvedLDR(uvS)))),
                          max(abs(lM - luminance(resolvedLDR(uvE))),
                              abs(lM - luminance(resolvedLDR(uvW)))));
    float lumaEdge = smoothstep(0.045, 0.20, lumaDelta);

    return clamp(max(max(depthEdge, normalEdge), lumaEdge * 0.72), 0.0, 1.0);
}

float presentationPixelBlend(vec2 uv) {
    float base = presentationPixelStrength();
    if (base <= 0.0) {
        return 0.0;
    }

    float edgeFocus = clamp(pc.pixelizeEdgeFocus, 0.0, 1.0);
    if (edgeFocus <= 0.0) {
        return base;
    }

    float edgeMask = smoothstep(0.05, 0.85, pixelPresentationEdgeMask(uv));
    float interiorBlend = base * (1.0 - 0.62 * edgeFocus);
    return mix(interiorBlend, base, edgeMask);
}

float fxaaGeometryMismatch(vec2 uvA, vec2 uvB) {
    float depthA = texture(uTex[kDepthSlot], clamp(uvA, 0.0, 1.0)).r;
    float depthB = texture(uTex[kDepthSlot], clamp(uvB, 0.0, 1.0)).r;
    if (depthA >= 0.9999 || depthB >= 0.9999) {
        return 1.0;
    }

    vec3 normalA = normalize(texture(uTex[kNormalSlot], clamp(uvA, 0.0, 1.0)).xyz * 2.0 - 1.0);
    vec3 normalB = normalize(texture(uTex[kNormalSlot], clamp(uvB, 0.0, 1.0)).xyz * 2.0 - 1.0);
    float depthEdge = smoothstep(0.00025, 0.004, abs(depthA - depthB));
    float normalEdge = smoothstep(0.10, 0.58, 1.0 - max(dot(normalA, normalB), 0.0));
    return max(depthEdge, normalEdge);
}

vec3 fxaaResolve(vec2 uv) {
    vec2 texel = vec2(pc.texelSizeX, pc.texelSizeY);

    vec3 rgbM = resolvedLDR(uv);
    vec3 rgbN = resolvedLDR(uv + vec2(0.0, texel.y));
    vec3 rgbS = resolvedLDR(uv + vec2(0.0, -texel.y));
    vec3 rgbE = resolvedLDR(uv + vec2(texel.x, 0.0));
    vec3 rgbW = resolvedLDR(uv + vec2(-texel.x, 0.0));

    float lumM = luminance(rgbM);
    float lumN = luminance(rgbN);
    float lumS = luminance(rgbS);
    float lumE = luminance(rgbE);
    float lumW = luminance(rgbW);

    float lumMin = min(lumM, min(min(lumN, lumS), min(lumE, lumW)));
    float lumMax = max(lumM, max(max(lumN, lumS), max(lumE, lumW)));
    float lumRange = lumMax - lumMin;

    if (lumRange < max(EDGE_THRESHOLD_MIN, lumMax * EDGE_THRESHOLD))
    {
        return rgbM;
    }

    vec3 rgbNW = resolvedLDR(uv + vec2(-texel.x, texel.y));
    vec3 rgbNE = resolvedLDR(uv + vec2( texel.x, texel.y));
    vec3 rgbSW = resolvedLDR(uv + vec2(-texel.x, -texel.y));
    vec3 rgbSE = resolvedLDR(uv + vec2( texel.x, -texel.y));

    float lumNW = luminance(rgbNW);
    float lumNE = luminance(rgbNE);
    float lumSW = luminance(rgbSW);
    float lumSE = luminance(rgbSE);

    float edgeH = abs(-2.0 * lumW + lumNW + lumSW)
                + abs(-2.0 * lumM + lumN  + lumS ) * 2.0
                + abs(-2.0 * lumE + lumNE + lumSE);

    float edgeV = abs(-2.0 * lumN + lumNW + lumNE)
                + abs(-2.0 * lumM + lumW  + lumE ) * 2.0
                + abs(-2.0 * lumS + lumSW + lumSE);

    bool isHorizontal = (edgeH >= edgeV);

    float lum1 = isHorizontal ? lumS : lumW;
    float lum2 = isHorizontal ? lumN : lumE;
    float gradient1 = abs(lum1 - lumM);
    float gradient2 = abs(lum2 - lumM);
    bool steepest1 = gradient1 >= gradient2;
    float gradientScaled = 0.25 * max(gradient1, gradient2);

    float stepLength = isHorizontal ? texel.y : texel.x;
    float lumLocalAvg;

    if (steepest1)
    {
        stepLength = -stepLength;
        lumLocalAvg = 0.5 * (lum1 + lumM);
    }
    else
    {
        lumLocalAvg = 0.5 * (lum2 + lumM);
    }

    vec2 currentUV = uv;
    if (isHorizontal)
    {
        currentUV.y += stepLength * 0.5;
    }
    else
    {
        currentUV.x += stepLength * 0.5;
    }

    vec2 offset = isHorizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);

    vec2 uv1 = currentUV - offset;
    vec2 uv2 = currentUV + offset;

    float lumEnd1 = luminance(resolvedLDR(uv1)) - lumLocalAvg;
    float lumEnd2 = luminance(resolvedLDR(uv2)) - lumLocalAvg;

    bool reached1 = abs(lumEnd1) >= gradientScaled;
    bool reached2 = abs(lumEnd2) >= gradientScaled;
    bool reachedBoth = reached1 && reached2;

    if (!reached1) uv1 -= offset;
    if (!reached2) uv2 += offset;

    if (!reachedBoth)
    {
        for (int i = 2; i < 12; ++i)
        {
            if (!reached1)
            {
                lumEnd1 = luminance(resolvedLDR(uv1)) - lumLocalAvg;
                reached1 = abs(lumEnd1) >= gradientScaled;
            }
            if (!reached2)
            {
                lumEnd2 = luminance(resolvedLDR(uv2)) - lumLocalAvg;
                reached2 = abs(lumEnd2) >= gradientScaled;
            }
            if (reached1 && reached2) break;
            if (!reached1) uv1 -= offset;
            if (!reached2) uv2 += offset;
        }
    }

    float dist1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float dist2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);

    bool direction1 = dist1 < dist2;
    float distFinal = min(dist1, dist2);
    float edgeLength = dist1 + dist2;
    edgeLength = max(edgeLength, 1e-6);

    float pixelOffset = -distFinal / edgeLength + 0.5;

    bool correctVariation = ((direction1 ? lumEnd1 : lumEnd2) < 0.0) != (lumM < lumLocalAvg);
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    float lumAvg = (1.0 / 12.0) * (2.0 * (lumN + lumS + lumE + lumW)
                 + lumNW + lumNE + lumSW + lumSE);
    float subpixelOffset1 = clamp(abs(lumAvg - lumM) / max(lumRange, 1e-6), 0.0, 1.0);
    float subpixelOffset2 = (-2.0 * subpixelOffset1 + 3.0) *
                            subpixelOffset1 * subpixelOffset1;
    float subpixelOffsetFinal = subpixelOffset2 * subpixelOffset2 * SUBPIXEL_QUALITY;

    finalOffset = max(finalOffset, subpixelOffsetFinal);

    vec2 finalUV = uv;
    if (isHorizontal)
    {
        finalUV.y += finalOffset * stepLength;
    }
    else
    {
        finalUV.x += finalOffset * stepLength;
    }

    vec3 aa = resolvedLDR(finalUV);

    // fxaa only sees final luminance, so high-contrast dda shadow/contact edges can pull
    // bright lit color across a voxel silhouette and show up as tiny white stitches. preserve
    // more of the center pixel only for that one failure mode: a dark center pixel, a brighter
    // fxaa sample, and a real g-buffer depth/normal discontinuity between them.
    float aaLum = luminance(aa);
    float brightBleed = smoothstep(0.035, 0.18, aaLum - lumM);
    float darkCenter = smoothstep(0.08, 0.30, lumMax - lumM);
    float geometryMismatch = fxaaGeometryMismatch(uv, finalUV);
    float preserveCenter = brightBleed * darkCenter * geometryMismatch;
    return mix(aa, rgbM, preserveCenter * 0.90);
}

vec3 sharpenLdr(vec3 center, vec2 uv) {
    if (pc.sharpenIntensity <= 0.0) return center;

    vec2 texelSize = vec2(pc.texelSizeX, pc.texelSizeY);

    vec3 n = resolvedLDR(uv + vec2(0.0, -1.0) * texelSize);
    vec3 s = resolvedLDR(uv + vec2(0.0,  1.0) * texelSize);
    vec3 e = resolvedLDR(uv + vec2(1.0,  0.0) * texelSize);
    vec3 w = resolvedLDR(uv + vec2(-1.0, 0.0) * texelSize);

    vec3 blur = (n + s + e + w) * 0.25;
    vec3 sharpened = center + (center - blur) * pc.sharpenIntensity;

    return clamp(sharpened, 0.0, 1.0);
}

vec3 applyColorGrade(vec3 color) {
    if (pc.colorGradeEnabled == 0 || pc.colorGradeStrength <= 0.0) {
        return color;
    }

    vec3 base = clamp(color, 0.0, 1.0);
    float luma = luminance(base);
    vec3 graded = mix(vec3(luma), base, max(pc.colorGradeSaturation, 0.0));
    graded = (graded - 0.5) * max(pc.colorGradeContrast, 0.0) + 0.5;

    float temp = clamp(pc.colorGradeTemperature, -1.0, 1.0);
    vec3 warmScale = vec3(1.0 + 0.08 * max(temp, 0.0),
                          1.0 + 0.015 * max(temp, 0.0),
                          1.0 - 0.06 * max(temp, 0.0));
    vec3 coolScale = vec3(1.0 + 0.04 * min(temp, 0.0),
                          1.0 + 0.010 * min(temp, 0.0),
                          1.0 - 0.08 * min(temp, 0.0));
    graded *= mix(coolScale, warmScale, step(0.0, temp));

    return mix(base, clamp(graded, 0.0, 1.0), clamp(pc.colorGradeStrength, 0.0, 1.0));
}

// ============================================================================
// procedural star rendering (world-Space)
// ============================================================================

uint starHash(int x, int y, int z, int seed) {
    uint h = uint(x) * 374761393u;
    h ^= uint(y) * 668265263u;
    h ^= uint(z) * 2147483647u;
    h ^= uint(seed) * 1013904223u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float starHashFloat(int x, int y, int z, int seed) {
    return float(starHash(x, y, z, seed)) / 4294967295.0;
}

// convert screen uv to world-space ray direction
vec3 screenToWorldDir(vec2 uv) {
    // convert uv to clip space (-1 to 1)
    vec4 clipPos = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    // transform to world space
    vec4 worldPos = pc.invViewProj * clipPos;
    // return normalized direction
    return normalize(worldPos.xyz / worldPos.w);
}

// physical camera ray used for medium intersections. keep proceduralStars on
// its established direction function so compatibility-off output is unchanged.
vec3 screenToCameraRayDir(vec2 uv) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 worldPos = pc.invViewProj * clipPos;
    vec3 cameraPosition = vec3(pc.cameraPosX, pc.cameraPosY, pc.cameraPosZ);
    return normalize(worldPos.xyz / worldPos.w - cameraPosition);
}

vec3 proceduralStars(vec2 uv, float time, uint seed, float density) {
    // get world-space direction for this pixel (stars are now fixed in the sky)
    vec3 worldDir = screenToWorldDir(uv);

    // only render stars in upper hemisphere (above horizon)
    if (worldDir.y < 0.0) {
        return vec3(0.0);
    }

    // map world direction to a 3D grid on a virtual sky sphere
    // use spherical coordinates: direction -> (theta, phi) -> grid cell
    float gridScale = 50.0;  // controls star spacing on sky sphere

    // project direction onto a virtual sphere grid
    vec3 gridPos = worldDir * gridScale;
    ivec3 cell = ivec3(floor(gridPos));

    vec3 starLight = vec3(0.0);

    // check this cell and neighbors for stars (3D grid for sky sphere)
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                ivec3 neighborCell = cell + ivec3(dx, dy, dz);

                // determine if this cell has a star
                float r = starHashFloat(neighborCell.x, neighborCell.y, neighborCell.z, int(seed));

                if (r < density) {
                    // star exists - get its position within the cell
                    float sx = starHashFloat(neighborCell.x, neighborCell.y, neighborCell.z, int(seed) + 100);
                    float sy = starHashFloat(neighborCell.x, neighborCell.y, neighborCell.z, int(seed) + 200);
                    float sz = starHashFloat(neighborCell.x, neighborCell.y, neighborCell.z, int(seed) + 300);
                    vec3 starPos = vec3(neighborCell) + vec3(sx, sy, sz);

                    // distance from current sample point to star in grid space
                    vec3 toStar = starPos - gridPos;
                    float dist = length(toStar);

                    // star size with variation
                    float starSize = 0.12 + 0.08 * starHashFloat(neighborCell.x, neighborCell.y, neighborCell.z, int(seed) + 400);

                    // point sprite falloff - sharp bright center
                    float brightness = smoothstep(starSize, starSize * 0.05, dist);
                    brightness = pow(brightness, 0.4);  // sharper falloff

                    // star color variation
                    float colorHash = starHashFloat(neighborCell.x, neighborCell.y, neighborCell.z, int(seed) + 500);
                    vec3 starColor;
                    if (colorHash < 0.5) {
                        starColor = vec3(1.0, 1.0, 1.0);        // white
                    } else if (colorHash < 0.7) {
                        starColor = vec3(1.0, 0.95, 0.85);      // warm white
                    } else if (colorHash < 0.85) {
                        starColor = vec3(0.85, 0.9, 1.0);       // cool blue-white
                    } else {
                        starColor = vec3(1.0, 0.8, 0.6);        // yellow-orange
                    }

                    // twinkle effect
                    float twinklePhase = starHashFloat(neighborCell.x, neighborCell.y, neighborCell.z, int(seed) + 600);
                    float twinkleFreq = 1.0 + 3.0 * starHashFloat(neighborCell.x, neighborCell.y, neighborCell.z, int(seed) + 700);
                    float twinkle = 0.6 + 0.4 * sin(time * pc.starTwinkleSpeed * twinkleFreq + twinklePhase * 6.28318);

                    // brightness variation between stars
                    float baseBrightness = 0.4 + 0.6 * starHashFloat(neighborCell.x, neighborCell.y, neighborCell.z, int(seed) + 800);

                    starLight += starColor * brightness * twinkle * baseBrightness * 2.0;
                }
            }
        }
    }

    return starLight;
}

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);
    float pixelBlend = (pc.mode == 0) ? presentationPixelBlend(uv) : 0.0;
    vec2 pixelUv = (pixelBlend > 0.0) ? presentationPixelUv(uv) : uv;
    vec2 effectUv = mix(uv, pixelUv, pixelBlend);

    vec4 sceneSample = mix(texture(uTex[0], uv), texture(uTex[0], pixelUv), pixelBlend);
    vec3 scene = sceneSample.rgb;
    vec3 resolvedHdr = mix(resolvedHDR(uv), resolvedHDR(pixelUv), pixelBlend);
    vec3 c = scene;
    vec3 atmosphericStars = vec3(0.0);

    if (pc.mode == 0 && pc.starsEnabled != 0 &&
        uAtmosphere.atmosphere0.x > 0.0) {
        float depth = texture(uTex[kDepthSlot], effectUv).r;
        if (depth >= 0.9999) {
            atmosphericStars = proceduralStars(
                effectUv, pc.time, pc.starSeed, pc.starDensity);
            vec3 viewDirection = screenToCameraRayDir(effectUv);
            float opticalDepth = sceneAtmosphereSkyAirOpticalDepthWithWater(
                uAtmosphere.atmosphere0.x, uAtmosphere.atmosphere0.y,
                uAtmosphere.atmosphere0.z,
                vec3(pc.cameraPosX, pc.cameraPosY, pc.cameraPosZ),
                viewDirection);
            atmosphericStars = attenuateSceneAtmosphereSkyEmission(
                uAtmosphere.atmosphere0.x, opticalDepth, atmosphericStars);
        }
    }

    if (pc.mode >= 1 && pc.mode <= 3) {
        c = texture(uTex[pc.mode], uv).rgb;
    } else if (pc.mode == 4) {
        ivec2 size = max(textureSize(uTex[kShadowSlot], 0), ivec2(1));
        vec2 texel = 1.0 / vec2(size);

        float d = texture(uTex[kShadowSlot], uv).r;
        // use the tonemap toggle as a debug switch:
        // - tonemap on  => contrast + edge overlay
        // - tonemap off => raw depth
        if (pc.tonemapOn == 0) {
            c = vec3(d);
        } else {
            float base = pow(clamp(1.0 - d, 0.0, 1.0), 0.2);

            vec2 uvx = clamp(uv + vec2(texel.x, 0.0), 0.0, 1.0);
            vec2 uvy = clamp(uv + vec2(0.0, texel.y), 0.0, 1.0);
            float dx = texture(uTex[kShadowSlot], uvx).r;
            float dy = texture(uTex[kShadowSlot], uvy).r;
            float dd = abs(d - dx) + abs(d - dy);
            float edge = smoothstep(0.0002, 0.002, dd);

            c = mix(vec3(base), vec3(1.0), edge);
        }
    } else if (pc.mode == 5) {
        c = vec3(sceneSample.a);
    } else if (pc.mode == 6) {
        c = (pc.bloomEnabled != 0)
                ? texture(uTex[kBloomExtractSlot], uv).rgb
                : vec3(0.0);
    } else if (pc.mode == 7) {
        c = (pc.bloomEnabled != 0)
                ? texture(uTex[kBloomBlurSlot], uv).rgb
                : vec3(0.0);
    } else if (pc.mode == 8) {
        vec3 bloom = (pc.bloomEnabled != 0)
                         ? texture(uTex[kBloomBlurSlot], uv).rgb * pc.bloomIntensity
                         : vec3(0.0);
        c = max((scene + bloom) * max(pc.exposure, 0.0), vec3(0.0));
    } else if (pc.mode == 9) {
        float lum = luminance(scene);
        float t = clamp(log2(lum + 1.0) / 4.0, 0.0, 1.0);
        c = heatmap(t);
    } else if (pc.mode == 10) {
        vec2 bn = STOCHASTIC_BLUE_NOISE_FETCH(ivec2(gl_FragCoord.xy));
        c = vec3(bn.r, bn.g, 0.0);
    } else if (pc.mode == 11) {
        vec2 noise = stochasticNoise2D(ivec2(gl_FragCoord.xy), pc.frameIndex);
        c = vec3(noise, 0.0);
    } else if (pc.mode == 12) {
        vec3 normal = texture(uTex[kNormalSlot], uv).xyz * 2.0 - 1.0;
        vec2 noise = stochasticNoise2D(ivec2(gl_FragCoord.xy), pc.frameIndex);
        vec3 dir = sampleHemisphere(noise, normalize(normal));
        c = dir * 0.5 + 0.5;
    } else if (pc.mode == 13) {
        float waterDist = texture(uTex[kWaterDistSlot], uv).r;
        c = waterDistanceDebug(waterDist);
    } else if (pc.mode == 14) {
        float analyticDist = texture(uTex[kWaterDistSlot], uv).r;
        float legacyDist = texture(uTex[kLegacyWaterDistSlot], uv).r;
        float delta = analyticDist - legacyDist;
        float mag = clamp(abs(delta) / 6.0, 0.0, 1.0);
        if (mag < 0.01) {
            c = vec3(0.0);
        } else if (delta > 0.0) {
            c = mix(vec3(0.0), vec3(1.0, 0.32, 0.08), mag);
        } else {
            c = mix(vec3(0.0), vec3(0.0, 0.48, 1.0), mag);
        }
    } else if (pc.mode == 15) {
        // record-friendly view of the lit scene after water/glass composition,
        // before temporal reconstruction and presentation-only effects.
        c = tonemapScene(scene);
    } else if (pc.mode == 16) {
        // record-friendly view after temporal reconstruction but before final
        // presentation grading, sharpening, vignette, and grain.
        c = tonemapScene(resolvedHdr);
    } else if (pc.mode == 17) {
        // direct display of normalized lighting debug signals such as raw and
        // resolved dda shadow visibility or ambient occlusion.
        c = scene;
    }

    if (pc.mode == 0) {
        c = tonemapScene(resolvedHdr);
        vec3 atmosphericStarLdrDelta =
            tonemapScene(resolvedHdr + atmosphericStars) - c;

        if (pc.tonemapOn != 0) {
            if (pc.fxaaEnabled > 0.5 && pixelBlend <= 0.05) {
                c = fxaaResolve(uv);
            }

            c = applyMaterialDetailPresentation(c, effectUv);

            c = sharpenLdr(c, effectUv);

            // star extinction is resolved before presentation grading. express
            // the hdr star as a tonemapped delta so the established post-FXAA
            // point-sprite behavior remains intact.
            c += atmosphericStarLdrDelta;

            c = applyColorGrade(c);

            if (pc.vignetteStrength > 0.0) {
                vec2 centered = uv - 0.5;
                float dist = dot(centered, centered);
                float vignette = 1.0 - smoothstep(0.2, 0.5, dist);
                c *= mix(1.0, vignette, pc.vignetteStrength);
            }

            if (pc.grainStrength > 0.0) {
                float g = filmGrain(effectUv) - 0.5;
                c += g * pc.grainStrength;
            }
        }
        else {
            c += atmosphericStarLdrDelta;
        }
    }

    // preserve the established compatibility-off star path exactly. atmosphere
    // enabled stars were resolved above so tonemap and grade remain downstream.
    if (pc.mode == 0 && pc.starsEnabled != 0 &&
        uAtmosphere.atmosphere0.x <= 0.0) {
        float depth = texture(uTex[kDepthSlot], effectUv).r;
        if (depth >= 0.9999) {
            vec3 stars = proceduralStars(effectUv, pc.time, pc.starSeed, pc.starDensity);
            c += stars;
        }
    }

    vec3 outColor = c;
    if ((pc.mode == 0 || pc.mode == 15 || pc.mode == 16) &&
        pc.tonemapOn != 0 && pc.swapchainIsSRGB > 0.5) {
        outColor = toLinear(c);
    }

    oColor = vec4(outColor, 1.0);
}
