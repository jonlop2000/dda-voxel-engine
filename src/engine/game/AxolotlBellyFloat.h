#pragma once

#include <string>

namespace engine::game
{

// the axolotl's focus celebration. while the axolotl is focused, it owns the
// mexico-vs-England typing banner; when enabled, it also rolls the axolotl over
// to swim belly up. pure logic (no engine deps), mirroring FishCelebration's
// contract.
//
// unlike FishCelebration this is not a timed one-shot: it follows the focus
// state for as long as focus holds. the caller drives it:
//   1. update(frameDt, focused)    once per frame; focused = "the axolotl is
//                                  the focused creature right now"
//   2. rollRadians()               added to the axolotl's swim roll so the
//                                  whole rig (body, tail, limbs) turns over
//
// mutual exclusion with the viva mexico celebration is enforced by the owner
// (GameRuntime), not here.
class AxolotlBellyFloat
{
public:
    struct Config
    {
        float rollSeconds = 0.9f; // ease duration for rolling over / righting
    };

    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }

    // advance focus timing and roll pose. the typing banner follows focused
    // state; the belly-up roll additionally requires enabled().
    void update(float dt, bool focused);

    // clear all state (snaps upright).
    void reset();

    bool active() const { return progress_ > 0.0f; }

    // smoothstep-eased 0..1 blend of the roll.
    float blend() const;

    // roll to add to the swim rig's local-X roll: blend() * pi (belly up).
    float rollRadians() const;

    // seconds the axolotl focus celebration has been held; drives the typing
    // banner animation. this accumulates while focused, even if the belly-roll
    // animation itself is disabled.
    float activeSeconds() const { return activeSeconds_; }

    // mexico-vs-England typing banner for the focused axolotl.
    std::string typedVersusBanner() const;

    const Config& config() const { return config_; }
    void setConfig(const Config& config) { config_ = config; }

private:
    Config config_{};
    bool enabled_ = false; // off by default; enabling it disables viva mexico
    float progress_ = 0.0f; // raw 0..1 roll progress
    float activeSeconds_ = 0.0f;
};

} // namespace engine::game
