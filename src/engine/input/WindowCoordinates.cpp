#include "engine/input/WindowCoordinates.h"

namespace engine::input
{

FramebufferPoint windowToFramebuffer(double x,
                                     double y,
                                     int windowWidth,
                                     int windowHeight,
                                     int framebufferWidth,
                                     int framebufferHeight) noexcept
{
    if (windowWidth <= 0 || windowHeight <= 0 ||
        framebufferWidth <= 0 || framebufferHeight <= 0)
    {
        return {static_cast<float>(x), static_cast<float>(y)};
    }

    const double scaleX = static_cast<double>(framebufferWidth) / windowWidth;
    const double scaleY = static_cast<double>(framebufferHeight) / windowHeight;
    return {static_cast<float>(x * scaleX), static_cast<float>(y * scaleY)};
}

} // namespace engine::input
