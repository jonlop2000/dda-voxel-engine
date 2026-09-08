#include "engine/game/FishCelebration.h"

#include <algorithm>
#include <cmath>

namespace engine::game
{

namespace
{
constexpr float kTwoPi = 6.28318530717958648f;
}

void FishCelebration::setEnabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled_)
    {
        // stop any in-progress celebration, but keep the accumulated path offset so
        // the fish do not jump; it is just a harmless phase shift.
        timer_ = 0.0f;
    }
}

void FishCelebration::trigger()
{
    if (!enabled_)
    {
        return;
    }
    timer_ = config_.durationSeconds;
}

void FishCelebration::update(float dt)
{
    if (timer_ <= 0.0f)
    {
        return;
    }
    const float elapsed = std::max(0.0f, dt);
    const float step = std::min(elapsed, timer_);
    pathTimeOffset_ += step;  // freeze the path clock so every fish holds position
    timer_ = std::max(0.0f, timer_ - elapsed);
}

void FishCelebration::reset()
{
    timer_ = 0.0f;
    pathTimeOffset_ = 0.0f;
}

float FishCelebration::blend() const
{
    if (timer_ <= 0.0f)
    {
        return 0.0f;
    }
    const float turn = std::max(0.0001f, config_.turnSeconds);
    const float elapsed = config_.durationSeconds - timer_;
    const float rampIn = std::clamp(elapsed / turn, 0.0f, 1.0f);
    const float rampOut = std::clamp(timer_ / turn, 0.0f, 1.0f);
    return std::min(rampIn, rampOut);
}

FishCelebration::Facing FishCelebration::faceCamera(float swimYaw, float swimPitch,
                                                    const glm::vec3& fishPos,
                                                    const glm::vec3& cameraPos) const
{
    const float b = blend();
    if (b <= 0.0f)
    {
        return {swimYaw, swimPitch};
    }

    const glm::vec3 toCam = cameraPos - fishPos;
    const glm::vec3 toCamHoriz(toCam.x, 0.0f, toCam.z);
    const float horizDist = std::max(0.001f, glm::length(toCamHoriz));
    const glm::vec3 camDir = toCamHoriz / horizDist;
    const float camYaw = std::atan2(-camDir.z, camDir.x);
    const float camPitch = std::clamp(toCam.y / horizDist, -0.45f, 0.45f);

    Facing facing{};
    // shortest-path angular blend so the fish never spins the long way.
    facing.yaw = swimYaw + std::remainder(camYaw - swimYaw, kTwoPi) * b;
    facing.pitch = swimPitch + (camPitch - swimPitch) * b;
    return facing;
}

float FishCelebration::bodyWiggle(float realTime, float phase) const
{
    const float b = blend();
    if (b <= 0.0f)
    {
        return 0.0f;
    }
    return config_.wiggleAmplitude *
           std::sin(realTime * config_.wiggleFrequency + phase) * b;
}

} // namespace engine::game
