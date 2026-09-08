#version 450

layout(location = 0) out vec4 fragColor;

void main() {
    // output depth value to color attachment for visualization
    // gl_FragCoord.z is in [0,1] range (0=near, 1=far)
    float depth = gl_FragCoord.z;
    fragColor = vec4(depth, depth, depth, 1.0);
}
