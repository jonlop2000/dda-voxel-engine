#version 450
layout(location=0) in vec3 vNormalWS;
layout(location=1) in vec2 vUV;
layout(location=2) in vec3 vWorldPos;

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

layout(set=1, binding=0) uniform sampler2D tBaseColor;
layout(set=1, binding=1) uniform sampler2D tNormal;
layout(set=1, binding=2) uniform sampler2D tORM;

layout(push_constant) uniform PC {
    mat4 model;
    vec4 baseColorFactor;
    vec4 rmAo;
} pc;

layout(location=0) out vec4 oAlbedo;
layout(location=1) out vec4 oNormal;
layout(location=2) out vec4 oMaterial;
layout(location=3) out vec4 oVelocity;
layout(location=4) out vec2 oWaterMeta;

void main() {
    vec4 base = texture(tBaseColor, vUV) * pc.baseColorFactor;
    // albedo is already linear when sampling from an sRGB texture.
    oAlbedo = vec4(base.rgb, 1.0);

    vec3 n = normalize(vNormalWS);
    // encode normal into 0..1 for storage.
    oNormal = vec4(n * 0.5 + 0.5, 1.0);

    float roughness = clamp(pc.rmAo.r, 0.02, 1.0);
    float metallic = clamp(pc.rmAo.g, 0.0, 1.0);
    float ao = clamp(pc.rmAo.b, 0.0, 1.0);
    oMaterial = vec4(roughness, metallic, ao, 0.0);

    vec4 currentClip = u.viewProjUnjittered * vec4(vWorldPos, 1.0);
    vec4 prevClip = u.prevViewProjUnjittered * vec4(vWorldPos, 1.0);
    vec2 currentNdc = currentClip.xy / max(currentClip.w, 0.0001);
    vec2 prevNdc = prevClip.xy / max(prevClip.w, 0.0001);
    vec2 velocity = (currentNdc - prevNdc) * 0.5;
    float reactiveMask = 0.0;
    oVelocity = vec4(velocity, reactiveMask, 0.0);
    oWaterMeta = vec2(0.0);
}
