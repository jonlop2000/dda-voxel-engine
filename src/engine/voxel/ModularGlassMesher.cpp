#include "engine/voxel/ModularGlassMesher.h"

#include <algorithm>
#include <array>

namespace engine
{
namespace
{
struct MaskCell
{
    uint8_t materialId = 0;
    uint8_t side = 0;
    bool valid = false;
};

bool dimensionsValid(const glm::ivec3& dims)
{
    return dims.x > 0 && dims.y > 0 && dims.z > 0;
}

size_t voxelCount(const glm::ivec3& dims)
{
    return static_cast<size_t>(dims.x) * static_cast<size_t>(dims.y) *
           static_cast<size_t>(dims.z);
}

size_t voxelIndex(const glm::ivec3& dims, int x, int y, int z)
{
    return static_cast<size_t>(x) + static_cast<size_t>(y) * static_cast<size_t>(dims.x) +
           static_cast<size_t>(z) * static_cast<size_t>(dims.x) *
               static_cast<size_t>(dims.y);
}

bool materialMatches(uint8_t id, const std::vector<uint8_t>& glassMaterialIds)
{
    return std::find(glassMaterialIds.begin(), glassMaterialIds.end(), id) !=
           glassMaterialIds.end();
}

uint8_t glassMaterialAt(const ModularGlassMeshInput& input, int x, int y, int z)
{
    if (!dimensionsValid(input.dimensions) || input.voxels == nullptr || x < 0 || y < 0 || z < 0 ||
        x >= input.dimensions.x || y >= input.dimensions.y || z >= input.dimensions.z)
    {
        return 0;
    }

    const std::vector<uint8_t>& voxels = *input.voxels;
    const uint8_t id = voxels[voxelIndex(input.dimensions, x, y, z)];
    return materialMatches(id, input.glassMaterialIds) ? id : 0;
}

glm::vec3 normalForSide(uint8_t side)
{
    switch (side)
    {
    case 0:
        return glm::vec3(1.0f, 0.0f, 0.0f);
    case 1:
        return glm::vec3(-1.0f, 0.0f, 0.0f);
    case 2:
        return glm::vec3(0.0f, 1.0f, 0.0f);
    case 3:
        return glm::vec3(0.0f, -1.0f, 0.0f);
    case 4:
        return glm::vec3(0.0f, 0.0f, 1.0f);
    default:
        return glm::vec3(0.0f, 0.0f, -1.0f);
    }
}

void appendQuad(ModularGlassMesh& mesh, const glm::vec3& origin, const glm::vec3& duVec,
                const glm::vec3& dvVec, uint8_t side, int width, int height)
{
    const glm::vec3 normal = normalForSide(side);
    const glm::vec2 uv0(0.0f, 0.0f);
    const glm::vec2 uv1(static_cast<float>(width), 0.0f);
    const glm::vec2 uv2(static_cast<float>(width), static_cast<float>(height));
    const glm::vec2 uv3(0.0f, static_cast<float>(height));

    const uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(Vertex{origin, normal, uv0});
    mesh.vertices.push_back(Vertex{origin + duVec, normal, uv1});
    mesh.vertices.push_back(Vertex{origin + duVec + dvVec, normal, uv2});
    mesh.vertices.push_back(Vertex{origin + dvVec, normal, uv3});

    if ((side & 1u) == 0u)
    {
        mesh.indices.push_back(baseIndex + 0);
        mesh.indices.push_back(baseIndex + 1);
        mesh.indices.push_back(baseIndex + 2);
        mesh.indices.push_back(baseIndex + 0);
        mesh.indices.push_back(baseIndex + 2);
        mesh.indices.push_back(baseIndex + 3);
    }
    else
    {
        mesh.indices.push_back(baseIndex + 0);
        mesh.indices.push_back(baseIndex + 3);
        mesh.indices.push_back(baseIndex + 2);
        mesh.indices.push_back(baseIndex + 0);
        mesh.indices.push_back(baseIndex + 2);
        mesh.indices.push_back(baseIndex + 1);
    }
    ++mesh.quadCount;
}
} // namespace

ModularGlassMesh buildModularGlassMesh(const ModularGlassMeshInput& input)
{
    ModularGlassMesh mesh{};
    if (!dimensionsValid(input.dimensions) || input.voxels == nullptr ||
        input.voxels->size() != voxelCount(input.dimensions) || input.glassMaterialIds.empty())
    {
        return mesh;
    }

    for (uint8_t id : *input.voxels)
    {
        if (materialMatches(id, input.glassMaterialIds))
        {
            ++mesh.occupiedVoxelCount;
        }
    }
    if (mesh.occupiedVoxelCount == 0)
    {
        return mesh;
    }

    const std::array<int, 3> dims = {input.dimensions.x, input.dimensions.y,
                                    input.dimensions.z};
    std::vector<MaskCell> mask{};

    for (int d = 0; d < 3; ++d)
    {
        const int u = (d + 1) % 3;
        const int v = (d + 2) % 3;
        const int du = dims[u];
        const int dv = dims[v];
        mask.resize(static_cast<size_t>(du * dv));

        for (int w = 0; w <= dims[d]; ++w)
        {
            for (int j = 0; j < dv; ++j)
            {
                for (int i = 0; i < du; ++i)
                {
                    int coords[3] = {0, 0, 0};
                    coords[d] = w;
                    coords[u] = i;
                    coords[v] = j;

                    int aCoords[3] = {coords[0], coords[1], coords[2]};
                    int bCoords[3] = {coords[0], coords[1], coords[2]};
                    aCoords[d] = w - 1;
                    bCoords[d] = w;

                    const uint8_t a =
                        glassMaterialAt(input, aCoords[0], aCoords[1], aCoords[2]);
                    const uint8_t b =
                        glassMaterialAt(input, bCoords[0], bCoords[1], bCoords[2]);

                    MaskCell cell{};
                    if (a != 0 && b == 0)
                    {
                        cell.materialId = a;
                        cell.side = static_cast<uint8_t>(d * 2);
                        cell.valid = true;
                    }
                    else if (a == 0 && b != 0)
                    {
                        cell.materialId = b;
                        cell.side = static_cast<uint8_t>(d * 2 + 1);
                        cell.valid = true;
                    }
                    mask[static_cast<size_t>(i + j * du)] = cell;
                }
            }

            for (int j = 0; j < dv; ++j)
            {
                int i = 0;
                while (i < du)
                {
                    const MaskCell cell = mask[static_cast<size_t>(i + j * du)];
                    if (!cell.valid)
                    {
                        ++i;
                        continue;
                    }

                    int width = 1;
                    while (i + width < du)
                    {
                        const MaskCell next = mask[static_cast<size_t>(i + width + j * du)];
                        if (!next.valid || next.materialId != cell.materialId ||
                            next.side != cell.side)
                        {
                            break;
                        }
                        ++width;
                    }

                    int height = 1;
                    bool done = false;
                    while (j + height < dv && !done)
                    {
                        for (int k = 0; k < width; ++k)
                        {
                            const MaskCell next =
                                mask[static_cast<size_t>(i + k + (j + height) * du)];
                            if (!next.valid || next.materialId != cell.materialId ||
                                next.side != cell.side)
                            {
                                done = true;
                                break;
                            }
                        }
                        if (!done)
                        {
                            ++height;
                        }
                    }

                    for (int y = 0; y < height; ++y)
                    {
                        for (int x = 0; x < width; ++x)
                        {
                            mask[static_cast<size_t>(i + x + (j + y) * du)].valid = false;
                        }
                    }

                    glm::vec3 duVec(0.0f);
                    glm::vec3 dvVec(0.0f);
                    duVec[u] = static_cast<float>(width);
                    dvVec[v] = static_cast<float>(height);

                    glm::vec3 base(0.0f);
                    base[d] = static_cast<float>(w);
                    base[u] = static_cast<float>(i);
                    base[v] = static_cast<float>(j);

                    appendQuad(mesh, base, duVec, dvVec, cell.side, width, height);
                    i += width;
                }
            }
        }
    }

    return mesh;
}

} // namespace engine
