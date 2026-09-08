#ifndef TERRAIN_SHADOW_COLUMNS_GLSL
#define TERRAIN_SHADOW_COLUMNS_GLSL

// binary height-field occlusion for classifier-approved terrain. each X/Z
// cell contains the exclusive top of the solid interval [0, topYExclusive).
// keep the cell/event ordering identical to traceDDAOcclusion; the speedup
// comes from a compact 2.5D load and integer height comparison instead of a
// 3D texture fetch plus palette/material classification.
bool traceTerrainShadowColumnOcclusion(
    vec3 localOrigin,
    vec3 localDir,
    ivec3 dims,
    float maxT,
    int maxSteps)
{
    ivec3 stepDirection = ivec3(sign(localDir));
    ivec3 voxelPos = ivec3(floor(localOrigin));
    voxelPos = clamp(voxelPos, ivec3(0), dims - 1);

    vec3 tDelta = abs(1.0 / localDir);
    vec3 voxelBoundary =
        vec3(voxelPos) + max(vec3(stepDirection), vec3(0.0));
    vec3 tMax = (voxelBoundary - localOrigin) / localDir;
    if (localDir.x == 0.0) tMax.x = 1e20;
    if (localDir.y == 0.0) tMax.y = 1e20;
    if (localDir.z == 0.0) tMax.z = 1e20;

    bool seenEmpty = false;
    float t = 0.0;

    for (int i = 0; i < maxSteps && t < maxT; ++i)
    {
        if (any(lessThan(voxelPos, ivec3(0))) ||
            any(greaterThanEqual(voxelPos, dims)))
        {
            return false;
        }

        uint topYExclusive = terrainShadowColumns.topYExclusive[
            uint(voxelPos.x + dims.x * voxelPos.z)];
        bool isOpaque = uint(voxelPos.y) < topYExclusive;
        if (isOpaque)
        {
            if (seenEmpty)
            {
                return true;
            }
        }
        else
        {
            seenEmpty = true;
        }

        if (tMax.x < tMax.y && tMax.x < tMax.z)
        {
            t = tMax.x;
            tMax.x += tDelta.x;
            voxelPos.x += stepDirection.x;
        }
        else if (tMax.y < tMax.z)
        {
            t = tMax.y;
            tMax.y += tDelta.y;
            voxelPos.y += stepDirection.y;
        }
        else
        {
            t = tMax.z;
            tMax.z += tDelta.z;
            voxelPos.z += stepDirection.z;
        }
    }

    return false;
}

#endif // TERRAIN_SHADOW_COLUMNS_GLSL
