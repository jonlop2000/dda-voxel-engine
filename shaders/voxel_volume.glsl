#ifndef VOXEL_VOLUME_GLSL
#define VOXEL_VOLUME_GLSL

#ifndef VOXEL_SET
#define VOXEL_SET 2
#endif

struct VolumeGpu
{
    mat4 worldFromLocal;
    mat4 localFromWorld;
    mat4 normalFromLocal;
    vec4 worldAabbMin;
    vec4 worldAabbMax;
    uvec4 dims_palette_flags;
    uvec4 misc;
    vec4 wrapOffset;  // xyz = wrap offset for infinite scrolling
    uvec4 occupiedMin;          // xyz = first occupied voxel index
    uvec4 occupiedMaxExclusive; // xyz = one-past-last occupied voxel index
};

// volume flags (stored in misc.x)
const uint FLAG_GLASS = 0x04u;
const uint FLAG_WATER = 0x08u;
const uint FLAG_WRAP_XZ = 0x10u;
const uint FLAG_CLOUD = 0x20u;
const uint FLAG_ALLOW_DENSE_SKIP = 0x40u;

// volume gpu feature bits (stored in misc.w)
const uint VOLUME_FEATURE_TERRAIN_SHADOW_COLUMNS = 0x01u;

// lighting occlusion mode (stored in misc.z)
const uint LIGHTING_OCCLUSION_BINARY_OPAQUE = 0u;
const uint LIGHTING_OCCLUSION_NONE = 1u;
const uint LIGHTING_OCCLUSION_TRANSLUCENT_FOLIAGE = 2u;
const uint LIGHTING_OCCLUSION_PROXY = 3u;
const uint LIGHTING_OCCLUSION_TRANSLUCENT_MEADOW = 4u;

// apply wrap offset to a local-space position (continuous, smooth)
// this should be called before converting to voxel coordinates
vec3 applyWrapOffset(vec3 localPos, VolumeGpu vol) {
    uint flags = vol.misc.x;
    if ((flags & FLAG_WRAP_XZ) != 0u) {
        vec3 dims = vec3(vol.dims_palette_flags.xyz);
        // add offset and wrap to [0, dims) - smooth float operation
        vec3 wrapped = mod(localPos + vol.wrapOffset.xyz, dims);
        // handle negative values
        wrapped = mod(wrapped + dims, dims);
        return wrapped;
    }
    return localPos;
}

// wrap integer voxel coordinates (for dda stepping that goes out of bounds)
ivec3 wrapVoxelCoord(ivec3 voxel, VolumeGpu vol) {
    uint flags = vol.misc.x;
    if ((flags & FLAG_WRAP_XZ) != 0u) {
        ivec3 dims = ivec3(vol.dims_palette_flags.xyz);
        // wrap x and z only (y stays bounded)
        voxel.x = int(mod(float(voxel.x) + float(dims.x), float(dims.x)));
        voxel.z = int(mod(float(voxel.z) + float(dims.z), float(dims.z)));
    }
    return voxel;
}

bool volumeHasOccupiedVoxels(VolumeGpu vol) {
    return vol.occupiedMaxExclusive.x > vol.occupiedMin.x &&
           vol.occupiedMaxExclusive.y > vol.occupiedMin.y &&
           vol.occupiedMaxExclusive.z > vol.occupiedMin.z;
}

bool volumeLocalTraceBounds(VolumeGpu vol, int padVoxels, out vec3 boundsMin, out vec3 boundsMax) {
    ivec3 dims = ivec3(vol.dims_palette_flags.xyz);
    boundsMin = vec3(0.0);
    boundsMax = vec3(dims);

    if (any(lessThanEqual(dims, ivec3(0)))) {
        return false;
    }

    uint flags = vol.misc.x;
    if ((flags & FLAG_WRAP_XZ) != 0u) {
        return true;
    }

    if (!volumeHasOccupiedVoxels(vol)) {
        boundsMax = vec3(0.0);
        return false;
    }

    int pad = max(padVoxels, 0);
    ivec3 occupiedMin = clamp(ivec3(vol.occupiedMin.xyz) - ivec3(pad), ivec3(0), dims);
    ivec3 occupiedMax = clamp(ivec3(vol.occupiedMaxExclusive.xyz) + ivec3(pad), ivec3(0), dims);
    if (any(lessThanEqual(occupiedMax, occupiedMin))) {
        boundsMax = boundsMin;
        return false;
    }

    boundsMin = vec3(occupiedMin);
    boundsMax = vec3(occupiedMax);
    return true;
}

layout(std430, set = VOXEL_SET, binding = 1) readonly buffer VolumesBuf
{
    VolumeGpu gVolumes[];
};

#endif // VOXEL_VOLUME_GLSL
