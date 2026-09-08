#version 450
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 inPos;    // unit cube [0..1]
layout(location = 1) in vec3 inNormal; // face normal (outward)

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 viewProjUnjittered;
    mat4 prevViewProjUnjittered;
    mat4 prevViewProj;
    mat4 invViewProjUnjittered;
    vec4 cameraWorld;
    vec2 renderSize;
    vec2 invRenderSize;
} uFrame;

#define VOXEL_SET 1
#include "voxel_volume.glsl"

#if OBB_SHARED_ALIGNED_LAYERS
layout(push_constant) uniform PC {
    layout(offset = 0) uint volumeIndex;
    layout(offset = 92) uint secondaryVolumeIndex;
} pc;
#else
layout(push_constant) uniform PC {
    uint volumeIndex;
    uint debugMode;
    uint skipEnabled;
    uint skipMip;
} pc;
#endif

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec3 vRawNormal; // debug: raw input normal
layout(location = 3) out vec3 vLocalPos;

void main()
{
    VolumeGpu v = gVolumes[pc.volumeIndex];
    vec3 dims = vec3(v.dims_palette_flags.xyz);
    uint flags = v.misc.x;
    bool wrapEnabled = (flags & FLAG_WRAP_XZ) != 0u;
    vec3 drawMin = vec3(0.0);
    vec3 drawMax = dims;
    if (!wrapEnabled)
    {
        volumeLocalTraceBounds(v, 0, drawMin, drawMax);
#if OBB_SHARED_ALIGNED_LAYERS
        VolumeGpu secondaryV = gVolumes[pc.secondaryVolumeIndex];
        vec3 secondaryMin = vec3(0.0);
        vec3 secondaryMax = dims;
        if (!volumeLocalTraceBounds(secondaryV, 0, secondaryMin, secondaryMax))
        {
            secondaryMin = drawMin;
            secondaryMax = drawMax;
        }
        drawMin = min(drawMin, secondaryMin);
        drawMax = max(drawMax, secondaryMax);
#endif
    }
    vec3 localPos = drawMin + inPos * max(drawMax - drawMin, vec3(0.0));

    // prevent seam overlap at chunk boundaries by shrinking max faces that have neighbors.
    uint neighborMask = v.misc.y;
    const float eps = 1e-3;
    if (inNormal.x > 0.5 && (neighborMask & 1u) != 0u && drawMax.x >= dims.x - 0.5)
    {
        localPos.x = max(drawMin.x, localPos.x - eps);
    }
    if (inNormal.y > 0.5 && (neighborMask & 2u) != 0u && drawMax.y >= dims.y - 0.5)
    {
        localPos.y = max(drawMin.y, localPos.y - eps);
    }
    if (inNormal.z > 0.5 && (neighborMask & 4u) != 0u && drawMax.z >= dims.z - 0.5)
    {
        localPos.z = max(drawMin.z, localPos.z - eps);
    }
    vec4 worldPos = v.worldFromLocal * vec4(localPos, 1.0);

    // transform normal to world space
    mat3 normalMat = transpose(inverse(mat3(v.worldFromLocal)));
    vec3 worldNormal = normalize(normalMat * inNormal);

    // for back-face rendering when camera is outside the box:
    // the outward-facing normals already point toward the camera, so no flip needed.
    // (flip would be needed if camera was inside the box looking at interior walls)
    vWorldNormal = worldNormal;
    vWorldPos = worldPos.xyz;
    vRawNormal = inNormal; // debug: pass raw normal for debugging
    vLocalPos = localPos;

    vec4 clip = uFrame.viewProj * worldPos;
    // expand clip-space bounds by half a pixel to stabilize coverage across jitter.
    vec2 expand = uFrame.invRenderSize * clip.w;
    vec2 signClip = vec2(clip.x >= 0.0 ? 1.0 : -1.0, clip.y >= 0.0 ? 1.0 : -1.0);
    clip.xy += signClip * expand;
    gl_Position = clip;
}
