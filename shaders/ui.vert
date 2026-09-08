#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 3) in uint inFlags;

layout(push_constant) uniform UiPushConstants
{
    vec2 viewportSize;
} pc;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
layout(location = 2) flat out uint outFlags;

void main()
{
    vec2 ndc;
    ndc.x = (inPos.x / max(pc.viewportSize.x, 1.0)) * 2.0 - 1.0;
    ndc.y = (inPos.y / max(pc.viewportSize.y, 1.0)) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
    outUv = inUv;
    outColor = inColor;
    outFlags = inFlags;
}
