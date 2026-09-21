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

// advances the ball by dt seconds under constant downward
// acceleration (m/s^2), restitution accounts for elastic floor impact per step.
void advanceBall(Ball& ball, double acceleration, double deltaTime, double restitution);

} 
