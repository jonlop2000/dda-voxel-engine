#pragma once

#include "engine/physics/BallBroadPhase.h"

namespace engine::physics {

// settle quiet contact groups only when their supports can balance gravity.
void settleSupportedBalls(std::vector<Ball>& balls, double gravity, double floorFriction,
                          BallBroadPhase& broadPhase, bool useBroadPhase);

} // namespace engine::physics
