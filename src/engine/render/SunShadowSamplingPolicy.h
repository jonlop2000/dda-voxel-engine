#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace engine::render
{

inline constexpr uint32_t kMaximumSunShadowSampleCount = 12u;

// transient renderer policy used to evaluate whether the existing temporal
// resolve can safely amortize the authored four-sample sun cone over frames.
// FullPerFrame remains the fail-closed product default.
enum class SunShadowSamplingMode : uint32_t
{
    FullPerFrame = 0,
    TemporalTwoOfFour = 1,
    TemporalTwoOfFourUncompensated = 2,
};

struct SunShadowSamplingDecision
{
    uint32_t authoredSampleCount = 1;
    uint32_t effectiveSampleCount = 1;
    bool temporallyAmortized = false;
    // reduced-resolution soft shadows normally spend extra samples to offset
    // reconstruction variance. the isolated uncompensated experiment opts out
    // only when its authored four-to-two policy activates successfully.
    bool compensateReducedResolution = true;
};

[[nodiscard]] const char* sunShadowSamplingModeId(SunShadowSamplingMode mode);
[[nodiscard]] std::optional<SunShadowSamplingMode> parseSunShadowSamplingMode(
    std::string_view id);
[[nodiscard]] SunShadowSamplingDecision resolveSunShadowSampling(
    SunShadowSamplingMode mode, int authoredSampleCount);

}  // namespace engine::render
