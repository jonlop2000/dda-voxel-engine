#include "engine/physics/PhysicsSandbox.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using engine::physics::Ball;
using engine::physics::BallCollisionEvent;
using engine::physics::BallSimulationSettings;
constexpr double timeStep = 0.01;
constexpr double radius = 0.015625;

struct Scene
{
    std::string name;
    std::vector<Ball> balls;
    BallSimulationSettings settings;
    double startTime = 0.0;
    std::size_t expectedImpacts = 0;
};

struct State
{
    std::vector<Ball> balls;
    std::vector<BallCollisionEvent> impacts;
};

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

Scene makeScene(const std::string& name, std::size_t count)
{
    Scene scene{name, {}, {}};
    scene.balls.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const double x = -4.0 + static_cast<double>(i % 32) * 0.25;
        const double z = -4.0 + static_cast<double>((i / 32) % 32) * 0.25;
        const double y = 1.0 + static_cast<double>(i / 1024) * 0.25;
        if (name == "free_fall")
            scene.balls.push_back({{x, y, z}, {0.25, -0.1, 0.125}, radius});
        else if (name == "resting_floor")
            scene.balls.push_back({{-4.5 + static_cast<double>(i % 100) * 0.09, radius,
                                    -4.5 + static_cast<double>(i / 100) * 0.09},
                                   {0.0, 0.0, 0.0}, radius, true});
        else if (name == "supported_stacks")
        {
            const std::size_t stack = i / 3;
            const std::size_t level = i % 3;
            const double masses[] = {4.0, 2.0, 1.0};
            scene.balls.push_back({{-4.0 + static_cast<double>(stack % 32) * 0.25,
                                    radius * static_cast<double>(1 + 2 * level),
                                    -4.0 + static_cast<double>(stack / 32) * 0.25},
                                   {0.0, 0.0, 0.0}, radius, level == 0, masses[level]});
        }
        else
        {
            const std::size_t pair = i / 2;
            const double side = i % 2 == 0 ? -1.0 : 1.0;
            const double contactTime = name == "staggered_impacts" ?
                static_cast<double>(4 + pair % 12) / 2048.0 : 1.0 / 128.0;
            const bool overlap = name == "overlap_corrections";
            const double halfSeparation = overlap ? radius * 0.5 : radius + 2.0 * contactTime;
            scene.balls.push_back({{-3.0 + static_cast<double>(pair % 25) * 0.25 + side * halfSeparation,
                                    1.0 + static_cast<double>(pair / 500) * 0.25,
                                    -3.0 + static_cast<double>((pair / 25) % 20) * 0.25},
                                   {overlap ? 0.0 : -side * 2.0, 0.0, 0.0}, radius});
        }
    }
    if (name == "simultaneous_impacts" || name == "staggered_impacts") scene.expectedImpacts = count / 2;
    return scene;
}

void verify(const Scene& scene, const State& filtered, const State& reference)
{
    require(filtered.balls.size() == reference.balls.size(), "Ball count mismatch");
    for (std::size_t i = 0; i < filtered.balls.size(); ++i)
    {
        const auto& a = filtered.balls[i];
        const auto& b = reference.balls[i];
        require(a.position == b.position && a.velocity == b.velocity && a.isResting == b.isResting &&
                a.radius == b.radius && a.mass == b.mass &&
                a.angularVelocity == b.angularVelocity && a.orientation == b.orientation,
                "Ball state mismatch at index " + std::to_string(i));
        for (int axis = 0; axis < 3; ++axis)
            require(std::isfinite(a.position[axis]) && std::isfinite(a.velocity[axis]), "Non-finite ball state");
        require(std::abs(a.position.x) <= 5.0 - a.radius + 1e-8 &&
                std::abs(a.position.z) <= 5.0 - a.radius + 1e-8 &&
                a.position.y >= a.radius - 1e-8, "Ball escaped the sandbox");
    }
    require(filtered.impacts.size() == reference.impacts.size(), "Impact count mismatch");
    require(filtered.impacts.size() == scene.expectedImpacts, "Unexpected fixture impact count");
    for (std::size_t i = 0; i < filtered.impacts.size(); ++i)
    {
        const auto& a = filtered.impacts[i];
        const auto& b = reference.impacts[i];
        require(a.ballA == b.ballA && a.ballB == b.ballB && a.simulationTime == b.simulationTime &&
                a.momentumBefore == b.momentumBefore && a.momentumAfter == b.momentumAfter &&
                a.kineticEnergyBefore == b.kineticEnergyBefore && a.kineticEnergyAfter == b.kineticEnergyAfter &&
                a.restitution == b.restitution, "Collision history mismatch");
    }
}

double measure(const Scene& scene, bool filtered, int iterations, State& state)
{
    auto settings = scene.settings;
    settings.useBroadPhase = filtered;
    double milliseconds = 0.0;
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        // reset outside the timer to replay the same complete physics step.
        state.balls = scene.balls;
        const auto begin = std::chrono::steady_clock::now();
        engine::physics::advanceBallSystem(state.balls, timeStep, scene.startTime, settings, state.impacts);
        const auto end = std::chrono::steady_clock::now();
        milliseconds += std::chrono::duration<double, std::milli>(end - begin).count();
    }
    return milliseconds / iterations;
}

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 == 0 ? (values[middle - 1] + values[middle]) * 0.5 : values[middle];
}

void benchmark(const Scene& scene, int samples, std::ostream* csv)
{
    const auto count = scene.balls.size();
    const int iterations = count <= 100 ? 32 : (count <= 1000 ? 2 : 1);
    State filtered, reference;
    std::cout << "Running " << scene.name << " with " << count << " balls..." << std::endl;
    measure(scene, true, 1, filtered);
    measure(scene, false, 1, reference);
    verify(scene, filtered, reference);
    std::vector<double> filteredTimes, referenceTimes;
    for (int sample = 0; sample < samples; ++sample)
    {
        double filteredMs, referenceMs;
        // alternate order to reduce consistent first-run and thermal bias.
        if (sample % 2 == 0)
        {
            filteredMs = measure(scene, true, iterations, filtered);
            referenceMs = measure(scene, false, iterations, reference);
        }
        else
        {
            referenceMs = measure(scene, false, iterations, reference);
            filteredMs = measure(scene, true, iterations, filtered);
        }
        verify(scene, filtered, reference);
        filteredTimes.push_back(filteredMs);
        referenceTimes.push_back(referenceMs);
        if (csv)
        {
            *csv << scene.name << ',' << count << ",filtered," << sample << ',' << iterations << ','
                 << std::setprecision(12) << filteredMs << ',' << filtered.impacts.size() << '\n';
            *csv << scene.name << ',' << count << ",all_pairs," << sample << ',' << iterations << ','
                 << referenceMs << ',' << reference.impacts.size() << '\n';
            csv->flush();
            require(static_cast<bool>(*csv), "Failed to write benchmark CSV");
        }
    }
    const double filteredMedian = median(filteredTimes);
    const double referenceMedian = median(referenceTimes);
    std::cout << std::fixed << std::setprecision(6)
              << "RESULT " << scene.name << " balls=" << count << " impacts=" << filtered.impacts.size()
              << " filtered_ms=" << filteredMedian << " all_pairs_ms=" << referenceMedian
              << " speedup=" << referenceMedian / filteredMedian
              << " filtered_min_ms=" << *std::min_element(filteredTimes.begin(), filteredTimes.end())
              << " filtered_max_ms=" << *std::max_element(filteredTimes.begin(), filteredTimes.end())
              << " exact_match=yes\n" << std::flush;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        int samples = 7;
        int maxBalls = 10000;
        std::string scenario;
        std::string csvPath;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--help")
            {
                std::cout << "ball_physics_benchmark [--samples 7] [--max-balls 10000] "
                             "[--scenario NAME] [--csv PATH]\n";
                return 0;
            }
            require(i + 1 < argc, "Missing option value");
            const std::string value = argv[++i];
            if (arg == "--samples" || arg == "--max-balls")
            {
                std::size_t used = 0;
                const int number = std::stoi(value, &used);
                require(used == value.size(), "Invalid numeric option");
                if (arg == "--samples") samples = number;
                else maxBalls = number;
            }
            else if (arg == "--scenario") scenario = value;
            else if (arg == "--csv") csvPath = value;
            else throw std::runtime_error("Unknown option: " + arg);
        }
        require(samples >= 1 && samples <= 101, "Samples must be between 1 and 101");
        require(maxBalls >= 3 && maxBalls <= 10000, "Max balls must be between 3 and 10000");
        std::vector<Scene> scenes;
        engine::physics::PhysicsSandbox sandbox;
        scenes.push_back({"default_free_fall", sandbox.balls(), {}});
        for (int step = 0; step < 41; ++step) sandbox.update(timeStep);
        scenes.push_back({"default_first_impact", sandbox.balls(), {}, sandbox.elapsedTime(), 1});
        for (const std::string name : {"free_fall", "simultaneous_impacts", "staggered_impacts",
                                       "overlap_corrections", "resting_floor"})
            for (int count : {100, 1000, 5000, 10000})
                if (count <= maxBalls) scenes.push_back(makeScene(name, static_cast<std::size_t>(count)));
        for (int count : {3, 99, 999})
            if (count <= maxBalls) scenes.push_back(makeScene("supported_stacks", static_cast<std::size_t>(count)));
        scenes.erase(std::remove_if(scenes.begin(), scenes.end(), [&](const Scene& scene) {
            return !scenario.empty() && scene.name != scenario;
        }), scenes.end());
        require(!scenes.empty(), "No scenes match the requested options");
        std::ofstream csv;
        if (!csvPath.empty())
        {
            csv.open(csvPath);
            require(csv.is_open(), "Cannot open CSV output: " + csvPath);
            csv << "scenario,balls,mode,sample,iterations,ms_per_step,impacts\n";
        }
        std::cout << "Complete 0.01 s physics steps; optimized benchmark target; no rendering.\n"
                  << "Samples per mode: " << samples << "; one warmup per mode; reset and validation untimed.\n";
        for (const auto& scene : scenes) benchmark(scene, samples, csv.is_open() ? &csv : nullptr);
        std::cout << "All measured fixtures matched the all-pairs path exactly.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
