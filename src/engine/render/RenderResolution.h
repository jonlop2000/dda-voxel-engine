#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

namespace engine::render
{

enum class SpatialUpscaleMode : uint32_t
{
    Bilinear = 0,
    EdgeAdaptive = 1,
};

struct RenderResolutionSettings
{
    float scale = 1.0f;
    SpatialUpscaleMode upscaleMode = SpatialUpscaleMode::EdgeAdaptive;
    float sharpness = 0.20f;
    float edgeStrength = 0.75f;
};

inline constexpr float kMinimumRenderScale = 0.50f;
inline constexpr float kMaximumRenderScale = 1.00f;
inline constexpr float kQualityRenderScale = 0.75f;
inline constexpr float kPerformanceRenderScale = 2.0f / 3.0f;

[[nodiscard]] float sanitizeRenderScale(float scale);
[[nodiscard]] RenderResolutionSettings sanitizeRenderResolutionSettings(
    const RenderResolutionSettings& settings);
[[nodiscard]] VkExtent2D internalRenderExtent(VkExtent2D presentationExtent,
                                              float renderScale);
[[nodiscard]] bool requiresSpatialUpscale(VkExtent2D internalExtent,
                                          VkExtent2D presentationExtent);
[[nodiscard]] const char* spatialUpscaleModeName(SpatialUpscaleMode mode);

}  // namespace engine::render
