#include "engine/render/SunShadowSamplingPolicy.h"

#include <algorithm>

namespace engine::render
{

const char* sunShadowSamplingModeId(SunShadowSamplingMode mode)
{
    switch (mode)
    {
    case SunShadowSamplingMode::TemporalTwoOfFourUncompensated:
        return "temporal-2-uncompensated";
    case SunShadowSamplingMode::TemporalTwoOfFour:
        return "temporal-2";
    case SunShadowSamplingMode::FullPerFrame:
    default:
        return "full";
    }
}

std::optional<SunShadowSamplingMode> parseSunShadowSamplingMode(std::string_view id)
{
    if (id == "full")
    {
        return SunShadowSamplingMode::FullPerFrame;
    }
    if (id == "temporal-2")
    {
        return SunShadowSamplingMode::TemporalTwoOfFour;
    }
    if (id == "temporal-2-uncompensated")
    {
        return SunShadowSamplingMode::TemporalTwoOfFourUncompensated;
    }
    return std::nullopt;
}

SunShadowSamplingDecision resolveSunShadowSampling(
    SunShadowSamplingMode mode, int authoredSampleCount)
{
    SunShadowSamplingDecision result{};
    result.authoredSampleCount =
        static_cast<uint32_t>(std::clamp(
            authoredSampleCount, 1,
            static_cast<int>(kMaximumSunShadowSampleCount)));
    const bool temporalTwoOfFour =
        mode == SunShadowSamplingMode::TemporalTwoOfFour ||
        mode == SunShadowSamplingMode::TemporalTwoOfFourUncompensated;
    result.temporallyAmortized =
        temporalTwoOfFour && result.authoredSampleCount == 4u;
    result.effectiveSampleCount =
        result.temporallyAmortized ? 2u : result.authoredSampleCount;
    result.compensateReducedResolution =
        mode != SunShadowSamplingMode::TemporalTwoOfFourUncompensated ||
        !result.temporallyAmortized;
    // the shader retains ownership of its frame-indexed stochastic sequence.
    // this policy bounds and, for the opt-in temporal route, changes only the
    // number of samples consumed.
    return result;
}

}  // namespace engine::render
