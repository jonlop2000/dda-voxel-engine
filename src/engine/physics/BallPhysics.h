#pragma once

namespace engine::physics {

struct Ball
{
    double position; // center height in meters
    double velocity; // m/s
    double radius;   // m
};

// advances the ball by dt seconds under constant downward
// acceleration (m/s^2), allowing one perfectly elastic floor impact per step.
void advanceBall(Ball& ball, double acceleration, double deltaTime);

} 
