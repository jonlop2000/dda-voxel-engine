#version 450
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(set = 1, binding = 0) uniform UBO {
    mat4 view;
    mat4 viewProj;
    mat4 invProj;
    mat4 reflectedViewProj;
    vec4 camPos;
    vec4 sunDirToSun;
    vec4 sunColor;
    vec4 waterTint;
    vec4 params0;
    vec4 params1;
    vec4 params2;
    vec4 params3;
    vec4 params4;
    vec4 v2Optics0;
    vec4 v2Optics1;
    vec4 v2Body0;
    vec4 v2Body1;
    vec4 v2Body2;
    vec4 v2Reflection0;
    vec4 v2Reflection1;
    vec4 v2Planar0;
    vec4 v2Planar1;
    vec4 v2Planar2;
    vec4 v2Contact0;
    vec4 v2Contact1;
    vec4 v2Contact2;
    vec4 v2Opacity0;
    vec4 v2Opacity1;
    vec4 v2Vfx0;
    vec4 v2Vfx1;
    vec4 v2Vfx2;
    vec4 v2BodyDetail0;
    vec4 atmosphere0;
} u;

layout(location = 0) out vec3 vWorldPos;

#include "water_waves.glsl"

void main() {
    vec3 pos = aPos;
    pos.y += u.params0.x;

    float time = u.params0.y;
    float waveScale = max(u.params0.z, 0.001);
    float waveAmp = max(u.params0.w, 0.0);
    int styleMode = int(u.params4.z + 0.5);
    if (styleMode < 2) {
        pos.y += waveHeight(pos.xz, time, waveScale, waveAmp);
    }

    vec4 worldPos = vec4(pos, 1.0);
    vWorldPos = worldPos.xyz;

    gl_Position = u.viewProj * worldPos;
}
