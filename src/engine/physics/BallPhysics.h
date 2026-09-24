#pragma once

#include <glm/vec3.hpp>

namespace engine::physics {

struct Ball
{
    glm::dvec3 position; // center position in meters
    glm::dvec3 velocity; // m/s
    double radius;   // m
    bool isResting = false;
};

struct Plane
{
    glm::dvec3 point; //holding a point on the surface
    glm::dvec3 normal; //holding the unit normal pointing to allowed space
};

// checking if two balls touch or overlap 
// lol
bool areBallsTouching(const Ball& ballA, const Ball& ballB);

// collision response, change the balls' velocities after contact
void resolveBallCollision(Ball& ballA, Ball& ballB, double restitution);

// returns signed distance in meters; plane.normal must have length 1.
// positive is on the side the normal points toward.
double signedDistanceToPlane(const glm::dvec3& position, const Plane& plane);

glm::dvec3 calculateBounceVelocity(const glm::dvec3& velocity, const glm::dvec3& normal, double restitution);

// advances the ball by dt seconds under constant downward
// acceleration (m/s^2), restitution accounts for elastic floor impact per step.
void advanceBall(Ball& ball, double acceleration, double deltaTime, double restitution);

} 
