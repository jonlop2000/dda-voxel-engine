#include "engine/voxel/VoxelMesher.h"

#include <array>

namespace
{
struct FaceDesc
{
    glm::ivec3 dir;
    glm::vec3 normal;
    std::array<glm::vec3, 4> corners;
};

const std::array<glm::vec2, 4> kFaceUvs = {
    glm::vec2(0.0f, 0.0f),
    glm::vec2(1.0f, 0.0f),
    glm::vec2(1.0f, 1.0f),
    glm::vec2(0.0f, 1.0f),
};

const std::array<FaceDesc, 6> kFaces = {
    FaceDesc{{1, 0, 0}, {1.0f, 0.0f, 0.0f},
             {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
              glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f)}},
    FaceDesc{{-1, 0, 0}, {-1.0f, 0.0f, 0.0f},
             {glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f),
              glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)}},
    FaceDesc{{0, 1, 0}, {0.0f, 1.0f, 0.0f},
             {glm::vec3(0.0f, 1.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f),
              glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)}},
    FaceDesc{{0, -1, 0}, {0.0f, -1.0f, 0.0f},
             {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
              glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f)}},
    FaceDesc{{0, 0, 1}, {0.0f, 0.0f, 1.0f},
             {glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f),
              glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f)}},
    FaceDesc{{0, 0, -1}, {0.0f, 0.0f, -1.0f},
             {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f),
              glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)}},
};
} // namespace

void meshChunk(const Chunk& chunk, std::array<std::vector<Vertex>, kBlockTypeCount>& out)
{
    for (auto& bucket : out)
    {
        bucket.clear();
    }

    const glm::vec3 chunkOrigin(static_cast<float>(chunk.coord.x * Chunk::SX),
                                static_cast<float>(chunk.coord.y * Chunk::SY),
                                static_cast<float>(chunk.coord.z * Chunk::SZ));

    for (int y = 0; y < Chunk::SY; ++y)
    {
        for (int z = 0; z < Chunk::SZ; ++z)
        {
            for (int x = 0; x < Chunk::SX; ++x)
            {
                const BlockId id = chunk.get(x, y, z);
                if (id == BLOCK_AIR || id >= kBlockTypeCount)
                {
                    continue;
                }

                auto& verts = out[static_cast<size_t>(id)];
                const glm::vec3 base =
                    chunkOrigin + glm::vec3(static_cast<float>(x), static_cast<float>(y),
                                            static_cast<float>(z));

                for (const auto& face : kFaces)
                {
                    const int nx = x + face.dir.x;
                    const int ny = y + face.dir.y;
                    const int nz = z + face.dir.z;
                    if (chunk.get(nx, ny, nz) != BLOCK_AIR)
                    {
                        continue;
                    }

                    Vertex v0{base + face.corners[0], face.normal, kFaceUvs[0]};
                    Vertex v1{base + face.corners[1], face.normal, kFaceUvs[1]};
                    Vertex v2{base + face.corners[2], face.normal, kFaceUvs[2]};
                    Vertex v3{base + face.corners[3], face.normal, kFaceUvs[3]};

                    verts.push_back(v0);
                    verts.push_back(v1);
                    verts.push_back(v2);
                    verts.push_back(v0);
                    verts.push_back(v2);
                    verts.push_back(v3);
                }
            }
        }
    }
}
