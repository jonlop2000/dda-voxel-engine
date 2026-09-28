#include "engine/physics/BallBroadPhase.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::physics {

bool areBoundsOverlapping(const Aabb& a, const Aabb& b)
{
    return a.minimum.x <= b.maximum.x && a.maximum.x >= b.minimum.x &&
           a.minimum.y <= b.maximum.y && a.maximum.y >= b.minimum.y &&
           a.minimum.z <= b.maximum.z && a.maximum.z >= b.minimum.z;
}

bool BallBroadPhase::findCandidates(const std::vector<Ball>& balls,
                                    const std::vector<glm::dvec3>& accelerations,
                                    double duration, double contactTolerance)
{
    entries_.clear();
    pairs_.clear();
    if (balls.size() != accelerations.size() || !std::isfinite(duration) || duration < 0.0 ||
        !std::isfinite(contactTolerance) || contactTolerance < 0.0) return false;

    entries_.reserve(balls.size());
    for (std::size_t i = 0; i < balls.size(); ++i)
    {
        const auto bounds = calculateSweptBallBounds(balls[i], accelerations[i], duration);
        if (!bounds || !addBounds(*bounds, i, contactTolerance)) return false;
    }
    return collectPairs();
}

bool BallBroadPhase::findContactCandidates(const std::vector<Ball>& balls,
                                           double contactTolerance)
{
    entries_.clear();
    pairs_.clear();
    if (!std::isfinite(contactTolerance) || contactTolerance < 0.0) return false;
    entries_.reserve(balls.size());
    for (std::size_t i = 0; i < balls.size(); ++i)
    {
        const auto& ball = balls[i];
        if (!std::isfinite(ball.radius) || ball.radius < 0.0) return false;
        if (!addBounds(calculateBallBounds(ball), i, contactTolerance)) return false;
    }
    return collectPairs();
}

bool BallBroadPhase::addBounds(Aabb bounds, std::size_t ballIndex, double contactTolerance)
{
    const double infinity = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis)
    {
        // pad outward to keep gaps accepted by the contact solver.
        bounds.minimum[axis] = std::nextafter(bounds.minimum[axis] - contactTolerance, -infinity);
        bounds.maximum[axis] = std::nextafter(bounds.maximum[axis] + contactTolerance, infinity);
        if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]))
            return false;
    }
    entries_.push_back({bounds, ballIndex});
    return true;
}

bool BallBroadPhase::collectPairs()
{
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.bounds.minimum.x != b.bounds.minimum.x) return a.bounds.minimum.x < b.bounds.minimum.x;
        return a.ballIndex < b.ballIndex;
    });
    for (std::size_t i = 0; i < entries_.size(); ++i)
    {
        const auto& a = entries_[i];
        for (std::size_t j = i + 1; j < entries_.size(); ++j)
        {
            const auto& b = entries_[j];
            // stop when later x minima exceed this box's x maximum.
            if (b.bounds.minimum.x > a.bounds.maximum.x) break;
            if (!areBoundsOverlapping(a.bounds, b.bounds)) continue;
            // fall back before the candidate list exceeds its storage limit.
            if (pairs_.size() == maxCandidatePairs)
            {
                pairs_.clear();
                return false;
            }
            pairs_.push_back({std::min(a.ballIndex, b.ballIndex), std::max(a.ballIndex, b.ballIndex)});
        }
    }
    std::sort(pairs_.begin(), pairs_.end(), [](const BallPair& a, const BallPair& b) {
        return a.first < b.first || (a.first == b.first && a.second < b.second);
    });
    return true;
}

} // namespace engine::physics
