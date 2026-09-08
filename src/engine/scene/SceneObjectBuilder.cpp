#include "engine/scene/SceneObjectBuilder.h"

#include <glm/gtc/matrix_transform.hpp>

namespace engine::scene
{
namespace
{

void appendObjects(std::vector<RenderObject>& out,
                   std::span<const RenderObject> objects)
{
    out.insert(out.end(), objects.begin(), objects.end());
}

} // namespace

void SceneObjectBuilder::buildBaseDrawLists(
    const SceneObjectBuilderInputs& inputs,
    std::vector<RenderObject>& sceneObjects,
    std::vector<RenderObject>& shadowObjects)
{
    sceneObjects.clear();
    shadowObjects.clear();

    const std::size_t voxelCount =
        inputs.voxelVisible ? inputs.voxelObjects.size() : 0;
    const std::size_t boundsCount =
        inputs.chunkBoundsVisible ? inputs.chunkBoundsVisibleObjects.size() : 0;
    const std::size_t volumeBoundsCount =
        inputs.volumeBoundsVisible ? inputs.volumeBoundsVisibleObjects.size() : 0;

    sceneObjects.reserve(inputs.staticObjects.size() + inputs.animatedObjects.size() +
                         inputs.waterFoamObjects.size() + voxelCount + boundsCount +
                         volumeBoundsCount + inputs.overlayReserveHint);
    shadowObjects.reserve(inputs.staticObjects.size() + inputs.animatedObjects.size() +
                          inputs.voxelShadowObjects.size());

    appendObjects(sceneObjects, inputs.staticObjects);
    appendObjects(shadowObjects, inputs.staticObjects);
    appendObjects(sceneObjects, inputs.animatedObjects);
    appendObjects(shadowObjects, inputs.animatedObjects);
    appendObjects(sceneObjects, inputs.waterFoamObjects);

    if (inputs.voxelVisible)
    {
        appendObjects(sceneObjects, inputs.voxelObjects);
        appendObjects(shadowObjects, inputs.voxelShadowObjects);
    }
    if (inputs.chunkBoundsVisible)
    {
        appendObjects(sceneObjects, inputs.chunkBoundsVisibleObjects);
    }
    if (inputs.volumeBoundsVisible)
    {
        appendObjects(sceneObjects, inputs.volumeBoundsVisibleObjects);
    }
}

void SceneObjectBuilder::buildChunkBoundsObjects(
    const ChunkBoundsInputs& inputs, std::vector<RenderObject>& outObjects)
{
    outObjects.clear();
    if (inputs.mesh == nullptr || inputs.material == nullptr)
    {
        return;
    }

    const glm::mat4 scaleMat =
        glm::scale(glm::mat4(1.0f), glm::vec3(inputs.chunkDimensions));

    outObjects.reserve(inputs.chunkCoords.size());
    for (const glm::ivec3& coord : inputs.chunkCoords)
    {
        const glm::vec3 origin(coord * inputs.chunkDimensions);
        RenderObject obj{};
        obj.model = glm::translate(glm::mat4(1.0f), origin) * scaleMat;
        obj.mesh = inputs.mesh;
        obj.material = inputs.material;
        outObjects.push_back(obj);
    }
}

void SceneObjectBuilder::buildVolumeBoundsObjects(
    const VolumeBoundsInputs& inputs, std::vector<RenderObject>& outObjects)
{
    outObjects.clear();
    if (inputs.mesh == nullptr || inputs.material == nullptr)
    {
        return;
    }

    outObjects.reserve(inputs.instances.size());
    for (const VolumeBoundsInstance& inst : inputs.instances)
    {
        if (!inst.visible)
        {
            continue;
        }
        if (!aabbInFrustum(inputs.frustum, inst.aabbMin, inst.aabbMax))
        {
            continue;
        }

        const glm::mat4 scaleMat =
            glm::scale(glm::mat4(1.0f), glm::vec3(inst.dimensions));
        RenderObject obj{};
        obj.model = inst.worldFromLocal * scaleMat;
        obj.mesh = inputs.mesh;
        obj.material = inputs.material;
        outObjects.push_back(obj);
    }
}

} // namespace engine::scene
