#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uHDR;

layout(push_constant) uniform PC {
    float threshold;
    float knee;
    float padding0;
    float padding1;
} pc;

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);
    vec3 c = texture(uHDR, uv).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float t = max(pc.threshold, 0.0);
    float k = max(pc.knee, 0.0001);
    float soft = smoothstep(t - k, t + k, lum);
    vec3 bright = c * soft;
    oColor = vec4(bright, 1.0);
}
