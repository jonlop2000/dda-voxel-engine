#include "engine/physics/PhysicsSandbox.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace engine::physics {

PhysicsSandbox::PhysicsSandbox(bool enableRotation)
    : rotationEnabled_(enableRotation)
{
    reset();
}

void PhysicsSandbox::loadPreset(Preset preset)
{
    preset_ = preset;
    reset();
    setPaused(true);
}

bool PhysicsSandbox::isStressPreset() const
{
    return preset_ == Preset::StressDrop || preset_ == Preset::StressPairs ||
           preset_ == Preset::StressStacks;
}

bool PhysicsSandbox::hasMotion() const
{
    return std::any_of(balls_.begin(), balls_.end(), [](const Ball& ball) {
        return !ball.isResting || ball.angularVelocity != glm::dvec3{0.0};
    });
}

void PhysicsSandbox::setStressBallCount(std::size_t count)
{
    count = std::clamp(count, std::size_t{2}, maxStressBalls);
    count -= count % 2;
    if (stressBallCount_ == count) return;
    stressBallCount_ = count;
    if (isStressPreset()) loadPreset(preset_);
}

void PhysicsSandbox::setPlaybackSpeed(double speed)
{
    if (std::isfinite(speed)) playbackSpeed_ = std::clamp(speed, 0.1, 1.0);
}

void PhysicsSandbox::resetStressBalls()
{
    balls_.clear();
    balls_.reserve(stressBallCount_);
    for (std::size_t i = 0; i < stressBallCount_; ++i)
    {
        if (preset_ == Preset::StressStacks)
        {
            const std::size_t column = i / 4;
            const std::size_t level = i % 4;
            balls_.push_back({{-3.5 + static_cast<double>(column % 8),
                              0.1 + static_cast<double>(level) * 0.4,
                              -3.5 + static_cast<double>(column / 8)},
                             {0.0, 0.0, 0.0}, 0.1, level == 0});
            continue;
        }
        const bool pairs = preset_ == Preset::StressPairs;
        const std::size_t cell = pairs ? i / 2 : i;
        const std::size_t columns = pairs ? 8 : 16;
        const double x = pairs ? -3.5 + static_cast<double>(cell % columns) :
                                 -3.75 + static_cast<double>(cell % columns) * 0.5;
        const double z = -3.75 + static_cast<double>(cell / columns) * 0.5;
        if (pairs)
        {
            // each pair first touches at one third of a simulated second.
            const double side = i % 2 == 0 ? -1.0 : 1.0;
            balls_.push_back({{x + side * 0.35, 1.6, z},
                              {-side * 0.75, 0.0, 0.0}, 0.1});
        }
        else
        {
            const double height = 1.2 + static_cast<double>(i % 4) * 0.4;
            balls_.push_back({{x, height, z}, {0.0, 0.0, 0.0}, 0.1});
        }
    }
}

void PhysicsSandbox::reset()
{
    if (isStressPreset())
    {
        resetStressBalls();
    }
    else if (preset_ == Preset::FreeSpin)
    {
        balls_.clear();
        for (int axis = 0; axis < 3; ++axis)
        {
            balls_.push_back({{0.75 * (axis - 1), 0.25, 0.0},
                              {0.0, 0.0, 0.0}, 0.25, true});
            // each ball completes one turn in four simulated seconds.
            balls_.back().angularVelocity[axis] = glm::half_pi<double>();
        }
    }
    else if (preset_ == Preset::SlideToRoll)
    {
        balls_.clear();
        for (int lane = 0; lane < 3; ++lane)
        {
            balls_.push_back({{-2.0, 0.25, 0.8 * (lane - 1)},
                              {1.5, 0.0, 0.0}, 0.25});
            // compare no spin, forward rolling, and backspin.
            balls_.back().angularVelocity.z = lane == 0 ? 0.0 :
                                             (lane == 1 ? -6.0 : 6.0);
        }
    }
    else if (preset_ == Preset::SpinCollision)
    {
        balls_ = {
            {{-1.25, 2.5, 0.0}, {2.0, 0.0, 0.0}, 0.25},
            {{0.25, 2.5, 0.15}, {0.0, 0.0, 0.0}, 0.25},
            {{2.0, 0.25, -1.5}, {0.0, 0.0, 0.0}, 0.25, true}
        };
        balls_[0].angularVelocity.y = 8.0;
    }
    else if (preset_ == Preset::FastCollision)
    {
        // a and b collide at 0.0045 s while c rests away from their paths.
        balls_ = {
            {{-1.0, 2.0, 0.0}, {200.0, 0.0, 0.0}, 0.1, false},
            {{1.0, 2.0, 0.0}, {-200.0, 0.0, 0.0}, 0.1, false},
            {{0.0, 0.1, -3.0}, {0.0, 0.0, 0.0}, 0.1, true, 4.0}
        };
    }
    else if (preset_ == Preset::ThreeBallChain)
    {
        // restitution one gives a-b contact at 0.004 s and b-c at 0.008 s.
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
    lastStepMilliseconds_ = 0.0;
    lastUpdateMilliseconds_ = 0.0;
    lastUpdateSteps_ = 0;
    discardedTime_ = 0.0;
    recentCollisions_.clear();
    stepCollisions_.clear();
}

void PhysicsSandbox::setPaused(bool paused)
{
    if (paused_ != paused)
    {
        paused_ = paused;
        // changing playback state discards any unfinished timestep.
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

void PhysicsSandbox::setBallFriction(double friction)
{
    if (std::isfinite(friction)) ballFriction_ = std::max(0.0, friction);
}

void PhysicsSandbox::setWallFriction(double friction)
{
    if (std::isfinite(friction)) wallFriction_ = std::max(0.0, friction);
}

void PhysicsSandbox::setRollingResistance(double resistance)
{
    if (std::isfinite(resistance)) rollingResistance_ = std::max(0.0, resistance);
}

void PhysicsSandbox::update(double frameTime)
{
    lastUpdateMilliseconds_ = 0.0;
    lastUpdateSteps_ = 0;
    if (paused_ || !std::isfinite(frameTime) || frameTime <= 0.0) return;
    accumulator_ += frameTime * playbackSpeed_;
    while (accumulator_ >= timeStep)
    {
        if (isStressPreset() && lastUpdateSteps_ == maxStressStepsPerFrame)
        {
            // discard whole-step debt so slow frames cannot grow a backlog.
            const double remainder = std::fmod(accumulator_, timeStep);
            discardedTime_ += accumulator_ - remainder;
            accumulator_ = remainder;
            break;
        }
        advanceStep();
        lastUpdateMilliseconds_ += lastStepMilliseconds_;
        ++lastUpdateSteps_;
        accumulator_ -= timeStep;
    }
}

void PhysicsSandbox::step()
{
    if (paused_)
    {
        advanceStep();
        lastUpdateMilliseconds_ = lastStepMilliseconds_;
        lastUpdateSteps_ = 1;
    }
}

void PhysicsSandbox::advanceStep()
{
    const auto start = std::chrono::steady_clock::now();
    advanceBallSystem(balls_, timeStep, elapsedTime_,
                      {-9.81, restitution_, floorFriction_, true,
                       rotationEnabled_ && preset_ != Preset::FreeSpin,
                       ballFriction_, wallFriction_, rollingResistance_},
                      stepCollisions_);
    lastStepMilliseconds_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    for (const auto& impact : stepCollisions_)
    {
        if (recentCollisions_.size() == maxRecentCollisions) recentCollisions_.pop_front();
        recentCollisions_.push_back(impact);
    }
    elapsedTime_ += timeStep;
}

} // namespace engine::physics
