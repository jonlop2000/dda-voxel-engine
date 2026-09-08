#version 450
#extension GL_GOOGLE_include_directive : enable

// aaa voxel glass refraction pass
// applies snell's law refraction, beer-Lambert absorption, and fresnel reflection
// to voxel glass pixels detected via g-buffer data.
// features: adaptive edge blur, hybrid ssr reflections, seamless glass plane rendering.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

// g-buffer inputs
layout(set = 0, binding = 0) uniform sampler2D gAlbedo;    // a=glassMask
layout(set = 0, binding = 1) uniform sampler2D gNormal;    // rgb=normal, a=unused
layout(set = 0, binding = 2) uniform sampler2D gMaterial;  // r=rough, g=metal, b=ao, a=emissive
layout(set = 0, binding = 3) uniform sampler2D gDepth;     // depth buffer
layout(set = 0, binding = 4) uniform sampler2D gVelocity;  // rg=velocity, b=reactive, a=thickness
layout(set = 0, binding = 5) uniform sampler2D uLitScene;  // lit scene to refract through

layout(set = 1, binding = 0) uniform UBO {
    mat4 invViewProj;
    vec4 camPos;
    vec4 tint;           // rgb tint color
    vec4 params0;        // x=absorption, y=refractStrength, z=IOR, w=debugMode
    vec4 params1;        // x=invResX, y=invResY, z=maxThickness, w=reflectionStrength
    vec4 reflectionColor;
    vec4 atmosphere0;    // density, height falloff, base height, sun phase strength
    vec4 atmosphere1;    // x=active water-volume count
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
#include "atmosphere_water_path.glsl"

// schlick's fresnel approximation
float schlickFresnel(float cosTheta, float F0) {
    float m = clamp(1.0 - cosTheta, 0.0, 1.0);
    float m2 = m * m;
    float m4 = m2 * m2;
    return F0 + (1.0 - F0) * m4 * m;
}

// reconstruct world position from depth
vec3 reconstructWorldPos(vec2 uv, float depth01) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth01, 1.0);
    vec4 wpos = u.invViewProj * ndc;
    return wpos.xyz / wpos.w;
}

// enhanced procedural sky for reflection fallback
vec3 sampleSkyReflection(vec3 reflectDir) {
    float y = reflectDir.y;
    vec3 horizonColor = vec3(0.5, 0.6, 0.75);
    vec3 zenithColor = u.reflectionColor.rgb;
    vec3 groundColor = vec3(0.15, 0.18, 0.22);

    if (y > 0.0) {
        // sky gradient with softer transition
        float t = pow(y, 0.4);
        return mix(horizonColor, zenithColor, t);
    }
    // ground reflection
    float t = pow(-y, 0.6);
    return mix(horizonColor, groundColor, t);
}

// =========================================
// heavy gaussian blur for seamless glass
// =========================================
struct SmoothResult {
    vec3 normal;
    float thickness;
    float edgeFactor;
};

SmoothResult heavyGlassBlur(vec2 uv, vec2 texel, vec3 centerNormal, float centerThickness, float centerDepth) {
    SmoothResult result;
    result.edgeFactor = 0.0;

    // large 5x5 gaussian kernel for heavy smoothing
    // weights approximate a gaussian with sigma ~1.5
    const int KERNEL_SIZE = 25;
    vec2 offsets[25];
    float weights[25];

    // build 5x5 kernel
    int idx = 0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            offsets[idx] = vec2(float(x) * texel.x, float(y) * texel.y);
            // gaussian weight based on distance
            float dist = sqrt(float(x*x + y*y));
            weights[idx] = exp(-dist * dist / 3.0);  // sigma^2 = 1.5
            idx++;
        }
    }

    vec3 normalAccum = vec3(0.0);
    float normalWeight = 0.0;
    float thicknessAccum = 0.0;
    float thicknessWeight = 0.0;
    float maxNormalDiff = 0.0;

    for (int i = 0; i < KERNEL_SIZE; ++i) {
        vec2 suv = clamp(uv + offsets[i], 0.001, 0.999);
        float neighMask = texture(gAlbedo, suv).a;

        if (neighMask > 0.5) {
            vec3 neighNormal = normalize(texture(gNormal, suv).rgb * 2.0 - 1.0);
            float neighThickness = texture(gVelocity, suv).a;
            float neighDepth = texture(gDepth, suv).r;

            // depth-aware weighting: reduce weight for samples at very different depths
            float depthDiff = abs(neighDepth - centerDepth);
            float depthWeight = exp(-depthDiff * 50.0);  // sharp falloff for depth discontinuities

            // normal discontinuity detection
            float normalDiff = 1.0 - max(dot(centerNormal, neighNormal), 0.0);
            maxNormalDiff = max(maxNormalDiff, normalDiff);

            float w = weights[i] * depthWeight;

            normalAccum += neighNormal * w;
            normalWeight += w;
            thicknessAccum += neighThickness * w;
            thicknessWeight += w;
        }
    }

    result.edgeFactor = smoothstep(0.02, 0.3, maxNormalDiff);

    if (normalWeight > 0.01) {
        // very aggressive blending - almost fully replace with average
        float blendStrength = 0.95;  // 95% blend to averaged values

        vec3 avgNormal = normalAccum / normalWeight;
        float avgThickness = thicknessAccum / thicknessWeight;

        result.normal = normalize(mix(centerNormal, avgNormal, blendStrength));
        result.thickness = mix(centerThickness, avgThickness, blendStrength);
    } else {
        result.normal = centerNormal;
        result.thickness = centerThickness;
    }

    return result;
}

// =========================================
// hybrid ssr - screen-space reflection with fallback
// =========================================
vec3 sampleHybridReflection(vec2 uv, vec3 reflectDir, vec3 viewDir, float depth01) {
    vec2 texel = vec2(u.params1.x, u.params1.y);

    // simple screen-space reflection: offset uv by reflection direction
    // this is a simplified ssr that works well for flat/near-flat surfaces
    vec2 reflectOffset = reflectDir.xy * 0.15;  // scale for screen-space
    vec2 reflectUV = uv + reflectOffset;

    // validate reflection sample
    bool validReflection = true;

    // check bounds
    if (reflectUV.x < 0.01 || reflectUV.x > 0.99 || reflectUV.y < 0.01 || reflectUV.y > 0.99) {
        validReflection = false;
    }

    // check if we're sampling glass (would cause recursion)
    if (validReflection) {
        float reflectMask = texture(gAlbedo, reflectUV).a;
        if (reflectMask > 0.5) {
            validReflection = false;
        }
    }

    // check depth - reflection should sample something behind or at similar depth
    if (validReflection) {
        float reflectDepth = texture(gDepth, reflectUV).r;
        // allow sampling if depth is similar or further
        if (reflectDepth < depth01 - 0.05) {
            validReflection = false;
        }
    }

    vec3 ssrColor = vec3(0.0);
    float ssrWeight = 0.0;

    if (validReflection) {
        ssrColor = texture(uLitScene, reflectUV).rgb;
        // boost ssr slightly for visibility
        ssrColor = ssrColor * 1.1 + vec3(0.02);
        ssrWeight = 0.7;  // blend with procedural
    }

    // procedural sky fallback
    vec3 skyColor = sampleSkyReflection(reflectDir);

    // blend ssr with procedural
    return mix(skyColor, ssrColor, ssrWeight);
}

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);
    vec2 texel = vec2(u.params1.x, u.params1.y);

    // sample g-buffer
    vec4 albedoSample = texture(gAlbedo, uv);
    float glassMask = albedoSample.a;

    // early out for non-glass pixels
    if (glassMask < 0.5) {
        oColor = texture(uLitScene, uv);
        return;
    }

    vec3 glassTint = clamp(u.tint.rgb, 0.0, 1.0);
    vec3 rawNormal = normalize(texture(gNormal, uv).rgb * 2.0 - 1.0);
    float rawThickness = texture(gVelocity, uv).a;
    float depth01 = texture(gDepth, uv).r;

    // =========================================
    // heavy blur for seamless glass plane
    // =========================================
    SmoothResult smoothed = heavyGlassBlur(uv, texel, rawNormal, rawThickness, depth01);
    vec3 normal = smoothed.normal;
    float thicknessNorm = smoothed.thickness;
    float thickness = thicknessNorm * u.params1.z;
    vec3 worldPos = reconstructWorldPos(uv, depth01);
    vec3 viewDir = normalize(u.camPos.xyz - worldPos);

    // extract parameters
    float absorption = max(u.params0.x, 0.0);
    float refractStrength = u.params0.y;
    float IOR = max(u.params0.z, 1.001);
    int debugMode = int(u.params0.w + 0.5);
    float reflectionStrength = u.params1.w;

    // =========================================
    // 1. screen-Space refraction (cleaner)
    // =========================================
    float eta = 1.0 / IOR;
    vec3 refractDir = refract(-viewDir, normal, eta);
    if (dot(refractDir, refractDir) < 1e-5) {
        refractDir = -viewDir;
    }

    // smoother refraction offset
    vec2 refractOffset = (refractDir.xy - (-viewDir.xy)) * refractStrength * thickness;
    vec2 refractedUV = uv + refractOffset;

    // soft clamp near screen edges
    vec2 edgeDist = min(refractedUV, 1.0 - refractedUV);
    float edgeFade = smoothstep(0.0, 0.08, min(edgeDist.x, edgeDist.y));
    refractedUV = mix(uv, refractedUV, edgeFade);
    refractedUV = clamp(refractedUV, 0.001, 0.999);

    // depth validation
    float refractedDepth = texture(gDepth, refractedUV).r;
    if (refractedDepth < depth01 - 0.01) {
        refractedUV = mix(refractedUV, uv, 0.9);
    }

    // self-sampling guard
    float refractedGlassMask = texture(gAlbedo, refractedUV).a;
    if (refractedGlassMask > 0.5 && abs(refractedDepth - depth01) < 0.005) {
        refractedUV = mix(refractedUV, uv, 0.9);
    }

    vec3 sceneColor = texture(uLitScene, refractedUV).rgb;

    // =========================================
    // 2. clean transmission (reduced frosting)
    // =========================================
    // only apply absorption if explicitly requested
    vec3 transmission = vec3(1.0);
    if (absorption > 0.001) {
        vec3 tintAbsorptionCoeff = (1.0 - glassTint) * absorption;
        float neutralAbsorption = absorption * 0.1;
        vec3 absorptionCoeff = tintAbsorptionCoeff + vec3(neutralAbsorption);
        transmission = exp(-absorptionCoeff * max(thickness, 0.0));
    }
    vec3 refractedColor = sceneColor * transmission;

    // =========================================
    // 3. fresnel reflection with hybrid ssr
    // =========================================
    float NoV = max(dot(normal, viewDir), 0.0);
    float r0 = (1.0 - IOR) / (1.0 + IOR);
    float F0 = max(r0 * r0, 0.02);
    float fresnel = schlickFresnel(NoV, F0);

    vec3 reflectDir = reflect(-viewDir, normal);

    // hybrid reflection: ssr where valid, procedural fallback
    vec3 reflectionColor = sampleHybridReflection(uv, reflectDir, viewDir, depth01);

    // =========================================
    // 4. final composite
    // =========================================
    // physical fresnel baseline
    float baseFresnel = fresnel * 0.5;
    float artistReflection = fresnel * reflectionStrength * 0.5;
    float reflectionMix = clamp(baseFresnel + artistReflection, 0.0, 0.95);

    vec3 finalColor = mix(refractedColor, reflectionColor, reflectionMix);

    // debug modes
    if (debugMode == 1) {
        // thickness visualization
        finalColor = vec3(thicknessNorm);
    } else if (debugMode == 2) {
        // refraction offset visualization
        finalColor = vec3(length(refractOffset) * 10.0);
    } else if (debugMode == 3) {
        // fresnel visualization
        finalColor = vec3(fresnel);
    } else if (debugMode == 4) {
        // glass mask visualization
        finalColor = vec3(glassMask);
    } else if (debugMode == 5) {
        // transmission visualization
        finalColor = transmission;
    } else if (debugMode == 6) {
        // edge factor visualization (voxel seams)
        finalColor = vec3(smoothed.edgeFactor);
    } else if (debugMode == 7) {
        // normal smootheding comparison (r=raw, g=smoothed)
        float rawDot = max(dot(rawNormal, viewDir), 0.0);
        float smoothedDot = max(dot(normal, viewDir), 0.0);
        finalColor = vec3(rawDot, smoothedDot, 0.0);
    }

    if (debugMode == 0 && u.atmosphere0.x > 0.0) {
        vec3 cameraToGlass = worldPos - u.camPos.xyz;
        float cameraToGlassDistance = length(cameraToGlass);
        float airOpticalDepth = sceneAtmosphereFiniteAirOpticalDepthWithWater(
            u.atmosphere0.x, u.atmosphere0.y, u.atmosphere0.z,
            u.camPos.xyz, cameraToGlass, cameraToGlassDistance);
        finalColor = attenuateSceneAtmosphereSurfaceContribution(
            u.atmosphere0.x, airOpticalDepth, sceneColor, finalColor);
    }

    oColor = vec4(finalColor, 1.0);
}
