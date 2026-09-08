#include "Assets/MeshLoaderOBJ.h"

#include <unordered_map>

#include "Core/Logger.h"
#include <glm/glm.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include "ThirdParty/tiny_obj_loader.h"

namespace
{
struct VertexKey
{
    glm::vec3 pos{};
    glm::vec3 normal{};
    glm::vec2 uv{};

    bool operator==(const VertexKey& other) const
    {
        return pos == other.pos && normal == other.normal && uv == other.uv;
    }
};

struct VertexKeyHash
{
    size_t operator()(const VertexKey& key) const
    {
        size_t seed = 0;
        auto hashCombine = [&seed](size_t value)
        {
            seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };

        const std::hash<float> hasher{};
        hashCombine(hasher(key.pos.x));
        hashCombine(hasher(key.pos.y));
        hashCombine(hasher(key.pos.z));
        hashCombine(hasher(key.normal.x));
        hashCombine(hasher(key.normal.y));
        hashCombine(hasher(key.normal.z));
        hashCombine(hasher(key.uv.x));
        hashCombine(hasher(key.uv.y));
        return seed;
    }
};

glm::vec3 fetchPos(const tinyobj::attrib_t& attrib, int index)
{
    const int base = index * 3;
    return glm::vec3(attrib.vertices[base + 0], attrib.vertices[base + 1],
                     attrib.vertices[base + 2]);
}

glm::vec3 fetchNormal(const tinyobj::attrib_t& attrib, int index)
{
    const int base = index * 3;
    return glm::vec3(attrib.normals[base + 0], attrib.normals[base + 1],
                     attrib.normals[base + 2]);
}

glm::vec2 fetchUV(const tinyobj::attrib_t& attrib, int index)
{
    const int base = index * 2;
    return glm::vec2(attrib.texcoords[base + 0], attrib.texcoords[base + 1]);
}
} // namespace

bool loadMeshOBJ(const std::filesystem::path& path, CpuMesh& out, std::string* error)
{
    out = CpuMesh{};
    if (!std::filesystem::exists(path))
    {
        if (error != nullptr)
        {
            *error = "OBJ not found: " + path.string();
        }
        return false;
    }

    tinyobj::attrib_t attrib{};
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    const std::string pathStr = path.string();
    const std::string baseDir = path.parent_path().string();
    const bool ok =
        tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, pathStr.c_str(),
                         baseDir.empty() ? nullptr : baseDir.c_str(), true);

    if (!warn.empty())
    {
        logWarning("Assets", std::string("OBJ load: ") + warn);
    }
    if (!ok)
    {
        if (error != nullptr)
        {
            *error = err.empty() ? "Failed to load OBJ." : err;
        }
        return false;
    }
    if (!err.empty())
    {
        logWarning("Assets", std::string("OBJ load: ") + err);
    }

    const bool hasNormals = !attrib.normals.empty();
    const bool hasUVs = !attrib.texcoords.empty();

    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> unique;
    unique.reserve(4096);

    for (const auto& shape : shapes)
    {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
        {
            const int fv = shape.mesh.num_face_vertices[f];
            if (fv < 3)
            {
                indexOffset += static_cast<size_t>(fv);
                continue;
            }

            glm::vec3 faceNormal(0.0f, 0.0f, 1.0f);
            if (!hasNormals)
            {
                const tinyobj::index_t i0 = shape.mesh.indices[indexOffset + 0];
                const tinyobj::index_t i1 = shape.mesh.indices[indexOffset + 1];
                const tinyobj::index_t i2 = shape.mesh.indices[indexOffset + 2];

                const glm::vec3 p0 = fetchPos(attrib, i0.vertex_index);
                const glm::vec3 p1 = fetchPos(attrib, i1.vertex_index);
                const glm::vec3 p2 = fetchPos(attrib, i2.vertex_index);
                const glm::vec3 e1 = p1 - p0;
                const glm::vec3 e2 = p2 - p0;
                const glm::vec3 n = glm::cross(e1, e2);
                const float lenSq = glm::dot(n, n);
                if (lenSq > 0.000001f)
                {
                    faceNormal = glm::normalize(n);
                }
            }

            for (int v = 0; v < fv; v++)
            {
                const tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                const glm::vec3 pos = fetchPos(attrib, idx.vertex_index);
                glm::vec3 normal = faceNormal;
                if (hasNormals && idx.normal_index >= 0)
                {
                    normal = fetchNormal(attrib, idx.normal_index);
                }

                glm::vec2 uv(0.0f, 0.0f);
                if (hasUVs && idx.texcoord_index >= 0)
                {
                    uv = fetchUV(attrib, idx.texcoord_index);
                }

                VertexKey key{pos, normal, uv};
                auto it = unique.find(key);
                if (it == unique.end())
                {
                    const uint32_t newIndex = static_cast<uint32_t>(out.vertices.size());
                    out.vertices.push_back(Vertex{pos, normal, uv});
                    unique.emplace(key, newIndex);
                    out.indices.push_back(newIndex);
                }
                else
                {
                    out.indices.push_back(it->second);
                }
            }

            indexOffset += static_cast<size_t>(fv);
        }
    }

    return !out.vertices.empty();
}
