#pragma once

#include <optional>
#include <string_view>

#include <glm/vec3.hpp>

namespace engine::render
{

// transient controls for the normal-oriented ambient approximation.
// a zero strength is the exact compatibility configuration.
// ScenePresentationProfile v1 owns persistent scene values.
struct HemisphereAmbientSettings
{
    float strength = 0.0f;
    glm::vec3 skyTint{1.0f};
    glm::vec3 groundTint{1.0f};

    [[nodiscard]] bool enabled() const;
    bool operator==(const HemisphereAmbientSettings&) const = default;
};

enum class HemisphereAmbientPreset
{
    Disabled,
    WarmCoolOutdoorV0,
};

[[nodiscard]] std::optional<HemisphereAmbientPreset>
hemisphereAmbientPresetFromName(std::string_view name);
[[nodiscard]] std::string_view
hemisphereAmbientPresetName(HemisphereAmbientPreset preset);
[[nodiscard]] HemisphereAmbientSettings
makeHemisphereAmbientSettings(HemisphereAmbientPreset preset);

// cpu reference used by focused compatibility and orientation tests. the
// lighting shader mirrors this tint evaluation before applying ao strength.
[[nodiscard]] glm::vec3 evaluateHemisphereAmbientColor(
    const glm::vec3& legacyAmbientColor, const glm::vec3& shadingNormal,
    const HemisphereAmbientSettings& settings);

} // namespace engine::render
