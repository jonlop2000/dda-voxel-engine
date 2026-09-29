#include "engine/physics/PhysicsSandbox.h"
#include "engine/physics/BallContact.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
using engine::physics::PhysicsSandbox;
using Preset = PhysicsSandbox::Preset;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool near(double a, double b, double tolerance = 1e-8)
{
    return std::abs(a - b) <= tolerance;
}

void sameBalls(const PhysicsSandbox& a, const PhysicsSandbox& b)
{
    require(a.balls().size() == b.balls().size(), "Ball counts differ");
    for (std::size_t i = 0; i < a.balls().size(); ++i)
    {
        const auto& first = a.balls()[i];
        const auto& second = b.balls()[i];
        require(first.position == second.position && first.velocity == second.velocity &&
                first.angularVelocity == second.angularVelocity && first.orientation == second.orientation &&
                first.radius == second.radius && first.mass == second.mass &&
                first.isResting == second.isResting, "Ball states differ");
    }
}

void sameState(const PhysicsSandbox& a, const PhysicsSandbox& b)
{
    sameBalls(a, b);
    require(a.elapsedTime() == b.elapsedTime(), "Simulation clocks differ");
    require(a.rotationEnabled() == b.rotationEnabled() && a.ballFriction() == b.ballFriction() &&
            a.wallFriction() == b.wallFriction() && a.rollingResistance() == b.rollingResistance() &&
            a.floorFriction() == b.floorFriction() && a.restitution() == b.restitution(),
            "Physics settings differ");
    const auto& first = a.recentCollisions();
    const auto& second = b.recentCollisions();
    require(first.size() == second.size(), "Collision counts differ");
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        require(first[i].ballA == second[i].ballA && first[i].ballB == second[i].ballB &&
                first[i].simulationTime == second[i].simulationTime &&
                first[i].momentumBefore == second[i].momentumBefore &&
                first[i].momentumAfter == second[i].momentumAfter &&
                first[i].kineticEnergyBefore == second[i].kineticEnergyBefore &&
                first[i].kineticEnergyAfter == second[i].kineticEnergyAfter &&
                first[i].includesRotation == second[i].includesRotation,
                "Collision histories differ");
    }
}

void finiteState(const PhysicsSandbox& sandbox)
{
    for (const auto& ball : sandbox.balls())
    {
        for (int axis = 0; axis < 3; ++axis)
            require(std::isfinite(ball.position[axis]) && std::isfinite(ball.velocity[axis]) &&
                    std::isfinite(ball.angularVelocity[axis]), "Non-finite ball state");
        require(near(glm::length(ball.orientation), 1.0, 1e-10), "Orientation lost normalization");
        require(ball.position.y >= ball.radius - 1e-6 &&
                std::abs(ball.position.x) <= 5.0 - ball.radius + 1e-6 &&
                std::abs(ball.position.z) <= 5.0 - ball.radius + 1e-6,
                "Ball escaped room boundaries");
    }
}

void checkSettings()
{
    PhysicsSandbox sandbox;
    require(sandbox.rotationEnabled() && sandbox.ballFriction() == 0.2 &&
            sandbox.wallFriction() == 0.2 && sandbox.rollingResistance() == 0.02,
            "Coupled sandbox defaults changed");
    require(!PhysicsSandbox(false).rotationEnabled(), "Legacy constructor ignored");
    using Setter = void (PhysicsSandbox::*)(double);
    using Getter = double (PhysicsSandbox::*)() const;
    const Setter setters[] = {&PhysicsSandbox::setBallFriction, &PhysicsSandbox::setWallFriction,
                              &PhysicsSandbox::setRollingResistance};
    const Getter getters[] = {&PhysicsSandbox::ballFriction, &PhysicsSandbox::wallFriction,
                              &PhysicsSandbox::rollingResistance};
    for (std::size_t i = 0; i < 3; ++i)
    {
        (sandbox.*setters[i])(-1.0);
        require((sandbox.*getters[i])() == 0.0, "Negative coefficient did not clamp");
        (sandbox.*setters[i])(1.5);
        for (double invalid : {std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()})
            (sandbox.*setters[i])(invalid);
        require((sandbox.*getters[i])() == 1.5, "Coefficient validation lost a valid value");
    }
    sandbox.setBallFriction(0.3);
    sandbox.setWallFriction(0.4);
    sandbox.setRollingResistance(0.015);
    const auto initial = sandbox;
    sandbox.setRotationEnabled(false);
    sameBalls(sandbox, initial);
    for (Preset preset : {Preset::SlideToRoll, Preset::SpinCollision, Preset::FreeSpin,
                          Preset::Default})
    {
        sandbox.loadPreset(preset);
        sandbox.reset();
        require(!sandbox.rotationEnabled() && sandbox.ballFriction() == 0.3 &&
                sandbox.wallFriction() == 0.4 && sandbox.rollingResistance() == 0.015 &&
                sandbox.isPaused(), "Reset or preset switch changed saved settings");
    }
}

void checkPlayback()
{
    for (Preset preset : {Preset::SlideToRoll, Preset::SpinCollision})
    {
        PhysicsSandbox manual;
        manual.setPlaybackSpeed(0.5);
        manual.loadPreset(preset);
        require(manual.isPaused() && manual.balls().size() == 3, "Preset did not reset and pause");
        const auto initial = manual;
        manual.update(1.0);
        sameState(manual, initial);
        auto automatic = manual;
        automatic.setPaused(false);
        for (int step = 0; step < 100; ++step)
        {
            manual.step();
            automatic.update(0.02);
            sameState(manual, automatic);
            finiteState(manual);
        }
        automatic.setPaused(true);
        const auto paused = automatic;
        automatic.update(1.0);
        sameState(automatic, paused);
        manual.reset();
        sameState(manual, initial);
        require(manual.isPaused() && manual.playbackSpeed() == 0.5,
                "Reset changed playback settings");
    }
}

void checkIdealRolling()
{
    PhysicsSandbox sandbox;
    sandbox.setRollingResistance(0.0);
    sandbox.loadPreset(Preset::SlideToRoll);
    for (int step = 0; step < 60; ++step) sandbox.step();
    const double expectedSpeeds[] = {1.5 * 5.0 / 7.0, 1.5, 1.5 * 3.0 / 7.0};
    for (std::size_t i = 0; i < sandbox.balls().size(); ++i)
    {
        const auto& ball = sandbox.balls()[i];
        require(near(ball.velocity.x, expectedSpeeds[i], 1e-7) &&
                near(ball.angularVelocity.z, -expectedSpeeds[i] / ball.radius, 1e-7),
                "Sliding did not reach the analytic rolling state");
        const auto slip = engine::physics::contactPointVelocity(ball, {0.0, -ball.radius, 0.0});
        require(glm::length(slip) < 1e-7 && !ball.isResting,
                "Rolling was mistaken for stopped or slipping");
    }
    require(sandbox.recentCollisions().empty(), "Separate lanes collided prematurely");
}

void checkFreeSpinOverride()
{
    PhysicsSandbox sandbox;
    sandbox.setRollingResistance(1.0);
    sandbox.setFloorFriction(1.0);
    sandbox.loadPreset(Preset::FreeSpin);
    const auto initial = sandbox;
    for (int step = 0; step < 100; ++step) sandbox.step();
    for (std::size_t i = 0; i < sandbox.balls().size(); ++i)
    {
        const auto& ball = sandbox.balls()[i];
        require(ball.position == initial.balls()[i].position &&
                ball.angularVelocity == initial.balls()[i].angularVelocity &&
                ball.velocity == glm::dvec3{0.0}, "Free-spin override changed motion");
        require(near(glm::length(ball.angularVelocity), glm::half_pi<double>()),
                "Free-spin speed changed");
    }
    require(sandbox.rotationEnabled() && sandbox.hasMotion(), "Free spin lost settings or idle state");
    sandbox.loadPreset(Preset::SlideToRoll);
    require(sandbox.rotationEnabled() && sandbox.rollingResistance() == 1.0,
            "Leaving free spin failed to restore coupling");
}

void checkSpinCollision()
{
    PhysicsSandbox sandbox;
    sandbox.loadPreset(Preset::SpinCollision);
    for (int step = 0; step < 60; ++step) sandbox.step();
    finiteState(sandbox);
    require(!sandbox.recentCollisions().empty(), "Spin-transfer preset missed its collision");
    const auto event = sandbox.recentCollisions().front();
    require(event.ballA == 0 && event.ballB == 1 && event.includesRotation,
            "Spin-transfer history lost pair or energy type");
    require(event.kineticEnergyAfter <= event.kineticEnergyBefore + 1e-8 &&
            glm::length(event.momentumAfter - event.momentumBefore) < 1e-8,
            "Spin collision added energy or changed pair momentum");
    require(glm::length(sandbox.balls()[1].angularVelocity) > 1e-5,
            "Contact friction failed to spin the target ball");
    sandbox.setRotationEnabled(false);
    require(sandbox.recentCollisions().front().includesRotation &&
            sandbox.recentCollisions().front().kineticEnergyBefore == event.kineticEnergyBefore,
            "Changing coupling rewrote collision history");
    sandbox.reset();
    for (int step = 0; step < 60; ++step) sandbox.step();
    require(!sandbox.recentCollisions().empty() &&
            !sandbox.recentCollisions().front().includesRotation,
            "Disabled coupling did not record translation-only history");
}

void checkCoupledStacks()
{
    for (std::size_t count : {32u, 100u, 250u})
    {
        PhysicsSandbox sandbox;
        sandbox.setStressBallCount(count);
        sandbox.loadPreset(Preset::StressStacks);
        for (int step = 0; step < 1000; ++step)
        {
            sandbox.step();
            finiteState(sandbox);
        }
        for (const auto& ball : sandbox.balls())
            require(ball.isResting && ball.velocity == glm::dvec3{0.0} &&
                    ball.angularVelocity == glm::dvec3{0.0}, "Coupled stack did not settle");
        require(!sandbox.hasMotion(), "Settled coupled stacks prevent idle throttling");
    }
}
} // namespace

int main()
{
    try
    {
        checkSettings();
        std::cout << "PASS coupled settings and preservation" << std::endl;
        checkPlayback();
        std::cout << "PASS coupled preset playback and reset" << std::endl;
        checkIdealRolling();
        std::cout << "PASS analytic sliding-to-rolling presets" << std::endl;
        checkFreeSpinOverride();
        std::cout << "PASS educational free-spin override" << std::endl;
        checkSpinCollision();
        std::cout << "PASS spin transfer and recorded total energy" << std::endl;
        checkCoupledStacks();
        std::cout << "PASS coupled 32/100/250-ball settling stacks" << std::endl;
        std::cout << "Coupled sandbox settings, presets, playback, and stacks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
