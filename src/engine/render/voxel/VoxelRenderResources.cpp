#include "engine/render/voxel/VoxelRenderResources.h"

#include <utility>

#include "Resources/MeshUploadQueue.h"
#include "engine/render/Commands.h"
#include "engine/render/VulkanContext.h"
#include "engine/render/voxel/VoxelAlignedLayerTraversal.h"
#include "engine/render/voxel/VoxelLightingOcclusionPolicy.h"
#include "engine/render/voxel/VoxelRasterFaceSelection.h"
#include "engine/voxel/ChunkGrid.h"
#include "engine/voxel/VoxelWorld.h"
#include "engine/voxel/VoxelSystem.h"

namespace engine::render
{

void VoxelRenderResources::resetChunks(std::span<const glm::ivec3> chunkCoords)
{
    chunkRenders_.clear();
    chunkRenders_.resize(chunkCoords.size());
    for (size_t i = 0; i < chunkCoords.size(); ++i)
    {
        chunkRenders_[i].coord = chunkCoords[i];
    }
}

void VoxelRenderResources::clear()
{
    chunkRenders_.clear();
}

void VoxelRenderResources::destroyGpuResources(VkDevice device)
{
    for (ChunkRender& render : chunkRenders_)
    {
        for (MeshGpu& mesh : render.meshes)
        {
            destroyMeshBuffer(device, mesh);
        }
    }
    clear();
}

VoxelRenderResources::PendingMeshApplyResult VoxelRenderResources::applyPendingMeshes(
    ChunkGrid& grid, VulkanContext& ctx, Commands& commands, MeshUploadQueue& uploadQueue,
    VkCommandBuffer cmd, uint32_t frameIndex)
{
    PendingMeshApplyResult result{};
    std::vector<engine::voxel::PendingChunkMesh> pendingMeshes =
        engine::voxel::consumePendingChunkMeshes(grid);

    for (engine::voxel::PendingChunkMesh& pending : pendingMeshes)
    {
        if (pending.chunkIndex >= chunkRenders_.size())
        {
            continue;
        }

        ChunkRender& render = chunkRenders_[pending.chunkIndex];
        result.changed = true;
        ++result.rebuiltChunks;

        for (BlockId id = 1; id < kBlockTypeCount; ++id)
        {
            auto& vertices = pending.mesh.vertices[static_cast<size_t>(id)];
            auto& indices = pending.mesh.indices[static_cast<size_t>(id)];
            uploadQueue.enqueue(&render.meshes[id], std::move(vertices),
                                std::move(indices));
        }
    }

    uploadQueue.flush(ctx, commands, cmd, frameIndex);
    return result;
}

const VoxelRenderResources::VolumeDrawPlan& VoxelRenderResources::prepareVolumeDrawPlan(
    engine::VoxelWorld& world, VulkanContext& ctx, const glm::vec3& cameraPos,
    VolumeFilter filter, VolumeDrawPlanOptions options)
{
    world.updateGpuVolumes(ctx);
    world.buildSortedDrawList(cameraPos);

    const std::vector<engine::VoxelInstance>& instances = world.instances();
    const std::vector<uint32_t>& drawList = world.drawList();

    volumeDrawPlan_.mainDdaVolumes.clear();
    volumeDrawPlan_.staticVolumes.clear();
    volumeDrawPlan_.sunShadowOccluderVolumes.clear();
    volumeDrawPlan_.localLightingOccluderVolumes.clear();
    volumeDrawPlan_.localShadowOccluderVolumes.clear();
    volumeDrawPlan_.alignedPrimaryTraversalPairCount = 0;
    volumeDrawPlan_.terrainShadowColumnVolumeCount = 0;
    volumeDrawPlan_.cameraInsideVolumeCount = 0;
    volumeDrawPlan_.projectedScreenCoverage = 0.0f;

    volumeDrawPlan_.mainDdaVolumes.reserve(drawList.size());
    volumeDrawPlan_.staticVolumes.reserve(instances.size());
    volumeDrawPlan_.sunShadowOccluderVolumes.reserve(instances.size());
    volumeDrawPlan_.localLightingOccluderVolumes.reserve(instances.size());
    volumeDrawPlan_.localShadowOccluderVolumes.reserve(instances.size());

    ProjectedCoverageAccumulator projectedCoverage{};
    for (uint32_t volumeIndex : drawList)
    {
        if (volumeIndex >= instances.size())
        {
            continue;
        }
        const engine::VoxelInstance& instance = instances[volumeIndex];
        if (!filter.accepts(volumeIndex, instance))
        {
            continue;
        }

        const uint32_t volumeFlags = instance.volume.flags();
        const bool isStaticVolume =
            (volumeFlags & engine::VoxelVolume::FLAG_STATIC) != 0u;
        const bool opaqueOnlyDda =
            voxelVolumeQualifiesForOpaqueOnlyDda(volumeFlags);
        const bool unwrappedOpaqueDda =
            voxelVolumeQualifiesForUnwrappedOpaqueDda(volumeFlags);
        const VoxelLightingOcclusionParticipation participation =
            voxelLightingOcclusionParticipation(instance.volume.lightingOcclusionMode(),
                                                isStaticVolume);
        if (!participation.mainDda)
        {
            continue;
        }

        const VkDescriptorSet descriptorSet = world.descriptorSet(volumeIndex);
        if (descriptorSet != VK_NULL_HANDLE)
        {
            const VoxelRasterFaceSelection faceSelection =
                classifyVoxelRasterFaces(
                    instance.volume.localFromWorld(),
                    instance.volume.worldFromLocal(),
                    (volumeFlags & engine::VoxelVolume::FLAG_WRAP_XZ) != 0u
                        ? instance.volume.localMin()
                        : glm::vec3(instance.volume.occupiedLocalMin()),
                    (volumeFlags & engine::VoxelVolume::FLAG_WRAP_XZ) != 0u
                        ? instance.volume.localMax()
                        : glm::vec3(instance.volume.occupiedLocalMaxExclusive()),
                    cameraPos, options.nearClipGuardRadiusWorld);
            volumeDrawPlan_.mainDdaVolumes.push_back(
                {volumeIndex, descriptorSet, opaqueOnlyDda,
                 unwrappedOpaqueDda, faceSelection.fullScreenCoverageRequired,
                 faceSelection.cameraInside,
                 faceSelection.safe, faceSelection.windingReversed});
            if (faceSelection.cameraInside)
            {
                ++volumeDrawPlan_.cameraInsideVolumeCount;
            }
            if (options.hasViewProjection)
            {
                const glm::vec3 boundsMin =
                    (volumeFlags & engine::VoxelVolume::FLAG_WRAP_XZ) != 0u
                        ? instance.volume.localMin()
                        : glm::vec3(instance.volume.occupiedLocalMin());
                const glm::vec3 boundsMax =
                    (volumeFlags & engine::VoxelVolume::FLAG_WRAP_XZ) != 0u
                        ? instance.volume.localMax()
                        : glm::vec3(instance.volume.occupiedLocalMaxExclusive());
                projectedCoverage.includeBounds(
                    options.viewProjection * instance.volume.worldFromLocal(),
                    boundsMin, boundsMax,
                    faceSelection.fullScreenCoverageRequired);
            }
        }
    }
    volumeDrawPlan_.projectedScreenCoverage = projectedCoverage.coverage();

    if (options.enableAlignedPrimaryTraversal)
    {
        auto layerInfo = [](const engine::VoxelInstance& instance,
                            const VolumeDrawPlan::Entry& entry) {
            const engine::VoxelVolume& volume = instance.volume;
            VoxelAlignedLayerInfo info{};
            info.dimensions = volume.dimensions();
            info.worldFromLocal = volume.worldFromLocal();
            info.localFromWorld = volume.localFromWorld();
            info.occupiedMin = volume.occupiedLocalMin();
            info.occupiedMaxExclusive = volume.occupiedLocalMaxExclusive();
            info.flags = volume.flags();
            info.opaqueOnlyDda = entry.opaqueOnlyDda;
            info.unwrappedOpaqueDda = entry.unwrappedOpaqueDda;
            return info;
        };

        for (size_t i = 0; i + 1 < volumeDrawPlan_.mainDdaVolumes.size(); ++i)
        {
            VolumeDrawPlan::Entry& first = volumeDrawPlan_.mainDdaVolumes[i];
            const VolumeDrawPlan::Entry& second =
                volumeDrawPlan_.mainDdaVolumes[i + 1];
            if (first.volumeIndex >= instances.size() ||
                second.volumeIndex >= instances.size())
            {
                continue;
            }

            const engine::VoxelInstance& firstInstance =
                instances[first.volumeIndex];
            const engine::VoxelInstance& secondInstance =
                instances[second.volumeIndex];
            const VoxelAlignedLayerTraversal aligned =
                classifyVoxelAlignedLayerTraversal(
                    layerInfo(firstInstance, first),
                    layerInfo(secondInstance, second));
            if (!aligned.compatible)
            {
                continue;
            }

            const VoxelRasterFaceSelection sharedFaceSelection =
                classifyVoxelRasterFaces(
                    firstInstance.volume.localFromWorld(),
                    firstInstance.volume.worldFromLocal(),
                    glm::vec3(aligned.occupiedMin),
                    glm::vec3(aligned.occupiedMaxExclusive), cameraPos,
                    options.nearClipGuardRadiusWorld);
            first.sharesAlignedTraversalWithNext = true;
            first.sharedFullScreenCoverageRequired =
                sharedFaceSelection.fullScreenCoverageRequired;
            first.sharedCameraInsideRasterBounds =
                sharedFaceSelection.cameraInside;
            first.sharedFaceCullingSafe = sharedFaceSelection.safe;
            first.sharedFrontFaceWindingReversed =
                sharedFaceSelection.windingReversed;
            ++volumeDrawPlan_.alignedPrimaryTraversalPairCount;
            ++i;
        }
    }

    for (uint32_t i = 0; i < instances.size(); ++i)
    {
        // auxiliary dda passes normally respect the same visibility/filter contract.
        // a narrowly scoped supplemental filter may admit color-replaced content to
        // sun shadows only; it cannot leak into reflections, ao, or local lighting.
        const engine::VoxelInstance& instance = instances[i];
        if (!instance.visible || !instance.volume.hasOccupiedVoxels())
        {
            continue;
        }

        const bool accepted = filter.accepts(i, instance);
        const bool supplementalSunShadowAccepted =
            options.supplementalSunShadowFilter.accept != nullptr &&
            options.supplementalSunShadowFilter.accepts(i, instance);
        if (!accepted && !supplementalSunShadowAccepted)
        {
            continue;
        }

        const VkDescriptorSet descriptorSet = world.descriptorSet(i);
        if (descriptorSet == VK_NULL_HANDLE)
        {
            continue;
        }

        const uint32_t volumeFlags = instance.volume.flags();
        const bool isStaticVolume =
            (volumeFlags & engine::VoxelVolume::FLAG_STATIC) != 0u;
        const bool opaqueOnlyDda =
            voxelVolumeQualifiesForOpaqueOnlyDda(volumeFlags);
        const bool unwrappedOpaqueDda =
            voxelVolumeQualifiesForUnwrappedOpaqueDda(volumeFlags);
        const VoxelLightingOcclusionParticipation participation =
            voxelLightingOcclusionParticipation(instance.volume.lightingOcclusionMode(),
                                                isStaticVolume);
        if (accepted && participation.planarReflection)
        {
            volumeDrawPlan_.staticVolumes.push_back(
                {i, descriptorSet, opaqueOnlyDda, unwrappedOpaqueDda});
        }
        if ((accepted || supplementalSunShadowAccepted) && participation.sunShadow)
        {
            VolumeDrawPlan::Entry entry{
                i, descriptorSet, opaqueOnlyDda, unwrappedOpaqueDda};
            entry.terrainShadowColumns = instance.hasTerrainShadowColumns();
            volumeDrawPlan_.sunShadowOccluderVolumes.push_back(entry);
            if (entry.terrainShadowColumns)
            {
                ++volumeDrawPlan_.terrainShadowColumnVolumeCount;
            }
        }
        if (accepted && participation.localLighting)
        {
            volumeDrawPlan_.localLightingOccluderVolumes.push_back(
                {i, descriptorSet, opaqueOnlyDda, unwrappedOpaqueDda});
            if (voxelVolumeMayOccludeLocalShadows(instance.volume,
                                                  options.localShadowLights))
            {
                volumeDrawPlan_.localShadowOccluderVolumes.push_back(
                    {i, descriptorSet, opaqueOnlyDda,
                     unwrappedOpaqueDda});
            }
        }
    }

    return volumeDrawPlan_;
}

VoxelRenderResources::MeshStats VoxelRenderResources::meshStats() const
{
    MeshStats stats{};
    for (const ChunkRender& render : chunkRenders_)
    {
        bool hasMesh = false;
        for (const MeshGpu& mesh : render.meshes)
        {
            stats.vertices += mesh.vertexCount;
            stats.indices += mesh.indexCount;
            if (mesh.vertexCount > 0)
            {
                hasMesh = true;
            }
        }
        if (hasMesh)
        {
            ++stats.meshedChunks;
        }
    }
    return stats;
}

}  // namespace engine::render
