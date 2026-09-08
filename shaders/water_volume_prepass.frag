#version 450
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec2 oWaterMeta;

layout(set = 0, binding = 0) uniform UBO {
    mat4 invViewProj;
    vec4 camPos;
    vec4 params; // x = container count
} u;

layout(set = 0, binding = 1) uniform sampler2D uDepth;

struct WaterContainerGpu
{
    vec4 boundsMin_fillHeight; // xyz = min, w = fill height
    vec4 boundsMax_fogDensity; // xyz = max, w = fog density
    vec4 absorption_shape;     // xyz = absorption, w = shape enum
    vec4 deepColor_flags;      // xyz = deep color, w = flags
};

layout(std430, set = 0, binding = 2) readonly buffer WaterContainerBuf {
    WaterContainerGpu waterContainers[];
};

#include "water_volume_shape.glsl"

const int kMaxWaterContainers = 8;

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

void main()
{
    vec2 uv = clamp(vUV, vec2(0.0), vec2(1.0));
    float depth01 = texture(uDepth, uv).r;
    if (depth01 >= 0.9999)
    {
        oWaterMeta = vec2(0.0);
        return;
    }

    vec3 cameraPos = u.camPos.xyz;
    vec3 hitPos = reconstructWorldPos(uv, depth01);
    vec3 toHit = hitPos - cameraPos;
    float hitDist = length(toHit);
    if (hitDist <= 1e-5)
    {
        oWaterMeta = vec2(0.0);
        return;
    }

    vec3 rayDir = toHit / hitDist;
    int containerCount = clamp(int(u.params.x + 0.5), 0, kMaxWaterContainers);
    float waterDist = 0.0;

    for (int i = 0; i < containerCount; ++i)
    {
        WaterContainerGpu container = waterContainers[i];
        int shape = int(container.absorption_shape.w + 0.5);

        vec3 boundsMin = container.boundsMin_fillHeight.xyz;
        vec3 boundsMax = container.boundsMax_fogDensity.xyz;
        float surfaceHeight = container.boundsMin_fillHeight.w;

        vec2 hit = vec2(0.0);
        if (shape == WATER_VOLUME_SHAPE_FISHBOWL)
        {
            if (!waterVolumeHasUsableBounds(boundsMin, boundsMax, surfaceHeight))
            {
                continue;
            }
            hit = waterVolumeIntersectShape(shape, cameraPos, rayDir, boundsMin, boundsMax,
                                            surfaceHeight);
        }
        else
        {
            boundsMax.y = min(boundsMax.y, surfaceHeight);
            if (any(lessThanEqual(boundsMax, boundsMin)))
            {
                continue;
            }
            hit = intersectAabb(cameraPos, rayDir, boundsMin, boundsMax);
        }
        float segStart = max(hit.x, 0.0);
        float segEnd = min(hit.y, hitDist);
        waterDist += max(segEnd - segStart, 0.0);
    }

    oWaterMeta = vec2(waterDist, 0.0);
}
