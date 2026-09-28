#include "engine/physics/BallSupport.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace engine::physics {
namespace {
constexpr double contactTolerance = 1e-9;
constexpr double quietSpeed = 0.01;
constexpr double penetrationTolerance = 1e-6;

struct Contact
{
    std::size_t a, b;
    glm::dvec3 normal;
    double normalForce = 0.0;
    glm::dvec2 tangentForce{0.0};
};

std::size_t root(std::vector<std::size_t>& parents, std::size_t index)
{
    while (parents[index] != index)
    {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}
} // namespace

void settleSupportedBalls(std::vector<Ball>& balls, double gravity, double floorFriction,
                          BallBroadPhase& broadPhase, bool useBroadPhase)
{
    if (!(gravity < 0.0) || balls.size() < 2) return;
    const double horizontalLimit = std::max(1e-14, 2.0 * floorFriction * -gravity * 1e-5);
    const auto quiet = [horizontalLimit](const Ball& ball) {
        return glm::dot(ball.velocity, ball.velocity) <= quietSpeed * quietSpeed &&
               ball.velocity.x * ball.velocity.x + ball.velocity.z * ball.velocity.z <= horizontalLimit;
    };
    if (std::none_of(balls.begin(), balls.end(), quiet)) return;
    const auto count = balls.size();
    std::vector<std::size_t> parents(count);
    std::iota(parents.begin(), parents.end(), 0);
    std::vector<Contact> contacts;
    bool overflow = false;
    std::vector<bool> penetrating(count, false);
    const auto addPair = [&](std::size_t a, std::size_t b) {
        const auto offset = balls[b].position - balls[a].position;
        const double distance = glm::length(offset);
        const double radius = balls[a].radius + balls[b].radius;
        if (distance > radius + contactTolerance || radius <= 0.0) return;
        if (contacts.size() == BallBroadPhase::maxCandidatePairs)
        {
            overflow = true;
            return;
        }
        parents[root(parents, b)] = root(parents, a);
        if (radius - distance > penetrationTolerance)
            penetrating[a] = penetrating[b] = true;
        contacts.push_back({a, b, distance > 0.0 ? offset / distance : glm::dvec3{1.0, 0.0, 0.0}});
    };
    if (useBroadPhase && broadPhase.findContactCandidates(balls, contactTolerance))
    {
        for (const auto& pair : broadPhase.pairs()) addPair(pair.first, pair.second);
    }
    else
    {
        for (std::size_t a = 0; a < count && !overflow; ++a)
            for (std::size_t b = a + 1; b < count && !overflow; ++b) addPair(a, b);
    }
    if (overflow) return;
    // stop negligible floor sliding that repeated contact impulses restart.
    const double stopLimit = 2.0 * floorFriction * -gravity * 1e-8;
    if (stopLimit > 0.0)
    {
        for (const auto& contact : contacts)
            for (const auto i : {contact.a, contact.b})
            {
                auto& ball = balls[i];
                const double speedSquared = ball.velocity.x * ball.velocity.x +
                                            ball.velocity.z * ball.velocity.z;
                if (ball.velocity.y == 0.0 && speedSquared <= stopLimit &&
                    std::abs(ball.position.y - ball.radius) <= contactTolerance)
                {
                    ball.velocity.x = ball.velocity.z = 0.0;
                    ball.isResting = true;
                }
            }
    }
    std::vector<bool> eligible(count, true), grounded(count, false);
    std::vector<std::size_t> sizes(count, 0);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto group = root(parents, i);
        ++sizes[group];
        eligible[group] = eligible[group] && quiet(balls[i]) && !penetrating[i];
        grounded[group] = grounded[group] ||
            std::abs(balls[i].position.y - balls[i].radius) <= contactTolerance;
    }
    bool anyEligible = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        eligible[i] = eligible[i] && grounded[i] && sizes[i] > 1;
        anyEligible = anyEligible || eligible[i];
    }
    if (!anyEligible) return;
    std::vector<bool> verticalSupport(count, false);
    for (std::size_t i = 0; i < count; ++i)
        verticalSupport[i] = std::abs(balls[i].position.y - balls[i].radius) <= contactTolerance;
    // aligned columns have an exact upward support path from the floor.
    for (std::size_t pass = 0; pass < count; ++pass)
    {
        bool changed = false;
        for (const auto& contact : contacts)
        {
            if (std::hypot(contact.normal.x, contact.normal.z) > 1e-12) continue;
            const auto lower = contact.normal.y > 0.0 ? contact.a : contact.b;
            const auto upper = contact.normal.y > 0.0 ? contact.b : contact.a;
            if (verticalSupport[lower] && !verticalSupport[upper])
            {
                verticalSupport[upper] = true;
                changed = true;
            }
        }
        if (!changed) break;
    }
    auto verticalGroups = eligible;
    for (std::size_t i = 0; i < count; ++i)
        if (!verticalSupport[i]) verticalGroups[root(parents, i)] = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!verticalGroups[root(parents, i)]) continue;
        balls[i].velocity = {0.0, 0.0, 0.0};
        balls[i].isResting = true;
    }
    anyEligible = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        eligible[i] = eligible[i] && !verticalGroups[i];
        anyEligible = anyEligible || eligible[i];
    }
    if (!anyEligible) return;
    // a boundary contact uses the ball count as its static-body index.
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!eligible[root(parents, i)]) continue;
        const auto& ball = balls[i];
        if (std::abs(ball.position.y - ball.radius) <= contactTolerance)
            contacts.push_back({i, count, {0.0, -1.0, 0.0}});
        for (int axis : {0, 2})
        {
            const double limit = 5.0 - ball.radius;
            for (double sign : {-1.0, 1.0})
                if (sign * ball.position[axis] >= limit - contactTolerance)
                {
                    glm::dvec3 normal{0.0};
                    normal[axis] = sign;
                    contacts.push_back({i, count, normal});
                }
        }
    }
    std::vector<glm::dvec3> acceleration(count, {0.0, gravity, 0.0});
    const double tolerance = 1e-7 * std::max(1.0, std::abs(gravity));
    // nonnegative support forces can push but never pull.
    for (int pass = 0; pass < 512; ++pass)
    {
        double largestChange = 0.0;
        for (auto& contact : contacts)
        {
            if (!eligible[root(parents, contact.a)]) continue;
            const double inverseA = 1.0 / balls[contact.a].mass;
            const double inverseB = contact.b == count ? 0.0 : 1.0 / balls[contact.b].mass;
            const auto other = contact.b == count ? glm::dvec3{0.0} : acceleration[contact.b];
            const double inward = glm::dot(other - acceleration[contact.a], contact.normal);
            const double next = std::max(0.0, contact.normalForce - inward / (inverseA + inverseB));
            const double change = next - contact.normalForce;
            contact.normalForce = next;
            acceleration[contact.a] -= inverseA * change * contact.normal;
            if (contact.b != count) acceleration[contact.b] += inverseB * change * contact.normal;
            largestChange = std::max(largestChange, std::abs(change) * (inverseA + inverseB));
            if (contact.b == count && contact.normal.y == -1.0)
            {
                const glm::dvec2 horizontal{acceleration[contact.a].x, acceleration[contact.a].z};
                auto tangent = contact.tangentForce + horizontal / inverseA;
                const double limit = floorFriction * contact.normalForce;
                const double magnitude = glm::length(tangent);
                if (magnitude > limit) tangent *= limit / magnitude;
                const auto difference = tangent - contact.tangentForce;
                contact.tangentForce = tangent;
                acceleration[contact.a].x -= inverseA * difference.x;
                acceleration[contact.a].z -= inverseA * difference.y;
                largestChange = std::max(largestChange, inverseA * glm::length(difference));
            }
        }
        if (largestChange < tolerance * 0.01) break;
    }
    for (std::size_t i = 0; i < count; ++i)
        if (glm::length(acceleration[i]) > tolerance) eligible[root(parents, i)] = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!eligible[root(parents, i)]) continue;
        balls[i].velocity = {0.0, 0.0, 0.0};
        balls[i].isResting = true;
    }
}

} // namespace engine::physics
