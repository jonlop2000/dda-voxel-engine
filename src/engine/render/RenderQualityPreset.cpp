#include "engine/render/RenderQualityPreset.h"

#include <cmath>

namespace engine::render
{

RenderQualityPresetSettings renderQualityPresetSettings(RenderQualityPreset preset)
{
    switch (preset)
    {
    case RenderQualityPreset::Quality:
        return {0.75f, 0.50f, 0.50f};
    case RenderQualityPreset::Performance:
        // 1536x864 scene plus 768x432 auxiliary rays at a 2560x1440
        // presentation extent. this is the qualified 7-core M1 30 fps tier.
        return {0.60f, 0.50f, 0.50f};
    case RenderQualityPreset::Native:
    default:
        return {1.0f, 1.0f, 1.0f};
    }
}

const char* renderQualityPresetId(RenderQualityPreset preset)
{
    switch (preset)
    {
    case RenderQualityPreset::Quality:
        return "quality";
    case RenderQualityPreset::Performance:
        return "performance";
    case RenderQualityPreset::Native:
    default:
        return "native";
    }
}

const char* renderQualityPresetLabel(RenderQualityPreset preset)
{
    switch (preset)
    {
    case RenderQualityPreset::Quality:
        return "Quality (75% + half rays)";
    case RenderQualityPreset::Performance:
        return "Performance (60% + half rays)";
    case RenderQualityPreset::Native:
    default:
        return "Native";
    }
}

std::optional<RenderQualityPreset> parseRenderQualityPreset(std::string_view id)
{
    if (id == "native")
    {
        return RenderQualityPreset::Native;
    }
    if (id == "quality")
    {
        return RenderQualityPreset::Quality;
    }
    if (id == "performance")
    {
        return RenderQualityPreset::Performance;
    }
    return std::nullopt;
}

std::optional<RenderQualityPreset> matchRenderQualityPreset(
    float renderScale, float shadowRayScale, float aoRayScale, float epsilon)
{
    if (!std::isfinite(renderScale) || !std::isfinite(shadowRayScale) ||
        !std::isfinite(aoRayScale) || !std::isfinite(epsilon) || epsilon < 0.0f)
    {
        return std::nullopt;
    }

    for (const RenderQualityPreset preset : kRenderQualityPresets)
    {
        const RenderQualityPresetSettings settings = renderQualityPresetSettings(preset);
        if (std::abs(renderScale - settings.renderScale) <= epsilon &&
            std::abs(shadowRayScale - settings.shadowRayScale) <= epsilon &&
            std::abs(aoRayScale - settings.aoRayScale) <= epsilon)
        {
            return preset;
        }
    }
    return std::nullopt;
}

}  // namespace engine::render
