#ifndef WATER_VOLUME_SHAPE_GLSL
#define WATER_VOLUME_SHAPE_GLSL

#define WATER_VOLUME_SHAPE_BOX 0
#define WATER_VOLUME_SHAPE_FISHBOWL 1

vec2 waterVolumeNoHit()
{
    return vec2(1.0e20, -1.0e20);
}

bool waterVolumeHasHit(vec2 hit)
{
    return hit.x < hit.y && hit.y > 0.0;
}

vec3 waterVolumeCappedMax(vec3 boundsMax, float surfaceHeight)
{
    return vec3(boundsMax.x, min(boundsMax.y, surfaceHeight), boundsMax.z);
}

bool waterVolumeHasUsableBounds(vec3 boundsMin, vec3 boundsMax, float surfaceHeight)
{
    vec3 cappedMax = waterVolumeCappedMax(boundsMax, surfaceHeight);
    return all(greaterThan(cappedMax, boundsMin));
}

vec2 waterVolumeIntersectAabb(vec3 rayOrigin, vec3 rayDir, vec3 boundsMin, vec3 boundsMax)
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

vec2 waterVolumeIntersectYSlab(float originY, float dirY, float minY, float maxY)
{
    if (abs(dirY) < 1e-7)
    {
        return (originY >= minY && originY <= maxY) ? vec2(-1.0e20, 1.0e20)
                                                   : waterVolumeNoHit();
    }

    float t0 = (minY - originY) / dirY;
    float t1 = (maxY - originY) / dirY;
    return vec2(min(t0, t1), max(t0, t1));
}

vec2 waterVolumeIntersectEllipsoid(vec3 rayOrigin, vec3 rayDir, vec3 center, vec3 radii)
{
    vec3 safeRadii = max(radii, vec3(0.001));
    vec3 o = (rayOrigin - center) / safeRadii;
    vec3 d = rayDir / safeRadii;

    float a = dot(d, d);
    float b = 2.0 * dot(o, d);
    float c = dot(o, o) - 1.0;
    float disc = b * b - 4.0 * a * c;
    if (a <= 1e-10 || disc < 0.0)
    {
        return waterVolumeNoHit();
    }

    float root = sqrt(disc);
    float invDenom = 0.5 / a;
    return vec2((-b - root) * invDenom, (-b + root) * invDenom);
}

void waterVolumeFishbowlEllipsoid(vec3 boundsMin, vec3 boundsMax, float surfaceHeight,
                                  out vec3 center, out vec3 radii)
{
    vec3 cappedMax = waterVolumeCappedMax(boundsMax, surfaceHeight);
    vec3 halfExtent = max((cappedMax - boundsMin) * 0.5, vec3(0.001));
    float topY = cappedMax.y;
    float depth = max(topY - boundsMin.y, 0.001);

    center = vec3((boundsMin.x + cappedMax.x) * 0.5, topY,
                  (boundsMin.z + cappedMax.z) * 0.5);
    radii = vec3(halfExtent.x, depth * 1.36, halfExtent.z);
}

vec2 waterVolumeIntersectShape(int shape, vec3 rayOrigin, vec3 rayDir,
                               vec3 boundsMin, vec3 boundsMax, float surfaceHeight)
{
    vec3 cappedMax = waterVolumeCappedMax(boundsMax, surfaceHeight);
    if (any(lessThanEqual(cappedMax, boundsMin)))
    {
        return waterVolumeNoHit();
    }

    if (shape == WATER_VOLUME_SHAPE_FISHBOWL)
    {
        vec3 center;
        vec3 radii;
        waterVolumeFishbowlEllipsoid(boundsMin, boundsMax, surfaceHeight, center, radii);
        vec2 ellipsoidHit = waterVolumeIntersectEllipsoid(rayOrigin, rayDir, center, radii);
        vec2 slabHit = waterVolumeIntersectYSlab(rayOrigin.y, rayDir.y, boundsMin.y, cappedMax.y);
        return vec2(max(ellipsoidHit.x, slabHit.x), min(ellipsoidHit.y, slabHit.y));
    }

    return waterVolumeIntersectAabb(rayOrigin, rayDir, boundsMin, cappedMax);
}

bool waterVolumeContainsPoint(int shape, vec3 point, vec3 boundsMin, vec3 boundsMax,
                              float surfaceHeight)
{
    vec3 cappedMax = waterVolumeCappedMax(boundsMax, surfaceHeight);
    if (any(lessThan(point, boundsMin)) || any(greaterThan(point, cappedMax)))
    {
        return false;
    }

    if (shape == WATER_VOLUME_SHAPE_FISHBOWL)
    {
        vec3 center;
        vec3 radii;
        waterVolumeFishbowlEllipsoid(boundsMin, boundsMax, surfaceHeight, center, radii);
        vec3 q = (point - center) / max(radii, vec3(0.001));
        return dot(q, q) <= 1.0005;
    }

    return true;
}

bool waterVolumeContainsSurfacePoint(int shape, vec3 point, vec3 boundsMin, vec3 boundsMax,
                                     float surfaceHeight)
{
    vec3 cappedMax = waterVolumeCappedMax(boundsMax, surfaceHeight);
    vec3 surfacePoint = vec3(point.x, cappedMax.y, point.z);
    return waterVolumeContainsPoint(shape, surfacePoint, boundsMin, boundsMax, surfaceHeight);
}

float waterVolumeSurfaceEdgeDistance(int shape, vec3 point, vec3 boundsMin, vec3 boundsMax,
                                     float surfaceHeight)
{
    vec3 cappedMax = waterVolumeCappedMax(boundsMax, surfaceHeight);
    if (shape == WATER_VOLUME_SHAPE_FISHBOWL)
    {
        vec3 center;
        vec3 radii;
        waterVolumeFishbowlEllipsoid(boundsMin, boundsMax, surfaceHeight, center, radii);
        vec2 q = (point.xz - center.xz) / max(radii.xz, vec2(0.001));
        float radialNorm = length(q);
        return max(0.0, (1.0 - radialNorm) * min(radii.x, radii.z));
    }

    float dx = min(point.x - boundsMin.x, cappedMax.x - point.x);
    float dz = min(point.z - boundsMin.z, cappedMax.z - point.z);
    return max(0.0, min(dx, dz));
}

float waterVolumeLateralBoundaryDistance(int shape, vec3 point, vec3 boundsMin, vec3 boundsMax,
                                         float surfaceHeight)
{
    vec3 cappedMax = waterVolumeCappedMax(boundsMax, surfaceHeight);
    if (shape == WATER_VOLUME_SHAPE_FISHBOWL)
    {
        vec3 center;
        vec3 radii;
        waterVolumeFishbowlEllipsoid(boundsMin, boundsMax, surfaceHeight, center, radii);
        vec3 safeRadii = max(radii, vec3(0.001));
        float qY = clamp((point.y - center.y) / safeRadii.y, -1.0, 1.0);
        float lateralLimit = sqrt(max(1.0 - qY * qY, 0.0));
        float radialNorm = length((point.xz - center.xz) / max(safeRadii.xz, vec2(0.001)));
        return (lateralLimit - radialNorm) * min(safeRadii.x, safeRadii.z);
    }

    float dx = min(point.x - boundsMin.x, cappedMax.x - point.x);
    float dz = min(point.z - boundsMin.z, cappedMax.z - point.z);
    return min(dx, dz);
}

#endif // WATER_VOLUME_SHAPE_GLSL
