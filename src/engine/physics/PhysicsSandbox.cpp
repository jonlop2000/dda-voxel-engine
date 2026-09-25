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

void PhysicsSandbox::reset()
{
    // Restore all three balls, including their resting flags.
    balls_ = {
        {{-0.5, 2.0, 0.0}, {1.0, 0.0, 0.0}, 0.1, false},
        {{0.5, 2.0, 0.1}, {-1.0, 0.0, 0.0}, 0.1, false},
        // C weighs 4 kg and meets A after the initial A-B collision.
        {{-0.25, 2.0, -0.8}, {0.0, 0.0, 1.0}, 0.1, false, 4.0}
    };
    accumulator_ = 0.0;
    elapsedTime_ = 0.0;
    recentCollisions_.clear();
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
    const double gravityAcceleration = -9.81;
    // bring every ball to the same moment before resolving any pairs.
    for (auto& ball : balls_)
    {
        advanceBall(ball, gravityAcceleration, timeStep, restitution_, floorFriction_);
    }
    const double stepEndTime = elapsedTime_ + timeStep;
    // Starting j after i avoids self-collisions and duplicate pairs.
    for (std::size_t i = 0; i < balls_.size(); ++i)
    {
        for (std::size_t j = i + 1; j < balls_.size(); ++j)
        {
            auto& ballA = balls_[i];
            auto& ballB = balls_[j];
            // gravity and boundary responses have already run. Measure only this pair's impact.
            const glm::dvec3 momentumBefore = ballA.mass * ballA.velocity + ballB.mass * ballB.velocity;
            const double kineticEnergyBefore =
                0.5 * ballA.mass * glm::dot(ballA.velocity, ballA.velocity)
                + 0.5 * ballB.mass * glm::dot(ballB.velocity, ballB.velocity);
            if (resolveBallCollision(ballA, ballB, restitution_))
            {
                const glm::dvec3 momentumAfter = ballA.mass * ballA.velocity + ballB.mass * ballB.velocity;
                const double kineticEnergyAfter =
                    0.5 * ballA.mass * glm::dot(ballA.velocity, ballA.velocity)
                    + 0.5 * ballB.mass * glm::dot(ballB.velocity, ballB.velocity);
                if (recentCollisions_.size() == maxRecentCollisions)
                {
                    recentCollisions_.pop_front();
                }
                recentCollisions_.push_back({i, j, stepEndTime, momentumBefore, momentumAfter,
                                             kineticEnergyBefore, kineticEnergyAfter, restitution_});
            }
        }
    }
    elapsedTime_ = stepEndTime;
}

} // namespace engine::physics
