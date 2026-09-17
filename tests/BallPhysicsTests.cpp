#include "engine/physics/BallPhysics.h"

#include <cmath>
#include <iomanip>
#include <iostream>

int main()
{
    engine::physics::Ball ball{2.0, 0.0, 0.1};
    const double gravityAcceleration = -9.81;
    const double physicsStep = 0.01;
    const double restitution = 0.8;

    // simulate 0.5 seconds without needing a renderer.
    for (int step = 0; step < 50; ++step)
    {
        engine::physics::advanceBall(ball, gravityAcceleration, physicsStep, restitution);
    }

    // y = 2 + 0.5 * (-9.81) * (0.5 * 0.5), v = 0 + (-9.81) * 0.5.
    // center is still above the 0.1 m contact height, so no bounce has occurred.
    const double expectedPosition = 0.77375;
    const double expectedVelocity = -4.905;
    const double tolerance = 1e-9;

    // allow tiny floating point rounding differences instead of requiring exact equality.
    const bool positionMatches = std::abs(ball.position - expectedPosition) < tolerance;
    const bool velocityMatches = std::abs(ball.velocity - expectedVelocity) < tolerance;

    if (!positionMatches || !velocityMatches || ball.isResting)
    {
        std::cerr << std::setprecision(17)
                  << "Ball physics free-fall test failed after 0.5 seconds\n"
                  << "Position: expected " << expectedPosition << " m, got "
                  << ball.position << " m\n"
                  << "Velocity: expected " << expectedVelocity << " m/s, got "
                  << ball.velocity << " m/s\n"
                  << "Resting: expected false, got " << std::boolalpha << ball.isResting
                  << '\n';
        return 1;
    }

    std::cout << "Ball physics free-fall test passed (0.5 seconds)\n";

    // start at floor contact, moving downward, so the impact occurs immediately.
    engine::physics::Ball impactBall{0.1, -5.0, 0.1};
    engine::physics::advanceBall(impactBall, gravityAcceleration, physicsStep, restitution);

    const double expectedImpactPosition = 0.1395095;
    const double expectedImpactVelocity = 3.9019;
    const bool impactPositionMatches =
        std::abs(impactBall.position - expectedImpactPosition) < tolerance;
    const bool impactVelocityMatches =
        std::abs(impactBall.velocity - expectedImpactVelocity) < tolerance;

    if (!impactPositionMatches || !impactVelocityMatches || impactBall.isResting)
    {
        std::cerr << std::setprecision(17)
                  << "Ball physics impact test failed after 0.01 seconds\n"
                  << "Position: expected " << expectedImpactPosition << " m, got "
                  << impactBall.position << " m\n"
                  << "Velocity: expected " << expectedImpactVelocity << " m/s, got "
                  << impactBall.velocity << " m/s\n"
                  << "Resting: expected false, got " << std::boolalpha << impactBall.isResting
                  << '\n';
        return 1;
    }

    std::cout << "Ball physics impact test passed (restitution 0.8)\n";

    // a -0.05 m/s impact with restitution 0.8 would rebound at only 0.04 m/s,
    // below the 0.1 m/s settling threshold. Start with isResting defaulting to false.
    engine::physics::Ball settlingBall{0.1, -0.05, 0.1};
    const double expectedRestPosition = 0.1;
    const double expectedRestVelocity = 0.0;

    // first update must settle the ball; the next 100 must keep it resting.
    for (int step = 0; step < 101; ++step)
    {
        engine::physics::advanceBall(settlingBall, gravityAcceleration, physicsStep, restitution);

        const bool restPositionMatches =
            std::abs(settlingBall.position - expectedRestPosition) < tolerance;
        const bool restVelocityMatches =
            std::abs(settlingBall.velocity - expectedRestVelocity) < tolerance;

        if (!restPositionMatches || !restVelocityMatches || !settlingBall.isResting)
        {
            std::cerr << std::setprecision(17)
                      << "Ball physics settling test failed on update " << step + 1
                      << " (update 1 settles; updates 2-101 must stay resting)\n"
                      << "Position: expected " << expectedRestPosition << " m, got "
                      << settlingBall.position << " m\n"
                      << "Velocity: expected " << expectedRestVelocity << " m/s, got "
                      << settlingBall.velocity << " m/s\n"
                      << "Resting: expected true, got " << std::boolalpha << settlingBall.isResting
                      << '\n';
            return 1;
        }
    }

    std::cout << "Ball physics settling test passed (settled, then 100 resting updates)\n";
    return 0;
}
