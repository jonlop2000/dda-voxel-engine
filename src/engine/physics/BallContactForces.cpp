#include "engine/physics/BallContactForces.h"
#include "engine/physics/BallContact.h"

#include <algorithm>
#include <cmath>

namespace engine::physics {
namespace {

constexpr double contactTolerance = 1e-9;
constexpr double slipTolerance = 1e-9;
constexpr double normalSpeedTolerance = 1e-7;
constexpr double impactSpeedThreshold = 0.1;
constexpr double contactTimeStep = 0.005;

struct Contact
{
    std::size_t a, b;
    glm::dvec3 normal, offsetA, offsetB, slip, relativeSpin;
    double inverseMassA, inverseMassB, inverseInertiaA, inverseInertiaB;
    double inverseNormalMass, inverseTangentMass, friction, resistance;
    double normalForce = 0.0;
    glm::dvec3 tangentForce{0.0};
    glm::dvec3 resistanceTorque{0.0};
};

glm::dvec3 limited(const glm::dvec3& value, double magnitude)
{
    const double length = glm::length(value);
    return length > magnitude && length > 0.0 ? value * (magnitude / length) : value;
}

glm::dvec3 relativeAcceleration(const Contact& contact,
                                const ContactAccelerations& result)
{
    const auto first = result.linear[contact.a] +
        glm::cross(result.angular[contact.a], contact.offsetA);
    if (contact.b == result.linear.size()) return -first;
    return result.linear[contact.b] +
        glm::cross(result.angular[contact.b], contact.offsetB) - first;
}

double applyForce(const Contact& contact, const glm::dvec3& force,
                  ContactAccelerations& result)
{
    result.linear[contact.a] -= contact.inverseMassA * force;
    result.angular[contact.a] -= contact.inverseInertiaA *
        glm::cross(contact.offsetA, force);
    if (contact.b != result.linear.size())
    {
        result.linear[contact.b] += contact.inverseMassB * force;
        result.angular[contact.b] += contact.inverseInertiaB *
            glm::cross(contact.offsetB, force);
    }
    return glm::length(force) * contact.inverseTangentMass;
}

double applyTorque(const Contact& contact, const glm::dvec3& torque,
                   ContactAccelerations& result)
{
    result.angular[contact.a] -= contact.inverseInertiaA * torque;
    if (contact.b != result.linear.size())
        result.angular[contact.b] += contact.inverseInertiaB * torque;
    return glm::length(torque) * (contact.inverseInertiaA +
        contact.inverseInertiaB) * std::max(glm::length(contact.offsetA),
                                          glm::length(contact.offsetB));
}

bool hasExactVerticalSupport(const std::vector<Ball>& balls,
                             const std::vector<Contact>& contacts)
{
    constexpr double penetrationTolerance = 1e-6;
    for (const auto& ball : balls)
    {
        if (ball.velocity != glm::dvec3{0.0} ||
            ball.angularVelocity != glm::dvec3{0.0} ||
            !std::isfinite(ball.radius) || ball.radius < 0.0 ||
            !std::isfinite(ball.mass) || ball.mass <= 0.0) return false;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(ball.position[axis])) return false;
        const double wallLimit = 5.0 - ball.radius + penetrationTolerance;
        if (ball.position.y < ball.radius - penetrationTolerance ||
            std::abs(ball.position.x) > wallLimit ||
            std::abs(ball.position.z) > wallLimit) return false;
    }
    const auto none = contacts.size();
    std::vector<std::size_t> heads(balls.size(), none), next(contacts.size(), none);
    std::vector<bool> supported(balls.size(), false);
    std::vector<std::size_t> queue;
    queue.reserve(balls.size());
    for (std::size_t i = 0; i < contacts.size(); ++i)
    {
        const auto& contact = contacts[i];
        if (contact.b != balls.size() && balls[contact.a].radius +
            balls[contact.b].radius - glm::length(balls[contact.b].position -
            balls[contact.a].position) > penetrationTolerance) return false;
        if (contact.normal.x != 0.0 || contact.normal.z != 0.0) continue;
        if (contact.b == balls.size())
        {
            if (contact.normal.y >= 0.0 || supported[contact.a]) continue;
            supported[contact.a] = true;
            queue.push_back(contact.a);
            continue;
        }
        const auto lower = contact.normal.y > 0.0 ? contact.a : contact.b;
        next[i] = heads[lower];
        heads[lower] = i;
    }
    // central forces on grounded vertical paths balance gravity without torque.
    for (std::size_t i = 0; i < queue.size(); ++i)
        for (auto link = heads[queue[i]]; link != none; link = next[link])
        {
            const auto& contact = contacts[link];
            const auto upper = contact.normal.y > 0.0 ? contact.b : contact.a;
            if (supported[upper]) continue;
            supported[upper] = true;
            queue.push_back(upper);
        }
    return queue.size() == balls.size();
}

void limitStoppingTime(const glm::dvec3& velocity,
                       const glm::dvec3& acceleration, double& horizon)
{
    const double speedSquared = glm::dot(velocity, velocity);
    if (speedSquared <= slipTolerance * slipTolerance) return;
    const double opposing = -glm::dot(velocity, acceleration);
    if (opposing <= 0.0) return;
    // stop before the acceleration reverses the current slip direction.
    const double time = speedSquared / opposing;
    if (time > 0.0 && std::isfinite(time)) horizon = std::min(horizon, time);
}

} // namespace

ContactAccelerations calculateContactAccelerations(
    const std::vector<Ball>& balls, const BallSimulationSettings& settings,
    BallBroadPhase& broadPhase, double maxDuration)
{
    ContactAccelerations result;
    result.linear.assign(balls.size(), {0.0, settings.acceleration, 0.0});
    result.angular.assign(balls.size(), glm::dvec3{0.0});
    result.horizon = maxDuration;
    std::vector<Contact> contacts;
    contacts.reserve(balls.size() * 2);
    const auto addContact = [&](std::size_t a, std::size_t b,
                                const glm::dvec3& normal, double friction) {
        const auto& first = balls[a];
        const bool boundary = b == balls.size();
        const auto offsetA = first.radius * normal;
        const auto offsetB = boundary ? glm::dvec3{0.0} : -balls[b].radius * normal;
        const auto secondVelocity = boundary ? glm::dvec3{0.0} :
            contactPointVelocity(balls[b], offsetB);
        const auto velocity = secondVelocity - contactPointVelocity(first, offsetA);
        const double normalSpeed = glm::dot(velocity, normal);
        if (normalSpeed > normalSpeedTolerance ||
            normalSpeed < -impactSpeedThreshold) return;
        const double inverseA = 1.0 / first.mass;
        const double inverseB = boundary ? 0.0 : 1.0 / balls[b].mass;
        const double inertiaA = ballInverseInertia(first);
        const double inertiaB = boundary ? 0.0 : ballInverseInertia(balls[b]);
        const double inverseNormal = inverseA + inverseB;
        const double inverseTangent = inverseNormal +
            glm::dot(offsetA, offsetA) * inertiaA +
            glm::dot(offsetB, offsetB) * inertiaB;
        const double radius = boundary ? first.radius :
            std::min(first.radius, balls[b].radius);
        const auto secondSpin = boundary ? glm::dvec3{0.0} : balls[b].angularVelocity;
        contacts.push_back({a, b, normal, offsetA, offsetB,
            velocity - normalSpeed * normal, secondSpin - first.angularVelocity,
            inverseA, inverseB, inertiaA, inertiaB, inverseNormal, inverseTangent,
            std::max(0.0, friction), std::max(0.0, settings.rollingResistance) * radius});
    };
    for (std::size_t i = 0; i < balls.size(); ++i)
    {
        const auto& ball = balls[i];
        if (ball.position.y <= ball.radius + contactTolerance)
            addContact(i, balls.size(), {0.0, -1.0, 0.0}, settings.floorFriction);
        const double limit = 5.0 - ball.radius;
        for (int axis : {0, 2})
            for (double sign : {-1.0, 1.0})
                if (sign * ball.position[axis] >= limit - contactTolerance)
                {
                    glm::dvec3 normal{0.0};
                    normal[axis] = sign;
                    addContact(i, balls.size(), normal, settings.wallFriction);
                }
    }
    const auto addPair = [&](std::size_t a, std::size_t b) {
        const auto offset = balls[b].position - balls[a].position;
        const double radius = balls[a].radius + balls[b].radius;
        const double distance = glm::length(offset);
        if (radius <= 0.0 || distance > radius + contactTolerance) return;
        const auto normal = distance > 0.0 ? offset / distance : glm::dvec3{1.0, 0.0, 0.0};
        addContact(a, b, normal, settings.ballFriction);
    };
    if (settings.useBroadPhase && broadPhase.findContactCandidates(balls, contactTolerance))
    {
        for (const auto& pair : broadPhase.pairs()) addPair(pair.first, pair.second);
    }
    else
    {
        for (std::size_t a = 0; a < balls.size(); ++a)
            for (std::size_t b = a + 1; b < balls.size(); ++b) addPair(a, b);
    }
    if (settings.acceleration < 0.0 && hasExactVerticalSupport(balls, contacts))
    {
        std::fill(result.linear.begin(), result.linear.end(), glm::dvec3{0.0});
        return result;
    }
    const double forceTolerance = 1e-10 * std::max(1.0, std::abs(settings.acceleration));
    // accumulated forces allow later passes to undo earlier corrections.
    for (int pass = 0; pass < 256; ++pass)
    {
        double largestChange = 0.0;
        for (auto& contact : contacts)
        {
            const double inward = glm::dot(relativeAcceleration(contact, result),
                                           contact.normal);
            const double normal = std::max(0.0,
                contact.normalForce - inward / contact.inverseNormalMass);
            largestChange = std::max(largestChange,
                applyForce(contact, (normal - contact.normalForce) * contact.normal, result));
            contact.normalForce = normal;
            const auto acceleration = relativeAcceleration(contact, result);
            const auto tangentAcceleration = acceleration -
                glm::dot(acceleration, contact.normal) * contact.normal;
            const double slipSpeed = glm::length(contact.slip);
            const double frictionLimit = contact.friction * normal;
            const auto tangent = slipSpeed > slipTolerance ?
                -frictionLimit * (contact.slip / slipSpeed) :
                limited(contact.tangentForce - tangentAcceleration /
                        contact.inverseTangentMass, frictionLimit);
            largestChange = std::max(largestChange,
                applyForce(contact, tangent - contact.tangentForce, result));
            contact.tangentForce = tangent;
            const double inverseAngularMass = contact.inverseInertiaA + contact.inverseInertiaB;
            if (inverseAngularMass <= 0.0) continue;
            const auto other = contact.b == balls.size() ? glm::dvec3{0.0} :
                result.angular[contact.b];
            const auto angularAcceleration = other - result.angular[contact.a];
            const double spinSpeed = glm::length(contact.relativeSpin);
            const double torqueLimit = contact.resistance * normal;
            const auto torque = spinSpeed > slipTolerance ?
                -torqueLimit * (contact.relativeSpin / spinSpeed) :
                limited(contact.resistanceTorque - angularAcceleration /
                        inverseAngularMass, torqueLimit);
            largestChange = std::max(largestChange,
                applyTorque(contact, torque - contact.resistanceTorque, result));
            contact.resistanceTorque = torque;
        }
        if (largestChange < forceTolerance) break;
    }
    for (const auto& contact : contacts)
    {
        const auto acceleration = relativeAcceleration(contact, result);
        const auto tangentAcceleration = acceleration -
            glm::dot(acceleration, contact.normal) * contact.normal;
        limitStoppingTime(contact.slip, tangentAcceleration, result.horizon);
        if (contact.resistance > 0.0)
        {
            const auto other = contact.b == balls.size() ? glm::dvec3{0.0} :
                result.angular[contact.b];
            limitStoppingTime(contact.relativeSpin,
                              other - result.angular[contact.a], result.horizon);
        }
        const auto relativeVelocity = contact.b == balls.size() ?
            -balls[contact.a].velocity : balls[contact.b].velocity - balls[contact.a].velocity;
        if (glm::length(relativeVelocity) > normalSpeedTolerance ||
            glm::length(result.angular[contact.a]) > slipTolerance ||
            (contact.b != balls.size() && glm::length(result.angular[contact.b]) > slipTolerance))
            result.horizon = std::min(result.horizon, contactTimeStep);
        if (contact.b != balls.size())
        {
            const double speed = glm::length(relativeVelocity);
            if (speed > normalSpeedTolerance)
                result.horizon = std::min(result.horizon,
                    0.05 * (balls[contact.a].radius + balls[contact.b].radius) / speed);
        }
    }
    return result;
}

} // namespace engine::physics
