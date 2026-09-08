#version 450

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec3 vRawNormal;
layout(location = 3) out vec3 vLocalPos;

void main()
{
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0));

    // the dda fragment shader reconstructs its ray from gl_FragCoord and does not
    // consume the proxy interpolants. keep its established interface intact so the
    // same optimized fragment modules can be paired with this coverage-only vertex.
    vWorldPos = vec3(0.0);
    vWorldNormal = vec3(0.0, 1.0, 0.0);
    vRawNormal = vec3(0.0, 1.0, 0.0);
    vLocalPos = vec3(0.0);
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
