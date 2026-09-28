#include "engine/physics/BallSimulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace {
using namespace engine::physics;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void checkBounds(const std::vector<Ball>& balls)
{
    for (const auto& ball : balls)
    {
        require(std::isfinite(glm::length(ball.position)) && std::isfinite(glm::length(ball.velocity)),
                "Non-finite settling state");
        require(ball.position.y >= ball.radius - 1e-8 &&
                std::abs(ball.position.x) <= 5.0 - ball.radius + 1e-8 &&
                std::abs(ball.position.z) <= 5.0 - ball.radius + 1e-8, "Ball escaped a boundary");
    }
}
std::vector<Ball> stack(int count, bool dropped)
{
    std::vector<Ball> balls;
    for (int i = 0; i < count; ++i)
        balls.push_back({{0.0, dropped ? 1.2 + i * 0.5 : 0.125 + i * 0.25, 0.0},
                         {0.0, 0.0, 0.0}, 0.125});
    return balls;
}
void checkStaticStacks()
{
    std::array<std::size_t, 3> order{0, 1, 2};
    do
    {
        auto initial = stack(3, false);
        initial[0].mass = 4.0;
        initial[1].mass = 2.0;
        std::vector<Ball> balls;
        for (const auto i : order) balls.push_back(initial[i]);
        auto reference = balls;
        std::vector<BallCollisionEvent> events, referenceEvents;
        for (int step = 0; step < 200; ++step)
        {
            advanceBallSystem(balls, 0.01, step * 0.01, {}, events);
            advanceBallSystem(reference, 0.01, step * 0.01, {-9.81, 0.8, 0.2, false}, referenceEvents);
            require(events.empty() && referenceEvents.empty(), "Resting stack generated impacts");
            for (std::size_t i = 0; i < balls.size(); ++i)
            {
                require(balls[i].isResting && balls[i].velocity == glm::dvec3{0.0},
                        "Supported stack must remain at exact zero velocity");
                require(balls[i].position == initial[order[i]].position,
                        "Initially settled stack drifted");
                require(balls[i].position == reference[i].position &&
                        balls[i].velocity == reference[i].velocity &&
                        balls[i].isResting == reference[i].isResting,
                        "Filtering changed supported-stack results");
            }
        }
    } while (std::next_permutation(order.begin(), order.end()));
}
void checkFallingStack()
{
    auto balls = stack(4, true);
    std::vector<BallCollisionEvent> events;
    std::size_t impactCount = 0;
    for (int step = 0; step < 1000; ++step)
    {
        advanceBallSystem(balls, 0.01, step * 0.01, {}, events);
        impactCount += events.size();
        checkBounds(balls);
        if (step > 800) require(events.empty(), "Settled stack still emits tiny impacts");
    }
    require(impactCount > 0 && impactCount < 2000, "Falling stack repeated too many impacts");
    const auto settled = balls;
    for (std::size_t i = 0; i < balls.size(); ++i)
    {
        require(balls[i].isResting && balls[i].velocity == glm::dvec3{0.0}, "Falling stack did not settle");
        require(std::abs(balls[i].position.y - (0.125 + i * 0.25)) < 1e-6,
                "Stack retained excessive penetration or gaps");
    }
    for (int step = 0; step < 100; ++step)
        advanceBallSystem(balls, 0.01, 10.0 + step * 0.01, {}, events);
    for (std::size_t i = 0; i < balls.size(); ++i)
        require(glm::length(balls[i].position - settled[i].position) < 1e-9,
                "Settled stack kept drifting");
}
void checkWakeAndUnsupportedMotion()
{
    auto balls = stack(3, false);
    std::vector<BallCollisionEvent> events;
    advanceBallSystem(balls, 0.01, 0.0, {}, events);
    auto struck = balls;
    struck.push_back({{-1.0, 0.625, 0.0}, {20.0, 0.0, 0.0}, 0.125});
    advanceBallSystem(struck, 0.04, 0.01, {}, events);
    require(!events.empty() && !struck[2].isResting && glm::length(struck[2].velocity) > 0.1,
            "An incoming ball must wake the stack it strikes");
    auto pushed = balls;
    pushed[0].velocity.x = 1.0;
    advanceBallSystem(pushed, 0.01, 0.01, {}, events);
    require(!pushed[1].isResting && pushed[0].position.x > 0.0,
            "Pushing the base must wake its supported group");
    balls.erase(balls.begin());
    const double oldHeight = balls[0].position.y;
    advanceBallSystem(balls, 0.01, 0.01, {}, events);
    require(!balls[0].isResting && balls[0].velocity.y < 0.0 && balls[0].position.y < oldHeight,
            "Removing support must make the upper balls fall");
    balls = stack(3, false);
    for (auto& ball : balls) ball.position.y += 2.0;
    advanceBallSystem(balls, 0.01, 0.0, {}, events);
    for (const auto& ball : balls)
        require(!ball.isResting && ball.velocity.y < -0.09, "Airborne touching balls must fall");
    balls = {{{0.0, 0.25, 0.0}, {0.0, 0.0, 0.0}, 0.25},
             {{0.3, 0.65, 0.0}, {0.0, 0.0, 0.0}, 0.25}};
    advanceBallSystem(balls, 0.01, 0.0, {-9.81, 0.8, 0.0}, events);
    require(!balls[1].isResting && glm::length(balls[1].velocity) > 0.001,
            "An unbalanced contact must not be frozen");
    balls = {{{-0.1, 0.1, 0.0}, {0.001, 0.0, 0.0}, 0.1},
             {{0.1, 0.1, 0.0}, {0.001, 0.0, 0.0}, 0.1}};
    advanceBallSystem(balls, 0.1, 0.0, {-9.81, 0.8, 0.0}, events);
    for (const auto& ball : balls)
        require(!ball.isResting && ball.velocity.x == 0.001, "Frictionless sliding must not sleep");
}
void checkStaticFloorFriction()
{
    for (double friction : {0.0, 1.0})
    {
        const double height = 0.1 + std::sqrt(0.04 - 0.0225);
        std::vector<Ball> balls = {{{-0.09, 0.1, -0.12}, {0.0, 0.0, 0.0}, 0.1},
                                  {{0.09, 0.1, 0.12}, {0.0, 0.0, 0.0}, 0.1},
                                  {{0.0, height, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
        std::vector<BallCollisionEvent> events;
        advanceBallSystem(balls, 0.01, 0.0, {-9.81, 0.8, friction}, events);
        if (friction > 0.0)
            for (const auto& ball : balls)
                require(ball.isResting && ball.velocity == glm::dvec3{0.0},
                        "Sufficient floor friction must support a balanced pile");
        else
            require(!balls[2].isResting && glm::length(balls[2].velocity) > 0.001,
                    "The pile must spread without the required floor friction");
    }
}
void checkLowSpeedContact()
{
    for (const double speed : {0.02, 0.2})
    {
        std::vector<Ball> balls = {{{-0.1, 2.0, 0.0}, {speed, 0.0, 0.0}, 0.1},
                                  {{0.1, 2.0, 0.0}, {-speed, 0.0, 0.0}, 0.1}};
        std::vector<BallCollisionEvent> events;
        advanceBallSystem(balls, 0.01, 0.0, {0.0, 0.8, 0.0}, events);
        const double expected = speed < 0.05 ? 0.0 : -0.8 * speed;
        require(std::abs(balls[0].velocity.x - expected) < 1e-10 &&
                std::abs(balls[1].velocity.x + expected) < 1e-10,
                "Low-speed damping or normal restitution is incorrect");
        require(events.size() == (speed < 0.05 ? 0u : 1u),
                "Impact reporting must exclude small support impulses");
    }
}
void checkSupportedArch()
{
    const double top = 2.5 + std::sqrt(18.75);
    std::vector<Ball> balls = {{{-2.5, 2.5, 0.0}, {0.0, 0.0, 0.0}, 2.5},
                              {{2.5, 2.5, 0.0}, {0.0, 0.0, 0.0}, 2.5},
                              {{0.0, top, 0.0}, {0.0, 0.0, 0.0}, 2.5}};
    std::vector<BallCollisionEvent> events;
    for (int step = 0; step < 100; ++step)
        advanceBallSystem(balls, 0.01, step * 0.01, {-9.81, 0.8, 0.0}, events);
    for (const auto& ball : balls)
        require(ball.isResting && ball.velocity == glm::dvec3{0.0}, "Wall-supported arch failed to rest");
    require(std::abs(balls[2].position.y - top) < 1e-7, "Supported arch drifted");
}
void checkOriginalStressLayouts()
{
    for (bool pairs : {false, true})
    {
        std::vector<Ball> balls;
        for (int i = 0; i < 250; ++i)
        {
            const int cell = pairs ? i / 2 : i;
            const double x = -3.5 + cell % 8;
            const double z = -3.5 + (cell / 8) % 8;
            const double layer = cell / 64;
            if (pairs)
            {
                const double side = i % 2 == 0 ? -1.0 : 1.0;
                balls.push_back({{x + side * 0.35, 1.6 + layer * 0.8, z}, {-side * 0.75, 0.0, 0.0}, 0.1});
            }
            else
                balls.push_back({{x, 1.2 + layer * 0.5, z},
                    {i % 2 == 0 ? 0.25 : -0.25, 0.0, (i / 8) % 2 == 0 ? 0.2 : -0.2}, 0.1});
        }
        std::vector<BallCollisionEvent> events;
        for (int step = 0; step < 1000; ++step)
        {
            advanceBallSystem(balls, 0.01, step * 0.01, {}, events);
            checkBounds(balls);
            if (step > 800)
                require(events.empty(), "Settled stress layout still generates impacts");
        }
    }
}
} // namespace

int main()
{
    try
    {
        checkStaticStacks();
        checkFallingStack();
        checkWakeAndUnsupportedMotion();
        checkSupportedArch();
        checkLowSpeedContact();
        checkStaticFloorFriction();
        std::cout << "PASS stack equilibrium, settling, wake-up, and unsupported motion" << std::endl;
        checkOriginalStressLayouts();
        std::cout << "PASS both original 250-ball layouts through ten simulated seconds" << std::endl;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << std::endl;
        return 1;
    }
}
