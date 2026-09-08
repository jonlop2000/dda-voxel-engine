#ifndef STOCHASTIC_COMMON_GLSL
#define STOCHASTIC_COMMON_GLSL

// configure these before including if needed.
#ifndef STOCHASTIC_BN_SIZE
#define STOCHASTIC_BN_SIZE 128
#endif

// if a custom blue-noise fetch is not provided, declare the sampler here.
#ifndef STOCHASTIC_BLUE_NOISE_FETCH
#ifndef STOCHASTIC_SET
#define STOCHASTIC_SET 0
#endif
#ifndef STOCHASTIC_BINDING
#define STOCHASTIC_BINDING 0
#endif
layout(set = STOCHASTIC_SET, binding = STOCHASTIC_BINDING) uniform sampler2D uBlueNoise;
#define STOCHASTIC_BLUE_NOISE_FETCH(pixel) \
    texelFetch(uBlueNoise, (pixel) % STOCHASTIC_BN_SIZE, 0).rg
#endif

const float STOCHASTIC_PI = 3.14159265358979323846;

// martin roberts' R2 sequence.
const float PLASTIC_CONSTANT = 1.324717957244746;
const float PLASTIC_INV1 = 1.0 / PLASTIC_CONSTANT;
const float PLASTIC_INV2 = 1.0 / (PLASTIC_CONSTANT * PLASTIC_CONSTANT);

float r1Sequence(uint n)
{
    const float PHI = 1.618033988749895;
    return fract(float(n) / PHI);
}

vec2 r2Sequence(uint n)
{
    // wrap to avoid float precision loss on long runs.
    uint wrapped = n & 0xFFFu;
    return fract(vec2(float(wrapped) * PLASTIC_INV1,
                      float(wrapped) * PLASTIC_INV2));
}

vec2 stochasticNoise2D(ivec2 pixel, uint frameIndex)
{
    vec2 blue = STOCHASTIC_BLUE_NOISE_FETCH(pixel);
    return fract(blue + r2Sequence(frameIndex));
}

float stochasticNoise1D(ivec2 pixel, uint frameIndex)
{
    float blue = STOCHASTIC_BLUE_NOISE_FETCH(pixel).r;
    return fract(blue + r1Sequence(frameIndex));
}

vec3 cosineWeightedHemisphere(vec2 noise)
{
    float phi = 2.0 * STOCHASTIC_PI * noise.x;
    float cosTheta = sqrt(noise.y);
    float sinTheta = sqrt(1.0 - noise.y);
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

mat3 buildTangentFrame(vec3 normal)
{
    vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return mat3(tangent, bitangent, normal);
}

vec3 sampleHemisphere(vec2 noise, vec3 worldNormal)
{
    vec3 localDir = cosineWeightedHemisphere(noise);
    mat3 tbn = buildTangentFrame(worldNormal);
    return normalize(tbn * localDir);
}

vec3 uniformHemisphere(vec2 noise)
{
    float phi = 2.0 * STOCHASTIC_PI * noise.x;
    float cosTheta = noise.y;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

vec3 jitterReflection(vec3 reflectDir, float roughness, vec2 noise, vec3 normal)
{
    if (roughness < 0.01)
    {
        return reflectDir;
    }

    mat3 tbn = buildTangentFrame(reflectDir);
    vec3 jitter = uniformHemisphere(noise);
    vec3 jittered = normalize(reflectDir + tbn * jitter * roughness * roughness);

    if (dot(jittered, normal) < 0.0)
    {
        jittered = reflect(jittered, normal);
    }

    return jittered;
}

vec3 jitterSunDirection(vec3 sunDir, float angularRadius, vec2 noise)
{
    mat3 tbn = buildTangentFrame(sunDir);

    float r = sqrt(noise.x) * angularRadius;
    float theta = 2.0 * STOCHASTIC_PI * noise.y;

    vec3 offset = tbn * vec3(cos(theta) * r, sin(theta) * r, 0.0);
    return normalize(sunDir + offset);
}

#endif // STOCHASTIC_COMMON_GLSL
