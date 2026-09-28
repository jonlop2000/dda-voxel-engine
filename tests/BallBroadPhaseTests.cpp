#include "engine/physics/BallBroadPhase.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using engine::physics::Aabb;
using engine::physics::Ball;
using engine::physics::BallBroadPhase;
using engine::physics::BallPair;
using engine::physics::areBoundsOverlapping;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void expectPairs(const BallBroadPhase& broadPhase, const std::vector<BallPair>& expected)
{
    const auto& actual = broadPhase.pairs();
    require(actual.size() == expected.size(), "Incorrect candidate count");
    for (std::size_t i = 0; i < actual.size(); ++i)
        require(actual[i].first == expected[i].first && actual[i].second == expected[i].second,
                "Incorrect candidate pair, duplicate, or ordering");
}

void testOverlapGeometry()
{
    const Aabb box{{-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0}};
    require(areBoundsOverlapping(box, box), "Identical boxes must overlap");
    for (const Aabb& other : {Aabb{{1.0, 0.0, 0.0}, {2.0, 1.0, 1.0}},
                             Aabb{{1.0, 1.0, 0.0}, {2.0, 2.0, 1.0}},
                             Aabb{{1.0, 1.0, 1.0}, {2.0, 2.0, 2.0}},
                             Aabb{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}})
        require(areBoundsOverlapping(box, other) && areBoundsOverlapping(other, box),
                "Face, edge, corner, or contained point was rejected");
    for (int axis = 0; axis < 3; ++axis)
        for (double direction : {-1.0, 1.0})
        {
            Aabb separated = box;
            separated.minimum[axis] += direction * 3.0;
            separated.maximum[axis] += direction * 3.0;
            require(!areBoundsOverlapping(box, separated) && !areBoundsOverlapping(separated, box),
                    "Separation on one axis must reject overlap");
        }
    const Ball a{{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 1.0};
    const Ball b{{1.5, 3.5, 0.0}, {0.0, 0.0, 0.0}, 1.0};
    require(areBoundsOverlapping(engine::physics::calculateBallBounds(a), engine::physics::calculateBallBounds(b)) &&
            !engine::physics::areBallsTouching(a, b), "Box overlap must remain only a possible sphere collision");
}

void testCandidateOrderAndMotion()
{
    BallBroadPhase broadPhase;
    std::vector<Ball> balls = {{{3.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1},
                               {{-2.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1},
                               {{-1.9, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1},
                               {{3.15, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1},
                               {{0.0, 5.0, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    std::vector<glm::dvec3> acceleration(balls.size(), glm::dvec3{0.0});
    require(broadPhase.findCandidates(balls, acceleration, 0.0), "Valid static query failed");
    expectPairs(broadPhase, {{0, 3}, {1, 2}});
    require(broadPhase.findCandidates(balls, acceleration, 0.0), "Repeated query failed");
    expectPairs(broadPhase, {{0, 3}, {1, 2}});

    balls = {{{-1.0, 2.0, 0.0}, {200.0, 0.0, 0.0}, 0.1},
             {{1.0, 2.0, 0.0}, {-200.0, 0.0, 0.0}, 0.1}};
    acceleration.assign(2, {0.0, -9.81, 0.0});
    require(broadPhase.findCandidates(balls, acceleration, 0.01), "Fast query failed");
    expectPairs(broadPhase, {{0, 1}});

    balls = {{{0.0, 2.0, 0.0}, {0.0, 10.0, 0.0}, 0.1},
             {{0.0, 7.15, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    acceleration = {{0.0, -10.0, 0.0}, {0.0, 0.0, 0.0}};
    require(broadPhase.findCandidates(balls, acceleration, 2.0), "Curved query failed");
    expectPairs(broadPhase, {{0, 1}});

    // paths can share space at different times; only the timing query decides an actual collision.
    balls = {{{0.0, 2.0, 0.0}, {1.0, 0.0, 0.0}, 0.1},
             {{1.0, 2.0, 0.0}, {1.0, 0.0, 0.0}, 0.1}};
    acceleration.assign(2, glm::dvec3{0.0});
    require(broadPhase.findCandidates(balls, acceleration, 1.0), "Shared motion query failed");
    expectPairs(broadPhase, {{0, 1}});
    require(!engine::physics::findBallCollisionTime(balls[0], balls[1], 1.0),
            "Shared motion should produce a candidate but no collision");

    balls[0].velocity = balls[1].velocity = glm::dvec3{0.0};
    balls[1].position.x = 0.2 + 5e-10;
    require(broadPhase.findCandidates(balls, acceleration, 0.0), "Zero tolerance query failed");
    expectPairs(broadPhase, {});
    require(broadPhase.findCandidates(balls, acceleration, 0.0, 1e-9), "Tolerance query failed");
    expectPairs(broadPhase, {{0, 1}});

    balls = {{{-1.0, 2.0, 0.0}, {200.0, 0.0, 0.0}, 0.1},
             {{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1},
             {{1.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    acceleration.assign(3, glm::dvec3{0.0});
    require(broadPhase.findCandidates(balls, acceleration, 0.01), "Initial chain query failed");
    expectPairs(broadPhase, {{0, 1}, {0, 2}});
    balls[0].position.x = -0.2;
    balls[0].velocity.x = 0.0;
    balls[1].velocity.x = 200.0;
    require(broadPhase.findCandidates(balls, acceleration, 0.006), "Updated chain query failed");
    expectPairs(broadPhase, {{0, 1}, {1, 2}});
}

void testAgainstBruteForce()
{
    std::mt19937 random{5741};
    std::uniform_real_distribution<double> coordinate(-4.0, 4.0);
    std::uniform_real_distribution<double> motion(-10.0, 10.0);
    std::uniform_real_distribution<double> radius(0.01, 0.4);
    BallBroadPhase broadPhase;
    const double infinity = std::numeric_limits<double>::infinity();
    const double tolerance = 1e-9;
    for (int scene = 0; scene < 20; ++scene)
    {
        std::vector<Ball> balls;
        std::vector<glm::dvec3> acceleration;
        std::vector<Aabb> bounds;
        for (int i = 0; i < 96; ++i)
        {
            balls.push_back({{coordinate(random), 5.0 + coordinate(random), coordinate(random)},
                             {motion(random), motion(random), motion(random)}, radius(random)});
            acceleration.push_back({motion(random), motion(random), motion(random)});
            auto swept = engine::physics::calculateSweptBallBounds(balls.back(), acceleration.back(), 0.2);
            require(swept.has_value(), "Reference bounds failed");
            for (int axis = 0; axis < 3; ++axis)
            {
                swept->minimum[axis] = std::nextafter(swept->minimum[axis] - tolerance, -infinity);
                swept->maximum[axis] = std::nextafter(swept->maximum[axis] + tolerance, infinity);
            }
            bounds.push_back(*swept);
        }
        std::vector<BallPair> expected;
        for (std::size_t i = 0; i < balls.size(); ++i)
            for (std::size_t j = i + 1; j < balls.size(); ++j)
                if (areBoundsOverlapping(bounds[i], bounds[j])) expected.push_back({i, j});
        require(broadPhase.findCandidates(balls, acceleration, 0.2, tolerance), "Seeded query failed");
        expectPairs(broadPhase, expected);
    }
}

void testFallbackAndSmallInputs()
{
    BallBroadPhase broadPhase;
    require(broadPhase.findCandidates({}, {}, 0.0), "Empty scene must succeed");
    expectPairs(broadPhase, {});
    std::vector<Ball> balls = {{{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    std::vector<glm::dvec3> acceleration(1, glm::dvec3{0.0});
    require(broadPhase.findCandidates(balls, acceleration, 0.1), "One ball must succeed");
    expectPairs(broadPhase, {});
    balls.push_back(balls[0]);
    acceleration.push_back(acceleration[0]);
    require(broadPhase.findCandidates(balls, acceleration, 0.1), "Overlapping query failed");
    expectPairs(broadPhase, {{0, 1}});
    require(!broadPhase.findCandidates(balls, {}, 0.1), "Mismatched accelerations need fallback");
    expectPairs(broadPhase, {});
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (double invalid : {-1.0, infinity, nan})
    {
        require(!broadPhase.findCandidates(balls, acceleration, invalid), "Invalid duration needs fallback");
        require(!broadPhase.findCandidates(balls, acceleration, 0.1, invalid), "Invalid tolerance needs fallback");
    }
    balls[0].velocity.x = nan;
    require(!broadPhase.findCandidates(balls, acceleration, 0.1), "Invalid bounds need fallback");
    expectPairs(broadPhase, {});
    balls[0].velocity.x = std::numeric_limits<double>::max();
    require(!broadPhase.findCandidates(balls, acceleration, 2.0), "Overflowing bounds need fallback");

    balls.assign(400, {{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1});
    acceleration.assign(balls.size(), glm::dvec3{0.0});
    require(!broadPhase.findCandidates(balls, acceleration, 0.0), "Dense pair list must use bounded-memory fallback");
    expectPairs(broadPhase, {});
    balls.resize(1);
    acceleration.resize(1);
    require(broadPhase.findCandidates(balls, acceleration, 0.1), "Query did not recover after fallback");
    expectPairs(broadPhase, {});
}

void testCurrentContactCandidates()
{
    BallBroadPhase broadPhase;
    std::vector<Ball> balls = {{{3.0, 2.0, 0.0}, {100.0, 0.0, 0.0}, 0.1},
                               {{-2.0, 2.0, 0.0}, {-100.0, 0.0, 0.0}, 0.1},
                               {{-1.9, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1},
                               {{3.15, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1}};
    require(broadPhase.findContactCandidates(balls), "Current contact query failed");
    expectPairs(broadPhase, {{0, 3}, {1, 2}});
    balls[0].position.x = 0.0;
    balls[2].position.x = 1.0;
    require(broadPhase.findContactCandidates(balls), "Current bounds did not rebuild");
    expectPairs(broadPhase, {});

    for (int axis = 0; axis < 3; ++axis)
        for (double direction : {-1.0, 1.0})
        {
            balls.assign(2, {{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1});
            balls[1].position[axis] += direction * (0.2 + 5e-10);
            require(broadPhase.findContactCandidates(balls), "Zero tolerance query failed");
            expectPairs(broadPhase, {});
            require(broadPhase.findContactCandidates(balls, 1e-9), "Contact tolerance query failed");
            expectPairs(broadPhase, {{0, 1}});
        }

    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (double invalid : {-1.0, infinity, nan})
    {
        require(!broadPhase.findContactCandidates(balls, invalid), "Invalid contact tolerance accepted");
        expectPairs(broadPhase, {});
        auto invalidBalls = balls;
        invalidBalls[0].radius = invalid;
        require(!broadPhase.findContactCandidates(invalidBalls), "Invalid radius accepted");
        expectPairs(broadPhase, {});
    }
    balls[0].position.x = nan;
    require(!broadPhase.findContactCandidates(balls), "Invalid position accepted");
    expectPairs(broadPhase, {});
    balls[0].position.x = balls[0].radius = std::numeric_limits<double>::max();
    require(!broadPhase.findContactCandidates(balls), "Overflowing current bounds accepted");
    expectPairs(broadPhase, {});
    balls.assign(400, {{0.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, 0.1});
    require(!broadPhase.findContactCandidates(balls), "Dense current bounds must trigger fallback");
    expectPairs(broadPhase, {});
    balls.resize(1);
    require(broadPhase.findContactCandidates(balls), "Single-ball query did not recover");
    expectPairs(broadPhase, {});
    require(broadPhase.findContactCandidates({}), "Empty current query failed");
    expectPairs(broadPhase, {});
}

void testCurrentContactCoverage()
{
    std::mt19937 random{9713};
    std::uniform_real_distribution<double> coordinate(-1.0, 1.0);
    std::uniform_real_distribution<double> radius(0.01, 0.3);
    BallBroadPhase broadPhase;
    const double tolerance = 1e-9;
    std::size_t contacts = 0;
    for (int scene = 0; scene < 20; ++scene)
    {
        std::vector<Ball> balls;
        for (int i = 0; i < 96; ++i)
            balls.push_back({{coordinate(random), 2.0 + coordinate(random), coordinate(random)},
                             {coordinate(random), coordinate(random), coordinate(random)}, radius(random)});
        require(broadPhase.findContactCandidates(balls, tolerance), "Seeded current query failed");
        const auto& pairs = broadPhase.pairs();
        for (std::size_t p = 0; p < pairs.size(); ++p)
        {
            require(pairs[p].first < pairs[p].second && pairs[p].second < balls.size(), "Invalid current pair");
            if (p > 0)
                require(pairs[p - 1].first < pairs[p].first ||
                        (pairs[p - 1].first == pairs[p].first && pairs[p - 1].second < pairs[p].second),
                        "Current pairs must be ordered without duplicates");
        }
        for (std::size_t i = 0; i < balls.size(); ++i)
            for (std::size_t j = i + 1; j < balls.size(); ++j)
            {
                const auto offset = balls[j].position - balls[i].position;
                const double reach = balls[i].radius + balls[j].radius + tolerance;
                const double squared = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
                if (squared > reach * reach) continue;
                ++contacts;
                require(std::any_of(pairs.begin(), pairs.end(), [&](const BallPair& pair) {
                    return pair.first == i && pair.second == j;
                }), "Current filtering missed a sphere contact");
            }
        require(pairs.size() < balls.size() * (balls.size() - 1) / 2, "Current query did not filter distant pairs");
    }
    require(contacts > 0, "Seeded coverage never exercised a contact");
}

void testLargeSparseScene()
{
    std::vector<Ball> balls;
    for (int x = 0; x < 50; ++x)
        for (int y = 0; y < 20; ++y)
            for (int z = 0; z < 10; ++z)
                balls.push_back({{-4.41 + x * 0.18, 0.5 + y * 0.18, -0.81 + z * 0.18},
                                 {0.0, 0.0, 0.0}, 0.01});
    balls[1].position = balls[0].position + glm::dvec3{0.015, 0.0, 0.0};
    const std::vector<glm::dvec3> acceleration(balls.size(), {0.0, -9.81, 0.0});
    BallBroadPhase broadPhase;
    require(broadPhase.findCandidates(balls, acceleration, 0.01, 1e-9), "Large sparse query failed");
    expectPairs(broadPhase, {{0, 1}});
    require(broadPhase.findContactCandidates(balls, 1e-9), "Large current contact query failed");
    expectPairs(broadPhase, {{0, 1}});
    std::cout << "Sparse swept and current candidate fixture: " << balls.size() << " balls, " << broadPhase.pairs().size()
              << " candidate vs " << balls.size() * (balls.size() - 1) / 2 << " possible pairs\n";
}

} // namespace

int main()
{
    try
    {
        testOverlapGeometry();
        testCandidateOrderAndMotion();
        testAgainstBruteForce();
        testFallbackAndSmallInputs();
        testCurrentContactCandidates();
        testCurrentContactCoverage();
        std::cout << "PASS current contacts: coverage, tolerance, ordering, rebuild, and fallback\n";
        testLargeSparseScene();
        std::cout << "PASS swept broad phase: overlap, order, motion, brute-force agreement, and fallback\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
