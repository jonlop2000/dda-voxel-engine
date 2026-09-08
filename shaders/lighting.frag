#version 450
layout(location=0) in vec2 vUV;
layout(location=0) out vec4 oColor;

layout(set=0, binding=0) uniform sampler2D gAlbedo;
layout(set=0, binding=1) uniform sampler2D gNormal;
layout(set=0, binding=2) uniform sampler2D gMaterial;
layout(set=0, binding=3) uniform sampler2D gDepth;

layout(set=1, binding=0) uniform UBO {
    mat4 invViewProj;
    vec4 camPos;
    vec4 lightDir;
    vec4 lightColor;
} u;

vec3 reconstructViewPos(vec2 uv, float depth01) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth01, 1.0);
    vec4 wpos = u.invViewProj * ndc;
    wpos /= wpos.w;
    return wpos.xyz;
}

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);

    vec3 albedo = texture(gAlbedo, uv).rgb;

    vec3 n = texture(gNormal, uv).rgb * 2.0 - 1.0;
    n = normalize(n);

    vec4 mat = texture(gMaterial, uv);
    float rough = clamp(mat.r, 0.02, 1.0);
    float emissive = max(mat.a, 0.0);

    float depth01 = texture(gDepth, uv).r;
    if (depth01 >= 0.9999) {
        oColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 pos = reconstructViewPos(uv, depth01);
    vec3 V = normalize(u.camPos.xyz - pos);

    vec3 L = normalize(-u.lightDir.xyz);
    float NdotL = max(dot(n, L), 0.0);

    vec3 H = normalize(L + V);
    float specPow = mix(128.0, 8.0, rough);
    float spec = pow(max(dot(n, H), 0.0), specPow);

    vec3 light = u.lightColor.rgb;
    vec3 color = albedo * NdotL * light + spec * light + albedo * emissive * 3.0;

    oColor = vec4(color, 1.0);
}
