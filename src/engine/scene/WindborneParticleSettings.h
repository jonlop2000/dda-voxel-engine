#pragma once

#include <cstdint>

namespace engine::scene
{

// fixed implementation ceiling shared by authoring, cpu generation, and tests.
// the scene-owned amount selects a deterministic subset; authored content cannot
// raise the thermal budget above this limit.
inline constexpr uint32_t kMaxWindborneParticleCount = 64u;

struct WindborneParticleSettings
{
    bool enabled = false;
    float amount = 0.65f;
    float leafFraction = 0.70f;
    float scale = 1.0f;
    float visibilityDistance = 36.0f;
};

[[nodiscard]] const WindborneParticleSettings&
defaultWindborneParticleSettings();
[[nodiscard]] WindborneParticleSettings sanitizeWindborneParticleSettings(
    const WindborneParticleSettings& settings);
[[nodiscard]] bool windborneParticleSettingsEquivalent(
    const WindborneParticleSettings& lhs,
    const WindborneParticleSettings& rhs,
    float epsilon = 1e-5f);

} // namespace engine::scene
