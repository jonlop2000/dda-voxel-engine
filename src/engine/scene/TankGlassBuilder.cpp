#include "engine/scene/TankGlassBuilder.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace TankGlassBuilder
{
namespace
{
glm::mat4 makeAabbModel(const glm::vec3& minP, const glm::vec3& maxP)
{
    const glm::vec3 size = glm::max(maxP - minP, glm::vec3(0.0001f));
    const glm::vec3 center = (minP + maxP) * 0.5f;

    glm::mat4 model(1.0f);
    model = glm::translate(model, center);
    model = glm::scale(model, size);
    return model;
}

glm::mat4 makeOrientedBoxModel(const glm::vec3& center, const glm::vec3& size, float yaw)
{
    glm::mat4 model(1.0f);
    model = glm::translate(model, center);
    model = glm::rotate(model, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::max(size, glm::vec3(0.0001f)));
    return model;
}

glm::mat4 makeCurvedPaneModel(const glm::vec3& center, const glm::vec3& size, float yaw,
                              float pitch)
{
    glm::mat4 model(1.0f);
    model = glm::translate(model, center);
    model = glm::rotate(model, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, pitch, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, glm::max(size, glm::vec3(0.0001f)));
    return model;
}

float fishbowlProfileT(float localY, const glm::ivec3& dims)
{
    const float bottom = 3.0f;
    const float top = static_cast<float>(dims.y) - 4.0f;
    return std::clamp((localY - bottom) / std::max(1.0f, top - bottom), 0.0f, 1.0f);
}

float fishbowlOuterRadiusAtLocalY(float localY, const FishbowlGlassSpec& spec)
{
    constexpr float kPi = 3.14159265359f;
    const float t = fishbowlProfileT(localY, spec.volumeDims);
    const float minDim =
        static_cast<float>(std::min(spec.volumeDims.x, spec.volumeDims.z));
    if (spec.profileShape == FishbowlProfileShape::RoundedDome)
    {
        const float roundedT = std::sqrt(std::max(0.0f, 2.0f * t - t * t));
        return minDim * (std::max(0.01f, spec.baseRadiusFactor) +
                         std::max(0.0f, spec.rimRadiusGrowthFactor) * roundedT);
    }
    const float bulb = std::pow(std::max(0.0f, std::sin(kPi * t)), 0.45f);
    return minDim * (std::max(0.01f, spec.baseRadiusFactor) +
                     std::max(0.0f, spec.rimRadiusGrowthFactor) * t +
                     std::max(0.0f, spec.bulbRadiusFactor) * bulb);
}

float fishbowlRadiusDerivativeAtLocalY(float localY, const FishbowlGlassSpec& spec)
{
    const float bottom = 3.0f;
    const float top = static_cast<float>(spec.volumeDims.y) - 4.0f;
    const float y0 = std::clamp(localY - 0.20f, bottom, top);
    const float y1 = std::clamp(localY + 0.20f, bottom, top);
    const float dy = y1 - y0;
    if (std::abs(dy) < 0.0001f)
    {
        return 0.0f;
    }
    return (fishbowlOuterRadiusAtLocalY(y1, spec) -
            fishbowlOuterRadiusAtLocalY(y0, spec)) /
           dy;
}

glm::vec3 fishbowlPoint(const glm::vec3& centerWorld, float localY, float radius, float angle)
{
    return centerWorld +
           glm::vec3(std::sin(angle) * radius, localY, std::cos(angle) * radius);
}

glm::vec3 fishbowlSurfaceNormal(float localY, float angle, const FishbowlGlassSpec& spec,
                                float outwardSign)
{
    const float drdy = fishbowlRadiusDerivativeAtLocalY(localY, spec);
    glm::vec3 normal(std::sin(angle), -drdy, std::cos(angle));
    const float len2 = glm::dot(normal, normal);
    if (len2 <= 1e-8f)
    {
        normal = glm::vec3(std::sin(angle), 0.0f, std::cos(angle));
    }
    else
    {
        normal *= 1.0f / std::sqrt(len2);
    }
    return normal * outwardSign;
}

Vertex makeFishbowlShellVertex(const glm::vec3& centerWorld, float localY, float radius,
                               float angle, const glm::vec3& normal, float u, float v)
{
    return {fishbowlPoint(centerWorld, localY, radius, angle), normal, glm::vec2(u, v)};
}

void appendTri(std::vector<Vertex>& vertices, const Vertex& a, const Vertex& b, const Vertex& c)
{
    vertices.push_back(a);
    vertices.push_back(b);
    vertices.push_back(c);
}
} // namespace

std::vector<glm::mat4> buildRectangularTankGlassPaneModels(const RectangularTankSpec& spec)
{
    std::vector<glm::mat4> models;

    const int wall = std::max(1, spec.wallThickness);
    const glm::ivec3 dims = spec.volumeDims;
    if (dims.x <= wall * 2 + 2 || dims.y <= wall * 2 + 2 || dims.z <= wall * 2 + 2)
    {
        return models;
    }

    const float paneThickness = std::max(0.01f, spec.paneThickness);
    const float paneInteriorGap = std::max(0.10f, paneThickness);

    // match voxel aperture carving in AquariumScene (one-voxel border kept as frame).
    const float xMin = static_cast<float>(wall + 1);
    const float xMax = static_cast<float>(dims.x - wall - 1);
    const float yMin = static_cast<float>(wall + 1);
    const float yMax = static_cast<float>(dims.y - wall - 1);
    const float zMin = static_cast<float>(wall + 1);
    const float zMax = static_cast<float>(dims.z - wall - 1);

    // keep panes in the carved wall aperture, but leave a small air gap before the
    // first interior voxel layer. when the pane touches that voxel face, glass depth
    // and refraction can merge with voxel surfaces and read as stippled speckles.
    const float leftX = static_cast<float>(wall) - paneInteriorGap - paneThickness * 0.5f;
    const float rightX = static_cast<float>(dims.x - wall) + paneInteriorGap + paneThickness * 0.5f;
    const float frontZ = static_cast<float>(wall) - paneInteriorGap - paneThickness * 0.5f;
    const float backZ = static_cast<float>(dims.z - wall) + paneInteriorGap + paneThickness * 0.5f;

    const glm::vec3 worldBase = spec.volumeWorldPos;

    // front pane
    models.push_back(makeAabbModel(worldBase + glm::vec3(xMin, yMin, frontZ - paneThickness * 0.5f),
                                   worldBase + glm::vec3(xMax, yMax, frontZ + paneThickness * 0.5f)));
    // back pane
    models.push_back(makeAabbModel(worldBase + glm::vec3(xMin, yMin, backZ - paneThickness * 0.5f),
                                   worldBase + glm::vec3(xMax, yMax, backZ + paneThickness * 0.5f)));
    // left pane
    models.push_back(makeAabbModel(worldBase + glm::vec3(leftX - paneThickness * 0.5f, yMin, zMin),
                                   worldBase + glm::vec3(leftX + paneThickness * 0.5f, yMax, zMax)));
    // right pane
    models.push_back(makeAabbModel(worldBase + glm::vec3(rightX - paneThickness * 0.5f, yMin, zMin),
                                   worldBase + glm::vec3(rightX + paneThickness * 0.5f, yMax, zMax)));

    if (spec.includeBottom)
    {
        const float bottomY = static_cast<float>(wall) + paneThickness * 0.5f;
        models.push_back(makeAabbModel(
            worldBase + glm::vec3(xMin, bottomY - paneThickness * 0.5f, zMin),
            worldBase + glm::vec3(xMax, bottomY + paneThickness * 0.5f, zMax)));
    }

    return models;
}

std::vector<Vertex> buildFishbowlGlassShellVertices(const FishbowlGlassSpec& spec)
{
    std::vector<Vertex> vertices;

    const glm::ivec3 dims = spec.volumeDims;
    if (dims.x <= 8 || dims.y <= 12 || dims.z <= 8)
    {
        return vertices;
    }

    const int segments = std::clamp(spec.radialSegments, 24, 128);
    const int bands = std::clamp(spec.verticalBands, 8, 36);
    const float paneThickness = std::max(0.02f, spec.paneThickness);
    const float shellDepth = paneThickness + 0.055f;
    constexpr float kTau = 6.28318530718f;
    const float bottomY = 3.0f;
    const float topY = static_cast<float>(dims.y) - 4.0f;
    const glm::vec3 centerWorld =
        spec.volumeWorldPos +
        glm::vec3(static_cast<float>(dims.x) * 0.5f, 0.0f, static_cast<float>(dims.z) * 0.5f);

    const int baseFootVertices = spec.includeBaseFoot ? segments * 12 : 0;
    vertices.reserve(static_cast<size_t>(
        segments * bands * 12 + segments * 12 + baseFootVertices));

    for (int band = 0; band < bands; ++band)
    {
        const float v0 = static_cast<float>(band) / static_cast<float>(bands);
        const float v1 = static_cast<float>(band + 1) / static_cast<float>(bands);
        const float y0 = bottomY + (topY - bottomY) * v0;
        const float y1 = bottomY + (topY - bottomY) * v1;
        const float outerRadius0 = fishbowlOuterRadiusAtLocalY(y0, spec);
        const float outerRadius1 = fishbowlOuterRadiusAtLocalY(y1, spec);
        const float innerRadius0 = std::max(0.05f, outerRadius0 - shellDepth);
        const float innerRadius1 = std::max(0.05f, outerRadius1 - shellDepth);

        for (int segment = 0; segment < segments; ++segment)
        {
            const float u0 = static_cast<float>(segment) / static_cast<float>(segments);
            const float u1 = static_cast<float>(segment + 1) / static_cast<float>(segments);
            const float angle0 = kTau * u0;
            const float angle1 = kTau * u1;

            const Vertex o00 = makeFishbowlShellVertex(
                centerWorld, y0, outerRadius0, angle0,
                fishbowlSurfaceNormal(y0, angle0, spec, 1.0f), u0, v0);
            const Vertex o01 = makeFishbowlShellVertex(
                centerWorld, y0, outerRadius0, angle1,
                fishbowlSurfaceNormal(y0, angle1, spec, 1.0f), u1, v0);
            const Vertex o10 = makeFishbowlShellVertex(
                centerWorld, y1, outerRadius1, angle0,
                fishbowlSurfaceNormal(y1, angle0, spec, 1.0f), u0, v1);
            const Vertex o11 = makeFishbowlShellVertex(
                centerWorld, y1, outerRadius1, angle1,
                fishbowlSurfaceNormal(y1, angle1, spec, 1.0f), u1, v1);

            // outer surface faces outward; inner surface faces inward. keeping both sides
            // preserves the existing glass thickness/depth behavior without cube panel seams.
            appendTri(vertices, o00, o11, o10);
            appendTri(vertices, o00, o01, o11);

            const Vertex i00 = makeFishbowlShellVertex(
                centerWorld, y0, innerRadius0, angle0,
                fishbowlSurfaceNormal(y0, angle0, spec, -1.0f), u0, v0);
            const Vertex i01 = makeFishbowlShellVertex(
                centerWorld, y0, innerRadius0, angle1,
                fishbowlSurfaceNormal(y0, angle1, spec, -1.0f), u1, v0);
            const Vertex i10 = makeFishbowlShellVertex(
                centerWorld, y1, innerRadius1, angle0,
                fishbowlSurfaceNormal(y1, angle0, spec, -1.0f), u0, v1);
            const Vertex i11 = makeFishbowlShellVertex(
                centerWorld, y1, innerRadius1, angle1,
                fishbowlSurfaceNormal(y1, angle1, spec, -1.0f), u1, v1);

            appendTri(vertices, i00, i10, i11);
            appendTri(vertices, i00, i11, i01);
        }
    }

    if (spec.includeRim)
    {
        const float outerRadius = fishbowlOuterRadiusAtLocalY(topY, spec);
        const float innerRadius = std::max(0.05f, outerRadius - shellDepth);
        const glm::vec3 normal(0.0f, 1.0f, 0.0f);
        for (int segment = 0; segment < segments; ++segment)
        {
            const float u0 = static_cast<float>(segment) / static_cast<float>(segments);
            const float u1 = static_cast<float>(segment + 1) / static_cast<float>(segments);
            const float angle0 = kTau * u0;
            const float angle1 = kTau * u1;

            const Vertex outer0 =
                makeFishbowlShellVertex(centerWorld, topY, outerRadius, angle0, normal, u0, 1.0f);
            const Vertex outer1 =
                makeFishbowlShellVertex(centerWorld, topY, outerRadius, angle1, normal, u1, 1.0f);
            const Vertex inner0 =
                makeFishbowlShellVertex(centerWorld, topY, innerRadius, angle0, normal, u0, 1.0f);
            const Vertex inner1 =
                makeFishbowlShellVertex(centerWorld, topY, innerRadius, angle1, normal, u1, 1.0f);

            appendTri(vertices, outer0, inner1, inner0);
            appendTri(vertices, outer0, outer1, inner1);
        }
    }

    if (spec.includeBase)
    {
        const float outerRadius = fishbowlOuterRadiusAtLocalY(bottomY, spec);
        const float innerRadius = std::max(0.05f, outerRadius - shellDepth);
        const glm::vec3 normal(0.0f, -1.0f, 0.0f);
        for (int segment = 0; segment < segments; ++segment)
        {
            const float u0 = static_cast<float>(segment) / static_cast<float>(segments);
            const float u1 = static_cast<float>(segment + 1) / static_cast<float>(segments);
            const float angle0 = kTau * u0;
            const float angle1 = kTau * u1;

            const Vertex outer0 = makeFishbowlShellVertex(centerWorld, bottomY, outerRadius,
                                                          angle0, normal, u0, 0.0f);
            const Vertex outer1 = makeFishbowlShellVertex(centerWorld, bottomY, outerRadius,
                                                          angle1, normal, u1, 0.0f);
            const Vertex inner0 = makeFishbowlShellVertex(centerWorld, bottomY, innerRadius,
                                                          angle0, normal, u0, 0.0f);
            const Vertex inner1 = makeFishbowlShellVertex(centerWorld, bottomY, innerRadius,
                                                          angle1, normal, u1, 0.0f);

            appendTri(vertices, outer0, inner0, inner1);
            appendTri(vertices, outer0, inner1, outer1);
        }
    }

    if (spec.includeBaseFoot)
    {
        const float shellBaseRadius = fishbowlOuterRadiusAtLocalY(bottomY, spec);
        const float footRadius =
            shellBaseRadius * std::clamp(spec.baseFootRadiusScale, 0.25f, 1.25f);
        const float footBottomY = bottomY - std::max(0.05f, spec.baseFootHeight);
        const float footTopY = bottomY + 0.04f;
        const Vertex bottomCenter = makeFishbowlShellVertex(
            centerWorld, footBottomY, 0.0f, 0.0f, glm::vec3(0.0f, -1.0f, 0.0f),
            0.5f, 0.5f);
        const Vertex topCenter = makeFishbowlShellVertex(
            centerWorld, footTopY, 0.0f, 0.0f, glm::vec3(0.0f, 1.0f, 0.0f),
            0.5f, 0.5f);
        for (int segment = 0; segment < segments; ++segment)
        {
            const float u0 = static_cast<float>(segment) / static_cast<float>(segments);
            const float u1 = static_cast<float>(segment + 1) / static_cast<float>(segments);
            const float angle0 = kTau * u0;
            const float angle1 = kTau * u1;
            const glm::vec3 normal0(std::sin(angle0), 0.0f, std::cos(angle0));
            const glm::vec3 normal1(std::sin(angle1), 0.0f, std::cos(angle1));
            const Vertex b0 = makeFishbowlShellVertex(
                centerWorld, footBottomY, footRadius, angle0, normal0, u0, 0.0f);
            const Vertex b1 = makeFishbowlShellVertex(
                centerWorld, footBottomY, footRadius, angle1, normal1, u1, 0.0f);
            const Vertex t0 = makeFishbowlShellVertex(
                centerWorld, footTopY, footRadius, angle0, normal0, u0, 1.0f);
            const Vertex t1 = makeFishbowlShellVertex(
                centerWorld, footTopY, footRadius, angle1, normal1, u1, 1.0f);
            appendTri(vertices, b0, t1, t0);
            appendTri(vertices, b0, b1, t1);

            const Vertex bottom0 = makeFishbowlShellVertex(
                centerWorld, footBottomY, footRadius, angle0,
                glm::vec3(0.0f, -1.0f, 0.0f), u0, 0.0f);
            const Vertex bottom1 = makeFishbowlShellVertex(
                centerWorld, footBottomY, footRadius, angle1,
                glm::vec3(0.0f, -1.0f, 0.0f), u1, 0.0f);
            appendTri(vertices, bottom0, bottomCenter, bottom1);

            const Vertex top0 = makeFishbowlShellVertex(
                centerWorld, footTopY, footRadius, angle0,
                glm::vec3(0.0f, 1.0f, 0.0f), u0, 1.0f);
            const Vertex top1 = makeFishbowlShellVertex(
                centerWorld, footTopY, footRadius, angle1,
                glm::vec3(0.0f, 1.0f, 0.0f), u1, 1.0f);
            appendTri(vertices, top0, top1, topCenter);
        }
    }

    return vertices;
}

std::vector<glm::mat4> buildFishbowlGlassPaneModels(const FishbowlGlassSpec& spec)
{
    std::vector<glm::mat4> models;

    const glm::ivec3 dims = spec.volumeDims;
    if (dims.x <= 8 || dims.y <= 12 || dims.z <= 8)
    {
        return models;
    }

    const int segments = std::clamp(spec.radialSegments, 12, 64);
    const int bands = std::clamp(spec.verticalBands, 4, 20);
    const float paneThickness = std::max(0.02f, spec.paneThickness);
    constexpr float kTau = 6.28318530718f;
    const float bottomY = 3.0f;
    const float topY = static_cast<float>(dims.y) - 4.0f;
    const glm::vec3 centerWorld =
        spec.volumeWorldPos +
        glm::vec3(static_cast<float>(dims.x) * 0.5f, 0.0f, static_cast<float>(dims.z) * 0.5f);

    models.reserve(static_cast<size_t>(segments * bands + segments * 3));
    for (int band = 0; band < bands; ++band)
    {
        const float t0 = static_cast<float>(band) / static_cast<float>(bands);
        const float t1 = static_cast<float>(band + 1) / static_cast<float>(bands);
        const float y0 = bottomY + (topY - bottomY) * t0;
        const float y1 = bottomY + (topY - bottomY) * t1;
        const float y = (y0 + y1) * 0.5f;
        const float radius0 = fishbowlOuterRadiusAtLocalY(y0, spec);
        const float radius1 = fishbowlOuterRadiusAtLocalY(y1, spec);
        const float radius = (radius0 + radius1) * 0.5f;
        const float profilePitch =
            std::clamp(std::atan2(radius1 - radius0, std::max(0.001f, y1 - y0)), -0.34f,
                       0.34f);
        const float arcWidth = (kTau * radius / static_cast<float>(segments)) * 0.995f;
        const float bandHeight = (y1 - y0) * 1.06f;
        const float radialDepth = paneThickness + 0.045f;

        for (int segment = 0; segment < segments; ++segment)
        {
            const float angle =
                kTau * static_cast<float>(segment) / static_cast<float>(segments);
            const glm::vec3 radial(std::sin(angle), 0.0f, std::cos(angle));
            const glm::vec3 panelCenter =
                centerWorld + glm::vec3(radial.x * radius, y, radial.z * radius);
            models.push_back(makeCurvedPaneModel(
                panelCenter, glm::vec3(arcWidth, bandHeight, radialDepth), angle,
                profilePitch));
        }
    }

    if (spec.includeRim)
    {
        const float rimY = topY + 0.18f;
        const float rimRadius =
            fishbowlOuterRadiusAtLocalY(topY, spec) + paneThickness * 0.95f;
        const float arcWidth = (kTau * rimRadius / static_cast<float>(segments)) * 0.995f;
        for (int segment = 0; segment < segments; ++segment)
        {
            const float angle =
                kTau * static_cast<float>(segment) / static_cast<float>(segments);
            const glm::vec3 radial(std::sin(angle), 0.0f, std::cos(angle));
            const glm::vec3 panelCenter =
                centerWorld + glm::vec3(radial.x * rimRadius, rimY, radial.z * rimRadius);
            models.push_back(makeOrientedBoxModel(
                panelCenter, glm::vec3(arcWidth, paneThickness * 1.35f, paneThickness * 2.10f),
                angle));
        }

        const float innerLipRadius = rimRadius - paneThickness * 2.25f;
        const float innerLipY = topY - paneThickness * 1.15f;
        const float innerArcWidth =
            (kTau * innerLipRadius / static_cast<float>(segments)) * 0.96f;
        for (int segment = 0; segment < segments; ++segment)
        {
            const float angle =
                kTau * static_cast<float>(segment) / static_cast<float>(segments);
            const glm::vec3 radial(std::sin(angle), 0.0f, std::cos(angle));
            const glm::vec3 panelCenter =
                centerWorld +
                glm::vec3(radial.x * innerLipRadius, innerLipY, radial.z * innerLipRadius);
            models.push_back(makeOrientedBoxModel(
                panelCenter,
                glm::vec3(innerArcWidth, paneThickness * 0.75f, paneThickness * 1.20f),
                angle));
        }
    }

    if (spec.includeBase)
    {
        const float baseY = bottomY - 0.54f;
        const float baseRadius = fishbowlOuterRadiusAtLocalY(bottomY, spec) * 0.74f;
        const float arcWidth = (kTau * baseRadius / static_cast<float>(segments)) * 0.985f;
        for (int segment = 0; segment < segments; ++segment)
        {
            const float angle =
                kTau * static_cast<float>(segment) / static_cast<float>(segments);
            const glm::vec3 radial(std::sin(angle), 0.0f, std::cos(angle));
            const glm::vec3 panelCenter =
                centerWorld + glm::vec3(radial.x * baseRadius, baseY, radial.z * baseRadius);
            models.push_back(makeOrientedBoxModel(
                panelCenter, glm::vec3(arcWidth, paneThickness * 2.10f, paneThickness * 3.60f),
                angle));
        }
    }

    return models;
}

} // namespace TankGlassBuilder
