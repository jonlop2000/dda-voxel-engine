#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "UI/Runtime/UiDebugFont.h"
#include "UI/Runtime/UiTypes.h"

namespace ui
{

enum class UiElementKind
{
    Container,
    Rect,
    Spacer,
    Text,
    Button,
};

enum class UiLayoutMode
{
    None,
    VerticalStack,
    HorizontalStack,
};

enum class UiCrossAxisAlign
{
    Start,
    Center,
    End,
    Stretch,
};

struct UiPadding
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct UiElementStyle
{
    uint32_t color = 0xffffffffu;
    uint32_t textColor = 0xffffffffu;
    uint32_t hoverColor = 0xffffffffu;
    uint32_t pressedColor = 0xffffffffu;
    uint32_t disabledColor = 0xffffffffu;
    uint32_t disabledTextColor = 0xffffffffu;
    bool hasHoverColor = false;
    bool hasPressedColor = false;
    bool hasDisabledColor = false;
    bool hasDisabledTextColor = false;
};

struct UiTextPayload
{
    std::string value{};
    float pixelHeight = 16.0f;
    TextLayoutOptions layout{};
};

struct UiElement
{
    std::string id{};
    std::string action{};
    std::string actionPayload{};
    UiElementKind kind = UiElementKind::Rect;
    UiRect rect{};
    UiRect computedRect{};
    UiLayoutMode layoutMode = UiLayoutMode::None;
    UiCrossAxisAlign crossAxisAlign = UiCrossAxisAlign::Start;
    UiPadding padding{};
    float spacing = 0.0f;
    float grow = 0.0f;
    float minMainSize = 0.0f;
    float scrollOffsetY = 0.0f;
    // animated presentation state, written by UiTweenSet. opacity is
    // hierarchical (multiplied down the subtree at paint); offsets shift this
    // element's computed rect (children follow since their rects derive from it).
    float opacity = 1.0f;
    float animOffsetX = 0.0f;
    float animOffsetY = 0.0f;
    UiElementStyle style{};
    UiTextPayload text{};
    std::vector<UiElement> children{};
    bool visible = true;
    bool enabled = true;
    bool clipChildren = true;
};

struct UiTree
{
    UiElement root{};
};

struct UiPaintStats
{
    uint32_t elements = 0;
    uint32_t rectElements = 0;
    uint32_t textElements = 0;
    uint32_t glyphCount = 0;
    uint32_t commandCountBefore = 0;
    uint32_t commandCountAfter = 0;
};

struct UiPaintOptions
{
    std::string_view hoveredElementId{};
    std::string_view pressedElementId{};
};

struct UiHitResult
{
    const UiElement* element = nullptr;
    bool hit = false;
};

const char* layoutModeLabel(UiLayoutMode mode);
const char* crossAxisAlignLabel(UiCrossAxisAlign align);
UiElement* findElementById(UiElement& element, std::string_view id);
const UiElement* findElementById(const UiElement& element, std::string_view id);
void computeLayout(UiElement& root);
UiHitResult hitTest(const UiElement& root, float x, float y);
UiPaintStats paintTree(const UiElement& root, const FontAsset& font, UiDrawList& drawList);
UiPaintStats paintTree(const UiElement& root, const FontAsset& font, UiDrawList& drawList,
                       const UiPaintOptions& options);

} // namespace ui
