#pragma once

#include <cstdint>
#include <string>

#include "UI/Runtime/UiElement.h"
#include "UI/Runtime/UiTheme.h"
#include "UI/Runtime/UiTypes.h"

namespace ui
{

struct UiButtonStyle
{
    uint32_t backgroundColor = themeColor(defaultUiTheme().buttonInk, 1.0f);
    uint32_t hoverColor = themeColor(defaultUiTheme().buttonHoverInk, 1.0f);
    uint32_t pressedColor = themeColor(defaultUiTheme().accent, 0.95f);
    uint32_t disabledColor = themeColor(defaultUiTheme().buttonDisabledInk, 0.72f);
    uint32_t textColor = themeColor(defaultUiTheme().foreground, 0.92f);
    uint32_t disabledTextColor = themeColor(defaultUiTheme().textDisabled, 0.76f);
};

struct UiButtonOptions
{
    std::string id{};
    UiRect rect{};
    std::string label{};
    std::string action{};
    std::string actionPayload{};
    UiButtonStyle style{};
    float pixelHeight = 16.0f;
    bool enabled = true;
};

UiElement makeButton(UiButtonOptions options);
UiElement makeButton(std::string id, UiRect rect, std::string label, std::string action);

} // namespace ui
