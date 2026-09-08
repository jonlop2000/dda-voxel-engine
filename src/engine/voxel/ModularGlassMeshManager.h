#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "Resources/Mesh.h"

struct Commands;
struct RenderObject;
struct VulkanContext;

namespace engine
{

class VoxelWorld;

class ModularGlassMeshManager
{
public:
    void rebuild(VulkanContext& ctx, Commands& commands, const VoxelWorld& world,
                 const std::vector<uint8_t>& glassMaterialIds);
    void clear(VkDevice device);
    void appendRenderObjects(std::vector<RenderObject>& out, const VoxelWorld& world);

    bool ownsVolume(uint32_t volumeIndex) const;
    bool empty() const { return entries_.empty(); }
    size_t meshCount() const { return entries_.size(); }

private:
    struct Entry
    {
        uint32_t volumeIndex = 0;
        MeshGpu mesh{};
        uint32_t occupiedVoxelCount = 0;
        uint32_t quadCount = 0;
        uint64_t sourceRevision = 0;
    };

    std::vector<Entry>::iterator findEntry(uint32_t volumeIndex);
    std::vector<Entry>::const_iterator findEntry(uint32_t volumeIndex) const;

    std::vector<Entry> entries_{};
    uint64_t sourceWorldRevision_ = 0;
};

} // namespace engine
