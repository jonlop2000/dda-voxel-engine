#include "App/AppRenderHelpers.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "App/Camera.h"
#include "App/AppSceneVolume.h"
#include "engine/render/RenderSettings.h"
#include "engine/scene/SceneConfig.h"
#include "engine/voxel/WaterVolume.h"
#include "Water/WaterPresets.h"

#include "engine/render/passes/GBufferPass.h"
#include "engine/render/passes/GlassPass.h"
#include "engine/render/passes/LightingPass.h"
#include "engine/render/passes/ShadowPass.h"
#include "engine/render/passes/WaterPass.h"

namespace
{

struct WaterV2SurfacePreset
{
    const char* name = "";
    glm::vec4 optics0{8.0f, 0.24f, 0.18f, 0.22f};
    glm::vec4 optics1{0.18f, 1.6f, 0.0f, 0.0f};
    glm::vec4 body0{0.12f, 0.22f, 0.36f, 0.18f};
    glm::vec4 body1{0.58f, 0.72f, 1.45f, 0.24f};
    glm::vec4 body2{0.45f, 0.26f, 0.45f, 0.040f};
    glm::vec4 reflection0{0.012f, 0.10f, 0.12f, 0.004f};
    glm::vec4 reflection1{0.03f, 0.08f, 0.25f, 0.045f};
    glm::vec4 planar0{0.0025f, 0.0070f, 0.10f, 0.060f};
    glm::vec4 planar1{0.34f, 0.015f, 0.42f, 0.025f};
    glm::vec4 planar2{0.045f, 0.16f, 0.18f, 0.70f};
    glm::vec4 contact0{0.045f, 0.34f, 0.006f, 0.070f};
    glm::vec4 contact1{0.35f, 0.18f, 0.032f, 0.065f};
    glm::vec4 contact2{0.085f, 0.055f, 0.085f, 0.22f};
    glm::vec4 opacity0{0.16f, 0.34f, 0.012f, 0.10f};
    glm::vec4 opacity1{0.13f, 0.075f, 0.18f, 0.66f};
    glm::vec4 bodyDetail0{0.56f, 0.44f, 0.145f, 0.34f};
};

const WaterV2SurfacePreset kWaterV2AquariumSurfacePreset{"Water V2 Aquarium Surface"};

const WaterV2SurfacePreset kWaterV2FishbowlSurfacePreset{
    "Water V2 Fishbowl Surface",
    {6.2f, 0.15f, 0.11f, 0.16f},
    {0.13f, 1.35f, 0.0f, 0.0f},
    {0.055f, 0.15f, 0.20f, 0.08f},
    {0.34f, 0.86f, 1.10f, 0.12f},
    {0.55f, 0.16f, 0.52f, 0.032f},
    {0.010f, 0.08f, 0.16f, 0.003f},
    {0.025f, 0.06f, 0.20f, 0.040f},
    {0.0020f, 0.0060f, 0.10f, 0.055f},
    {0.34f, 0.015f, 0.42f, 0.025f},
    {0.045f, 0.15f, 0.18f, 0.62f},
    {0.10f, 0.68f, 0.030f, 0.22f},
    {0.22f, 0.10f, 0.026f, 0.055f},
    {0.12f, 0.035f, 0.12f, 0.30f},
    {0.08f, 0.23f, 0.006f, 0.045f},
    {0.060f, 0.045f, 0.08f, 0.48f},
    {0.42f, 0.34f, 0.10f, 0.26f}};

const WaterV2SurfacePreset& waterV2SurfacePresetForScene(const SceneConfig& sceneConfig)
{
    if (isFishbowlAquariumScene(sceneConfig))
    {
        return kWaterV2FishbowlSurfacePreset;
    }
    return kWaterV2AquariumSurfacePreset;
}

void applyWaterV2SurfacePreset(WaterPass::FrameUbo& ubo, const WaterV2SurfacePreset& preset)
{
    ubo.v2Optics0 = preset.optics0;
    ubo.v2Optics1 = preset.optics1;
    ubo.v2Body0 = preset.body0;
    ubo.v2Body1 = preset.body1;
    ubo.v2Body2 = preset.body2;
    ubo.v2Reflection0 = preset.reflection0;
    ubo.v2Reflection1 = preset.reflection1;
    ubo.v2Planar0 = preset.planar0;
    ubo.v2Planar1 = preset.planar1;
    ubo.v2Planar2 = preset.planar2;
    ubo.v2Contact0 = preset.contact0;
    ubo.v2Contact1 = preset.contact1;
    ubo.v2Contact2 = preset.contact2;
    ubo.v2Opacity0 = preset.opacity0;
    ubo.v2Opacity1 = preset.opacity1;
    ubo.v2BodyDetail0 = preset.bodyDetail0;
}

} // namespace

namespace app::render
{

std::array<glm::vec3, 8> computeFrustumCornersWS(const Camera& camera,
                                                 const glm::mat4& invView,
                                                 float aspect, float nearZ, float farZ)
{
    const float safeAspect = std::max(aspect, 0.001f);
    float nearY = 0.0f;
    float nearX = 0.0f;
    float farY = 0.0f;
    float farX = 0.0f;
    if (camera.projectionMode == CameraProjectionMode::Orthographic)
    {
        nearY = std::max(camera.orthoHeight, 0.1f) * 0.5f;
        nearX = nearY * safeAspect;
        farY = nearY;
        farX = nearX;
    }
    else
    {
        const float tanHalfFov = std::tan(camera.fovY * 0.5f);
        nearY = tanHalfFov * nearZ;
        nearX = nearY * safeAspect;
        farY = tanHalfFov * farZ;
        farX = farY * safeAspect;
    }

    const std::array<glm::vec3, 8> cornersVS = {
        glm::vec3(-nearX, -nearY, -nearZ), glm::vec3(nearX, -nearY, -nearZ),
        glm::vec3(-nearX, nearY, -nearZ),  glm::vec3(nearX, nearY, -nearZ),
        glm::vec3(-farX, -farY, -farZ),    glm::vec3(farX, -farY, -farZ),
        glm::vec3(-farX, farY, -farZ),     glm::vec3(farX, farY, -farZ)};

    std::array<glm::vec3, 8> out{};
    for (size_t i = 0; i < out.size(); i++)
    {
        glm::vec4 p = invView * glm::vec4(cornersVS[i], 1.0f);
        out[i] = glm::vec3(p);
    }
    return out;
}

CascadeFit computeCascadeLightViewProj(const std::array<glm::vec3, 8>& frustumCorners,
                                       const glm::vec3& lightDir, VkExtent2D shadowExtent)
{
    CascadeFit out{};

    glm::vec3 center(0.0f);
    for (const auto& corner : frustumCorners)
    {
        center += corner;
    }
    center /= static_cast<float>(frustumCorners.size());

    float radius = 0.0f;
    for (const auto& corner : frustumCorners)
    {
        radius = std::max(radius, glm::length(corner - center));
    }

    out.lightDistance = radius + 10.0f;
    const glm::vec3 lightPos = center - lightDir * out.lightDistance;
    const glm::mat4 lightView = glm::lookAt(lightPos, center, glm::vec3(0.0f, 1.0f, 0.0f));

    glm::vec3 minLS(FLT_MAX);
    glm::vec3 maxLS(-FLT_MAX);
    for (const auto& corner : frustumCorners)
    {
        glm::vec4 ls = lightView * glm::vec4(corner, 1.0f);
        minLS = glm::min(minLS, glm::vec3(ls));
        maxLS = glm::max(maxLS, glm::vec3(ls));
    }

    const float padXY = 0.5f;
    const float padZ = 5.0f;
    minLS.x -= padXY;
    minLS.y -= padXY;
    maxLS.x += padXY;
    maxLS.y += padXY;
    minLS.z -= padZ;
    maxLS.z += padZ;

    const float extentX = maxLS.x - minLS.x;
    const float extentY = maxLS.y - minLS.y;
    const float shadowResX = static_cast<float>(shadowExtent.width);
    const float shadowResY = static_cast<float>(shadowExtent.height);
    if (extentX > 0.0f && extentY > 0.0f && shadowResX > 0.0f && shadowResY > 0.0f)
    {
        const float worldUnitsPerTexelX = extentX / shadowResX;
        const float worldUnitsPerTexelY = extentY / shadowResY;
        glm::vec2 centerXY((minLS.x + maxLS.x) * 0.5f, (minLS.y + maxLS.y) * 0.5f);
        centerXY.x = std::floor(centerXY.x / worldUnitsPerTexelX) * worldUnitsPerTexelX;
        centerXY.y = std::floor(centerXY.y / worldUnitsPerTexelY) * worldUnitsPerTexelY;

        minLS.x = centerXY.x - extentX * 0.5f;
        maxLS.x = centerXY.x + extentX * 0.5f;
        minLS.y = centerXY.y - extentY * 0.5f;
        maxLS.y = centerXY.y + extentY * 0.5f;
    }

    out.nearPlane = std::max(0.01f, -maxLS.z);
    out.farPlane = std::max(out.nearPlane + 1.0f, -minLS.z);
    glm::mat4 lightProj =
        glm::ortho(minLS.x, maxLS.x, minLS.y, maxLS.y, out.nearPlane, out.farPlane);
    lightProj[1][1] *= -1.0f;

    out.lightViewProj = lightProj * lightView;
    out.minLS = minLS;
    out.maxLS = maxLS;
    return out;
}

ShadowCascadeSetup computeShadowCascadeSetup(const Camera& camera, const glm::mat4& invView,
                                             float aspect, const glm::vec3& lightDir,
                                             VkExtent2D shadowExtent)
{
    ShadowCascadeSetup out{};
    const float shadowDistance = std::min(camera.farZ, 40.0f);
    out.shadowFar = std::max(shadowDistance, camera.nearZ + 0.1f);
    out.split0 = std::min(out.shadowFar, 4.0f);
    out.split1 = std::min(out.shadowFar, 12.0f);
    if (out.split1 < out.split0 + 0.1f)
    {
        out.split1 = std::min(out.shadowFar, out.split0 + 0.1f);
    }
    out.split2 = out.shadowFar;
    const std::array<float, kShadowCascades + 1> cascadeRanges = {
        camera.nearZ, out.split0, out.split1, out.split2};

    for (uint32_t c = 0; c < kShadowCascades; ++c)
    {
        const auto frustumCorners = computeFrustumCornersWS(camera, invView, aspect,
                                                            cascadeRanges[c], cascadeRanges[c + 1]);
        const CascadeFit fit = computeCascadeLightViewProj(frustumCorners, lightDir, shadowExtent);
        out.lightViewProjs[c] = fit.lightViewProj;
        if (c == 0)
        {
            out.cascade0Fit = fit;
        }
    }
    return out;
}

WaterPass::FrameUbo buildWaterFrameUbo(const WaterFrameUboInputs& in,
                                       const SceneConfig& sceneConfig,
                                       const engine::render::WaterSettings& water,
                                       const engine::WaterVolumeManager& volumes)
{
    WaterPass::FrameUbo ubo{};
    ubo.view = in.view;
    ubo.viewProj = in.viewProjJittered;
    ubo.invProj = in.invProjJittered;
    ubo.reflectedViewProj = in.reflectedViewProj;
    ubo.camPos = glm::vec4(in.cameraPos, 1.0f);
    ubo.sunDirToSun = glm::vec4(-in.lightDir, 0.0f);
    ubo.sunColor = in.sunColor;
    ubo.waterTint = isSunroofAquariumScene(sceneConfig)
                        ? glm::vec4(0.025f, 0.075f, 0.085f, 1.0f)
                        : (isFishbowlAquariumScene(sceneConfig)
                               ? glm::vec4(0.015f, 0.105f, 0.150f, 1.0f)
                               : glm::vec4(0.02f, 0.12f, 0.18f, 1.0f));
    ubo.params0 = glm::vec4(water.waterLevel_, in.animationNow, water.waterWaveScale_,
                            water.waterWaveAmp_);
    ubo.params1 =
        glm::vec4(water.waterRefract_, water.waterSpecPower_, water.waterSpecIntensity_,
                  water.useWaterV2_ ? std::max(water.waterFresnelBias_, 0.02f)
                                    : (water.waterStylizedMode_ ? water.waterFresnelBias_ : 0.02f));
    const float crestIntensity =
        (water.waterStylizedMode_ && water.waterCrestHighlightsEnabled_)
            ? water.waterCrestIntensity_
            : 0.0f;
    ubo.params2 = glm::vec4(water.waterCrestThreshold_, water.waterCrestSoftness_, crestIntensity,
                            water.waterDistortionDepthScale_);
    ubo.params3 = glm::vec4(water.waterEdgeFadeDepth_, water.waterBandHardness_,
                            water.waterReflectionStrength_, water.waterGradientStrength_);
    ubo.params4 = glm::vec4(static_cast<float>(in.waterDebugMode),
                            static_cast<float>(volumes.count()),
                            water.useWaterV2_ ? 2.0f : (water.waterStylizedMode_ ? 1.0f : 0.0f),
                            in.planarStrength);
    applyWaterV2SurfacePreset(ubo, waterV2SurfacePresetForScene(sceneConfig));
    ubo.v2Vfx0 =
        glm::vec4((water.useWaterV2_ && water.waterParticlesPlanned_) ? 1.0f : 0.0f,
                  water.waterParticlesPlannedDensity_, water.waterParticlesPlannedDrift_,
                  water.waterParticlesPlannedScale_);
    glm::vec2 foamEmitterCenter{water.waterFoamEmitterOffsetX_, water.waterFoamEmitterOffsetZ_};
    if (!volumes.volumes().empty())
    {
        const engine::WaterVolume& volume = volumes.volumes().front();
        const glm::vec3 center = (volume.boundsMin + volume.boundsMax) * 0.5f;
        const glm::vec3 halfExtent = (volume.boundsMax - volume.boundsMin) * 0.5f;
        foamEmitterCenter.x = center.x + water.waterFoamEmitterOffsetX_ * halfExtent.x;
        foamEmitterCenter.y = center.z + water.waterFoamEmitterOffsetZ_ * halfExtent.z;
    }
    ubo.v2Vfx1 =
        glm::vec4((water.useWaterV2_ && water.waterFoamEmitterEnabled_) ? 1.0f : 0.0f,
                  water.waterFoamEmitterIntensity_, water.waterFoamEmitterRadius_,
                  0.40f + water.waterFoamEmitterSpread_ * 0.45f);
    ubo.v2Vfx2 =
        glm::vec4(foamEmitterCenter.x, foamEmitterCenter.y, water.waterFoamEmitterScale_,
                  water.waterFoamEmitterSpread_);
    ubo.atmosphere0 =
        engine::render::packSceneAtmosphereParameters(in.sceneAtmosphere);
    const bool sunroofScene = isSunroofAquariumScene(sceneConfig);
    const WaterBodyShaftPreset& shaftPreset = waterBodyShaftPreset(sunroofScene);
    if (sunroofScene)
    {
        const engine::render::SunroofLivingWaterVfxSettings& livingWater =
            water.sunroofLivingWaterVfx_;
        ubo.v2BodyShaft0 = tuneWaterBodyShaftControls(
            shaftPreset, livingWater.enabled, livingWater.rayIntensity,
            livingWater.raySoftness, livingWater.rayWarmth);
    }
    else
    {
        ubo.v2BodyShaft0 = shaftPreset.controls;
    }
    return ubo;
}


VkExtent2D quarterExtent(VkExtent2D extent)
{
    VkExtent2D out{};
    out.width = std::max(1u, extent.width / 4u);
    out.height = std::max(1u, extent.height / 4u);
    return out;
}

void clearGeneralColorImage(VkCommandBuffer cmd, VkImage image, float value,
                            VkPipelineStageFlags dstStageMask, VkAccessFlags dstAccessMask)
{
    if (image == VK_NULL_HANDLE)
    {
        return;
    }

    VkClearColorValue clearValue{};
    clearValue.float32[0] = value;
    clearValue.float32[1] = value;
    clearValue.float32[2] = value;
    clearValue.float32[3] = value;

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &range);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = range;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = dstAccessMask;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, dstStageMask, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

void recordGBufferToLightingBarrier(VkCommandBuffer cmd, const GBufferPass& gbuffer,
                                    uint32_t frameIndex)
{
    std::array<VkImageMemoryBarrier, GBufferPass::kGBufferCount + 1> barriers{};
    for (size_t i = 0; i < GBufferPass::kGBufferCount; i++)
    {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = gbuffer.color(frameIndex, static_cast<GBufferPass::Slot>(i)).image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barriers[i] = barrier;
    }

    VkImageMemoryBarrier depthBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.image = gbuffer.depth(frameIndex).image;
    depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthBarrier.subresourceRange.baseMipLevel = 0;
    depthBarrier.subresourceRange.levelCount = 1;
    depthBarrier.subresourceRange.baseArrayLayer = 0;
    depthBarrier.subresourceRange.layerCount = 1;
    barriers[GBufferPass::kGBufferCount] = depthBarrier;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()),
                         barriers.data());
}

void recordShadowToLightingBarrier(VkCommandBuffer cmd, const ShadowMap& shadow)
{
    constexpr size_t kBarrierCount = kShadowCascades * 2;
    std::array<VkImageMemoryBarrier, kBarrierCount> barriers{};

    size_t barrierIndex = 0;
    for (const auto& cascade : shadow.cascades)
    {
        VkImageMemoryBarrier depthBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = cascade.image;
        depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        barriers[barrierIndex++] = depthBarrier;

        VkImageMemoryBarrier colorBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorBarrier.image = cascade.debugImage;
        colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorBarrier.subresourceRange.baseMipLevel = 0;
        colorBarrier.subresourceRange.levelCount = 1;
        colorBarrier.subresourceRange.baseArrayLayer = 0;
        colorBarrier.subresourceRange.layerCount = 1;
        barriers[barrierIndex++] = colorBarrier;
    }

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(barriers.size()), barriers.data());
}

void recordLightingToWaterBarrier(VkCommandBuffer cmd, const LightingPass& lighting,
                                  uint32_t frameIndex)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = lighting.lit(frameIndex).image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

void recordLightingReadToColorAttachmentBarrier(VkCommandBuffer cmd, const LightingPass& lighting,
                                                uint32_t frameIndex)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = lighting.lit(frameIndex).image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);
}

void recordWaterToGlassBarrier(VkCommandBuffer cmd, const WaterPass& water,
                               const LightingPass& lighting, uint32_t frameIndex)
{
    std::array<VkImageMemoryBarrier, 2> barriers{};

    VkImageMemoryBarrier waterBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    waterBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    waterBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    waterBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    waterBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    waterBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    waterBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    waterBarrier.image = water.target(frameIndex).image;
    waterBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    waterBarrier.subresourceRange.baseMipLevel = 0;
    waterBarrier.subresourceRange.levelCount = 1;
    waterBarrier.subresourceRange.baseArrayLayer = 0;
    waterBarrier.subresourceRange.layerCount = 1;
    barriers[0] = waterBarrier;

    VkImageMemoryBarrier litBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    litBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    litBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    litBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    litBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    litBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    litBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    litBarrier.image = lighting.lit(frameIndex).image;
    litBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    litBarrier.subresourceRange.baseMipLevel = 0;
    litBarrier.subresourceRange.levelCount = 1;
    litBarrier.subresourceRange.baseArrayLayer = 0;
    litBarrier.subresourceRange.layerCount = 1;
    barriers[1] = litBarrier;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()),
                         barriers.data());
}

void recordGlassBackDepthToShadeBarrier(VkCommandBuffer cmd, const GlassPass& glass,
                                        uint32_t frameIndex)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = glass.backDepth(frameIndex).colorImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

void recordGlassToPostBarrier(VkCommandBuffer cmd, const LightingPass& lighting,
                              uint32_t frameIndex)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = lighting.lit(frameIndex).image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

}  // namespace app::render
