#include "engine/game/AxolotlBellyFloat.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace engine::game
{

namespace
{
constexpr float kPi = 3.14159265358979323846f;
}

void AxolotlBellyFloat::setEnabled(bool enabled)
{
    enabled_ = enabled;
    // keep any accumulated progress; a disabled float simply eases back
    // upright on the next updates instead of snapping.
}

void AxolotlBellyFloat::update(float dt, bool focused)
{
    const float elapsed = std::max(0.0f, dt);
    activeSeconds_ = focused ? activeSeconds_ + elapsed : 0.0f;

    const bool floating = enabled_ && focused;
    const float target = floating ? 1.0f : 0.0f;
    const float rate = config_.rollSeconds > 0.0f ? elapsed / config_.rollSeconds : 1.0f;
    if (progress_ < target)
    {
        progress_ = std::min(target, progress_ + rate);
    }
    else if (progress_ > target)
    {
        progress_ = std::max(target, progress_ - rate);
    }
}

void AxolotlBellyFloat::reset()
{
    progress_ = 0.0f;
    activeSeconds_ = 0.0f;
}

float AxolotlBellyFloat::blend() const
{
    const float t = std::clamp(progress_, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float AxolotlBellyFloat::rollRadians() const
{
    return blend() * kPi;
}

std::string AxolotlBellyFloat::typedVersusBanner() const
{
    constexpr std::string_view kBanner = "MEXICO VS ENGLAND!";
    constexpr float kCharsPerSecond = 12.0f;
    constexpr float kCursorBlinksPerSecond = 6.0f;

    const float seconds = std::max(0.0f, activeSeconds_);
    const size_t typed = std::min(
        kBanner.size(), static_cast<size_t>(seconds * kCharsPerSecond));
    std::string text(kBanner.substr(0, typed));
    const bool stillTyping = typed < kBanner.size();
    const bool cursorVisible =
        static_cast<int>(std::floor(seconds * kCursorBlinksPerSecond)) % 2 == 0;
    if (stillTyping && cursorVisible)
    {
        text.push_back('|');
    }
    return text;
}

} // namespace engine::game
