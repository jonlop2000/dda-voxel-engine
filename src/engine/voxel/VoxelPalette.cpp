#include "engine/voxel/VoxelPalette.h"

#include "engine/scene/AquariumScene.h"
#include "engine/render/VulkanContext.h"

#include <algorithm>
#include <cstring>

namespace engine
{

namespace
{
namespace AquariumPaletteCategory
{
constexpr float Generic = 0.0f;
constexpr float Frame = 1.0f;
constexpr float Gravel = 2.0f;
constexpr float Plant = 3.0f;
constexpr float Fish = 4.0f;
constexpr float Stone = 5.0f;
constexpr float Wood = 6.0f;
constexpr float Coral = 7.0f;
constexpr float Glass = 8.0f;
constexpr float Water = 9.0f;
constexpr float Room = 10.0f;
} // namespace AquariumPaletteCategory

float shadingModelValue(uint32_t value)
{
    return static_cast<float>(value);
}

PaletteEntryCPU makeAquariumEntry(const glm::vec3& color, float roughness, float category,
                                  float emissive, float metallic, uint32_t shadingModel,
                                  float alpha, float ior, float transmission, float absorption)
{
    return PaletteEntryCPU{glm::vec4(color, alpha),
                           glm::vec4(metallic, roughness, emissive,
                                     shadingModelValue(shadingModel)),
                           glm::vec4(ior, transmission, absorption, category)};
}

PaletteEntryCPU makeAquariumOpaqueEntry(const glm::vec3& color, float roughness, float category,
                                        float emissive = 0.0f, float metallic = 0.0f)
{
    return makeAquariumEntry(color, roughness, category, emissive, metallic, 0u, 1.0f, 1.0f,
                             0.0f, 0.0f);
}
} // namespace

VoxelPalette::VoxelPalette(VoxelPalette&& other) noexcept
{
    *this = std::move(other);
}

VoxelPalette& VoxelPalette::operator=(VoxelPalette&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    m_paletteCount = other.m_paletteCount;
    m_debugName = std::move(other.m_debugName);
    m_cpu = std::move(other.m_cpu);
    m_buffer = std::move(other.m_buffer);
    m_dirty = other.m_dirty;

    other.m_paletteCount = 0;
    other.m_dirty = false;
    return *this;
}

bool VoxelPalette::create(::VulkanContext& ctx, uint32_t paletteCount,
                          const std::string& debugName)
{
    if (paletteCount == 0 || isValid())
    {
        return false;
    }

    m_paletteCount = paletteCount;
    m_debugName = debugName;

    PaletteEntryCPU defaultEntry{};
    defaultEntry.baseColor_alpha = glm::vec4(0, 0, 0, 0);
    defaultEntry.pbr0 = glm::vec4(0, 1, 0, 0);
    defaultEntry.extra = glm::vec4(1.0f, 0, 0, 0);
    m_cpu.assign(totalEntries(), defaultEntry);

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(totalBytes());

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    const std::string allocationName = m_debugName + "_paletteSSBO";
    if (engine::render::createUniqueBuffer(ctx.memoryAllocator, bufferInfo, allocationInfo,
                                            m_buffer, allocationName.c_str()) != VK_SUCCESS)
    {
        return false;
    }

    if (!m_debugName.empty())
    {
        ctx.setDebugName(reinterpret_cast<uint64_t>(m_buffer.get()), VK_OBJECT_TYPE_BUFFER,
                         (m_debugName + "_paletteSSBO").c_str());
    }

    m_dirty = true;
    return true;
}

void VoxelPalette::destroy(::VulkanContext&)
{
    m_buffer.reset();
    m_cpu.clear();
    m_paletteCount = 0;
    m_dirty = false;
}

void VoxelPalette::setEntry(uint32_t paletteId, uint32_t voxelId, const PaletteEntryCPU& entry)
{
    if (paletteId >= m_paletteCount || voxelId >= 256)
    {
        return;
    }
    m_cpu[static_cast<size_t>(paletteId) * 256u + voxelId] = entry;
    m_dirty = true;
}

const PaletteEntryCPU& VoxelPalette::getEntry(uint32_t paletteId, uint32_t voxelId) const
{
    static const PaletteEntryCPU dummy{};
    if (paletteId >= m_paletteCount || voxelId >= 256)
    {
        return dummy;
    }
    return m_cpu[static_cast<size_t>(paletteId) * 256u + voxelId];
}

bool VoxelPalette::upload(::VulkanContext& ctx)
{
    if (!isValid())
    {
        return false;
    }
    if (!m_dirty)
    {
        return true;
    }

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(totalBytes());

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    engine::render::UniqueBuffer staging{};
    const std::string allocationName = m_debugName + "_paletteUploadStaging";
    if (engine::render::createUniqueBuffer(ctx.memoryAllocator, bufferInfo, allocationInfo,
                                            staging, allocationName.c_str()) != VK_SUCCESS)
    {
        return false;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(ctx.memoryAllocator, staging.allocation(), &mapped) != VK_SUCCESS)
    {
        return false;
    }
    std::memcpy(mapped, m_cpu.data(), static_cast<size_t>(bytes));
    const VkResult flushResult =
        vmaFlushAllocation(ctx.memoryAllocator, staging.allocation(), 0, bytes);
    vmaUnmapMemory(ctx.memoryAllocator, staging.allocation());
    if (flushResult != VK_SUCCESS)
    {
        return false;
    }

    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();

    VkBufferCopy copy{};
    copy.srcOffset = 0;
    copy.dstOffset = 0;
    copy.size = bytes;

    vkCmdCopyBuffer(cmd, staging.get(), m_buffer.get(), 1, &copy);

    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = m_buffer.get();
    barrier.offset = 0;
    barrier.size = bytes;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 1, &barrier, 0, nullptr);

    ctx.endSingleTimeCommands(cmd);

    m_dirty = false;
    return true;
}

bool VoxelPalette::download(::VulkanContext& ctx, std::vector<PaletteEntryCPU>& out) const
{
    if (!isValid())
    {
        return false;
    }

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(totalBytes());
    out.resize(totalEntries());

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    engine::render::UniqueBuffer staging{};
    const std::string allocationName = m_debugName + "_paletteReadbackStaging";
    if (engine::render::createUniqueBuffer(ctx.memoryAllocator, bufferInfo, allocationInfo,
                                            staging, allocationName.c_str()) != VK_SUCCESS)
    {
        return false;
    }

    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();

    VkBufferCopy copy{};
    copy.srcOffset = 0;
    copy.dstOffset = 0;
    copy.size = bytes;

    // buffer is only written by upload() via vkCmdCopyBuffer, so use precise access mask
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = m_buffer.get();
    barrier.offset = 0;
    barrier.size = bytes;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &barrier, 0,
                         nullptr);

    vkCmdCopyBuffer(cmd, m_buffer.get(), staging.get(), 1, &copy);

    ctx.endSingleTimeCommands(cmd);

    void* mapped = nullptr;
    if (vmaMapMemory(ctx.memoryAllocator, staging.allocation(), &mapped) != VK_SUCCESS)
    {
        return false;
    }
    const VkResult invalidateResult =
        vmaInvalidateAllocation(ctx.memoryAllocator, staging.allocation(), 0, bytes);
    if (invalidateResult != VK_SUCCESS)
    {
        vmaUnmapMemory(ctx.memoryAllocator, staging.allocation());
        return false;
    }
    std::memcpy(out.data(), mapped, static_cast<size_t>(bytes));
    vmaUnmapMemory(ctx.memoryAllocator, staging.allocation());

    return true;
}

void VoxelPalette::buildDefaultPalette0()
{
    auto SM = [](uint32_t v) { return static_cast<float>(v); };

    setEntry(0, 0,
             PaletteEntryCPU{glm::vec4(0, 0, 0, 0), glm::vec4(0, 1, 0, SM(0)),
                             glm::vec4(1.0f, 0, 0, 0)});

    setEntry(0, 1,
             PaletteEntryCPU{glm::vec4(0.35f, 0.35f, 0.38f, 1.0f),
                             glm::vec4(0.0f, 0.9f, 0.0f, SM(0)),
                             glm::vec4(1.0f, 0, 0, 0)});

    setEntry(0, 2,
             PaletteEntryCPU{glm::vec4(0.78f, 0.72f, 0.52f, 1.0f),
                             glm::vec4(0.0f, 0.85f, 0.0f, SM(0)),
                             glm::vec4(1.0f, 0, 0, 0)});

    setEntry(0, 3,
             PaletteEntryCPU{glm::vec4(0.90f, 0.95f, 1.00f, 0.08f),
                             glm::vec4(0.0f, 0.02f, 0.0f, SM(1)),
                             glm::vec4(1.50f, 1.0f, 0.02f, 0.0f)});

    setEntry(0, 4,
             PaletteEntryCPU{glm::vec4(0.70f, 0.85f, 1.00f, 0.12f),
                             glm::vec4(0.0f, 0.02f, 0.0f, SM(2)),
                             glm::vec4(1.333f, 1.0f, 0.10f, 0.0f)});
}

void VoxelPalette::buildAquariumPalette()
{
    // override default glass for aquarium scenes to be visually readable while
    // preserving transmissive behavior in the dda path.
    setEntry(0, 3,
             makeAquariumEntry(glm::vec3(0.82f, 0.90f, 0.98f), 0.02f,
                                AquariumPaletteCategory::Glass, 0.0f, 0.0f, 1u, 0.15f, 1.50f,
                                1.0f, 0.12f));

    // id 4: voxel water. Analytic/post water is usually the visible water layer in mesh-glass
    // aquarium scenes, but this keeps the legacy voxel-water path material-categorized.
    setEntry(0, 4,
             makeAquariumEntry(glm::vec3(0.70f, 0.85f, 1.00f), 0.02f,
                                AquariumPaletteCategory::Water, 0.0f, 0.0f, 2u, 0.12f, 1.333f,
                                1.0f, 0.10f));

    // id 5: aperture trim / secondary chamber wall
    setEntry(0, 5,
             makeAquariumOpaqueEntry(glm::vec3(0.16f, 0.18f, 0.20f), 0.94f,
                                     AquariumPaletteCategory::Room));

    // id 6: Room floor - dark enough for strong caustic contrast
    setEntry(0, 6,
             makeAquariumOpaqueEntry(glm::vec3(0.22f, 0.24f, 0.27f), 0.86f,
                                     AquariumPaletteCategory::Room));

    // id 7: rock - brown/gray, very rough
    setEntry(0, 7,
             makeAquariumOpaqueEntry(glm::vec3(0.44f, 0.45f, 0.46f), 0.94f,
                                     AquariumPaletteCategory::Stone));

    // id 8: Plant - green
    setEntry(0, 8,
             makeAquariumOpaqueEntry(glm::vec3(0.12f, 0.50f, 0.28f), 0.68f,
                                     AquariumPaletteCategory::Plant));

    // id 9: Fish - bright orange
    setEntry(0, 9,
             makeAquariumOpaqueEntry(glm::vec3(1.0f, 0.46f, 0.12f), 0.22f,
                                     AquariumPaletteCategory::Fish));

    // id 10: Fish accent - white
    setEntry(0, 10,
             makeAquariumOpaqueEntry(glm::vec3(1.0f, 0.96f, 0.86f), 0.14f,
                                     AquariumPaletteCategory::Fish, 0.035f));

    // id 11: tank frame / chamber shell - dark graphite matte
    setEntry(0, 11,
             makeAquariumOpaqueEntry(glm::vec3(0.22f, 0.24f, 0.27f), 0.82f,
                                     AquariumPaletteCategory::Frame, 0.0f, 0.04f));

    // --- palette bands for texture variation (teardown-quality, 12 entries each) ---
    // non-linear distributions with accent colors for natural material appearance

    // tank frame band: indices 150-161 (12 variants)
    // readable graphite-to-slate variation for the voxel rim, trim, and fasteners.
    // avoid near-black albedo here: the frame is viewed through water/glass and
    // must remain readable when the sun direction is valid for the visible face.
    {
        const glm::vec3 frameColors[12] = {
            {0.120f, 0.135f, 0.155f}, // recessed detail
            {0.145f, 0.162f, 0.185f}, // dark graphite
            {0.170f, 0.190f, 0.215f},
            {0.195f, 0.218f, 0.245f},
            {0.220f, 0.245f, 0.275f},
            {0.245f, 0.272f, 0.305f}, // mid graphite
            {0.275f, 0.305f, 0.340f},
            {0.305f, 0.338f, 0.375f},
            {0.335f, 0.370f, 0.410f},
            {0.365f, 0.400f, 0.435f}, // worn edge
            {0.430f, 0.460f, 0.490f}, // screw/edge highlight
            {0.180f, 0.200f, 0.225f}, // muted patch
        };
        const float roughness[12] = {0.88f, 0.86f, 0.84f, 0.82f, 0.80f, 0.78f,
                                     0.76f, 0.74f, 0.72f, 0.69f, 0.62f, 0.83f};
        const float metallic[12] = {0.02f, 0.03f, 0.03f, 0.04f, 0.04f, 0.04f,
                                    0.05f, 0.05f, 0.05f, 0.06f, 0.08f, 0.03f};
        for (int i = 0; i < 12; ++i)
        {
            setEntry(0, 150 + i,
                     makeAquariumOpaqueEntry(frameColors[i], roughness[i],
                                             AquariumPaletteCategory::Frame, 0.0f,
                                             metallic[i]));
        }
    }

    // Gravel band: indices 20-31 (12 variants)
    // dark pebbles at bottom, sandy shell highlights at top, with cool wet accents.
    {
        const glm::vec3 gravelColors[12] = {
            {0.22f, 0.20f, 0.17f}, // damp shadow pebble
            {0.30f, 0.27f, 0.22f}, // dark tan gravel
            {0.36f, 0.33f, 0.27f},
            {0.43f, 0.39f, 0.31f},
            {0.50f, 0.46f, 0.36f},
            {0.56f, 0.52f, 0.41f}, // mid sand
            {0.62f, 0.58f, 0.47f},
            {0.68f, 0.64f, 0.52f},
            {0.74f, 0.70f, 0.58f},
            {0.76f, 0.72f, 0.60f}, // shell fleck
            {0.82f, 0.78f, 0.66f}, // bright shell highlight
            {0.56f, 0.63f, 0.64f}, // cool wet accent
        };
        const float roughness[12] = {0.99f, 0.98f, 0.97f, 0.96f, 0.95f, 0.94f,
                                     0.93f, 0.92f, 0.90f, 0.88f, 0.86f, 0.91f};
        for (int i = 0; i < 12; ++i)
        {
            setEntry(0, 20 + i,
                     makeAquariumOpaqueEntry(gravelColors[i], roughness[i],
                                             AquariumPaletteCategory::Gravel));
        }
    }

    // Coral band: indices 35-46 (12 variants)
    // deep reef base to bright cyan and pink-tipped accents.
    {
        const glm::vec3 coralColors[12] = {
            {0.06f, 0.22f, 0.28f}, // deep base
            {0.08f, 0.30f, 0.36f},
            {0.10f, 0.38f, 0.43f},
            {0.14f, 0.47f, 0.50f},
            {0.18f, 0.56f, 0.55f},
            {0.24f, 0.65f, 0.60f}, // mid teal
            {0.30f, 0.74f, 0.66f},
            {0.42f, 0.84f, 0.76f},
            {0.56f, 0.86f, 0.80f}, // pale tip
            {0.86f, 0.66f, 0.78f}, // pink tip
            {0.88f, 0.76f, 0.62f}, // warm polyp
            {0.24f, 0.38f, 0.58f}, // blue shadow accent
        };
        const float roughness[12] = {0.82f, 0.80f, 0.78f, 0.76f, 0.74f, 0.72f,
                                     0.70f, 0.68f, 0.65f, 0.62f, 0.60f, 0.76f};
        const float emissive[12] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    0.008f, 0.012f, 0.015f, 0.022f, 0.026f, 0.0f};
        for (int i = 0; i < 12; ++i)
        {
            setEntry(0, 35 + i,
                     makeAquariumOpaqueEntry(coralColors[i], roughness[i],
                                             AquariumPaletteCategory::Coral, emissive[i]));
        }
    }

    // Wood band: indices 50-61 (12 variants)
    // dark knots, heartwood, sapwood with grain accents
    {
        const glm::vec3 woodColors[12] = {
            {0.20f, 0.14f, 0.08f},  // dark knot
            {0.25f, 0.18f, 0.12f},  // heartwood dark
            {0.30f, 0.22f, 0.15f},
            {0.35f, 0.26f, 0.18f},
            {0.40f, 0.30f, 0.20f},
            {0.45f, 0.34f, 0.24f},  // mid grain
            {0.48f, 0.38f, 0.26f},
            {0.52f, 0.40f, 0.28f},
            {0.55f, 0.43f, 0.30f},  // sapwood
            {0.58f, 0.46f, 0.32f},
            {0.60f, 0.48f, 0.34f},  // light sapwood
            {0.22f, 0.16f, 0.10f},  // accent: dark ring/knot
        };
        const float roughness[12] = {0.94f, 0.92f, 0.90f, 0.88f, 0.86f, 0.84f,
                                     0.82f, 0.80f, 0.78f, 0.76f, 0.74f, 0.95f};
        for (int i = 0; i < 12; ++i)
        {
            setEntry(0, 50 + i,
                     makeAquariumOpaqueEntry(woodColors[i], roughness[i],
                                             AquariumPaletteCategory::Wood));
        }
    }

    // table wood band: indices 162-173 (12 variants)
    // pale sealed tabletop tones with darker seams and knot accents. kept separate
    // from aquarium driftwood so the fishbowl stand can read as finished furniture.
    {
        const glm::vec3 tableColors[12] = {
            {0.23f, 0.15f, 0.08f}, // dark knot core
            {0.34f, 0.22f, 0.12f}, // deep board seam
            {0.46f, 0.31f, 0.17f}, // shadowed groove
            {0.55f, 0.39f, 0.23f},
            {0.64f, 0.48f, 0.29f},
            {0.73f, 0.58f, 0.37f}, // honey base
            {0.82f, 0.68f, 0.46f},
            {0.90f, 0.78f, 0.56f},
            {0.96f, 0.87f, 0.68f}, // pale worn grain
            {1.00f, 0.93f, 0.78f},
            {0.92f, 0.96f, 0.95f}, // cool varnish catch-light
            {0.42f, 0.29f, 0.18f}, // ring/knot accent
        };
        const float roughness[12] = {0.90f, 0.89f, 0.88f, 0.86f, 0.84f, 0.82f,
                                     0.80f, 0.78f, 0.75f, 0.72f, 0.66f, 0.90f};
        for (int i = 0; i < 12; ++i)
        {
            setEntry(0, AquariumMaterial::TableWood::Base + i,
                     makeAquariumOpaqueEntry(tableColors[i], roughness[i],
                                             AquariumPaletteCategory::Wood));
        }
    }

    // Stone band: indices 65-76 (12 variants)
    // weathered stone with dark crevices and light highlights
    {
        const glm::vec3 stoneColors[12] = {
            {0.35f, 0.36f, 0.38f},  // dark crevice
            {0.40f, 0.41f, 0.43f},  // dark weathered
            {0.45f, 0.46f, 0.48f},
            {0.50f, 0.50f, 0.52f},
            {0.54f, 0.54f, 0.56f},
            {0.58f, 0.58f, 0.60f},  // mid-tone
            {0.62f, 0.61f, 0.62f},
            {0.66f, 0.64f, 0.64f},
            {0.70f, 0.68f, 0.66f},
            {0.73f, 0.71f, 0.68f},  // light surface
            {0.76f, 0.74f, 0.70f},  // bright highlight
            {0.42f, 0.44f, 0.46f},  // accent: cool shadow
        };
        const float roughness[12] = {0.97f, 0.96f, 0.95f, 0.94f, 0.93f, 0.92f,
                                     0.91f, 0.90f, 0.89f, 0.88f, 0.86f, 0.96f};
        for (int i = 0; i < 12; ++i)
        {
            setEntry(0, 65 + i,
                     makeAquariumOpaqueEntry(stoneColors[i], roughness[i],
                                             AquariumPaletteCategory::Stone));
        }
    }

    // Plant band: indices 80-91 (12 variants)
    // dark blue-green stems to translucent mint leaf tips with natural variation.
    {
        const glm::vec3 plantColors[12] = {
            {0.04f, 0.18f, 0.10f}, // stem base
            {0.05f, 0.24f, 0.14f},
            {0.07f, 0.32f, 0.18f},
            {0.09f, 0.40f, 0.23f},
            {0.11f, 0.48f, 0.28f},
            {0.14f, 0.56f, 0.34f}, // mid leaf
            {0.18f, 0.64f, 0.40f},
            {0.24f, 0.72f, 0.48f},
            {0.34f, 0.80f, 0.58f},
            {0.44f, 0.80f, 0.62f}, // lit leaf
            {0.58f, 0.86f, 0.70f}, // mint tip
            {0.10f, 0.36f, 0.30f}, // blue-green shadow
        };
        const float roughness[12] = {0.86f, 0.84f, 0.82f, 0.80f, 0.78f, 0.76f,
                                     0.74f, 0.72f, 0.70f, 0.68f, 0.66f, 0.82f};
        for (int i = 0; i < 12; ++i)
        {
            setEntry(0, 80 + i,
                     makeAquariumOpaqueEntry(plantColors[i], roughness[i],
                                             AquariumPaletteCategory::Plant));
        }
    }

    // grass band: indices 92-103 (12 variants)
    // warmer ground cover kept below the pale PlantBand tips to avoid pellet-like highlights.
    {
        const glm::vec3 grassColors[12] = {
            {0.07f, 0.18f, 0.04f}, // damp root
            {0.09f, 0.24f, 0.05f},
            {0.11f, 0.31f, 0.07f},
            {0.14f, 0.38f, 0.09f},
            {0.18f, 0.45f, 0.12f},
            {0.23f, 0.52f, 0.16f},
            {0.29f, 0.59f, 0.21f},
            {0.35f, 0.65f, 0.27f},
            {0.42f, 0.70f, 0.33f},
            {0.49f, 0.74f, 0.39f},
            {0.56f, 0.78f, 0.46f}, // muted sunlit blade
            {0.15f, 0.33f, 0.11f}, // olive shadow
        };
        const float roughness[12] = {0.90f, 0.89f, 0.88f, 0.86f, 0.84f, 0.82f,
                                     0.80f, 0.78f, 0.76f, 0.74f, 0.72f, 0.88f};
        for (int i = 0; i < 12; ++i)
        {
            setEntry(0, AquariumMaterial::GrassBand::Base + i,
                     makeAquariumOpaqueEntry(grassColors[i], roughness[i],
                                             AquariumPaletteCategory::Plant));
        }
    }

    // algae band: indices 104-115 (12 variants)
    // substrate-tinted olive/teal mats. avoid near-black values here: these voxels are flat
    // floor-level ground cover and can otherwise read as stray shadow artifacts on sand.
    {
        const glm::vec3 algaeColors[12] = {
            {0.08f, 0.18f, 0.13f}, // damp mat
            {0.10f, 0.22f, 0.15f},
            {0.12f, 0.26f, 0.18f},
            {0.14f, 0.30f, 0.21f},
            {0.16f, 0.34f, 0.24f},
            {0.19f, 0.38f, 0.28f},
            {0.22f, 0.42f, 0.32f},
            {0.26f, 0.46f, 0.36f},
            {0.30f, 0.50f, 0.40f},
            {0.35f, 0.55f, 0.45f},
            {0.42f, 0.60f, 0.52f}, // wet highlight
            {0.15f, 0.31f, 0.30f}, // blue-green shadow
        };
        const float roughness[12] = {0.98f, 0.97f, 0.96f, 0.95f, 0.94f, 0.93f,
                                     0.92f, 0.91f, 0.90f, 0.88f, 0.86f, 0.96f};
        for (int i = 0; i < 12; ++i)
        {
            setEntry(0, AquariumMaterial::AlgaeBand::Base + i,
                     makeAquariumOpaqueEntry(algaeColors[i], roughness[i],
                                             AquariumPaletteCategory::Plant));
        }
    }

    // foliage accent band: indices 186-197 (12 variants)
    // copper, rose, and burgundy aquarium plants for contrast against the green bands.
    {
        const glm::vec3 accentColors[12] = {
            {0.18f, 0.07f, 0.07f}, // burgundy root
            {0.25f, 0.09f, 0.08f},
            {0.34f, 0.12f, 0.09f},
            {0.43f, 0.16f, 0.10f},
            {0.52f, 0.21f, 0.13f}, // copper leaf
            {0.62f, 0.28f, 0.17f},
            {0.72f, 0.36f, 0.24f},
            {0.78f, 0.43f, 0.32f},
            {0.82f, 0.48f, 0.42f}, // rose highlight
            {0.68f, 0.32f, 0.48f},
            {0.52f, 0.22f, 0.40f}, // purple tip
            {0.30f, 0.13f, 0.18f}, // deep shadow
        };
        const float roughness[12] = {0.88f, 0.86f, 0.84f, 0.82f, 0.80f, 0.78f,
                                     0.76f, 0.74f, 0.72f, 0.78f, 0.82f, 0.88f};
        for (int i = 0; i < 12; ++i)
        {
            setEntry(0, AquariumMaterial::FoliageAccentBand::Base + i,
                     makeAquariumOpaqueEntry(accentColors[i], roughness[i],
                                             AquariumPaletteCategory::Plant));
        }
    }

    auto setFishOpaque = [&](uint8_t id, const glm::vec3& color, float roughness,
                             float emissive = 0.0f, float metallic = 0.0f) {
        setEntry(0, id,
                 makeAquariumOpaqueEntry(color, roughness, AquariumPaletteCategory::Fish,
                                         emissive, metallic));
    };

    // animated fish palettes. these keep the voxel fish inside the aquarium style while
    // allowing body, stripe, and fin variation per fish.
    {
        const glm::vec3 body[4] = {
            {0.26f, 0.07f, 0.03f},
            {0.68f, 0.18f, 0.06f},
            {0.98f, 0.42f, 0.10f},
            {1.00f, 0.78f, 0.36f},
        };
        const glm::vec3 accent[2] = {
            {0.98f, 0.90f, 0.76f},
            {1.00f, 0.98f, 0.88f},
        };
        const glm::vec3 fin[2] = {
            {0.82f, 0.28f, 0.08f},
            {1.00f, 0.62f, 0.18f},
        };
        for (int i = 0; i < 4; ++i)
        {
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::FishSunset::BodyBase + i),
                          body[i], 0.28f - 0.035f * static_cast<float>(i));
        }
        for (int i = 0; i < 2; ++i)
        {
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::FishSunset::AccentBase + i),
                          accent[i], 0.15f - 0.025f * static_cast<float>(i),
                          i == 1 ? 0.04f : 0.012f);
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::FishSunset::FinBase + i), fin[i],
                          0.34f - 0.045f * static_cast<float>(i), i == 1 ? 0.018f : 0.0f);
        }
    }

    {
        const glm::vec3 body[4] = {
            {0.14f, 0.18f, 0.24f},
            {0.46f, 0.55f, 0.62f},
            {0.76f, 0.84f, 0.90f},
            {0.98f, 0.96f, 0.86f},
        };
        const glm::vec3 accent[2] = {
            {1.00f, 0.52f, 0.16f},
            {0.10f, 0.14f, 0.20f},
        };
        const glm::vec3 fin[2] = {
            {0.78f, 0.70f, 0.34f},
            {1.00f, 0.92f, 0.60f},
        };
        for (int i = 0; i < 4; ++i)
        {
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::FishPearl::BodyBase + i),
                          body[i], 0.25f - 0.032f * static_cast<float>(i), 0.0f,
                          i >= 2 ? 0.010f : 0.0f);
        }
        for (int i = 0; i < 2; ++i)
        {
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::FishPearl::AccentBase + i),
                          accent[i], i == 0 ? 0.13f : 0.22f, i == 0 ? 0.055f : 0.0f);
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::FishPearl::FinBase + i), fin[i],
                          0.32f - 0.045f * static_cast<float>(i), i == 1 ? 0.014f : 0.0f);
        }
    }

    {
        const glm::vec3 body[4] = {
            {0.02f, 0.06f, 0.16f},
            {0.06f, 0.22f, 0.42f},
            {0.16f, 0.58f, 0.82f},
            {0.48f, 0.92f, 0.98f},
        };
        const glm::vec3 accent[2] = {
            {0.05f, 0.12f, 0.28f},
            {0.72f, 0.98f, 1.00f},
        };
        const glm::vec3 fin[2] = {
            {0.10f, 0.58f, 0.78f},
            {0.58f, 0.96f, 1.00f},
        };
        for (int i = 0; i < 4; ++i)
        {
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::FishReef::BodyBase + i),
                          body[i], 0.28f - 0.04f * static_cast<float>(i));
        }
        for (int i = 0; i < 2; ++i)
        {
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::FishReef::AccentBase + i),
                          accent[i], 0.18f - 0.05f * static_cast<float>(i),
                          i == 1 ? 0.12f : 0.0f);
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::FishReef::FinBase + i), fin[i],
                          0.31f - 0.055f * static_cast<float>(i),
                          i == 1 ? 0.055f : 0.0f);
        }
    }

    setFishOpaque(AquariumMaterial::FishEye, glm::vec3(0.012f, 0.014f, 0.018f), 0.18f);

    // axolotl (leucistic pink): pale pink body, coral external gills with a hint
    // of emissive so the fans read under water, rosy tail-fin membrane. shares
    // FishEye for the dark eyes.
    {
        const glm::vec3 body[4] = {
            {0.62f, 0.42f, 0.40f}, // belly / feet shadow
            {0.78f, 0.56f, 0.53f}, // lower flank
            {0.90f, 0.70f, 0.66f}, // main body pink
            {0.97f, 0.83f, 0.78f}, // top highlight
        };
        const glm::vec3 gill[2] = {
            {0.80f, 0.24f, 0.26f}, // stalk
            {0.98f, 0.40f, 0.36f}, // bright tip
        };
        const glm::vec3 fin[2] = {
            {0.84f, 0.56f, 0.55f}, // inner membrane
            {0.95f, 0.72f, 0.69f}, // bright edge
        };
        for (int i = 0; i < 4; ++i)
        {
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::AxolotlPink::BodyBase + i),
                          body[i], 0.30f - 0.03f * static_cast<float>(i));
        }
        for (int i = 0; i < 2; ++i)
        {
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::AxolotlPink::GillBase + i),
                          gill[i], 0.20f - 0.04f * static_cast<float>(i),
                          i == 1 ? 0.05f : 0.015f);
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::AxolotlPink::FinBase + i),
                          fin[i], 0.30f - 0.04f * static_cast<float>(i),
                          i == 1 ? 0.012f : 0.0f);
        }

        // detail band: freckles, cheek blush, belly cream, and the dark-rose mouth.
        const glm::vec3 detail[4] = {
            {0.86f, 0.55f, 0.52f}, // freckle rose
            {0.96f, 0.52f, 0.55f}, // cheek blush
            {0.99f, 0.93f, 0.87f}, // belly cream
            {0.30f, 0.08f, 0.10f}, // dark rose (mouth line)
        };
        const float detailRoughness[4] = {0.28f, 0.18f, 0.26f, 0.22f};
        const float detailEmissive[4] = {0.0f, 0.02f, 0.0f, 0.0f};
        for (int i = 0; i < 4; ++i)
        {
            setFishOpaque(static_cast<uint8_t>(AquariumMaterial::AxolotlPink::DetailBase + i),
                          detail[i], detailRoughness[i], detailEmissive[i]);
        }
    }
}

void VoxelPalette::buildCloudPalette()
{
    auto SM = [](uint32_t v) { return static_cast<float>(v); };

    // --- cloud palette (ids 100-115): 16 realistic cloud colors ---
    // designed for cumulus clouds with bright cores, gray shadows, and wispy edges

    // core white (ids 100-102): bright white cloud centers
    {
        const glm::vec3 coreColors[3] = {
            {1.00f, 1.00f, 1.00f},  // pure white
            {0.98f, 0.99f, 1.00f},  // slight blue tint (cool light)
            {1.00f, 0.99f, 0.97f},  // slight warm tint (sun-lit)
        };
        for (int i = 0; i < 3; ++i)
        {
            setEntry(0, 100 + i,
                     PaletteEntryCPU{glm::vec4(coreColors[i], 1.0f),
                                     glm::vec4(0.0f, 0.95f, 0.0f, SM(0)),
                                     glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
        }
    }

    // mid white (ids 103-106): graduated white-to-light-gray body
    {
        const glm::vec3 midColors[4] = {
            {0.94f, 0.95f, 0.98f},  // cool mid-white
            {0.90f, 0.91f, 0.94f},  // slightly darker
            {0.86f, 0.87f, 0.90f},  // transitional
            {0.82f, 0.83f, 0.88f},  // light gray-white
        };
        for (int i = 0; i < 4; ++i)
        {
            setEntry(0, 103 + i,
                     PaletteEntryCPU{glm::vec4(midColors[i], 1.0f),
                                     glm::vec4(0.0f, 0.92f, 0.0f, SM(0)),
                                     glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
        }
    }

    // edge gray (ids 107-110): shadow grays for depth and contour
    {
        const glm::vec3 edgeColors[4] = {
            {0.75f, 0.76f, 0.82f},  // light shadow
            {0.68f, 0.70f, 0.76f},  // medium shadow
            {0.60f, 0.62f, 0.70f},  // darker edge
            {0.52f, 0.55f, 0.65f},  // deep shadow
        };
        for (int i = 0; i < 4; ++i)
        {
            setEntry(0, 107 + i,
                     PaletteEntryCPU{glm::vec4(edgeColors[i], 1.0f),
                                     glm::vec4(0.0f, 0.88f, 0.0f, SM(0)),
                                     glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
        }
    }

    // Wispy/Translucent (ids 111-113): soft edges with transparency
    {
        const glm::vec4 wispyColors[3] = {
            {0.95f, 0.96f, 0.98f, 0.6f},  // light translucent
            {0.90f, 0.92f, 0.96f, 0.4f},  // more translucent
            {0.85f, 0.88f, 0.94f, 0.2f},  // very thin wisp
        };
        for (int i = 0; i < 3; ++i)
        {
            setEntry(0, 111 + i,
                     PaletteEntryCPU{wispyColors[i],
                                     glm::vec4(0.0f, 0.95f, 0.0f, SM(0)),
                                     glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
        }
    }

    // dark underbelly (ids 114-115): storm cloud accents for bottom shadows
    {
        const glm::vec3 darkColors[2] = {
            {0.45f, 0.48f, 0.55f},  // dark underbelly
            {0.38f, 0.42f, 0.50f},  // storm cloud accent
        };
        for (int i = 0; i < 2; ++i)
        {
            setEntry(0, 114 + i,
                     PaletteEntryCPU{glm::vec4(darkColors[i], 1.0f),
                                     glm::vec4(0.0f, 0.82f, 0.0f, SM(0)),
                                     glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
        }
    }
}

} // namespace engine
