#include "engine/scene/WindborneParticleSettings.h"

#include <algorithm>
#include <cmath>

namespace engine::scene
{
namespace
{
constexpr WindborneParticleSettings kDefaults{};

float finiteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}
} // namespace

const WindborneParticleSettings& defaultWindborneParticleSettings()
{
    return kDefaults;
}

WindborneParticleSettings sanitizeWindborneParticleSettings(
    const WindborneParticleSettings& settings)
{
    WindborneParticleSettings result = settings;
    result.amount =
        std::clamp(finiteOr(result.amount, kDefaults.amount), 0.0f, 1.0f);
    result.leafFraction = std::clamp(
        finiteOr(result.leafFraction, kDefaults.leafFraction), 0.0f, 1.0f);
    result.scale =
        std::clamp(finiteOr(result.scale, kDefaults.scale), 0.5f, 1.75f);
    result.visibilityDistance =
        std::clamp(finiteOr(result.visibilityDistance,
                            kDefaults.visibilityDistance),
                   8.0f, 64.0f);
    return result;
}

bool windborneParticleSettingsEquivalent(
    const WindborneParticleSettings& lhs,
    const WindborneParticleSettings& rhs,
    float epsilon)
{
    const float tolerance = std::max(epsilon, 0.0f);
    const auto close = [tolerance](float a, float b) {
        return std::abs(a - b) <= tolerance;
    };
    return lhs.enabled == rhs.enabled && close(lhs.amount, rhs.amount) &&
           close(lhs.leafFraction, rhs.leafFraction) &&
           close(lhs.scale, rhs.scale) &&
           close(lhs.visibilityDistance, rhs.visibilityDistance);
}

} // namespace engine::scene
