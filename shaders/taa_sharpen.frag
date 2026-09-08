#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform PC {
    vec2 texelSize;
    float sharpenStrength;
    float padding;
} pc;

void main() {
    vec3 center = texture(uInput, vUV).rgb;
    vec3 left = texture(uInput, vUV - vec2(pc.texelSize.x, 0.0)).rgb;
    vec3 right = texture(uInput, vUV + vec2(pc.texelSize.x, 0.0)).rgb;
    vec3 top = texture(uInput, vUV - vec2(0.0, pc.texelSize.y)).rgb;
    vec3 bottom = texture(uInput, vUV + vec2(0.0, pc.texelSize.y)).rgb;

    vec3 neighbors = left + right + top + bottom;
    vec3 sharpened = center * (1.0 + 4.0 * pc.sharpenStrength) - neighbors * pc.sharpenStrength;

    oColor = vec4(max(sharpened, vec3(0.0)), 1.0);
}
