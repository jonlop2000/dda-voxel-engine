#include "engine/physics/BallPhysics.h"

#include <cmath>
#include <iomanip>
#include <iostream>

namespace
{

bool vectorsMatch(const glm::dvec3& actual, const glm::dvec3& expected)
{
    // allow tiny rounding differences, and check every coordinate.
    const double tolerance = 1e-9;
    return std::abs(actual.x - expected.x) < tolerance
        && std::abs(actual.y - expected.y) < tolerance
        && std::abs(actual.z - expected.z) < tolerance;
}

bool expectSignedDistance(const char* testName, const glm::dvec3& position,
                          const engine::physics::Plane& plane, double expectedDistance)
{
    const double actual = engine::physics::signedDistanceToPlane(position, plane);
    const double tolerance = 1e-9;
    if (std::abs(actual - expectedDistance) < tolerance)
    {
        std::cout << "Plane distance " << testName << " test passed\n";
        return true;
    }

    std::cerr << std::setprecision(17) << "Plane distance " << testName << " failed"
              << "\nDistance (m): expected " << expectedDistance << ", got " << actual << '\n';
    return false;
}

bool expectBounceVelocity(const char* testName, const glm::dvec3& velocity,
                          const glm::dvec3& normal, double restitution,
                          const glm::dvec3& expectedVelocity)
{
    const glm::dvec3 actual = engine::physics::calculateBounceVelocity(velocity, normal, restitution);
    if (vectorsMatch(actual, expectedVelocity))
    {
        std::cout << "Bounce velocity " << testName << " test passed\n";
        return true;
    }

    // show which components differ from the expected bounce.
    std::cerr << std::setprecision(17) << "Bounce velocity " << testName << " failed"
              << "\nVelocity (m/s): expected ("
              << expectedVelocity.x << ", " << expectedVelocity.y << ", " << expectedVelocity.z
              << "), got (" << actual.x << ", " << actual.y << ", " << actual.z << ")\n";
    return false;
}

bool expectBallState(const char* testName, const engine::physics::Ball& ball,
                     const glm::dvec3& expectedPosition,
                     const glm::dvec3& expectedVelocity, bool expectedResting,
                     int update = 0)
{
    if (vectorsMatch(ball.position, expectedPosition)
        && vectorsMatch(ball.velocity, expectedVelocity)
        && ball.isResting == expectedResting)
    {
        return true;
    }

    // show the complete state so a failed coordinate is easy to find.
    std::cerr << std::setprecision(17) << testName << " failed";
    if (update > 0)
    {
        std::cerr << " on update " << update;
    }
    std::cerr << "\nPosition (m): expected ("
              << expectedPosition.x << ", " << expectedPosition.y << ", " << expectedPosition.z
              << "), got (" << ball.position.x << ", " << ball.position.y << ", " << ball.position.z
              << ")\nVelocity (m/s): expected ("
              << expectedVelocity.x << ", " << expectedVelocity.y << ", " << expectedVelocity.z
              << "), got (" << ball.velocity.x << ", " << ball.velocity.y << ", " << ball.velocity.z
              << ")\nResting: expected " << std::boolalpha << expectedResting
              << ", got " << ball.isResting << '\n';
    return false;
}

} // namespace

int main()
{
    const double gravityAcceleration = -9.81;
    const double physicsStep = 0.01;
    const double restitution = 0.8;

    // Report an impact once, while keeping the existing impulse and overlap correction.
    engine::physics::Ball impactA{{0.0, 2.0, 0.0}, {1.0, 0.0, 0.0}, 0.1};
    engine::physics::Ball impactB{{0.19, 2.0, 0.0}, {-1.0, 0.0, 0.0}, 0.1};
    if (!engine::physics::resolveBallCollision(impactA, impactB, restitution) ||
        !expectBallState("Reported impact A", impactA, {-0.005, 2.0, 0.0}, {-0.8, 0.0, 0.0}, false) ||
        !expectBallState("Reported impact B", impactB, {0.195, 2.0, 0.0}, {0.8, 0.0, 0.0}, false) ||
        engine::physics::resolveBallCollision(impactA, impactB, restitution))
    {
        std::cerr << "An impact must report once and preserve the collision response\n";
        return 1;
    }
    // Correcting an overlap without an impulse must not appear as a new hit.
    engine::physics::Ball stationaryA{{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1};
    engine::physics::Ball stationaryB{{0.15, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1};
    if (engine::physics::resolveBallCollision(stationaryA, stationaryB, restitution) ||
        !expectBallState("Stationary overlap A", stationaryA, {-0.025, 2.0, 0.0}, {0.0, 0.0, 0.0}, false) ||
        !expectBallState("Stationary overlap B", stationaryB, {0.175, 2.0, 0.0}, {0.0, 0.0, 0.0}, false))
    {
        std::cerr << "Stationary overlap correction must not report an impact\n";
        return 1;
    }
    engine::physics::Ball separatingA{{0.0, 2.0, 0.0}, {-1.0, 0.0, 0.0}, 0.1};
    engine::physics::Ball separatingB{{0.19, 2.0, 0.0}, {1.0, 0.0, 0.0}, 0.1};
    if (engine::physics::resolveBallCollision(separatingA, separatingB, restitution) ||
        !expectBallState("Separating overlap A", separatingA, {-0.005, 2.0, 0.0}, {-1.0, 0.0, 0.0}, false) ||
        !expectBallState("Separating overlap B", separatingB, {0.195, 2.0, 0.0}, {1.0, 0.0, 0.0}, false))
    {
        std::cerr << "Separating overlap correction must not report an impact\n";
        return 1;
    }
    engine::physics::Ball distantA{{0.0, 2.0, 0.0}, {1.0, 0.0, 0.0}, 0.1};
    engine::physics::Ball distantB{{1.0, 2.0, 0.0}, {-1.0, 0.0, 0.0}, 0.1};
    if (engine::physics::resolveBallCollision(distantA, distantB, restitution))
    {
        std::cerr << "Separated balls must not report an impact\n";
        return 1;
    }
    std::cout << "Ball collision reporting tests passed (impacts, repeated contact, overlap, separation)\n";

    // A 0.01 m overlap splits 80/20 for 1 kg versus 4 kg, in either argument order.
    // Equal 2 kg masses still split it evenly, independent of the absolute mass.
    struct SeparationCase { double massA, massB, expectedAx, expectedBx; };
    const SeparationCase separationCases[] = {
        {1.0, 4.0, -0.008, 0.192},
        {4.0, 1.0, -0.002, 0.198},
        {2.0, 2.0, -0.005, 0.195}
    };
    for (const auto& example : separationCases)
    {
        engine::physics::Ball a{{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1, true, example.massA};
        engine::physics::Ball b{{0.19, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1, true, example.massB};
        const glm::dvec3 weightedPositionBefore = a.mass * a.position + b.mass * b.position;
        if (engine::physics::resolveBallCollision(a, b, restitution) ||
            !expectBallState("Weighted separation A", a, {example.expectedAx, 2.0, 0.0},
                             {0.0, 0.0, 0.0}, false) ||
            !expectBallState("Weighted separation B", b, {example.expectedBx, 2.0, 0.0},
                             {0.0, 0.0, 0.0}, false) ||
            !vectorsMatch(a.mass * a.position + b.mass * b.position, weightedPositionBefore))
        {
            std::cerr << "Mass-weighted correction must remove overlap and preserve center of mass\n";
            return 1;
        }
    }
    // The correction also follows an angled normal in 3D, not just the x axis.
    engine::physics::Ball angledA{{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1, false, 1.0};
    engine::physics::Ball angledB{{0.114, 2.0, 0.152}, {0.0, 0.0, 0.0}, 0.1, false, 4.0};
    if (engine::physics::resolveBallCollision(angledA, angledB, restitution) ||
        !expectBallState("Angled weighted separation A", angledA, {-0.0048, 2.0, -0.0064},
                         {0.0, 0.0, 0.0}, false) ||
        !expectBallState("Angled weighted separation B", angledB, {0.1152, 2.0, 0.1536},
                         {0.0, 0.0, 0.0}, false))
    {
        std::cerr << "Mass-weighted correction must follow the collision normal\n";
        return 1;
    }
    // An approaching pair receives both the velocity impulse and weighted separation.
    engine::physics::Ball movingLight{{0.0, 2.0, 0.0}, {1.0, 0.0, 0.0}, 0.1, false, 1.0};
    engine::physics::Ball stillHeavy{{0.19, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1, false, 4.0};
    if (!engine::physics::resolveBallCollision(movingLight, stillHeavy, restitution) ||
        !expectBallState("Light ball impact", movingLight, {-0.008, 2.0, 0.0}, {-0.44, 0.0, 0.0}, false) ||
        !expectBallState("Heavy ball impact", stillHeavy, {0.192, 2.0, 0.0}, {0.36, 0.0, 0.0}, false))
    {
        std::cerr << "An unequal-mass impact must apply the impulse and remove overlap\n";
        return 1;
    }
    std::cout << "Ball mass-weighted separation tests passed (mass ratios, center of mass, angled contact, impact)\n";

    const engine::physics::Plane rightWall{{5.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}};
    // the center is 0.3 m from the wall, regardless of its y and z coordinates.
    if (!expectSignedDistance("allowed side", {4.7, 2.0, -3.0}, rightWall, 0.3))
    {
        return 1;
    }
    if (!expectSignedDistance("on the plane", {5.0, 2.0, -3.0}, rightWall, 0.0))
    {
        return 1;
    }
    // crossing the plane must change the sign of the distance.
    if (!expectSignedDistance("opposite side", {5.2, 2.0, -3.0}, rightWall, -0.2))
    {
        return 1;
    }
    // this floor is raised to y = 1.5; the position is 0.5 m above it.
    const engine::physics::Plane raisedFloor{{0.0, 1.5, 0.0}, {0.0, 1.0, 0.0}};
    if (!expectSignedDistance("raised floor", {2.0, 2.0, -3.0}, raisedFloor, 0.5))
    {
        return 1;
    }
    // the angled plane is 0.6*y + 0.8*z = 3.6; x runs parallel to it.
    // this position gives 4.6 on the left, so its signed distance is 1 m.
    const engine::physics::Plane angledPlane{{1.0, 2.0, 3.0}, {0.0, 0.6, 0.8}};
    if (!expectSignedDistance("angled plane", {8.0, 1.0, 5.0}, angledPlane, 1.0))
    {
        return 1;
    }

    // each surface reverses its incoming normal motion and preserves tangential motion.
    if (!expectBounceVelocity("right wall", {1.0, 2.0, 0.0}, {-1.0, 0.0, 0.0}, restitution,
                              {-0.8, 2.0, 0.0}))
    {
        return 1;
    }
    if (!expectBounceVelocity("left wall", {-1.0, 2.0, -0.5}, {1.0, 0.0, 0.0}, restitution,
                              {0.8, 2.0, -0.5}))
    {
        return 1;
    }
    if (!expectBounceVelocity("floor", {2.0, -5.0, -3.0}, {0.0, 1.0, 0.0}, restitution,
                              {2.0, 4.0, -3.0}))
    {
        return 1;
    }

    // (0.6, 0.8, 0) is a unit normal; an angled bounce changes both x and y.
    if (!expectBounceVelocity("angled surface", {0.0, -5.0, 2.0}, {0.6, 0.8, 0.0}, restitution,
                              {4.32, 0.76, 2.0}))
    {
        return 1;
    }
    // zero restitution removes normal motion, but the ball can still slide.
    if (!expectBounceVelocity("zero restitution", {0.0, -5.0, 2.0}, {0.6, 0.8, 0.0}, 0.0,
                              {2.4, -1.8, 2.0}))
    {
        return 1;
    }
    // full restitution reverses normal motion without reducing its speed.
    if (!expectBounceVelocity("full restitution", {0.0, -5.0, 2.0}, {0.6, 0.8, 0.0}, 1.0,
                              {4.8, 1.4, 2.0}))
    {
        return 1;
    }

    // moving away from or parallel to the surface should leave velocity unchanged.
    if (!expectBounceVelocity("moving away", {1.0, 2.0, -3.0}, {0.0, 1.0, 0.0}, restitution,
                              {1.0, 2.0, -3.0}))
    {
        return 1;
    }
    if (!expectBounceVelocity("parallel motion", {1.0, 0.0, -3.0}, {0.0, 1.0, 0.0}, restitution,
                              {1.0, 0.0, -3.0}))
    {
        return 1;
    }

    engine::physics::Ball ball{{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1};
    // simulate 0.5 seconds without needing a renderer.
    for (int step = 0; step < 50; ++step)
    {
        engine::physics::advanceBall(ball, gravityAcceleration, physicsStep, restitution);
    }

    // y = 2 + 0.5 * (-9.81) * 0.5 * 0.5; vertical velocity = -9.81 * 0.5.
    // the ball has not reached the floor, and x and z should stay unchanged.
    if (!expectBallState("Free fall", ball,
                         {0.0, 0.77375, 0.0}, {0.0, -4.905, 0.0}, false))
    {
        return 1;
    }
    std::cout << "Ball physics free-fall test passed (0.5 seconds)\n";

    // contact happens immediately; the rebound starts at 4 m/s upward.
    engine::physics::Ball impactBall{{0.0, 0.1, 0.0}, {0.0, -5.0, 0.0}, 0.1};
    engine::physics::advanceBall(impactBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Immediate impact", impactBall,
                         {0.0, 0.1395095, 0.0}, {0.0, 3.9019, 0.0}, false))
    {
        return 1;
    }
    std::cout << "Ball physics impact test passed (restitution 0.8)\n";

    // the rebound would be 0.04 m/s, below the 0.1 m/s settling threshold.
    engine::physics::Ball settlingBall{{0.0, 0.1, 0.0}, {0.0, -0.05, 0.0}, 0.1};
    // first update settles the ball; the next 100 must keep it resting.
    for (int step = 1; step <= 101; ++step)
    {
        engine::physics::advanceBall(settlingBall, gravityAcceleration, physicsStep, restitution);
        if (!expectBallState("Stationary settling", settlingBall,
                             {0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, true, step))
        {
            return 1;
        }
    }
    std::cout << "Ball physics settling test passed (settled, then 100 resting updates)\n";

    // contact at 0.004 seconds: 0.12007848 - 5 * 0.004 - 4.905 * 0.004 * 0.004 = 0.1.
    engine::physics::Ball midStepBall{{0.0, 0.12007848, 0.0}, {0.0, -5.0, 0.0}, 0.1};
    engine::physics::advanceBall(midStepBall, gravityAcceleration, physicsStep, restitution);
    // contact velocity is -5.03924; rebound velocity is 4.031392; 0.006 seconds remain.
    if (!expectBallState("Impact during step", midStepBall,
                         {0.0, 0.124011772, 0.0}, {0.0, 3.972532, 0.0}, false))
    {
        return 1;
    }
    std::cout << "Ball physics impact during step test passed (contact at 0.004 seconds)\n";

    // gravity changes y only: after 0.5 seconds, x moves 0.5 m and z moves -0.25 m.
    engine::physics::Ball movingBall{{0.0, 2.0, 0.0}, {1.0, 0.0, -0.5}, 0.1};
    for (int step = 0; step < 50; ++step)
    {
        engine::physics::advanceBall(movingBall, gravityAcceleration, physicsStep, restitution);
    }
    if (!expectBallState("Horizontal free fall", movingBall,
                         {0.5, 0.77375, -0.25}, {1.0, -4.905, -0.5}, false))
    {
        return 1;
    }
    std::cout << "Ball physics horizontal free-fall test passed (x and z motion)\n";

    // a floor bounce changes y only; x and z must advance for the full 0.01 seconds.
    engine::physics::Ball movingImpactBall{{1.0, 0.12007848, 2.0}, {1.0, -5.0, -0.5}, 0.1};
    engine::physics::advanceBall(movingImpactBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Horizontal motion through impact", movingImpactBall,
                         {1.01, 0.124011772, 1.995}, {1.0, 3.972532, -0.5}, false))
    {
        return 1;
    }
    std::cout << "Ball physics horizontal impact test passed (full step preserved)\n";

    // contact occurs at 0.002 seconds; the small rebound settles immediately.
    // horizontal movement must include the entire first step, then keep going.
    engine::physics::Ball slidingXBall{{1.0, 0.10011962, 2.0}, {1.0, -0.05, 0.0}, 0.1};
    for (int step = 1; step <= 101; ++step)
    {
        engine::physics::advanceBall(slidingXBall, gravityAcceleration, physicsStep, restitution);
        if (!expectBallState("Sliding along x after settling", slidingXBall,
                             {1.0 + step * 0.01, 0.1, 2.0}, {1.0, 0.0, 0.0}, false, step))
        {
            return 1;
        }
    }
    std::cout << "Ball physics x sliding test passed (settled, then 100 sliding updates)\n";

    // x velocity is zero: motion along z alone must also prevent the resting flag.
    engine::physics::Ball slidingZBall{{1.0, 0.10011962, 2.0}, {0.0, -0.05, -0.5}, 0.1};
    for (int step = 1; step <= 101; ++step)
    {
        engine::physics::advanceBall(slidingZBall, gravityAcceleration, physicsStep, restitution);
        if (!expectBallState("Sliding along z after settling", slidingZBall,
                             {1.0, 0.1, 2.0 - step * 0.005}, {0.0, 0.0, -0.5}, false, step))
        {
            return 1;
        }
    }
    std::cout << "Ball physics z sliding test passed (settled, then 100 sliding updates)\n";

    // the right wall should still rebound after contact at 0.004 seconds.
    engine::physics::Ball rightWallBall{{4.896, 2.0, 1.0}, {1.0, 0.0, -0.5}, 0.1};
    engine::physics::advanceBall(rightWallBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Right wall impact", rightWallBall,
                         {4.8952, 1.9995095, 0.995}, {-0.8, -0.0981, -0.5}, false))
    {
        return 1;
    }
    std::cout << "Ball physics right-wall test passed (contact at 0.004 seconds)\n";

    // reach x = -4.9 at 0.004 seconds, then travel right at 0.8 m/s for 0.006 seconds.
    engine::physics::Ball leftWallBall{{-4.896, 2.0, 1.0}, {-1.0, 0.0, -0.5}, 0.1};
    engine::physics::advanceBall(leftWallBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Left wall impact", leftWallBall,
                         {-4.8952, 1.9995095, 0.995}, {0.8, -0.0981, -0.5}, false))
    {
        return 1;
    }
    std::cout << "Ball physics left-wall test passed (contact at 0.004 seconds)\n";

    // a 0.125 m radius puts contact at -4.875, exactly at the end of this step.
    // these binary-exact positions and duration avoid rounding around the boundary.
    engine::physics::Ball largerWallBall{{-4.75, 2.0, 1.0}, {-1.0, 0.0, 0.0}, 0.125};
    engine::physics::advanceBall(largerWallBall, gravityAcceleration, 0.125, restitution);
    if (!expectBallState("Left wall contact at step end", largerWallBall,
                         {-4.875, 1.923359375, 1.0}, {0.8, -1.22625, 0.0}, false))
    {
        return 1;
    }
    std::cout << "Ball physics left-wall boundary test passed (different radius)\n";

    // the floor's settling branch must save the wall rebound and allow more sliding.
    engine::physics::Ball slidingWallBall{{-4.896, 0.1, 1.0}, {-1.0, 0.0, -0.5}, 0.1};
    for (int step = 1; step <= 101; ++step)
    {
        engine::physics::advanceBall(slidingWallBall, gravityAcceleration, physicsStep, restitution);
        // after the impact step, x advances 0.008 m per step and z advances -0.005 m.
        if (!expectBallState("Left wall impact while sliding", slidingWallBall,
                             {-4.8952 + (step - 1) * 0.008, 0.1, 1.0 - step * 0.005},
                             {0.8, 0.0, -0.5}, false, step))
        {
            return 1;
        }
    }
    std::cout << "Ball physics left-wall sliding test passed (impact, then 100 updates)\n";

    // the floor and wall are both reached at 0.004 seconds; each changes its own axis.
    engine::physics::Ball cornerBall{{-4.896, 0.12007848, 1.0}, {-1.0, -5.0, -0.5}, 0.1};
    engine::physics::advanceBall(cornerBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Left wall and floor impact", cornerBall,
                         {-4.8952, 0.124011772, 0.995}, {0.8, 3.972532, -0.5}, false))
    {
        return 1;
    }
    std::cout << "Ball physics left-wall and floor test passed (simultaneous contact)\n";

    // a ball already moving away from the left wall must keep moving right.
    engine::physics::Ball leavingWallBall{{-4.9, 2.0, 1.0}, {0.8, 0.0, -0.5}, 0.1};
    engine::physics::advanceBall(leavingWallBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Moving away from left wall", leavingWallBall,
                         {-4.892, 1.9995095, 0.995}, {0.8, -0.0981, -0.5}, false))
    {
        return 1;
    }
    std::cout << "Ball physics moving-away test passed (no extra rebound)\n";

    // reach z = 4.9 at 0.004 seconds, then rebound for the remaining 0.006 seconds.
    engine::physics::Ball frontWallBall{{1.0, 2.0, 4.896}, {-0.5, 0.0, 1.0}, 0.1};
    engine::physics::advanceBall(frontWallBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Front wall impact", frontWallBall,
                         {0.995, 1.9995095, 4.8952}, {-0.5, -0.0981, -0.8}, false))
    {
        return 1;
    }
    std::cout << "Ball physics front-wall test passed (contact at 0.004 seconds)\n";

    engine::physics::Ball backWallBall{{1.0, 2.0, -4.896}, {-0.5, 0.0, -1.0}, 0.1};
    engine::physics::advanceBall(backWallBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Back wall impact", backWallBall,
                         {0.995, 1.9995095, -4.8952}, {-0.5, -0.0981, 0.8}, false))
    {
        return 1;
    }
    std::cout << "Ball physics back-wall test passed (contact at 0.004 seconds)\n";

    // a different radius changes the contact position; this contact is exactly at step end.
    engine::physics::Ball largerFrontWallBall{{1.0, 2.0, 4.75}, {0.0, 0.0, 1.0}, 0.125};
    engine::physics::advanceBall(largerFrontWallBall, gravityAcceleration, 0.125, restitution);
    if (!expectBallState("Front wall contact at step end", largerFrontWallBall,
                         {1.0, 1.923359375, 4.875}, {0.0, -1.22625, -0.8}, false))
    {
        return 1;
    }
    std::cout << "Ball physics front-wall boundary test passed (different radius)\n";

    // moving away must not bounce again; zero normal velocity must not divide by zero.
    engine::physics::Ball leavingFrontWallBall{{1.0, 2.0, 4.9}, {-0.5, 0.0, -0.8}, 0.1};
    engine::physics::advanceBall(leavingFrontWallBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Moving away from front wall", leavingFrontWallBall,
                         {0.995, 1.9995095, 4.892}, {-0.5, -0.0981, -0.8}, false))
    {
        return 1;
    }
    engine::physics::Ball leavingBackWallBall{{1.0, 2.0, -4.9}, {-0.5, 0.0, 0.8}, 0.1};
    engine::physics::advanceBall(leavingBackWallBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Moving away from back wall", leavingBackWallBall,
                         {0.995, 1.9995095, -4.892}, {-0.5, -0.0981, 0.8}, false))
    {
        return 1;
    }
    engine::physics::Ball parallelFrontWallBall{{1.0, 2.0, 4.9}, {-0.5, 0.0, 0.0}, 0.1};
    engine::physics::advanceBall(parallelFrontWallBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Parallel to front wall", parallelFrontWallBall,
                         {0.995, 1.9995095, 4.9}, {-0.5, -0.0981, 0.0}, false))
    {
        return 1;
    }
    std::cout << "Ball physics z-wall moving-away and parallel tests passed\n";

    // settling on the floor must retain the z rebound and keep the sliding ball awake.
    engine::physics::Ball slidingBackWallBall{{1.0, 0.1, -4.896}, {0.0, 0.0, -1.0}, 0.1};
    for (int step = 1; step <= 101; ++step)
    {
        engine::physics::advanceBall(slidingBackWallBall, gravityAcceleration, physicsStep, restitution);
        if (!expectBallState("Back wall impact while sliding", slidingBackWallBall,
                             {1.0, 0.1, -4.8952 + (step - 1) * 0.008},
                             {0.0, 0.0, 0.8}, false, step))
        {
            return 1;
        }
    }
    std::cout << "Ball physics back-wall sliding test passed (impact, then 100 updates)\n";

    // left wall, front wall, and floor are all reached at 0.004 seconds.
    engine::physics::Ball threeSurfaceBall{{-4.896, 0.12007848, 4.896}, {-1.0, -5.0, 1.0}, 0.1};
    engine::physics::advanceBall(threeSurfaceBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Three simultaneous surface impacts", threeSurfaceBall,
                         {-4.8952, 0.124011772, 4.8952}, {0.8, 3.972532, -0.8}, false))
    {
        return 1;
    }
    std::cout << "Ball physics x/z-wall and floor test passed (simultaneous contact)\n";

    // independent axes can reach surfaces at different times in the same step:
    // right wall at 0.002 seconds, floor at 0.004, and back wall at 0.008.
    engine::physics::Ball staggeredSurfaceBall{{4.898, 0.12007848, -4.892}, {1.0, -5.0, -1.0}, 0.1};
    engine::physics::advanceBall(staggeredSurfaceBall, gravityAcceleration, physicsStep, restitution);
    if (!expectBallState("Three staggered surface impacts", staggeredSurfaceBall,
                         {4.8936, 0.124011772, -4.8984}, {-0.8, 3.972532, 0.8}, false))
    {
        return 1;
    }
    std::cout << "Ball physics x/z-wall and floor test passed (different contact times)\n";
    return 0;
}
