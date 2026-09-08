#ifndef WATER_COMMON_GLSL
#define WATER_COMMON_GLSL

#define MAX_WATER_VOLUMES 8

#include "water_volume_shape.glsl"

struct WaterVolumeGPU
{
    vec4 boundsMin;       // xyz = min, w = surfaceHeight
    vec4 boundsMax;       // xyz = max, w = fogDensity
    vec4 absorptionCoeff; // xyz = coefficients
    vec4 deepColor;       // xyz = color
};

struct WaterSegment
{
    float tEnter;
    float tExit;
    int volumeIndex;
};

vec3 waterSafeInvDir(vec3 dir)
{
    const float eps = 1e-8;
    vec3 inv;
    inv.x = abs(dir.x) > eps ? 1.0 / dir.x : 1e20;
    inv.y = abs(dir.y) > eps ? 1.0 / dir.y : 1e20;
    inv.z = abs(dir.z) > eps ? 1.0 / dir.z : 1e20;
    return inv;
}

vec2 intersectWaterAABB(vec3 rayOrigin, vec3 rayDirInv, vec3 boxMin, vec3 boxMax)
{
    vec3 t0 = (boxMin - rayOrigin) * rayDirInv;
    vec3 t1 = (boxMax - rayOrigin) * rayDirInv;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);
    float tNear = max(max(tMin.x, tMin.y), tMin.z);
    float tFar = min(min(tMax.x, tMax.y), tMax.z);
    return vec2(tNear, tFar);
}

int computeWaterSegments(vec3 rayOriginWorld, vec3 rayDirWorld,
                         WaterVolumeGPU waterVolumes[MAX_WATER_VOLUMES],
                         uint waterVolumeCount, out WaterSegment segs[MAX_WATER_VOLUMES])
{
    int numSegs = 0;
    vec3 rayDirInv = waterSafeInvDir(rayDirWorld);

    for (uint i = 0u; i < waterVolumeCount && i < uint(MAX_WATER_VOLUMES); ++i)
    {
        vec3 wMin = waterVolumes[i].boundsMin.xyz;
        vec3 wMax = waterVolumes[i].boundsMax.xyz;
        float surfaceY = waterVolumes[i].boundsMin.w;
        int shape = int(waterVolumes[i].absorptionCoeff.w + 0.5);

        vec2 nearFar = vec2(0.0);
        if (shape == WATER_VOLUME_SHAPE_FISHBOWL)
        {
            if (!waterVolumeHasUsableBounds(wMin, wMax, surfaceY))
            {
                continue;
            }
            nearFar =
                waterVolumeIntersectShape(shape, rayOriginWorld, rayDirWorld, wMin, wMax,
                                          surfaceY);
        }
        else
        {
            wMax.y = min(wMax.y, surfaceY);
            if (surfaceY <= wMin.y)
            {
                continue;
            }
            nearFar = intersectWaterAABB(rayOriginWorld, rayDirInv, wMin, wMax);
        }
        if (nearFar.x < nearFar.y && nearFar.y > 0.0)
        {
            segs[numSegs].tEnter = max(nearFar.x, 0.0);
            segs[numSegs].tExit = nearFar.y;
            segs[numSegs].volumeIndex = int(i);
            numSegs++;
        }
    }

    return numSegs;
}

float calcWaterDistance(WaterSegment segs[MAX_WATER_VOLUMES], int numSegs, float tRayStart,
                        float tHit)
{
    float totalDist = 0.0;
    for (int i = 0; i < numSegs; ++i)
    {
        float segStart = max(segs[i].tEnter, tRayStart);
        float segEnd = min(segs[i].tExit, tHit);
        if (segEnd > segStart)
        {
            totalDist += (segEnd - segStart);
        }
    }
    return totalDist;
}

#endif // WATER_COMMON_GLSL
