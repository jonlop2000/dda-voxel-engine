#include "engine/voxel/GreedyMesher.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace
{
struct MaskCell
{
    BlockId id = BLOCK_AIR;
    uint8_t side = 0;
    bool valid = false;
};

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
} // namespace

VoxelAccessor::VoxelAccessor(const ChunkGrid& gridIn)
    : grid(&gridIn), lock(gridIn.lockShared())
{
}

BlockId VoxelAccessor::getVoxelWorld(int wx, int wy, int wz) const
{
    if (grid == nullptr)
    {
        return BLOCK_AIR;
    }
    return grid->getVoxelWorld(wx, wy, wz);
}

void buildGreedyMesh(const VoxelAccessor& accessor, const Chunk& chunk,
                     std::array<std::vector<Vertex>, kBlockTypeCount>& vertices,
                     std::array<std::vector<uint32_t>, kBlockTypeCount>& indices)
{
    for (auto& bucket : vertices)
    {
        bucket.clear();
    }
    for (auto& bucket : indices)
    {
        bucket.clear();
    }

    const std::array<int, 3> dims = {Chunk::SX, Chunk::SY, Chunk::SZ};
    const glm::vec3 chunkOrigin(static_cast<float>(chunk.coord.x * Chunk::SX),
                                static_cast<float>(chunk.coord.y * Chunk::SY),
                                static_cast<float>(chunk.coord.z * Chunk::SZ));

    std::vector<MaskCell> mask{};
    mask.resize(static_cast<size_t>(dims[1] * dims[2]));

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

                    const int aWorldX = chunk.coord.x * Chunk::SX + aCoords[0];
                    const int aWorldY = chunk.coord.y * Chunk::SY + aCoords[1];
                    const int aWorldZ = chunk.coord.z * Chunk::SZ + aCoords[2];

                    const int bWorldX = chunk.coord.x * Chunk::SX + bCoords[0];
                    const int bWorldY = chunk.coord.y * Chunk::SY + bCoords[1];
                    const int bWorldZ = chunk.coord.z * Chunk::SZ + bCoords[2];

                    const BlockId a = accessor.getVoxelWorld(aWorldX, aWorldY, aWorldZ);
                    const BlockId b = accessor.getVoxelWorld(bWorldX, bWorldY, bWorldZ);

                    MaskCell cell{};
                    if (a != BLOCK_AIR && b == BLOCK_AIR)
                    {
                        cell.id = a;
                        cell.side = static_cast<uint8_t>(d * 2);
                        cell.valid = true;
                    }
                    else if (a == BLOCK_AIR && b != BLOCK_AIR)
                    {
                        cell.id = b;
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
                    MaskCell cell = mask[static_cast<size_t>(i + j * du)];
                    if (!cell.valid || cell.id == BLOCK_AIR || cell.id >= kBlockTypeCount)
                    {
                        ++i;
                        continue;
                    }

                    int width = 1;
                    while (i + width < du)
                    {
                        const MaskCell next = mask[static_cast<size_t>(i + width + j * du)];
                        if (!next.valid || next.id != cell.id || next.side != cell.side)
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
                            if (!next.valid || next.id != cell.id || next.side != cell.side)
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

                    const glm::vec3 origin = chunkOrigin + base;
                    const glm::vec3 normal = normalForSide(cell.side);

                    const glm::vec2 uv0(0.0f, 0.0f);
                    const glm::vec2 uv1(static_cast<float>(width), 0.0f);
                    const glm::vec2 uv2(static_cast<float>(width), static_cast<float>(height));
                    const glm::vec2 uv3(0.0f, static_cast<float>(height));

                    auto& vertBucket = vertices[static_cast<size_t>(cell.id)];
                    auto& idxBucket = indices[static_cast<size_t>(cell.id)];
                    const uint32_t baseIndex = static_cast<uint32_t>(vertBucket.size());

                    vertBucket.push_back(Vertex{origin, normal, uv0});
                    vertBucket.push_back(Vertex{origin + duVec, normal, uv1});
                    vertBucket.push_back(Vertex{origin + duVec + dvVec, normal, uv2});
                    vertBucket.push_back(Vertex{origin + dvVec, normal, uv3});

                    idxBucket.push_back(baseIndex + 0);
                    idxBucket.push_back(baseIndex + 1);
                    idxBucket.push_back(baseIndex + 2);
                    idxBucket.push_back(baseIndex + 0);
                    idxBucket.push_back(baseIndex + 2);
                    idxBucket.push_back(baseIndex + 3);

                    i += width;
                }
            }
        }
    }
}
