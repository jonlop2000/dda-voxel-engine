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
    return 0;
}
