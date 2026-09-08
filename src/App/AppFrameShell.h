#pragma once

#include <chrono>

#include <glm/mat4x4.hpp>

// values computed by app::updateFrame (the simulation half of the frame
// shell) and consumed by app::recordFrame (the render half) across the
// renderer beginFrame seam.
// shared state for the two halves of a frame.
struct FrameShellInputs
{
    std::chrono::high_resolution_clock::time_point frameStart{};
    double realNow = 0.0;
    double animationNow = 0.0;
    bool voxelVisible = true;
    bool chunkBoundsVisible = false;
    bool volumeBoundsVisible = false;
};

struct FrameCompletionState
{
    glm::mat4 viewProjUnjittered{1.0f};
    glm::mat4 viewProjJittered{1.0f};
};

inline constexpr const char* kWindowTitle = "Voxel Aquarium (Day 12)";
