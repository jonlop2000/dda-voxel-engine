#include "engine/physics/BallPhysics.h"

#include <cmath>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>

namespace engine::physics {

    bool areBallsTouching(const Ball &ballA, const Ball &ballB) 
    {
        const glm::dvec3 offset = ballB.position - ballA.position;
        const double distanceSquared = glm::dot(offset, offset);
        const double combinedRadius = ballA.radius + ballB.radius;
        return distanceSquared <= combinedRadius * combinedRadius;
    }

    void resolveBallCollision(Ball &ballA, Ball &ballB, double restitution)
    {
        const bool touching = areBallsTouching(ballA, ballB);
        if (!touching)
        {
            return;
        }
        const glm::dvec3 offset = ballB.position - ballA.position;
        const double distanceSquared = glm::dot(offset,offset);
        const double distance = std::sqrt(distanceSquared);
        glm::dvec3 normal;
        if (distance > 0.0)
        {
            normal = offset / distance;
        }
        else
        {
            // coincident centers give no direction; choose a fixed unit normal.
            normal = glm::dvec3{1.0, 0.0, 0.0};
        }

        const glm::dvec3 relativeVelocity = ballB.velocity - ballA.velocity;
        const double velocityAlongNormal = glm::dot(relativeVelocity, normal);
        if (velocityAlongNormal < 0.0)
        {
            // equal masses share the required change in relative normal velocity.
            const double velocityChangeMagnitude =
                -(1.0 + restitution) * velocityAlongNormal / 2.0;
            const glm::dvec3 velocityChange = velocityChangeMagnitude * normal;

            // the normal points from A to B, so their changes are opposite.
            ballA.velocity -= velocityChange;
            ballB.velocity += velocityChange;

            // an impact can start a resting ball moving
            ballA.isResting = false;
            ballB.isResting = false;
        }

        const double penetrationDepth = ballA.radius + ballB.radius - distance;
        if (penetrationDepth > 0.0) 
        {
            const double correctionMagnitude = penetrationDepth / 2.0;
            const glm::dvec3 positionCorrection = correctionMagnitude * normal;

            // share the separation equally along the line between the centers.
            ballA.position -= positionCorrection;
            ballB.position += positionCorrection;

            // moving a ball can change its support; let physics update it again.
            ballA.isResting = false;
            ballB.isResting = false;
        }
    }
    
    double signedDistanceToPlane(const glm::dvec3& position, const Plane& plane)
    {
        // measure from a point on the plane to the supplied position.
        const glm::dvec3 offset = position - plane.point;
        // a unit normal gives the perpendicular distance and its sign.
        return glm::dot(offset, plane.normal);
    }

    glm::dvec3 calculateBounceVelocity(const glm::dvec3& velocity, const glm::dvec3& normal, double restitution) 
    {
        const double velocityAlongNormal = glm::dot(velocity, normal);
        if (velocityAlongNormal >= 0.0) {
            return velocity;
        }
        const glm::dvec3 normalPart = velocityAlongNormal * normal;
        return velocity - (1.0 + restitution) * normalPart;
    }

    void advanceBall(Ball& ball, double acceleration, double deltaTime, double restitution)
    {
        if (ball.isResting) {
            return;
        }
        const double restSpeedThreshold = 0.1;
        const glm::dvec3 accelerationVector{0.0, acceleration, 0.0};

        const double rightWallX = 5.0;
        // the wall is at x = 5 and its normal points left, into the sandbox.
        const Plane rightWall{{rightWallX, 0.0, 0.0}, {-1.0, 0.0, 0.0}};
        const double rightWallContactX = rightWallX - ball.radius;
        const double leftWallX = -5.0;
        // the wall is at x = -5 and its normal points right, into the sandbox.
        const Plane leftWall{{leftWallX, 0.0, 0.0}, {1.0, 0.0, 0.0}};
        const double leftWallContactX = leftWallX + ball.radius;
        // the floor is at y = 0 and its normal points upward, into the sandbox.
        const Plane floor{{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};

        // predict the end of the step using original position and velocity.
        glm::dvec3 newPosition = ball.position + ball.velocity * deltaTime
            + 0.5 * accelerationVector * deltaTime * deltaTime;
        glm::dvec3 newVelocity = ball.velocity + accelerationVector * deltaTime;

        // measure the predicted center's distance from each wall.
        const double rightWallDistance = signedDistanceToPlane(newPosition, rightWall);
        const double leftWallDistance = signedDistanceToPlane(newPosition, leftWall);
        // contact requires reaching the wall while moving toward it.
        if (rightWallDistance <= ball.radius && glm::dot(newVelocity, rightWall.normal) < 0.0) {
            double timeToWall = (rightWallContactX - ball.position.x) / ball.velocity.x;
            double remainingTime = deltaTime - timeToWall;

            glm::dvec3 wallBounceVelocity = calculateBounceVelocity(newVelocity, rightWall.normal, restitution);
            newVelocity = wallBounceVelocity;
            newPosition.x = rightWallContactX + wallBounceVelocity.x * remainingTime;
        }
        else if (leftWallDistance <= ball.radius && glm::dot(newVelocity, leftWall.normal) < 0.0) {
            // negative displacement divided by negative velocity gives positive time.
            double timeToWall = (leftWallContactX - ball.position.x) / ball.velocity.x;
            double remainingTime = deltaTime - timeToWall;

            // bounce x while preserving this step's y and z velocity.
            const glm::dvec3 wallBounceVelocity = calculateBounceVelocity(newVelocity, leftWall.normal, restitution);
            newVelocity = wallBounceVelocity;
            // move right from contact for the rest of the step.
            newPosition.x = leftWallContactX + wallBounceVelocity.x * remainingTime;
        }

        // contact requires the predicted center to reach one radius from the floor.
        const double floorDistance = signedDistanceToPlane(newPosition, floor);
        // a negative dot product means the ball is moving toward the floor.
        if (floorDistance <= ball.radius && glm::dot(newVelocity, floor.normal) < 0.0)
        {
            // split the step at the actual contact time measured from its start.
            double timeToContact = (-ball.velocity.y - std::sqrt(ball.velocity.y * ball.velocity.y
                - 2 * acceleration * (ball.position.y - ball.radius))) / acceleration;
            double remainingTime = deltaTime - timeToContact;

            // keep the wall response in x and calculate y velocity at floor contact.
            glm::dvec3 contactVelocity = newVelocity;
            contactVelocity.y = ball.velocity.y + acceleration * timeToContact;
            const glm::dvec3 bounceVelocity = calculateBounceVelocity(contactVelocity, floor.normal, restitution);
  
            if (bounceVelocity.y <= restSpeedThreshold) {
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
            // update only y so the horizontal motion and wall response stay intact.
            newPosition.y = ball.radius + bounceVelocity.y * remainingTime
                + 0.5 * acceleration * remainingTime * remainingTime;
            newVelocity.y = bounceVelocity.y + acceleration * remainingTime;
        }

        // Ball& makes these changes update the object supplied by the caller.
        ball.position = newPosition;
        ball.velocity = newVelocity;
    }

}
