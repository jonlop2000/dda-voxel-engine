#version 450
#define SHADOW_CASCADE_COUNT 3
layout(location=0) in vec3 aPos;

layout(set=0, binding=0) uniform ShadowUBO {
    mat4 lightViewProj[SHADOW_CASCADE_COUNT];
} u;

layout(push_constant) uniform ObjectPC {
    mat4 model;
    ivec4 cascadeIndex;
} pc;

void main() {
    gl_Position = u.lightViewProj[pc.cascadeIndex.x] * pc.model * vec4(aPos, 1.0);
}
