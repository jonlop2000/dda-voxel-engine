#include "engine/physics/BallSimulation.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

namespace {
using engine::physics::Ball;
using engine::physics::BallCollisionEvent;
using engine::physics::BallSimulationSettings;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, const char* message, double tolerance = 1e-8)
{
    if (!(std::abs(actual - expected) <= tolerance))
    {
        std::cerr << std::setprecision(17) << message << ": expected " << expected << ", got " << actual << '\n';
        throw std::runtime_error(message);
    }
}

void vectorNear(const glm::dvec3& actual, const glm::dvec3& expected, const char* message)
{
    for (int axis = 0; axis < 3; ++axis) near(actual[axis], expected[axis], message);
}

std::vector<BallCollisionEvent> advance(std::vector<Ball>& balls, double duration,
                                      BallSimulationSettings settings = {}, double startTime = 0.0)
{
    std::vector<BallCollisionEvent> impacts;
    engine::physics::advanceBallSystem(balls, duration, startTime, settings, impacts);
    for (const auto& ball : balls)
    {
        require(std::isfinite(ball.position.x) && std::isfinite(ball.position.y) &&
                std::isfinite(ball.position.z) && std::isfinite(glm::length(ball.velocity)), "Non-finite ball state");
        require(std::abs(ball.position.x) <= 5.0 - ball.radius + 1e-8 &&
                std::abs(ball.position.z) <= 5.0 - ball.radius + 1e-8 &&
                ball.position.y >= ball.radius - 1e-8, "Ball escaped the sandbox");
    }
    for (const auto& impact : impacts)
    {
        require(impact.simulationTime >= startTime - 1e-10 &&
                impact.simulationTime <= startTime + duration + 1e-10, "Impact time escaped the step");
        vectorNear(impact.momentumAfter, impact.momentumBefore, "Pair momentum changed");
        require(impact.kineticEnergyAfter <= impact.kineticEnergyBefore + 1e-8, "Impact created energy");
    }
    return impacts;
}

void testContactTolerance()
{
    const glm::dvec3 direction{1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0};
    Ball a{{0.0, 2.0, 0.0}, direction, 0.1};
    Ball b{a.position + direction * (0.2 + 5e-10), -direction, 0.1};
    require(!engine::physics::resolveBallCollision(a, b, 0.8), "Default contact tolerance changed");
    const auto beforeA = a.position;
    const auto beforeB = b.position;
    require(engine::physics::resolveBallCollision(a, b, 0.8, 1e-9), "Angled near-contact was not resolved");
    vectorNear(a.velocity, -0.8 * direction, "Angled response A");
    vectorNear(b.velocity, 0.8 * direction, "Angled response B");
    require(a.position == beforeA && b.position == beforeB, "Tolerance snapped a positive gap");
    require(!engine::physics::resolveBallCollision(a, b, 0.8, 1e-9), "Separating contact repeated an impulse");
    b.position = a.position + direction * (0.2 + 2e-9);
    a.velocity = direction;
    b.velocity = -direction;
    require(!engine::physics::resolveBallCollision(a, b, 0.8, 1e-9), "Tolerance accepted too large a gap");
    require(!engine::physics::resolveBallCollision(a, b, 0.8, -1.0), "Negative tolerance accepted");
    require(!engine::physics::resolveBallCollision(a, b, 0.8,
                std::numeric_limits<double>::quiet_NaN()), "Non-finite tolerance accepted");
}

void testFastAndAngledImpacts()
{
    const glm::dvec3 directions[] = {{1.0, 0.0, 0.0}, {0.0, 0.0, 1.0},
                                    {1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0}};
    for (const auto& n : directions)
    {
        std::vector<Ball> balls = {{glm::dvec3{0.0, 2.0, 0.0} - n, 200.0 * n, 0.1},
                                  {glm::dvec3{0.0, 2.0, 0.0} + n, -200.0 * n, 0.1}};
        const auto impacts = advance(balls, 0.01, {}, 3.0);
        require(impacts.size() == 1, "Expected one fast-ball impact");
        near(impacts[0].simulationTime, 3.0045, "Fast contact time");
        near(impacts[0].kineticEnergyBefore - impacts[0].kineticEnergyAfter, 14400.0, "Fast collision energy loss");
        const glm::dvec3 center{0.0, 1.9995095, 0.0};
        const glm::dvec3 gravityVelocity{0.0, -0.0981, 0.0};
        vectorNear(balls[0].position, center - 0.98 * n, "Fast final position A");
        vectorNear(balls[1].position, center + 0.98 * n, "Fast final position B");
        vectorNear(balls[0].velocity, gravityVelocity - 160.0 * n, "Fast final velocity A");
        vectorNear(balls[1].velocity, gravityVelocity + 160.0 * n, "Fast final velocity B");
    }
    std::vector<Ball> angled = {{{-1.0, 2.0, 0.0}, {200.0, 0.0, 0.0}, 0.1},
                                {{1.0, 2.0, 0.1}, {-200.0, 0.0, 0.0}, 0.1}};
    const auto impacts = advance(angled, 0.01);
    require(impacts.size() == 1, "Expected one oblique impact");
    near(impacts[0].simulationTime, (2.0 - std::sqrt(0.03)) / 400.0, "Oblique contact time");
    vectorNear(angled[0].velocity, {-70.0, -0.0981, -90.0 * std::sqrt(3.0)}, "Oblique response A");
    vectorNear(angled[1].velocity, {70.0, -0.0981, 90.0 * std::sqrt(3.0)}, "Oblique response B");
}

void testMultipleAndRestingContacts()
{
    std::vector<Ball> balls = {{{-1.0, 2.0, 0.0}, {200.0, 0.0, 0.0}, 0.1},
                              {{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1},
                              {{1.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    const auto impacts = advance(balls, 0.01, {0.0, 1.0, 0.0});
    require(impacts.size() == 2, "Expected two sequential impacts in one step");
    near(impacts[0].simulationTime, 0.004, "First chain impact");
    near(impacts[1].simulationTime, 0.008, "Second chain impact");
    near(balls[0].position.x, -0.2, "Chain position A");
    near(balls[1].position.x, 0.8, "Chain position B");
    near(balls[2].position.x, 1.4, "Chain position C");
    std::vector<Ball> touching = {{{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1},
                                 {{0.2, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    require(advance(touching, 0.01).empty(), "Stationary touching balls produced impact events");
    near(touching[0].position.y, 1.9995095, "Touching balls must still advance time");
    touching[1].position.x = 0.15;
    touching[0].velocity = touching[1].velocity = {0.0, 0.0, 0.0};
    require(advance(touching, 0.01, {0.0, 0.8, 0.0}).empty(), "Overlap correction reported an impact");
    near(touching[1].position.x - touching[0].position.x, 0.2, "Overlap was not corrected");
}

void testBoundaryOrder()
{
    std::vector<Ball> before = {{{4.8, 2.0, 0.0}, {200.0, 0.0, 0.0}, 0.1},
                               {{4.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    const auto impacts = advance(before, 0.01, {0.0, 1.0, 0.0});
    require(impacts.size() == 1, "Wall must redirect ball into a new pair impact");
    near(impacts[0].simulationTime, 0.004, "Wall-before-pair timing");
    near(before[0].position.x, 4.2, "Wall-before-pair position A");
    near(before[1].position.x, 2.8, "Wall-before-pair position B");
    std::vector<Ball> after = {{{3.0, 2.0, 0.0}, {200.0, 0.0, 0.0}, 0.1},
                              {{4.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    require(advance(after, 0.01, {0.0, 1.0, 0.0}).size() == 1, "Pair-before-wall impact count");
    near(after[0].position.x, 3.8, "Pair-before-wall position A");
    near(after[1].position.x, 4.6, "Pair-before-wall position B");
    std::vector<Ball> manyWalls = {{{0.0, 2.0, 0.0}, {4000.0, 0.0, 0.0}, 0.1}};
    advance(manyWalls, 0.01, {0.0, 1.0, 0.0});
    near(manyWalls[0].position.x, 0.8, "Multiple wall bounces");
    near(manyWalls[0].velocity.x, 4000.0, "Multiple wall velocity");
}

void testAcceleratedContacts()
{
    std::vector<Ball> vertical = {{{0.0, 1.0, 0.0}, {0.0, -20.0, 0.0}, 0.1},
                                 {{0.0, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1, true}};
    const auto hits = advance(vertical, 0.1, {-9.81, 1.0, 0.0});
    const double time = 1.4 / (20.0 + std::sqrt(400.0 + 2.0 * 9.81 * 0.7));
    require(hits.size() == 2, "Expected floor-supported pair to transfer its impulse back upward");
    near(hits[0].simulationTime, time, "Different accelerations contact time");
    near(hits[1].simulationTime, time, "Simultaneous pair/floor response time");
    const double remaining = 0.1 - time;
    near(vertical[0].position.y, 0.3 + (20.0 + 9.81 * time) * remaining - 4.905 * remaining * remaining,
         "Supported pair final height");
    near(vertical[1].position.y, 0.1, "Supported ball remains above floor");

    std::vector<Ball> sliding = {{{-0.3, 0.1, 0.0}, {2.0, 0.0, 0.0}, 0.1},
                                {{0.3, 0.1, 0.0}, {-2.0, 0.0, 0.0}, 0.1}};
    const auto initial = sliding;
    const auto impacts = advance(sliding, 0.2, {-10.0, 0.8, 0.2});
    const double contact = 1.0 - std::sqrt(0.8);
    require(impacts.size() == 1, "Sliding pair impact count");
    near(impacts[0].simulationTime, contact, "Friction-adjusted contact time");
    const double left = 0.2 - contact;
    const double reboundSpeed = 0.8 * (2.0 - 2.0 * contact);
    near(sliding[0].position.x, -0.1 - reboundSpeed * left + left * left, "Sliding pair final position");
    near(sliding[0].velocity.x, -reboundSpeed + 2.0 * left, "Sliding pair final velocity");
    auto subdivided = initial;
    std::vector<BallCollisionEvent> accumulated;
    for (int step = 0; step < 20; ++step)
    {
        const auto events = advance(subdivided, 0.01, {-10.0, 0.8, 0.2}, step * 0.01);
        accumulated.insert(accumulated.end(), events.begin(), events.end());
    }
    require(accumulated.size() == 1, "Subdivision changed impact count");
    near(accumulated[0].simulationTime, contact, "Subdivision changed impact time");
    for (std::size_t i = 0; i < sliding.size(); ++i)
    {
        vectorNear(subdivided[i].position, sliding[i].position, "Subdivision changed position");
        vectorNear(subdivided[i].velocity, sliding[i].velocity, "Subdivision changed velocity");
    }
    std::vector<Ball> stopping = {{{-0.4, 0.1, 0.0}, {1.0, 0.0, 0.0}, 0.1},
                                 {{0.4, 0.1, 0.0}, {0.0, 0.0, 0.0}, 0.1, true}};
    require(advance(stopping, 1.0, {-10.0, 0.8, 0.2}).empty(), "Friction should stop the ball before contact");
    near(stopping[0].position.x, -0.15, "Stopping distance before another ball");
    require(stopping[0].isResting, "Sliding stop did not enter rest");

    // tiny spheres expose false zero-time roots when a global polynomial tolerance is too loose.
    std::vector<Ball> tiny = {{{-5e-8, 1e-8, 0.0}, {200.0, 0.0, 0.0}, 1e-8},
                             {{5e-8, 1e-8, 0.0}, {-200.0, 0.0, 0.0}, 1e-8}};
    const auto tinyImpacts = advance(tiny, 0.01, {-10.0, 0.8, 0.2});
    require(tinyImpacts.size() == 1, "Tiny spheres must resolve one impact and finish the step");
    near(tinyImpacts[0].simulationTime, 2e-10, "Tiny sphere contact time", 1e-12);
    require(tiny[0].position.x < -1.0 && tiny[1].position.x > 1.0,
            "Tiny-sphere collision did not advance the remaining time");
}

void testSingleBallAgreement()
{
    const Ball initialStates[] = {
        {{4.896, 0.10047658, 0.0}, {1.0, -0.05, 0.0}, 0.1},
        {{4.894015696, 0.10011962, 0.0}, {1.0, -0.05, 0.0}, 0.1},
        {{4.8976094176, 0.1, 4.8968125568}, {0.6, 0.0, 0.8}, 0.1},
        {{0.0, 0.1, -4.896015696}, {0.0, 0.0, -1.0}, 0.1},
        {{0.0, 0.1, 0.0}, {0.01, 0.0, 0.0}, 0.1}
    };
    for (const auto& initial : initialStates)
    {
        Ball reference = initial;
        engine::physics::advanceBall(reference, -9.81, 0.01, 0.8, 0.2);
        std::vector<Ball> actual{initial};
        require(advance(actual, 0.01).empty(), "Single ball generated a pair impact");
        vectorNear(actual[0].position, reference.position, "Single-ball position changed");
        vectorNear(actual[0].velocity, reference.velocity, "Single-ball velocity changed");
        require(actual[0].isResting == reference.isResting, "Single-ball rest state changed");
    }
}

} // namespace

int main()
{
    try
    {
        testContactTolerance();
        std::cout << "PASS general contact tolerance and separating contacts\n";
        testFastAndAngledImpacts();
        std::cout << "PASS fast, angled, and three-axis continuous collisions\n";
        testMultipleAndRestingContacts();
        std::cout << "PASS multiple impacts and non-impact contact progress\n";
        testBoundaryOrder();
        std::cout << "PASS boundary ordering and multiple wall bounces\n";
        testSingleBallAgreement();
        std::cout << "PASS single-ball floor, wall, corner, and stopping regressions\n";
        testAcceleratedContacts();
        std::cout << "PASS supported contacts, friction timing, stopping, and subdivision\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
