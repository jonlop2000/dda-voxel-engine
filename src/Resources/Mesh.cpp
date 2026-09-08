#include "Resources/Mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/render/Commands.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/GpuBuffer.h"

namespace
{

constexpr float kPi = 3.14159265358979323846f;

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep01(float value)
{
    const float t = clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

template <typename RadiusFn>
std::vector<Vertex> makeFishSectionVertices(float length, int slices, int rings, RadiusFn radiiAt,
                                            float thetaMin = 0.0f, float thetaMax = 2.0f * kPi,
                                            float inflate = 1.0f)
{
    slices = std::max(3, slices);
    rings = std::max(1, rings);

    std::vector<Vertex> verts;
    verts.reserve(static_cast<size_t>(slices) * static_cast<size_t>(rings) * 6);

    auto makeVertex = [&](float theta, float t, float u, float v) {
        constexpr float kEps = 0.01f;
        const glm::vec2 radii = radiiAt(t) * inflate;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);
        const glm::vec3 pos(-length * t, radii.x * sinTheta, radii.y * cosTheta);

        const float t0 = std::max(0.0f, t - kEps);
        const float t1 = std::min(1.0f, t + kEps);
        const glm::vec2 prevR = radiiAt(t0) * inflate;
        const glm::vec2 nextR = radiiAt(t1) * inflate;
        const float denom = std::max(1e-4f, t1 - t0);
        const glm::vec2 dR = (nextR - prevR) / denom;

        const glm::vec3 dT(-length, dR.x * sinTheta, dR.y * cosTheta);
        const glm::vec3 dTheta(0.0f, radii.x * cosTheta, -radii.y * sinTheta);
        const glm::vec3 normal = glm::normalize(glm::cross(dTheta, dT));
        return Vertex{pos, normal, glm::vec2(u, v)};
    };

    for (int ring = 0; ring < rings; ++ring)
    {
        const float v0 = static_cast<float>(ring) / static_cast<float>(rings);
        const float v1 = static_cast<float>(ring + 1) / static_cast<float>(rings);

        for (int slice = 0; slice < slices; ++slice)
        {
            const float u0 = static_cast<float>(slice) / static_cast<float>(slices);
            const float u1 = static_cast<float>(slice + 1) / static_cast<float>(slices);
            const float theta0 = thetaMin + (thetaMax - thetaMin) * u0;
            const float theta1 = thetaMin + (thetaMax - thetaMin) * u1;

            const Vertex v00 = makeVertex(theta0, v0, u0, v0);
            const Vertex v01 = makeVertex(theta1, v0, u1, v0);
            const Vertex v10 = makeVertex(theta0, v1, u0, v1);
            const Vertex v11 = makeVertex(theta1, v1, u1, v1);

            verts.push_back(v00);
            verts.push_back(v10);
            verts.push_back(v11);

            verts.push_back(v00);
            verts.push_back(v11);
            verts.push_back(v01);
        }
    }

    return verts;
}

void appendDoubleSidedTri(std::vector<Vertex>& verts, const glm::vec3& a, const glm::vec3& b,
                          const glm::vec3& c, const glm::vec2& uva, const glm::vec2& uvb,
                          const glm::vec2& uvc)
{
    const glm::vec3 frontNormal = glm::normalize(glm::cross(b - a, c - a));
    verts.push_back({a, frontNormal, uva});
    verts.push_back({b, frontNormal, uvb});
    verts.push_back({c, frontNormal, uvc});

    const glm::vec3 backNormal = -frontNormal;
    verts.push_back({a, backNormal, uva});
    verts.push_back({c, backNormal, uvc});
    verts.push_back({b, backNormal, uvb});
}

} // namespace

std::vector<Vertex> makeCubeVertices()
{
    const float s = 0.5f;
    const Vertex v[] = {
        {{-s, -s,  s}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{ s, -s,  s}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{ s,  s,  s}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-s, -s,  s}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{ s,  s,  s}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-s,  s,  s}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},

        {{-s, -s, -s}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{ s,  s, -s}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{ s, -s, -s}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{-s, -s, -s}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{-s,  s, -s}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{ s,  s, -s}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},

        {{-s, -s, -s}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-s, -s,  s}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-s,  s,  s}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-s, -s, -s}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-s,  s,  s}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-s,  s, -s}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

        {{ s, -s, -s}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ s,  s,  s}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{ s, -s,  s}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ s, -s, -s}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ s,  s, -s}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{ s,  s,  s}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},

        {{-s,  s, -s}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{-s,  s,  s}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ s,  s,  s}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-s,  s, -s}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ s,  s,  s}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ s,  s, -s}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},

        {{-s, -s, -s}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ s, -s,  s}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-s, -s,  s}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        {{-s, -s, -s}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ s, -s, -s}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ s, -s,  s}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
    };

    return std::vector<Vertex>(v, v + (sizeof(v) / sizeof(v[0])));
}

std::vector<Vertex> makePlaneVertices(float halfExtent)
{
    const float s = halfExtent;
    const Vertex v[] = {
        {{-s, 0.0f, -s}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ s, 0.0f, -s}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ s, 0.0f,  s}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-s, 0.0f, -s}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ s, 0.0f,  s}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-s, 0.0f,  s}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
    };

    return std::vector<Vertex>(v, v + (sizeof(v) / sizeof(v[0])));
}

std::vector<Vertex> makeGridVertices(float halfExtent, int gridSize)
{
    if (gridSize < 1)
    {
        gridSize = 1;
    }

    std::vector<Vertex> verts;
    verts.reserve(static_cast<size_t>(gridSize) * static_cast<size_t>(gridSize) * 6);

    const float step = (2.0f * halfExtent) / static_cast<float>(gridSize);
    const glm::vec3 n{0.0f, 1.0f, 0.0f};

    for (int z = 0; z < gridSize; ++z)
    {
        for (int x = 0; x < gridSize; ++x)
        {
            const float x0 = -halfExtent + static_cast<float>(x) * step;
            const float x1 = x0 + step;
            const float z0 = -halfExtent + static_cast<float>(z) * step;
            const float z1 = z0 + step;

            const float u0 = static_cast<float>(x) / static_cast<float>(gridSize);
            const float u1 = static_cast<float>(x + 1) / static_cast<float>(gridSize);
            const float v0 = static_cast<float>(z) / static_cast<float>(gridSize);
            const float v1 = static_cast<float>(z + 1) / static_cast<float>(gridSize);

            verts.push_back({{x0, 0.0f, z0}, n, {u0, v0}});
            verts.push_back({{x0, 0.0f, z1}, n, {u0, v1}});
            verts.push_back({{x1, 0.0f, z0}, n, {u1, v0}});

            verts.push_back({{x1, 0.0f, z0}, n, {u1, v0}});
            verts.push_back({{x0, 0.0f, z1}, n, {u0, v1}});
            verts.push_back({{x1, 0.0f, z1}, n, {u1, v1}});
        }
    }

    return verts;
}

std::vector<Vertex> makeFishHeadVertices()
{
    return makeFishSectionVertices(
        0.82f, 28, 8,
        [](float t) {
            const float cheek = std::sin(kPi * clamp01(t));
            const float y = 0.10f + 0.30f * std::pow(clamp01(t), 0.72f) + 0.04f * cheek;
            const float z = 0.07f + 0.20f * std::pow(clamp01(t), 0.78f) + 0.03f * cheek;
            return glm::vec2(y, z);
        });
}

std::vector<Vertex> makeFishMidVertices()
{
    return makeFishSectionVertices(
        0.74f, 28, 7,
        [](float t) {
            const float s = smoothstep01(t);
            const float bulge = std::sin(kPi * clamp01(t));
            const float y = 0.38f + 0.05f * bulge - 0.09f * s;
            const float z = 0.25f + 0.04f * bulge - 0.07f * s;
            return glm::vec2(y, z);
        });
}

std::vector<Vertex> makeFishTailPeduncleVertices()
{
    return makeFishSectionVertices(
        0.48f, 24, 5,
        [](float t) {
            const float s = smoothstep01(t);
            const float y = 0.27f - 0.16f * s;
            const float z = 0.18f - 0.11f * s;
            return glm::vec2(y, z);
        });
}

std::vector<Vertex> makeFishHeadStripeVertices()
{
    return makeFishSectionVertices(
        0.82f, 18, 8,
        [](float t) {
            const float cheek = std::sin(kPi * clamp01(t));
            const float y = 0.10f + 0.30f * std::pow(clamp01(t), 0.72f) + 0.04f * cheek;
            const float z = 0.07f + 0.20f * std::pow(clamp01(t), 0.78f) + 0.03f * cheek;
            return glm::vec2(y, z);
        },
        0.18f * kPi, 0.82f * kPi, 1.035f);
}

std::vector<Vertex> makeFishMidStripeVertices()
{
    return makeFishSectionVertices(
        0.74f, 18, 7,
        [](float t) {
            const float s = smoothstep01(t);
            const float bulge = std::sin(kPi * clamp01(t));
            const float y = 0.38f + 0.05f * bulge - 0.09f * s;
            const float z = 0.25f + 0.04f * bulge - 0.07f * s;
            return glm::vec2(y, z);
        },
        0.16f * kPi, 0.84f * kPi, 1.03f);
}

std::vector<Vertex> makeFishTailFinVertices()
{
    std::vector<Vertex> verts;
    verts.reserve(24);

    const glm::vec3 rootTop(0.0f, 0.11f, 0.0f);
    const glm::vec3 rootBottom(0.0f, -0.11f, 0.0f);
    const glm::vec3 notch(-0.16f, 0.0f, 0.0f);
    const glm::vec3 tipTop(-0.82f, 0.47f, 0.0f);
    const glm::vec3 tipBottom(-0.82f, -0.47f, 0.0f);

    appendDoubleSidedTri(verts, rootTop, notch, tipTop, {1.0f, 0.65f}, {0.72f, 0.5f},
                         {0.0f, 1.0f});
    appendDoubleSidedTri(verts, rootBottom, tipBottom, notch, {1.0f, 0.35f}, {0.0f, 0.0f},
                         {0.72f, 0.5f});
    return verts;
}

std::vector<Vertex> makeFishDetailFinVertices()
{
    std::vector<Vertex> verts;
    verts.reserve(12);

    const glm::vec3 rootFront(0.02f, 0.0f, 0.0f);
    const glm::vec3 rootBack(-0.10f, 0.0f, 0.0f);
    const glm::vec3 tip(-0.46f, 0.34f, 0.0f);
    const glm::vec3 trailing(-0.32f, 0.08f, 0.0f);

    appendDoubleSidedTri(verts, rootFront, rootBack, tip, {1.0f, 0.45f}, {0.82f, 0.45f},
                         {0.0f, 1.0f});
    appendDoubleSidedTri(verts, rootBack, trailing, tip, {0.82f, 0.45f}, {0.38f, 0.2f},
                         {0.0f, 1.0f});
    return verts;
}

std::vector<Vertex> makeFishEyeVertices()
{
    constexpr int kSegments = 12;
    constexpr float kRadiusX = 0.06f;
    constexpr float kRadiusY = 0.048f;

    std::vector<Vertex> verts;
    verts.reserve(static_cast<size_t>(kSegments) * 6);

    const glm::vec3 center(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < kSegments; ++i)
    {
        const float a0 = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(kSegments);
        const float a1 =
            (2.0f * kPi * static_cast<float>(i + 1)) / static_cast<float>(kSegments);
        const glm::vec3 p0(0.0f, std::sin(a0) * kRadiusY, std::cos(a0) * kRadiusX);
        const glm::vec3 p1(0.0f, std::sin(a1) * kRadiusY, std::cos(a1) * kRadiusX);

        const glm::vec3 frontNormal(1.0f, 0.0f, 0.0f);
        verts.push_back({center, frontNormal, {0.5f, 0.5f}});
        verts.push_back({p0, frontNormal,
                         {0.5f + (p0.z / (2.0f * kRadiusX)), 0.5f + (p0.y / (2.0f * kRadiusY))}});
        verts.push_back({p1, frontNormal,
                         {0.5f + (p1.z / (2.0f * kRadiusX)), 0.5f + (p1.y / (2.0f * kRadiusY))}});

        const glm::vec3 backNormal(-1.0f, 0.0f, 0.0f);
        verts.push_back({center, backNormal, {0.5f, 0.5f}});
        verts.push_back({p1, backNormal,
                         {0.5f + (p1.z / (2.0f * kRadiusX)), 0.5f + (p1.y / (2.0f * kRadiusY))}});
        verts.push_back({p0, backNormal,
                         {0.5f + (p0.z / (2.0f * kRadiusX)), 0.5f + (p0.y / (2.0f * kRadiusY))}});
    }

    return verts;
}

void createMeshBuffer(VulkanContext& ctx, Commands& commands, const std::vector<Vertex>& vertices,
                      const std::vector<uint32_t>& indices, MeshGpu& mesh)
{
    const VkDeviceSize size = sizeof(Vertex) * vertices.size();

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    createBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);

    void* data = nullptr;
    if (vkMapMemory(ctx.device, stagingMem, 0, size, 0, &data) != VK_SUCCESS)
    {
        die("vkMapMemory failed");
    }
    std::memcpy(data, vertices.data(), static_cast<size_t>(size));
    vkUnmapMemory(ctx.device, stagingMem);

    createBuffer(ctx, size,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vbo, mesh.vboMem);
    copyBuffer(ctx, commands, staging, mesh.vbo, size);

    vkDestroyBuffer(ctx.device, staging, nullptr);
    vkFreeMemory(ctx.device, stagingMem, nullptr);

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.vboCapacityBytes = size;
    mesh.useIndex = !indices.empty();
    mesh.indexCount = static_cast<uint32_t>(indices.size());
    mesh.iboCapacityBytes = 0;

    if (indices.empty())
    {
        return;
    }

    const VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();

    VkBuffer indexStaging = VK_NULL_HANDLE;
    VkDeviceMemory indexStagingMem = VK_NULL_HANDLE;
    createBuffer(ctx, indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 indexStaging, indexStagingMem);

    void* indexData = nullptr;
    if (vkMapMemory(ctx.device, indexStagingMem, 0, indexSize, 0, &indexData) != VK_SUCCESS)
    {
        die("vkMapMemory (index) failed");
    }
    std::memcpy(indexData, indices.data(), static_cast<size_t>(indexSize));
    vkUnmapMemory(ctx.device, indexStagingMem);

    createBuffer(ctx, indexSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.ibo, mesh.iboMem);
    copyBuffer(ctx, commands, indexStaging, mesh.ibo, indexSize);

    vkDestroyBuffer(ctx.device, indexStaging, nullptr);
    vkFreeMemory(ctx.device, indexStagingMem, nullptr);

    mesh.indexCount = static_cast<uint32_t>(indices.size());
    mesh.iboCapacityBytes = indexSize;
}

void createMeshBuffer(VulkanContext& ctx, Commands& commands, const std::vector<Vertex>& vertices,
                      MeshGpu& mesh)
{
    createMeshBuffer(ctx, commands, vertices, {}, mesh);
}

void destroyMeshBuffer(VkDevice device, MeshGpu& mesh)
{
    if (mesh.vbo != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, mesh.vbo, nullptr);
    }
    if (mesh.vboMem != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, mesh.vboMem, nullptr);
    }
    if (mesh.useIndex && mesh.ibo != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, mesh.ibo, nullptr);
    }
    if (mesh.useIndex && mesh.iboMem != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, mesh.iboMem, nullptr);
    }
    mesh = MeshGpu{};
}
