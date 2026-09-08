#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace engine::render
{

// a transient hardware-cost policy. presentation profiles continue to own the
// authored look; these presets only coordinate scene and auxiliary-ray extents.
enum class RenderQualityPreset : uint32_t
{
    Native = 0,
    Quality = 1,
    Performance = 2,
};

inline constexpr std::array<RenderQualityPreset, 3> kRenderQualityPresets = {
    RenderQualityPreset::Native,
    RenderQualityPreset::Quality,
    RenderQualityPreset::Performance,
};

struct RenderQualityPresetSettings
{
    float renderScale = 1.0f;
    float shadowRayScale = 1.0f;
    float aoRayScale = 1.0f;
};

[[nodiscard]] RenderQualityPresetSettings renderQualityPresetSettings(
    RenderQualityPreset preset);
[[nodiscard]] const char* renderQualityPresetId(RenderQualityPreset preset);
[[nodiscard]] const char* renderQualityPresetLabel(RenderQualityPreset preset);
[[nodiscard]] std::optional<RenderQualityPreset> parseRenderQualityPreset(
    std::string_view id);
[[nodiscard]] std::optional<RenderQualityPreset> matchRenderQualityPreset(
    float renderScale, float shadowRayScale, float aoRayScale,
    float epsilon = 1e-4f);

}  // namespace engine::render
