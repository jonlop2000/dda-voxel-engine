#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform PC {
    vec2 inputTexelSize;
    float sharpness;
    float edgeStrength;
    int mode;
} pc;

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 sampleInput(vec2 uv) {
    vec2 halfTexel = pc.inputTexelSize * 0.5;
    return texture(uInput, clamp(uv, halfTexel, vec2(1.0) - halfTexel)).rgb;
}

void main() {
    vec3 center = sampleInput(vUV);
    if (pc.mode == 0) {
        oColor = vec4(center, 1.0);
        return;
    }

    vec2 texel = pc.inputTexelSize;
    vec3 north = sampleInput(vUV + vec2(0.0, -texel.y));
    vec3 south = sampleInput(vUV + vec2(0.0, texel.y));
    vec3 east = sampleInput(vUV + vec2(texel.x, 0.0));
    vec3 west = sampleInput(vUV + vec2(-texel.x, 0.0));

    float horizontalGradient = abs(luminance(east) - luminance(west));
    float verticalGradient = abs(luminance(north) - luminance(south));
    float edge = smoothstep(0.015, 0.20,
                            max(horizontalGradient, verticalGradient));

    // prefer reconstruction samples that run along an edge, avoiding the
    // high-contrast direction that would pull foreground color across it.
    vec3 alongEdge = horizontalGradient > verticalGradient
                         ? (north + south) * 0.5
                         : (east + west) * 0.5;
    float directionalWeight = edge * clamp(pc.edgeStrength, 0.0, 1.0) * 0.18;
    vec3 reconstructed = mix(center, alongEdge, directionalWeight);

    // a bounded contrast-adaptive sharpen restores detail lost by reconstruction.
    // the neighborhood clamp prevents ringing around voxel silhouettes and emissives.
    vec3 blur = (north + south + east + west) * 0.25;
    float adaptiveSharpness = clamp(pc.sharpness, 0.0, 1.0) * (0.35 + 0.65 * edge);
    vec3 sharpened = reconstructed + (reconstructed - blur) * adaptiveSharpness;
    vec3 neighborhoodMin = min(center, min(min(north, south), min(east, west)));
    vec3 neighborhoodMax = max(center, max(max(north, south), max(east, west)));
    oColor = vec4(clamp(sharpened, neighborhoodMin, neighborhoodMax), 1.0);
}
