#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

#include "engine/scene/EnvironmentWind.h"
#include "engine/scene/WindborneParticleSettings.h"

namespace engine::scene
{

// static scene-space envelope and palette contract used to author one bounded
// windborne field. runtime controls never regenerate these candidates.
struct WindborneParticleDomain
{
    glm::vec3 minimum{-12.0f, 4.0f, -10.0f};
    glm::vec3 maximum{12.0f, 8.0f, 10.0f};
    uint32_t leafMaterialBase = 0u;
    uint32_t leafMaterialCount = 0u;
    uint32_t moteMaterialBase = 0u;
    uint32_t moteMaterialCount = 0u;
};

// one stable candidate in the fixed wind-002 budget. selectionRank and
// kindRank are normalized deterministic ranks, allowing amount and leaf mix
// to change live without reallocating or uploading particle data.
struct WindborneParticleCandidate
{
    uint32_t stableId = 0u;
    glm::vec3 anchorWorld{0.0f};
    float phaseRadians = 0.0f;
    float selectionRank = 0.0f;
    float kindRank = 0.0f;
    float travelAmplitude = 0.0f;
    float motionSpeed = 0.0f;
    float flutter = 0.0f;
    uint32_t randomSeed = 0u;
    uint32_t leafMaterialId = 0u;
    uint32_t moteMaterialId = 0u;
};

struct WindborneParticleSample
{
    glm::vec3 worldPosition{0.0f};
    bool active = false;
    bool leaf = true;
};

[[nodiscard]] bool validWindborneParticleDomain(
    const WindborneParticleDomain& domain) noexcept;

// always returns the fixed ceiling for a valid domain. candidate order is
// selection-rank order, so a single prefix is the complete active subset.
[[nodiscard]] std::vector<WindborneParticleCandidate>
buildWindborneParticleField(const WindborneParticleDomain& domain,
                            uint32_t worldSeed);

[[nodiscard]] uint32_t activeWindborneParticleCount(
    const WindborneParticleSettings& settings,
    uint32_t residentCandidateCount = kMaxWindborneParticleCount) noexcept;

// cpu reference for tests/tooling. the production vertex shader implements the
// same bounded closed-loop motion and never integrates mutable particle state.
[[nodiscard]] WindborneParticleSample sampleWindborneParticle(
    const WindborneParticleCandidate& candidate,
    const WindborneParticleSettings& settings,
    const EnvironmentWindSettings& wind,
    float timeSeconds) noexcept;

[[nodiscard]] bool windborneParticleFieldEquivalent(
    std::span<const WindborneParticleCandidate> lhs,
    std::span<const WindborneParticleCandidate> rhs,
    float epsilon = 1e-5f) noexcept;

} // namespace engine::scene
