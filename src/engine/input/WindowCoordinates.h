#pragma once

namespace engine::input
{

struct FramebufferPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

// glfw cursor positions use window coordinates while rendering and runtime-UI
// layout use framebuffer pixels. the two spaces differ on high-DPI displays.
[[nodiscard]] FramebufferPoint windowToFramebuffer(
    double x,
    double y,
    int windowWidth,
    int windowHeight,
    int framebufferWidth,
    int framebufferHeight) noexcept;

} // namespace engine::input
