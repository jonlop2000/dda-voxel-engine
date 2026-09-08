#include "engine/scene/VoxelImportScene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <vector>

#include "Core/Logger.h"
#include "Assets/MeshLoaderOBJ.h"
#include "engine/render/VulkanContext.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelWorld.h"

namespace
{
constexpr uint8_t kMatImport = 11;

engine::PaletteEntryCPU makeEntry(const glm::vec3& color, float roughness, float metallic)
{
    engine::PaletteEntryCPU entry{};
    entry.baseColor_alpha = glm::vec4(color, 1.0f);
    entry.pbr0 = glm::vec4(metallic, roughness, 0.0f, 0.0f);
    entry.extra = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    return entry;
}

glm::vec3 minVec3(const glm::vec3& a, const glm::vec3& b)
{
    return glm::vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
}

glm::vec3 maxVec3(const glm::vec3& a, const glm::vec3& b)
{
    return glm::vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
}

bool planeBoxOverlap(const glm::vec3& normal, const glm::vec3& vert,
                     const glm::vec3& maxBox)
{
    glm::vec3 vmin;
    glm::vec3 vmax;
    if (normal.x > 0.0f)
    {
        vmin.x = -maxBox.x - vert.x;
        vmax.x = maxBox.x - vert.x;
    }
    else
    {
        vmin.x = maxBox.x - vert.x;
        vmax.x = -maxBox.x - vert.x;
    }

    if (normal.y > 0.0f)
    {
        vmin.y = -maxBox.y - vert.y;
        vmax.y = maxBox.y - vert.y;
    }
    else
    {
        vmin.y = maxBox.y - vert.y;
        vmax.y = -maxBox.y - vert.y;
    }

    if (normal.z > 0.0f)
    {
        vmin.z = -maxBox.z - vert.z;
        vmax.z = maxBox.z - vert.z;
    }
    else
    {
        vmin.z = maxBox.z - vert.z;
        vmax.z = -maxBox.z - vert.z;
    }

    if (glm::dot(normal, vmin) > 0.0f)
    {
        return false;
    }
    if (glm::dot(normal, vmax) >= 0.0f)
    {
        return true;
    }
    return false;
}

bool triBoxOverlap(const glm::vec3& boxCenter, const glm::vec3& boxHalf,
                   const glm::vec3& v0In, const glm::vec3& v1In, const glm::vec3& v2In)
{
    glm::vec3 v0 = v0In - boxCenter;
    glm::vec3 v1 = v1In - boxCenter;
    glm::vec3 v2 = v2In - boxCenter;

    const glm::vec3 e0 = v1 - v0;
    const glm::vec3 e1 = v2 - v1;
    const glm::vec3 e2 = v0 - v2;

    const float fex = std::fabs(e0.x);
    const float fey = std::fabs(e0.y);
    const float fez = std::fabs(e0.z);

    float p0 = e0.z * v0.y - e0.y * v0.z;
    float p2 = e0.z * v2.y - e0.y * v2.z;
    float min = std::min(p0, p2);
    float max = std::max(p0, p2);
    float rad = fez * boxHalf.y + fey * boxHalf.z;
    if (min > rad || max < -rad)
    {
        return false;
    }

    p0 = -e0.z * v0.x + e0.x * v0.z;
    p2 = -e0.z * v2.x + e0.x * v2.z;
    min = std::min(p0, p2);
    max = std::max(p0, p2);
    rad = fez * boxHalf.x + fex * boxHalf.z;
    if (min > rad || max < -rad)
    {
        return false;
    }

    float p1 = e0.y * v1.x - e0.x * v1.y;
    p2 = e0.y * v2.x - e0.x * v2.y;
    min = std::min(p1, p2);
    max = std::max(p1, p2);
    rad = fey * boxHalf.x + fex * boxHalf.y;
    if (min > rad || max < -rad)
    {
        return false;
    }

    const float fex1 = std::fabs(e1.x);
    const float fey1 = std::fabs(e1.y);
    const float fez1 = std::fabs(e1.z);

    p0 = e1.z * v0.y - e1.y * v0.z;
    p2 = e1.z * v2.y - e1.y * v2.z;
    min = std::min(p0, p2);
    max = std::max(p0, p2);
    rad = fez1 * boxHalf.y + fey1 * boxHalf.z;
    if (min > rad || max < -rad)
    {
        return false;
    }

    p0 = -e1.z * v0.x + e1.x * v0.z;
    p2 = -e1.z * v2.x + e1.x * v2.z;
    min = std::min(p0, p2);
    max = std::max(p0, p2);
    rad = fez1 * boxHalf.x + fex1 * boxHalf.z;
    if (min > rad || max < -rad)
    {
        return false;
    }

    p0 = e1.y * v0.x - e1.x * v0.y;
    p1 = e1.y * v1.x - e1.x * v1.y;
    min = std::min(p0, p1);
    max = std::max(p0, p1);
    rad = fey1 * boxHalf.x + fex1 * boxHalf.y;
    if (min > rad || max < -rad)
    {
        return false;
    }

    const float fex2 = std::fabs(e2.x);
    const float fey2 = std::fabs(e2.y);
    const float fez2 = std::fabs(e2.z);

    p0 = e2.z * v0.y - e2.y * v0.z;
    p1 = e2.z * v1.y - e2.y * v1.z;
    min = std::min(p0, p1);
    max = std::max(p0, p1);
    rad = fez2 * boxHalf.y + fey2 * boxHalf.z;
    if (min > rad || max < -rad)
    {
        return false;
    }

    p0 = -e2.z * v0.x + e2.x * v0.z;
    p1 = -e2.z * v1.x + e2.x * v1.z;
    min = std::min(p0, p1);
    max = std::max(p0, p1);
    rad = fez2 * boxHalf.x + fex2 * boxHalf.z;
    if (min > rad || max < -rad)
    {
        return false;
    }

    p1 = e2.y * v1.x - e2.x * v1.y;
    p2 = e2.y * v2.x - e2.x * v2.y;
    min = std::min(p1, p2);
    max = std::max(p1, p2);
    rad = fey2 * boxHalf.x + fex2 * boxHalf.y;
    if (min > rad || max < -rad)
    {
        return false;
    }

    const glm::vec3 minV = minVec3(v0, minVec3(v1, v2));
    const glm::vec3 maxV = maxVec3(v0, maxVec3(v1, v2));
    if (minV.x > boxHalf.x || maxV.x < -boxHalf.x)
    {
        return false;
    }
    if (minV.y > boxHalf.y || maxV.y < -boxHalf.y)
    {
        return false;
    }
    if (minV.z > boxHalf.z || maxV.z < -boxHalf.z)
    {
        return false;
    }

    const glm::vec3 normal = glm::cross(e0, e1);
    if (!planeBoxOverlap(normal, v0, boxHalf))
    {
        return false;
    }

    return true;
}

void floodFillInterior(std::vector<uint8_t>& data, int res, uint8_t fillId)
{
    const size_t total = static_cast<size_t>(res) * res * res;
    std::vector<uint8_t> visited(total, 0);

    auto idx3 = [&](int x, int y, int z) -> size_t
    {
        return static_cast<size_t>(x) + static_cast<size_t>(y) * res +
               static_cast<size_t>(z) * res * res;
    };

    std::vector<glm::ivec3> stack;
    stack.reserve(total / 8);

    auto pushIfEmpty = [&](int x, int y, int z)
    {
        const size_t idx = idx3(x, y, z);
        if (data[idx] == 0 && visited[idx] == 0)
        {
            visited[idx] = 1;
            stack.emplace_back(x, y, z);
        }
    };

    for (int z = 0; z < res; ++z)
    {
        for (int y = 0; y < res; ++y)
        {
            pushIfEmpty(0, y, z);
            pushIfEmpty(res - 1, y, z);
        }
    }

    for (int z = 0; z < res; ++z)
    {
        for (int x = 0; x < res; ++x)
        {
            pushIfEmpty(x, 0, z);
            pushIfEmpty(x, res - 1, z);
        }
    }

    for (int y = 0; y < res; ++y)
    {
        for (int x = 0; x < res; ++x)
        {
            pushIfEmpty(x, y, 0);
            pushIfEmpty(x, y, res - 1);
        }
    }

    const glm::ivec3 dirs[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    while (!stack.empty())
    {
        const glm::ivec3 p = stack.back();
        stack.pop_back();
        for (const auto& d : dirs)
        {
            const glm::ivec3 n = p + d;
            if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= res || n.y >= res || n.z >= res)
            {
                continue;
            }
            const size_t idx = idx3(n.x, n.y, n.z);
            if (data[idx] == 0 && visited[idx] == 0)
            {
                visited[idx] = 1;
                stack.push_back(n);
            }
        }
    }

    for (int z = 0; z < res; ++z)
    {
        for (int y = 0; y < res; ++y)
        {
            for (int x = 0; x < res; ++x)
            {
                const size_t idx = idx3(x, y, z);
                if (data[idx] == 0 && visited[idx] == 0)
                {
                    data[idx] = fillId;
                }
            }
        }
    }
}

} // namespace

bool VoxelImportScene::init(VulkanContext& ctx, engine::VoxelWorld& world,
                            engine::VoxelPalette& palette, const VoxelImportSettings& settings,
                            VoxelImportStats* outStats)
{
    if (outStats != nullptr)
    {
        *outStats = {};
    }

    if (settings.meshPath.empty())
    {
        logError("VoxelImportScene", "Mesh path is empty.");
        return false;
    }

    CpuMesh mesh{};
    std::string err;
    if (!loadMeshOBJ(settings.meshPath, mesh, &err))
    {
        logError("VoxelImportScene", makeLogMessage("Mesh load failed: ", err));
        return false;
    }

    palette.buildDefaultPalette0();
    palette.setEntry(0, kMatImport, makeEntry(glm::vec3(0.85f, 0.85f, 0.9f), 0.4f, 0.15f));
    if (!palette.upload(ctx))
    {
        logError("VoxelImportScene", "Failed to upload import palette.");
        return false;
    }

    const auto start = std::chrono::steady_clock::now();

    glm::vec3 minP(FLT_MAX);
    glm::vec3 maxP(-FLT_MAX);
    for (const auto& v : mesh.vertices)
    {
        minP = minVec3(minP, v.pos);
        maxP = maxVec3(maxP, v.pos);
    }

    const glm::vec3 size = maxP - minP;
    const float maxExtent = std::max(size.x, std::max(size.y, size.z));
    if (maxExtent <= 0.0001f)
    {
        logError("VoxelImportScene", "Mesh bounds are invalid.");
        return false;
    }

    const int res = std::max(8, settings.resolution);
    const float scale = (static_cast<float>(res) - 2.0f) / maxExtent;
    const glm::vec3 center = (minP + maxP) * 0.5f;
    const glm::vec3 offset = glm::vec3(res) * 0.5f - center * scale;

    std::vector<glm::vec3> voxelVerts;
    voxelVerts.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices)
    {
        voxelVerts.push_back(v.pos * scale + offset);
    }

    std::vector<uint8_t> voxels(static_cast<size_t>(res) * res * res, 0);
    auto idx3 = [&](int x, int y, int z) -> size_t
    {
        return static_cast<size_t>(x) + static_cast<size_t>(y) * res +
               static_cast<size_t>(z) * res * res;
    };

    const std::vector<uint32_t>& indices = mesh.indices;
    const bool indexed = !indices.empty();
    const size_t triCount = indexed ? indices.size() / 3 : mesh.vertices.size() / 3;

    for (size_t t = 0; t < triCount; ++t)
    {
        const uint32_t i0 = indexed ? indices[t * 3 + 0] : static_cast<uint32_t>(t * 3 + 0);
        const uint32_t i1 = indexed ? indices[t * 3 + 1] : static_cast<uint32_t>(t * 3 + 1);
        const uint32_t i2 = indexed ? indices[t * 3 + 2] : static_cast<uint32_t>(t * 3 + 2);

        const glm::vec3 v0 = voxelVerts[i0];
        const glm::vec3 v1 = voxelVerts[i1];
        const glm::vec3 v2 = voxelVerts[i2];

        const glm::vec3 triMin = minVec3(v0, minVec3(v1, v2));
        const glm::vec3 triMax = maxVec3(v0, maxVec3(v1, v2));

        const int x0 = std::max(0, static_cast<int>(std::floor(triMin.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(triMin.y)));
        const int z0 = std::max(0, static_cast<int>(std::floor(triMin.z)));
        const int x1 = std::min(res - 1, static_cast<int>(std::ceil(triMax.x)));
        const int y1 = std::min(res - 1, static_cast<int>(std::ceil(triMax.y)));
        const int z1 = std::min(res - 1, static_cast<int>(std::ceil(triMax.z)));

        for (int z = z0; z <= z1; ++z)
        {
            for (int y = y0; y <= y1; ++y)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    const glm::vec3 boxCenter(x + 0.5f, y + 0.5f, z + 0.5f);
                    const glm::vec3 boxHalf(0.5f);
                    if (triBoxOverlap(boxCenter, boxHalf, v0, v1, v2))
                    {
                        voxels[idx3(x, y, z)] = kMatImport;
                    }
                }
            }
        }
    }

    floodFillInterior(voxels, res, kMatImport);

    uint32_t filled = 0;
    for (uint8_t v : voxels)
    {
        if (v != 0)
        {
            ++filled;
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (outStats != nullptr)
    {
        outStats->triangleCount = static_cast<uint32_t>(triCount);
        outStats->filledVoxels = filled;
        outStats->voxelizeMs = ms;
    }

    const glm::vec3 basePos(-0.5f * static_cast<float>(res), 0.0f,
                            -0.5f * static_cast<float>(res));

    std::vector<engine::VolumeSpec> specs;
    std::vector<std::vector<uint8_t>> data;

    if (settings.splitIntoChunks)
    {
        const int chunkRes = res / 2;
        const glm::ivec3 chunkDims(chunkRes);
        specs.reserve(8);
        data.reserve(8);

        for (int z = 0; z < 2; ++z)
        {
            for (int y = 0; y < 2; ++y)
            {
                for (int x = 0; x < 2; ++x)
                {
                    engine::VolumeSpec spec{};
                    spec.name = "Chunk(" + std::to_string(x) + "," + std::to_string(y) + "," +
                                std::to_string(z) + ")";
                    spec.dims = chunkDims;
                    spec.position =
                        basePos + glm::vec3(x * chunkRes, y * chunkRes, z * chunkRes);
                    spec.flags = engine::VoxelVolume::FLAG_STATIC;
                    specs.push_back(spec);

                    std::vector<uint8_t> chunk(static_cast<size_t>(chunkRes) * chunkRes *
                                                   chunkRes,
                                               0);
                    auto idxChunk = [&](int lx, int ly, int lz) -> size_t
                    {
                        return static_cast<size_t>(lx) + static_cast<size_t>(ly) * chunkRes +
                               static_cast<size_t>(lz) * chunkRes * chunkRes;
                    };

                    for (int lz = 0; lz < chunkRes; ++lz)
                    {
                        for (int ly = 0; ly < chunkRes; ++ly)
                        {
                            for (int lx = 0; lx < chunkRes; ++lx)
                            {
                                const int gx = x * chunkRes + lx;
                                const int gy = y * chunkRes + ly;
                                const int gz = z * chunkRes + lz;
                                chunk[idxChunk(lx, ly, lz)] = voxels[idx3(gx, gy, gz)];
                            }
                        }
                    }
                    data.push_back(std::move(chunk));
                }
            }
        }
    }
    else
    {
        engine::VolumeSpec spec{};
        spec.name = "ImportedMesh";
        spec.dims = glm::ivec3(res);
        spec.position = basePos;
        spec.flags = engine::VoxelVolume::FLAG_STATIC;
        specs.push_back(spec);
        data.push_back(std::move(voxels));
    }

    if (!world.initAquariumScene(ctx, palette, specs.size(),
                                 [&](size_t index) -> const engine::VolumeSpec& {
                                     return specs[index];
                                 },
                                 [&](size_t index) -> const std::vector<uint8_t>& {
                                     return data[index];
                                 }))
    {
        logError("VoxelImportScene", "Failed to initialize voxel import volumes.");
        return false;
    }

    logInfo("VoxelImportScene",
            makeLogMessage("Voxelized ", triCount, " triangles in ", ms, " ms"));
    return true;
}
