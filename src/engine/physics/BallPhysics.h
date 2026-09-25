#pragma once

#include <glm/vec3.hpp>

namespace engine::physics {

struct Ball
{
    glm::dvec3 position; // center position in meters
    glm::dvec3 velocity; // m/s
    double radius;   // m
    bool isResting = false;
    double mass = 1.0; // kg
};

struct Plane
{
    glm::dvec3 point; //holding a point on the surface
    glm::dvec3 normal; //holding the unit normal pointing to allowed space
};

// checking if two balls touch or overlap 
// lol
bool areBallsTouching(const Ball& ballA, const Ball& ballB);

// resolve contact and overlap; return true only when an impact applies a velocity impulse.
// separating or stationary contacts may need position correction without a new impact.
bool resolveBallCollision(Ball& ballA, Ball& ballB, double restitution);

// returns signed distance in meters; plane.normal must have length 1.
// positive is on the side the normal points toward.
double signedDistanceToPlane(const glm::dvec3& position, const Plane& plane);

glm::dvec3 calculateBounceVelocity(const glm::dvec3& velocity, const glm::dvec3& normal, double restitution);

// support requires floor contact and zero vertical velocity, excluding an active bounce.
// shared by floor friction and the motion readout so both use the same contact tolerance.
bool isBallSupportedByFloor(const Ball& ball);

// advances the ball by dt seconds under constant downward
// acceleration (m/s^2), with restitution for the floor and x/z sandbox walls.
// floor friction is a dimensionless sliding coefficient; zero disables it.
// friction acts only while supported by the floor, after vertical bouncing settles.
void advanceBall(Ball& ball, double acceleration, double deltaTime, double restitution,
                 double floorFriction = 0.0);

} 
