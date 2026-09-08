#version 450

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 2) flat in uint inFlags;

layout(set = 0, binding = 0) uniform sampler2D uAtlas;

layout(location = 0) out vec4 outColor;

const uint UI_TEXTURE_FLAG_MSDF = 1u;

float median3(vec3 value)
{
    return max(min(value.r, value.g), min(max(value.r, value.g), value.b));
}

void main()
{
    vec4 texel = texture(uAtlas, inUv);
    float alpha = texel.a;
    vec3 textureColor = texel.rgb;

    if ((inFlags & UI_TEXTURE_FLAG_MSDF) != 0u)
    {
        float signedDistance = median3(texel.rgb) - 0.5;
        float smoothing = max(fwidth(signedDistance), 0.0001);
        alpha = smoothstep(-smoothing, smoothing, signedDistance);
        textureColor = vec3(1.0);
    }

    outColor = vec4(inColor.rgb * textureColor * alpha, inColor.a * alpha);
}
