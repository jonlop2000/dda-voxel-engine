#include "engine/physics/BallPhysics.h"

#include <cmath>

namespace engine::physics {

    void advanceBall(Ball& ball, double acceleration, double deltaTime)
    {
        // predict the end of the step using original position and velocity.
        double newPosition = ball.position + ball.velocity * deltaTime
            + 0.5 * acceleration * deltaTime * deltaTime;
        double newVelocity = ball.velocity + acceleration * deltaTime;

        if (newPosition <= ball.radius && newVelocity < 0)
        {
            // split the step at the actual contact time measured from its start.
            double timeToContact = (-ball.velocity - std::sqrt(ball.velocity * ball.velocity
                - 2 * acceleration * (ball.position - ball.radius))) / acceleration;
            double remainingTime = deltaTime - timeToContact;

            double contactVelocity = ball.velocity + acceleration * timeToContact;
            double bounceVelocity = -contactVelocity;

            // continue from contact height for the remainder of the same step.
            newPosition = ball.radius + bounceVelocity * remainingTime
                + 0.5 * acceleration * remainingTime * remainingTime;
            newVelocity = bounceVelocity + acceleration * remainingTime;
        }

        // Ball& makes these changes update the object supplied by the caller.
        ball.position = newPosition;
        ball.velocity = newVelocity;
    }

}
