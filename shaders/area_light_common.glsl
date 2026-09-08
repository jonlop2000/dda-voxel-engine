#ifndef AREA_LIGHT_COMMON_GLSL
#define AREA_LIGHT_COMMON_GLSL

const uint LIGHT_SPHERE = 0u;
const uint LIGHT_RECTANGLE = 1u;
const uint LIGHT_DISC = 2u;
const uint LIGHT_CAPSULE = 3u;
const uint LIGHT_POINT = 4u;

struct AreaLightGpu
{
    vec4 posRadius;       // xyz = center, w = influence radius
    vec4 colorIntensity;  // rgb = color, w = intensity
    vec4 shapeParam0;     // shape-specific
    vec4 shapeParam1;     // shape-specific
    uvec4 shapeInfo;      // x = shapeType, y = sourceRadius bits, z = flags, w = reserved
};

float sourceRadius(AreaLightGpu light)
{
    return uintBitsToFloat(light.shapeInfo.y);
}

vec3 buildOrthoTangent(vec3 n)
{
    if (abs(n.y) < 0.999f)
    {
        return normalize(cross(n, vec3(0.0, 1.0, 0.0)));
    }
    return normalize(cross(n, vec3(1.0, 0.0, 0.0)));
}

vec3 sampleLightPoint(AreaLightGpu light, vec2 rand2)
{
    uint shapeType = light.shapeInfo.x;
    vec3 center = light.posRadius.xyz;
    float srcRadius = sourceRadius(light);

    if (shapeType == LIGHT_SPHERE || shapeType == LIGHT_POINT)
    {
        float z = rand2.x * 2.0 - 1.0;
        float phi = rand2.y * 6.28318530718;
        float r = sqrt(max(0.0, 1.0 - z * z));
        return center + srcRadius * vec3(r * cos(phi), r * sin(phi), z);
    }

    if (shapeType == LIGHT_RECTANGLE)
    {
        vec3 edge1 = light.shapeParam0.xyz;
        vec3 edge2 = light.shapeParam1.xyz;
        float u = rand2.x * 2.0 - 1.0;
        float v = rand2.y * 2.0 - 1.0;
        return center + edge1 * u + edge2 * v;
    }

    if (shapeType == LIGHT_DISC)
    {
        float r = sqrt(rand2.x) * srcRadius;
        float theta = rand2.y * 6.28318530718;
        vec3 n = normalize(light.shapeParam0.xyz);
        vec3 t = buildOrthoTangent(n);
        vec3 b = cross(n, t);
        return center + t * (r * cos(theta)) + b * (r * sin(theta));
    }

    if (shapeType == LIGHT_CAPSULE)
    {
        vec3 endA = light.shapeParam0.xyz;
        vec3 endB = light.shapeParam1.xyz;
        vec3 axis = endB - endA;
        float axisLen = length(axis);
        if (axisLen < 1e-4)
        {
            return center;
        }

        vec3 axisDir = axis / axisLen;
        vec3 t = buildOrthoTangent(axisDir);
        vec3 b = cross(axisDir, t);

        float along = rand2.x;
        float angle = rand2.y * 6.28318530718;
        vec3 axisPoint = mix(endA, endB, along);
        vec3 radial = t * cos(angle) + b * sin(angle);
        return axisPoint + radial * srcRadius;
    }

    return center;
}

vec3 closestPointOnLight(AreaLightGpu light, vec3 worldPos)
{
    uint shapeType = light.shapeInfo.x;
    vec3 center = light.posRadius.xyz;
    float srcRadius = sourceRadius(light);

    if (shapeType == LIGHT_SPHERE || shapeType == LIGHT_POINT)
    {
        vec3 toCenter = center - worldPos;
        float dist = length(toCenter);
        if (dist < 1e-4)
        {
            return center;
        }
        return center - normalize(toCenter) * min(srcRadius, dist);
    }

    if (shapeType == LIGHT_RECTANGLE)
    {
        vec3 edge1 = light.shapeParam0.xyz;
        vec3 edge2 = light.shapeParam1.xyz;
        float len1 = length(edge1);
        float len2 = length(edge2);
        if (len1 < 1e-4 || len2 < 1e-4)
        {
            return center;
        }

        vec3 dir1 = edge1 / len1;
        vec3 dir2 = edge2 / len2;
        vec3 toPoint = worldPos - center;

        float proj1 = clamp(dot(toPoint, dir1), -len1, len1);
        float proj2 = clamp(dot(toPoint, dir2), -len2, len2);
        return center + dir1 * proj1 + dir2 * proj2;
    }

    if (shapeType == LIGHT_DISC)
    {
        vec3 n = normalize(light.shapeParam0.xyz);
        vec3 toPoint = worldPos - center;
        vec3 onPlane = toPoint - n * dot(toPoint, n);
        float dist = length(onPlane);
        if (dist > srcRadius && dist > 1e-4)
        {
            onPlane *= srcRadius / dist;
        }
        return center + onPlane;
    }

    if (shapeType == LIGHT_CAPSULE)
    {
        vec3 endA = light.shapeParam0.xyz;
        vec3 endB = light.shapeParam1.xyz;
        vec3 axis = endB - endA;
        float axisLen = length(axis);
        if (axisLen < 1e-4)
        {
            return center;
        }

        vec3 axisDir = axis / axisLen;
        float t = clamp(dot(worldPos - endA, axisDir) / axisLen, 0.0, 1.0);
        vec3 closestOnAxis = mix(endA, endB, t);

        vec3 toSurface = worldPos - closestOnAxis;
        float dist = length(toSurface);
        if (dist > 1e-4)
        {
            return closestOnAxis + normalize(toSurface) * min(srcRadius, dist);
        }
        return closestOnAxis;
    }

    return center;
}

#endif
