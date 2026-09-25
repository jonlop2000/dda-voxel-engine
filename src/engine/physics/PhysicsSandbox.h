#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "engine/physics/BallPhysics.h"

namespace engine::physics {

struct BallCollisionEvent
{
    std::size_t ballA;
    std::size_t ballB;
    double simulationTime; // end of the fixed step that resolved the impact.

    // total momentum of this pair, immediately around its collision response (kg*m/s).
    glm::dvec3 momentumBefore{0.0};
    glm::dvec3 momentumAfter{0.0};

    // store combined kinetic energy of two colliding balls 
    double kineticEnergyBefore = 0.0;
    double kineticEnergyAfter = 0.0;
    double restitution = 0.0; // setting used for this impact, even if the slider changes later.
};

// Owns the experiment and its clock; rendering and UI remain in App.
class PhysicsSandbox
{
public:
    PhysicsSandbox();

    // restore the experiment, clocks, and history, preserving pause and physics settings.
    void reset();
    void setPaused(bool paused);
    bool isPaused() const { return paused_; }

    // clamp finite values to [0, 1]; ignore non-finite input.
    void setRestitution(double restitution);
    double restitution() const { return restitution_; }

    // clamp finite values to zero or above; ignore non-finite input.
    void setFloorFriction(double floorFriction);
    double floorFriction() const { return floorFriction_; }

    // Accumulate running time and advance complete fixed steps only.
    void update(double frameTime);
    // Advance exactly one fixed step while paused.
    void step();

    const std::vector<Ball>& balls() const { return balls_; }
    // Stored oldest first; UI can iterate backward to show the newest impact first.
    const std::deque<BallCollisionEvent>& recentCollisions() const { return recentCollisions_; }
    static constexpr std::size_t maxRecentCollisions = 8;
    double elapsedTime() const { return elapsedTime_; }
    static constexpr double timeStep = 0.01;

private:
    void advanceStep();

    // The scene creates its sphere volumes in this same order.
    std::vector<Ball> balls_;
    std::deque<BallCollisionEvent> recentCollisions_;
    double accumulator_ = 0.0;
    double elapsedTime_ = 0.0;
    bool paused_ = false;
    double restitution_ = 0.8;
    double floorFriction_ = 0.2; // sliding friction coefficient for the floor (dimensionless).
};

} // namespace engine::physics
