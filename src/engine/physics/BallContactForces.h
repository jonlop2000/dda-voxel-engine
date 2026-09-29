#pragma once

#include "engine/physics/BallSimulation.h"
#include "engine/physics/BallBroadPhase.h"

namespace engine::physics {

struct ContactAccelerations
{
    std::vector<glm::dvec3> linear;
    std::vector<glm::dvec3> angular;
    double horizon = 0.0;
};

// solve sustained contact forces and bound changes in sliding or spin.
ContactAccelerations calculateContactAccelerations(
    const std::vector<Ball>& balls, const BallSimulationSettings& settings,
    BallBroadPhase& broadPhase, double maxDuration);

} // namespace engine::physics
