#include "engine/physics/BallPhysics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>

namespace engine::physics {

    Aabb calculateBallBounds(const Ball& ball)
    {
        // a sphere extends by its radius in both directions along every axis.
        const glm::dvec3 extent{ball.radius};
        return {ball.position - extent, ball.position + extent};
    }

    std::optional<Aabb> calculateSweptBallBounds(const Ball& ball,
                                                 const glm::dvec3& acceleration, double duration)
    {
        if (!std::isfinite(duration) || duration < 0.0 ||
            !std::isfinite(ball.radius) || ball.radius < 0.0) return std::nullopt;

        Aabb bounds;
        const double infinity = std::numeric_limits<double>::infinity();
        for (int axis = 0; axis < 3; ++axis)
        {
            const double position = ball.position[axis];
            const double velocity = ball.velocity[axis];
            const double axisAcceleration = acceleration[axis];
            if (!std::isfinite(position) || !std::isfinite(velocity) ||
                !std::isfinite(axisAcceleration)) return std::nullopt;

            const auto positionAt = [&](double time) {
                return std::fma(0.5 * axisAcceleration * time, time,
                                std::fma(velocity, time, position));
            };
            const double endPosition = positionAt(duration);
            if (!std::isfinite(endPosition)) return std::nullopt;
            double minimumCenter = std::min(position, endPosition);
            double maximumCenter = std::max(position, endPosition);

            // include interior turning points where v + a*t = 0.
            if (axisAcceleration != 0.0)
            {
                const double turningTime = -velocity / axisAcceleration;
                if (turningTime > 0.0 && turningTime < duration)
                {
                    const double turningPosition = positionAt(turningTime);
                    if (!std::isfinite(turningPosition)) return std::nullopt;
                    minimumCenter = std::min(minimumCenter, turningPosition);
                    maximumCenter = std::max(maximumCenter, turningPosition);
                }
            }

            // use absolute motion terms to retain rounding error estimates.
            const double scale = std::abs(position) + std::abs(velocity * duration) +
                                 std::abs(0.5 * axisAcceleration * duration * duration) + ball.radius;
            const double padding = 8.0 * std::numeric_limits<double>::epsilon() * scale;
            bounds.minimum[axis] = std::nextafter(minimumCenter - ball.radius - padding, -infinity);
            bounds.maximum[axis] = std::nextafter(maximumCenter + ball.radius + padding, infinity);
            if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]))
                return std::nullopt;
        }
        return bounds;
    }

    bool areBallsTouching(const Ball &ballA, const Ball &ballB) 
    {
        const glm::dvec3 offset = ballB.position - ballA.position;
        const double distanceSquared = glm::dot(offset, offset);
        const double combinedRadius = ballA.radius + ballB.radius;
        return distanceSquared <= combinedRadius * combinedRadius;
    }

    std::optional<double> findBallCollisionTime(const Ball& ballA, const Ball& ballB,
                                                double maxTime)
    {
        if (!std::isfinite(maxTime) || maxTime < 0.0 ||
            !std::isfinite(ballA.radius) || ballA.radius < 0.0 ||
            !std::isfinite(ballB.radius) || ballB.radius < 0.0)
        {
            return std::nullopt;
        }

        const glm::dvec3 relativePosition = ballB.position - ballA.position;
        const glm::dvec3 relativeVelocity = ballB.velocity - ballA.velocity;
        const double combinedRadius = ballA.radius + ballB.radius;
        const double quadraticA = glm::dot(relativeVelocity, relativeVelocity);
        const double quadraticB = 2.0 * glm::dot(relativePosition, relativeVelocity);
        const double quadraticC = glm::dot(relativePosition, relativePosition)
            - combinedRadius * combinedRadius;
        // reject invalid or overflowing quadratic coefficients.
        if (!std::isfinite(quadraticA) || !std::isfinite(quadraticB) || !std::isfinite(quadraticC))
        {
            return std::nullopt;
        }

        // existing contact returns zero regardless of approach or separation.
        if (quadraticC <= 0.0)
        {
            return 0.0;
        }
        // separated balls must approach each other to collide.
        if (quadraticA == 0.0 || quadraticB >= 0.0)
        {
            return std::nullopt;
        }

        // fused multiply-add reduces rounding in the discriminant.
        const double discriminant = std::fma(-4.0 * quadraticA, quadraticC,
                                             quadraticB * quadraticB);
        if (!std::isfinite(discriminant) || discriminant < 0.0)
        {
            return std::nullopt;
        }
        // rearrange the smaller root to avoid subtracting similar numbers.
        const double timeToContact = (2.0 * quadraticC) /
            (-quadraticB + std::sqrt(discriminant));
        if (!std::isfinite(timeToContact) || timeToContact < 0.0 || timeToContact > maxTime)
        {
            return std::nullopt;
        }
        return timeToContact;
    }

    bool resolveBallCollision(Ball &ballA, Ball &ballB, double restitution, double contactTolerance)
    {
        if (!std::isfinite(contactTolerance) || contactTolerance < 0.0) return false;
        const glm::dvec3 offset = ballB.position - ballA.position;
        const double distanceSquared = glm::dot(offset, offset);
        const double allowedDistance = ballA.radius + ballB.radius + contactTolerance;
        if (!(distanceSquared <= allowedDistance * allowedDistance))
        {
            return false;
        }

        const double inverseMassA = (1.0 /ballA.mass);
        const double inverseMassB = (1.0 /ballB.mass);
        const double inverseMassSum = inverseMassA + inverseMassB;

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
        const bool impact = velocityAlongNormal < 0.0;
        if (impact)
        {
            const double impulseMagnitude = (-(1 + restitution) * velocityAlongNormal) / inverseMassSum;
            const glm::dvec3 impulse = impulseMagnitude * normal;

            // the normal points from a to b, so their changes are opposite.
            ballA.velocity -= impulse * inverseMassA;
            ballB.velocity += impulse * inverseMassB;

            // an impact can start a resting ball moving
            ballA.isResting = false;
            ballB.isResting = false;
        }

        const double penetrationDepth = ballA.radius + ballB.radius - distance;
        if (penetrationDepth > 0.0) 
        {
            const double correctionMagnitude = penetrationDepth / inverseMassSum;
            const glm::dvec3 positionCorrection = correctionMagnitude * normal;

            // inverse mass makes the lighter ball move farther.
            ballA.position -= positionCorrection * inverseMassA;
            ballB.position += positionCorrection * inverseMassB;

            // recheck support after correcting the position.
            ballA.isResting = false;
            ballB.isResting = false;
        }
        return impact;
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

    bool isBallSupportedByFloor(const Ball& ball)
    {
        const double supportTolerance = 1e-9;
        return ball.velocity.y == 0.0 &&
            std::abs(ball.position.y - ball.radius) <= supportTolerance;
    }

    namespace {

    void advanceHorizontalMotion(Ball& ball, double duration, double deceleration,
                                 double restitution)
    {
        const double wallLimit = 5.0 - ball.radius;
        const double infinity = std::numeric_limits<double>::infinity();
        // overlap correction can push the ball outside a wall.
        ball.position.x = std::clamp(ball.position.x, -wallLimit, wallLimit);
        ball.position.z = std::clamp(ball.position.z, -wallLimit, wallLimit);
        double remainingTime = duration;

        while (remainingTime > 0.0)
        {
            const double speed = std::hypot(ball.velocity.x, ball.velocity.z);
            if (speed == 0.0) break;
            const glm::dvec3 direction{ball.velocity.x / speed, 0.0, ball.velocity.z / speed};
            const double stopTime = deceleration > 0.0 ? speed / deceleration : infinity;
            const double moveTime = std::min(remainingTime, stopTime);
            const double distance = (speed - 0.5 * deceleration * moveTime) * moveTime;

            // measure distance along the path.
            const double wallX = direction.x > 0.0 ? wallLimit : -wallLimit;
            const double wallZ = direction.z > 0.0 ? wallLimit : -wallLimit;
            const double distanceX = direction.x == 0.0 ? infinity :
                std::max(0.0, (wallX - ball.position.x) / direction.x);
            const double distanceZ = direction.z == 0.0 ? infinity :
                std::max(0.0, (wallZ - ball.position.z) / direction.z);
            const double wallDistance = std::min(distanceX, distanceZ);

            if (wallDistance > distance)
            {
                ball.position += direction * distance;
                // stop before friction can reverse motion.
                const double finalSpeed = stopTime <= remainingTime ? 0.0 :
                    std::max(0.0, speed - deceleration * moveTime);
                ball.velocity.x = direction.x * finalSpeed;
                ball.velocity.z = direction.z * finalSpeed;
                break;
            }

            const double contactSpeed = std::sqrt(std::max(0.0,
                speed * speed - 2.0 * deceleration * wallDistance));
            // this stable quadratic root also handles zero deceleration.
            const double timeToWall = 2.0 * wallDistance / (speed + contactSpeed);
            ball.position += direction * wallDistance;
            ball.velocity.x = direction.x * contactSpeed;
            ball.velocity.z = direction.z * contactSpeed;

            // reflect both components at corners within rounding tolerance.
            const double cornerTolerance = 1e-12 * std::max(1.0, wallDistance);
            if (distanceX - wallDistance <= cornerTolerance)
            {
                ball.position.x = wallX;
                ball.velocity.x *= -restitution;
            }
            if (distanceZ - wallDistance <= cornerTolerance)
            {
                ball.position.z = wallZ;
                ball.velocity.z *= -restitution;
            }
            remainingTime = std::max(0.0, remainingTime - timeToWall);
            // use the rebound velocity for the next segment.
        }
    }

    void advanceFloorSlide(Ball& ball, double duration, double deceleration,
                           double restitution)
    {
        // floor support cancels gravity while the ball slides horizontally.
        ball.position.y = ball.radius;
        ball.velocity.y = 0.0;
        advanceHorizontalMotion(ball, duration, deceleration, restitution);
        ball.isResting = ball.velocity.x == 0.0 && ball.velocity.z == 0.0;
    }

    } // namespace

    void advanceBall(Ball& ball, double acceleration, double deltaTime, double restitution,
                     double floorFriction)
    {
        if (ball.isResting || deltaTime <= 0.0) {
            return;
        }
        // dividing friction force by mass leaves coefficient times gravity.
        const double frictionDeceleration = floorFriction > 0.0 && acceleration < 0.0 ?
            floorFriction * -acceleration : 0.0;
        if (frictionDeceleration > 0.0 && isBallSupportedByFloor(ball))
        {
            advanceFloorSlide(ball, deltaTime, frictionDeceleration, restitution);
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
        const double frontWallZ = 5.0;
        // at z = 5, the allowed space is toward negative z.
        const Plane frontWall{{0.0, 0.0, frontWallZ}, {0.0, 0.0, -1.0}};
        const double frontWallContactZ = frontWallZ - ball.radius;
        const double backWallZ = -5.0;
        // at z = -5, the allowed space is toward positive z.
        const Plane backWall{{0.0, 0.0, backWallZ}, {0.0, 0.0, 1.0}};
        const double backWallContactZ = backWallZ + ball.radius;
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
            // negative displacement and velocity give positive contact time.
            double timeToWall = (leftWallContactX - ball.position.x) / ball.velocity.x;
            double remainingTime = deltaTime - timeToWall;

            // bounce x while preserving this step's y and z velocity.
            const glm::dvec3 wallBounceVelocity = calculateBounceVelocity(newVelocity, leftWall.normal, restitution);
            newVelocity = wallBounceVelocity;
            // move right from contact for the rest of the step.
            newPosition.x = leftWallContactX + wallBounceVelocity.x * remainingTime;
        }

        // handle z walls independently of x walls.
        const double frontWallDistance = signedDistanceToPlane(newPosition, frontWall);
        const double backWallDistance = signedDistanceToPlane(newPosition, backWall);
        if (frontWallDistance <= ball.radius && glm::dot(newVelocity, frontWall.normal) < 0.0) {
            const double timeToWall = (frontWallContactZ - ball.position.z) / ball.velocity.z;
            const double remainingTime = deltaTime - timeToWall;

            // reflect z while preserving x and y velocity.
            const glm::dvec3 wallBounceVelocity = calculateBounceVelocity(newVelocity, frontWall.normal, restitution);
            newVelocity = wallBounceVelocity;
            newPosition.z = frontWallContactZ + wallBounceVelocity.z * remainingTime;
        }
        else if (backWallDistance <= ball.radius && glm::dot(newVelocity, backWall.normal) < 0.0) {
            const double timeToWall = (backWallContactZ - ball.position.z) / ball.velocity.z;
            const double remainingTime = deltaTime - timeToWall;

            const glm::dvec3 wallBounceVelocity = calculateBounceVelocity(newVelocity, backWall.normal, restitution);
            newVelocity = wallBounceVelocity;
            newPosition.z = backWallContactZ + wallBounceVelocity.z * remainingTime;
        }

        // floor contact occurs when the center reaches one radius in height.
        const double floorDistance = signedDistanceToPlane(newPosition, floor);
        // a negative dot product means the ball is moving toward the floor.
        if (floorDistance <= ball.radius && glm::dot(newVelocity, floor.normal) < 0.0)
        {
            // split the step at its actual contact time.
            double timeToContact = (-ball.velocity.y - std::sqrt(ball.velocity.y * ball.velocity.y
                - 2 * acceleration * (ball.position.y - ball.radius))) / acceleration;
            double remainingTime = deltaTime - timeToContact;

            // compute vertical impact velocity while keeping wall responses.
            glm::dvec3 contactVelocity = newVelocity;
            contactVelocity.y = ball.velocity.y + acceleration * timeToContact;
            const glm::dvec3 bounceVelocity = calculateBounceVelocity(contactVelocity, floor.normal, restitution);
  
            if (bounceVelocity.y <= restSpeedThreshold) {
                if (frictionDeceleration > 0.0)
                {
                    // replay horizontal motion and walls up to landing.
                    const double airborneTime = std::clamp(timeToContact, 0.0, deltaTime);
                    advanceHorizontalMotion(ball, airborneTime, 0.0, restitution);
                    advanceFloorSlide(ball, deltaTime - airborneTime,
                                      frictionDeceleration, restitution);
                    return;
                }
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

            // advance only y to preserve horizontal wall responses.
            newPosition.y = ball.radius + bounceVelocity.y * remainingTime
                + 0.5 * acceleration * remainingTime * remainingTime;
            newVelocity.y = bounceVelocity.y + acceleration * remainingTime;
        }

        // update the caller's ball through its reference.
        ball.position = newPosition;
        ball.velocity = newVelocity;
    }

}
