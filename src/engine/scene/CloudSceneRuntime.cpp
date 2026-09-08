#include "engine/scene/CloudScene.h"

#include "Core/Logger.h"
#include "engine/render/VulkanContext.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelWorld.h"

namespace
{

static std::vector<CloudVolumeInfo> s_cloudVolumeInfos;

} // namespace

int CloudScene::init(VulkanContext& ctx, engine::VoxelWorld& world,
                     engine::VoxelPalette& palette, const CloudSettings& settings)
{
    logInfo("CloudScene", "Initializing Cloud Scene.");
    const CloudBuildResult result = buildVolumeData(settings);

    palette.buildCloudPalette();
    if (!palette.upload(ctx))
    {
        logError("CloudScene", "Failed to upload cloud palette.");
        return -1;
    }

    s_cloudVolumeInfos.clear();
    s_cloudVolumeInfos.push_back(
        CloudVolumeInfo{result.spec.name, result.spec.dims, result.spec.position, result.spec.flags});

    const int volumeIndex = world.addVolume(ctx, palette, result.spec, result.voxels);
    if (volumeIndex < 0)
    {
        logError("CloudScene", "Failed to add cloud volume to VoxelWorld.");
        return -1;
    }

    logInfo("CloudScene",
            makeLogMessage("Cloud Scene initialized successfully (volume index: ", volumeIndex,
                           ")."));
    return volumeIndex;
}

const std::vector<CloudVolumeInfo>& CloudScene::getVolumeInfos()
{
    return s_cloudVolumeInfos;
}
