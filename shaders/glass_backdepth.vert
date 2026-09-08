#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 viewProj;
    vec4 camPos;
    vec4 tint;
    vec4 params0;
    vec4 params1;
    vec4 params2;
    vec4 reflectionColor;
} u;

layout(push_constant) uniform PC {
    mat4 model;
} pc;

layout(location = 0) out vec3 vViewPos;

void main() {
    vec4 worldPos = pc.model * vec4(aPos, 1.0);
    vec4 viewPos = u.view * worldPos;
    vViewPos = viewPos.xyz;
    gl_Position = u.viewProj * worldPos;
}
