#include "engine/render/RenderResolution.h"

#include <algorithm>
#include <cmath>

namespace engine::render
{

float sanitizeRenderScale(float scale)
{
    if (!std::isfinite(scale))
    {
        return 1.0f;
    }
    return std::clamp(scale, kMinimumRenderScale, kMaximumRenderScale);
}

RenderResolutionSettings sanitizeRenderResolutionSettings(
    const RenderResolutionSettings& settings)
{
    RenderResolutionSettings sanitized = settings;
    sanitized.scale = sanitizeRenderScale(settings.scale);
    sanitized.sharpness = std::isfinite(settings.sharpness)
                              ? std::clamp(settings.sharpness, 0.0f, 1.0f)
                              : RenderResolutionSettings{}.sharpness;
    sanitized.edgeStrength = std::isfinite(settings.edgeStrength)
                                 ? std::clamp(settings.edgeStrength, 0.0f, 1.0f)
                                 : RenderResolutionSettings{}.edgeStrength;
    if (settings.upscaleMode != SpatialUpscaleMode::Bilinear &&
        settings.upscaleMode != SpatialUpscaleMode::EdgeAdaptive)
    {
        sanitized.upscaleMode = SpatialUpscaleMode::EdgeAdaptive;
    }
    return sanitized;
}

VkExtent2D internalRenderExtent(VkExtent2D presentationExtent, float renderScale)
{
    if (presentationExtent.width == 0 || presentationExtent.height == 0)
    {
        return {};
    }

    const float scale = sanitizeRenderScale(renderScale);
    const auto scaledDimension = [scale](uint32_t dimension) {
        const double scaled = std::round(static_cast<double>(dimension) * scale);
        return std::clamp(static_cast<uint32_t>(std::max(1.0, scaled)), 1u, dimension);
    };

    return {scaledDimension(presentationExtent.width),
            scaledDimension(presentationExtent.height)};
}

bool requiresSpatialUpscale(VkExtent2D internalExtent, VkExtent2D presentationExtent)
{
    return internalExtent.width > 0 && internalExtent.height > 0 &&
           presentationExtent.width > 0 && presentationExtent.height > 0 &&
           (internalExtent.width != presentationExtent.width ||
            internalExtent.height != presentationExtent.height);
}

const char* spatialUpscaleModeName(SpatialUpscaleMode mode)
{
    switch (mode)
    {
    case SpatialUpscaleMode::Bilinear:
        return "Bilinear";
    case SpatialUpscaleMode::EdgeAdaptive:
        return "Edge-Adaptive";
    default:
        return "Unknown";
    }
}

}  // namespace engine::render
