#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "Resources/Mesh.h"
#include "engine/render/AuxiliaryRayResolution.h"
#include "engine/render/voxel/VoxelLightingOcclusionPolicy.h"
#include "engine/voxel/VoxelTypes.h"

class ChunkGrid;
class MeshUploadQueue;
struct Commands;
struct VulkanContext;

namespace engine
{
struct VoxelInstance;
class VoxelWorld;
}

namespace engine::render
{

class VoxelRenderResources
{
public:
    struct ChunkRender
    {
        glm::ivec3 coord{0};
        std::array<MeshGpu, kBlockTypeCount> meshes{};
    };

    struct PendingMeshApplyResult
    {
        bool changed = false;
        uint32_t rebuiltChunks = 0;
    };

    struct VolumeFilter
    {
        using AcceptFn = bool (*)(void* user, uint32_t volumeIndex,
                                  const engine::VoxelInstance& instance);

        void* user = nullptr;
        AcceptFn accept = nullptr;

        bool accepts(uint32_t volumeIndex, const engine::VoxelInstance& instance) const
        {
            return accept == nullptr || accept(user, volumeIndex, instance);
        }
    };

    struct VolumeDrawPlan
    {
        struct Entry
        {
            uint32_t volumeIndex = 0;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
            bool opaqueOnlyDda = false;
            bool unwrappedOpaqueDda = false;
            bool fullScreenCoverageRequired = false;
            bool cameraInsideRasterBounds = false;
            bool faceCullingSafe = false;
            bool frontFaceWindingReversed = false;
            bool sharesAlignedTraversalWithNext = false;
            bool sharedFullScreenCoverageRequired = false;
            bool sharedCameraInsideRasterBounds = false;
            bool sharedFaceCullingSafe = false;
            bool sharedFrontFaceWindingReversed = false;
            bool terrainShadowColumns = false;
        };

        std::vector<Entry> mainDdaVolumes{};
        std::vector<Entry> staticVolumes{};
        std::vector<Entry> sunShadowOccluderVolumes{};
        std::vector<Entry> localLightingOccluderVolumes{};
        std::vector<Entry> localShadowOccluderVolumes{};
        uint32_t alignedPrimaryTraversalPairCount = 0;
        uint32_t terrainShadowColumnVolumeCount = 0;
        uint32_t cameraInsideVolumeCount = 0;
        float projectedScreenCoverage = 0.0f;
    };

    struct VolumeDrawPlanOptions
    {
        std::span<const LocalShadowLightInfluence> localShadowLights{};
        // opts color-replaced content into sun shadows only. it must not leak the
        // replacement proxy into primary dda, planar reflections, ao, or local light.
        VolumeFilter supplementalSunShadowFilter{};
        glm::mat4 viewProjection{1.0f};
        float nearClipGuardRadiusWorld = 0.0f;
        bool hasViewProjection = false;
        bool enableAlignedPrimaryTraversal = false;
    };

    void resetChunks(std::span<const glm::ivec3> chunkCoords);
    void clear();
    void destroyGpuResources(VkDevice device);

    PendingMeshApplyResult applyPendingMeshes(ChunkGrid& grid, VulkanContext& ctx,
                                              Commands& commands,
                                              MeshUploadQueue& uploadQueue,
                                              VkCommandBuffer cmd,
                                              uint32_t frameIndex);

    const VolumeDrawPlan& prepareVolumeDrawPlan(engine::VoxelWorld& world,
                                                VulkanContext& ctx,
                                                const glm::vec3& cameraPos,
                                                VolumeFilter filter,
                                                VolumeDrawPlanOptions options);

    const VolumeDrawPlan& volumeDrawPlan() const { return volumeDrawPlan_; }

    bool empty() const { return chunkRenders_.empty(); }
    size_t size() const { return chunkRenders_.size(); }
    const std::vector<ChunkRender>& chunkRenders() const { return chunkRenders_; }
    std::vector<ChunkRender>& chunkRenders() { return chunkRenders_; }

    struct MeshStats
    {
        size_t vertices = 0;
        size_t indices = 0;
        size_t meshedChunks = 0;
    };

    MeshStats meshStats() const;

private:
    std::vector<ChunkRender> chunkRenders_{};
    VolumeDrawPlan volumeDrawPlan_{};
};

}  // namespace engine::render
