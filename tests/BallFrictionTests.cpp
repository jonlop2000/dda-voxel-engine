#include "engine/physics/BallPhysics.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using engine::physics::Ball;
using engine::physics::advanceBall;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void requireVector(const char* name, const glm::dvec3& actual, const glm::dvec3& expected)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!(std::abs(actual[axis] - expected[axis]) < 1e-9))
        {
            std::cerr << std::setprecision(17) << name << " axis " << axis
                      << ": expected " << expected[axis] << ", got " << actual[axis] << '\n';
            throw std::runtime_error(std::string(name) + " differs from expected motion");
        }
    }
}

void requireState(const char* name, const Ball& ball, const glm::dvec3& position,
                  const glm::dvec3& velocity, bool resting = false)
{
    requireVector(name, ball.position, position);
    requireVector(name, ball.velocity, velocity);
    require(ball.isResting == resting, "Incorrect resting flag");
}

void slide(Ball& ball, double duration = 0.01, double restitution = 0.8)
{
    advanceBall(ball, -9.81, duration, restitution, 0.2);
}

void checkFreeSliding()
{
    Ball straight{{0.0, 0.1, 0.0}, {1.0, 0.0, 0.0}, 0.1};
    slide(straight);
    requireState("Straight slide", straight, {0.0099019, 0.1, 0.0}, {0.98038, 0.0, 0.0});

    Ball diagonal{{0.0, 0.1, 0.0}, {3.0, 0.0, 4.0}, 0.1};
    Ball heavy = diagonal;
    heavy.mass = 4.0;
    slide(diagonal);
    slide(heavy);
    requireState("Diagonal slide", diagonal, {0.02994114, 0.1, 0.03992152},
                 {2.988228, 0.0, 3.984304});
    requireState("Mass-independent deceleration", heavy, diagonal.position, diagonal.velocity);

    Ball negative{{0.0, 0.1, 0.0}, {0.0, 0.0, -0.5}, 0.1};
    slide(negative, 0.1);
    requireState("Negative z slide", negative, {0.0, 0.1, -0.04019}, {0.0, 0.0, -0.3038});

    Ball noFriction{{0.0, 0.1, 0.0}, {1.0, 0.0, 0.0}, 0.1};
    advanceBall(noFriction, -9.81, 0.01, 0.8);
    requireState("Default disables friction", noFriction, {0.01, 0.1, 0.0}, {1.0, 0.0, 0.0});
    const Ball unchanged = diagonal;
    slide(diagonal, 0.0);
    requireState("Zero duration", diagonal, unchanged.position, unchanged.velocity);
}

void checkStopping()
{
    Ball slow{{0.0, 0.1, 0.0}, {0.01, 0.0, 0.0}, 0.1};
    // initial kinetic energy equals friction force times stopping distance.
    const double stoppingDistance = 0.0001 / (2.0 * 1.962);
    slide(slow);
    requireState("Stop during a step", slow, {stoppingDistance, 0.1, 0.0}, {0.0, 0.0, 0.0}, true);
    for (int step = 0; step < 100; ++step) slide(slow);
    requireState("Stay at rest", slow, {stoppingDistance, 0.1, 0.0}, {0.0, 0.0, 0.0}, true);

    Ball stationary{{0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1};
    slide(stationary);
    requireState("Zero speed", stationary, {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, true);
    Ball longStep{{0.0, 0.1, 0.0}, {1.0, 0.0, 0.0}, 0.1};
    slide(longStep, 1.0);
    requireState("Long step stops", longStep, {1.0 / (2.0 * 1.962), 0.1, 0.0},
                 {0.0, 0.0, 0.0}, true);

    Ball beforeWall{{4.895, 0.1, 0.0}, {0.01, 0.0, 0.0}, 0.1};
    slide(beforeWall);
    requireState("Stop before wall", beforeWall, {4.895 + stoppingDistance, 0.1, 0.0},
                 {0.0, 0.0, 0.0}, true);
    Ball atWall{{4.4, 0.1, 0.0}, {1.0, 0.0, 0.0}, 0.1};
    advanceBall(atWall, -10.0, 2.0, 0.8, 0.1);
    requireState("Stop at wall", atWall, {4.9, 0.1, 0.0}, {0.0, 0.0, 0.0}, true);

    Ball incoming{{stoppingDistance - 0.19, 0.1, 0.0}, {1.0, 0.0, 0.0}, 0.1};
    require(engine::physics::resolveBallCollision(incoming, slow, 0.8), "Expected waking impact");
    require(!slow.isResting && slow.velocity.x > 0.0, "Impact did not wake stopped ball");
    const double before = slow.position.x;
    slide(slow);
    require(slow.position.x > before, "Woken ball did not resume sliding");
}

void checkLanding()
{
    // free flight and a substantial rebound must not experience floor friction.
    for (double height : {2.0, 0.105})
    {
        Ball airborne{{0.0, height, 0.0}, {1.0, -1.0, 0.5}, 0.1};
        Ball reference = airborne;
        slide(airborne);
        advanceBall(reference, -9.81, 0.01, 0.8, 0.0);
        requireState("Airborne motion", airborne, reference.position, reference.velocity);
    }

    // these starting heights put settling contact at exactly 0.002 or 0.006 seconds.
    Ball landing{{0.0, 0.10011962, 0.0}, {1.0, -0.05, 0.0}, 0.1};
    slide(landing);
    requireState("Partial-step support", landing, {0.009937216, 0.1, 0.0}, {0.984304, 0.0, 0.0});

    Ball wallFirst{{4.896, 0.10047658, 0.0}, {1.0, -0.05, 0.0}, 0.1};
    slide(wallFirst);
    requireState("Wall before landing", wallFirst, {4.895215696, 0.1, 0.0}, {-0.792152, 0.0, 0.0});
    Ball floorFirst{{4.894015696, 0.10011962, 0.0}, {1.0, -0.05, 0.0}, 0.1};
    slide(floorFirst);
    requireState("Wall after landing", floorFirst, {4.8968408096, 0.1, 0.0}, {-0.7858736, 0.0, 0.0});
}

void checkWalls()
{
    // each trajectory reaches its wall after 0.004 seconds, then slides back for 0.006.
    for (int axis : {0, 2})
    {
        for (double side : {-1.0, 1.0})
        {
            Ball ball{{0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1};
            ball.position[axis] = side * 4.896015696;
            ball.velocity[axis] = side;
            slide(ball);
            glm::dvec3 expectedPosition{0.0, 0.1, 0.0};
            glm::dvec3 expectedVelocity{0.0};
            expectedPosition[axis] = side * 4.8952729864;
            expectedVelocity[axis] = -side * 0.7819496;
            requireState("Sliding wall bounce", ball, expectedPosition, expectedVelocity);
        }
    }
    Ball outward{{4.9, 0.1, 0.0}, {1.0, 0.0, 0.0}, 0.1};
    slide(outward);
    requireState("Immediate wall contact", outward, {4.8920981, 0.1, 0.0}, {-0.78038, 0.0, 0.0});
    Ball almost{{4.89, 0.1, 0.0}, {1.0, 0.0, 0.0}, 0.1};
    slide(almost);
    requireState("Friction delays contact", almost, {4.8999019, 0.1, 0.0}, {0.98038, 0.0, 0.0});

    Ball corner{{4.8976094176, 0.1, 4.8968125568}, {0.6, 0.0, 0.8}, 0.1};
    slide(corner);
    requireState("Simultaneous corner", corner, {4.89716379184, 0.1, 4.89621838912},
                 {-0.46916976, 0.0, -0.62555968});
    Ball tangent{{4.8976094176, 0.1, 0.0}, {0.6, 0.0, 0.8}, 0.1};
    slide(tangent, 0.01, 0.0);
    requireState("Inelastic wall preserves tangent", tangent, {4.9, 0.1, 0.0079144568},
                 {0.0, 0.0, 0.7819496});

    Ball whole{{4.85, 0.1, 4.8}, {12.0, 0.0, 9.0}, 0.1};
    Ball subdivided = whole;
    slide(whole, 1.0);
    for (int step = 0; step < 100; ++step) slide(subdivided);
    requireState("Multiple wall contacts agree across time steps", whole,
                 subdivided.position, subdivided.velocity, subdivided.isResting);
    require(std::abs(whole.position.x) <= 4.9 && std::abs(whole.position.z) <= 4.9,
            "Sliding ball escaped the walls");
}

} // namespace

int main()
{
    try
    {
        checkFreeSliding();
        std::cout << "PASS straight, diagonal, signed, and mass-independent sliding\n";
        checkStopping();
        std::cout << "PASS exact stopping, persistent rest, and waking on impact\n";
        checkLanding();
        std::cout << "PASS airborne motion and split landing/wall contact times\n";
        checkWalls();
        std::cout << "PASS all walls, corners, zero restitution, and multiple contacts\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
