#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/game/FoliageArchetype.h"
#include "engine/render/FoliageMotionHistory.h"
#include "engine/render/FoliagePresentation.h"
#include "engine/render/FoliageVoxelGeometry.h"
#include "engine/render/WindborneParticleVoxelGeometry.h"
#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"
#include "engine/scene/EnvironmentWind.h"
#include "engine/scene/WindborneParticleSettings.h"

struct VulkanContext;

namespace engine::render
{

enum class FoliageRendererMode : uint32_t
{
    Voxel = 0,
    Instanced = 1,
};

[[nodiscard]] FoliageRendererMode parseFoliageRendererMode(std::string_view value);
[[nodiscard]] const char* foliageRendererModeName(FoliageRendererMode mode);

class FoliagePass
{
public:
    struct DrawSettings
    {
        bool reflectionClipEnabled = false;
        float reflectionClipY = 0.0f;
    };

    void setShaderPaths(std::string vertexPath, std::string fragmentPath);
    void setRequestedRenderer(FoliageRendererMode mode);
    void setSwayEnabled(bool enabled);
    void setSwayStrength(float strength);
    void setEnvironmentWind(
        const engine::scene::EnvironmentWindSettings& settings);
    void setWindborneParticleSettings(
        const engine::scene::WindborneParticleSettings& settings);
    void setPaletteResponse(FoliagePaletteResponse response);

    [[nodiscard]] bool create(VulkanContext& context, VkRenderPass renderPass,
                              VkDescriptorSetLayout frameSetLayout,
                              VkBuffer paletteBuffer);
    void destroy(VulkanContext& context);
    [[nodiscard]] bool recreatePipeline(VulkanContext& context,
                                        VkRenderPass renderPass,
                                        VkDescriptorSetLayout frameSetLayout);

    // may be called before create at startup. reload callers already wait for in-flight work.
    [[nodiscard]] bool setSceneData(
        VulkanContext& context,
        std::span<const engine::game::FoliageBladeInstance> instances,
        uint32_t legacyVoxelVolumeIndex = std::numeric_limits<uint32_t>::max(),
        std::span<const engine::scene::WindborneParticleCandidate>
            windborneParticles = {});
    void clearSceneData();

    void beginFrame(float currentTimeSeconds);
    void completeFrame();
    void record(VkCommandBuffer commandBuffer, VkDescriptorSet frameSet,
                const DrawSettings& settings) const;

    [[nodiscard]] bool drawable() const;
    [[nodiscard]] bool replacesVoxelVolume(uint32_t volumeIndex) const;
    [[nodiscard]] FoliageRendererMode requestedRenderer() const
    {
        return requestedRenderer_;
    }
    [[nodiscard]] bool swayEnabled() const { return swayEnabled_; }
    [[nodiscard]] const engine::scene::EnvironmentWindSettings&
    environmentWind() const
    {
        return environmentWind_;
    }
    [[nodiscard]] FoliagePaletteResponse paletteResponse() const
    {
        return paletteResponse_;
    }
    [[nodiscard]] std::string_view paletteResponseName() const
    {
        return drawable()
                   ? foliagePaletteResponseName(paletteResponse_)
                   : std::string_view("n/a");
    }
    [[nodiscard]] std::string_view topologyName() const
    {
        return drawable() ? kFoliageVoxelTopologyName : "n/a";
    }
    [[nodiscard]] uint32_t instanceCount() const
    {
        return drawable() ? semanticInstanceCount_ : 0u;
    }
    [[nodiscard]] uint32_t primitiveCount() const
    {
        return drawable() ? foliagePrimitiveCount_ : 0u;
    }
    [[nodiscard]] uint32_t activeWindborneParticleCount() const;
    [[nodiscard]] uint32_t residentWindborneParticleCount() const
    {
        return windborneParticleCount_;
    }
    [[nodiscard]] uint32_t submittedPrimitiveCount() const
    {
        return drawable()
                   ? foliagePrimitiveCount_ + activeWindborneParticleCount()
                   : 0u;
    }
    [[nodiscard]] uint32_t patchCount() const
    {
        return drawable() ? patchCount_ : 0u;
    }
    [[nodiscard]] uint32_t verticesPerPrimitive() const
    {
        return drawable() ? kFoliageVoxelVerticesPerPrimitive : 0u;
    }
    [[nodiscard]] uint64_t submittedVertices() const
    {
        return static_cast<uint64_t>(primitiveCount()) *
               verticesPerPrimitive();
    }
    [[nodiscard]] uint64_t submittedWindborneVertices() const
    {
        return static_cast<uint64_t>(activeWindborneParticleCount()) *
               kFoliageVoxelVerticesPerPrimitive;
    }
    [[nodiscard]] uint32_t batchCount() const { return drawable() ? 1u : 0u; }
    [[nodiscard]] size_t gpuBytes() const
    {
        return static_cast<size_t>(primitiveCount()) *
               sizeof(FoliageGpuPrimitive);
    }
    [[nodiscard]] size_t residentGpuBytes() const
    {
        return primitiveBuffer_
                   ? static_cast<size_t>(foliagePrimitiveCount_) *
                         sizeof(FoliageGpuPrimitive)
                   : 0u;
    }
    [[nodiscard]] size_t residentWindborneGpuBytes() const
    {
        return primitiveBuffer_
                   ? static_cast<size_t>(windborneParticleCount_) *
                         sizeof(FoliageGpuPrimitive)
                   : 0u;
    }
    [[nodiscard]] float currentTimeSeconds() const
    {
        return motionHistory_.currentTimeSeconds();
    }
    [[nodiscard]] float previousTimeSeconds() const
    {
        return motionHistory_.previousTimeSeconds();
    }

private:
    struct PushConstants
    {
        glm::vec4 timeWind{0.0f}; // current time, previous time, wind x, wind z
        glm::vec4 controls{0.0f}; // current/previous sway strength, clip y, clip enabled
        glm::vec4 secondaryWave{0.0f};
        glm::vec4 crossWave{0.0f};
        glm::vec4 appearance{0.0f};
        glm::vec4 surface{0.0f};
        glm::vec4 material{0.0f};
        glm::vec4 ambient{0.0f};
    };
    static_assert(std::is_standard_layout_v<PushConstants>);
    static_assert(sizeof(PushConstants) == 128);
    static_assert(offsetof(PushConstants, timeWind) == 0);
    static_assert(offsetof(PushConstants, controls) == 16);
    static_assert(offsetof(PushConstants, secondaryWave) == 32);
    static_assert(offsetof(PushConstants, crossWave) == 48);
    static_assert(offsetof(PushConstants, appearance) == 64);
    static_assert(offsetof(PushConstants, surface) == 80);
    static_assert(offsetof(PushConstants, material) == 96);
    static_assert(offsetof(PushConstants, ambient) == 112);

    [[nodiscard]] bool createDescriptorResources(VulkanContext& context,
                                                 VkBuffer paletteBuffer);
    [[nodiscard]] bool createGraphicsPipeline(VulkanContext& context,
                                              VkRenderPass renderPass,
                                              VkDescriptorSetLayout frameSetLayout);
    [[nodiscard]] bool uploadPendingGeometry(VulkanContext& context);
    void refreshReadyState();

    std::vector<engine::game::FoliageBladeInstance> pendingInstances_{};
    std::vector<engine::scene::WindborneParticleCandidate>
        pendingWindborneParticles_{};
    UniqueBuffer primitiveBuffer_{};
    UniquePipelineLayout pipelineLayout_{};
    UniquePipeline pipeline_{};
    VkDescriptorSetLayout instanceSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet instanceSet_ = VK_NULL_HANDLE;
    VkBuffer paletteBuffer_ = VK_NULL_HANDLE;
    uint32_t legacyVoxelVolumeIndex_ = std::numeric_limits<uint32_t>::max();
    uint32_t semanticInstanceCount_ = 0;
    uint32_t patchCount_ = 0;
    uint32_t foliagePrimitiveCount_ = 0;
    uint32_t windborneParticleCount_ = 0;
    uint32_t primitiveCount_ = 0;
    bool resourcesCreated_ = false;
    bool ready_ = false;
    bool swayEnabled_ = true;
    float swayStrength_ = 1.0f;
    engine::scene::EnvironmentWindSettings environmentWind_ =
        engine::scene::defaultEnvironmentWindSettings();
    engine::scene::WindborneParticleSettings windborneParticleSettings_ =
        engine::scene::defaultWindborneParticleSettings();
    FoliagePaletteResponse paletteResponse_ =
        FoliagePaletteResponse::PastelLightV4;
    FoliageRendererMode requestedRenderer_ = FoliageRendererMode::Instanced;

    FoliageMotionHistory motionHistory_{};

    std::string vertexPath_{};
    std::string fragmentPath_{};
};

} // namespace engine::render
