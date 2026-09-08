#pragma once

#include <glm/glm.hpp>

namespace engine::game
{

// self-contained "viva mexico" fish celebration.
//
// when triggered (e.g. on fish focus), every fish briefly freezes in place, turns
// to face the camera, and does a tiny wiggle, then resumes its swim seamlessly.
// this owns its own state and tuning and is pure logic (no engine deps), so the
// behavior lives outside app and can be toggled on/off.
//
// integration contract (the caller drives the fish animation):
//   1. trigger()                       on the celebration event (fish focus)
//   2. update(frameDt)                 once per frame, with the same dt the fish
//                                      path clock advances by
//   3. pathTimeOffset()                subtract from the path clock to freeze fish
//   4. faceCamera()/bodyWiggle()       per fish, to blend facing + add the wiggle
class FishCelebration
{
public:
    struct Config
    {
        float durationSeconds = 5.0f;     // total hold duration
        float turnSeconds = 0.6f;         // ramp in/out (turn to/from camera)
        float wiggleAmplitude = 0.13f;    // body shimmy amplitude (radians)
        float wiggleFrequency = 13.0f;    // body shimmy speed (radians/sec)
    };

    struct Facing
    {
        float yaw = 0.0f;
        float pitch = 0.0f;
    };

    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }

    // begin a celebration. no-op while disabled.
    void trigger();

    // advance the timer and frozen-path accumulator. large hitches consume the
    // celebration instead of stretching it longer than its configured duration.
    void update(float dt);

    // clear all state (timer + accumulated path offset).
    void reset();

    bool active() const { return timer_ > 0.0f; }

    // 0..1 ramp that eases the facing + wiggle in at the start and out at the end.
    float blend() const;

    // seconds the fish path clock should be rewound by so every fish holds still.
    float pathTimeOffset() const { return pathTimeOffset_; }

    // blend a fish's swim facing toward the camera by the current blend amount.
    // returns the unchanged swim facing when the celebration is not active.
    Facing faceCamera(float swimYaw, float swimPitch, const glm::vec3& fishPos,
                      const glm::vec3& cameraPos) const;

    // extra body-yaw wiggle (radians) for the current blend. realTime keeps the
    // shimmy animating even though the swim path is frozen. returns 0 when idle.
    float bodyWiggle(float realTime, float phase) const;

    const Config& config() const { return config_; }
    void setConfig(const Config& config) { config_ = config; }

private:
    Config config_{};
    bool enabled_ = true;
    float timer_ = 0.0f;
    float pathTimeOffset_ = 0.0f;
};

} // namespace engine::game
