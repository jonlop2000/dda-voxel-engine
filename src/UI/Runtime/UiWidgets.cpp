#include "UI/Runtime/UiWidgets.h"

#include <utility>

namespace ui
{

UiElement makeButton(UiButtonOptions options)
{
    UiElement element{};
    element.id = std::move(options.id);
    element.action = std::move(options.action);
    element.actionPayload = std::move(options.actionPayload);
    element.kind = UiElementKind::Button;
    element.rect = options.rect;
    element.enabled = options.enabled;
    element.style.color = options.style.backgroundColor;
    element.style.hoverColor = options.style.hoverColor;
    element.style.pressedColor = options.style.pressedColor;
    element.style.disabledColor = options.style.disabledColor;
    element.style.textColor = options.style.textColor;
    element.style.disabledTextColor = options.style.disabledTextColor;
    element.style.hasHoverColor = true;
    element.style.hasPressedColor = true;
    element.style.hasDisabledColor = true;
    element.style.hasDisabledTextColor = true;
    element.text.value = std::move(options.label);
    element.text.pixelHeight = options.pixelHeight;
    element.text.layout = TextLayoutOptions{0.0f, 0.0f, TextAlign::Center};
    element.clipChildren = false;
    return element;
}

UiElement makeButton(std::string id, UiRect rect, std::string label, std::string action)
{
    UiButtonOptions options{};
    options.id = std::move(id);
    options.rect = rect;
    options.label = std::move(label);
    options.action = std::move(action);
    return makeButton(std::move(options));
}

} // namespace ui
