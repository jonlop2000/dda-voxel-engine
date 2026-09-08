#version 450

layout(set = 0, binding = 0) uniform FrameUBO
{
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 viewProjUnjittered;
    mat4 prevViewProjUnjittered;
    mat4 prevViewProj;
    mat4 invViewProjUnjittered;
    vec4 cameraWorld;
    vec2 renderSize;
    vec2 invRenderSize;
} u;

struct FoliageGpuPrimitive
{
    vec4 centerMotionT;
    vec4 halfExtentPhase;
    uvec4 materialSeedFlags;
    vec4 swayProfile;
};

layout(std430, set = 1, binding = 0) readonly buffer FoliagePrimitives
{
    FoliageGpuPrimitive primitives[];
} gFoliage;

layout(push_constant) uniform FoliagePC
{
    vec4 timeWind;      // current time, previous time, wind x, wind z
    vec4 controls;      // current/previous sway strength, reflection clip y, clip enabled
    vec4 secondaryWave; // rooted secondary wave
    vec4 crossWave;     // rooted wave xyz, windborne vertical lift w
    vec4 appearance;    // strength, luma contrast, saturation retention, normal up-bias
    vec4 surface;       // botanical identity, roughness separation, face contrast, meadow volume
    vec4 material;      // soft material strength, wind speed, gust strength/frequency
    vec4 ambient;       // raw wind strength, visibility distance, leaf fraction, scale
} pc;

layout(location = 0) flat out vec3 vNormalWS;
layout(location = 1) out vec3 vWorldCurrent;
layout(location = 2) out vec3 vWorldPrevious;
layout(location = 3) flat out uint vMaterialId;
layout(location = 4) flat out uint vPrimitiveRole;
layout(location = 5) flat out float vMotionHeight;

const uint kOrientedSlabFlag = 0x80000000u;
const uint kYawOctantShift = 28u;
const uint kWindborneParticleFlag = 0x80000000u;
const uint kWindborneLeafRole = 4u;
const uint kWindborneMoteRole = 5u;

vec2 rotateOctagonal(vec2 value, uint octant)
{
    const float diagonal = 0.70710678118;
    const mat2 rotations[8] = mat2[8](
        mat2(1.0, 0.0, 0.0, 1.0),
        mat2(diagonal, diagonal, -diagonal, diagonal),
        mat2(0.0, 1.0, -1.0, 0.0),
        mat2(-diagonal, diagonal, -diagonal, -diagonal),
        mat2(-1.0, 0.0, 0.0, -1.0),
        mat2(-diagonal, -diagonal, diagonal, -diagonal),
        mat2(0.0, -1.0, 1.0, 0.0),
        mat2(diagonal, -diagonal, diagonal, diagonal));
    return rotations[octant & 7u] * value;
}

vec2 normalizedWind()
{
    vec2 wind = pc.timeWind.zw;
    float windLength = length(wind);
    return windLength > 1e-5 ? wind / windLength : vec2(1.0, 0.0);
}

vec2 rotateRadians(vec2 value, float angle)
{
    float sine = sin(angle);
    float cosine = cos(angle);
    return mat2(cosine, sine, -sine, cosine) * value;
}

vec3 displacement(FoliageGpuPrimitive primitive, float timeSeconds,
                  float swayStrength)
{
    float t = clamp(primitive.centerMotionT.w, 0.0, 1.0);
    float rootRigidity = clamp(primitive.swayProfile.w, 0.0, 0.9999);
    float bendHeight = clamp((t - rootRigidity) / (1.0 - rootRigidity),
                             0.0, 1.0);
    float bend = pow(bendHeight, primitive.swayProfile.z);
    float amplitude = primitive.swayProfile.x * max(swayStrength, 0.0);
    float speed = primitive.swayProfile.y;
    float phase = primitive.halfExtentPhase.w;
    float scaledTime = timeSeconds * max(pc.material.y, 0.0);
    float gustPhase = timeSeconds * 6.28318530718 *
                          max(pc.material.w, 0.01) +
                      phase;
    float gust = max(0.0, 1.0 + pc.material.z * sin(gustPhase));
    amplitude *= gust;

    float primary = sin(scaledTime * speed + phase);
    float secondary = pc.secondaryWave.z *
        sin(scaledTime * speed * pc.secondaryWave.x +
            phase * pc.secondaryWave.y + pc.secondaryWave.w);
    float alongWave =
        (primary + secondary) / (1.0 + abs(pc.secondaryWave.z));
    float acrossWave = pc.crossWave.z *
        sin(scaledTime * speed * pc.crossWave.x +
            phase * pc.crossWave.y + 2.4);

    vec2 wind = normalizedWind();
    vec2 across = vec2(-wind.y, wind.x);
    vec2 animated = (wind * alongWave + across * acrossWave) * amplitude * bend;
    return vec3(animated.x, 0.0, animated.y);
}

vec3 windborneDisplacement(FoliageGpuPrimitive primitive, float timeSeconds,
                           float windStrength)
{
    float animationTime = timeSeconds * max(pc.material.y, 0.0);
    float phase = primitive.halfExtentPhase.w;
    float motionSpeed = primitive.swayProfile.y;
    float cycle = animationTime * motionSpeed + phase;
    float gustPhase = timeSeconds * 6.28318530718 *
                          max(pc.material.w, 0.01) +
                      dot(primitive.centerMotionT.xz,
                          vec2(0.09125, 0.05125));
    float gust = max(0.0, 1.0 + pc.material.z * sin(gustPhase));
    float amplitude = primitive.swayProfile.x * max(windStrength, 0.0) * gust;
    float turbulenceStrength =
        clamp((pc.crossWave.z - 0.10) / 0.20, 0.0, 1.0);
    float turbulence = turbulenceStrength *
        sin(timeSeconds * 6.28318530718 * max(pc.material.w, 0.01) * 0.73 +
            dot(primitive.centerMotionT.xz,
                vec2(-0.07875, 0.11875)) +
            1.7);
    float along = sin(cycle) * amplitude;
    float across = cos(cycle * 0.83 + 1.7) * amplitude *
        (0.28 + abs(turbulence) * 0.25);
    float flutter = primitive.swayProfile.z;
    float vertical =
        sin(cycle * (1.31 + flutter * 0.22) + 2.4) * amplitude *
            (0.22 + flutter * 0.16) +
        sin(cycle * 0.47 + phase) * amplitude * pc.crossWave.w * 0.18;
    vec2 wind = normalizedWind();
    vec2 cross = vec2(-wind.y, wind.x);
    vec2 horizontal = wind * along + cross * across;
    return vec3(horizontal.x, vertical, horizontal.y);
}

float windborneFlutterAngle(FoliageGpuPrimitive primitive, float timeSeconds)
{
    float animationTime = timeSeconds * max(pc.material.y, 0.0);
    return primitive.halfExtentPhase.w +
        animationTime * primitive.swayProfile.y *
            (1.4 + primitive.swayProfile.z * 2.2);
}

float windborneTiltAngle(FoliageGpuPrimitive primitive, float flutterAngle)
{
    return sin(flutterAngle * 1.37 + primitive.halfExtentPhase.w) * 0.34;
}

vec3 windborneLocal(FoliageGpuPrimitive primitive, vec3 signs,
                    uint yawOctant, float timeSeconds, bool leaf)
{
    vec3 halfExtent = leaf ? primitive.halfExtentPhase.xyz
                           : vec3(0.035);
    halfExtent *= max(pc.ambient.w, 0.0);
    vec3 local = signs * halfExtent;
    local.xz = rotateOctagonal(local.xz, yawOctant);
    float flutterAngle = windborneFlutterAngle(primitive, timeSeconds);
    local.xz = rotateRadians(local.xz, flutterAngle);
    if (leaf)
    {
        local.xy = rotateRadians(local.xy,
                                 windborneTiltAngle(primitive, flutterAngle));
    }
    return local;
}

vec3 faceNormal(uint face)
{
    if (face == 0u) return vec3(1.0, 0.0, 0.0);
    if (face == 1u) return vec3(-1.0, 0.0, 0.0);
    if (face == 2u) return vec3(0.0, 1.0, 0.0);
    if (face == 3u) return vec3(0.0, -1.0, 0.0);
    if (face == 4u) return vec3(0.0, 0.0, 1.0);
    return vec3(0.0, 0.0, -1.0);
}

vec3 cornerSigns(uint face, uint corner)
{
    const vec3 corners[24] = vec3[24](
        vec3( 1.0, -1.0, -1.0), vec3( 1.0,  1.0, -1.0),
        vec3( 1.0,  1.0,  1.0), vec3( 1.0, -1.0,  1.0),
        vec3(-1.0, -1.0,  1.0), vec3(-1.0,  1.0,  1.0),
        vec3(-1.0,  1.0, -1.0), vec3(-1.0, -1.0, -1.0),
        vec3(-1.0,  1.0,  1.0), vec3( 1.0,  1.0,  1.0),
        vec3( 1.0,  1.0, -1.0), vec3(-1.0,  1.0, -1.0),
        vec3(-1.0, -1.0, -1.0), vec3( 1.0, -1.0, -1.0),
        vec3( 1.0, -1.0,  1.0), vec3(-1.0, -1.0,  1.0),
        vec3(-1.0, -1.0,  1.0), vec3( 1.0, -1.0,  1.0),
        vec3( 1.0,  1.0,  1.0), vec3(-1.0,  1.0,  1.0),
        vec3( 1.0, -1.0, -1.0), vec3(-1.0, -1.0, -1.0),
        vec3(-1.0,  1.0, -1.0), vec3( 1.0,  1.0, -1.0));
    return corners[face * 4u + corner];
}

void main()
{
    FoliageGpuPrimitive primitive = gFoliage.primitives[gl_InstanceIndex];
    uint blockVertex = uint(gl_VertexIndex);
    uint face = blockVertex / 6u;
    uint triangleVertex = blockVertex % 6u;
    const uint triangleCorners[6] = uint[6](0u, 1u, 2u, 0u, 2u, 3u);
    uint corner = triangleCorners[triangleVertex];

    vec3 signs = cornerSigns(face, corner);
    bool windborne =
        (primitive.materialSeedFlags.y & kWindborneParticleFlag) != 0u;
    bool windborneLeaf = primitive.swayProfile.w < pc.ambient.z;
    vec3 local = signs * primitive.halfExtentPhase.xyz;
    vec3 normal = faceNormal(face);
    uint encodedSeed = primitive.materialSeedFlags.z;
    uint yawOctant = 0u;
    if ((encodedSeed & kOrientedSlabFlag) != 0u)
    {
        yawOctant = (encodedSeed >> kYawOctantShift) & 7u;
        if (!windborne)
        {
            local.xz = rotateOctagonal(local.xz, yawOctant);
        }
        normal.xz = rotateOctagonal(normal.xz, yawOctant);
    }
    vec3 currentWorld;
    vec3 previousWorld;
    if (windborne)
    {
        float particleDistance =
            distance(primitive.centerMotionT.xyz, u.cameraWorld.xyz);
        float fadeStart = pc.ambient.y * 0.82;
        float visibilityScale = 1.0 - smoothstep(
            fadeStart, pc.ambient.y, particleDistance);
        vec3 currentLocal = windborneLocal(
            primitive, signs, yawOctant, pc.timeWind.x, windborneLeaf) *
            visibilityScale;
        vec3 previousLocal = windborneLocal(
            primitive, signs, yawOctant, pc.timeWind.y, windborneLeaf) *
            visibilityScale;
        currentWorld = primitive.centerMotionT.xyz + currentLocal +
            windborneDisplacement(primitive, pc.timeWind.x, pc.ambient.x);
        previousWorld = primitive.centerMotionT.xyz + previousLocal +
            windborneDisplacement(primitive, pc.timeWind.y, pc.ambient.x);
        float flutterAngle =
            windborneFlutterAngle(primitive, pc.timeWind.x);
        normal.xz = rotateRadians(normal.xz, flutterAngle);
        if (windborneLeaf)
        {
            normal.xy = rotateRadians(
                normal.xy, windborneTiltAngle(primitive, flutterAngle));
        }
        // shrink over the final 18% of the authored range before clipping the
        // degenerate primitive. this avoids a hard leaf/mote pop while keeping
        // the fixed candidate and draw budgets unchanged.
        if (visibilityScale <= 0.001)
        {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
    }
    else
    {
        vec3 restWorld = primitive.centerMotionT.xyz + local;
        currentWorld = restWorld +
            displacement(primitive, pc.timeWind.x, pc.controls.x);
        previousWorld = restWorld +
            displacement(primitive, pc.timeWind.y, pc.controls.y);
    }

    vNormalWS = normal;
    vWorldCurrent = currentWorld;
    vWorldPrevious = previousWorld;
    vMaterialId = windborne
        ? (windborneLeaf ? primitive.materialSeedFlags.x & 0xffu
                         : (primitive.materialSeedFlags.x >> 8u) & 0xffu)
        : primitive.materialSeedFlags.x;
    vPrimitiveRole = windborne
        ? (windborneLeaf ? kWindborneLeafRole : kWindborneMoteRole)
        : primitive.materialSeedFlags.w;
    vMotionHeight = windborne
        ? float((encodedSeed >> 8u) & 0xffffu) * (1.0 / 65535.0)
        : clamp(primitive.centerMotionT.w, 0.0, 1.0);
    // thin, single-sampled voxel blocks crawl when camera jitter changes their
    // binary raster coverage. keep their coverage on the stable output grid;
    // current/previous world positions still produce honest unjittered motion
    // vectors for camera movement and sway.
    gl_Position = u.viewProjUnjittered * vec4(currentWorld, 1.0);
}
