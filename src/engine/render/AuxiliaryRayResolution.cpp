#include "engine/render/AuxiliaryRayResolution.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#include <glm/common.hpp>

#include "engine/render/SunShadowSamplingPolicy.h"

namespace engine::render
{

float sanitizeAuxiliaryRayScale(float scale)
{
    if (!std::isfinite(scale))
    {
        return 1.0f;
    }
    return std::clamp(scale, kMinimumAuxiliaryRayScale, kMaximumAuxiliaryRayScale);
}

AdaptiveAuxiliaryRayConfig ambientOcclusionAdaptiveRayConfig(bool enabled)
{
    AdaptiveAuxiliaryRayConfig config{};
    config.enabled = enabled;
    // equal thresholds collapse the intermediate state: coverage below the
    // Half entry stays Native, and coverage at/above it selects Half.
    config.qualityCoverageThreshold = config.halfCoverageThreshold;
    return config;
}

VkExtent2D auxiliaryRayExtent(VkExtent2D sceneExtent, float scale)
{
    if (sceneExtent.width == 0 || sceneExtent.height == 0)
    {
        return {};
    }

    const float sanitizedScale = sanitizeAuxiliaryRayScale(scale);
    const auto scaledDimension = [sanitizedScale](uint32_t dimension) {
        const double scaled = std::round(static_cast<double>(dimension) * sanitizedScale);
        return std::clamp(static_cast<uint32_t>(std::max(1.0, scaled)), 1u, dimension);
    };
    return {scaledDimension(sceneExtent.width), scaledDimension(sceneExtent.height)};
}

const char* auxiliaryRayScaleName(float scale)
{
    const float sanitizedScale = sanitizeAuxiliaryRayScale(scale);
    if (std::abs(sanitizedScale - kHalfResolutionAuxiliaryRayScale) < 0.001f)
    {
        return "Half";
    }
    if (std::abs(sanitizedScale - kQualityAuxiliaryRayScale) < 0.001f)
    {
        return "Quality";
    }
    return "Native";
}

uint32_t effectiveSunShadowSampleCount(uint32_t baseSampleCount,
                                       float effectiveScale,
                                       float effectiveAngularRadius,
                                       bool compensateReducedResolution)
{
    const uint32_t sanitizedSampleCount =
        std::clamp(baseSampleCount, 1u, kMaximumSunShadowSampleCount);
    if (!compensateReducedResolution)
    {
        return sanitizedSampleCount;
    }
    if (!std::isfinite(effectiveAngularRadius) || effectiveAngularRadius <= 1e-6f)
    {
        return sanitizedSampleCount;
    }

    const float sanitizedScale = sanitizeAuxiliaryRayScale(effectiveScale);
    const float sampleMultiplier = 1.0f + 2.0f * (1.0f - sanitizedScale);
    const uint32_t compensatedSampleCount = static_cast<uint32_t>(
        std::ceil(static_cast<float>(sanitizedSampleCount) * sampleMultiplier));
    return std::min(compensatedSampleCount, kMaximumSunShadowSampleCount);
}

bool useSunShadowNeighborhoodClamp(bool reconstructCurrent,
                                    float effectiveAngularRadius)
{
    if (!reconstructCurrent || !std::isfinite(effectiveAngularRadius) ||
        effectiveAngularRadius <= 1e-6f)
    {
        return true;
    }
    return false;
}

uint32_t effectiveAmbientOcclusionRayCount(uint32_t configuredRayCount,
                                           float effectiveScale)
{
    constexpr uint32_t kMaximumAoRayCount = 4u;
    const uint32_t sanitizedRayCount =
        std::clamp(configuredRayCount, 1u, kMaximumAoRayCount);
    const bool halfResolution =
        sanitizeAuxiliaryRayScale(effectiveScale) <=
        kHalfResolutionAuxiliaryRayScale + 0.001f;
    return halfResolution
               ? std::min(sanitizedRayCount * 2u, kMaximumAoRayCount)
               : sanitizedRayCount;
}

float effectiveAmbientOcclusionTemporalBlendAlpha(float configuredBlendAlpha,
                                                   float effectiveScale)
{
    constexpr float kDefaultAoBlendAlpha = 0.08f;
    const float sanitizedBlendAlpha =
        std::isfinite(configuredBlendAlpha)
            ? std::clamp(configuredBlendAlpha, 0.0f, 1.0f)
            : kDefaultAoBlendAlpha;
    const bool halfResolution =
        sanitizeAuxiliaryRayScale(effectiveScale) <=
        kHalfResolutionAuxiliaryRayScale + 0.001f;
    return halfResolution ? sanitizedBlendAlpha * 0.5f : sanitizedBlendAlpha;
}

bool useAmbientOcclusionSpatialFilter(float effectiveScale)
{
    return sanitizeAuxiliaryRayScale(effectiveScale) <=
           kHalfResolutionAuxiliaryRayScale + 0.001f;
}

const char* adaptiveAuxiliaryRayTierName(AdaptiveAuxiliaryRayTier tier)
{
    switch (tier)
    {
    case AdaptiveAuxiliaryRayTier::Native:
        return "Native";
    case AdaptiveAuxiliaryRayTier::Quality:
        return "Quality";
    case AdaptiveAuxiliaryRayTier::Half:
        return "Half";
    }
    return "Native";
}

const char* adaptiveAuxiliaryRayReasonName(AdaptiveAuxiliaryRayReason reason)
{
    switch (reason)
    {
    case AdaptiveAuxiliaryRayReason::Fixed:
        return "Fixed";
    case AdaptiveAuxiliaryRayReason::LowCoverage:
        return "LowCoverage";
    case AdaptiveAuxiliaryRayReason::HighCoverage:
        return "HighCoverage";
    case AdaptiveAuxiliaryRayReason::CameraInsideVolume:
        return "CameraInsideVolume";
    }
    return "Fixed";
}

namespace
{

float tierScale(AdaptiveAuxiliaryRayTier tier)
{
    switch (tier)
    {
    case AdaptiveAuxiliaryRayTier::Native:
        return kMaximumAuxiliaryRayScale;
    case AdaptiveAuxiliaryRayTier::Quality:
        return kQualityAuxiliaryRayScale;
    case AdaptiveAuxiliaryRayTier::Half:
        return kHalfResolutionAuxiliaryRayScale;
    }
    return kMaximumAuxiliaryRayScale;
}

bool sameExtent(VkExtent2D lhs, VkExtent2D rhs)
{
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

}  // namespace

AdaptiveAuxiliaryRayDecision resolveAdaptiveAuxiliaryRayResolution(
    VkExtent2D sceneExtent, float configuredScale, float projectedCoverage,
    bool cameraInsideVolume, const AdaptiveAuxiliaryRayConfig& config,
    AdaptiveAuxiliaryRayState& state)
{
    const float scaleLimit = sanitizeAuxiliaryRayScale(configuredScale);
    const float coverage = std::isfinite(projectedCoverage)
                               ? std::clamp(projectedCoverage, 0.0f, 1.0f)
                               : 1.0f;
    const float qualityEnter = std::clamp(config.qualityCoverageThreshold, 0.0f, 1.0f);
    const float halfEnter = std::clamp(
        std::max(config.halfCoverageThreshold, qualityEnter), 0.0f, 1.0f);
    const float hysteresis = std::isfinite(config.coverageHysteresis)
                                 ? std::clamp(config.coverageHysteresis, 0.0f, 0.25f)
                                 : 0.0f;
    const float qualityExit = std::max(qualityEnter - hysteresis, 0.0f);
    const float halfExit = std::max(halfEnter - hysteresis, qualityExit);

    AdaptiveAuxiliaryRayTier tier = AdaptiveAuxiliaryRayTier::Native;
    AdaptiveAuxiliaryRayReason reason = AdaptiveAuxiliaryRayReason::Fixed;
    if (config.enabled)
    {
        reason = AdaptiveAuxiliaryRayReason::LowCoverage;
        if (cameraInsideVolume)
        {
            tier = AdaptiveAuxiliaryRayTier::Half;
            reason = AdaptiveAuxiliaryRayReason::CameraInsideVolume;
        }
        else if (!state.initialized)
        {
            tier = coverage >= halfEnter
                       ? AdaptiveAuxiliaryRayTier::Half
                       : (coverage >= qualityEnter
                              ? AdaptiveAuxiliaryRayTier::Quality
                              : AdaptiveAuxiliaryRayTier::Native);
        }
        else
        {
            tier = state.tier;
            switch (state.tier)
            {
            case AdaptiveAuxiliaryRayTier::Native:
                if (coverage >= halfEnter)
                {
                    tier = AdaptiveAuxiliaryRayTier::Half;
                }
                else if (coverage >= qualityEnter)
                {
                    tier = AdaptiveAuxiliaryRayTier::Quality;
                }
                break;
            case AdaptiveAuxiliaryRayTier::Quality:
                if (coverage >= halfEnter)
                {
                    tier = AdaptiveAuxiliaryRayTier::Half;
                }
                else if (coverage < qualityExit)
                {
                    tier = AdaptiveAuxiliaryRayTier::Native;
                }
                break;
            case AdaptiveAuxiliaryRayTier::Half:
                if (coverage < qualityExit)
                {
                    tier = AdaptiveAuxiliaryRayTier::Native;
                }
                else if (coverage < halfExit)
                {
                    tier = AdaptiveAuxiliaryRayTier::Quality;
                }
                break;
            }
        }
        if (tier != AdaptiveAuxiliaryRayTier::Native)
        {
            reason = AdaptiveAuxiliaryRayReason::HighCoverage;
        }
    }

    const float effectiveScale = std::min(scaleLimit, tierScale(tier));
    const VkExtent2D activeExtent = auxiliaryRayExtent(sceneExtent, effectiveScale);
    AdaptiveAuxiliaryRayDecision decision{};
    decision.tier = tier;
    decision.reason = cameraInsideVolume && config.enabled
                          ? AdaptiveAuxiliaryRayReason::CameraInsideVolume
                          : reason;
    decision.effectiveScale = effectiveScale;
    decision.activeExtent = activeExtent;
    decision.changed = !state.initialized ||
                       !sameExtent(state.activeExtent, activeExtent);

    state.tier = tier;
    state.effectiveScale = effectiveScale;
    state.activeExtent = activeExtent;
    state.initialized = true;
    return decision;
}

void ProjectedCoverageAccumulator::includeBounds(
    const glm::mat4& clipFromLocal, const glm::vec3& boundsMin,
    const glm::vec3& boundsMax, bool fullScreenCoverageRequired)
{
    const auto fill = [this]() {
        rowMasks_.fill(std::numeric_limits<uint16_t>::max());
    };
    if (fullScreenCoverageRequired)
    {
        fill();
        return;
    }
    if (glm::any(glm::lessThanEqual(boundsMax, boundsMin)))
    {
        return;
    }

    glm::vec2 ndcMin(std::numeric_limits<float>::max());
    glm::vec2 ndcMax(std::numeric_limits<float>::lowest());
    bool sawFront = false;
    bool sawBehind = false;
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        const glm::vec3 local(
            (corner & 1u) != 0u ? boundsMax.x : boundsMin.x,
            (corner & 2u) != 0u ? boundsMax.y : boundsMin.y,
            (corner & 4u) != 0u ? boundsMax.z : boundsMin.z);
        const glm::vec4 clip = clipFromLocal * glm::vec4(local, 1.0f);
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
            !std::isfinite(clip.w))
        {
            fill();
            return;
        }
        if (clip.w <= 1e-5f)
        {
            sawBehind = true;
            continue;
        }
        sawFront = true;
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        ndcMin = glm::min(ndcMin, ndc);
        ndcMax = glm::max(ndcMax, ndc);
    }
    if (!sawFront)
    {
        return;
    }
    if (sawBehind)
    {
        fill();
        return;
    }

    ndcMin = glm::max(ndcMin, glm::vec2(-1.0f));
    ndcMax = glm::min(ndcMax, glm::vec2(1.0f));
    if (glm::any(glm::lessThan(ndcMax, ndcMin)))
    {
        return;
    }

    const glm::vec2 unitMin = ndcMin * 0.5f + 0.5f;
    const glm::vec2 unitMax = ndcMax * 0.5f + 0.5f;
    const uint32_t minColumn = std::min(
        static_cast<uint32_t>(std::floor(unitMin.x * kColumns)), kColumns - 1u);
    const uint32_t maxColumn = std::min(
        static_cast<uint32_t>(std::floor(unitMax.x * kColumns)), kColumns - 1u);
    const uint32_t minRow = std::min(
        static_cast<uint32_t>(std::floor(unitMin.y * kRows)), kRows - 1u);
    const uint32_t maxRow = std::min(
        static_cast<uint32_t>(std::floor(unitMax.y * kRows)), kRows - 1u);
    const uint32_t width = maxColumn - minColumn + 1u;
    const uint32_t bits = width >= kColumns
                              ? std::numeric_limits<uint16_t>::max()
                              : ((1u << width) - 1u) << minColumn;
    for (uint32_t row = minRow; row <= maxRow; ++row)
    {
        rowMasks_[row] |= static_cast<uint16_t>(bits);
    }
}

uint32_t ProjectedCoverageAccumulator::coveredCellCount() const
{
    uint32_t count = 0;
    for (uint16_t row : rowMasks_)
    {
        count += static_cast<uint32_t>(std::popcount(row));
    }
    return count;
}

float ProjectedCoverageAccumulator::coverage() const
{
    return static_cast<float>(coveredCellCount()) /
           static_cast<float>(kColumns * kRows);
}

}  // namespace engine::render
