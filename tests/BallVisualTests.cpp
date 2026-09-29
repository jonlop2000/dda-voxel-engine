#include "engine/physics/PhysicsSandbox.h"
#include "engine/scene/PhysicsSandboxVisuals.h"
#include "engine/voxel/VoxelBuilder.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {
using namespace engine::physics;
const std::array<glm::dvec3, 3> axes{{{1.0, 0.0, 0.0},
                                    {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void near(const glm::dvec3& actual, const glm::dvec3& expected, double tolerance,
          const char* message)
{
    require(glm::length(actual - expected) < tolerance, message);
}

void checkCenterPivot()
{
    const std::array<glm::dvec3, 4> rotationAxes{{axes[0], axes[1], axes[2],
                                               glm::normalize(glm::dvec3{1.0, 2.0, 3.0})}};
    for (const double radius : {0.1, 0.25, 0.6})
        for (const auto& axis : rotationAxes)
            for (int step = 0; step <= 32; ++step)
            {
                Ball ball{{1.2, 2.5, -3.0}, {0.0, 0.0, 0.0}, radius};
                ball.orientation = glm::angleAxis(step * glm::two_pi<double>() / 32.0, axis);
                const auto transform = makeBallVisualTransform(ball);
                // the renderer normalizes the supplied quaternion once more.
                const auto rotation = transform.rotation /
                    std::sqrt(glm::dot(transform.rotation, transform.rotation));
                const glm::mat4 matrix = glm::translate(glm::mat4{1.0f}, transform.position) *
                    glm::mat4_cast(rotation) * glm::scale(glm::mat4{1.0f}, transform.scale);
                const glm::vec3 localCenter{ballVoxelSize * 0.5f};
                near(glm::vec3(matrix * glm::vec4(localCenter, 1.0f)), ball.position, 2e-6,
                     "Rotating the voxel volume moved the ball center");
                for (const auto& direction : axes)
                {
                    const auto localSurface = localCenter + glm::vec3(direction) * ballVoxelRadius;
                    const auto surface = glm::vec3(matrix * glm::vec4(localSurface, 1.0f));
                    const auto expected = ball.position + ball.orientation * (radius * direction);
                    near(surface, expected, 2e-6, "Render rotation or radius disagrees with physics");
                    near(glm::vec3(glm::inverse(matrix) * glm::vec4(surface, 1.0f)),
                         localSurface, 1e-4, "DDA local transform did not invert the world transform");
                }
                const auto repeated = makeBallVisualTransform(ball);
                require(transform.position == repeated.position && transform.rotation == repeated.rotation &&
                        transform.scale == repeated.scale, "Unchanged pose produced a different transform");
                const auto noise = rotation - transform.rotation;
                require(glm::dot(noise, noise) <= 1e-12f,
                        "Normalization noise would keep paused transforms dirty");
            }
}

void checkMarkers()
{
    const auto data = buildMarkedBallVoxels(12);
    engine::VoxelBuilder original(glm::ivec3{ballVoxelSize});
    original.fillSphere(glm::vec3{ballVoxelSize * 0.5f}, ballVoxelRadius, 12);
    require(data.size() == original.data().size(), "Marked sphere dimensions changed");
    std::array<int, 3> materials{}, changed{};
    const auto at = [&](int x, int y, int z) {
        return data[static_cast<std::size_t>(x + ballVoxelSize * (y + ballVoxelSize * z))];
    };
    for (int z = 0; z < ballVoxelSize; ++z)
        for (int y = 0; y < ballVoxelSize; ++y)
            for (int x = 0; x < ballVoxelSize; ++x)
            {
                const auto material = at(x, y, z);
                require((material == 0) == (original.getVoxel(x, y, z) == 0),
                        "Marker changed sphere occupancy or its padding");
                if (material == 12) ++materials[0];
                else if (material == ballStripeMaterial) ++materials[1];
                else if (material == ballMarkerMaterial) ++materials[2];
                else require(material == 0, "Unexpected marker material");
                const int end = ballVoxelSize - 1;
                changed[0] += material != at(x, end - z, y);
                changed[1] += material != at(z, y, end - x);
                changed[2] += material != at(end - y, x, z);
            }
    for (const int count : materials) require(count > 0, "A marker color is missing");
    for (const int count : changed)
        require(count > 100, "Marker is invariant under rotation about an axis");
}

void samePose(const PhysicsSandbox& a, const PhysicsSandbox& b)
{
    require(a.elapsedTime() == b.elapsedTime() && a.balls().size() == b.balls().size(),
            "Sandbox clocks or ball counts differ");
    for (std::size_t i = 0; i < a.balls().size(); ++i)
    {
        const auto& first = a.balls()[i];
        const auto& second = b.balls()[i];
        require(first.position == second.position && first.velocity == second.velocity &&
                first.orientation == second.orientation && first.angularVelocity == second.angularVelocity &&
                first.radius == second.radius && first.isResting == second.isResting,
                "Sandbox poses differ");
    }
}

void checkSpinPreset()
{
    PhysicsSandbox manual;
    manual.setRestitution(0.3);
    manual.setFloorFriction(0.7);
    manual.setPlaybackSpeed(0.5);
    manual.loadPreset(PhysicsSandbox::Preset::FreeSpin);
    require(manual.isPaused() && !manual.isStressPreset() && manual.hasMotion(),
            "Spin preset should pause with active rotational motion");
    require(manual.balls().size() == 3 && manual.elapsedTime() == 0.0,
            "Incorrect initial spin scene");
    const auto initial = manual;
    manual.update(10.0);
    samePose(manual, initial);
    auto automatic = manual;
    automatic.setPaused(false);
    for (int step = 0; step < 400; ++step)
    {
        manual.step();
        automatic.update(0.02);
        samePose(manual, automatic);
        for (std::size_t i = 0; i < axes.size(); ++i)
        {
            const auto& ball = manual.balls()[i];
            require(ball.position == initial.balls()[i].position && ball.velocity == glm::dvec3{0.0} &&
                    ball.isResting && ball.radius == 0.25, "Free-spin center moved");
            near(ball.angularVelocity, glm::half_pi<double>() * axes[i], 1e-12,
                 "Incorrect spin axis or speed");
            if (step == 99)
            {
                const auto direction = axes[(i + 1) % axes.size()];
                near(ball.orientation * direction, glm::cross(axes[i], direction), 1e-11,
                     "Expected a quarter turn after one second");
            }
            if (step == 399)
                for (const auto& direction : axes)
                    near(ball.orientation * direction, direction, 1e-11,
                         "Expected a full turn after four seconds");
        }
        require(manual.hasMotion(), "Spinning balls were classified as idle");
    }
    require(manual.recentCollisions().empty(), "Stationary centers produced a collision");
    manual.reset();
    samePose(manual, initial);
    require(manual.isPaused() && manual.restitution() == 0.3 && manual.floorFriction() == 0.7 &&
            manual.playbackSpeed() == 0.5, "Reset changed playback or physics settings");
    manual.loadPreset(PhysicsSandbox::Preset::Default);
    for (const auto& ball : manual.balls())
        require(ball.angularVelocity == glm::dvec3{0.0} && ball.radius == 0.1 &&
                ball.orientation == glm::dquat{1.0, 0.0, 0.0, 0.0},
                "Switching presets retained spin, radius, or orientation");

    manual.loadPreset(PhysicsSandbox::Preset::StressStacks);
    manual.setStressBallCount(2);
    for (int step = 0; step < 1000 && manual.hasMotion(); ++step) manual.step();
    require(!manual.hasMotion(), "Settled balls without spin should allow idle throttling");
}
} // namespace

int main()
{
    try
    {
        checkCenterPivot();
        checkMarkers();
        checkSpinPreset();
        std::cout << "Ball visual transforms, markers, and spin preset tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
