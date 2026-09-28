#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "engine/physics/BallSimulation.h"

namespace engine::physics {

// Owns the experiment and its clock; rendering and UI remain in App.
class PhysicsSandbox
{
public:
    enum class Preset { Default, FastCollision, ThreeBallChain };

    PhysicsSandbox();

    // selecting a preset resets and pauses, preserving restitution and friction.
    void loadPreset(Preset preset);
    Preset preset() const { return preset_; }

    // restore the selected preset, clocks, and history, preserving pause and physics settings.
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
    std::vector<BallCollisionEvent> stepCollisions_;
    double accumulator_ = 0.0;
    double elapsedTime_ = 0.0;
    Preset preset_ = Preset::Default;
    bool paused_ = false;
    double restitution_ = 0.8;
    double floorFriction_ = 0.2; // sliding friction coefficient for the floor (dimensionless).
};

} // namespace engine::physics
