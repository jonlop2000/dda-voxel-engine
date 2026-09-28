#pragma once

#include <optional>

#include <glm/vec3.hpp>

namespace engine::physics {

struct Ball
{
    glm::dvec3 position; // center position in meters
    glm::dvec3 velocity; // m/s
    double radius;   // m
    bool isResting = false; // stopped with validated support.
    double mass = 1.0; // kg
};

// an axis-aligned box stores the minimum and maximum on each axis.
struct Aabb
{
    glm::dvec3 minimum{0.0}; // lower x, y, and z bounds in meters
    glm::dvec3 maximum{0.0}; // upper x, y, and z bounds in meters
};

// get current bounds for a finite position and finite nonnegative radius.
Aabb calculateBallBounds(const Ball& ball);

// bound constant-acceleration motion, or return no value for invalid bounds.
std::optional<Aabb> calculateSweptBallBounds(const Ball& ball,
                                             const glm::dvec3& acceleration, double duration);

struct Plane
{
    glm::dvec3 point; // a point on the surface
    glm::dvec3 normal; // unit normal toward allowed space
};

// check whether two balls touch or overlap.
bool areBallsTouching(const Ball& ballA, const Ball& ballB);

// predict contact within the time limit using constant relative velocity.
std::optional<double> findBallCollisionTime(
    const Ball& ballA,
    const Ball& ballB,
    double maxTime);

// resolve contact and overlap, returning true only when an impulse is applied.
bool resolveBallCollision(Ball& ballA, Ball& ballB, double restitution,
                          double contactTolerance = 0.0); // gap in meters

// measure signed distance in meters along the plane's unit normal.
double signedDistanceToPlane(const glm::dvec3& position, const Plane& plane);

glm::dvec3 calculateBounceVelocity(const glm::dvec3& velocity, const glm::dvec3& normal, double restitution);

// floor support requires contact and zero vertical velocity.
bool isBallSupportedByFloor(const Ball& ball);

// advance through gravity, boundary impacts, and supported floor friction.
void advanceBall(Ball& ball, double acceleration, double deltaTime, double restitution,
                 double floorFriction = 0.0); // sliding coefficient

} 
