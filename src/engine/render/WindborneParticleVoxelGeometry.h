#pragma once

#include <span>
#include <vector>

#include "engine/render/FoliageVoxelGeometry.h"
#include "engine/scene/WindborneParticleField.h"

namespace engine::render
{

inline constexpr uint32_t kWindborneParticlePrimitiveFlag = 0x80000000u;

// packs the fixed wind-002 candidates into the established foliage ssbo abi.
// no new descriptor, allocation type, or draw call is introduced.
[[nodiscard]] std::vector<FoliageGpuPrimitive>
buildWindborneParticleVoxelGeometry(
    std::span<const engine::scene::WindborneParticleCandidate> candidates);

} // namespace engine::render
