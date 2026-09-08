#include "engine/voxel/ModularGlassMeshManager.h"

#include "Core/Logger.h"
#include "engine/render/RenderObject.h"
#include "engine/render/VulkanContext.h"
#include "Resources/Mesh.h"
#include "engine/voxel/ModularGlassMesher.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelWorld.h"

#include <algorithm>

namespace engine
{
namespace
{
bool isEligibleGlassVolume(const VoxelInstance& inst)
{
    return inst.visible && (inst.volume.flags() & VoxelVolume::FLAG_GLASS) != 0u &&
           !inst.cpuVoxels.empty();
}
} // namespace

void ModularGlassMeshManager::clear(VkDevice device)
{
    for (Entry& entry : entries_)
    {
        destroyMeshBuffer(device, entry.mesh);
    }
    entries_.clear();
    sourceWorldRevision_ = 0;
}

void ModularGlassMeshManager::rebuild(VulkanContext& ctx, Commands& commands,
                                      const VoxelWorld& world,
                                      const std::vector<uint8_t>& glassMaterialIds)
{
    if (glassMaterialIds.empty())
    {
        clear(ctx.device);
        return;
    }

    const std::vector<VoxelInstance>& instances = world.instances();
    if (sourceWorldRevision_ != 0 && sourceWorldRevision_ != world.structureRevision())
    {
        clear(ctx.device);
    }
    sourceWorldRevision_ = world.structureRevision();

    uint32_t removedMeshes = 0;
    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (it->volumeIndex >= instances.size() ||
            !isEligibleGlassVolume(instances[it->volumeIndex]))
        {
            destroyMeshBuffer(ctx.device, it->mesh);
            it = entries_.erase(it);
            ++removedMeshes;
        }
        else
        {
            ++it;
        }
    }

    entries_.reserve(instances.size());

    uint32_t rebuiltMeshes = 0;
    uint32_t rebuiltVoxels = 0;
    uint32_t rebuiltQuads = 0;
    for (uint32_t i = 0; i < instances.size(); ++i)
    {
        const VoxelInstance& inst = instances[i];
        if (!isEligibleGlassVolume(inst))
        {
            continue;
        }

        auto entryIt = findEntry(i);
        if (entryIt != entries_.end() && entryIt->sourceRevision == inst.cpuVoxelRevision)
        {
            continue;
        }

        ModularGlassMesh mesh = buildModularGlassMesh(ModularGlassMeshInput{
            inst.volume.dimensions(), &inst.cpuVoxels, glassMaterialIds});
        if (mesh.vertices.empty() || mesh.indices.empty())
        {
            if (entryIt != entries_.end())
            {
                destroyMeshBuffer(ctx.device, entryIt->mesh);
                entries_.erase(entryIt);
                ++removedMeshes;
            }
            continue;
        }

        if (entryIt == entries_.end())
        {
            entries_.push_back(Entry{});
            entryIt = entries_.end() - 1;
            entryIt->volumeIndex = i;
        }
        else
        {
            destroyMeshBuffer(ctx.device, entryIt->mesh);
        }

        entryIt->occupiedVoxelCount = mesh.occupiedVoxelCount;
        entryIt->quadCount = mesh.quadCount;
        entryIt->sourceRevision = inst.cpuVoxelRevision;
        createMeshBuffer(ctx, commands, mesh.vertices, mesh.indices, entryIt->mesh);

        ++rebuiltMeshes;
        rebuiltVoxels += mesh.occupiedVoxelCount;
        rebuiltQuads += mesh.quadCount;
    }

    if (rebuiltMeshes > 0 || removedMeshes > 0)
    {
        logInfo("ModularGlass",
                makeLogMessage("Refreshed ", rebuiltMeshes, " connected mesh volume(s), ",
                               rebuiltVoxels, " glass voxel(s), ", rebuiltQuads,
                               " quad(s), removed ", removedMeshes,
                               " stale mesh(es); active meshes: ", entries_.size(), "."));
    }
}

void ModularGlassMeshManager::appendRenderObjects(std::vector<RenderObject>& out,
                                                  const VoxelWorld& world)
{
    const std::vector<VoxelInstance>& instances = world.instances();
    for (Entry& entry : entries_)
    {
        if (entry.volumeIndex >= instances.size() || !instances[entry.volumeIndex].visible ||
            entry.mesh.vbo == VK_NULL_HANDLE)
        {
            continue;
        }

        RenderObject obj{};
        obj.model = instances[entry.volumeIndex].volume.worldFromLocal();
        obj.mesh = &entry.mesh;
        obj.material = nullptr;
        out.push_back(obj);
    }
}

bool ModularGlassMeshManager::ownsVolume(uint32_t volumeIndex) const
{
    return findEntry(volumeIndex) != entries_.end();
}

std::vector<ModularGlassMeshManager::Entry>::iterator
ModularGlassMeshManager::findEntry(uint32_t volumeIndex)
{
    return std::find_if(entries_.begin(), entries_.end(),
                        [volumeIndex](const Entry& entry)
                        {
                            return entry.volumeIndex == volumeIndex;
                        });
}

std::vector<ModularGlassMeshManager::Entry>::const_iterator
ModularGlassMeshManager::findEntry(uint32_t volumeIndex) const
{
    return std::find_if(entries_.begin(), entries_.end(),
                        [volumeIndex](const Entry& entry)
                        {
                            return entry.volumeIndex == volumeIndex;
                        });
}

} // namespace engine
