#include "engine/physics/BallPhysics.h"

#include <cmath>
#include <glm/vec3.hpp>

namespace engine::physics {

    void advanceBall(Ball& ball, double acceleration, double deltaTime, double restitution)
    {
        if (ball.isResting) {
            return;
        }
        const double restSpeedThreshold = 0.1;
        const glm::dvec3 accelerationVector{0.0, acceleration, 0.0};
        // predict the end of the step using original position and velocity.
        glm::dvec3 newPosition = ball.position + ball.velocity * deltaTime
            + 0.5 * accelerationVector * deltaTime * deltaTime;
        glm::dvec3 newVelocity = ball.velocity + accelerationVector * deltaTime;

        if (newPosition.y <= ball.radius && newVelocity.y < 0)
        {
            // split the step at the actual contact time measured from its start.
            double timeToContact = (-ball.velocity.y - std::sqrt(ball.velocity.y * ball.velocity.y
                - 2 * acceleration * (ball.position.y - ball.radius))) / acceleration;
            double remainingTime = deltaTime - timeToContact;

            double contactVelocity = ball.velocity.y + acceleration * timeToContact;
            double bounceVelocity = -contactVelocity * restitution;

            if (bounceVelocity <= restSpeedThreshold) {
                // stop the tiny bounce while keeping horizontal movement.
                newPosition.y = ball.radius;
                newVelocity.y = 0.0;

                // save all three components before leaving the function.
                ball.position = newPosition;
                ball.velocity = newVelocity;

                // the ball is resting only when it has also stopped sliding.
                ball.isResting = (newVelocity.x == 0.0 && newVelocity.z == 0.0);
                return;
            }

            // continue from contact height for the remainder of the same step.
            newPosition.y = ball.radius + bounceVelocity * remainingTime
                + 0.5 * acceleration * remainingTime * remainingTime;
            newVelocity.y = bounceVelocity + acceleration * remainingTime;
        }

        // Ball& makes these changes update the object supplied by the caller.
        ball.position = newPosition;
        ball.velocity = newVelocity;
    }

}
