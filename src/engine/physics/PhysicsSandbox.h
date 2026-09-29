#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "engine/physics/BallSimulation.h"

namespace engine::physics {

// own the experiment and its clock.
class PhysicsSandbox
{
public:
    enum class Preset { Default, FastCollision, ThreeBallChain, StressDrop,
                        StressPairs, StressStacks, FreeSpin, SlideToRoll, SpinCollision };

    explicit PhysicsSandbox(bool enableRotation = true);

    // reset and pause the new preset while keeping restitution and friction.
    void loadPreset(Preset preset);
    Preset preset() const { return preset_; }
    bool isStressPreset() const;
    // report translation or spin independently of playback state.
    bool hasMotion() const;
    // changing the count resets and pauses an active stress scene.
    void setStressBallCount(std::size_t count);
    std::size_t stressBallCount() const { return stressBallCount_; }
    static constexpr std::size_t maxStressBalls = 250;
    static constexpr std::size_t maxStressStepsPerFrame = 4;

    void setPlaybackSpeed(double speed);
    double playbackSpeed() const { return playbackSpeed_; }
    double lastStepMilliseconds() const { return lastStepMilliseconds_; }
    double lastUpdateMilliseconds() const { return lastUpdateMilliseconds_; }
    std::size_t lastUpdateSteps() const { return lastUpdateSteps_; }
    double discardedTime() const { return discardedTime_; }
    std::size_t lastStepImpacts() const { return stepCollisions_.size(); }

    // reset the preset and history while keeping pause and physics settings.
    void reset();
    void setPaused(bool paused);
    bool isPaused() const { return paused_; }

    // clamp finite values to [0, 1]; ignore non-finite input.
    void setRestitution(double restitution);
    double restitution() const { return restitution_; }

    // clamp finite values to zero or above; ignore non-finite input.
    void setFloorFriction(double floorFriction);
    double floorFriction() const { return floorFriction_; }

    // free spin overrides coupling without changing the saved setting.
    void setRotationEnabled(bool enabled) { rotationEnabled_ = enabled; }
    bool rotationEnabled() const { return rotationEnabled_; }
    void setBallFriction(double friction);
    double ballFriction() const { return ballFriction_; }
    void setWallFriction(double friction);
    double wallFriction() const { return wallFriction_; }
    void setRollingResistance(double resistance);
    double rollingResistance() const { return rollingResistance_; }

    // advance fixed steps, limiting stress scenes to four steps per frame.
    void update(double frameTime);
    // advance exactly one fixed step while paused.
    void step();

    const std::vector<Ball>& balls() const { return balls_; }
    // store impacts oldest first.
    const std::deque<BallCollisionEvent>& recentCollisions() const { return recentCollisions_; }
    static constexpr std::size_t maxRecentCollisions = 8;
    double elapsedTime() const { return elapsedTime_; }
    static constexpr double timeStep = 0.01;

private:
    void advanceStep();
    void resetStressBalls();

    // match the scene's sphere order.
    std::vector<Ball> balls_;
    std::deque<BallCollisionEvent> recentCollisions_;
    std::vector<BallCollisionEvent> stepCollisions_;
    std::size_t stressBallCount_ = 100;
    double playbackSpeed_ = 1.0;
    double lastStepMilliseconds_ = 0.0;
    double lastUpdateMilliseconds_ = 0.0;
    std::size_t lastUpdateSteps_ = 0;
    double discardedTime_ = 0.0;
    double accumulator_ = 0.0;
    double elapsedTime_ = 0.0;
    Preset preset_ = Preset::Default;
    bool paused_ = false;
    double restitution_ = 0.8;
    double floorFriction_ = 0.2; // dimensionless sliding coefficient.
    bool rotationEnabled_ = true;
    double ballFriction_ = 0.2;
    double wallFriction_ = 0.2;
    double rollingResistance_ = 0.02;
};

} // namespace engine::physics
