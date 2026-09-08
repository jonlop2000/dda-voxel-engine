#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uColorCurrent;
layout(set = 0, binding = 1) uniform sampler2D uColorHistory;
layout(set = 0, binding = 2) uniform sampler2D uMotion;  // velocity.xy, reactive.z
layout(set = 0, binding = 3) uniform sampler2D uDepth;
layout(set = 0, binding = 4) uniform sampler2D uBloom;
layout(set = 0, binding = 5) uniform sampler2D uNormal;
layout(set = 0, binding = 6) uniform sampler2D uDepthHistory;  // previous frame's depth

layout(push_constant) uniform PC {
    vec2 resolution;
    vec2 texelSize;
    vec2 jitter;
    vec2 prevJitter;
    float similarityThreshold;
    float velocityScale;
    float blendFactorMin;
    float blendFactorMax;
    float bloomIntensity;
    int enabled;
    int debugMode;
    float depthEdgeThreshold;        // day 12E: depth edge rejection threshold
    float nearPlane;                 // for depth linearization
    float crossFrameDepthThreshold;  // cross-frame depth rejection threshold
    float farPlane;                  // for depth linearization
    float colorVarianceThreshold;    // local color variance rejection threshold
    float softEdgeStrength;          // full-scene, edge-local reconstruction strength
} pc;

vec3 RGBToYCoCg(vec3 rgb) {
    return vec3(
        0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,
        0.5 * rgb.r - 0.5 * rgb.b,
        -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b
    );
}

vec3 YCoCgToRGB(vec3 ycocg) {
    return vec3(
        ycocg.x + ycocg.y - ycocg.z,
        ycocg.x + ycocg.z,
        ycocg.x - ycocg.y - ycocg.z
    );
}

vec3 sampleCurrent(vec2 uv) {
    vec3 scene = texture(uColorCurrent, uv).rgb;
    vec3 bloom = texture(uBloom, uv).rgb * pc.bloomIntensity;
    return scene + bloom;
}

vec3 sampleHistory(vec2 uv) {
    return texture(uColorHistory, uv).rgb;
}

// forward declaration so helpers can use it.
bool isSkyDepth(float d);

vec3 sampleNormal(vec2 uv) {
    vec3 n = texture(uNormal, uv).rgb * 2.0 - 1.0;
    float len = length(n);
    if (len < 1e-4) {
        return vec3(0.0, 0.0, 1.0);
    }
    return n / len;
}

void samplePlusPatternCurrent(vec2 uv, out vec3 center, out vec3 minColor, out vec3 maxColor,
                              out vec3 crossAverage) {
    vec3 c = sampleCurrent(uv);
    float centerDepth = texture(uDepth, uv).r;
    bool centerIsSky = isSkyDepth(centerDepth);

    center = c;
    minColor = c;
    maxColor = c;
    vec3 crossSum = c * 4.0;

    vec2 offsets[4] = vec2[4](
        vec2(0.0, -pc.texelSize.y),
        vec2(0.0,  pc.texelSize.y),
        vec2(-pc.texelSize.x, 0.0),
        vec2( pc.texelSize.x, 0.0)
    );

    for (int i = 0; i < 4; i++) {
        vec2 nuv = uv + offsets[i];
        float d = texture(uDepth, nuv).r;
        bool neighborIsSky = isSkyDepth(d);
        vec3 s = sampleCurrent(nuv);
        crossSum += s;
        if (neighborIsSky == centerIsSky) {
            minColor = min(minColor, s);
            maxColor = max(maxColor, s);
        }
    }
    crossAverage = crossSum * 0.125;
}

void samplePlusPatternHistory(vec2 uv, out vec3 center, out vec3 minColor, out vec3 maxColor) {
    vec3 c = sampleHistory(uv);
    vec3 t = sampleHistory(uv + vec2(0.0, -pc.texelSize.y));
    vec3 b = sampleHistory(uv + vec2(0.0,  pc.texelSize.y));
    vec3 l = sampleHistory(uv + vec2(-pc.texelSize.x, 0.0));
    vec3 r = sampleHistory(uv + vec2( pc.texelSize.x, 0.0));

    center = c;
    minColor = min(min(min(min(c, t), b), l), r);
    maxColor = max(max(max(max(c, t), b), l), r);
}

float colorSimilarity(vec3 a, vec3 b) {
    vec3 diff = abs(a - b);
    return dot(diff, vec3(0.299, 0.587, 0.114));
}

// day 12E: check if depth value represents sky (far plane)
bool isSkyDepth(float d) {
    float nearSkyThreshold = 0.0001;  // reversed-Z: sky at 0
    float farSkyThreshold = 0.9999;   // standard-Z: sky at 1
    return (d < nearSkyThreshold) || (d > farSkyThreshold);
}

// day 12E: detect depth edges by sampling neighborhood
// returns 0 for flat areas, 1 for strong edges (silhouettes)
float detectDepthEdge(vec2 uv) {
    float centerDepth = texture(uDepth, uv).r;

    // sample cross pattern neighbors
    float depthT = texture(uDepth, uv + vec2(0.0, -pc.texelSize.y)).r;
    float depthB = texture(uDepth, uv + vec2(0.0,  pc.texelSize.y)).r;
    float depthL = texture(uDepth, uv + vec2(-pc.texelSize.x, 0.0)).r;
    float depthR = texture(uDepth, uv + vec2( pc.texelSize.x, 0.0)).r;

    bool centerIsSky = isSkyDepth(centerDepth);
    bool tIsSky = isSkyDepth(depthT);
    bool bIsSky = isSkyDepth(depthB);
    bool lIsSky = isSkyDepth(depthL);
    bool rIsSky = isSkyDepth(depthR);
    bool anyNeighborSky = tIsSky || bIsSky || lIsSky || rIsSky;

    // strong edge if mixing sky and geometry
    if (centerIsSky != anyNeighborSky) {
        return 1.0;  // full rejection at sky/geometry boundary
    }

    // for geometry-only areas, use relative depth comparison
    float maxDiff = max(max(abs(centerDepth - depthT), abs(centerDepth - depthB)),
                        max(abs(centerDepth - depthL), abs(centerDepth - depthR)));
    float scaledDiff = maxDiff / max(centerDepth, 0.001);

    float threshold = max(pc.depthEdgeThreshold, 0.001);
    return smoothstep(0.0, threshold, scaledDiff);
}

// day 12E: detect normal edges by sampling neighborhood
float detectNormalEdge(vec2 uv) {
    float centerDepth = texture(uDepth, uv).r;
    if (isSkyDepth(centerDepth)) {
        return 0.0;
    }

    vec3 centerNormal = sampleNormal(uv);
    vec2 offsets[4] = vec2[4](
        vec2(0.0, -pc.texelSize.y),
        vec2(0.0,  pc.texelSize.y),
        vec2(-pc.texelSize.x, 0.0),
        vec2( pc.texelSize.x, 0.0)
    );

    float maxDiff = 0.0;
    for (int i = 0; i < 4; i++) {
        vec2 neighborUV = uv + offsets[i];
        float neighborDepth = texture(uDepth, neighborUV).r;
        if (isSkyDepth(neighborDepth)) {
            continue;
        }
        vec3 neighborNormal = sampleNormal(neighborUV);
        float d = 1.0 - clamp(dot(centerNormal, neighborNormal), -1.0, 1.0);
        maxDiff = max(maxDiff, clamp(d, 0.0, 1.0));
    }

    float threshold = max(pc.depthEdgeThreshold, 0.001);
    return smoothstep(0.0, threshold, maxDiff);
}

// linearize depth for meaningful cross-frame comparison
float linearizeDepth(float d) {
    // standard perspective linearization
    float nf = pc.nearPlane * pc.farPlane;
    float range = pc.farPlane - pc.nearPlane;
    return nf / (pc.farPlane - d * range);
}

// cross-frame depth rejection: compare current depth vs history depth at reprojected uv
float detectCrossFrameDepthChange(vec2 currentUV, vec2 historyUV) {
    float depthCurrent = texture(uDepth, currentUV).r;
    float depthHistory = texture(uDepthHistory, historyUV).r;

    // sky mismatch = full rejection (one is sky, other is geometry)
    bool currentIsSky = isSkyDepth(depthCurrent);
    bool historyIsSky = isSkyDepth(depthHistory);
    if (currentIsSky != historyIsSky) {
        return 1.0;  // full rejection - disocclusion detected
    }
    if (currentIsSky) {
        return 0.0;  // both sky - no rejection needed
    }

    // linearize for meaningful comparison at all distances
    float linearCurrent = linearizeDepth(depthCurrent);
    float linearHistory = linearizeDepth(depthHistory);

    // relative depth difference (percentage-based)
    float relDiff = abs(linearCurrent - linearHistory) / max(linearCurrent, 0.001);

    float threshold = max(pc.crossFrameDepthThreshold, 0.001);
    return smoothstep(0.0, threshold, relDiff);
}

// detect high local color variance in current neighborhood.
// at shadow boundaries, neighbors include both shadowed (dark) and lit (bright) pixels,
// producing high variance. forces taa to prefer current frame at such edges.
float detectColorVariance(vec3 currentMin, vec3 currentMax) {
    vec3 rangeRGB = currentMax - currentMin;
    float lumaRange = dot(rangeRGB, vec3(0.2126, 0.7152, 0.0722));
    float threshold = max(pc.colorVarianceThreshold, 0.001);
    return smoothstep(0.0, threshold, lumaRange);
}

// day 12E: get color from nearest geometry neighbor when center is sky at edge
vec3 getEdgeCorrectedColor(vec2 uv) {
    float centerDepth = texture(uDepth, uv).r;

    if (!isSkyDepth(centerDepth)) {
        // center is geometry, use normal sampling
        return sampleCurrent(uv);
    }

    // center is sky - check if we're at an edge with geometry neighbors
    vec2 offsets[4] = vec2[4](
        vec2(0.0, -pc.texelSize.y),  // top
        vec2(0.0,  pc.texelSize.y),  // bottom
        vec2(-pc.texelSize.x, 0.0),  // left
        vec2( pc.texelSize.x, 0.0)   // right
    );

    // find closest geometry neighbor
    float closestDepth = 0.0;
    vec3 closestColor = sampleCurrent(uv);  // fallback to center
    bool foundGeometry = false;

    for (int i = 0; i < 4; i++) {
        vec2 neighborUV = uv + offsets[i];
        float neighborDepth = texture(uDepth, neighborUV).r;

        if (!isSkyDepth(neighborDepth)) {
            // this neighbor is geometry
            if (!foundGeometry || neighborDepth > closestDepth) {
                closestDepth = neighborDepth;
                closestColor = sampleCurrent(neighborUV);
                foundGeometry = true;
            }
        }
    }

    return foundGeometry ? closestColor : sampleCurrent(uv);
}

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);

    if (pc.enabled == 0) {
        outColor = vec4(sampleCurrent(uv), 1.0);
        return;
    }

    vec3 currentCenter, currentMin, currentMax, currentCrossAverage;
    samplePlusPatternCurrent(uv, currentCenter, currentMin, currentMax,
                             currentCrossAverage);

    // day 12E: if center is sky at a silhouette edge, use geometry neighbor color
    if (pc.depthEdgeThreshold > 0.0) {
        float depthEdgeEarly = detectDepthEdge(uv);
        if (depthEdgeEarly > 0.5) {
            currentCenter = getEdgeCorrectedColor(uv);
        }
    }

    vec3 motionData = texture(uMotion, uv).rgb;
    vec2 velocity = motionData.rg;
    float reactiveMask = motionData.b;

    // color history is stored on a stable pixel grid; use unjittered reprojection.
    vec2 historyUV = uv - velocity;
    // depth history comes from a jittered depth buffer; align previous depth sampling to the
    // current jitter to avoid false cross-frame rejection due to sub-pixel jitter.
    vec2 jitterDelta = (pc.jitter - pc.prevJitter) * pc.texelSize;
    vec2 depthHistoryUV = uv - velocity - jitterDelta;
    bool validHistory = historyUV.x >= 0.0 && historyUV.x <= 1.0 &&
                        historyUV.y >= 0.0 && historyUV.y <= 1.0;
    if (!validHistory) {
        outColor = vec4(currentCenter, 1.0);
        return;
    }

    vec3 historyCenter, historyMin, historyMax;
    samplePlusPatternHistory(historyUV, historyCenter, historyMin, historyMax);
    vec3 historyClamped = clamp(historyCenter, historyMin, historyMax);
    vec3 historyYCoCg = RGBToYCoCg(historyClamped);
    vec3 minYCoCg = RGBToYCoCg(currentMin);
    vec3 maxYCoCg = RGBToYCoCg(currentMax);
    vec3 clampedYCoCg = clamp(historyYCoCg, minYCoCg, maxYCoCg);
    vec3 historyClampedCurrent = YCoCgToRGB(clampedYCoCg);

    float similarity = colorSimilarity(currentCenter, historyClampedCurrent);
    float threshold = max(pc.similarityThreshold, 1e-5);
    float velocityMag = length(velocity * pc.resolution);

    // day 12E: detect depth edges (silhouettes)
    float depthEdge = 0.0;
    if (pc.depthEdgeThreshold > 0.0) {
        depthEdge = detectDepthEdge(uv);
    }
    float normalEdge = 0.0;
    if (pc.depthEdgeThreshold > 0.0) {
        normalEdge = detectNormalEdge(uv);
    }

    // cross-frame depth rejection (disocclusion detection)
    float crossFrameReject = 0.0;
    if (pc.crossFrameDepthThreshold > 0.0) {
        crossFrameReject = detectCrossFrameDepthChange(uv, depthHistoryUV);
    }

    // local color variance rejection (shadow edge detection)
    float colorVariance = 0.0;
    if (pc.colorVarianceThreshold > 0.0) {
        colorVariance = detectColorVariance(currentMin, currentMax);
    }

    float t = similarity / threshold;
    t += velocityMag * pc.velocityScale / max(pc.resolution.x, 1.0);
    t += reactiveMask;
    // edge bias: prefer current a bit on edges, but don't fully reject history.
    float edgeRejectSoft = max(depthEdge, normalEdge);
    t += edgeRejectSoft * 0.5;
    // use max() so cross-frame rejection takes priority when it fires
    // (disocclusion should reject regardless of color similarity)
    t = max(t, crossFrameReject);
    t = clamp(t, 0.0, 1.0);

    vec3 finalHistory = historyClampedCurrent;

    float blendFactor = mix(pc.blendFactorMin, pc.blendFactorMax, t);

    // day 12E: at cross-frame disocclusion, force more of the current frame.
    {
        float edgeMaskStrong = max(crossFrameReject, colorVariance);
        blendFactor = mix(blendFactor, 1.0, edgeMaskStrong);
        // soft boost at normal/depth edges to reduce ghosting without jittering.
        blendFactor = mix(blendFactor, min(1.0, blendFactor + 0.25), edgeRejectSoft);
    }

    blendFactor = clamp(blendFactor, 0.0, 1.0);

    vec3 result = mix(finalHistory, currentCenter, blendFactor);

    // the historical taa resolve intentionally preserved nearly all no-AA edge energy.
    // for soft-voxel profiles, reuse the already-sampled plus neighborhoods to apply a
    // one-pixel, center-weighted reconstruction only at real geometry or tonal edges.
    // this leaves broad texture/sky regions untouched, avoids a separate full-screen pass,
    // and keeps disocclusions on the current frame through the existing blend factor.
    if (pc.softEdgeStrength > 0.0) {
        float tonalSpan = colorSimilarity(currentMin, currentMax);
        float tonalEdge = smoothstep(0.035, 0.18, tonalSpan);
        float reconstructionMask = max(max(depthEdge, normalEdge), tonalEdge * 0.55);
        float reconstructionStrength =
            clamp(pc.softEdgeStrength, 0.0, 1.0) * reconstructionMask;
        result = mix(result, currentCrossAverage, reconstructionStrength);
    }

    if (pc.debugMode == 1) {
        outColor = vec4(t, 1.0 - t, 0.0, 1.0);
        return;
    }
    if (pc.debugMode == 2) {
        outColor = vec4(reactiveMask, reactiveMask, reactiveMask, 1.0);
        return;
    }
    if (pc.debugMode == 3) {
        float velVis = clamp(velocityMag * 10.0 / max(pc.resolution.x, 1.0), 0.0, 1.0);
        outColor = vec4(velVis, 0.0, 1.0 - velVis, 1.0);
        return;
    }
    // day 12E: debug mode 4 - visualize depth edges
    if (pc.debugMode == 4) {
        float edge = detectDepthEdge(uv);
        outColor = vec4(edge, edge, 0.0, 1.0);
        return;
    }
    // day 12E: debug mode 5 - visualize normal edges
    if (pc.debugMode == 5) {
        float edge = detectNormalEdge(uv);
        outColor = vec4(edge, edge, 0.0, 1.0);
        return;
    }
    // debug mode 6 - visualize cross-frame depth rejection (red = rejected, blue = accepted)
    if (pc.debugMode == 6) {
        float reject = detectCrossFrameDepthChange(uv, depthHistoryUV);
        outColor = vec4(reject, 0.0, 1.0 - reject, 1.0);
        return;
    }
    // debug mode 7 - visualize local color variance (bright = high variance / shadow edges)
    if (pc.debugMode == 7) {
        float cv = detectColorVariance(currentMin, currentMax);
        outColor = vec4(cv, cv * 0.5, 0.0, 1.0);
        return;
    }

    outColor = vec4(result, 1.0);
}
