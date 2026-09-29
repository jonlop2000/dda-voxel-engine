#include "engine/physics/BallRotationalSupport.h"
#include "engine/physics/BallContactForces.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace engine::physics;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

ContactAccelerations settle(std::vector<Ball>& balls,
                             const BallSimulationSettings& settings)
{
    BallBroadPhase broadPhase;
    auto motion = calculateContactAccelerations(balls, settings, broadPhase, 0.01);
    settleRotatingBalls(balls, settings, motion, broadPhase);
    return motion;
}

std::vector<Ball> column()
{
    std::vector<Ball> balls;
    for (int level = 0; level < 4; ++level)
        balls.push_back({{0.0, 0.1 + level * 0.2, 0.0}, {0.0, 0.0, 0.0},
                         0.1, false, 4.0 - level});
    return balls;
}

BallSimulationSettings rotationalSettings()
{
    BallSimulationSettings settings;
    settings.enableRotation = true;
    return settings;
}

void checkExactSupportForces()
{
    auto settings = rotationalSettings();
    const auto initial = column();
    for (const bool filtered : {true, false})
    {
        settings.useBroadPhase = filtered;
        std::array<std::size_t, 4> order{0, 1, 2, 3};
        do
        {
            std::vector<Ball> balls;
            for (const auto index : order) balls.push_back(initial[index]);
            BallBroadPhase broadPhase;
            const auto motion = calculateContactAccelerations(balls, settings, broadPhase, 0.01);
            require(motion.horizon == 0.01, "Exact support unnecessarily shortened the interval");
            for (std::size_t i = 0; i < balls.size(); ++i)
                require(motion.linear[i] == glm::dvec3{0.0} &&
                        motion.angular[i] == glm::dvec3{0.0},
                        "An unequal-mass column lacks exact support in this index order");
        } while (std::next_permutation(order.begin(), order.end()));
    }
}

void checkForceFallbacks()
{
    auto settings = rotationalSettings();
    BallBroadPhase broadPhase;
    for (const bool removeBase : {true, false})
    {
        auto balls = column();
        if (removeBase) balls.erase(balls.begin());
        else for (auto& ball : balls) ball.position.y += 1.0;
        const auto motion = calculateContactAccelerations(balls, settings, broadPhase, 0.01);
        for (std::size_t i = 0; i < balls.size(); ++i)
            require(motion.linear[i] == glm::dvec3{0.0, settings.acceleration, 0.0} &&
                    motion.angular[i] == glm::dvec3{0.0},
                    "An unsupported aligned column lost gravitational acceleration");
    }
    std::vector<Ball> tilted{{{0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1},
                              {{0.12, 0.26, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    const auto tiltedMotion = calculateContactAccelerations(tilted, settings, broadPhase, 0.01);
    bool nonzeroResidual = false;
    for (std::size_t i = 0; i < tilted.size(); ++i)
        nonzeroResidual = nonzeroResidual || glm::length(tiltedMotion.linear[i]) > 1e-6 ||
                          tilted[i].radius * glm::length(tiltedMotion.angular[i]) > 1e-6;
    require(nonzeroResidual, "A tilted unsupported pair incorrectly used exact static support");
    auto spinning = column();
    spinning.back().angularVelocity.x = 1.0;
    const auto spinningMotion = calculateContactAccelerations(spinning, settings, broadPhase, 0.01);
    require(glm::dot(spinning.back().angularVelocity, spinningMotion.angular.back()) < 0.0,
            "The exact support shortcut removed contact friction torque from a spinning ball");
    require(glm::length(spinningMotion.linear.back()) > 1e-6,
            "A spinning top ball did not transfer contact friction into translation");
}

void checkSupportedRestAndReference()
{
    auto settings = rotationalSettings();
    auto balls = column();
    const auto motion = settle(balls, settings);
    for (std::size_t i = 0; i < balls.size(); ++i)
    {
        require(balls[i].isResting, "Unequal-mass column did not sleep");
        require(motion.linear[i] == glm::dvec3{0.0} &&
                motion.angular[i] == glm::dvec3{0.0}, "Sleeping column can still accelerate");
    }
    auto reference = column();
    settings.useBroadPhase = false;
    const auto referenceMotion = settle(reference, settings);
    require(motion.horizon == referenceMotion.horizon, "Filtering changed the support horizon");
    for (std::size_t i = 0; i < balls.size(); ++i)
        require(balls[i].isResting == reference[i].isResting &&
                balls[i].velocity == reference[i].velocity &&
                balls[i].angularVelocity == reference[i].angularVelocity &&
                motion.linear[i] == referenceMotion.linear[i] &&
                motion.angular[i] == referenceMotion.angular[i],
                "Filtering changed the support or sleep result");
    balls = {{{0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    settle(balls, settings);
    require(balls[0].isResting, "An isolated floor contact did not sleep");
}

void checkIdealMotionAndDampedSettling()
{
    auto settings = rotationalSettings();
    std::vector<Ball> balls{{{0.0, 0.1, 0.0}, {1e-6, 0.0, 0.0}, 0.1, true}};
    balls[0].angularVelocity.z = -1e-5;
    settle(balls, settings);
    require(!balls[0].isResting && balls[0].velocity.x == 1e-6 &&
            balls[0].angularVelocity.z == -1e-5, "Ideal slow rolling was frozen");
    balls[0].velocity = glm::dvec3{0.0};
    balls[0].angularVelocity = {0.0, 1e-5, 0.0};
    settle(balls, settings);
    require(!balls[0].isResting && balls[0].angularVelocity.y == 1e-5,
            "Ideal slow axial spin was frozen");
    settings.rollingResistance = 0.01;
    balls[0].velocity.x = 1e-5;
    const auto motion = settle(balls, settings);
    require(balls[0].isResting && balls[0].velocity == glm::dvec3{0.0} &&
            balls[0].angularVelocity == glm::dvec3{0.0},
            "Damped quiet motion did not settle");
    require(motion.linear[0] == glm::dvec3{0.0} && motion.angular[0] == glm::dvec3{0.0},
            "A settled ball retained acceleration");
}

void checkWakeAndOverlap()
{
    auto settings = rotationalSettings();
    auto balls = column();
    settle(balls, settings);
    const auto original = balls;
    balls.erase(balls.begin());
    settle(balls, settings);
    for (const auto& ball : balls)
        require(!ball.isResting, "Removing support did not wake the column");
    balls = original;
    balls[3].angularVelocity.x = 0.1;
    settle(balls, settings);
    for (const auto& ball : balls)
        require(!ball.isResting, "An active contact group was partly frozen");
    balls = original;
    balls[1].position.y -= 1e-5;
    settle(balls, settings);
    require(!balls[0].isResting && !balls[1].isResting,
            "An overlapping support group was frozen");
    balls = {{{4.91, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1, true}};
    settle(balls, settings);
    require(!balls[0].isResting, "A wall overlap was frozen");
    balls = {{{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1, true}};
    settle(balls, settings);
    require(!balls[0].isResting, "An airborne ball retained its sleep state");
}

void checkUndampedComponents()
{
    auto settings = rotationalSettings();
    settings.rollingResistance = 0.01;
    settings.floorFriction = 0.0;
    std::vector<Ball> balls{{{0.0, 0.1, 0.0}, {1e-5, 0.0, 0.0}, 0.1, true}};
    auto motion = settle(balls, settings);
    require(!balls[0].isResting && balls[0].velocity.x == 1e-5,
            "Resistance torque froze frictionless translation");
    require(motion.linear[0] == glm::dvec3{0.0},
            "Frictionless motion gained translational resistance");
    balls[0].velocity = glm::dvec3{0.0};
    balls[0].angularVelocity = {0.0, 1e-5, 0.0};
    motion.angular[0] = glm::dvec3{0.0};
    BallBroadPhase broadPhase;
    settleRotatingBalls(balls, settings, motion, broadPhase);
    require(!balls[0].isResting && balls[0].angularVelocity.y == 1e-5,
            "Nonzero resistance setting froze spin without opposing torque");
}

void checkTorqueBalance()
{
    auto settings = rotationalSettings();
    BallBroadPhase broadPhase;
    std::vector<Ball> balls{{{0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1, true}};
    auto motion = calculateContactAccelerations(balls, settings, broadPhase, 0.01);
    require(motion.linear[0] == glm::dvec3{0.0}, "Floor support did not balance gravity");
    motion.angular[0].y = 1.0;
    settleRotatingBalls(balls, settings, motion, broadPhase);
    require(!balls[0].isResting, "Balanced forces hid an unbalanced torque");
    settings.floorFriction = 10.0;
    settings.ballFriction = 10.0;
    balls = {{{0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1},
             {{0.12, 0.26, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    const auto tiltedMotion = settle(balls, settings);
    require(!balls[0].isResting && !balls[1].isResting,
            "A tilted two-ball stack froze despite its unbalanced torques");
    require(glm::length(tiltedMotion.angular[0]) > 1e-5 ||
            glm::length(tiltedMotion.angular[1]) > 1e-5,
            "Tilted contacts did not expose a rotational imbalance");
}

void checkContactLimit()
{
    auto settings = rotationalSettings();
    BallBroadPhase broadPhase;
    std::vector<Ball> balls(364, {{0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1, true});
    ContactAccelerations motion;
    motion.linear.assign(balls.size(), glm::dvec3{0.0});
    motion.angular.assign(balls.size(), glm::dvec3{0.0});
    settleRotatingBalls(balls, settings, motion, broadPhase);
    for (const auto& ball : balls)
        require(!ball.isResting, "Contact overflow failed to skip sleeping safely");
}
} // namespace

int main()
{
    try
    {
        checkExactSupportForces();
        checkForceFallbacks();
        checkSupportedRestAndReference();
        checkIdealMotionAndDampedSettling();
        checkUndampedComponents();
        checkWakeAndOverlap();
        checkTorqueBalance();
        checkContactLimit();
        std::cout << "Ball rotational support tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
