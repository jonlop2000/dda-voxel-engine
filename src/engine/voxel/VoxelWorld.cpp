#include "engine/voxel/VoxelWorld.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Core/Logger.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "engine/render/voxel/TerrainShadowColumnEligibility.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelVolumeGpu.h"

namespace engine
{
namespace
{
float distanceToAabb(const glm::vec3& p, const glm::vec3& bmin, const glm::vec3& bmax)
{
    const glm::vec3 q = glm::clamp(p, bmin, bmax);
    return glm::length(q - p);
}

bool createVoxelDescriptorSet(VulkanContext& ctx, VkDescriptorSetLayout layout,
                              VkDescriptorPool pool, VkBuffer paletteBuffer,
                              VkBuffer volumeBuffer, const VkDescriptorImageInfo& voxelImage,
                              const VkDescriptorImageInfo& occImage,
                              const VkDescriptorImageInfo& materialAtlasImage,
                              VkBuffer terrainShadowColumnBuffer,
                              VkDescriptorSet& outSet)
{
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout;

    if (vkAllocateDescriptorSets(ctx.device, &alloc, &outSet) != VK_SUCCESS)
    {
        return false;
    }

    VkDescriptorBufferInfo paletteInfo{};
    paletteInfo.buffer = paletteBuffer;
    paletteInfo.offset = 0;
    paletteInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo volumeInfo{};
    volumeInfo.buffer = volumeBuffer;
    volumeInfo.offset = 0;
    volumeInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo terrainShadowColumnInfo{};
    terrainShadowColumnInfo.buffer = terrainShadowColumnBuffer;
    terrainShadowColumnInfo.offset = 0;
    terrainShadowColumnInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet writes[6]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = outSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &paletteInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = outSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &volumeInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = outSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &voxelImage;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = outSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].pImageInfo = &occImage;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = outSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].pImageInfo = &materialAtlasImage;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = outSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].pBufferInfo = &terrainShadowColumnInfo;

    vkUpdateDescriptorSets(ctx.device, 6, writes, 0, nullptr);
    return true;
}

void updateTerrainShadowColumnDescriptor(VulkanContext& ctx,
                                         VoxelInstance& instance,
                                         VkBuffer fallbackBuffer)
{
    if (instance.descriptorSet == VK_NULL_HANDLE)
    {
        return;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = instance.terrainShadowColumnBuffer.valid()
                            ? instance.terrainShadowColumnBuffer.buffer()
                            : fallbackBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = instance.descriptorSet;
    write.dstBinding = 5;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
}

size_t voxelElementCount(const glm::ivec3& dims)
{
    if (dims.x <= 0 || dims.y <= 0 || dims.z <= 0)
    {
        return 0;
    }

    return static_cast<size_t>(dims.x) * static_cast<size_t>(dims.y) *
           static_cast<size_t>(dims.z);
}

bool containsVoxel(const glm::ivec3& dims, const glm::ivec3& voxel)
{
    return voxel.x >= 0 && voxel.y >= 0 && voxel.z >= 0 &&
           voxel.x < dims.x && voxel.y < dims.y && voxel.z < dims.z;
}

bool finiteVec3(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool normalizedVolumeRotation(const glm::quat& rotation, glm::quat& outRotation)
{
    if (!std::isfinite(rotation.w) || !std::isfinite(rotation.x) ||
        !std::isfinite(rotation.y) || !std::isfinite(rotation.z))
    {
        return false;
    }

    const float lengthSquared = glm::dot(rotation, rotation);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-12f)
    {
        return false;
    }

    outRotation = rotation / std::sqrt(lengthSquared);
    return true;
}

bool validVolumeTransform(const glm::vec3& position, const glm::quat& rotation,
                          const glm::vec3& scale, glm::quat& outRotation)
{
    return finiteVec3(position) && finiteVec3(scale) && scale.x > 0.0f &&
           scale.y > 0.0f && scale.z > 0.0f &&
           std::isfinite(1.0f / scale.x) &&
           std::isfinite(1.0f / scale.y) &&
           std::isfinite(1.0f / scale.z) &&
           normalizedVolumeRotation(rotation, outRotation);
}

size_t voxelIndex(const glm::ivec3& dims, const glm::ivec3& voxel)
{
    return static_cast<size_t>(voxel.x) +
           static_cast<size_t>(voxel.y) * static_cast<size_t>(dims.x) +
           static_cast<size_t>(voxel.z) * static_cast<size_t>(dims.x) *
               static_cast<size_t>(dims.y);
}

void rebuildTerrainShadowColumns(VulkanContext& ctx, VoxelInstance& instance)
{
    render::TerrainShadowColumnInput input{};
    input.dimensions = instance.volume.dimensions();
    input.volumeFlags = instance.volume.flags();
    input.lightingOcclusionMode = instance.volume.lightingOcclusionMode();
    input.voxels = instance.cpuVoxels;

    render::TerrainShadowColumnEligibility eligibility =
        render::classifyTerrainShadowColumns(input);
    bool gpuReady = false;
    if (eligibility.eligible())
    {
        gpuReady = instance.terrainShadowColumnBuffer.upload(
            ctx, eligibility.topYExclusive);
        if (!gpuReady)
        {
            logWarning(
                "VoxelWorld",
                makeLogMessage(
                    "Terrain shadow-column upload failed for volume '",
                    instance.volume.debugName(),
                    "'; retaining the existing 3D DDA fallback."));
        }
    }

    instance.terrainShadowColumns = std::move(eligibility);
    instance.terrainShadowColumnRevision = instance.cpuVoxelRevision;
    instance.terrainShadowColumnGpuReady = gpuReady;
}
} // namespace

bool VoxelWorld::initDescriptors(VulkanContext& ctx, const VoxelPalette& palette, size_t maxVolumes)
{
    if (!palette.isValid())
    {
        return false;
    }
    if (m_materialAtlasInfo.imageView == VK_NULL_HANDLE ||
        m_materialAtlasInfo.sampler == VK_NULL_HANDLE)
    {
        logError("VoxelWorld", "Voxel material atlas descriptor was not configured.");
        return false;
    }
    if (m_voxelDescPool || m_volumeBuffer.buffer() != VK_NULL_HANDLE ||
        m_terrainShadowColumnFallbackBuffer.valid())
    {
        logError("VoxelWorld",
                 "Scene descriptors must be cleared before initializing a new voxel scene.");
        return false;
    }

    if (!m_volumeBuffer.create(ctx, maxVolumes))
    {
        return false;
    }

    constexpr uint32_t fallbackTerrainShadowColumn = 0u;
    if (!m_terrainShadowColumnFallbackBuffer.upload(
            ctx, std::span<const uint32_t>(&fallbackTerrainShadowColumn, 1)))
    {
        m_volumeBuffer.destroy(ctx);
        return false;
    }

    bool createdLayout = false;
    if (!m_voxelSetLayout)
    {
        VkDescriptorSetLayoutBinding paletteBinding{};
        paletteBinding.binding = 0;
        paletteBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        paletteBinding.descriptorCount = 1;
        paletteBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding volumeBinding{};
        volumeBinding.binding = 1;
        volumeBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        volumeBinding.descriptorCount = 1;
        volumeBinding.stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
            VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding texBinding{};
        texBinding.binding = 2;
        texBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texBinding.descriptorCount = 1;
        texBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding occBinding{};
        occBinding.binding = 3;
        occBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        occBinding.descriptorCount = 1;
        occBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding materialAtlasBinding{};
        materialAtlasBinding.binding = 4;
        materialAtlasBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        materialAtlasBinding.descriptorCount = 1;
        materialAtlasBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding terrainShadowColumnBinding{};
        terrainShadowColumnBinding.binding = 5;
        terrainShadowColumnBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        terrainShadowColumnBinding.descriptorCount = 1;
        terrainShadowColumnBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bindings[] = {paletteBinding, volumeBinding, texBinding,
                                                   occBinding, materialAtlasBinding,
                                                   terrainShadowColumnBinding};
        VkDescriptorSetLayoutCreateInfo layoutInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<uint32_t>(sizeof(bindings) / sizeof(bindings[0]));
        layoutInfo.pBindings = bindings;

        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        if (vkCreateDescriptorSetLayout(ctx.device, &layoutInfo, nullptr, &setLayout) !=
            VK_SUCCESS)
        {
            m_terrainShadowColumnFallbackBuffer.reset();
            m_volumeBuffer.destroy(ctx);
            return false;
        }
        m_voxelSetLayout = engine::render::UniqueDescriptorSetLayout(ctx.device, setLayout);
        createdLayout = true;
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(maxVolumes * 3);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(maxVolumes * 3);

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = static_cast<uint32_t>(maxVolumes);

    VkDescriptorPool descPool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &descPool) != VK_SUCCESS)
    {
        m_terrainShadowColumnFallbackBuffer.reset();
        m_volumeBuffer.destroy(ctx);
        if (createdLayout)
        {
            m_voxelSetLayout.reset();
        }
        return false;
    }
    m_voxelDescPool = engine::render::UniqueDescriptorPool(ctx.device, descPool);

    return true;
}

bool VoxelWorld::initTestWorld(VulkanContext& ctx, const VoxelPalette& palette)
{
    const size_t maxVolumes = 64;
    if (!initDescriptors(ctx, palette, maxVolumes))
    {
        return false;
    }

    m_instances.clear();
    m_instances.reserve(2);
    ++m_structureRevision;

    VoxelVolume::CreateInfo a{};
    a.dimensions = {32, 32, 32};
    a.worldPosition = {0.0f, 0.0f, 0.0f};
    a.worldScale = {1.0f, 1.0f, 1.0f};
    a.worldRotation = glm::quat(1, 0, 0, 0);
    a.paletteId = 0;
    a.flags = VoxelVolume::FLAG_STATIC;
    a.debugName = "TestVolumeA";

    VoxelInstance instA{};
    if (!instA.volume.create(ctx, a))
    {
        clearScene(ctx);
        return false;
    }
    instA.patternType = 2;
    if (!instA.volume.fillTestPatternAndUpload(ctx, static_cast<int>(instA.patternType),
                                              palette.buffer()))
    {
        instA.volume.destroy(ctx);
        clearScene(ctx);
        return false;
    }
    instA.volumeIndex = 0;

    VoxelVolume::CreateInfo b = a;
    b.worldPosition = {16.0f, 0.0f, 8.0f};
    b.debugName = "TestVolumeB";

    VoxelInstance instB{};
    if (!instB.volume.create(ctx, b))
    {
        instA.volume.destroy(ctx);
        clearScene(ctx);
        return false;
    }
    instB.patternType = 1;
    if (!instB.volume.fillTestPatternAndUpload(ctx, static_cast<int>(instB.patternType),
                                              palette.buffer()))
    {
        instB.volume.destroy(ctx);
        instA.volume.destroy(ctx);
        clearScene(ctx);
        return false;
    }
    instB.volumeIndex = 1;

    m_instances.push_back(std::move(instA));
    m_instances.push_back(std::move(instB));
    m_gpuVolumesDirty = true;
    m_drawListDirty = true;
    m_hasLastDrawListCameraPos = false;

    for (size_t i = 0; i < m_instances.size(); ++i)
    {
        VoxelInstance& inst = m_instances[i];
        inst.volumeIndex = static_cast<uint32_t>(i);
        const VkDescriptorImageInfo imageInfo = inst.volume.descriptorInfo();
        const VkDescriptorImageInfo occInfo = inst.volume.occupancyDescriptorInfo();
        if (!createVoxelDescriptorSet(ctx, m_voxelSetLayout.get(), m_voxelDescPool.get(), palette.buffer(),
                                      m_volumeBuffer.buffer(), imageInfo, occInfo,
                                      m_materialAtlasInfo,
                                      m_terrainShadowColumnFallbackBuffer.buffer(),
                                      inst.descriptorSet))
        {
            clearScene(ctx);
            return false;
        }
    }

    updateGpuVolumes(ctx);
    buildSortedDrawList(glm::vec3(0.0f));
    return true;
}

bool VoxelWorld::initEmptyWorld(VulkanContext& ctx, const VoxelPalette& palette)
{
    const size_t maxVolumes = 64;
    if (!initDescriptors(ctx, palette, maxVolumes))
    {
        return false;
    }

    m_instances.clear();
    m_drawList.clear();
    m_lastDrawList.clear();
    m_gpuVolumes.clear();
    ++m_structureRevision;
    m_gpuVolumesDirty = true;
    m_drawListDirty = true;
    m_hasLastDrawListCameraPos = false;
    return true;
}

bool VoxelWorld::initAquariumScene(VulkanContext& ctx, const VoxelPalette& palette,
                                    size_t volumeCount,
                                    std::function<const VolumeSpec&(size_t)> getSpec,
                                    std::function<const std::vector<uint8_t>&(size_t)> getData)
{
    constexpr size_t kDefaultMaxVolumes = 160;
    constexpr size_t kMaxAllowedVolumes = 256;
    if (volumeCount > kMaxAllowedVolumes)
    {
        logError("VoxelWorld",
                 makeLogMessage("Too many volumes for voxel scene: ", volumeCount, " > ",
                                kMaxAllowedVolumes));
        return false;
    }

    const size_t maxVolumes = std::max(volumeCount, kDefaultMaxVolumes);

    if (!initDescriptors(ctx, palette, maxVolumes))
    {
        return false;
    }

    m_instances.clear();
    m_instances.reserve(maxVolumes);
    ++m_structureRevision;
    m_gpuVolumesDirty = true;
    m_drawListDirty = true;
    m_hasLastDrawListCameraPos = false;

    // create each volume
    for (size_t i = 0; i < volumeCount; ++i)
    {
        const VolumeSpec& spec = getSpec(i);
        const std::vector<uint8_t>& data = getData(i);

        glm::quat normalizedRotation{};
        if (!validVolumeTransform(spec.position, spec.rotation, spec.scale,
                                  normalizedRotation))
        {
            logError("VoxelWorld",
                     makeLogMessage("Invalid transform for volume: ", spec.name));
            clearScene(ctx);
            return false;
        }

        VoxelVolume::CreateInfo info{};
        info.dimensions = spec.dims;
        info.worldPosition = spec.position;
        info.worldScale = spec.scale;
        info.worldRotation = normalizedRotation;
        info.paletteId = 0;
        info.flags = spec.flags;
        info.lightingOcclusionMode = spec.lightingOcclusionMode;
        info.debugName = spec.name;

        VoxelInstance inst{};
        if (!inst.volume.create(ctx, info))
        {
            logError("VoxelWorld", makeLogMessage("Failed to create volume: ", spec.name));
            clearScene(ctx);
            return false;
        }

        if (!inst.volume.upload(ctx, data, palette.buffer()))
        {
            logError("VoxelWorld", makeLogMessage("Failed to upload volume data: ", spec.name));
            inst.volume.destroy(ctx);
            clearScene(ctx);
            return false;
        }

        inst.volumeIndex = static_cast<uint32_t>(i);
        inst.visible = true;
        inst.cpuVoxels = data;
        inst.cpuVoxelRevision = 1;
        rebuildTerrainShadowColumns(ctx, inst);
        m_instances.push_back(std::move(inst));
        m_gpuVolumesDirty = true;
        m_drawListDirty = true;

        logInfo("VoxelWorld",
                makeLogMessage("Created aquarium volume: ", spec.name, " (", spec.dims.x, "x",
                               spec.dims.y, "x", spec.dims.z, ")"));
    }

    // create descriptor sets for each volume
    for (size_t i = 0; i < m_instances.size(); ++i)
    {
        VoxelInstance& inst = m_instances[i];
        inst.volumeIndex = static_cast<uint32_t>(i);
        const VkDescriptorImageInfo imageInfo = inst.volume.descriptorInfo();
        const VkDescriptorImageInfo occInfo = inst.volume.occupancyDescriptorInfo();
        const VkBuffer terrainShadowColumnBuffer =
            inst.terrainShadowColumnBuffer.valid()
                ? inst.terrainShadowColumnBuffer.buffer()
                : m_terrainShadowColumnFallbackBuffer.buffer();
        if (!createVoxelDescriptorSet(ctx, m_voxelSetLayout.get(), m_voxelDescPool.get(), palette.buffer(),
                                      m_volumeBuffer.buffer(), imageInfo, occInfo,
                                      m_materialAtlasInfo,
                                      terrainShadowColumnBuffer,
                                      inst.descriptorSet))
        {
            logError("VoxelWorld",
                     makeLogMessage("Failed to create descriptor set for volume ", i));
            clearScene(ctx);
            return false;
        }
    }

    updateGpuVolumes(ctx);
    buildSortedDrawList(glm::vec3(0.0f));

    logInfo("VoxelWorld",
            makeLogMessage("Aquarium scene initialized with ", volumeCount, " volumes"));
    return true;
}

int VoxelWorld::addVolume(VulkanContext& ctx, const VoxelPalette& palette,
                          const VolumeSpec& spec, const std::vector<uint8_t>& data)
{
    // ensure descriptors are initialized
    if (!m_voxelSetLayout || !m_voxelDescPool)
    {
        logError("VoxelWorld", "VoxelWorld not initialized - call init*() first.");
        return -1;
    }

    // check if we have room for another volume
    const size_t newIndex = m_instances.size();
    constexpr size_t kMaxAllowedVolumes = 256;
    if (newIndex >= kMaxAllowedVolumes)
    {
        logError("VoxelWorld",
                 makeLogMessage("Maximum volume count exceeded: ", newIndex));
        return -1;
    }

    // create volume
    glm::quat normalizedRotation{};
    if (!validVolumeTransform(spec.position, spec.rotation, spec.scale,
                              normalizedRotation))
    {
        logError("VoxelWorld",
                 makeLogMessage("Invalid transform for volume: ", spec.name));
        return -1;
    }

    VoxelVolume::CreateInfo info{};
    info.dimensions = spec.dims;
    info.worldPosition = spec.position;
    info.worldScale = spec.scale;
    info.worldRotation = normalizedRotation;
    info.paletteId = 0;
    info.flags = spec.flags;
    info.lightingOcclusionMode = spec.lightingOcclusionMode;
    info.debugName = spec.name;

    VoxelInstance inst{};
    if (!inst.volume.create(ctx, info))
    {
        logError("VoxelWorld", makeLogMessage("Failed to create volume: ", spec.name));
        return -1;
    }

    if (!inst.volume.upload(ctx, data, palette.buffer()))
    {
        logError("VoxelWorld", makeLogMessage("Failed to upload volume data: ", spec.name));
        inst.volume.destroy(ctx);
        return -1;
    }

    inst.volumeIndex = static_cast<uint32_t>(newIndex);
    inst.visible = true;
    inst.cpuVoxels = data;
    inst.cpuVoxelRevision = 1;
    rebuildTerrainShadowColumns(ctx, inst);

    // create descriptor set
    const VkDescriptorImageInfo imageInfo = inst.volume.descriptorInfo();
    const VkDescriptorImageInfo occInfo = inst.volume.occupancyDescriptorInfo();
    const VkBuffer terrainShadowColumnBuffer =
        inst.terrainShadowColumnBuffer.valid()
            ? inst.terrainShadowColumnBuffer.buffer()
            : m_terrainShadowColumnFallbackBuffer.buffer();
    if (!createVoxelDescriptorSet(ctx, m_voxelSetLayout.get(), m_voxelDescPool.get(), palette.buffer(),
                                  m_volumeBuffer.buffer(), imageInfo, occInfo,
                                  m_materialAtlasInfo,
                                  terrainShadowColumnBuffer,
                                  inst.descriptorSet))
    {
        logError("VoxelWorld",
                 makeLogMessage("Failed to create descriptor set for volume: ", spec.name));
        inst.volume.destroy(ctx);
        return -1;
    }

    m_instances.push_back(std::move(inst));
    ++m_structureRevision;
    m_gpuVolumesDirty = true;
    m_drawListDirty = true;
    m_hasLastDrawListCameraPos = false;

    logInfo("VoxelWorld",
            makeLogMessage("Added volume: ", spec.name, " (", spec.dims.x, "x", spec.dims.y,
                           "x", spec.dims.z, ") at index ", newIndex));

    // update gpu data
    updateGpuVolumes(ctx);

    return static_cast<int>(newIndex);
}

void VoxelWorld::clearScene(VulkanContext& ctx)
{
    // release descriptor sets before the images and buffers they reference.
    // renderer pipelines retain the compatible set layout across scene rebuilds.
    destroySceneDescriptors(ctx);
    for (auto& inst : m_instances)
    {
        if (inst.volume.isValid())
        {
            inst.volume.destroy(ctx);
        }
        inst.descriptorSet = VK_NULL_HANDLE;
    }
    m_instances.clear();
    m_drawList.clear();
    m_lastDrawList.clear();
    m_gpuVolumes.clear();
    ++m_structureRevision;
    m_gpuVolumesDirty = true;
    m_drawListDirty = true;
    m_hasLastDrawListCameraPos = false;
    m_terrainShadowColumnFallbackBuffer.reset();
    m_volumeBuffer.destroy(ctx);
}

void VoxelWorld::shutdown(VulkanContext& ctx)
{
    clearScene(ctx);
    m_voxelSetLayout.reset();
}

void VoxelWorld::updateGpuVolumes(VulkanContext& ctx)
{
    (void)ctx;
    if (!m_gpuVolumesDirty)
    {
        return;
    }
    if (m_instances.empty())
    {
        m_gpuVolumes.clear();
        m_gpuVolumesDirty = false;
        return;
    }

    std::vector<uint32_t> neighborMask(m_instances.size(), 0u);
    if (m_instances.size() > 1)
    {
        const float eps = 1e-4f;
        auto overlaps = [eps](float a0, float a1, float b0, float b1) -> bool
        {
            const float lo = std::max(a0, b0);
            const float hi = std::min(a1, b1);
            return (hi - lo) > eps;
        };

        for (size_t i = 0; i < m_instances.size(); ++i)
        {
            const auto& a = m_instances[i].volume;
            const glm::vec3 aMin = a.worldAabbMin();
            const glm::vec3 aMax = a.worldAabbMax();
            uint32_t mask = 0u;

            for (size_t j = 0; j < m_instances.size(); ++j)
            {
                if (i == j)
                {
                    continue;
                }
                const auto& b = m_instances[j].volume;
                const glm::vec3 bMin = b.worldAabbMin();
                const glm::vec3 bMax = b.worldAabbMax();

                if (std::abs(aMax.x - bMin.x) < eps &&
                    overlaps(aMin.y, aMax.y, bMin.y, bMax.y) &&
                    overlaps(aMin.z, aMax.z, bMin.z, bMax.z))
                {
                    mask |= 1u; // +x neighbor
                }
                if (std::abs(aMax.y - bMin.y) < eps &&
                    overlaps(aMin.x, aMax.x, bMin.x, bMax.x) &&
                    overlaps(aMin.z, aMax.z, bMin.z, bMax.z))
                {
                    mask |= 2u; // +y neighbor
                }
                if (std::abs(aMax.z - bMin.z) < eps &&
                    overlaps(aMin.x, aMax.x, bMin.x, bMax.x) &&
                    overlaps(aMin.y, aMax.y, bMin.y, bMax.y))
                {
                    mask |= 4u; // +z neighbor
                }
            }

            neighborMask[i] = mask;
        }
    }

    m_gpuVolumes.resize(m_instances.size());
    for (size_t i = 0; i < m_instances.size(); ++i)
    {
        const auto& inst = m_instances[i];
        VolumeGpu gpu{};
        const glm::mat3 normalMat3 =
            glm::transpose(glm::inverse(glm::mat3(inst.volume.worldFromLocal())));
        gpu.worldFromLocal = inst.volume.worldFromLocal();
        gpu.localFromWorld = inst.volume.localFromWorld();
        gpu.normalFromLocal = glm::mat4(1.0f);
        gpu.normalFromLocal[0] = glm::vec4(normalMat3[0], 0.0f);
        gpu.normalFromLocal[1] = glm::vec4(normalMat3[1], 0.0f);
        gpu.normalFromLocal[2] = glm::vec4(normalMat3[2], 0.0f);
        gpu.worldAabbMin = glm::vec4(inst.volume.worldAabbMin(), 0.0f);
        gpu.worldAabbMax = glm::vec4(inst.volume.worldAabbMax(), 0.0f);
        const glm::ivec3 dims = inst.volume.dimensions();
        gpu.dims_palette_flags = glm::uvec4(static_cast<uint32_t>(dims.x),
                                            static_cast<uint32_t>(dims.y),
                                            static_cast<uint32_t>(dims.z),
                                            inst.volume.paletteId());
        const uint32_t gpuFeatureFlags = inst.hasTerrainShadowColumns()
                                             ? kVolumeGpuTerrainShadowColumns
                                             : 0u;
        gpu.misc = glm::uvec4(
            inst.volume.flags(), neighborMask[i],
            static_cast<uint32_t>(inst.volume.lightingOcclusionMode()),
            gpuFeatureFlags);
        gpu.wrapOffset = glm::vec4(inst.volume.wrapOffset(), 0.0f);
        const glm::ivec3 occupiedMin = inst.volume.occupiedLocalMin();
        const glm::ivec3 occupiedMaxExclusive = inst.volume.occupiedLocalMaxExclusive();
        gpu.occupiedMin = glm::uvec4(static_cast<uint32_t>(std::max(occupiedMin.x, 0)),
                                     static_cast<uint32_t>(std::max(occupiedMin.y, 0)),
                                     static_cast<uint32_t>(std::max(occupiedMin.z, 0)), 0u);
        gpu.occupiedMaxExclusive =
            glm::uvec4(static_cast<uint32_t>(std::max(occupiedMaxExclusive.x, 0)),
                       static_cast<uint32_t>(std::max(occupiedMaxExclusive.y, 0)),
                       static_cast<uint32_t>(std::max(occupiedMaxExclusive.z, 0)), 0u);
        m_gpuVolumes[i] = gpu;
    }

    m_volumeBuffer.upload(m_gpuVolumes.data(), m_gpuVolumes.size() * sizeof(VolumeGpu));
    m_gpuVolumesDirty = false;
}

void VoxelWorld::buildSortedDrawList(const glm::vec3& cameraPos)
{
    if (!m_drawListDirty && m_hasLastDrawListCameraPos &&
        cameraPos.x == m_lastDrawListCameraPos.x &&
        cameraPos.y == m_lastDrawListCameraPos.y &&
        cameraPos.z == m_lastDrawListCameraPos.z)
    {
        return;
    }

    m_drawList.clear();
    m_drawList.reserve(m_instances.size());

    // only include visible volumes that actually contain voxels. empty volumes are kept for
    // stable scene indexing, but they should not rasterize obbs or launch dda work.
    for (size_t i = 0; i < m_instances.size(); ++i)
    {
        if (m_instances[i].visible && m_instances[i].volume.hasOccupiedVoxels())
        {
            m_drawList.push_back(static_cast<uint32_t>(i));
        }
    }

    std::vector<float> distances(m_instances.size(), 0.0f);
    for (size_t i = 0; i < m_instances.size(); ++i)
    {
        const auto& inst = m_instances[i];
        distances[i] = distanceToAabb(cameraPos, inst.volume.worldAabbMin(),
                                      inst.volume.worldAabbMax());
    }

    std::stable_sort(m_drawList.begin(), m_drawList.end(),
                     [&distances](uint32_t a, uint32_t b)
                     {
                         return distances[a] < distances[b];
                     });

    m_lastDrawList = m_drawList;

    m_lastDrawListCameraPos = cameraPos;
    m_hasLastDrawListCameraPos = true;
    m_drawListDirty = false;
}

bool VoxelWorld::isVolumeVisible(uint32_t index) const
{
    if (index >= m_instances.size())
    {
        return false;
    }
    return m_instances[index].visible;
}

void VoxelWorld::setVolumeVisible(uint32_t index, bool visible)
{
    if (index < m_instances.size() && m_instances[index].visible != visible)
    {
        m_instances[index].visible = visible;
        m_drawListDirty = true;
    }
}

bool VoxelWorld::setInstanceTransform(uint32_t instanceIndex, const glm::vec3& position,
                                      const glm::quat& rotation, const glm::vec3& scale)
{
    if (instanceIndex >= m_instances.size())
    {
        return false;
    }

    glm::quat normalizedRotation{};
    if (!validVolumeTransform(position, rotation, scale, normalizedRotation))
    {
        return false;
    }

    auto& inst = m_instances[instanceIndex];
    inst.volume.updateTransform(position, normalizedRotation, scale);
    m_gpuVolumesDirty = true;
    m_drawListDirty = true;
    return true;
}

bool VoxelWorld::setInstancePosition(uint32_t instanceIndex, const glm::vec3& position)
{
    if (instanceIndex >= m_instances.size())
    {
        return false;
    }

    const auto& inst = m_instances[instanceIndex];
    return setInstanceTransform(instanceIndex, position, inst.volume.worldRotation(),
                                inst.volume.worldScale());
}

glm::vec3 VoxelWorld::instancePosition(uint32_t instanceIndex) const
{
    if (instanceIndex >= m_instances.size())
    {
        return glm::vec3(0.0f);
    }

    return m_instances[instanceIndex].volume.worldPosition();
}

bool VoxelWorld::hasCpuVoxelData(uint32_t instanceIndex) const
{
    if (instanceIndex >= m_instances.size())
    {
        return false;
    }

    const VoxelInstance& inst = m_instances[instanceIndex];
    return inst.cpuVoxels.size() == voxelElementCount(inst.volume.dimensions());
}

uint8_t VoxelWorld::instanceVoxel(uint32_t instanceIndex, const glm::ivec3& localVoxel) const
{
    if (!hasCpuVoxelData(instanceIndex))
    {
        return 0u;
    }

    const VoxelInstance& inst = m_instances[instanceIndex];
    const glm::ivec3 dims = inst.volume.dimensions();
    if (!containsVoxel(dims, localVoxel))
    {
        return 0u;
    }

    return inst.cpuVoxels[voxelIndex(dims, localVoxel)];
}

bool VoxelWorld::setInstanceVoxel(uint32_t instanceIndex, const glm::ivec3& localVoxel,
                                  uint8_t id)
{
    if (!hasCpuVoxelData(instanceIndex))
    {
        return false;
    }

    VoxelInstance& inst = m_instances[instanceIndex];
    const glm::ivec3 dims = inst.volume.dimensions();
    if (!containsVoxel(dims, localVoxel))
    {
        return false;
    }

    const size_t index = voxelIndex(dims, localVoxel);
    if (inst.cpuVoxels[index] == id)
    {
        return false;
    }

    inst.cpuVoxels[index] = id;
    ++inst.cpuVoxelRevision;
    m_gpuVolumesDirty = true;
    m_drawListDirty = true;
    m_hasLastDrawListCameraPos = false;
    return true;
}

bool VoxelWorld::replaceInstanceCpuVoxels(uint32_t instanceIndex, std::vector<uint8_t> data)
{
    if (instanceIndex >= m_instances.size())
    {
        return false;
    }

    VoxelInstance& inst = m_instances[instanceIndex];
    if (data.size() != voxelElementCount(inst.volume.dimensions()))
    {
        return false;
    }
    if (inst.cpuVoxels == data)
    {
        return false;
    }

    inst.cpuVoxels = std::move(data);
    ++inst.cpuVoxelRevision;
    m_gpuVolumesDirty = true;
    m_drawListDirty = true;
    m_hasLastDrawListCameraPos = false;
    return true;
}

bool VoxelWorld::uploadInstanceCpuVoxels(VulkanContext& ctx, uint32_t instanceIndex,
                                         VkBuffer paletteBuffer)
{
    if (!hasCpuVoxelData(instanceIndex))
    {
        return false;
    }

    VoxelInstance& inst = m_instances[instanceIndex];
    if (!inst.volume.upload(ctx, inst.cpuVoxels, paletteBuffer))
    {
        return false;
    }

    rebuildTerrainShadowColumns(ctx, inst);
    updateTerrainShadowColumnDescriptor(
        ctx, inst, m_terrainShadowColumnFallbackBuffer.buffer());

    m_gpuVolumesDirty = true;
    m_drawListDirty = true;
    m_hasLastDrawListCameraPos = false;
    return true;
}

bool VoxelWorld::setVolumeWrapOffset(uint32_t instanceIndex, const glm::vec3& offset)
{
    if (instanceIndex >= m_instances.size())
    {
        return false;
    }

    m_instances[instanceIndex].volume.setWrapOffset(offset);
    m_gpuVolumesDirty = true;
    return true;
}

glm::vec3 VoxelWorld::volumeWrapOffset(uint32_t instanceIndex) const
{
    if (instanceIndex >= m_instances.size())
    {
        return glm::vec3(0.0f);
    }

    return m_instances[instanceIndex].volume.wrapOffset();
}

VkDescriptorSet VoxelWorld::descriptorSet(uint32_t instanceIndex) const
{
    if (instanceIndex >= m_instances.size())
    {
        return VK_NULL_HANDLE;
    }
    return m_instances[instanceIndex].descriptorSet;
}

void VoxelWorld::setMaterialAtlasDescriptor(const VkDescriptorImageInfo& info)
{
    m_materialAtlasInfo = info;
}

void VoxelWorld::destroySceneDescriptors(VulkanContext& ctx)
{
    // descriptor sets are implicitly released with the pool. the layout remains
    // stable across scene rebuilds because renderer pipelines reference it.
    (void)ctx;
    m_voxelDescPool.reset();
}

} // namespace engine
