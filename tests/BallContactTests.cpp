#include "engine/physics/BallContact.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace engine::physics;
constexpr glm::dvec3 floorNormal{0.0, 1.0, 0.0};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, const char* message)
{
    require(std::abs(actual - expected) < 1e-11, message);
}

void near(const glm::dvec3& actual, const glm::dvec3& expected, const char* message)
{
    require(glm::length(actual - expected) < 1e-11, message);
}

Ball makeBall(double radius = 0.5, double mass = 2.0)
{
    return {{0.0, radius, 0.0}, {0.0, 0.0, 0.0}, radius, false, mass};
}

glm::dvec3 angularMomentum(const Ball& ball)
{
    return glm::cross(ball.position, ball.mass * ball.velocity) +
           ballMomentOfInertia(ball) * ball.angularVelocity;
}

void checkInertiaAndEnergy()
{
    auto ball = makeBall();
    near(ballMomentOfInertia(ball), 0.2, "Wrong solid-sphere inertia");
    near(ballInverseInertia(ball), 5.0, "Wrong inverse inertia");
    ball.velocity = {3.0, 0.0, 0.0};
    ball.angularVelocity = {0.0, 0.0, 4.0};
    near(ballKineticEnergy(ball), 10.6, "Energy omitted translation or rotation");
    ball.mass *= 2.0;
    near(ballMomentOfInertia(ball), 0.4, "Inertia must scale with mass");
    ball.radius *= 2.0;
    near(ballMomentOfInertia(ball), 1.6, "Inertia must scale with radius squared");
}

void checkPointVelocityAndImpulse()
{
    auto ball = makeBall();
    ball.velocity = {2.0, 0.0, 0.0};
    ball.angularVelocity = {0.0, 0.0, -4.0};
    near(contactPointVelocity(ball, {0.0, -0.5, 0.0}), glm::dvec3{0.0},
         "A rolling contact should have zero slip");
    near(contactPointVelocity(ball, {0.0, 0.5, 0.0}), {4.0, 0.0, 0.0},
         "The top of a rolling ball should move twice as fast as its center");
    ball = makeBall();
    ball.isResting = true;
    applyBallImpulse(ball, {0.0, 2.0, 0.0}, {0.0, -0.5, 0.0});
    near(ball.velocity, {0.0, 1.0, 0.0}, "Impulse did not use inverse mass");
    near(ball.angularVelocity, glm::dvec3{0.0}, "A radial impulse produced torque");
    require(!ball.isResting, "An impulse did not wake the center");
    applyBallImpulse(ball, {-1.0, 0.0, 0.0}, {0.0, -0.5, 0.0});
    near(ball.angularVelocity, {0.0, 0.0, -2.5}, "Tangential impulse has wrong torque");
    ball.isResting = true;
    applyBallImpulse(ball, glm::dvec3{0.0}, {0.0, -0.5, 0.0});
    require(ball.isResting, "A zero impulse woke a resting center");
}

void checkBoundaryFriction()
{
    auto ball = makeBall();
    ball.velocity = {3.0, -2.0, 0.0};
    const double before = ballKineticEnergy(ball);
    const auto impulse = applyBallBoundaryFriction(ball, floorNormal, 100.0, 1.0);
    near(impulse, {-12.0 / 7.0, 0.0, 0.0}, "Incorrect cancellation impulse");
    near(ball.velocity, {15.0 / 7.0, -2.0, 0.0}, "Incorrect sliding-to-rolling speed");
    near(ball.angularVelocity, {0.0, 0.0, -30.0 / 7.0}, "Incorrect rolling spin");
    near(contactPointVelocity(ball, {0.0, -0.5, 0.0}), {0.0, -2.0, 0.0},
         "Friction did not remove slip or changed normal motion");
    require(ballKineticEnergy(ball) < before, "Floor friction increased energy");

    auto limited = makeBall();
    limited.velocity = {3.0, 0.0, 4.0};
    const auto limitedImpulse = applyBallBoundaryFriction(limited, floorNormal, 0.4, 0.2);
    near(limitedImpulse, {-0.048, 0.0, -0.064}, "Coulomb friction has wrong direction");
    near(glm::length(limitedImpulse), 0.08, "Friction exceeded its Coulomb budget");
    near(contactPointVelocity(limited, {0.0, -0.5, 0.0}), {2.916, 0.0, 3.888},
         "Limited friction changed slip incorrectly");

    auto spinning = makeBall();
    spinning.angularVelocity = {0.0, 0.0, 4.0};
    applyBallBoundaryFriction(spinning, floorNormal, 100.0, 1.0);
    require(spinning.velocity.x < 0.0 && spinning.angularVelocity.z > 0.0,
            "Spin did not transfer into translation with the correct sign");
    near(contactPointVelocity(spinning, {0.0, -0.5, 0.0}), glm::dvec3{0.0},
         "Spin-driven friction did not reach rolling");
}

void checkScalingAndNoOps()
{
    for (const double radius : {0.1, 0.5, 2.0})
    {
        for (const double mass : {0.1, 1.0, 10.0})
        {
            auto ball = makeBall(radius, mass);
            ball.velocity.x = 7.0;
            const auto impulse = applyBallBoundaryFriction(ball, floorNormal, 100.0, 1.0);
            near(impulse.x, -2.0 * mass, "Friction impulse did not scale with mass");
            near(ball.velocity.x, 5.0, "Rolling transition depends on mass or radius");
            near(ball.angularVelocity.z, -5.0 / radius, "Spin did not scale with radius");
        }
    }
    for (const auto budget : std::array<std::array<double, 2>, 4>{{
             {0.0, 1.0}, {1.0, 0.0}, {-1.0, 1.0}, {1.0, -1.0}}})
    {
        auto ball = makeBall();
        ball.velocity = {1.0, 2.0, 3.0};
        ball.angularVelocity = {4.0, 5.0, 6.0};
        const auto initial = ball;
        const auto impulse = applyBallBoundaryFriction(ball, floorNormal, budget[0], budget[1]);
        require(impulse == glm::dvec3{0.0} && ball.velocity == initial.velocity &&
                ball.angularVelocity == initial.angularVelocity, "Invalid budget changed motion");
        auto other = makeBall();
        const auto pairImpulse = applyBallPairFriction(ball, other, floorNormal,
                                                       budget[0], budget[1]);
        require(pairImpulse == glm::dvec3{0.0} && ball.velocity == initial.velocity &&
                ball.angularVelocity == initial.angularVelocity &&
                other.velocity == glm::dvec3{0.0} && other.angularVelocity == glm::dvec3{0.0},
                "Invalid pair budget changed motion");
    }
}

void checkPairConservation()
{
    const glm::dvec3 normal = glm::normalize(glm::dvec3{1.0, 2.0, 3.0});
    for (const double friction : {0.001, 0.2, 100.0})
    {
        auto a = makeBall(0.3, 2.0);
        auto b = makeBall(0.7, 5.0);
        a.position = {1.0, -2.0, 3.0};
        b.position = a.position + (a.radius + b.radius) * normal;
        a.velocity = {4.0, -3.0, 1.0};
        b.velocity = {-2.0, 5.0, 3.0};
        a.angularVelocity = {7.0, 3.0, -5.0};
        b.angularVelocity = {-3.0, 2.0, 8.0};
        const auto momentum = a.mass * a.velocity + b.mass * b.velocity;
        const auto angular = angularMomentum(a) + angularMomentum(b);
        const double energy = ballKineticEnergy(a) + ballKineticEnergy(b);
        const auto slipBefore = contactPointVelocity(b, -b.radius * normal) -
                                contactPointVelocity(a, a.radius * normal);
        const auto impulse = applyBallPairFriction(a, b, normal, 0.5, friction);
        const auto slipAfter = contactPointVelocity(b, -b.radius * normal) -
                               contactPointVelocity(a, a.radius * normal);
        near(a.mass * a.velocity + b.mass * b.velocity, momentum,
             "Pair friction failed to conserve linear momentum");
        near(angularMomentum(a) + angularMomentum(b), angular,
             "Pair friction failed to conserve total angular momentum");
        require(ballKineticEnergy(a) + ballKineticEnergy(b) <= energy + 1e-12,
                "Pair friction created kinetic energy");
        near(glm::dot(impulse, normal), 0.0, "Friction impulse contains a normal component");
        near(glm::dot(slipAfter, normal), glm::dot(slipBefore, normal),
             "Pair friction changed normal relative velocity");
        require(glm::length(impulse) <= friction * 0.5 + 1e-12,
                "Pair friction exceeded its Coulomb budget");
        if (friction == 100.0)
            near(slipAfter - glm::dot(slipAfter, normal) * normal, glm::dvec3{0.0},
                 "Sufficient pair friction did not remove tangential slip");
    }
    auto a = makeBall(0.5, 1.0);
    auto b = a;
    a.velocity = {0.0, 2.0, 0.0};
    const auto impulse = applyBallPairFriction(a, b, {1.0, 0.0, 0.0}, 10.0, 1.0);
    near(impulse, {0.0, 2.0 / 7.0, 0.0}, "Returned pair impulse must act on b");
    near(a.angularVelocity, {0.0, 0.0, -10.0 / 7.0}, "Ball a spins in the wrong direction");
    near(b.angularVelocity, a.angularVelocity, "Touching balls should gain matching spin");
}

void checkPointSpheres()
{
    auto ball = makeBall(0.0, 2.0);
    near(ballMomentOfInertia(ball), 0.0, "Point sphere has rotational inertia");
    near(ballInverseInertia(ball), 0.0, "Point sphere has invalid inverse inertia");
    ball.velocity = {3.0, -1.0, 0.0};
    ball.angularVelocity = {2.0, 3.0, 4.0};
    const auto spin = ball.angularVelocity;
    applyBallBoundaryFriction(ball, floorNormal, 100.0, 1.0);
    near(ball.velocity, {0.0, -1.0, 0.0}, "Point-sphere friction did not stop sliding");
    require(ball.angularVelocity == spin, "Point sphere gained torque");
    auto a = makeBall(0.0, 2.0);
    auto b = makeBall(0.0, 3.0);
    a.velocity = {0.0, 5.0, 0.0};
    applyBallPairFriction(a, b, {1.0, 0.0, 0.0}, 100.0, 1.0);
    near(a.velocity, {0.0, 2.0, 0.0}, "Point-pair friction has wrong effective mass");
    near(b.velocity, a.velocity, "Point pair did not cancel relative sliding");
    require(a.angularVelocity == glm::dvec3{0.0} && b.angularVelocity == glm::dvec3{0.0},
            "Point pair created angular velocity");
}
} // namespace

int main()
{
    try
    {
        checkInertiaAndEnergy();
        checkPointVelocityAndImpulse();
        checkBoundaryFriction();
        checkScalingAndNoOps();
        checkPairConservation();
        checkPointSpheres();
        std::cout << "Ball contact tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
