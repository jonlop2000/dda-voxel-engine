#include "BallContact.h"

#include <algorithm>
#include <cmath>

namespace engine::physics {
namespace {

glm::dvec3 frictionImpulse(const glm::dvec3& slip, double inverseEffectiveMass,
                           double normalImpulse, double friction)
{
    if (!(normalImpulse > 0.0) || !(friction > 0.0)) return glm::dvec3{0.0};
    const double speed = std::hypot(slip.x, slip.y, slip.z);
    if (!(speed > 0.0)) return glm::dvec3{0.0};
    const double magnitude = std::min(speed / inverseEffectiveMass,
                                      friction * normalImpulse);
    return -magnitude * (slip / speed);
}

} // namespace

double ballMomentOfInertia(const Ball& ball)
{
    return ball.radius > 0.0 ? 0.4 * ball.mass * ball.radius * ball.radius : 0.0;
}

double ballInverseInertia(const Ball& ball)
{
    const double inertia = ballMomentOfInertia(ball);
    return inertia > 0.0 ? 1.0 / inertia : 0.0;
}

double ballKineticEnergy(const Ball& ball)
{
    return 0.5 * ball.mass * glm::dot(ball.velocity, ball.velocity) +
           0.5 * ballMomentOfInertia(ball) *
               glm::dot(ball.angularVelocity, ball.angularVelocity);
}

glm::dvec3 contactPointVelocity(const Ball& ball, const glm::dvec3& offset)
{
    return ball.velocity + glm::cross(ball.angularVelocity, offset);
}

void applyBallImpulse(Ball& ball, const glm::dvec3& impulse,
                      const glm::dvec3& offset)
{
    const glm::dvec3 previousVelocity = ball.velocity;
    ball.velocity += impulse / ball.mass;
    ball.angularVelocity += glm::cross(offset, impulse) * ballInverseInertia(ball);
    if (ball.velocity != previousVelocity) ball.isResting = false;
}

glm::dvec3 applyBallPairFriction(Ball& a, Ball& b, const glm::dvec3& normal,
                                double normalImpulse, double friction)
{
    const glm::dvec3 offsetA = a.radius * normal;
    const glm::dvec3 offsetB = -b.radius * normal;
    const glm::dvec3 relativeVelocity = contactPointVelocity(b, offsetB) -
                                        contactPointVelocity(a, offsetA);
    const glm::dvec3 slip = relativeVelocity - glm::dot(relativeVelocity, normal) * normal;
    const double inverseEffectiveMass = 1.0 / a.mass + 1.0 / b.mass +
        a.radius * a.radius * ballInverseInertia(a) +
        b.radius * b.radius * ballInverseInertia(b);
    const glm::dvec3 impulse = frictionImpulse(slip, inverseEffectiveMass,
                                              normalImpulse, friction);
    applyBallImpulse(a, -impulse, offsetA);
    applyBallImpulse(b, impulse, offsetB);
    return impulse;
}

glm::dvec3 applyBallBoundaryFriction(Ball& ball, const glm::dvec3& inwardNormal,
                                    double normalImpulse, double friction)
{
    const glm::dvec3 offset = -ball.radius * inwardNormal;
    const glm::dvec3 velocity = contactPointVelocity(ball, offset);
    const glm::dvec3 slip = velocity - glm::dot(velocity, inwardNormal) * inwardNormal;
    const double inverseEffectiveMass = 1.0 / ball.mass +
        ball.radius * ball.radius * ballInverseInertia(ball);
    const glm::dvec3 impulse = frictionImpulse(slip, inverseEffectiveMass,
                                              normalImpulse, friction);
    applyBallImpulse(ball, impulse, offset);
    return impulse;
}

} // namespace engine::physics
