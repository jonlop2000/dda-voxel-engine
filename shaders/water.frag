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
    vec4 v2Optics0;
    vec4 v2Optics1;
    vec4 v2Body0;
    vec4 v2Body1;
    vec4 v2Body2;
    vec4 v2Reflection0;
    vec4 v2Reflection1;
    vec4 v2Planar0;
    vec4 v2Planar1;
    vec4 v2Planar2;
    vec4 v2Contact0;
    vec4 v2Contact1;
    vec4 v2Contact2;
    vec4 v2Opacity0;
    vec4 v2Opacity1;
    vec4 v2Vfx0;
    vec4 v2Vfx1;
    vec4 v2Vfx2;
    vec4 v2BodyDetail0;
    vec4 atmosphere0; // x=density, y=heightFalloff, z=baseHeight, w=sunPhaseStrength
    vec4 v2BodyShaft0; // x=strength, y=contrast, z=apertureHalfExtent, w=warmth
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

float stylizedFresnel(float NdotV, float bias)
{
    float rim = 1.0 - NdotV;
    return clamp(bias + (1.0 - bias) * rim * rim * rim, 0.0, 1.0);
}

float toonSpecular(vec3 normal, vec3 viewDir, vec3 lightDir, float power, float intensity)
{
    vec3 halfVec = normalize(viewDir + lightDir);
    float NdotH = max(dot(normal, halfVec), 0.0);
    float NdotL = max(dot(normal, lightDir), 0.0);

    float rawSpec = pow(NdotH, max(power, 1.0));
    float threshold = mix(0.45, 0.84, clamp((power - 64.0) / 576.0, 0.0, 1.0));
    float spec = smoothstep(threshold - 0.10, threshold + 0.10, rawSpec);
    return spec * NdotL * intensity * 0.65;
}

vec3 stylizedDepthColor(vec3 sceneColor, float depth, float hardness)
{
    vec3 shoreColor = vec3(0.52, 0.86, 0.80);
    vec3 shallowColor = vec3(0.26, 0.62, 0.68);
    vec3 midColor = vec3(0.13, 0.39, 0.54);
    vec3 deepColor = vec3(0.06, 0.20, 0.36);
    vec3 abyssColor = vec3(0.02, 0.09, 0.18);

    float edge = mix(0.35, 0.03, hardness);
    float t1 = smoothstep(0.5 - edge, 0.5 + edge, depth);
    float t2 = smoothstep(2.0 - edge, 2.0 + edge, depth);
    float t3 = smoothstep(5.0 - edge, 5.0 + edge, depth);
    float t4 = smoothstep(10.0 - edge, 10.0 + edge, depth);

    vec3 waterColor = shoreColor;
    waterColor = mix(waterColor, shallowColor, t1);
    waterColor = mix(waterColor, midColor, t2);
    waterColor = mix(waterColor, deepColor, t3);
    waterColor = mix(waterColor, abyssColor, t4);

    float sceneVisibility = 1.0 - smoothstep(0.0, 4.0, depth);
    return mix(waterColor, sceneColor * waterColor * 1.6, sceneVisibility);
}

vec3 sampleSkyReflection(vec3 reflectDir)
{
    float y = reflectDir.y;
    vec3 horizonColor = vec3(0.35, 0.45, 0.65);
    vec3 zenithColor = vec3(0.15, 0.30, 0.60);
    vec3 nadirColor = vec3(0.1, 0.15, 0.25);

    vec3 sky;
    if (y > 0.0)
    {
        sky = mix(horizonColor, zenithColor, pow(y, 0.5));
    }
    else
    {
        sky = mix(horizonColor, nadirColor, pow(-y, 0.8));
    }

    vec3 sunDir = normalize(u.sunDirToSun.xyz);
    float sunDot = max(dot(reflectDir, sunDir), 0.0);
    float sunDisc = pow(sunDot, 256.0) * 8.0;
    float sunGlow = pow(sunDot, 16.0) * 0.5;
    sky += u.sunColor.rgb * (sunDisc + sunGlow);

    return sky;
}

float toonCrestMask(float waveH, float waveAmp, vec3 waveNormal, float threshold, float softness,
                    float noiseJitter)
{
    float amp = max(waveAmp, 1e-4);
    float crestCoord = clamp(waveH / amp * 0.5 + 0.5, 0.0, 1.0);
    float center = clamp(threshold + noiseJitter, 0.0, 1.0);
    float crestBand = smoothstep(center - softness, center + softness, crestCoord);

    // keep crest strokes on actual ripples rather than perfectly flat patches.
    float slope = length(waveNormal.xz);
    float slopeMask = smoothstep(0.015, 0.14, slope);
    return crestBand * slopeMask;
}

void main()
{
    // discard fragments outside water volume bounds (xyz)
    if (u.params4.y > 0.5) {
        vec3 bMin = waterVolumes[0].boundsMin.xyz;
        vec3 bMax = waterVolumes[0].boundsMax.xyz;
        float surfaceHeight = waterVolumes[0].boundsMin.w;  // water level
        int shape = int(waterVolumes[0].absorptionCoeff.w + 0.5);

        bool outsideVolume =
            shape == WATER_VOLUME_SHAPE_FISHBOWL
                ? (!waterVolumeContainsSurfacePoint(shape, vWorldPos, bMin, bMax,
                                                    surfaceHeight) ||
                   vWorldPos.y < bMin.y || vWorldPos.y > surfaceHeight)
                : (vWorldPos.x < bMin.x || vWorldPos.x > bMax.x ||
                   vWorldPos.z < bMin.z || vWorldPos.z > bMax.z ||
                   vWorldPos.y < bMin.y || vWorldPos.y > surfaceHeight);
        if (outsideVolume) {
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

    int waterVolumeCount = int(u.params4.y + 0.5);
    int styleMode = int(u.params4.z + 0.5);
    float surfaceHeight = u.params0.x;
    if (waterVolumeCount > 0)
    {
        surfaceHeight = waterVolumes[0].boundsMin.w;
    }

    float time = u.params0.y;
    float waveScale = max(u.params0.z, 0.001);
    float waveAmp = max(u.params0.w, 0.0);
    float underwaterView = float(u.camPos.y < surfaceHeight - 0.05);
    vec3 waveNormal = computeWaveNormal(vWorldPos.xz, time, waveScale, waveAmp);
    vec3 detailWaveNormal = computeDetailWaveNormal(vWorldPos.xz, time, waveScale, waveAmp);
    waveNormal = normalize(mix(vec3(0.0, 1.0, 0.0), waveNormal, clamp(waveAmp, 0.0, 1.0)));
    waveNormal = normalize(mix(waveNormal, detailWaveNormal, mix(0.25, 0.68, underwaterView)));

    vec3 viewDir = normalize(u.camPos.xyz - vWorldPos);
    float NdotV = max(dot(waveNormal, viewDir), 0.001);
    vec2 refractionOffset = waveNormal.xz * u.params1.x;
    refractionOffset *= clamp(waterPath * max(u.params2.w, 0.0), 0.0, 1.0);
    refractionOffset *= mix(1.0, 1.85, underwaterView);

    vec2 refractedUV = clamp(uv + refractionOffset, vec2(0.002), vec2(0.998));
    float distortedEye = eyeDepth(refractedUV, texture(uSceneDepth, refractedUV).r);
    if (distortedEye + 1e-3 < waterEye)
    {
        refractedUV = uv;
    }

    vec3 sceneColor = texture(uRefractionSource, refractedUV).rgb;
    vec3 waterColor = sceneColor;
    vec3 atmosphereBaseColor = sceneColor;
    float fresnel = 0.0;
    float reflectionMixDebug = 0.0;
    vec3 planarDebug = vec3(0.0);
    vec3 planarProjectedDebug = vec3(0.0);
    vec2 planarProjectedUvDebug = vec2(0.0);
    vec3 planarNoDistortDebug = vec3(0.0);

    if (styleMode == 0)
    {
        vec3 refractedColor = sceneColor;

        if (waterVolumeCount > 0 && waterPath > 0.0)
        {
            vec3 absorption = waterVolumes[0].absorptionCoeff.xyz;
            vec3 deepColor = waterVolumes[0].deepColor.xyz;
            float fogDensity = max(waterVolumes[0].boundsMax.w, 0.0);

            vec3 transmission = exp(-absorption * waterPath);
            refractedColor *= transmission;

            float fogFactor = 1.0 - exp(-fogDensity * waterPath);
            refractedColor = mix(refractedColor, deepColor, clamp(fogFactor, 0.0, 1.0));
        }
        else if (waterPath > 0.0)
        {
            float trans = exp(-0.06 * waterPath);
            refractedColor = mix(u.waterTint.rgb, refractedColor, trans);
        }

        if (underwaterView > 0.5)
        {
            vec2 undersideUV = vWorldPos.xz * (waveScale * 0.80 + 0.08);
            float ripple0 = texture(uNoiseTex, undersideUV + time * vec2(0.021, -0.018)).r;
            float ripple1 = texture(uNoiseTex, undersideUV * 1.71 - time * vec2(0.015, 0.026)).r;
            float undersideShimmer = mix(ripple0, ripple1, 0.5);
            refractedColor *= mix(0.92, 1.10, undersideShimmer);
            refractedColor += u.sunColor.rgb * undersideShimmer * 0.05;
        }

        atmosphereBaseColor = refractedColor;

        fresnel = schlickFresnel(NdotV, u.params1.w);
        vec3 reflectDir = reflect(-viewDir, waveNormal);
        vec3 reflectionColor = sampleSkyReflection(reflectDir);
        float reflectionMix =
            clamp(pow(fresnel, 1.25) * mix(0.82, 0.42, underwaterView), 0.0,
                  mix(0.90, 0.58, underwaterView));
        waterColor = mix(refractedColor, reflectionColor, reflectionMix);
        reflectionMixDebug = reflectionMix;

        vec3 sunDir = normalize(u.sunDirToSun.xyz);
        vec3 halfVec = normalize(viewDir + sunDir);
        float NdotH = max(dot(waveNormal, halfVec), 0.0);
        float NdotL = max(dot(waveNormal, sunDir), 0.0);

        float specPower = max(u.params1.y * 0.55, 1.0);
        float specTight = pow(NdotH, specPower) * u.params1.z * 0.45;
        float specBroad = pow(NdotH, 18.0) * 0.035;
        waterColor += u.sunColor.rgb * (specTight + specBroad) * NdotL;
    }
    else
    {
        float bandHardness = clamp(u.params3.y, 0.0, 1.0);
        vec3 baseStylized = stylizedDepthColor(sceneColor, waterPath, bandHardness);
        float gradientStrength = clamp(u.params3.w, 0.0, 1.0);
        vec2 gradUV = vWorldPos.xz * (0.06 + u.params0.z * 0.35);
        float g0 = texture(uNoiseTex, gradUV + time * vec2(0.015, -0.011)).r;
        float g1 = texture(uNoiseTex, gradUV * 0.57 - time * vec2(0.009, 0.013)).r;
        float gradNoise = mix(g0, g1, 0.45);
        float angleGrad = 1.0 - NdotV;
        float gradMix = clamp(0.35 + angleGrad * 0.70 + (gradNoise - 0.5) * 0.35, 0.0, 1.0);
        vec3 gradTop = vec3(0.94, 1.03, 1.08);
        vec3 gradBottom = vec3(0.72, 0.90, 1.02);
        vec3 gradTint = mix(gradTop, gradBottom, gradMix);
        baseStylized = mix(baseStylized, baseStylized * gradTint, gradientStrength);
        atmosphereBaseColor = baseStylized;

        float fresnelBias = clamp(u.params1.w, 0.0, 0.5);
        fresnel = stylizedFresnel(NdotV, fresnelBias);

        float reflectionStrength = max(u.params3.z, 0.0);
        vec3 reflectionTint = vec3(0.50, 0.65, 0.85);

        vec3 sunDir = normalize(u.sunDirToSun.xyz);
        vec3 reflectDir = reflect(-viewDir, waveNormal);
        float sunFacing = max(dot(reflectDir, sunDir), 0.0);
        vec3 reflectionColor = reflectionTint + u.sunColor.rgb * pow(sunFacing, 8.0) * 0.4;
        vec3 fallbackReflection = reflectionColor;
        float planarStrength = clamp(u.params4.w, 0.0, 1.0);

        vec4 planarWorldPos = vec4(vWorldPos.x, u.params0.x, vWorldPos.z, 1.0);
        vec4 projectedClipR = u.reflectedViewProj * planarWorldPos;
        bool projectedClipValid = projectedClipR.w > 1e-5;
        vec2 projectedUvBase = projectedClipR.xy / max(projectedClipR.w, 1e-5) * 0.5 + 0.5;
        planarProjectedUvDebug = projectedUvBase;
        vec2 projectedUvClamped = clamp(projectedUvBase, vec2(0.001), vec2(0.999));
        planarNoDistortDebug = texture(uPlanarReflection, projectedUvClamped).rgb;
        vec2 planarUv = projectedUvBase + waveNormal.xz * 0.015;
        vec2 uvClamped = clamp(planarUv, vec2(0.001), vec2(0.999));
        const float planarGuardBand = 0.12;
        float inRange = float(all(greaterThanEqual(projectedUvBase, vec2(-planarGuardBand))) &&
                              all(lessThanEqual(projectedUvBase, vec2(1.0 + planarGuardBand))));
        float outside = max(max(-projectedUvBase.x, projectedUvBase.x - 1.0),
                            max(-projectedUvBase.y, projectedUvBase.y - 1.0));
        float guardFade = 1.0 - smoothstep(0.0, planarGuardBand, max(outside, 0.0));
        float edgeDist = min(min(uvClamped.x, uvClamped.y),
                             min(1.0 - uvClamped.x, 1.0 - uvClamped.y));
        float borderFade = smoothstep(0.0, 0.08, edgeDist);
        float planarMask = (projectedClipValid ? 1.0 : 0.0) * inRange * guardFade * borderFade;
        vec3 planarSample = texture(uPlanarReflection, uvClamped).rgb;
        planarProjectedDebug = planarSample;
        vec3 planarStyled = mix(planarSample, reflectionTint, 0.35);
        vec3 planarReflection = mix(fallbackReflection, planarStyled, planarMask);
        reflectionColor = mix(fallbackReflection, planarReflection, planarStrength);
        planarDebug = planarReflection;

        float reflectionMix = clamp(pow(fresnel, 1.35) * reflectionStrength, 0.0, 0.78);
        reflectionMixDebug = reflectionMix;
        waterColor = mix(baseStylized, reflectionColor, reflectionMix);

        float specPower = max(u.params1.y * 0.45, 1.0);
        float specIntensity = u.params1.z * 0.55;
        float spec = toonSpecular(waveNormal, viewDir, sunDir, specPower, specIntensity);
        vec3 specColor = u.sunColor.rgb * vec3(0.95, 0.97, 1.0);
        waterColor += specColor * spec * 0.75;

        float crestIntensity = max(u.params2.z, 0.0);
        if (crestIntensity > 0.001 && waveAmp > 0.001)
        {
            float crestThreshold = clamp(u.params2.x, 0.0, 1.0);
            float crestSoftness = clamp(u.params2.y, 0.01, 0.35);
            float waveH = waveHeight(vWorldPos.xz, time, waveScale, waveAmp);
            vec2 crestUV = vWorldPos.xz * (waveScale * 0.45 + 0.02);
            float n0 = texture(uNoiseTex, crestUV + time * vec2(0.012, -0.010)).r;
            float n1 = texture(uNoiseTex, crestUV * 1.83 - time * vec2(0.009, 0.015)).r;
            float n2 = texture(uNoiseTex, crestUV * 4.7 + time * vec2(-0.028, 0.031)).r;
            float jitter = (mix(n0, n1, 0.35) - 0.5) * 0.16;
            waveH += (n2 - 0.5) * waveAmp * 0.14;
            float crestMask =
                toonCrestMask(waveH, waveAmp, waveNormal, crestThreshold, crestSoftness, jitter);
            float breakup = smoothstep(0.30, 0.82, mix(n0, n2, 0.55));
            crestMask *= mix(0.58, 1.0, breakup);
            float crestMix = clamp(crestMask * crestIntensity, 0.0, 1.0);
            vec3 crestColor = vec3(0.94, 0.98, 1.0);
            waterColor = mix(waterColor, crestColor, crestMix);
        }
    }

    if (underwaterView > 0.5 && waterVolumeCount > 0)
    {
        vec3 absorption = waterVolumes[0].absorptionCoeff.xyz;
        vec3 deepColor = waterVolumes[0].deepColor.xyz;
        float fogDensity = max(waterVolumes[0].boundsMax.w, 0.0);
        float surfaceViewPath = max(length(u.camPos.xyz - vWorldPos), 0.0);

        vec3 transmission = exp(-absorption * surfaceViewPath);
        waterColor *= transmission;

        float fogFactor = 1.0 - exp(-fogDensity * surfaceViewPath);
        waterColor = mix(waterColor, deepColor, clamp(fogFactor * 0.72, 0.0, 1.0));

        if (u.v2BodyShaft0.z > 0.0001)
        {
            // sunroof V2.3 gives the underside a restrained painted wash so the
            // water ceiling participates in the same warm/cool palette as the
            // body medium. broad world-space waves avoid adding texture samples.
            float wash0 = 0.5 + 0.5 * sin(dot(vWorldPos.xz, vec2(0.105, 0.071)) +
                                           time * 0.035);
            float wash1 = 0.5 + 0.5 * sin(dot(vWorldPos.xz, vec2(-0.061, 0.129)) -
                                           time * 0.024 + 1.7);
            float paintedWash = mix(wash0, wash1, 0.42);
            vec3 paletteBalance = mix(vec3(0.94, 1.00, 1.04),
                                      vec3(1.08, 1.03, 0.92), paintedWash);
            vec3 undersideLift = mix(vec3(0.055, 0.105, 0.115),
                                     vec3(0.145, 0.155, 0.095), paintedWash);
            float grazing = pow(clamp(1.0 - NdotV, 0.0, 1.0), 0.70);
            float paintedStrength = 0.10 + grazing * 0.08;
            waterColor = mix(waterColor,
                             waterColor * paletteBalance + undersideLift * 0.055,
                             paintedStrength);
        }
    }

    // lighting already atmosphered the refracted scene. attenuate only the
    // reflection/specular/crest delta accumulated at this transparent surface.
    vec3 cameraToSurface = vWorldPos - u.camPos.xyz;
    float atmosphereViewDistance = length(cameraToSurface);
    if (u.atmosphere0.x > 0.0 && atmosphereViewDistance > 0.0)
    {
        float opticalDepth = sceneAtmosphereFiniteAirOpticalDepthWithWater(
            u.atmosphere0.x, u.atmosphere0.y, u.atmosphere0.z,
            u.camPos.xyz, cameraToSurface / atmosphereViewDistance,
            atmosphereViewDistance);
        waterColor = attenuateSceneAtmosphereSurfaceContribution(
            u.atmosphere0.x, opticalDepth, atmosphereBaseColor, waterColor);
    }

    float alpha = 1.0;
    if (hasSceneGeometry)
    {
        alpha = smoothstep(0.0, max(u.params3.x, 0.001), depthToGeometry);
    }

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
        oColor = vec4(vec3(reflectionMixDebug), alpha);
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
        float bandViz = clamp(waterPath / 12.0, 0.0, 1.0);
        oColor = vec4(bandViz, 1.0 - bandViz, 0.3, alpha);
        return;
    }
    if (debugMode == 7)
    {
        oColor = vec4(planarDebug, 1.0);
        return;
    }
    if (debugMode == 13)
    {
        oColor = vec4(planarProjectedDebug, 1.0);
        return;
    }
    if (debugMode == 15)
    {
        oColor = vec4(texture(uPlanarReflection, clamp(uv, vec2(0.001), vec2(0.999))).rgb, 1.0);
        return;
    }
    if (debugMode == 16)
    {
        oColor = vec4(planarProjectedUvDebug, 1.0, 1.0);
        return;
    }
    if (debugMode == 17)
    {
        oColor = vec4(planarNoDistortDebug, 1.0);
        return;
    }
    oColor = vec4(waterColor, alpha);
}
