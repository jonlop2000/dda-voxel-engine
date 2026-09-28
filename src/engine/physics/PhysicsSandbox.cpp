#include "engine/physics/PhysicsSandbox.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/geometric.hpp>

namespace engine::physics {

PhysicsSandbox::PhysicsSandbox()
{
    reset();
}

void PhysicsSandbox::loadPreset(Preset preset)
{
    preset_ = preset;
    reset();
    setPaused(true);
}

void PhysicsSandbox::reset()
{
    if (preset_ == Preset::FastCollision)
    {
        // a and b reach contact at 0.0045 s, inside the first 0.01 s step.
        // keep c resting away from their paths and preserve the scene's sphere order.
        balls_ = {
            {{-1.0, 2.0, 0.0}, {200.0, 0.0, 0.0}, 0.1, false},
            {{1.0, 2.0, 0.0}, {-200.0, 0.0, 0.0}, 0.1, false},
            {{0.0, 0.1, -3.0}, {0.0, 0.0, 0.0}, 0.1, true, 4.0}
        };
    }
    else if (preset_ == Preset::ThreeBallChain)
    {
        // equal masses transfer the incoming horizontal velocity at restitution one.
        // a reaches b at 0.004 s; b then reaches c at 0.008 s with that restitution.
        balls_ = {
            {{-1.0, 2.0, 0.0}, {200.0, 0.0, 0.0}, 0.1, false, 1.0},
            {{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1, false, 1.0},
            {{1.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1, false, 1.0}
        };
    }
    else
    {
        // restore all three balls, including their resting flags.
        balls_ = {
            {{-0.5, 2.0, 0.0}, {1.0, 0.0, 0.0}, 0.1, false},
            {{0.5, 2.0, 0.1}, {-1.0, 0.0, 0.0}, 0.1, false},
            // c weighs 4 kg and meets a after the initial a-b collision.
            {{-0.25, 2.0, -0.8}, {0.0, 0.0, 1.0}, 0.1, false, 4.0}
        };
    }
    accumulator_ = 0.0;
    elapsedTime_ = 0.0;
    recentCollisions_.clear();
    stepCollisions_.clear();
}

void PhysicsSandbox::setPaused(bool paused)
{
    if (paused_ != paused)
    {
        paused_ = paused;
        // Changing playback state discards any unfinished timestep.
        accumulator_ = 0.0;
    }
}

void PhysicsSandbox::setRestitution(double restitution)
{
    if (std::isfinite(restitution))
    {
        restitution_ = std::clamp(restitution, 0.0, 1.0);
    }
}

void PhysicsSandbox::setFloorFriction(double floorFriction)
{
    if (std::isfinite(floorFriction))
    {
        floorFriction_ = std::max(0.0, floorFriction);
    }
}

void PhysicsSandbox::update(double frameTime)
{
    if (paused_)
    {
        return;
    }
    accumulator_ += frameTime;
    while (accumulator_ >= timeStep)
    {
        advanceStep();
        accumulator_ -= timeStep;
    }
}

void PhysicsSandbox::step()
{
    if (paused_)
    {
        advanceStep();
    }
}

void PhysicsSandbox::advanceStep()
{
    advanceBallSystem(balls_, timeStep, elapsedTime_,
                      {-9.81, restitution_, floorFriction_}, stepCollisions_);
    for (const auto& impact : stepCollisions_)
    {
        if (recentCollisions_.size() == maxRecentCollisions) recentCollisions_.pop_front();
        recentCollisions_.push_back(impact);
    }
    elapsedTime_ += timeStep;
}

} // namespace engine::physics
