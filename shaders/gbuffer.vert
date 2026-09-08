#version 450
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

layout(set=0, binding=0) uniform FrameUBO {
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
} u;

layout(push_constant) uniform ObjectPC {
    mat4 model;
    vec4 baseColorFactor;
    vec4 rmAo;
} pc;

layout(location=0) out vec3 vNormalWS;
layout(location=1) out vec2 vUV;
layout(location=2) out vec3 vWorldPos;

void main() {
    // world-space normal for the g-buffer.
    vNormalWS = normalize(mat3(pc.model) * aNormal);
    vUV = aUV;
    vec4 world = pc.model * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    gl_Position = u.viewProj * world;
}
