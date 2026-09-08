#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp> // important: glm::quat

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;

namespace engine
{

/**
 * VoxelVolume
 * ----------
 * stores a bounded voxel grid as a gpu 3D texture (R8_UINT).
 * each texel is a palette index: 0 = empty, 1..255 = material ids.
 *
 * layout tracking:
 *   - starts undefined
 *   - after upload becomes SHADER_READ_ONLY_OPTIMAL
 *   - supports subsequent uploads safely
 */
class VoxelVolume
{
  public:
    enum class LightingOcclusionMode : uint32_t
    {
        BinaryOpaque = 0,
        None = 1,
        TranslucentFoliage = 2,
        // sparse, static, color-invisible mass routed only to AO/local-light tracing.
        // the corresponding visible canopy remains a separate TranslucentFoliage volume.
        Proxy = 3,
        // visible voxel fallback for instanced meadow foliage. when the instanced
        // renderer replaces its color path, this volume remains available as a
        // lightweight translucent sun-shadow proxy.
        TranslucentMeadow = 4,
    };

    struct CreateInfo
    {
        glm::ivec3 dimensions = {0, 0, 0}; // voxel resolution: e.g. 64x64x64
        glm::vec3 worldPosition = {0, 0, 0};
        glm::vec3 worldScale = {1, 1, 1}; // 1 voxel == 1 unit (for now)
        glm::quat worldRotation = glm::quat(1, 0, 0, 0);

        uint32_t paletteId = 0;
        uint32_t flags = 0;
        LightingOcclusionMode lightingOcclusionMode = LightingOcclusionMode::BinaryOpaque;
        std::string debugName;
    };

    static constexpr uint32_t FLAG_STATIC = 0x01;
    static constexpr uint32_t FLAG_DYNAMIC = 0x02;
    // conservative material declarations: set when any voxel in the volume may use the
    // corresponding transmissive shading model. the primary dda hot path relies on these
    // flags before bypassing traversal-time material classification.
    static constexpr uint32_t FLAG_GLASS = 0x04;
    static constexpr uint32_t FLAG_WATER = 0x08;
    static constexpr uint32_t FLAG_WRAP_XZ = 0x10; // wrap x and z coordinates (for infinite clouds)
    static constexpr uint32_t FLAG_CLOUD = 0x20;   // cloud volume (sun transmittance attenuation)
    static constexpr uint32_t FLAG_ALLOW_DENSE_SKIP =
        0x40; // validated for precomputed exact dense empty jumps

    VoxelVolume() = default;
    ~VoxelVolume() = default;

    VoxelVolume(const VoxelVolume&) = delete;
    VoxelVolume& operator=(const VoxelVolume&) = delete;

    VoxelVolume(VoxelVolume&&) noexcept;
    VoxelVolume& operator=(VoxelVolume&&) noexcept;

    bool create(::VulkanContext& ctx, const CreateInfo& info);
    void destroy(::VulkanContext& ctx);

    bool upload(::VulkanContext& ctx, const uint8_t* data, size_t bytes,
                VkBuffer paletteBuffer);
    bool upload(::VulkanContext& ctx, const std::vector<uint8_t>& data,
                VkBuffer paletteBuffer);

    // optional but strongly recommended for validation:
    bool download(::VulkanContext& ctx, std::vector<uint8_t>& outData) const;

    // simple procedural patterns for testing
    bool fillTestPatternAndUpload(::VulkanContext& ctx, int patternType,
                                  VkBuffer paletteBuffer);

    void updateTransform(const glm::vec3& position, const glm::quat& rotation,
                         const glm::vec3& scale);

    bool isValid() const
    {
        return static_cast<bool>(m_image);
    }

    glm::ivec3 dimensions() const
    {
        return m_dimensions;
    }

    const glm::mat4& worldFromLocal() const
    {
        return m_worldFromLocal;
    }
    const glm::mat4& localFromWorld() const
    {
        return m_localFromWorld;
    }

    glm::vec3 localMin() const
    {
        return {0, 0, 0};
    }
    glm::vec3 localMax() const
    {
        return glm::vec3(m_dimensions);
    }

    glm::vec3 worldAabbMin() const
    {
        return m_worldAabbMin;
    }
    glm::vec3 worldAabbMax() const
    {
        return m_worldAabbMax;
    }
    glm::ivec3 occupiedLocalMin() const
    {
        return m_occupiedLocalMin;
    }
    glm::ivec3 occupiedLocalMaxExclusive() const
    {
        return m_occupiedLocalMaxExclusive;
    }
    bool hasOccupiedVoxels() const
    {
        return m_occupiedLocalMaxExclusive.x > m_occupiedLocalMin.x &&
               m_occupiedLocalMaxExclusive.y > m_occupiedLocalMin.y &&
               m_occupiedLocalMaxExclusive.z > m_occupiedLocalMin.z;
    }

    glm::vec3 worldPosition() const
    {
        return m_worldPosition;
    }
    glm::quat worldRotation() const
    {
        return m_worldRotation;
    }
    glm::vec3 worldScale() const
    {
        return m_worldScale;
    }

    uint32_t paletteId() const
    {
        return m_paletteId;
    }
    uint32_t flags() const
    {
        return m_flags;
    }
    LightingOcclusionMode lightingOcclusionMode() const
    {
        return m_lightingOcclusionMode;
    }
    const std::string& debugName() const
    {
        return m_debugName;
    }

    // wrap offset for infinite scrolling (used by clouds)
    void setWrapOffset(const glm::vec3& offset)
    {
        m_wrapOffset = offset;
    }
    glm::vec3 wrapOffset() const
    {
        return m_wrapOffset;
    }

    VkImage image() const
    {
        return m_image.get();
    }
    VkImageView view() const
    {
        return m_view.get();
    }
    VkSampler sampler() const
    {
        return m_sampler.get();
    }

    // useful for descriptor writes later
    VkDescriptorImageInfo descriptorInfo() const
    {
        VkDescriptorImageInfo info{};
        info.sampler = m_sampler.get();
        info.imageView = m_view.get();
        info.imageLayout = m_layout;
        return info;
    }

    VkDescriptorImageInfo occupancyDescriptorInfo() const
    {
        VkDescriptorImageInfo info{};
        info.sampler = m_occSampler.get();
        info.imageView = m_occView.get();
        info.imageLayout = m_occLayout;
        return info;
    }

  private:
    bool validateFormatSupport(::VulkanContext& ctx) const;
    void computeWorldAabb();
    bool rebuildOccupancy(::VulkanContext& ctx, VkBuffer paletteBuffer);

    // meta
    glm::ivec3 m_dimensions{0, 0, 0};
    uint32_t m_paletteId = 0;
    uint32_t m_flags = 0;
    LightingOcclusionMode m_lightingOcclusionMode = LightingOcclusionMode::BinaryOpaque;
    std::string m_debugName;

    // transform
    glm::vec3 m_worldPosition{0, 0, 0};
    glm::quat m_worldRotation{1, 0, 0, 0};
    glm::vec3 m_worldScale{1, 1, 1};
    glm::vec3 m_wrapOffset{0, 0, 0}; // for infinite scrolling (clouds)
    glm::mat4 m_worldFromLocal{1.0f};
    glm::mat4 m_localFromWorld{1.0f};
    glm::vec3 m_worldAabbMin{0, 0, 0};
    glm::vec3 m_worldAabbMax{0, 0, 0};
    glm::ivec3 m_occupiedLocalMin{0, 0, 0};
    glm::ivec3 m_occupiedLocalMaxExclusive{0, 0, 0};

    // gpu
    render::UniqueImage m_image{};
    render::UniqueImageView m_view{};
    render::UniqueSampler m_sampler{};
    render::UniqueImage m_occImage{};
    render::UniqueImageView m_occView{};
    render::UniqueSampler m_occSampler{};
    std::vector<render::UniqueImageView> m_occMipViews{};
    glm::ivec3 m_occDimensions{0, 0, 0};
    uint32_t m_occMipCount = 0;

    // layout tracking for safe re-upload
    VkImageLayout m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout m_occLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace engine
