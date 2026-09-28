#pragma once

#include <cstddef>
#include <vector>

#include "engine/physics/BallPhysics.h"

namespace engine::physics {

struct BallCollisionEvent
{
    std::size_t ballA;
    std::size_t ballB;
    double simulationTime; // absolute contact time in seconds.
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
    // disable filtering to use the all-pairs reference search.
    bool useBroadPhase = true;
};

// advance valid balls through contacts in time order, replacing output events.
void advanceBallSystem(std::vector<Ball>& balls, double duration, double startTime,
                       const BallSimulationSettings& settings,
                       std::vector<BallCollisionEvent>& impacts);

} // namespace engine::physics
