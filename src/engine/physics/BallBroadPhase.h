#pragma once

#include <cstddef>
#include <vector>

#include "engine/physics/BallPhysics.h"

namespace engine::physics {

// include touching boundaries when comparing finite, ordered boxes.
bool areBoundsOverlapping(const Aabb& a, const Aabb& b);

struct BallPair
{
    std::size_t first;
    std::size_t second;
};

class BallBroadPhase
{
public:
    // rebuild candidates, returning false if an all-pairs fallback is needed.
    bool findCandidates(const std::vector<Ball>& balls,
                        const std::vector<glm::dvec3>& accelerations,
                        double duration, double contactTolerance = 0.0);

    // query current bounds, returning false if an all-pairs fallback is needed.
    bool findContactCandidates(const std::vector<Ball>& balls,
                               double contactTolerance = 0.0);

    // return unique pairs in the original search order.
    const std::vector<BallPair>& pairs() const { return pairs_; }
    static constexpr std::size_t maxCandidatePairs = 65536;

private:
    struct Entry { Aabb bounds; std::size_t ballIndex; };
    bool addBounds(Aabb bounds, std::size_t ballIndex, double contactTolerance);
    bool collectPairs();
    std::vector<Entry> entries_;
    std::vector<BallPair> pairs_;
};

} // namespace engine::physics
