#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/voxel/VoxelMaterialCategory.h"

struct VulkanContext;

namespace engine
{

// 3x vec4 = 48 bytes, matches glsl PaletteEntry.
struct alignas(16) PaletteEntryCPU
{
    glm::vec4 baseColor_alpha; // rgb + alpha
    glm::vec4 pbr0;            // metallic, roughness, emissive, shadingModel(float)
    glm::vec4 extra;           // ior, transmission, absorption, materialCategory(float)
};

static_assert(sizeof(PaletteEntryCPU) == 48, "PaletteEntryCPU must match GLSL struct size");
static_assert(alignof(PaletteEntryCPU) == 16, "PaletteEntryCPU must be 16-byte aligned");

class VoxelPalette
{
public:
    VoxelPalette() = default;
    ~VoxelPalette() = default;

    VoxelPalette(const VoxelPalette&) = delete;
    VoxelPalette& operator=(const VoxelPalette&) = delete;

    VoxelPalette(VoxelPalette&&) noexcept;
    VoxelPalette& operator=(VoxelPalette&&) noexcept;

    bool create(::VulkanContext& ctx, uint32_t paletteCount, const std::string& debugName);
    void destroy(::VulkanContext& ctx);

    // cpu-side edit
    void setEntry(uint32_t paletteId, uint32_t voxelId, const PaletteEntryCPU& entry);
    const PaletteEntryCPU& getEntry(uint32_t paletteId, uint32_t voxelId) const;

    // Upload/download
    bool upload(::VulkanContext& ctx);
    bool download(::VulkanContext& ctx, std::vector<PaletteEntryCPU>& out) const;

    bool isValid() const { return static_cast<bool>(m_buffer); }

    uint32_t paletteCount() const { return m_paletteCount; }
    VkBuffer buffer() const { return m_buffer.get(); }
    VkDescriptorBufferInfo descriptorInfo() const
    {
        VkDescriptorBufferInfo info{};
        info.buffer = m_buffer.get();
        info.offset = 0;
        info.range = VK_WHOLE_SIZE;
        return info;
    }

    // convenience: fill a default palette (palette 0).
    void buildDefaultPalette0();

    // adds aquarium-specific materials (glass/water ids 3-4, opaque ids 5-11, and authored bands)
    // to palette 0.
    // call after buildDefaultPalette0() or standalone.
    void buildAquariumPalette();

    // adds cloud materials (ids 100-115) to palette 0.
    // 16 colors: white cores, gray edges, translucent wisps, dark undersides.
    void buildCloudPalette();

private:
    size_t totalEntries() const { return static_cast<size_t>(m_paletteCount) * 256u; }
    size_t totalBytes() const { return totalEntries() * sizeof(PaletteEntryCPU); }

    uint32_t m_paletteCount = 0;
    std::string m_debugName;

    std::vector<PaletteEntryCPU> m_cpu; // paletteCount * 256

    engine::render::UniqueBuffer m_buffer{};

    bool m_dirty = false;
};

} // namespace engine
