#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform PC {
    vec2 dir;
    float sigma;
    float padding;
} pc;

float gaussian(float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma));
}

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);
    vec2 texel = 1.0 / vec2(textureSize(uTex, 0));
    vec2 d = pc.dir * texel;
    float sigma = max(pc.sigma, 0.001);

    float w0 = 1.0;
    float w1 = gaussian(1.0, sigma);
    float w2 = gaussian(2.0, sigma);
    float w3 = gaussian(3.0, sigma);
    float w4 = gaussian(4.0, sigma);
    float sum = w0 + 2.0 * (w1 + w2 + w3 + w4);

    vec3 s = texture(uTex, uv).rgb * w0;
    s += texture(uTex, uv + d * 1.0).rgb * w1;
    s += texture(uTex, uv - d * 1.0).rgb * w1;
    s += texture(uTex, uv + d * 2.0).rgb * w2;
    s += texture(uTex, uv - d * 2.0).rgb * w2;
    s += texture(uTex, uv + d * 3.0).rgb * w3;
    s += texture(uTex, uv - d * 3.0).rgb * w3;
    s += texture(uTex, uv + d * 4.0).rgb * w4;
    s += texture(uTex, uv - d * 4.0).rgb * w4;
    s /= sum;

    oColor = vec4(s, 1.0);
}
