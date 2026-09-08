#pragma once

#include <array>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/RendererConfig.h"

#include "engine/render/passes/WaterPass.h"

struct Camera;
struct GBufferPass;
struct GlassPass;
struct LightingPass;
struct SceneConfig;
struct ShadowMap;
namespace engine
{
class WaterVolumeManager;
}
namespace engine::render
{
struct WaterSettings;
}

namespace app::render
{

// pure cascaded-shadow fitting math (frame-shell-ownership-design.md,
// policy-assembly extraction). no app or renderer state; inputs in,
// matrices/splits out.
struct CascadeFit
{
    glm::mat4 lightViewProj{1.0f};
    glm::vec3 minLS{0.0f};
    glm::vec3 maxLS{0.0f};
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
    float lightDistance = 0.0f;
};

std::array<glm::vec3, 8> computeFrustumCornersWS(const Camera& camera,
                                                 const glm::mat4& invView,
                                                 float aspect, float nearZ, float farZ);
CascadeFit computeCascadeLightViewProj(const std::array<glm::vec3, 8>& frustumCorners,
                                       const glm::vec3& lightDir, VkExtent2D shadowExtent);

struct ShadowCascadeSetup
{
    std::array<glm::mat4, kShadowCascades> lightViewProjs{};
    float split0 = 0.0f;
    float split1 = 0.0f;
    float split2 = 0.0f;
    float shadowFar = 0.0f;
    CascadeFit cascade0Fit{};
};

ShadowCascadeSetup computeShadowCascadeSetup(const Camera& camera, const glm::mat4& invView,
                                             float aspect, const glm::vec3& lightDir,
                                             VkExtent2D shadowExtent);

// pure Water/Water-V2 frame-UBO assembly (frame-shell-ownership-design.md,
// policy-assembly extraction). owns the per-scene surface presets internally.
struct WaterFrameUboInputs
{
    glm::mat4 view{1.0f};
    glm::mat4 viewProjJittered{1.0f};
    glm::mat4 invProjJittered{1.0f};
    glm::mat4 reflectedViewProj{1.0f};
    glm::vec3 cameraPos{0.0f};
    glm::vec3 lightDir{0.0f};
    glm::vec4 sunColor{1.0f};
    float animationNow = 0.0f;
    int waterDebugMode = 0;
    float planarStrength = 0.0f;  // pre-resolved: 0 when planar reflection is inactive
    engine::render::SceneAtmosphereSettings sceneAtmosphere{};
};

WaterPass::FrameUbo buildWaterFrameUbo(const WaterFrameUboInputs& in,
                                       const SceneConfig& sceneConfig,
                                       const engine::render::WaterSettings& water,
                                       const engine::WaterVolumeManager& volumes);

VkExtent2D quarterExtent(VkExtent2D extent);
void clearGeneralColorImage(VkCommandBuffer cmd, VkImage image, float value,
                            VkPipelineStageFlags dstStageMask, VkAccessFlags dstAccessMask);
void recordGBufferToLightingBarrier(VkCommandBuffer cmd, const GBufferPass& gbuffer,
                                    uint32_t frameIndex);
void recordShadowToLightingBarrier(VkCommandBuffer cmd, const ShadowMap& shadow);
void recordLightingToWaterBarrier(VkCommandBuffer cmd, const LightingPass& lighting,
                                  uint32_t frameIndex);
void recordLightingReadToColorAttachmentBarrier(VkCommandBuffer cmd, const LightingPass& lighting,
                                                uint32_t frameIndex);
void recordWaterToGlassBarrier(VkCommandBuffer cmd, const WaterPass& water,
                               const LightingPass& lighting, uint32_t frameIndex);
void recordGlassBackDepthToShadeBarrier(VkCommandBuffer cmd, const GlassPass& glass,
                                        uint32_t frameIndex);
void recordGlassToPostBarrier(VkCommandBuffer cmd, const LightingPass& lighting,
                              uint32_t frameIndex);

}  // namespace app::render
