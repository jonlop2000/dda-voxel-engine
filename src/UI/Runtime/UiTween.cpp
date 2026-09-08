#include "UI/Runtime/UiTween.h"

#include <algorithm>

namespace ui
{

const char* tweenPropertyLabel(UiTweenProperty property)
{
    switch (property)
    {
    case UiTweenProperty::Opacity:
        return "opacity";
    case UiTweenProperty::OffsetX:
        return "offset_x";
    case UiTweenProperty::OffsetY:
        return "offset_y";
    }

    return "unknown";
}

float uiEaseValue(UiEase ease, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    switch (ease)
    {
    case UiEase::Linear:
        return t;
    case UiEase::EaseOutCubic:
    {
        const float inv = 1.0f - t;
        return 1.0f - inv * inv * inv;
    }
    case UiEase::EaseInCubic:
        return t * t * t;
    }

    return t;
}

void UiTweenSet::applyValue(UiTree& tree, const std::string& elementId,
                            UiTweenProperty property, float value)
{
    UiElement* element = findElementById(tree.root, elementId);
    if (element == nullptr)
    {
        return;
    }

    switch (property)
    {
    case UiTweenProperty::Opacity:
        element->opacity = std::clamp(value, 0.0f, 1.0f);
        break;
    case UiTweenProperty::OffsetX:
        element->animOffsetX = value;
        break;
    case UiTweenProperty::OffsetY:
        element->animOffsetY = value;
        break;
    }
}

bool UiTweenSet::hasLayoutAffectingTweens() const
{
    for (const ActiveTween& tween : active_)
    {
        if (tween.spec.property != UiTweenProperty::Opacity)
        {
            return true;
        }
    }
    return false;
}

void UiTweenSet::start(const UiTweenSpec& spec, UiTree& tree)
{
    std::erase_if(active_, [&](const ActiveTween& tween) {
        return tween.spec.elementId == spec.elementId &&
               tween.spec.property == spec.property;
    });

    applyValue(tree, spec.elementId, spec.property, spec.from);
    active_.push_back(ActiveTween{spec, 0.0f});
}

std::vector<UiTweenCompletion> UiTweenSet::update(float dtSeconds, UiTree& tree)
{
    std::vector<UiTweenCompletion> completions;
    if (active_.empty())
    {
        return completions;
    }

    const float dt = std::max(0.0f, dtSeconds);
    std::vector<ActiveTween> stillActive;
    stillActive.reserve(active_.size());

    for (ActiveTween& tween : active_)
    {
        const float duration = std::max(tween.spec.durationSeconds, 1e-4f);
        tween.elapsedSeconds = instant_ ? duration : tween.elapsedSeconds + dt;
        const float t = std::clamp(tween.elapsedSeconds / duration, 0.0f, 1.0f);
        const float eased = uiEaseValue(tween.spec.ease, t);
        const float value = tween.spec.from + (tween.spec.to - tween.spec.from) * eased;
        applyValue(tree, tween.spec.elementId, tween.spec.property, value);

        if (t >= 1.0f)
        {
            completions.push_back(
                UiTweenCompletion{tween.spec.elementId, tween.spec.property,
                                  tween.spec.to});
        }
        else
        {
            stillActive.push_back(tween);
        }
    }

    active_ = std::move(stillActive);
    return completions;
}

} // namespace ui
