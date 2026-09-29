#pragma once

#include "engine/physics/BallBroadPhase.h"
#include "engine/physics/BallSimulation.h"

namespace engine::physics {

struct ContactAccelerations;

// sleep quiet grounded groups only when both forces and torques balance.
void settleRotatingBalls(std::vector<Ball>& balls,
                         const BallSimulationSettings& settings,
                         ContactAccelerations& motion, BallBroadPhase& broadPhase);

} // namespace engine::physics
