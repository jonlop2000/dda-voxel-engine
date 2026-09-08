#pragma once

#include <span>

#include <vulkan/vulkan.h>

#include "engine/render/passes/OBBPass.h"
#include "engine/render/voxel/VoxelRenderResources.h"

namespace app::render
{

struct VoxelDrawBatchData
{
    engine::OBBPass* pass = nullptr;
    std::span<const engine::render::VoxelRenderResources::VolumeDrawPlan::Entry> volumes{};
};

inline void drawVoxelBatchCallback(VkCommandBuffer cmd, VkDescriptorSet frameSet, void* user)
{
    auto* data = static_cast<VoxelDrawBatchData*>(user);
    if (data == nullptr || data->pass == nullptr)
    {
        return;
    }
    for (size_t volumeOffset = 0; volumeOffset < data->volumes.size();
         ++volumeOffset)
    {
        const auto& volume = data->volumes[volumeOffset];
        if (volume.sharesAlignedTraversalWithNext &&
            volumeOffset + 1 < data->volumes.size() &&
            data->pass->supportsSharedAlignedTraversal())
        {
            const auto& second = data->volumes[volumeOffset + 1];
            data->pass->drawSharedAligned(
                cmd, frameSet, volume.descriptorSet, second.descriptorSet,
                volume.volumeIndex, second.volumeIndex,
                volume.sharedFullScreenCoverageRequired,
                volume.sharedCameraInsideRasterBounds,
                volume.sharedFaceCullingSafe,
                volume.sharedFrontFaceWindingReversed);
            ++volumeOffset;
            continue;
        }
        data->pass->draw(cmd, frameSet, volume.descriptorSet, volume.volumeIndex,
                         volume.opaqueOnlyDda, volume.unwrappedOpaqueDda,
                         volume.fullScreenCoverageRequired,
                         volume.cameraInsideRasterBounds, volume.faceCullingSafe,
                         volume.frontFaceWindingReversed);
    }
}

}  // namespace app::render
