#pragma once

#include <algorithm>

#include <glm/glm.hpp>

#include "engine/voxel/WaterVolume.h"

// water absorption/fog presets shared by app scene setup and the editor debug
// panel. header-only (mirrors App/GlassPresets.h) so both sides use the same
// definitions without linking a new translation unit.

struct WaterPreset
{
    const char* name = "";
    glm::vec3 absorption{0.45f, 0.08f, 0.04f};
    glm::vec3 deepColor{0.02f, 0.12f, 0.22f};
    float fogDensity = 0.06f;
};

// analytical underwater-shaft controls consumed by WaterBodyPass. keeping these
// scene-authored values beside the water-medium presets makes the sunroof look a
// water policy rather than an app or shader special case.
struct WaterBodyShaftPreset
{
    const char* name = "";
    // x=strength multiplier, y=band contrast, z=centered aperture half-extent
    // normalized against the water volume, w=warm-white tint blend.
    glm::vec4 controls{1.0f, 0.0f, 0.0f, 0.0f};
};

// exact compatibility route: no aperture gate, no extra contrast, and the
// historical cool shaft color/strength.
inline const WaterBodyShaftPreset kDefaultWaterBodyShaftPreset = {
    "Water Body Shafts Compatibility", {1.0f, 0.0f, 0.0f, 0.0f}};

// the authored sunroof opening is 24 cells across inside a 64-world-unit water
// span, giving a normalized half-extent of 12/32 = 0.375. V2.6 preserves V2.5's
// accepted full-segment soft ribbons and layers bounded living-water details on
// top without increasing the ray-integration budget.
inline const WaterBodyShaftPreset kSunroofEnvironmentV26LivingWaterPreset = {
    "Sunroof Environment V2.6 Living Water", {1.45f, 0.58f, 0.375f, 0.30f}};

inline const WaterBodyShaftPreset& waterBodyShaftPreset(bool useSunroofAperture)
{
    return useSunroofAperture ? kSunroofEnvironmentV26LivingWaterPreset
                              : kDefaultWaterBodyShaftPreset;
}

// applies bounded live look-development controls without changing the authored
// aperture. a softness value above one lowers contrast; below one firms it up.
inline glm::vec4 tuneWaterBodyShaftControls(const WaterBodyShaftPreset& preset,
                                            bool enabled, float intensity,
                                            float softness, float warmth)
{
    glm::vec4 controls = preset.controls;
    const float safeSoftness = std::clamp(softness, 0.5f, 1.5f);
    controls.x = enabled ? controls.x * std::clamp(intensity, 0.0f, 2.0f) : 0.0f;
    controls.y = std::clamp(controls.y / safeSoftness, 0.0f, 1.0f);
    controls.w = std::clamp(controls.w * std::clamp(warmth, 0.0f, 1.5f),
                            0.0f, 0.45f);
    return controls;
}

inline const WaterPreset kWaterV2AquariumPreset = {
    "Water V2 Aquarium", {0.014f, 0.007f, 0.005f}, {0.075f, 0.145f, 0.145f}, 0.010f};

// sunroof environment V2 keeps the near field clear while adding enough extinction
// and blue-green inscatter for the chamber walls, fish, and foliage layers to separate
// with distance. this is a conservative 68% blend from the old sunroof base toward
// the ultra-clear aquarium preset.
inline const WaterPreset kSunroofEnvironmentV2WaterPreset = {
    "Sunroof Environment V2",
    {0.02552f, 0.01308f, 0.00916f},
    {0.06220f, 0.12260f, 0.12420f},
    0.01640f,
};

inline const WaterPreset kWaterPresets[] = {
    kWaterV2AquariumPreset,
    kSunroofEnvironmentV2WaterPreset,
    {"Aero Clear", {0.014f, 0.007f, 0.005f}, {0.075f, 0.145f, 0.145f}, 0.01f},
    {"Crystal Clear", {0.15f, 0.03f, 0.01f}, {0.01f, 0.08f, 0.18f}, 0.02f},
    {"Tropical Ocean", {0.30f, 0.06f, 0.02f}, {0.02f, 0.15f, 0.22f}, 0.04f},
    {"Aquarium Tank", {0.45f, 0.08f, 0.04f}, {0.02f, 0.12f, 0.22f}, 0.06f},
    {"Murky Pond", {0.80f, 0.40f, 0.30f}, {0.04f, 0.08f, 0.06f}, 0.15f},
    {"Stylized", {0.50f, 0.10f, 0.03f}, {0.01f, 0.10f, 0.18f}, 0.05f},
};

inline const WaterPreset kSunroofBaseWaterPreset = {
    "Sunroof Default", {0.050f, 0.026f, 0.018f}, {0.035f, 0.075f, 0.080f}, 0.030f};
inline const WaterPreset& kSunroofAeroWaterPreset = kWaterV2AquariumPreset;

namespace waterpreset_detail
{
inline float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

inline float absorptionMetric(const glm::vec3& absorption)
{
    return glm::dot(absorption, glm::vec3(0.55f, 0.30f, 0.15f));
}
} // namespace waterpreset_detail

inline WaterPreset blendWaterPreset(const WaterPreset& a, const WaterPreset& b, float t)
{
    const float blend = waterpreset_detail::clamp01(t);
    return WaterPreset{
        "",
        glm::mix(a.absorption, b.absorption, blend),
        glm::mix(a.deepColor, b.deepColor, blend),
        glm::mix(a.fogDensity, b.fogDensity, blend),
    };
}

inline float estimateWaterPresetBlend(const engine::WaterVolume& volume,
                                      const WaterPreset& base, const WaterPreset& target)
{
    using waterpreset_detail::absorptionMetric;
    using waterpreset_detail::clamp01;

    const float baseAbsorption = absorptionMetric(base.absorption);
    const float targetAbsorption = absorptionMetric(target.absorption);
    const float currentAbsorption = absorptionMetric(volume.absorptionCoeff);

    const float absorptionSpan = std::max(baseAbsorption - targetAbsorption, 1e-5f);
    const float fogSpan = std::max(base.fogDensity - target.fogDensity, 1e-5f);

    const float absorptionBlend =
        1.0f - clamp01((currentAbsorption - targetAbsorption) / absorptionSpan);
    const float fogBlend = 1.0f - clamp01((volume.fogDensity - target.fogDensity) / fogSpan);
    return clamp01(absorptionBlend * 0.7f + fogBlend * 0.3f);
}

inline void applyWaterPreset(engine::WaterVolume& volume, const WaterPreset& preset)
{
    volume.absorptionCoeff = preset.absorption;
    volume.deepColor = preset.deepColor;
    volume.fogDensity = preset.fogDensity;
}
