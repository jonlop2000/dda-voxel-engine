#version 450
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec3 vRawNormal; // debug: raw input normal from vertex buffer

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

#define VOXEL_SET 2
#include "voxel_volume.glsl"

layout(push_constant) uniform PC {
    uint volumeIndex;
    uint debugMode;
    uint skipEnabled;
    uint skipMip;
} pc;

layout(location = 0) out vec4 oAlbedo;
layout(location = 1) out vec4 oNormal;
layout(location = 2) out vec4 oMaterial;
layout(location = 3) out vec4 oVelocity;
layout(location = 4) out vec2 oWaterMeta;

vec3 debugColor(uint id)
{
    float f = fract(sin(float(id) * 12.9898) * 43758.5453);
    return vec3(0.25 + 0.75 * f, 0.2 + 0.8 * fract(f * 7.0),
                0.25 + 0.75 * fract(f * 13.0));
}

void main()
{
    VolumeGpu v = gVolumes[pc.volumeIndex];
    uint id = v.dims_palette_flags.w;
    vec3 c = debugColor(id);

    // use interpolated normal from vertex shader (already transformed and flipped)
    vec3 N = normalize(vWorldNormal);

    oAlbedo = vec4(c, 1.0);
    oNormal = vec4(N * 0.5 + 0.5, 1.0);
    oMaterial = vec4(0.8, 0.0, 1.0, 0.0);

    vec4 currentClip = uFrame.viewProjUnjittered * vec4(vWorldPos, 1.0);
    vec4 prevClip = uFrame.prevViewProjUnjittered * vec4(vWorldPos, 1.0);
    vec2 currentNdc = currentClip.xy / max(currentClip.w, 0.0001);
    vec2 prevNdc = prevClip.xy / max(prevClip.w, 0.0001);
    vec2 velocity = (currentNdc - prevNdc) * 0.5;
    float reactiveMask = 0.0;
    oVelocity = vec4(velocity, reactiveMask, 0.0);
    oWaterMeta = vec2(0.0);
}
