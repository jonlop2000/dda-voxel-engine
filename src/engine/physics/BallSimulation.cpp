#include "engine/physics/BallSimulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/geometric.hpp>

namespace engine::physics {
namespace {

constexpr double contactTolerance = 1e-9;
constexpr double restingNormalSpeed = 1e-7;
constexpr double floorRestSpeed = 0.1;
using Polynomial = std::array<long double, 5>;

long double evaluate(const Polynomial& coefficients, int degree, long double x)
{
    long double value = coefficients[degree];
    for (int i = degree - 1; i >= 0; --i) value = value * x + coefficients[i];
    return value;
}

// derivative roots partition a polynomial into monotonic intervals.
// bisect those intervals instead of using an ill-conditioned closed-form quartic formula.
std::vector<long double> rootsInUnitInterval(const Polynomial& coefficients, int degree)
{
    while (degree > 0 && coefficients[degree] == 0.0L) --degree;
    if (degree == 0) return {};
    if (degree == 1)
    {
        const long double root = -coefficients[0] / coefficients[1];
        if (root >= 0.0L && root <= 1.0L) return {root};
        return {};
    }
    Polynomial derivative{};
    long double scale = 0.0L;
    for (int i = 0; i <= degree; ++i) scale += std::abs(coefficients[i]);
    for (int i = 1; i <= degree; ++i) derivative[i - 1] = i * coefficients[i];
    auto points = rootsInUnitInterval(derivative, degree - 1);
    points.insert(points.begin(), 0.0L);
    points.push_back(1.0L);
    const long double roundoff = 16.0L * std::numeric_limits<long double>::epsilon() * scale;
    std::vector<long double> roots;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const long double atPoint = evaluate(coefficients, degree, points[i]);
        if (std::abs(atPoint) <= roundoff) roots.push_back(points[i]);
        if (i + 1 == points.size()) break;
        long double lo = points[i];
        long double hi = points[i + 1];
        long double atLo = atPoint;
        const long double atHi = evaluate(coefficients, degree, hi);
        if (!((atLo < 0.0L && atHi > 0.0L) || (atLo > 0.0L && atHi < 0.0L))) continue;
        for (int iteration = 0; iteration < 64; ++iteration)
        {
            const long double middle = (lo + hi) * 0.5L;
            if (middle == lo || middle == hi) break;
            const long double value = evaluate(coefficients, degree, middle);
            if ((value < 0.0L) == (atLo < 0.0L)) { lo = middle; atLo = value; }
            else hi = middle;
        }
        roots.push_back((lo + hi) * 0.5L);
    }
    std::sort(roots.begin(), roots.end());
    return roots;
}

bool supported(const Ball& ball)
{
    return std::abs(ball.position.y - ball.radius) <= contactTolerance && ball.velocity.y == 0.0;
}

glm::dvec3 contactNormal(const Ball& a, const Ball& b)
{
    const glm::dvec3 offset = b.position - a.position;
    const double distance = glm::length(offset);
    return distance > 0.0 ? offset / distance : glm::dvec3{1.0, 0.0, 0.0};
}

bool nearContact(const Ball& a, const Ball& b)
{
    const glm::dvec3 offset = b.position - a.position;
    const double reach = a.radius + b.radius + contactTolerance;
    return glm::dot(offset, offset) <= reach * reach;
}

void resolveBoundaries(Ball& ball, double restitution)
{
    const double limit = 5.0 - ball.radius;
    for (int axis : {0, 2})
    {
        if (ball.position[axis] >= limit - contactTolerance && ball.velocity[axis] > 0.0)
        {
            ball.position[axis] = limit;
            ball.velocity[axis] *= -restitution;
        }
        if (ball.position[axis] <= -limit + contactTolerance && ball.velocity[axis] < 0.0)
        {
            ball.position[axis] = -limit;
            ball.velocity[axis] *= -restitution;
        }
        ball.position[axis] = std::clamp(ball.position[axis], -limit, limit);
    }
    if (ball.position.y <= ball.radius + contactTolerance && ball.velocity.y <= 0.0)
    {
        ball.position.y = ball.radius;
        const double rebound = -restitution * ball.velocity.y;
        ball.velocity.y = rebound <= floorRestSpeed ? 0.0 : rebound;
    }
    ball.position.y = std::max(ball.position.y, ball.radius);
    ball.isResting = supported(ball) && ball.velocity.x == 0.0 && ball.velocity.z == 0.0;
}

void resolveContacts(std::vector<Ball>& balls, double time, double restitution,
                     std::vector<BallCollisionEvent>& impacts)
{
    // repeat simultaneous contacts so a wall response or pair impulse can reach its neighbors.
    for (int pass = 0; pass < 16; ++pass)
    {
        bool changed = false;
        for (auto& ball : balls) resolveBoundaries(ball, restitution);
        for (std::size_t i = 0; i < balls.size(); ++i)
        {
            for (std::size_t j = i + 1; j < balls.size(); ++j)
            {
                auto& a = balls[i];
                auto& b = balls[j];
                if (!nearContact(a, b)) continue;
                const auto oldA = a.position;
                const auto oldB = b.position;
                const double approach = -glm::dot(b.velocity - a.velocity, contactNormal(a, b));
                // settle numerically tiny rebounds instead of producing an endless contact sequence.
                const double impactRestitution = approach < restingNormalSpeed ? 0.0 : restitution;
                const auto momentumBefore = a.mass * a.velocity + b.mass * b.velocity;
                const double energyBefore = 0.5 * (a.mass * glm::dot(a.velocity, a.velocity) +
                                                   b.mass * glm::dot(b.velocity, b.velocity));
                if (resolveBallCollision(a, b, impactRestitution, contactTolerance))
                {
                    // only record impacts above the resting-contact speed tolerance.
                    if (approach >= restingNormalSpeed)
                    {
                        impacts.push_back({i, j, time, momentumBefore,
                            a.mass * a.velocity + b.mass * b.velocity, energyBefore,
                            0.5 * (a.mass * glm::dot(a.velocity, a.velocity) +
                                   b.mass * glm::dot(b.velocity, b.velocity)), impactRestitution});
                    }
                    changed = true;
                }
                changed = changed || a.position != oldA || b.position != oldB;
            }
        }
        if (!changed) break;
    }
    for (auto& ball : balls) resolveBoundaries(ball, restitution);
}

std::vector<glm::dvec3> accelerations(const std::vector<Ball>& balls,
                                    const BallSimulationSettings& settings)
{
    std::vector<glm::dvec3> result(balls.size(), {0.0, settings.acceleration, 0.0});
    for (std::size_t i = 0; i < balls.size(); ++i)
    {
        const auto& ball = balls[i];
        if (!supported(ball)) continue;
        const double speed = std::hypot(ball.velocity.x, ball.velocity.z);
        const double deceleration = settings.floorFriction * std::max(0.0, -settings.acceleration);
        result[i] = speed > 0.0 ? -deceleration * ball.velocity / speed : glm::dvec3{0.0};
    }
    // resting contacts must resist inward acceleration as well as inward velocity.
    for (int pass = 0; pass < 32; ++pass)
    {
        double largestChange = 0.0;
        for (std::size_t i = 0; i < balls.size(); ++i)
        {
            const auto& ball = balls[i];
            if (supported(ball)) result[i].y = std::max(0.0, result[i].y);
            for (int axis : {0, 2})
            {
                const double limit = 5.0 - ball.radius;
                if (std::abs(ball.velocity[axis]) > restingNormalSpeed) continue;
                if (ball.position[axis] >= limit - contactTolerance) result[i][axis] = std::min(0.0, result[i][axis]);
                if (ball.position[axis] <= -limit + contactTolerance) result[i][axis] = std::max(0.0, result[i][axis]);
            }
            for (std::size_t j = i + 1; j < balls.size(); ++j)
            {
                if (!nearContact(ball, balls[j])) continue;
                const auto normal = contactNormal(ball, balls[j]);
                if (std::abs(glm::dot(balls[j].velocity - ball.velocity, normal)) > restingNormalSpeed) continue;
                const double inward = glm::dot(result[j] - result[i], normal);
                if (inward >= 0.0) continue;
                const double correction = -inward / (1.0 / ball.mass + 1.0 / balls[j].mass);
                result[i] -= correction / ball.mass * normal;
                result[j] += correction / balls[j].mass * normal;
                largestChange = std::max(largestChange, -inward);
            }
        }
        if (largestChange < 1e-10) break;
    }
    return result;
}

double planeContactTime(double gap, double velocity, double acceleration, double horizon)
{
    const Polynomial polynomial{gap, velocity * horizon, 0.5L * acceleration * horizon * horizon, 0.0L, 0.0L};
    for (long double root : rootsInUnitInterval(polynomial, 2))
    {
        const double time = static_cast<double>(root) * horizon;
        if (time > 0.0 && velocity + acceleration * time < 0.0) return time;
    }
    return horizon;
}

double pairContactTime(const Ball& a, const Ball& b, const glm::dvec3& accelerationA,
                       const glm::dvec3& accelerationB, double horizon)
{
    const glm::dvec3 p = b.position - a.position;
    const glm::dvec3 v = b.velocity - a.velocity;
    const glm::dvec3 relativeAcceleration = accelerationB - accelerationA;
    if (relativeAcceleration == glm::dvec3{0.0})
    {
        const auto time = findBallCollisionTime(a, b, horizon);
        if (!time) return horizon;
        const auto offset = p + v * *time;
        if (glm::dot(offset, v) < -restingNormalSpeed * glm::length(offset)) return *time;
        return horizon;
    }
    // different accelerations produce a quartic distance equation; scale time to [0, 1].
    Polynomial polynomial{};
    const long double radius = a.radius + b.radius;
    polynomial[0] = -radius * radius;
    for (int axis = 0; axis < 3; ++axis)
    {
        const long double position = p[axis];
        const long double velocity = static_cast<long double>(v[axis]) * horizon;
        const long double acceleration = 0.5L * relativeAcceleration[axis] * horizon * horizon;
        polynomial[0] += position * position;
        polynomial[1] += 2.0L * position * velocity;
        polynomial[2] += velocity * velocity + 2.0L * position * acceleration;
        polynomial[3] += 2.0L * velocity * acceleration;
        polynomial[4] += acceleration * acceleration;
    }
    for (long double root : rootsInUnitInterval(polynomial, 4))
    {
        const double time = static_cast<double>(root) * horizon;
        const auto offset = p + v * time + 0.5 * relativeAcceleration * time * time;
        const auto velocity = v + relativeAcceleration * time;
        const double distance = glm::length(offset);
        // a polynomial close to zero must still describe actual geometric contact.
        // this also rejects a spurious zero-time root for very small spheres.
        if (distance <= a.radius + b.radius + contactTolerance &&
            glm::dot(offset, velocity) < -restingNormalSpeed * distance) return time;
    }
    return horizon;
}

} // namespace

void advanceBallSystem(std::vector<Ball>& balls, double duration, double startTime,
                       const BallSimulationSettings& settings,
                       std::vector<BallCollisionEvent>& impacts)
{
    impacts.clear();
    if (!std::isfinite(duration) || duration <= 0.0) return;
    double remaining = duration;
    std::vector<double> stoppingTimes(balls.size());
    for (;;)
    {
        resolveContacts(balls, startTime + duration - remaining, settings.restitution, impacts);
        if (remaining <= 0.0) break;
        const auto acceleration = accelerations(balls, settings);
        double segment = remaining;
        for (std::size_t i = 0; i < balls.size(); ++i)
        {
            const auto& ball = balls[i];
            const auto& force = acceleration[i];
            stoppingTimes[i] = std::numeric_limits<double>::infinity();
            const double speed = std::hypot(ball.velocity.x, ball.velocity.z);
            if (supported(ball) && speed > 0.0)
            {
                const double rate = -glm::dot(force, ball.velocity) / (speed * speed);
                if (rate > 0.0 && glm::length(force + rate * ball.velocity) < 1e-10)
                {
                    stoppingTimes[i] = 1.0 / rate;
                    segment = std::min(segment, stoppingTimes[i]);
                }
            }
            segment = std::min(segment, planeContactTime(ball.position.y - ball.radius,
                ball.velocity.y, force.y, remaining));
            const double limit = 5.0 - ball.radius;
            for (int axis : {0, 2})
            {
                segment = std::min(segment, planeContactTime(limit - ball.position[axis],
                    -ball.velocity[axis], -force[axis], remaining));
                segment = std::min(segment, planeContactTime(ball.position[axis] + limit,
                    ball.velocity[axis], force[axis], remaining));
            }
        }
        // boundary and stopping events limit how long the current accelerations remain valid.
        for (std::size_t i = 0; i < balls.size(); ++i)
            for (std::size_t j = i + 1; j < balls.size(); ++j)
                segment = std::min(segment, pairContactTime(balls[i], balls[j],
                    acceleration[i], acceleration[j], segment));
        for (std::size_t i = 0; i < balls.size(); ++i)
        {
            auto& ball = balls[i];
            ball.position += ball.velocity * segment + 0.5 * acceleration[i] * segment * segment;
            ball.velocity += acceleration[i] * segment;
            if (stoppingTimes[i] <= segment) ball.velocity.x = ball.velocity.z = 0.0;
        }
        remaining = std::max(0.0, remaining - segment);
    }
}

} // namespace engine::physics
