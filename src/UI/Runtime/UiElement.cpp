#include "UI/Runtime/UiElement.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ui
{
namespace
{

UiRect offsetRect(const UiRect& rect, const UiRect& parent)
{
    return UiRect{parent.x + rect.x, parent.y + rect.y, rect.width, rect.height};
}

float horizontalOffsetForAlignment(float availableWidth, float childWidth,
                                   UiCrossAxisAlign align)
{
    switch (align)
    {
    case UiCrossAxisAlign::Start:
    case UiCrossAxisAlign::Stretch:
        return 0.0f;
    case UiCrossAxisAlign::Center:
        return std::max(0.0f, (availableWidth - childWidth) * 0.5f);
    case UiCrossAxisAlign::End:
        return std::max(0.0f, availableWidth - childWidth);
    }

    return 0.0f;
}

float verticalOffsetForAlignment(float availableHeight, float childHeight,
                                 UiCrossAxisAlign align)
{
    switch (align)
    {
    case UiCrossAxisAlign::Start:
    case UiCrossAxisAlign::Stretch:
        return 0.0f;
    case UiCrossAxisAlign::Center:
        return std::max(0.0f, (availableHeight - childHeight) * 0.5f);
    case UiCrossAxisAlign::End:
        return std::max(0.0f, availableHeight - childHeight);
    }

    return 0.0f;
}

UiRect verticalStackChildRect(const UiElement& parent, const UiElement& child,
                              float cursorY, float resolvedHeight)
{
    const float availableWidth =
        std::max(0.0f, parent.computedRect.width - parent.padding.left -
                           parent.padding.right - child.rect.x);
    const bool fillWidth = child.rect.width <= 0.0f ||
                           parent.crossAxisAlign == UiCrossAxisAlign::Stretch;
    const float width = fillWidth ? availableWidth
                                  : std::min(child.rect.width, availableWidth);
    const float x =
        parent.computedRect.x + parent.padding.left + child.rect.x +
        horizontalOffsetForAlignment(availableWidth, width, parent.crossAxisAlign);
    const float y = cursorY + child.rect.y;
    const float height = std::max(0.0f, resolvedHeight);
    return UiRect{x, y, width, height};
}

UiRect horizontalStackChildRect(const UiElement& parent, const UiElement& child,
                                float cursorX, float resolvedWidth)
{
    const float availableHeight =
        std::max(0.0f, parent.computedRect.height - parent.padding.top -
                           parent.padding.bottom - child.rect.y);
    const bool fillHeight = child.rect.height <= 0.0f ||
                            parent.crossAxisAlign == UiCrossAxisAlign::Stretch;
    const float width = std::max(0.0f, resolvedWidth);
    const float height = fillHeight ? availableHeight
                                    : std::min(child.rect.height, availableHeight);
    const float x = cursorX + child.rect.x;
    const float y =
        parent.computedRect.y + parent.padding.top + child.rect.y +
        verticalOffsetForAlignment(availableHeight, height, parent.crossAxisAlign);
    return UiRect{x, y, width, height};
}

float verticalMainSizeBase(const UiElement& child)
{
    return std::max(std::max(0.0f, child.rect.height), child.minMainSize);
}

float horizontalMainSizeBase(const UiElement& child)
{
    return std::max(std::max(0.0f, child.rect.width), child.minMainSize);
}

float positiveGrowWeight(const UiElement& child)
{
    return std::max(0.0f, child.grow);
}

float stackSpacingTotal(const UiElement& element)
{
    if (element.children.size() < 2)
    {
        return 0.0f;
    }

    return element.spacing * static_cast<float>(element.children.size() - 1);
}

std::vector<float> resolveVerticalMainSizes(const UiElement& element)
{
    std::vector<float> sizes;
    sizes.reserve(element.children.size());

    float baseTotal = 0.0f;
    float growTotal = 0.0f;
    for (const UiElement& child : element.children)
    {
        const float base = verticalMainSizeBase(child);
        sizes.push_back(base);
        baseTotal += base;
        growTotal += positiveGrowWeight(child);
    }

    if (growTotal <= 0.0f)
    {
        return sizes;
    }

    const float contentHeight =
        std::max(0.0f, element.computedRect.height - element.padding.top -
                           element.padding.bottom);
    const float remaining =
        std::max(0.0f, contentHeight - stackSpacingTotal(element) - baseTotal);

    for (size_t i = 0; i < element.children.size(); ++i)
    {
        const float grow = positiveGrowWeight(element.children[i]);
        if (grow > 0.0f)
        {
            sizes[i] += remaining * (grow / growTotal);
        }
    }

    return sizes;
}

std::vector<float> resolveHorizontalMainSizes(const UiElement& element)
{
    std::vector<float> sizes;
    sizes.reserve(element.children.size());

    float baseTotal = 0.0f;
    float growTotal = 0.0f;
    for (const UiElement& child : element.children)
    {
        const float base = horizontalMainSizeBase(child);
        sizes.push_back(base);
        baseTotal += base;
        growTotal += positiveGrowWeight(child);
    }

    if (growTotal <= 0.0f)
    {
        return sizes;
    }

    const float contentWidth =
        std::max(0.0f, element.computedRect.width - element.padding.left -
                           element.padding.right);
    const float remaining =
        std::max(0.0f, contentWidth - stackSpacingTotal(element) - baseTotal);

    for (size_t i = 0; i < element.children.size(); ++i)
    {
        const float grow = positiveGrowWeight(element.children[i]);
        if (grow > 0.0f)
        {
            sizes[i] += remaining * (grow / growTotal);
        }
    }

    return sizes;
}

void applyAnimOffset(UiElement& element)
{
    element.computedRect.x += element.animOffsetX;
    element.computedRect.y += element.animOffsetY;
}

void computeLayoutRecursive(UiElement& element)
{
    if (element.layoutMode == UiLayoutMode::VerticalStack)
    {
        float cursor = element.computedRect.y + element.padding.top -
                       std::max(0.0f, element.scrollOffsetY);
        const std::vector<float> childHeights = resolveVerticalMainSizes(element);
        for (size_t i = 0; i < element.children.size(); ++i)
        {
            UiElement& child = element.children[i];
            child.computedRect =
                verticalStackChildRect(element, child, cursor, childHeights[i]);
            applyAnimOffset(child);
            computeLayoutRecursive(child);
            cursor = child.computedRect.y + child.computedRect.height + element.spacing;
        }
        return;
    }

    if (element.layoutMode == UiLayoutMode::HorizontalStack)
    {
        float cursor = element.computedRect.x + element.padding.left;
        const std::vector<float> childWidths = resolveHorizontalMainSizes(element);
        for (size_t i = 0; i < element.children.size(); ++i)
        {
            UiElement& child = element.children[i];
            child.computedRect =
                horizontalStackChildRect(element, child, cursor, childWidths[i]);
            applyAnimOffset(child);
            computeLayoutRecursive(child);
            cursor = child.computedRect.x + child.computedRect.width + element.spacing;
        }
        return;
    }

    for (UiElement& child : element.children)
    {
        child.computedRect = offsetRect(child.rect, element.computedRect);
        child.computedRect.y -= std::max(0.0f, element.scrollOffsetY);
        applyAnimOffset(child);
        computeLayoutRecursive(child);
    }
}

UiScissor rectToScissor(const UiRect& rect)
{
    const float x0 = std::max(0.0f, std::floor(rect.x));
    const float y0 = std::max(0.0f, std::floor(rect.y));
    const float x1 = std::max(x0, std::ceil(rect.x + rect.width));
    const float y1 = std::max(y0, std::ceil(rect.y + rect.height));

    return UiScissor{
        static_cast<int32_t>(x0),
        static_cast<int32_t>(y0),
        static_cast<uint32_t>(x1 - x0),
        static_cast<uint32_t>(y1 - y0),
    };
}

UiScissor intersectScissor(UiScissor a, UiScissor b)
{
    const int32_t ax1 = a.x + static_cast<int32_t>(a.width);
    const int32_t ay1 = a.y + static_cast<int32_t>(a.height);
    const int32_t bx1 = b.x + static_cast<int32_t>(b.width);
    const int32_t by1 = b.y + static_cast<int32_t>(b.height);

    const int32_t x0 = std::max(a.x, b.x);
    const int32_t y0 = std::max(a.y, b.y);
    const int32_t x1 = std::min(ax1, bx1);
    const int32_t y1 = std::min(ay1, by1);

    if (x1 <= x0 || y1 <= y0)
    {
        return UiScissor{};
    }

    return UiScissor{
        x0,
        y0,
        static_cast<uint32_t>(x1 - x0),
        static_cast<uint32_t>(y1 - y0),
    };
}

uint32_t scaleColorByOpacity(uint32_t color, float opacity)
{
    if (opacity >= 1.0f)
    {
        return color;
    }
    if (opacity <= 0.0f)
    {
        return 0u;
    }

    // packed colors are premultiplied by alpha, so a uniform per-channel scale
    // fades the color correctly.
    uint32_t result = 0u;
    for (uint32_t shift = 0u; shift < 32u; shift += 8u)
    {
        const uint32_t channel = (color >> shift) & 0xffu;
        const uint32_t scaled =
            static_cast<uint32_t>(static_cast<float>(channel) * opacity + 0.5f);
        result |= (scaled & 0xffu) << shift;
    }
    return result;
}

void paintElement(const UiElement& element, const FontAsset& font, UiDrawList& drawList,
                  UiScissor scissor, UiPaintStats& stats,
                  const UiPaintOptions& options, float parentOpacity)
{
    if (!element.visible || scissor.width == 0 || scissor.height == 0)
    {
        return;
    }

    const float opacity =
        std::clamp(parentOpacity * std::clamp(element.opacity, 0.0f, 1.0f), 0.0f, 1.0f);
    if (opacity <= 0.0f)
    {
        return;
    }

    ++stats.elements;

    const auto resolvedColor = [&]() -> uint32_t {
        if (!element.enabled && element.style.hasDisabledColor)
        {
            return element.style.disabledColor;
        }
        if (element.style.hasPressedColor &&
            std::string_view(element.id) == options.pressedElementId)
        {
            return element.style.pressedColor;
        }
        if (element.style.hasHoverColor &&
            std::string_view(element.id) == options.hoveredElementId)
        {
            return element.style.hoverColor;
        }
        return element.style.color;
    };

    switch (element.kind)
    {
    case UiElementKind::Container:
    case UiElementKind::Spacer:
        break;
    case UiElementKind::Rect:
        drawList.addSolidRect(element.computedRect,
                              scaleColorByOpacity(resolvedColor(), opacity), scissor);
        ++stats.rectElements;
        break;
    case UiElementKind::Button:
    {
        drawList.addSolidRect(element.computedRect,
                              scaleColorByOpacity(resolvedColor(), opacity), scissor);
        ++stats.rectElements;

        if (!element.text.value.empty())
        {
            TextLayoutOptions layout = element.text.layout;
            if (layout.maxWidth <= 0.0f && element.computedRect.width > 0.0f)
            {
                layout.maxWidth = element.computedRect.width;
            }

            const TextLayoutResult measured =
                measureText(font, element.text.value, element.text.pixelHeight, layout);
            const float textY =
                element.computedRect.y +
                std::max(0.0f, (element.computedRect.height - measured.height) * 0.5f);
            const uint32_t textColor =
                !element.enabled && element.style.hasDisabledTextColor
                    ? element.style.disabledTextColor
                    : element.style.textColor;
            const TextLayoutResult textResult =
                addText(font, drawList, element.text.value, element.computedRect.x, textY,
                        element.text.pixelHeight, scaleColorByOpacity(textColor, opacity),
                        scissor, layout);
            stats.glyphCount += textResult.glyphCount;
            ++stats.textElements;
        }
        break;
    }
    case UiElementKind::Text:
    {
        TextLayoutOptions layout = element.text.layout;
        if (layout.maxWidth <= 0.0f && element.computedRect.width > 0.0f)
        {
            layout.maxWidth = element.computedRect.width;
        }

        const TextLayoutResult textResult =
            addText(font, drawList, element.text.value, element.computedRect.x,
                    element.computedRect.y, element.text.pixelHeight,
                    scaleColorByOpacity(element.style.color, opacity), scissor, layout);
        stats.glyphCount += textResult.glyphCount;
        ++stats.textElements;
        break;
    }
    }

    const UiScissor childScissor =
        element.clipChildren ? intersectScissor(scissor, rectToScissor(element.computedRect))
                             : scissor;
    for (const UiElement& child : element.children)
    {
        paintElement(child, font, drawList, childScissor, stats, options, opacity);
    }
}

bool pointInRect(const UiRect& rect, float x, float y)
{
    return rect.width > 0.0f && rect.height > 0.0f && x >= rect.x && y >= rect.y &&
           x < rect.x + rect.width && y < rect.y + rect.height;
}

bool canHitElementSelf(const UiElement& element)
{
    switch (element.kind)
    {
    case UiElementKind::Button:
    case UiElementKind::Rect:
    case UiElementKind::Text:
        return true;
    case UiElementKind::Container:
    case UiElementKind::Spacer:
        return false;
    }

    return false;
}

UiHitResult hitTestElement(const UiElement& element, float x, float y)
{
    if (!element.visible || !element.enabled)
    {
        return {};
    }

    const bool inside = pointInRect(element.computedRect, x, y);
    if (element.clipChildren && !inside)
    {
        return {};
    }

    for (auto it = element.children.rbegin(); it != element.children.rend(); ++it)
    {
        UiHitResult childHit = hitTestElement(*it, x, y);
        if (childHit.hit)
        {
            return childHit;
        }
    }

    if (inside && canHitElementSelf(element))
    {
        return UiHitResult{&element, true};
    }

    return {};
}

} // namespace

const char* layoutModeLabel(UiLayoutMode mode)
{
    switch (mode)
    {
    case UiLayoutMode::None:
        return "none";
    case UiLayoutMode::VerticalStack:
        return "vertical_stack";
    case UiLayoutMode::HorizontalStack:
        return "horizontal_stack";
    }

    return "unknown";
}

const char* crossAxisAlignLabel(UiCrossAxisAlign align)
{
    switch (align)
    {
    case UiCrossAxisAlign::Start:
        return "start";
    case UiCrossAxisAlign::Center:
        return "center";
    case UiCrossAxisAlign::End:
        return "end";
    case UiCrossAxisAlign::Stretch:
        return "stretch";
    }

    return "unknown";
}

UiElement* findElementById(UiElement& element, std::string_view id)
{
    if (std::string_view(element.id) == id)
    {
        return &element;
    }

    for (UiElement& child : element.children)
    {
        if (UiElement* result = findElementById(child, id))
        {
            return result;
        }
    }

    return nullptr;
}

const UiElement* findElementById(const UiElement& element, std::string_view id)
{
    if (std::string_view(element.id) == id)
    {
        return &element;
    }

    for (const UiElement& child : element.children)
    {
        if (const UiElement* result = findElementById(child, id))
        {
            return result;
        }
    }

    return nullptr;
}

void computeLayout(UiElement& root)
{
    root.computedRect = root.rect;
    root.computedRect.x += root.animOffsetX;
    root.computedRect.y += root.animOffsetY;
    computeLayoutRecursive(root);
}

UiHitResult hitTest(const UiElement& root, float x, float y)
{
    return hitTestElement(root, x, y);
}

UiPaintStats paintTree(const UiElement& root, const FontAsset& font, UiDrawList& drawList)
{
    return paintTree(root, font, drawList, UiPaintOptions{});
}

UiPaintStats paintTree(const UiElement& root, const FontAsset& font, UiDrawList& drawList,
                       const UiPaintOptions& options)
{
    UiPaintStats stats{};
    stats.commandCountBefore = static_cast<uint32_t>(drawList.commands.size());
    paintElement(root, font, drawList, rectToScissor(root.computedRect), stats, options,
                 1.0f);
    stats.commandCountAfter = static_cast<uint32_t>(drawList.commands.size());
    return stats;
}

} // namespace ui
