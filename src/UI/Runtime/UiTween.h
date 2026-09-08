#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "UI/Runtime/UiElement.h"

namespace ui
{

enum class UiTweenProperty
{
    Opacity,
    OffsetX,
    OffsetY,
};

enum class UiEase
{
    Linear,
    EaseOutCubic,
    EaseInCubic,
};

const char* tweenPropertyLabel(UiTweenProperty property);

// pure easing evaluation; t is clamped to [0, 1].
float uiEaseValue(UiEase ease, float t);

struct UiTweenSpec
{
    std::string elementId{};
    UiTweenProperty property = UiTweenProperty::Opacity;
    float from = 0.0f;
    float to = 1.0f;
    float durationSeconds = 0.2f;
    UiEase ease = UiEase::EaseOutCubic;
};

struct UiTweenCompletion
{
    std::string elementId{};
    UiTweenProperty property = UiTweenProperty::Opacity;
    float value = 0.0f;
};

// retained-tree tween set. starting a tween applies its from-value
// immediately; update() advances all tweens and writes values into the tree.
// instant mode (used under ui automation scripts) completes every tween in a
// single update so smoke logs and layout needles stay frame-deterministic.
class UiTweenSet
{
public:
    void setInstant(bool instant) { instant_ = instant; }
    bool instant() const { return instant_; }

    // drops presentation-only work when the retained screen tree is replaced.
    // a tween targets an element id in one concrete tree and must never carry
    // across a screen/session boundary into a later tree with the same id.
    void clear()
    {
        active_.clear();
        instant_ = false;
    }

    // replaces any active tween on the same element+property.
    void start(const UiTweenSpec& spec, UiTree& tree);
    std::vector<UiTweenCompletion> update(float dtSeconds, UiTree& tree);

    bool anyActive() const { return !active_.empty(); }
    size_t activeCount() const { return active_.size(); }

    // true if any active tween moves elements (offsets). Opacity-only tweens are
    // paint-time and do not invalidate layout.
    bool hasLayoutAffectingTweens() const;

private:
    struct ActiveTween
    {
        UiTweenSpec spec{};
        float elapsedSeconds = 0.0f;
    };

    static void applyValue(UiTree& tree, const std::string& elementId,
                           UiTweenProperty property, float value);

    std::vector<ActiveTween> active_{};
    bool instant_ = false;
};

} // namespace ui
