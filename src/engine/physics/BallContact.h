#pragma once

#include "BallPhysics.h"

namespace engine::physics {

// use the inertia of a uniform solid sphere about its center.
double ballMomentOfInertia(const Ball& ball);
double ballInverseInertia(const Ball& ball);
double ballKineticEnergy(const Ball& ball);

// the contact offset is measured from the center in world coordinates.
glm::dvec3 contactPointVelocity(const Ball& ball, const glm::dvec3& offset);
void applyBallImpulse(Ball& ball, const glm::dvec3& impulse,
                      const glm::dvec3& offset);

// the normal points from a to b and the returned impulse acts on b.
glm::dvec3 applyBallPairFriction(Ball& a, Ball& b, const glm::dvec3& normal,
                                double normalImpulse, double friction);

// the normal points into allowed space and the impulse acts on the ball.
glm::dvec3 applyBallBoundaryFriction(Ball& ball, const glm::dvec3& inwardNormal,
                                    double normalImpulse, double friction);

} // namespace engine::physics
