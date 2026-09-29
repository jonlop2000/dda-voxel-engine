#include "engine/physics/BallSimulation.h"
#include "engine/physics/BallBroadPhase.h"
#include "engine/physics/BallSupport.h"
#include "engine/physics/BallContact.h"
#include "engine/physics/BallContactForces.h"
#include "engine/physics/BallRotationalSupport.h"

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
constexpr double restitutionSpeedThreshold = 0.1;
constexpr double contactTimeStep = 0.005;
using Polynomial = std::array<long double, 5>;

long double evaluate(const Polynomial& coefficients, int degree, long double x)
{
    long double value = coefficients[degree];
    for (int i = degree - 1; i >= 0; --i) value = value * x + coefficients[i];
    return value;
}

// bisect monotonic intervals separated by derivative roots.
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

bool persistentContact(const Ball& a, const Ball& b)
{
    return a.radius + b.radius > 0.0 && nearContact(a, b) &&
        std::abs(glm::dot(b.velocity - a.velocity, contactNormal(a, b))) < restitutionSpeedThreshold;
}

bool resolveBoundaries(Ball& ball, const BallSimulationSettings& settings)
{
    const auto oldVelocity = ball.velocity;
    const auto oldSpin = ball.angularVelocity;
    const double restitution = settings.restitution;
    const auto reflect = [&](const glm::dvec3& normal, double bounce, double friction) {
        const double incoming = glm::dot(ball.velocity, normal);
        const double impulse = -(1.0 + bounce) * incoming * ball.mass;
        ball.velocity -= (1.0 + bounce) * incoming * normal;
        applyBallBoundaryFriction(ball, normal, impulse, friction);
    };
    const double limit = 5.0 - ball.radius;
    for (int axis : {0, 2})
    {
        if (ball.position[axis] >= limit - contactTolerance && ball.velocity[axis] > 0.0)
        {
            ball.position[axis] = limit;
            if (settings.enableRotation)
            {
                glm::dvec3 normal{0.0};
                normal[axis] = -1.0;
                reflect(normal, restitution, settings.wallFriction);
            }
            else ball.velocity[axis] *= -restitution;
        }
        if (ball.position[axis] <= -limit + contactTolerance && ball.velocity[axis] < 0.0)
        {
            ball.position[axis] = -limit;
            if (settings.enableRotation)
            {
                glm::dvec3 normal{0.0};
                normal[axis] = 1.0;
                reflect(normal, restitution, settings.wallFriction);
            }
            else ball.velocity[axis] *= -restitution;
        }
        ball.position[axis] = std::clamp(ball.position[axis], -limit, limit);
    }
    if (ball.position.y <= ball.radius + contactTolerance && ball.velocity.y <= 0.0)
    {
        ball.position.y = ball.radius;
        const double rebound = -restitution * ball.velocity.y;
        if (settings.enableRotation && ball.velocity.y < 0.0)
            reflect({0.0, 1.0, 0.0}, rebound <= floorRestSpeed ? 0.0 : restitution,
                    settings.floorFriction);
        else ball.velocity.y = rebound <= floorRestSpeed ? 0.0 : rebound;
    }
    ball.position.y = std::max(ball.position.y, ball.radius);
    ball.isResting = supported(ball) && ball.velocity.x == 0.0 && ball.velocity.z == 0.0 &&
        (!settings.enableRotation || ball.angularVelocity == glm::dvec3{0.0});
    return settings.enableRotation && (ball.velocity != oldVelocity || ball.angularVelocity != oldSpin);
}

void resolveContacts(std::vector<Ball>& balls, double time,
                     const BallSimulationSettings& settings, BallBroadPhase& broadPhase,
                     std::vector<BallCollisionEvent>& impacts)
{
    constexpr int maxRebuildsPerPass = 4;
    const auto energy = [&](const Ball& ball) {
        return settings.enableRotation ? ballKineticEnergy(ball) :
            0.5 * ball.mass * glm::dot(ball.velocity, ball.velocity);
    };
    // repeat contacts so impulses can propagate to neighboring balls.
    for (int pass = 0; pass < 16; ++pass)
    {
        bool changed = false;
        for (auto& ball : balls) changed = resolveBoundaries(ball, settings) || changed;
        bool filtered = settings.useBroadPhase &&
            broadPhase.findContactCandidates(balls, contactTolerance);
        const auto resolvePair = [&](std::size_t i, std::size_t j) {
            auto& a = balls[i];
            auto& b = balls[j];
            if (!nearContact(a, b)) return false;
            const auto oldA = a.position;
            const auto oldB = b.position;
            const double approach = -glm::dot(b.velocity - a.velocity, contactNormal(a, b));
            const double penetration = a.radius + b.radius - glm::length(b.position - a.position);
            const double slop = std::abs(approach) < restitutionSpeedThreshold ?
                std::min(1e-6, (a.radius + b.radius) * 1e-6) : 0.0;
            if (approach <= 0.0 && penetration <= slop) return false;
            // suppress small rebounds before they become repeated impacts.
            const double impactRestitution = approach < restitutionSpeedThreshold ? 0.0 : settings.restitution;
            const bool reportImpact = approach >= restitutionSpeedThreshold;
            const auto momentumBefore = reportImpact ? a.mass * a.velocity + b.mass * b.velocity : glm::dvec3{0.0};
            const double energyBefore = reportImpact ? energy(a) + energy(b) : 0.0;
            if (resolveBallCollision(a, b, impactRestitution, contactTolerance,
                                     settings.enableRotation ? settings.ballFriction : 0.0))
            {
                // support impulses below the bounce threshold are not new hits.
                if (reportImpact)
                {
                    impacts.push_back({i, j, time, momentumBefore,
                        a.mass * a.velocity + b.mass * b.velocity, energyBefore,
                        energy(a) + energy(b), impactRestitution, settings.enableRotation});
                }
                changed = changed || approach >= restingNormalSpeed;
            }
            // leave submicrometer overlap alone while slow contacts settle.
            if (penetration <= slop)
            {
                a.position = oldA;
                b.position = oldB;
            }
            const bool moved = a.position != oldA || b.position != oldB;
            changed = changed || moved;
            return moved;
        };
        std::size_t i = 0;
        std::size_t j = 1;
        std::size_t candidateIndex = 0;
        int rebuilds = 0;
        for (;;)
        {
            if (filtered)
            {
                if (candidateIndex == broadPhase.pairs().size()) break;
                const auto pair = broadPhase.pairs()[candidateIndex++];
                i = pair.first;
                j = pair.second;
            }
            else
            {
                if (j >= balls.size()) { ++i; j = i + 1; }
                if (j >= balls.size()) break;
            }
            if (resolvePair(i, j) && filtered)
            {
                // fall back after exhausting this pass's rebuild budget.
                filtered = rebuilds++ < maxRebuildsPerPass &&
                    broadPhase.findContactCandidates(balls, contactTolerance);
                if (filtered)
                {
                    // resume after this pair to preserve solver order.
                    const auto& pairs = broadPhase.pairs();
                    const auto next = std::upper_bound(pairs.begin(), pairs.end(), BallPair{i, j},
                        [](const BallPair& a, const BallPair& b) {
                            return a.first < b.first || (a.first == b.first && a.second < b.second);
                        });
                    candidateIndex = static_cast<std::size_t>(next - pairs.begin());
                }
            }
            ++j;
        }
        if (!changed) break;
    }
    for (auto& ball : balls) resolveBoundaries(ball, settings);
}

std::vector<glm::dvec3> accelerations(const std::vector<Ball>& balls,
                                    const BallSimulationSettings& settings,
                                    BallBroadPhase& broadPhase, double& contactHorizon)
{
    std::vector<glm::dvec3> result(balls.size(), {0.0, settings.acceleration, 0.0});
    for (std::size_t i = 0; i < balls.size(); ++i)
    {
        const auto& ball = balls[i];
        if (ball.isResting)
        {
            result[i] = {0.0, 0.0, 0.0};
            continue;
        }
        if (!supported(ball)) continue;
        const double speed = std::hypot(ball.velocity.x, ball.velocity.z);
        const double deceleration = settings.floorFriction * std::max(0.0, -settings.acceleration);
        result[i] = speed > 0.0 ? -deceleration * ball.velocity / speed : glm::dvec3{0.0};
    }
    // positions stay fixed while acceleration constraints converge.
    const bool filtered = settings.useBroadPhase &&
        broadPhase.findContactCandidates(balls, contactTolerance);
    const auto& candidates = broadPhase.pairs();
    for (int pass = 0; pass < 32; ++pass)
    {
        double largestChange = 0.0;
        std::size_t pairIndex = 0;
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
            const auto constrainPair = [&](std::size_t j) {
                if (!nearContact(ball, balls[j])) return;
                const auto normal = contactNormal(ball, balls[j]);
                if (persistentContact(ball, balls[j]) && (!ball.isResting || !balls[j].isResting))
                {
                    const double relativeSpeed = glm::length(balls[j].velocity - ball.velocity);
                    const double distanceLimit = 0.05 * (ball.radius + balls[j].radius);
                    const double interval = relativeSpeed > 0.0 ?
                        std::min(contactTimeStep, distanceLimit / relativeSpeed) : contactTimeStep;
                    contactHorizon = std::min(contactHorizon, interval);
                }
                const double normalSpeed = glm::dot(balls[j].velocity - ball.velocity, normal);
                if (normalSpeed > restingNormalSpeed || normalSpeed < -restitutionSpeedThreshold) return;
                const double inward = glm::dot(result[j] - result[i], normal);
                if (inward >= 0.0) return;
                const double correction = -inward / (1.0 / ball.mass + 1.0 / balls[j].mass);
                result[i] -= correction / ball.mass * normal;
                result[j] += correction / balls[j].mass * normal;
                largestChange = std::max(largestChange, -inward);
            };
            // preserve the original order of boundary and pair constraints.
            if (filtered)
            {
                while (pairIndex < candidates.size() && candidates[pairIndex].first == i)
                    constrainPair(candidates[pairIndex++].second);
            }
            else
            {
                for (std::size_t j = i + 1; j < balls.size(); ++j) constrainPair(j);
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
    // existing slow contacts use bounded constraint steps instead of new hits.
    if (persistentContact(a, b)) return horizon;
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
    // scale time to [0, 1] for the quartic from unequal accelerations.
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
        // check geometry to reject false roots, including tiny-sphere cases.
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
    BallBroadPhase broadPhase;
    for (;;)
    {
        resolveContacts(balls, startTime + duration - remaining, settings, broadPhase, impacts);
        double segment = remaining;
        ContactAccelerations motion;
        if (settings.enableRotation)
        {
            motion = calculateContactAccelerations(balls, settings, broadPhase, remaining);
            settleRotatingBalls(balls, settings, motion, broadPhase);
            segment = motion.horizon;
        }
        else
        {
            settleSupportedBalls(balls, settings.acceleration, settings.floorFriction,
                                 broadPhase, settings.useBroadPhase);
            if (remaining > 0.0) motion.linear = accelerations(balls, settings, broadPhase, segment);
        }
        if (remaining <= 0.0) break;
        const auto& acceleration = motion.linear;
        for (std::size_t i = 0; i < balls.size(); ++i)
        {
            const auto& ball = balls[i];
            const auto& force = acceleration[i];
            stoppingTimes[i] = std::numeric_limits<double>::infinity();
            const double speed = std::hypot(ball.velocity.x, ball.velocity.z);
            if (!settings.enableRotation && supported(ball) && speed > 0.0)
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
        // rebuild swept paths after each event that can change motion.
        if (settings.useBroadPhase && broadPhase.findCandidates(balls, acceleration, segment, contactTolerance))
        {
            for (const auto& pair : broadPhase.pairs())
                segment = std::min(segment, pairContactTime(balls[pair.first], balls[pair.second],
                    acceleration[pair.first], acceleration[pair.second], segment));
        }
        else
        {
            // search all pairs when filtering is disabled or fails.
            for (std::size_t i = 0; i < balls.size(); ++i)
                for (std::size_t j = i + 1; j < balls.size(); ++j)
                    segment = std::min(segment, pairContactTime(balls[i], balls[j],
                        acceleration[i], acceleration[j], segment));
        }
        for (std::size_t i = 0; i < balls.size(); ++i)
        {
            auto& ball = balls[i];
            ball.position += ball.velocity * segment + 0.5 * acceleration[i] * segment * segment;
            ball.velocity += acceleration[i] * segment;
            if (settings.enableRotation)
            {
                // use midpoint spin for the orientation update.
                const auto angularChange = motion.angular[i] * segment;
                ball.angularVelocity += 0.5 * angularChange;
                advanceBallRotation(ball, segment);
                ball.angularVelocity += 0.5 * angularChange;
            }
            else advanceBallRotation(ball, segment);
            if (stoppingTimes[i] <= segment) ball.velocity.x = ball.velocity.z = 0.0;
        }
        remaining = std::max(0.0, remaining - segment);
    }
}

} // namespace engine::physics
