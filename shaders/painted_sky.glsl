#ifndef DDA_VOXEL_PAINTED_SKY_GLSL
#define DDA_VOXEL_PAINTED_SKY_GLSL

// mirrors engine/render/PaintedSky.cpp. atmosphere is composed by the owning
// lighting stage after this texture-free base sky is evaluated.
vec3 evaluatePaintedSky(
    vec3 authoredSkyColor,
    vec3 viewDirection,
    vec3 directionToSun,
    vec3 sunColor,
    vec4 paintedSky0,
    vec4 paintedSky1,
    vec4 paintedSky2,
    vec4 paintedSky3,
    vec4 paintedSky4)
{
    vec3 base = max(authoredSkyColor, vec3(0.0));
    float strength = clamp(paintedSky0.w, 0.0, 1.0);
    if (strength <= 0.0)
    {
        return authoredSkyColor;
    }

    vec3 view = normalize(viewDirection);
    vec3 sun = normalize(directionToSun);
    float gradientExponent = max(paintedSky1.w, 0.10);
    float upper = pow(max(view.y, 0.0), gradientExponent);
    float lower = pow(max(-view.y, 0.0), gradientExponent);
    vec3 horizon = base * max(paintedSky0.xyz, vec3(0.0));
    vec3 upperColor = mix(horizon, base * max(paintedSky1.xyz, vec3(0.0)), upper);
    vec3 lowerColor = mix(horizon, base * max(paintedSky2.xyz, vec3(0.0)), lower);
    vec3 painted = view.y >= 0.0 ? upperColor : lowerColor;

    float horizonBand = pow(max(1.0 - abs(view.y), 0.0), max(paintedSky4.y, 0.10));
    painted += base * max(paintedSky0.xyz - vec3(1.0), vec3(0.0)) *
               horizonBand * max(paintedSky2.w, 0.0);

    float sunDot = clamp(dot(view, sun), -1.0, 1.0);
    float radius = max(paintedSky3.x, 0.001);
    float softness = clamp(paintedSky3.y, 0.0, radius * 0.95);
    float discInner = cos(max(radius - softness, 0.0));
    float discOuter = cos(radius + softness);
    float disc = smoothstep(discOuter, discInner, sunDot);
    float halo = pow(max(sunDot, 0.0), max(paintedSky4.x, 1.0));
    painted += max(sunColor, vec3(0.0)) *
               (disc * max(paintedSky3.z, 0.0) +
                halo * max(paintedSky3.w, 0.0));
    return mix(base, painted, strength);
}

#endif
