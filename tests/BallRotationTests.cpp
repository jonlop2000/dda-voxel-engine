#include "engine/physics/BallSimulation.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include <glm/gtc/constants.hpp>

namespace {
using namespace engine::physics;
constexpr double pi = glm::pi<double>();
const std::array<glm::dvec3, 3> axes{{{1.0, 0.0, 0.0},
                                    {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void near(const glm::dvec3& actual, const glm::dvec3& expected, const char* message)
{
    require(glm::length(actual - expected) < 1e-11, message);
}

void sameRotation(const Ball& a, const Ball& b)
{
    // compare rotated directions because q and -q describe the same rotation.
    for (const auto& axis : axes)
        near(a.orientation * axis, b.orientation * axis, "Different orientations");
}

Ball makeBall()
{
    return {{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1};
}

void checkKnownTurns()
{
    for (std::size_t i = 0; i < axes.size(); ++i)
    {
        auto ball = makeBall();
        const auto perpendicular = axes[(i + 1) % axes.size()];
        ball.angularVelocity = pi * axes[i];
        advanceBallRotation(ball, 0.5);
        near(ball.orientation * perpendicular, glm::cross(axes[i], perpendicular),
             "Quarter turn has the wrong direction");
        advanceBallRotation(ball, 0.5);
        near(ball.orientation * perpendicular, -perpendicular, "Expected a half turn");
        advanceBallRotation(ball, 1.0);
        near(ball.orientation * perpendicular, perpendicular, "Expected a full turn");
        require(ball.angularVelocity == pi * axes[i], "Integration changed angular velocity");
        require(ball.position == makeBall().position && ball.velocity == glm::dvec3{0.0},
                "Rotation changed translation");
    }
    auto reverse = makeBall();
    reverse.angularVelocity = {0.0, -pi, 0.0};
    advanceBallRotation(reverse, 0.5);
    near(reverse.orientation * axes[0], axes[2], "Negative spin has the wrong sign");

    auto diagonal = makeBall();
    const glm::dvec3 axis{1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0};
    diagonal.angularVelocity = pi * axis;
    advanceBallRotation(diagonal, 1.0);
    near(diagonal.orientation * axes[0], 2.0 * axis.x * axis - axes[0],
         "Oblique half turn is incorrect");
    near(diagonal.orientation * axis, axis, "Rotation moved its own axis");
}

void checkWorldAxes()
{
    auto ball = makeBall();
    ball.orientation = glm::angleAxis(pi / 2.0, axes[0]);
    ball.angularVelocity = {0.0, pi / 2.0, 0.0};
    advanceBallRotation(ball, 1.0);
    near(ball.orientation * axes[1], axes[0], "Spin used local axes instead of world axes");
    near(ball.orientation * axes[0], -axes[2], "Incorrect composition order");
}

void checkNoOpsAndFiniteSteps()
{
    auto initial = makeBall();
    initial.orientation = glm::angleAxis(0.4, axes[2]);
    initial.angularVelocity = {0.0, pi, 0.0};
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const double duration : {0.0, -1.0, infinity, nan})
    {
        auto ball = initial;
        advanceBallRotation(ball, duration);
        require(ball.orientation == initial.orientation, "Invalid time changed orientation");
        advanceBall(ball, -9.81, duration, 0.8);
        require(ball.orientation == initial.orientation && ball.position == initial.position,
                "Single-ball update accepted invalid time");
    }
    for (const glm::dvec3 spin : {glm::dvec3{0.0}, {infinity, 0.0, 0.0},
                                {0.0, nan, 0.0}})
    {
        auto ball = initial;
        ball.angularVelocity = spin;
        advanceBallRotation(ball, 1.0);
        require(ball.orientation == initial.orientation, "Zero or invalid spin changed orientation");
    }
    auto overflow = initial;
    overflow.angularVelocity = {0.0, 1e308, 0.0};
    advanceBallRotation(overflow, 10.0);
    require(overflow.orientation == initial.orientation, "Overflowing angle changed orientation");

    for (const double speed : {1e-200, 1e200})
    {
        auto ball = makeBall();
        ball.angularVelocity = {0.0, speed, 0.0};
        advanceBallRotation(ball, 1.0 / speed);
        near(ball.orientation * axes[0], {std::cos(1.0), 0.0, -std::sin(1.0)},
             "Finite angular speed was lost to a squared-length overflow or underflow");
    }
}

void checkRepeatedSteps()
{
    auto whole = makeBall();
    whole.orientation = glm::angleAxis(0.7, axes[0]);
    whole.angularVelocity = {0.8, -1.1, 2.3};
    auto split = whole;
    advanceBallRotation(whole, 10.0);
    for (int step = 0; step < 10000; ++step)
    {
        advanceBallRotation(split, 0.001);
        require(std::abs(glm::length(split.orientation) - 1.0) < 1e-12,
                "Orientation lost unit length");
    }
    sameRotation(whole, split);
}

void checkSingleBallPaths()
{
    const std::array<Ball, 4> initial{{
        {{0.0, 2.0, 0.0}, {1.0, 0.0, 0.0}, 0.1},
        {{4.896, 0.12007848, 0.0}, {1.0, -5.0, 0.0}, 0.1},
        {{0.0, 0.1, 0.0}, {0.01, 0.0, 0.0}, 0.1},
        {{0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1, true}
    }};
    for (auto ball : initial)
    {
        auto reference = ball;
        ball.angularVelocity = {0.0, pi, 0.0};
        advanceBall(ball, -9.81, 0.01, 0.8, 0.2);
        advanceBall(reference, -9.81, 0.01, 0.8, 0.2);
        near(ball.orientation * axes[0], {std::cos(pi * 0.01), 0.0, -std::sin(pi * 0.01)},
             "Single-ball path skipped or repeated rotation");
        require(ball.position == reference.position && ball.velocity == reference.velocity &&
                ball.isResting == reference.isResting, "Spin changed single-ball translation");
        require(ball.angularVelocity == glm::dvec3{0.0, pi, 0.0}, "Contact changed free spin");
    }
}

void checkCollisionSegments()
{
    const std::vector<Ball> initial{{{-1.0, 2.0, 0.0}, {200.0, 0.0, 0.0}, 0.1},
                                   {{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1},
                                   {{1.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    for (const double duration : {0.004, 0.008, 0.01})
    {
        auto filtered = initial;
        for (auto& ball : filtered) ball.angularVelocity = {0.0, 50.0 * pi, 0.0};
        auto reference = filtered;
        auto unspun = initial;
        std::vector<BallCollisionEvent> events, referenceEvents, unspunEvents;
        advanceBallSystem(filtered, duration, 0.0, {0.0, 1.0, 0.0, true}, events);
        advanceBallSystem(reference, duration, 0.0, {0.0, 1.0, 0.0, false}, referenceEvents);
        advanceBallSystem(unspun, duration, 0.0, {0.0, 1.0, 0.0, true}, unspunEvents);
        const std::size_t expectedImpacts = duration < 0.008 ? 1 : 2;
        require(events.size() == expectedImpacts && referenceEvents.size() == expectedImpacts &&
                unspunEvents.size() == expectedImpacts, "Expected sequential impacts");
        for (std::size_t i = 0; i < filtered.size(); ++i)
        {
            const auto& ball = filtered[i];
            const double angle = 50.0 * pi * duration;
            near(ball.orientation * axes[0], {std::cos(angle), 0.0, -std::sin(angle)},
                 "Collision segments skipped or repeated angular time");
            require(ball.orientation == reference[i].orientation,
                    "Broad phase changed rotational integration");
            require(ball.position == unspun[i].position && ball.velocity == unspun[i].velocity,
                    "Free spin changed collision response");
            require(ball.angularVelocity == glm::dvec3{0.0, 50.0 * pi, 0.0},
                    "Collision changed angular velocity without torque");
        }
    }
}

void checkSupportedSpin()
{
    std::vector<Ball> balls;
    for (int level = 0; level < 3; ++level)
    {
        balls.push_back({{0.0, 0.125 + level * 0.25, 0.0}, {0.0, 0.0, 0.0}, 0.125});
        balls.back().angularVelocity = {0.0, pi, 0.0};
    }
    const auto initial = balls;
    std::vector<BallCollisionEvent> events;
    advanceBallSystem(balls, 1.0, 0.0, {}, events);
    require(events.empty(), "Supported spin created a collision");
    for (std::size_t i = 0; i < balls.size(); ++i)
    {
        require(balls[i].position == initial[i].position &&
                balls[i].velocity == glm::dvec3{0.0} && balls[i].isResting,
                "Spin disrupted translational support");
        near(balls[i].orientation * axes[0], -axes[0], "Supported rest froze rotation");
        require(balls[i].angularVelocity == initial[i].angularVelocity,
                "Settling removed angular velocity");
    }
}
} // namespace

int main()
{
    try
    {
        checkKnownTurns();
        checkWorldAxes();
        checkNoOpsAndFiniteSteps();
        checkRepeatedSteps();
        checkSingleBallPaths();
        checkCollisionSegments();
        checkSupportedSpin();
        std::cout << "Ball rotation tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
