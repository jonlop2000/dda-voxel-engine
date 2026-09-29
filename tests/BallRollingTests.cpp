#include "engine/physics/BallSimulation.h"
#include "engine/physics/BallContact.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/constants.hpp>

namespace {
using namespace engine::physics;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message, double tolerance = 1e-8)
{
    if (!(std::abs(actual - expected) <= tolerance))
    {
        std::cerr << message << ": got " << actual << ", expected " << expected << '\n';
        throw std::runtime_error(message);
    }
}
void near(const glm::dvec3& a, const glm::dvec3& b, const char* message)
{
    for (int axis = 0; axis < 3; ++axis) near(a[axis], b[axis], message);
}
BallSimulationSettings coupled(double friction = 0.2)
{
    BallSimulationSettings settings;
    settings.enableRotation = true;
    settings.floorFriction = friction;
    settings.ballFriction = friction;
    settings.wallFriction = friction;
    return settings;
}
std::vector<BallCollisionEvent> advance(std::vector<Ball>& balls, double time,
                                      const BallSimulationSettings& settings)
{
    std::vector<BallCollisionEvent> events;
    advanceBallSystem(balls, time, 0.0, settings, events);
    for (const auto& ball : balls)
    {
        require(std::isfinite(ballKineticEnergy(ball)), "Non-finite kinetic energy");
        near(glm::length(ball.orientation), 1.0, "Quaternion lost unit length", 1e-12);
        require(ball.position.y >= ball.radius - 1e-8 &&
                std::abs(ball.position.x) <= 5.0 - ball.radius + 1e-8 &&
                std::abs(ball.position.z) <= 5.0 - ball.radius + 1e-8,
                "Ball escaped a boundary");
    }
    return events;
}
glm::dvec3 floorSlip(const Ball& ball)
{
    auto slip = contactPointVelocity(ball, {0.0, -ball.radius, 0.0});
    slip.y = 0.0;
    return slip;
}
void checkAnalyticRolling()
{
    const auto settings = coupled();
    const double gravity = -settings.acceleration;
    const double slideTime = 2.0 / (7.0 * settings.floorFriction * gravity);
    for (const double radius : {0.1, 0.25})
        for (const double mass : {1.0, 4.0})
        {
            std::vector<Ball> balls{{{0.0, radius, 0.0}, {1.0, 0.0, 0.0}, radius, false, mass}};
            const double initialEnergy = ballKineticEnergy(balls[0]);
            advance(balls, slideTime, settings);
            near(balls[0].velocity.x, 5.0 / 7.0, "Sliding-to-rolling speed");
            near(balls[0].angularVelocity.z, -5.0 / (7.0 * radius), "Rolling angular speed");
            near(floorSlip(balls[0]), {0.0, 0.0, 0.0}, "Slip did not vanish");
            const double expectedPosition = slideTime -
                0.5 * settings.floorFriction * gravity * slideTime * slideTime;
            near(balls[0].position.x, expectedPosition, "Sliding distance");
            near(ballKineticEnergy(balls[0]), initialEnergy * 5.0 / 7.0, "Dissipated sliding energy");
            advance(balls, 1.0 - slideTime, settings);
            near(balls[0].velocity.x, 5.0 / 7.0, "Ideal rolling slowed down");
            require(!balls[0].isResting, "Rolling was put to sleep");
            near(balls[0].position.x, expectedPosition + (1.0 - slideTime) * 5.0 / 7.0,
                 "Rolling distance");
            const double angle = -5.0 / (7.0 * radius) * (1.0 - 0.5 * slideTime);
            near(balls[0].orientation * glm::dvec3{1.0, 0.0, 0.0},
                 {std::cos(angle), std::sin(angle), 0.0}, "Accelerating spin orientation");
        }
    for (const double initialSpin : {-10.0, 10.0})
    {
        std::vector<Ball> balls{{{0.0, 0.1, 0.0},
                                 {initialSpin < 0.0 ? 0.0 : 1.0, 0.0, 0.0}, 0.1}};
        balls[0].angularVelocity.z = initialSpin;
        advance(balls, 0.5, settings);
        near(balls[0].velocity.x, initialSpin < 0.0 ? 2.0 / 7.0 : 3.0 / 7.0,
             "Spin-to-translation or backspin result");
        near(floorSlip(balls[0]), {0.0, 0.0, 0.0}, "Backspin did not reach rolling");
    }
}
void checkFrictionlessAndResistance()
{
    auto settings = coupled(0.0);
    std::vector<Ball> balls{{{0.0, 0.1, 0.0}, {1.0, 0.0, 0.0}, 0.1}};
    balls[0].angularVelocity = {3.0, 4.0, 5.0};
    const double energy = ballKineticEnergy(balls[0]);
    advance(balls, 0.5, settings);
    near(balls[0].velocity.x, 1.0, "Frictionless translation changed");
    near(balls[0].angularVelocity, {3.0, 4.0, 5.0}, "Frictionless spin changed");
    near(ballKineticEnergy(balls[0]), energy, "Frictionless energy changed");

    settings = coupled();
    balls = {{{0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    balls[0].angularVelocity.y = 10.0;
    advance(balls, 1.0, settings);
    near(balls[0].angularVelocity.y, 10.0, "Point friction damped normal-axis twist");
    settings.rollingResistance = 0.02;
    advance(balls, 10.0, settings);
    require(balls[0].velocity == glm::dvec3{0.0} &&
            balls[0].angularVelocity == glm::dvec3{0.0} && balls[0].isResting,
            "Resistance did not settle twist");

    balls = {{{-2.0, 0.1, 0.0}, {1.0, 0.0, 0.0}, 0.1}};
    balls[0].angularVelocity.z = -10.0;
    advance(balls, 1.0, settings);
    const double deceleration = settings.rollingResistance * 9.81 / 1.4;
    near(balls[0].velocity.x, 1.0 - deceleration, "Rolling resistance deceleration");
    near(floorSlip(balls[0]), {0.0, 0.0, 0.0}, "Resistance broke no-slip rolling");
    advance(balls, 9.0, settings);
    require(balls[0].velocity == glm::dvec3{0.0} &&
            balls[0].angularVelocity == glm::dvec3{0.0} && balls[0].isResting,
            "Rolling resistance did not stop the ball");
}
void checkRoughImpacts()
{
    auto settings = coupled(0.5);
    settings.acceleration = 0.0;
    settings.restitution = 0.0;
    std::vector<Ball> floor{{{0.0, 0.11, 0.0}, {1.0, -1.0, 0.0}, 0.1}};
    advance(floor, 0.02, settings);
    near(floor[0].velocity, {5.0 / 7.0, 0.0, 0.0}, "Rough landing velocity");
    near(floor[0].angularVelocity.z, -50.0 / 7.0, "Rough landing spin");
    std::vector<Ball> wall{{{4.89, 2.0, 0.0}, {1.0, 0.0, 1.0}, 0.1}};
    advance(wall, 0.02, settings);
    near(wall[0].velocity, {0.0, 0.0, 5.0 / 7.0}, "Rough wall tangent velocity");
    near(wall[0].angularVelocity.y, 50.0 / 7.0, "Rough wall spin");

    std::vector<Ball> pair{{{-0.3, 2.0, 0.0}, {1.0, 0.0, 0.2}, 0.1},
                           {{0.3, 2.0, 0.0}, {-1.0, 0.0, -0.2}, 0.1, false, 2.0}};
    pair[0].angularVelocity.y = 4.0;
    pair[1].angularVelocity.y = -1.0;
    const auto initial = pair;
    const auto momentum = [](const std::vector<Ball>& state) {
        glm::dvec3 total{0.0};
        for (const auto& b : state) total += b.mass * b.velocity;
        return total;
    };
    const auto angularMomentum = [](const std::vector<Ball>& state) {
        glm::dvec3 total{0.0};
        for (const auto& b : state)
            total += glm::cross(b.position, b.mass * b.velocity) +
                     ballMomentOfInertia(b) * b.angularVelocity;
        return total;
    };
    settings.restitution = 0.8;
    const auto events = advance(pair, 0.4, settings);
    require(events.size() == 1 && events[0].includesRotation, "Expected a total-energy impact");
    near(momentum(pair), momentum(initial), "Pair linear momentum changed");
    near(angularMomentum(pair), angularMomentum(initial), "Pair angular momentum changed");
    require(events[0].kineticEnergyAfter <= events[0].kineticEnergyBefore + 1e-9,
            "Friction created total kinetic energy");
    require(pair[0].angularVelocity != initial[0].angularVelocity &&
            pair[1].angularVelocity != initial[1].angularVelocity, "Impact failed to transfer spin");
}
void checkSplitsAndFiltering()
{
    auto settings = coupled();
    std::vector<Ball> whole{{{-1.0, 0.25, 0.0}, {1.0, 0.0, 0.0}, 0.25}};
    auto split = whole;
    advance(whole, 1.0, settings);
    for (int step = 0; step < 100; ++step) advance(split, 0.01, settings);
    near(whole[0].position, split[0].position, "Split rolling position");
    near(whole[0].velocity, split[0].velocity, "Split rolling velocity");
    near(whole[0].angularVelocity, split[0].angularVelocity, "Split rolling spin");
    near(whole[0].orientation * glm::dvec3{1.0, 0.0, 0.0},
         split[0].orientation * glm::dvec3{1.0, 0.0, 0.0}, "Split orientation");
    std::vector<Ball> filtered{{{-0.4, 1.0, 0.0}, {1.0, 0.0, 0.0}, 0.2},
                              {{0.4, 1.0, 0.1}, {-1.0, 0.0, 0.0}, 0.2},
                              {{0.0, 0.2, 2.0}, {0.5, 0.0, 0.0}, 0.2}};
    filtered[0].angularVelocity.y = 6.0;
    auto reference = filtered;
    auto allPairs = settings;
    allPairs.useBroadPhase = false;
    for (int step = 0; step < 100; ++step)
    {
        const auto events = advance(filtered, 0.01, settings);
        const auto otherEvents = advance(reference, 0.01, allPairs);
        require(events.size() == otherEvents.size(), "Filtering changed impact count");
        for (std::size_t i = 0; i < filtered.size(); ++i)
        {
            const auto& a = filtered[i];
            const auto& b = reference[i];
            require(a.position == b.position && a.velocity == b.velocity &&
                    a.angularVelocity == b.angularVelocity && a.orientation == b.orientation &&
                    a.isResting == b.isResting, "Filtering changed coupled state");
        }
    }
}
} // namespace

int main()
{
    try
    {
        checkAnalyticRolling();
        checkFrictionlessAndResistance();
        checkRoughImpacts();
        checkSplitsAndFiltering();
        std::cout << "Coupled rolling, resistance, rough impacts, and filtering tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
