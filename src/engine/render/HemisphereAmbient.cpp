#include "engine/render/HemisphereAmbient.h"

#include <algorithm>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace engine::render
{

bool HemisphereAmbientSettings::enabled() const
{
    return strength > 0.0f;
}

std::optional<HemisphereAmbientPreset>
hemisphereAmbientPresetFromName(std::string_view name)
{
    if (name == "off")
    {
        return HemisphereAmbientPreset::Disabled;
    }
    if (name == "warm-cool-v0")
    {
        return HemisphereAmbientPreset::WarmCoolOutdoorV0;
    }
    return std::nullopt;
}

std::string_view hemisphereAmbientPresetName(HemisphereAmbientPreset preset)
{
    switch (preset)
    {
    case HemisphereAmbientPreset::Disabled:
        return "off";
    case HemisphereAmbientPreset::WarmCoolOutdoorV0:
        return "warm-cool-v0";
    }
    return "unknown";
}

HemisphereAmbientSettings
makeHemisphereAmbientSettings(HemisphereAmbientPreset preset)
{
    HemisphereAmbientSettings settings{};
    if (preset == HemisphereAmbientPreset::WarmCoolOutdoorV0)
    {
        settings.strength = 1.0f;
        settings.skyTint = glm::vec3(0.72f, 1.00f, 1.32f);
        settings.groundTint = glm::vec3(1.35f, 0.85f, 0.55f);
    }
    return settings;
}

glm::vec3 evaluateHemisphereAmbientColor(
    const glm::vec3& legacyAmbientColor, const glm::vec3& shadingNormal,
    const HemisphereAmbientSettings& settings)
{
    const float strength = std::clamp(settings.strength, 0.0f, 1.0f);
    if (strength <= 0.0f)
    {
        return legacyAmbientColor;
    }

    const float normalLength = glm::length(shadingNormal);
    const glm::vec3 normal =
        normalLength > 1e-6f ? shadingNormal / normalLength : glm::vec3(0.0f, 1.0f, 0.0f);
    const float skyWeight = std::clamp(normal.y * 0.5f + 0.5f, 0.0f, 1.0f);
    const glm::vec3 skyTint = glm::max(settings.skyTint, glm::vec3(0.0f));
    const glm::vec3 groundTint = glm::max(settings.groundTint, glm::vec3(0.0f));
    const glm::vec3 hemisphereTint =
        groundTint * (1.0f - skyWeight) + skyTint * skyWeight;
    const glm::vec3 effectiveTint =
        glm::vec3(1.0f) * (1.0f - strength) + hemisphereTint * strength;
    return legacyAmbientColor * effectiveTint;
}

} // namespace engine::render
