#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "engine/render/Frustum.h"
#include "engine/render/RenderObject.h"

namespace engine::scene
{

struct ChunkBoundsInputs
{
    std::span<const glm::ivec3> chunkCoords{};
    glm::ivec3 chunkDimensions{1};
    MeshGpu* mesh = nullptr;
    Material* material = nullptr;
};

struct VolumeBoundsInstance
{
    bool visible = false;
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
    glm::ivec3 dimensions{1};
    glm::mat4 worldFromLocal{1.0f};
};

struct VolumeBoundsInputs
{
    std::span<const VolumeBoundsInstance> instances{};
    Frustum frustum{};
    MeshGpu* mesh = nullptr;
    Material* material = nullptr;
};

struct SceneObjectBuilderInputs
{
    std::span<const RenderObject> staticObjects{};
    std::span<const RenderObject> animatedObjects{};
    std::span<const RenderObject> waterFoamObjects{};
    std::span<const RenderObject> voxelObjects{};
    std::span<const RenderObject> voxelShadowObjects{};
    std::span<const RenderObject> chunkBoundsVisibleObjects{};
    std::span<const RenderObject> volumeBoundsVisibleObjects{};
    bool voxelVisible = false;
    bool chunkBoundsVisible = false;
    bool volumeBoundsVisible = false;
    std::size_t overlayReserveHint = 0;
};

class SceneObjectBuilder
{
public:
    static void buildBaseDrawLists(const SceneObjectBuilderInputs& inputs,
                                   std::vector<RenderObject>& sceneObjects,
                                   std::vector<RenderObject>& shadowObjects);

    static void buildChunkBoundsObjects(const ChunkBoundsInputs& inputs,
                                        std::vector<RenderObject>& outObjects);

    static void buildVolumeBoundsObjects(const VolumeBoundsInputs& inputs,
                                         std::vector<RenderObject>& outObjects);
};

} // namespace engine::scene
