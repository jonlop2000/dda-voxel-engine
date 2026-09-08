#include "Water/WaterPresets.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 1.0e-5f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

bool nearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = 1.0e-5f)
{
    return nearlyEqual(lhs.x, rhs.x, epsilon) && nearlyEqual(lhs.y, rhs.y, epsilon) &&
           nearlyEqual(lhs.z, rhs.z, epsilon);
}

bool nearlyEqual(const glm::vec4& lhs, const glm::vec4& rhs, float epsilon = 1.0e-5f)
{
    return nearlyEqual(lhs.x, rhs.x, epsilon) && nearlyEqual(lhs.y, rhs.y, epsilon) &&
           nearlyEqual(lhs.z, rhs.z, epsilon) && nearlyEqual(lhs.w, rhs.w, epsilon);
}

void testWaterBodyShaftPresets()
{
    require(nearlyEqual(kDefaultWaterBodyShaftPreset.controls,
                        glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)),
            "default water-body shafts should preserve the ungated compatibility route");
    require(nearlyEqual(kSunroofEnvironmentV26LivingWaterPreset.controls,
                        glm::vec4(1.45f, 0.58f, 0.375f, 0.30f)),
            "sunroof shafts should retain the authored strength, contrast, aperture, and tint");
    require(&waterBodyShaftPreset(false) == &kDefaultWaterBodyShaftPreset &&
                &waterBodyShaftPreset(true) == &kSunroofEnvironmentV26LivingWaterPreset,
            "water-body shaft selection should preserve compatibility and route only authored "
            "sunroof scenes to the aperture preset");

    const glm::vec4 sunroof = kSunroofEnvironmentV26LivingWaterPreset.controls;
    require(sunroof.x > kDefaultWaterBodyShaftPreset.controls.x && sunroof.x <= 3.0f,
            "sunroof shaft strength should be visibly stronger but remain bounded");
    require(sunroof.y > 0.0f && sunroof.y <= 1.0f,
            "sunroof shaft contrast should stay normalized");
    require(nearlyEqual(sunroof.z, 12.0f / 32.0f),
            "sunroof aperture should match the centered 24-cell opening in the 64-unit span");
    require(sunroof.w > 0.0f && sunroof.w <= 0.45f,
            "sunroof shaft tint should remain a restrained warm-white blend");

    require(nearlyEqual(
                tuneWaterBodyShaftControls(
                    kSunroofEnvironmentV26LivingWaterPreset, true,
                    1.0f, 1.0f, 1.0f),
                sunroof),
            "default live controls should preserve the accepted V2.6 shaft preset");
    const glm::vec4 softened = tuneWaterBodyShaftControls(
        kSunroofEnvironmentV26LivingWaterPreset, true, 1.25f, 1.5f, 1.5f);
    require(nearlyEqual(softened.x, sunroof.x * 1.25f) &&
                softened.y < sunroof.y && nearlyEqual(softened.z, sunroof.z) &&
                nearlyEqual(softened.w, 0.45f),
            "live shaft tuning should preserve the aperture while bounding strength, "
            "softness, and warmth");
    const glm::vec4 disabled = tuneWaterBodyShaftControls(
        kSunroofEnvironmentV26LivingWaterPreset, false, 2.0f, 0.5f, 1.5f);
    require(nearlyEqual(disabled.x, 0.0f) && nearlyEqual(disabled.z, sunroof.z),
            "disabling living-water rays should remove lift without changing authored geometry");
}

void testSunroofEnvironmentV2Preset()
{
    const WaterPreset blended =
        blendWaterPreset(kSunroofBaseWaterPreset, kWaterV2AquariumPreset, 0.68f);
    require(nearlyEqual(kSunroofEnvironmentV2WaterPreset.absorption,
                        glm::vec3(0.02552f, 0.01308f, 0.00916f)) &&
                nearlyEqual(kSunroofEnvironmentV2WaterPreset.deepColor,
                            glm::vec3(0.06220f, 0.12260f, 0.12420f)) &&
                nearlyEqual(kSunroofEnvironmentV2WaterPreset.fogDensity, 0.01640f),
            "sunroof environment V2 should retain its authored depth coefficients");
    require(nearlyEqual(blended.absorption, kSunroofEnvironmentV2WaterPreset.absorption) &&
                nearlyEqual(blended.deepColor, kSunroofEnvironmentV2WaterPreset.deepColor) &&
                nearlyEqual(blended.fogDensity, kSunroofEnvironmentV2WaterPreset.fogDensity),
            "sunroof environment V2 should remain the documented base-to-clear "
            "blend");

    for (int channel = 0; channel < 3; ++channel)
    {
        require(kSunroofEnvironmentV2WaterPreset.absorption[channel] >
                        kWaterV2AquariumPreset.absorption[channel] &&
                    kSunroofEnvironmentV2WaterPreset.absorption[channel] <
                        kSunroofBaseWaterPreset.absorption[channel],
                "sunroof V2 absorption should sit between the clear and base presets");
        require(kSunroofEnvironmentV2WaterPreset.deepColor[channel] >
                        kSunroofBaseWaterPreset.deepColor[channel] &&
                    kSunroofEnvironmentV2WaterPreset.deepColor[channel] <
                        kWaterV2AquariumPreset.deepColor[channel],
                "sunroof V2 inscatter should sit between the base and clear presets");
        require(std::isfinite(kSunroofEnvironmentV2WaterPreset.absorption[channel]) &&
                    std::isfinite(kSunroofEnvironmentV2WaterPreset.deepColor[channel]),
                "sunroof V2 water coefficients should remain finite");
    }
    require(kSunroofEnvironmentV2WaterPreset.fogDensity > kWaterV2AquariumPreset.fogDensity &&
                kSunroofEnvironmentV2WaterPreset.fogDensity < kSunroofBaseWaterPreset.fogDensity,
            "sunroof V2 fog should add depth without returning to the old base haze");

    engine::WaterVolume volume{};
    applyWaterPreset(volume, kSunroofEnvironmentV2WaterPreset);
    require(nearlyEqual(volume.absorptionCoeff, kSunroofEnvironmentV2WaterPreset.absorption) &&
                nearlyEqual(volume.deepColor, kSunroofEnvironmentV2WaterPreset.deepColor) &&
                nearlyEqual(volume.fogDensity, kSunroofEnvironmentV2WaterPreset.fogDensity),
            "applying the sunroof V2 preset should update every runtime depth input");
}

} // namespace

int main()
{
    try
    {
        testWaterBodyShaftPresets();
        testSunroofEnvironmentV2Preset();
        std::cout << "Water preset tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Water preset test failure: " << error.what() << '\n';
        return 1;
    }
}
