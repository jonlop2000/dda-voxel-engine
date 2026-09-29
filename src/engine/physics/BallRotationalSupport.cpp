#include "engine/physics/BallRotationalSupport.h"
#include "engine/physics/BallContactForces.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace engine::physics {
namespace {
constexpr double contactTolerance = 1e-9;
constexpr double penetrationTolerance = 1e-6;

std::size_t root(std::vector<std::size_t>& parents, std::size_t index)
{
    while (parents[index] != index)
    {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}

double speed(const glm::dvec3& value)
{
    return std::hypot(value.x, value.y, value.z);
}

bool stationary(const Ball& ball)
{
    return ball.velocity == glm::dvec3{0.0} &&
           ball.angularVelocity == glm::dvec3{0.0};
}

bool balanced(const Ball& ball, const ContactAccelerations& motion,
               std::size_t index, double tolerance)
{
    return speed(motion.linear[index]) <= tolerance &&
           ball.radius * speed(motion.angular[index]) <= tolerance;
}

void sleep(Ball& ball, ContactAccelerations& motion, std::size_t index)
{
    ball.velocity = glm::dvec3{0.0};
    ball.angularVelocity = glm::dvec3{0.0};
    ball.isResting = true;
    motion.linear[index] = glm::dvec3{0.0};
    motion.angular[index] = glm::dvec3{0.0};
}
} // namespace

void settleRotatingBalls(std::vector<Ball>& balls,
                         const BallSimulationSettings& settings,
                         ContactAccelerations& motion, BallBroadPhase& broadPhase)
{
    for (auto& ball : balls) ball.isResting = false;
    const std::size_t count = balls.size();
    if (!(settings.acceleration < 0.0) || count == 0 ||
        motion.linear.size() != count || motion.angular.size() != count) return;
    const auto quiet = [&](std::size_t index) {
        const auto& ball = balls[index];
        const double linearSpeed = speed(ball.velocity);
        const double angularSpeed = speed(ball.angularVelocity);
        const bool slowing = settings.rollingResistance > 0.0 &&
            glm::dot(ball.velocity, motion.linear[index]) < -1e-12 * linearSpeed;
        const bool spinningDown = settings.rollingResistance > 0.0 &&
            glm::dot(ball.angularVelocity, motion.angular[index]) < -1e-12 * angularSpeed;
        return linearSpeed <= (slowing ? 1e-4 : 1e-10) &&
               ball.radius * angularSpeed <= (spinningDown ? 1e-4 : 1e-10);
    };
    bool anyQuiet = false;
    for (std::size_t i = 0; i < count && !anyQuiet; ++i) anyQuiet = quiet(i);
    if (!anyQuiet) return;
    std::vector<std::size_t> parents(count);
    std::iota(parents.begin(), parents.end(), 0);
    std::vector<bool> penetrating(count, false);
    std::size_t contactCount = 0;
    bool overflow = false;
    const auto addPair = [&](std::size_t a, std::size_t b) {
        const double radius = balls[a].radius + balls[b].radius;
        const double distance = speed(balls[b].position - balls[a].position);
        if (radius <= 0.0 || distance > radius + contactTolerance) return;
        if (contactCount == BallBroadPhase::maxCandidatePairs)
        {
            overflow = true;
            return;
        }
        ++contactCount;
        parents[root(parents, b)] = root(parents, a);
        if (radius - distance > penetrationTolerance)
            penetrating[a] = penetrating[b] = true;
    };
    if (settings.useBroadPhase && broadPhase.findContactCandidates(balls, contactTolerance))
    {
        for (const auto& pair : broadPhase.pairs()) addPair(pair.first, pair.second);
    }
    else
    {
        for (std::size_t a = 0; a < count && !overflow; ++a)
            for (std::size_t b = a + 1; b < count && !overflow; ++b) addPair(a, b);
    }
    if (overflow) return;
    std::vector<bool> eligible(count, true), grounded(count, false), exact(count, true);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& ball = balls[i];
        const auto group = root(parents, i);
        const double wallLimit = 5.0 - ball.radius + penetrationTolerance;
        const bool boundaryOverlap = ball.position.y < ball.radius - penetrationTolerance ||
            std::abs(ball.position.x) > wallLimit || std::abs(ball.position.z) > wallLimit;
        eligible[group] = eligible[group] && quiet(i) &&
                          !penetrating[i] && !boundaryOverlap;
        grounded[group] = grounded[group] ||
            std::abs(ball.position.y - ball.radius) <= contactTolerance;
        exact[group] = exact[group] && stationary(ball);
    }
    for (std::size_t i = 0; i < count; ++i)
        eligible[i] = eligible[i] && grounded[i];
    const double tolerance = 1e-7 * std::max(1.0, std::abs(settings.acceleration));
    // exact rest can reuse the force solve already performed for this state.
    auto accepted = eligible;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto group = root(parents, i);
        accepted[group] = accepted[group] && exact[group] &&
                          balanced(balls[i], motion, i, tolerance);
    }
    bool needsTrial = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto group = root(parents, i);
        if (accepted[group]) sleep(balls[i], motion, i);
        needsTrial = needsTrial || (eligible[group] && !exact[group]);
    }
    if (!needsTrial) return;
    auto trial = balls;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto group = root(parents, i);
        if (!eligible[group] || exact[group]) continue;
        trial[i].velocity = glm::dvec3{0.0};
        trial[i].angularVelocity = glm::dvec3{0.0};
    }
    const auto support = calculateContactAccelerations(trial, settings, broadPhase,
                                                       motion.horizon);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto group = root(parents, i);
        if (!exact[group])
            eligible[group] = eligible[group] && balanced(trial[i], support, i, tolerance);
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto group = root(parents, i);
        if (eligible[group] && !exact[group]) sleep(balls[i], motion, i);
    }
}

} // namespace engine::physics
