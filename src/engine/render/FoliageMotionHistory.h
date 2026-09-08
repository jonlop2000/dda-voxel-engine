#pragma once

#include <algorithm>
#include <cmath>

namespace engine::render
{

// owns the deformation state paired with the renderer's previous camera matrices.
// a consumer-specific history reset must not alter produced motion vectors.
class FoliageMotionHistory
{
public:
    void reset()
    {
        valid_ = false;
        prepared_ = false;
        committedTimeSeconds_ = 0.0f;
        committedSwayStrength_ = 0.0f;
        currentTimeSeconds_ = 0.0f;
        previousTimeSeconds_ = 0.0f;
        currentSwayStrength_ = 0.0f;
        previousSwayStrength_ = 0.0f;
    }

    void beginFrame(float timeSeconds, float swayStrength)
    {
        currentTimeSeconds_ =
            std::isfinite(timeSeconds) ? timeSeconds : committedTimeSeconds_;
        currentSwayStrength_ = std::isfinite(swayStrength)
                                   ? std::max(swayStrength, 0.0f)
                                   : committedSwayStrength_;
        previousTimeSeconds_ = valid_ ? committedTimeSeconds_ : currentTimeSeconds_;
        previousSwayStrength_ =
            valid_ ? committedSwayStrength_ : currentSwayStrength_;
        prepared_ = true;
    }

    void completeFrame()
    {
        if (!prepared_)
        {
            return;
        }
        committedTimeSeconds_ = currentTimeSeconds_;
        committedSwayStrength_ = currentSwayStrength_;
        valid_ = true;
        prepared_ = false;
    }

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] float currentTimeSeconds() const { return currentTimeSeconds_; }
    [[nodiscard]] float previousTimeSeconds() const { return previousTimeSeconds_; }
    [[nodiscard]] float currentSwayStrength() const { return currentSwayStrength_; }
    [[nodiscard]] float previousSwayStrength() const { return previousSwayStrength_; }

private:
    bool valid_ = false;
    bool prepared_ = false;
    float committedTimeSeconds_ = 0.0f;
    float committedSwayStrength_ = 0.0f;
    float currentTimeSeconds_ = 0.0f;
    float previousTimeSeconds_ = 0.0f;
    float currentSwayStrength_ = 0.0f;
    float previousSwayStrength_ = 0.0f;
};

} // namespace engine::render
