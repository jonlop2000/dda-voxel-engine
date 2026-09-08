#version 450

layout(location = 0) in vec3 vViewPos;
layout(location = 0) out float oDepth;

void main() {
    oDepth = max(-vViewPos.z, 0.0);
}
