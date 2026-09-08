#ifndef DDA_COMMON_GLSL
#define DDA_COMMON_GLSL

#include "palette.glsl"
#include "../voxel_volume.glsl"

// ray-AABB intersection
// returns vec2(tNear, tFar), tNear > tFar means no intersection
vec2 intersectAABB(vec3 origin, vec3 dir, vec3 aabbMin, vec3 aabbMax)
{
    vec3 invDir = 1.0 / max(abs(dir), vec3(0.0001));
    invDir *= sign(dir);

    vec3 t0 = (aabbMin - origin) * invDir;
    vec3 t1 = (aabbMax - origin) * invDir;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float tNear = max(max(tmin.x, tmin.y), tmin.z);
    float tFar = min(min(tmax.x, tmax.y), tmax.z);
    return vec2(tNear, tFar);
}

// dda traversal for occlusion testing (shadow rays)
// returns true if any solid voxel is hit within maxT
bool traceDDAOcclusion(
    usampler3D voxelTex,
    vec3 localOrigin,
    vec3 localDir,
    VolumeGpu vol,
    float maxT,
    int maxSteps
)
{
    // setup dda
    vec3 invDir = 1.0 / max(abs(localDir), vec3(0.0001));
    ivec3 step = ivec3(sign(localDir));
    ivec3 voxelPos = ivec3(floor(localOrigin));

    // volume properties
    ivec3 dims = ivec3(vol.dims_palette_flags.xyz);
    uint paletteId = vol.dims_palette_flags.w;
    uint flags = vol.misc.x;
    bool wrapXZ = (flags & FLAG_WRAP_XZ) != 0u;

    // clamp starting position to volume bounds.
    voxelPos = clamp(voxelPos, ivec3(0), dims - 1);

    // skip the originating "shell" of solid voxels at the start of the ray so
    // shadows do not self-occlude on multi-voxel-thick walls (e.g. the 2-voxel
    // tank frame). any solid cell hit before crossing an empty cell is treated
    // as part of the surface we are shading; once we've seen an empty cell, all
    // subsequent solid hits are real occluders.
    bool seenEmpty = false;

    // calculate tMax and tDelta
    vec3 tDelta = abs(1.0 / localDir);
    vec3 voxelBoundary = vec3(voxelPos) + max(vec3(step), vec3(0.0));
    vec3 tMax = (voxelBoundary - localOrigin) / localDir;

    // ensure tMax values are positive
    if (localDir.x == 0.0) tMax.x = 1e20;
    if (localDir.y == 0.0) tMax.y = 1e20;
    if (localDir.z == 0.0) tMax.z = 1e20;

    float t = 0.0;

    for (int i = 0; i < maxSteps && t < maxT; ++i)
    {
        // bounds check (skip for wrapped axes)
        if (!wrapXZ)
        {
            if (any(lessThan(voxelPos, ivec3(0))) || any(greaterThanEqual(voxelPos, dims)))
            {
                return false;  // exited volume without hit
            }
        }
        else
        {
            // for wrapped volumes, only check y bounds
            if (voxelPos.y < 0 || voxelPos.y >= dims.y)
            {
                return false;  // exited volume vertically
            }
        }

        // sample voxel (with wrapping if enabled)
        ivec3 samplePos = wrapVoxelCoord(voxelPos, vol);
        uint voxel = texelFetch(voxelTex, samplePos, 0).r;

        bool isOpaque = false;
        if (voxel != 0u)
        {
            PaletteEntry pe = GetPaletteEntry(paletteId, voxel);
            uint shadingModel = uint(pe.pbr0.w + 0.5);
            isOpaque = (shadingModel != SHADING_GLASS && shadingModel != SHADING_WATER);
        }

        if (isOpaque)
        {
            if (seenEmpty)
            {
                return true;  // genuine occluder past the originating surface
            }
            // still inside the originating shell — skip
        }
        else
        {
            seenEmpty = true;  // entered empty / glass / water — future hits are real
        }

        // step to next voxel
        if (tMax.x < tMax.y && tMax.x < tMax.z)
        {
            t = tMax.x;
            tMax.x += tDelta.x;
            voxelPos.x += step.x;
        }
        else if (tMax.y < tMax.z)
        {
            t = tMax.y;
            tMax.y += tDelta.y;
            voxelPos.y += step.y;
        }
        else
        {
            t = tMax.z;
            tMax.z += tDelta.z;
            voxelPos.z += step.z;
        }
    }

    return false;  // no hit within maxT or maxSteps
}

#endif // DDA_COMMON_GLSL
