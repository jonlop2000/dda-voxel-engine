#include "engine/physics/PhysicsSandbox.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

bool near(double actual, double expected)
{
    return std::abs(actual - expected) < 1e-9;
}

void requireConservedMomentum(const engine::physics::BallCollisionEvent& event,
                              const glm::dvec3& expected)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        require(near(event.momentumBefore[axis], expected[axis]), "Incorrect pre-impact momentum");
        require(near(event.momentumAfter[axis], expected[axis]), "Incorrect post-impact momentum");
        require(near(event.momentumAfter[axis] - event.momentumBefore[axis], 0.0),
                "Impact did not conserve pair momentum");
    }
}

void requireSameState(const engine::physics::PhysicsSandbox& a,
                      const engine::physics::PhysicsSandbox& b)
{
    require(a.balls().size() == b.balls().size(), "Different ball counts");
    require(a.elapsedTime() == b.elapsedTime(), "Different simulated times");
    require(a.restitution() == b.restitution(), "Different restitution settings");
    require(a.floorFriction() == b.floorFriction(), "Different floor friction settings");
    require(a.recentCollisions().size() == b.recentCollisions().size(), "Different collision counts");
    for (std::size_t i = 0; i < a.recentCollisions().size(); ++i)
    {
        const auto& first = a.recentCollisions()[i];
        const auto& second = b.recentCollisions()[i];
        require(first.ballA == second.ballA && first.ballB == second.ballB &&
                first.simulationTime == second.simulationTime && first.restitution == second.restitution,
                "Different collision histories");
        require(first.kineticEnergyBefore == second.kineticEnergyBefore &&
                first.kineticEnergyAfter == second.kineticEnergyAfter, "Different collision energy snapshots");
        for (int axis = 0; axis < 3; ++axis)
        {
            require(first.momentumBefore[axis] == second.momentumBefore[axis] &&
                    first.momentumAfter[axis] == second.momentumAfter[axis],
                    "Different collision momentum snapshots");
        }
    }
    for (std::size_t i = 0; i < a.balls().size(); ++i)
    {
        const auto& first = a.balls()[i];
        const auto& second = b.balls()[i];
        for (int axis = 0; axis < 3; ++axis)
        {
            require(first.position[axis] == second.position[axis], "Different positions");
            require(first.velocity[axis] == second.velocity[axis], "Different velocities");
        }
        require(first.radius == second.radius && first.isResting == second.isResting &&
                first.mass == second.mass, "Different ball properties");
    }
}

void checkFloorFrictionControl()
{
    using engine::physics::PhysicsSandbox;
    PhysicsSandbox floor;
    floor.setRestitution(0.0);
    floor.setFloorFriction(0.0);
    for (int step = 0; step < 100; ++step) floor.update(PhysicsSandbox::timeStep);
    floor.setPaused(true);
    const auto initialBall = floor.balls()[1];
    const double initialSpeed = std::hypot(initialBall.velocity.x, initialBall.velocity.z);
    require(initialBall.position.y == initialBall.radius && initialBall.velocity.y == 0.0 &&
            initialSpeed > 0.1, "Friction control test needs a sliding ball");

    for (double coefficient : {0.0, 0.2, 0.8})
    {
        PhysicsSandbox changed = floor;
        changed.setFloorFriction(coefficient);
        require(changed.elapsedTime() == floor.elapsedTime() && changed.isPaused() &&
                changed.recentCollisions().size() == floor.recentCollisions().size(),
                "Changing friction reset playback or collision history");
        for (std::size_t i = 0; i < floor.balls().size(); ++i)
        {
            require(changed.balls()[i].position == floor.balls()[i].position &&
                    changed.balls()[i].velocity == floor.balls()[i].velocity &&
                    changed.balls()[i].isResting == floor.balls()[i].isResting,
                    "Changing friction immediately changed a ball");
        }
        PhysicsSandbox automatic = changed;
        changed.step();
        automatic.setPaused(false);
        automatic.update(PhysicsSandbox::timeStep);
        requireSameState(changed, automatic);
        require(changed.recentCollisions().size() == floor.recentCollisions().size(),
                "Unexpected impact during the isolated sliding measurement");
        const auto& ball = changed.balls()[1];
        const double speed = std::hypot(ball.velocity.x, ball.velocity.z);
        require(near(speed, initialSpeed - coefficient * 9.81 * PhysicsSandbox::timeStep),
                "Selected friction did not reach the next physics step");

        for (bool paused : {false, true})
        {
            changed.setPaused(paused);
            changed.setRestitution(0.65);
            changed.reset();
            PhysicsSandbox expected;
            expected.setRestitution(0.65);
            expected.setFloorFriction(coefficient);
            requireSameState(changed, expected);
            require(changed.isPaused() == paused, "Reset changed playback state");
        }
    }

    PhysicsSandbox bounded;
    bounded.setFloorFriction(-0.5);
    require(bounded.floorFriction() == 0.0, "Negative friction must clamp to zero");
    // friction coefficients can exceed one; only the ui slider uses a range of zero to one.
    bounded.setFloorFriction(1.5);
    require(bounded.floorFriction() == 1.5, "Valid friction above one was rejected");
    bounded.setFloorFriction(std::numeric_limits<double>::quiet_NaN());
    bounded.setFloorFriction(std::numeric_limits<double>::infinity());
    bounded.setFloorFriction(-std::numeric_limits<double>::infinity());
    require(bounded.floorFriction() == 1.5, "Invalid friction replaced the previous setting");
    std::cout << "PASS live friction control, manual/automatic motion, and reset preservation\n";
}

} // namespace

int main()
{
    using engine::physics::PhysicsSandbox;
    try
    {
        PhysicsSandbox fresh;
        require(fresh.balls().size() == 3 && !fresh.isPaused(), "Invalid initial experiment");
        require(fresh.balls()[0].mass == 1.0 && fresh.balls()[1].mass == 1.0 &&
                fresh.balls()[2].mass == 4.0, "Expected masses of 1, 1, and 4 kg");
        require(fresh.elapsedTime() == 0.0, "Initial time must be zero");
        require(fresh.restitution() == 0.8, "Default restitution changed");
        require(fresh.floorFriction() == 0.2, "Default floor friction changed");
        require(fresh.recentCollisions().empty(), "Initial collision history must be empty");
        require(near(fresh.balls()[0].position.x, -0.5) &&
                near(fresh.balls()[1].position.z, 0.1), "Incorrect starting positions");

        PhysicsSandbox accumulated;
        accumulated.update(0.025);
        require(near(accumulated.elapsedTime(), 0.02), "Expected two complete steps");
        accumulated.update(0.006);
        require(near(accumulated.elapsedTime(), 0.03), "Partial time was not retained");
        require(near(accumulated.balls()[0].position.x, -0.47) &&
                near(accumulated.balls()[0].position.y, 1.9955855), "Incorrect accumulated motion");
        std::cout << "PASS fixed steps retain fractional frame time\n";

        PhysicsSandbox paused;
        paused.update(0.009);
        paused.setPaused(true);
        paused.update(100.0);
        requireSameState(paused, fresh);
        paused.step();
        require(near(paused.elapsedTime(), 0.01) && paused.isPaused(), "Manual step must stay paused");
        require(near(paused.balls()[0].position.y, 1.9995095) &&
                near(paused.balls()[0].velocity.y, -0.0981), "Incorrect manual step");
        paused.setPaused(false);
        paused.update(0.002);
        require(near(paused.elapsedTime(), 0.01), "Resume retained old partial or paused time");
        paused.step();
        require(near(paused.elapsedTime(), 0.01), "Manual stepping while running should be ignored");
        paused.update(0.009);
        require(near(paused.elapsedTime(), 0.02), "Resume failed to advance");
        std::cout << "PASS pause, manual step, and resume without catch-up\n";

        paused.setPaused(true);
        paused.reset();
        require(paused.isPaused(), "Reset must preserve pause state");
        requireSameState(paused, fresh);
        paused.step();
        require(near(paused.elapsedTime(), 0.01), "Reset experiment cannot be stepped");
        paused.setPaused(false);
        paused.update(0.009);
        paused.reset();
        require(!paused.isPaused(), "Reset must preserve running state");
        paused.update(0.002);
        requireSameState(paused, fresh);
        std::cout << "PASS reset restores balls, clears both clocks, and preserves playback state\n";

        PhysicsSandbox automatic;
        PhysicsSandbox manual;
        manual.setPaused(true);
        for (int step = 0; step < 42; ++step)
        {
            automatic.update(PhysicsSandbox::timeStep);
            manual.step();
            requireSameState(automatic, manual);
        }
        require(near(manual.elapsedTime(), 0.42), "Incorrect collision time");
        require(near(manual.balls()[0].velocity.x, -0.29438202247191) &&
                near(manual.balls()[0].velocity.z, -0.808988764044944) &&
                near(manual.balls()[1].velocity.x, 0.29438202247191) &&
                near(manual.balls()[1].velocity.z, 0.808988764044944), "Off-center response changed");
        require(near(manual.balls()[0].velocity.y, -4.1202) &&
                near(manual.balls()[1].velocity.y, -4.1202), "Vertical motion changed");
        std::cout << "PASS automatic and manual steps agree through the off-center collision\n";
        require(manual.recentCollisions().size() == 1, "Expected one A-B impact");
        const auto firstImpact = manual.recentCollisions().front();
        require(firstImpact.ballA == 0 && firstImpact.ballB == 1 &&
                near(firstImpact.simulationTime, 0.42), "Incorrect first impact pair or timestamp");
        // Two 1 kg balls at vy = -9.81 * 0.42; gravity is outside the impact measurement.
        requireConservedMomentum(firstImpact, {0.0, -8.2404, 0.0});
        for (int step = 42; step < 60; ++step)
        {
            automatic.update(PhysicsSandbox::timeStep);
            manual.step();
            requireSameState(automatic, manual);
        }
        require(manual.recentCollisions().size() == 2, "Expected A-B and A-C impacts without duplicates");
        const auto secondImpact = manual.recentCollisions().back();
        require(secondImpact.ballA == 0 && secondImpact.ballB == 2 &&
                near(secondImpact.simulationTime, 0.55), "Incorrect second impact pair or timestamp");
        // A is 1 kg and C is 4 kg; these values belong to the 0.55 s impact, not the current 0.60 s frame.
        requireConservedMomentum(secondImpact, {-0.29438202247191, -26.9775, 3.191011235955056});
        requireConservedMomentum(manual.recentCollisions().front(), {0.0, -8.2404, 0.0});
        std::cout << "PASS equal- and unequal-mass impacts conserve captured pair momentum\n";
        // Both impacts can occur within one rendered frame; retain their individual step times.
        PhysicsSandbox oneFrame;
        oneFrame.update(0.605);
        requireSameState(oneFrame, automatic);
        manual.update(10.0);
        requireSameState(manual, automatic);
        manual.reset();
        require(manual.isPaused(), "Clearing history must preserve pause state");
        requireSameState(manual, fresh);
        std::cout << "PASS collision history preserves pairs and step times across playback and reset\n";

        PhysicsSandbox bounded;
        bounded.setRestitution(-0.5);
        require(bounded.restitution() == 0.0, "Restitution must clamp to zero");
        bounded.setRestitution(1.5);
        require(bounded.restitution() == 1.0, "Restitution must clamp to one");
        bounded.setRestitution(0.65);
        bounded.setRestitution(std::numeric_limits<double>::quiet_NaN());
        bounded.setRestitution(std::numeric_limits<double>::infinity());
        bounded.setRestitution(-std::numeric_limits<double>::infinity());
        require(bounded.restitution() == 0.65, "Non-finite input must preserve the valid setting");

        for (double value : {0.0, 0.8, 1.0})
        {
            PhysicsSandbox stepped;
            stepped.setRestitution(value);
            stepped.setPaused(true);
            for (int step = 0; step < 42; ++step) stepped.step();
            PhysicsSandbox batched;
            batched.setRestitution(value);
            batched.update(0.425);
            requireSameState(stepped, batched);
            require(stepped.recentCollisions().size() == 1, "Expected the first impact only");
            const auto event = stepped.recentCollisions().front();
            require(event.restitution == value, "Impact did not record its restitution setting");
            requireConservedMomentum(event, {0.0, -8.2404, 0.0});
            // the first pair has equal 1 kg masses and approach direction (0.16, 0, 0.10).
            // lost energy is half the reduced mass times (1 - e squared) times normal speed squared.
            const double normalSpeedSquared = 4.0 * 0.16 * 0.16 / (0.16 * 0.16 + 0.10 * 0.10);
            const double expectedLoss = 0.25 * (1.0 - value * value) * normalSpeedSquared;
            require(near(event.kineticEnergyBefore, 17.97604804), "Incorrect initial impact energy");
            require(near(event.kineticEnergyBefore - event.kineticEnergyAfter, expectedLoss),
                    "Impact energy loss does not match restitution");
            require(event.kineticEnergyAfter > 0.0, "Zero restitution must preserve shared and tangential motion");

            // changing the control affects future impacts without rewriting the previous measurement.
            const double nextValue = value == 1.0 ? 0.0 : 1.0;
            stepped.setRestitution(nextValue);
            require(near(stepped.elapsedTime(), 0.42) && stepped.recentCollisions().size() == 1,
                    "Changing restitution must not reset the experiment");
            require(stepped.recentCollisions().front().restitution == value &&
                    stepped.recentCollisions().front().kineticEnergyAfter == event.kineticEnergyAfter,
                    "Changing restitution rewrote the recorded impact");
            stepped.reset();
            require(stepped.isPaused(), "Reset must preserve pause while keeping restitution");
            PhysicsSandbox expectedReset;
            expectedReset.setRestitution(nextValue);
            requireSameState(stepped, expectedReset);
        }
        std::cout << "PASS restitution bounds, energy loss, historical settings, and reset preservation\n";

        // the same setting must reach the boundary response: zero settles, one rebounds.
        for (double value : {0.0, 1.0})
        {
            PhysicsSandbox floor;
            floor.setRestitution(value);
            floor.update(0.705);
            const auto& ball = floor.balls().front();
            const double contactTime = std::sqrt(2.0 * (2.0 - 0.1) / 9.81);
            const double remainingTime = 0.7 - contactTime;
            const double expectedVy = value == 0.0 ? 0.0 : 9.81 * contactTime - 9.81 * remainingTime;
            const double expectedY = value == 0.0 ? 0.1 :
                0.1 + 9.81 * contactTime * remainingTime - 0.5 * 9.81 * remainingTime * remainingTime;
            require(near(ball.velocity.y, expectedVy) && near(ball.position.y, expectedY),
                    "Floor response ignored the restitution setting");
        }
        std::cout << "PASS restitution controls floor bounces as well as ball impacts\n";

        checkFloorFrictionControl();

        PhysicsSandbox settled;
        for (int step = 0; step < 3000; ++step) settled.update(PhysicsSandbox::timeStep);
        const auto stoppedBalls = settled.balls();
        for (const auto& ball : stoppedBalls)
        {
            require(ball.isResting, "Floor friction did not bring the experiment to rest");
            require(ball.position.y == ball.radius && ball.velocity == glm::dvec3{0.0},
                    "Resting ball must be supported with zero velocity");
            require(std::abs(ball.position.x) <= 5.0 - ball.radius &&
                    std::abs(ball.position.z) <= 5.0 - ball.radius, "Resting ball escaped the sandbox");
        }
        for (int step = 0; step < 100; ++step) settled.update(PhysicsSandbox::timeStep);
        for (std::size_t i = 0; i < stoppedBalls.size(); ++i)
        {
            require(settled.balls()[i].isResting &&
                    settled.balls()[i].position == stoppedBalls[i].position &&
                    settled.balls()[i].velocity == glm::dvec3{0.0}, "Settled experiment drifted");
        }
        std::cout << "PASS floor friction stops all three balls and keeps them at rest\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
