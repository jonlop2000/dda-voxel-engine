#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform PC {
    int mode;
    int tonemapOn;
    float exposure;
    float padding;
} pc;

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);
    vec4 sampleColor = texture(uTex, uv);
    vec3 c = sampleColor.rgb;

    if (pc.tonemapOn != 0) {
        c = max(c, vec3(0.0));
        c = vec3(1.0) - exp(-c * max(pc.exposure, 0.0));
        c = pow(c, vec3(1.0 / 2.2));
    }

    oColor = vec4(c, sampleColor.a);
}
