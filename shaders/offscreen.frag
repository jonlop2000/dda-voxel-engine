#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);
    oColor = vec4(uv.x, uv.y, 0.25, 1.0);
}
