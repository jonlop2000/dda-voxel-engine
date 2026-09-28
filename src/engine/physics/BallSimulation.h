#pragma once

#include <cstddef>
#include <vector>

#include "engine/physics/BallPhysics.h"

namespace engine::physics {

struct BallCollisionEvent
{
    std::size_t ballA;
    std::size_t ballB;
    double simulationTime; // actual contact time, including its position within a fixed step.
    glm::dvec3 momentumBefore{0.0}; // total pair momentum in kg*m/s.
    glm::dvec3 momentumAfter{0.0};
    double kineticEnergyBefore = 0.0; // total pair kinetic energy in joules.
    double kineticEnergyAfter = 0.0;
    double restitution = 0.0; // restitution used by this impact.
};

struct BallSimulationSettings
{
    double acceleration = -9.81;
    double restitution = 0.8;
    double floorFriction = 0.2;
};

// advance valid balls inside the sandbox, processing contacts in time order.
// output events are replaced on each call; their times include the supplied start time.
void advanceBallSystem(std::vector<Ball>& balls, double duration, double startTime,
                       const BallSimulationSettings& settings,
                       std::vector<BallCollisionEvent>& impacts);

} // namespace engine::physics
