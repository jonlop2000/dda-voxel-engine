#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueHandle.h"
#include "engine/render/voxel/TerrainShadowColumnEligibility.h"
#include "engine/render/voxel/TerrainShadowColumnGpuBuffer.h"
#include "engine/voxel/VolumeGpuBuffer.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelVolumeGpu.h"

struct VulkanContext;

namespace engine
{

class VoxelPalette;

struct VoxelInstance
{
    VoxelVolume volume{};
    uint32_t volumeIndex = 0;
    uint32_t patternType = 0;
    glm::vec3 debugTint{1.0f};
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    bool visible = true;
    std::vector<uint8_t> cpuVoxels{};
    uint64_t cpuVoxelRevision = 0;
    render::TerrainShadowColumnEligibility terrainShadowColumns{};
    render::TerrainShadowColumnGpuBuffer terrainShadowColumnBuffer{};
    uint64_t terrainShadowColumnRevision = 0;
    bool terrainShadowColumnGpuReady = false;

    [[nodiscard]] bool hasTerrainShadowColumns() const noexcept
    {
        return terrainShadowColumnGpuReady &&
               terrainShadowColumnRevision == cpuVoxelRevision &&
               terrainShadowColumns.eligible() &&
               terrainShadowColumnBuffer.valid();
    }
};

// volume specification for aquarium scene creation
struct VolumeSpec
{
    std::string name;
    glm::ivec3 dims;
    glm::vec3 position;
    uint32_t flags;
    glm::vec3 scale{1.0f};
    VoxelVolume::LightingOcclusionMode lightingOcclusionMode =
        VoxelVolume::LightingOcclusionMode::BinaryOpaque;
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

class VoxelWorld
{
public:
    bool initTestWorld(VulkanContext& ctx, const VoxelPalette& palette);
    bool initEmptyWorld(VulkanContext& ctx, const VoxelPalette& palette);

    // initialize with custom volume data (for aquarium scene)
    // getSpec(index) returns the volume specification
    // getData(index) returns the voxel data to upload
    bool initAquariumScene(VulkanContext& ctx, const VoxelPalette& palette, size_t volumeCount,
                           std::function<const VolumeSpec&(size_t)> getSpec,
                           std::function<const std::vector<uint8_t>&(size_t)> getData);

    // add a single volume after initialization (returns volume index or -1 on failure)
    int addVolume(VulkanContext& ctx, const VoxelPalette& palette,
                  const VolumeSpec& spec, const std::vector<uint8_t>& data);

    // releases scene-local volumes, descriptor sets/pool, and the volume buffer while
    // preserving the descriptor-set layout referenced by renderer pipelines.
    void clearScene(VulkanContext& ctx);
    void shutdown(VulkanContext& ctx);

    void updateGpuVolumes(VulkanContext& ctx);
    void buildSortedDrawList(const glm::vec3& cameraPos);

    const std::vector<uint32_t>& drawList() const { return m_drawList; }
    const std::vector<VoxelInstance>& instances() const { return m_instances; }
    std::vector<VoxelInstance>& instances() { return m_instances; }
    uint64_t structureRevision() const { return m_structureRevision; }
    bool gpuVolumesDirty() const { return m_gpuVolumesDirty; }
    bool drawListDirty() const { return m_drawListDirty; }

    bool setInstanceTransform(uint32_t instanceIndex, const glm::vec3& position,
                              const glm::quat& rotation, const glm::vec3& scale);
    bool setInstancePosition(uint32_t instanceIndex, const glm::vec3& position);
    glm::vec3 instancePosition(uint32_t instanceIndex) const;

    bool hasCpuVoxelData(uint32_t instanceIndex) const;
    uint8_t instanceVoxel(uint32_t instanceIndex, const glm::ivec3& localVoxel) const;
    bool setInstanceVoxel(uint32_t instanceIndex, const glm::ivec3& localVoxel, uint8_t id);
    bool replaceInstanceCpuVoxels(uint32_t instanceIndex, std::vector<uint8_t> data);
    bool uploadInstanceCpuVoxels(VulkanContext& ctx, uint32_t instanceIndex,
                                 VkBuffer paletteBuffer);

    // wrap offset for infinite scrolling (clouds)
    bool setVolumeWrapOffset(uint32_t instanceIndex, const glm::vec3& offset);
    glm::vec3 volumeWrapOffset(uint32_t instanceIndex) const;

    // volume visibility control
    bool isVolumeVisible(uint32_t index) const;
    void setVolumeVisible(uint32_t index, bool visible);

    VkDescriptorSetLayout voxelSetLayout() const { return m_voxelSetLayout.get(); }
    VkDescriptorSet descriptorSet(uint32_t instanceIndex) const;
    VkBuffer volumeBuffer() const { return m_volumeBuffer.buffer(); }
    void setMaterialAtlasDescriptor(const VkDescriptorImageInfo& info);

private:
    bool initDescriptors(VulkanContext& ctx, const VoxelPalette& palette, size_t maxVolumes);
    void destroySceneDescriptors(VulkanContext& ctx);

    VolumeGpuBuffer m_volumeBuffer{};
    std::vector<VoxelInstance> m_instances{};
    std::vector<uint32_t> m_drawList{};
    std::vector<uint32_t> m_lastDrawList{};
    std::vector<VolumeGpu> m_gpuVolumes{};
    uint64_t m_structureRevision = 0;
    bool m_gpuVolumesDirty = true;
    bool m_drawListDirty = true;
    glm::vec3 m_lastDrawListCameraPos{0.0f};
    bool m_hasLastDrawListCameraPos = false;

    // raii-owned with separate scene-pool and renderer-schema lifetimes.
    // each resource follows its corresponding owner.
    engine::render::UniqueDescriptorSetLayout m_voxelSetLayout{};
    engine::render::UniqueDescriptorPool m_voxelDescPool{};
    render::TerrainShadowColumnGpuBuffer m_terrainShadowColumnFallbackBuffer{};
    VkDescriptorImageInfo m_materialAtlasInfo{};
};

} // namespace engine
