#ifndef ATMOSPHERE_WATER_PATH_GLSL
#define ATMOSPHERE_WATER_PATH_GLSL

// the includer supplies:
//   SCENE_ATMOSPHERE_WATER_VOLUME_COUNT
//   SCENE_ATMOSPHERE_WATER_VOLUME(index)
// and declares WaterVolumeGPU before this file is included.
#include "water_volume_shape.glsl"
#include "atmosphere_common.glsl"

const int kSceneAtmosphereMaxWaterIntervals = 8;

int collectSceneAtmosphereWaterIntervals(
    vec3 rayOrigin, vec3 rayDirection, float maxDistance,
    out vec2 intervals[kSceneAtmosphereMaxWaterIntervals])
{
    int count = 0;
    int volumeCount = clamp(SCENE_ATMOSPHERE_WATER_VOLUME_COUNT, 0,
                            kSceneAtmosphereMaxWaterIntervals);
    for (int i = 0; i < volumeCount; ++i)
    {
        WaterVolumeGPU volume = SCENE_ATMOSPHERE_WATER_VOLUME(i);
        vec3 boundsMin = volume.boundsMin.xyz;
        vec3 boundsMax = volume.boundsMax.xyz;
        float surfaceHeight = volume.boundsMin.w;
        int shape = int(volume.absorptionCoeff.w + 0.5);
        vec2 hit = waterVolumeIntersectShape(shape, rayOrigin, rayDirection,
                                             boundsMin, boundsMax, surfaceHeight);
        float startDistance = clamp(hit.x, 0.0, maxDistance);
        float endDistance = clamp(hit.y, 0.0, maxDistance);
        if (waterVolumeHasHit(hit) && endDistance > startDistance)
        {
            intervals[count++] = vec2(startDistance, endDistance);
        }
    }

    // upload order is scene-owned, not ray-owned. sort before merging so every
    // overlapping participating-medium interval is excluded exactly once.
    for (int i = 1; i < count; ++i)
    {
        vec2 key = intervals[i];
        int j = i - 1;
        while (j >= 0 && intervals[j].x > key.x)
        {
            intervals[j + 1] = intervals[j];
            --j;
        }
        intervals[j + 1] = key;
    }
    return count;
}

float sceneAtmosphereFiniteAirOpticalDepthWithWater(
    float density, float heightFalloff, float baseHeight, vec3 rayOrigin,
    vec3 rayDirection, float lengthMeters)
{
    if (density <= 0.0 || lengthMeters <= 0.0)
    {
        return 0.0;
    }

    vec3 direction = normalize(rayDirection);
    vec2 intervals[kSceneAtmosphereMaxWaterIntervals];
    int count = collectSceneAtmosphereWaterIntervals(
        rayOrigin, direction, lengthMeters, intervals);
    float opticalDepth = 0.0;
    float airStart = 0.0;
    for (int i = 0; i < count; ++i)
    {
        if (intervals[i].x > airStart)
        {
            float segmentLength = intervals[i].x - airStart;
            float segmentOriginHeight = rayOrigin.y + direction.y * airStart;
            opticalDepth += sceneAtmosphereOpticalDepth(
                density, heightFalloff, baseHeight, segmentOriginHeight,
                direction.y, segmentLength);
        }
        airStart = max(airStart, intervals[i].y);
    }
    if (airStart < lengthMeters)
    {
        float segmentOriginHeight = rayOrigin.y + direction.y * airStart;
        opticalDepth += sceneAtmosphereOpticalDepth(
            density, heightFalloff, baseHeight, segmentOriginHeight,
            direction.y, lengthMeters - airStart);
    }
    return opticalDepth;
}

float sceneAtmosphereSkyAirOpticalDepthWithWater(
    float density, float heightFalloff, float baseHeight, vec3 rayOrigin,
    vec3 rayDirection)
{
    if (density <= 0.0)
    {
        return 0.0;
    }

    vec3 direction = normalize(rayDirection);
    vec2 intervals[kSceneAtmosphereMaxWaterIntervals];
    int count = collectSceneAtmosphereWaterIntervals(
        rayOrigin, direction, 1.0e19, intervals);
    float opticalDepth = 0.0;
    float airStart = 0.0;
    for (int i = 0; i < count; ++i)
    {
        if (intervals[i].x > airStart)
        {
            float segmentLength = intervals[i].x - airStart;
            float segmentOriginHeight = rayOrigin.y + direction.y * airStart;
            opticalDepth += sceneAtmosphereOpticalDepth(
                density, heightFalloff, baseHeight, segmentOriginHeight,
                direction.y, segmentLength);
        }
        airStart = max(airStart, intervals[i].y);
    }

    float tailOriginHeight = rayOrigin.y + direction.y * airStart;
    return opticalDepth + sceneAtmosphereSkyOpticalDepth(
        density, heightFalloff, baseHeight, tailOriginHeight, direction.y);
}

#endif
