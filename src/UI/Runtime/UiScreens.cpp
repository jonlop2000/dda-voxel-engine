#include "UI/Runtime/UiScreens.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "UI/Runtime/UiTheme.h"
#include "UI/Runtime/UiTypes.h"
#include "UI/Runtime/UiWidgets.h"

namespace ui
{
namespace
{

constexpr float kDesignViewportW = 1280.0f;
constexpr float kDesignViewportH = 720.0f;
constexpr float kHudMargin = 32.0f;
constexpr float kHudBottomMargin = 72.0f;
constexpr float kSmokePanelX = kHudMargin;
constexpr float kSmokePanelY = kHudMargin;
constexpr float kSmokePanelW = 460.0f;
constexpr float kSmokePanelH = 240.0f;
constexpr float kSecondaryPanelH = 48.0f;
constexpr float kFishPanelW = 220.0f;
constexpr float kFishPanelH = 260.0f; // minimum; the list binding grows it to fit the roster
constexpr float kFishRowStride = 34.0f;
constexpr float kFishRowHeight = 28.0f;
constexpr float kFishListContentTop = 48.0f;
constexpr float kFishListBottomPad = 12.0f;
constexpr float kHudPanelGap = 16.0f;
constexpr float kCarePanelW = 260.0f;
constexpr float kCarePanelH = 154.0f;
constexpr float kCareHungerMeterMaxW = kCarePanelW - 32.0f;
constexpr float kFishWorldLabelW = 92.0f;
constexpr float kFishWorldLabelH = 24.0f;
constexpr float kFishWorldLabelMargin = 8.0f;
// focused fish get a larger celebration bubble with an 8-bit mexico flag and a
// "viva mexico!" banner. the font uppercases, so the banner reads in caps; the
// accents come from the latin-1 glyphs added to the debug font (u+00A1, u+00E9).
constexpr float kFishWorldLabelFocusedW = 132.0f;
constexpr float kFishWorldLabelFocusedH = 50.0f;
// focused-axolotl "versus" bubble: two 8-bit flags with a typing banner below.
constexpr float kFishWorldLabelVersusW = 196.0f;
constexpr float kFishWorldLabelVersusH = 54.0f;
// "¡viva méxico!" as utf-8 (\xC2\xA1 = u+00A1 inverted !, \xC3\xA9 = u+00E9 é),
// written with escapes so this source stays ascii-clean for msvc.
constexpr const char* kVivaMexicoBanner = "\xC2\xA1VIVA M\xC3\xA9XICO!";
constexpr float kWaterTemperatureMaxC = 40.0f;
constexpr float kTelemetryOxygenMaxW = 56.0f;
constexpr float kTelemetryTemperatureMaxW = 70.0f;
constexpr float kTelemetryFlowMaxW = 40.0f;
constexpr float kPausePanelW = 320.0f;
constexpr float kPausePanelH = 210.0f;
constexpr float kCatalogPanelX = kSmokePanelX;
constexpr float kCatalogPanelY = kSmokePanelY + kSmokePanelH + 16.0f;
constexpr float kCatalogPanelW = kSmokePanelW;
constexpr float kCatalogPanelH = 260.0f;
constexpr float kCatalogListX = 24.0f;
constexpr float kCatalogListY = 92.0f;
constexpr float kCatalogListW = 412.0f;
constexpr float kCatalogListH = 124.0f;
constexpr float kCatalogItemX = 0.0f;
constexpr float kCatalogItemFirstY = 4.0f;
constexpr float kCatalogItemHeight = 28.0f;
constexpr float kCatalogItemSpacingY = 38.0f;
constexpr float kCompactViewportMinW = 800.0f;
constexpr float kCompactViewportMinH = 668.0f;
constexpr float kCompactHudMargin = 12.0f;
constexpr float kCompactHudPanelW = 340.0f;
constexpr float kCompactCatalogListX = 16.0f;
constexpr float kCompactCatalogListW =
    kCompactHudPanelW - 2.0f * kCompactCatalogListX;
constexpr float kCompactCatalogFooterButtonW = 146.0f;
constexpr float kFishListWheelStep = kFishRowStride;
constexpr float kCompactTelemetryMeterMaxW = 16.0f;

UiElement makeRect(std::string id, UiRect rect, uint32_t color)
{
    UiElement element{};
    element.id = std::move(id);
    element.kind = UiElementKind::Rect;
    element.rect = rect;
    element.style.color = color;
    element.clipChildren = false;
    return element;
}

UiElement makeText(std::string id, UiRect rect, std::string value, float pixelHeight,
                   uint32_t color, TextLayoutOptions layout = {})
{
    UiElement element{};
    element.id = std::move(id);
    element.kind = UiElementKind::Text;
    element.rect = rect;
    element.style.color = color;
    element.text.value = std::move(value);
    element.text.pixelHeight = pixelHeight;
    element.text.layout = layout;
    element.clipChildren = false;
    return element;
}

UiElement makeSpacer(std::string id, float height)
{
    UiElement element{};
    element.id = std::move(id);
    element.kind = UiElementKind::Spacer;
    element.rect = UiRect{0.0f, 0.0f, 0.0f, height};
    element.clipChildren = false;
    return element;
}

UiElement makeGrowSpacer(std::string id, float grow, float minHeight = 0.0f)
{
    UiElement element = makeSpacer(std::move(id), 0.0f);
    element.grow = grow;
    element.minMainSize = minHeight;
    return element;
}

std::string buildItemDisplayLabel(std::string_view itemKey)
{
    const BuildCatalogItemDefinition* item = findRuntimeBuildCatalogItem(itemKey);
    if (item != nullptr)
    {
        return std::string(item->label);
    }
    return "ITEM";
}

std::string buildGhostPreviewLabel(std::string_view itemKey)
{
    if (itemKey == kBuildRemoveKey)
    {
        return "REMOVING TARGET";
    }

    return std::string("PLACING ") + buildItemDisplayLabel(itemKey);
}

std::string buildGhostPreviewHint(std::string_view itemKey, int rotationSteps)
{
    if (itemKey == kBuildRemoveKey)
    {
        return "TARGET READY";
    }

    return "ROT " + std::to_string(normalizeBuildPlacementRotationSteps(rotationSteps) *
                                    90) +
           " READY";
}

UiButtonStyle makeCatalogItemButtonStyle(size_t itemIndex, bool selected = false)
{
    const UiTheme& theme = defaultUiTheme();
    UiButtonStyle style{};
    if (selected)
    {
        style.backgroundColor = themeColor(theme.accent, 0.92f);
        style.hoverColor = themeColor(theme.accent, 1.0f);
        style.pressedColor = themeColor(theme.positive, 0.96f);
        style.textColor = themeColor(theme.foreground, 1.0f);
        return style;
    }

    switch (itemIndex)
    {
        case 0:
            style.backgroundColor = themeColor(theme.info, 0.88f);
            style.hoverColor = themeColor(theme.infoHover, 0.94f);
            break;
        case 1:
            style.backgroundColor = themeColor(theme.positive, 0.86f);
            style.hoverColor = themeColor(theme.positiveHover, 0.92f);
            break;
        default:
            style.backgroundColor = themeColor(theme.neutral, 0.86f);
            style.hoverColor = themeColor(theme.neutralHover, 0.92f);
            break;
    }

    style.pressedColor = themeColor(theme.accent, 0.95f);
    style.textColor = themeColor(theme.foreground, 0.92f);
    return style;
}

UiButtonStyle makeCatalogRemoveButtonStyle(bool selected = false)
{
    const UiTheme& theme = defaultUiTheme();
    UiButtonStyle style{};
    style.backgroundColor = themeColor(theme.neutral, 0.80f);
    style.hoverColor = themeColor(theme.neutralHover, 0.92f);
    style.pressedColor = themeColor(theme.accent, 0.95f);
    style.textColor = themeColor(theme.foreground, 0.92f);
    if (selected)
    {
        style.backgroundColor = themeColor(theme.accentAlt, 0.92f);
        style.hoverColor = themeColor(theme.accentAlt, 1.0f);
        style.pressedColor = themeColor(theme.positive, 0.96f);
        style.textColor = themeColor(theme.foreground, 1.0f);
    }
    return style;
}

UiElement makeCatalogItemButton(const BuildCatalogItemDefinition& item, size_t itemIndex)
{
    UiButtonOptions options{};
    options.id = std::string(item.id);
    options.rect = UiRect{kCatalogItemX,
                          kCatalogItemFirstY +
                              static_cast<float>(itemIndex) * kCatalogItemSpacingY,
                          item.rowWidth, kCatalogItemHeight};
    options.label = std::string(item.label);
    options.action = std::string(kSelectBuildCatalogItemAction);
    options.actionPayload = std::string(item.key);
    options.style = makeCatalogItemButtonStyle(itemIndex);
    return ui::makeButton(std::move(options));
}

UiButtonStyle makeFishRowButtonStyle(bool focused)
{
    const UiTheme& theme = defaultUiTheme();
    UiButtonStyle style{};
    if (focused)
    {
        style.backgroundColor = themeColor(theme.accent, 0.78f);
        style.hoverColor = themeColor(theme.accent, 0.88f);
        style.pressedColor = themeColor(theme.accent, 1.0f);
        style.textColor = themeColor(theme.foreground, 1.0f);
    }
    return style;
}

UiButtonStyle makeReleaseButtonStyle(bool active)
{
    const UiTheme& theme = defaultUiTheme();
    UiButtonStyle style{};
    style.disabledColor = themeColor(theme.buttonDisabledInk, 0.52f);
    style.disabledTextColor = themeColor(theme.textDisabled, 0.56f);
    if (active)
    {
        style.backgroundColor = themeColor(theme.accentAlt, 0.88f);
        style.hoverColor = themeColor(theme.accentAlt, 1.0f);
        style.pressedColor = themeColor(theme.positive, 0.94f);
        style.textColor = themeColor(theme.foreground, 1.0f);
    }
    return style;
}

void applyButtonStyle(UiElement& element, const UiButtonStyle& style)
{
    element.style.color = style.backgroundColor;
    element.style.hoverColor = style.hoverColor;
    element.style.pressedColor = style.pressedColor;
    element.style.disabledColor = style.disabledColor;
    element.style.textColor = style.textColor;
    element.style.disabledTextColor = style.disabledTextColor;
}

UiButtonStyle makeMenuButtonStyle(bool primary)
{
    const UiTheme& theme = defaultUiTheme();
    UiButtonStyle style{};
    if (primary)
    {
        style.backgroundColor = themeColor(theme.accent, 0.82f);
        style.hoverColor = themeColor(theme.accent, 0.96f);
        style.pressedColor = themeColor(theme.positive, 0.95f);
        style.textColor = themeColor(theme.foreground, 1.0f);
    }
    else
    {
        style.backgroundColor = themeColor(theme.neutral, 0.78f);
        style.hoverColor = themeColor(theme.neutralHover, 0.92f);
        style.pressedColor = themeColor(theme.accent, 0.90f);
        style.textColor = themeColor(theme.foreground, 0.94f);
    }
    return style;
}

std::string labelsOptionText(bool visible)
{
    return visible ? "LABELS: VISIBLE" : "LABELS: HIDDEN";
}

UiButtonStyle makeOptionToggleStyle(bool enabled)
{
    const UiTheme& theme = defaultUiTheme();
    UiButtonStyle style = makeMenuButtonStyle(enabled);
    if (!enabled)
    {
        style.backgroundColor = themeColor(theme.neutral, 0.66f);
        style.hoverColor = themeColor(theme.neutralHover, 0.84f);
        style.pressedColor = themeColor(theme.accent, 0.84f);
        style.textColor = themeColor(theme.foreground, 0.82f);
    }
    return style;
}

UiButtonStyle makeWaterMaintenanceButtonStyle(bool available, bool feedbackActive)
{
    const UiTheme& theme = defaultUiTheme();
    UiButtonStyle style = makeMenuButtonStyle(available);
    style.disabledColor = themeColor(theme.buttonDisabledInk, 0.54f);
    style.disabledTextColor = themeColor(theme.textDisabled, 0.66f);
    if (feedbackActive)
    {
        style.backgroundColor = themeColor(theme.positive, 0.88f);
        style.hoverColor = themeColor(theme.positiveHover, 0.94f);
        style.pressedColor = themeColor(theme.positive, 1.0f);
        style.textColor = themeColor(theme.foreground, 1.0f);
        style.disabledColor = themeColor(theme.positive, 0.88f);
        style.disabledTextColor = themeColor(theme.foreground, 1.0f);
    }
    return style;
}

UiButtonStyle makeCreatureFeedButtonStyle(bool available, bool feedbackActive)
{
    const UiTheme& theme = defaultUiTheme();
    UiButtonStyle style = makeMenuButtonStyle(available);
    style.disabledColor = themeColor(theme.buttonDisabledInk, 0.54f);
    style.disabledTextColor = themeColor(theme.textDisabled, 0.66f);
    if (feedbackActive)
    {
        style.backgroundColor = themeColor(theme.positive, 0.88f);
        style.hoverColor = themeColor(theme.positiveHover, 0.94f);
        style.pressedColor = themeColor(theme.positive, 1.0f);
        style.textColor = themeColor(theme.foreground, 1.0f);
        style.disabledColor = themeColor(theme.positive, 0.88f);
        style.disabledTextColor = themeColor(theme.foreground, 1.0f);
    }
    return style;
}

float clampLabelCoordinate(float value, float minValue, float maxValue)
{
    return std::clamp(value, minValue, std::max(minValue, maxValue));
}

bool rectsOverlapWithPadding(const UiRect& a, const UiRect& b, float padding)
{
    return a.x < b.x + b.width + padding && a.x + a.width + padding > b.x &&
           a.y < b.y + b.height + padding && a.y + a.height + padding > b.y;
}

UiElement makeHudStatusRow(std::string id, std::string label, std::string value,
                           uint32_t meterColor, float meterWidth)
{
    const UiTheme& theme = defaultUiTheme();
    UiElement row{};
    row.id = std::move(id);
    row.kind = UiElementKind::Container;
    row.rect = UiRect{0.0f, 0.0f, 0.0f, 16.0f};
    row.layoutMode = UiLayoutMode::HorizontalStack;
    row.crossAxisAlign = UiCrossAxisAlign::Center;
    row.spacing = 8.0f;
    row.clipChildren = false;
    row.children.reserve(4);

    row.children.push_back(makeText(row.id + "_label", UiRect{0.0f, 0.0f, 82.0f, 16.0f},
                                    std::move(label), theme.captionTextHeight,
                                    themeColor(theme.foreground, 0.72f)));
    row.children.push_back(makeText(row.id + "_value", UiRect{0.0f, 0.0f, 102.0f, 16.0f},
                                    std::move(value), theme.captionTextHeight,
                                    themeColor(theme.foreground, 0.92f)));
    row.children.push_back(makeGrowSpacer(row.id + "_gap", 1.0f));
    row.children.push_back(
        makeRect(row.id + "_meter", UiRect{0.0f, 0.0f, meterWidth, 8.0f}, meterColor));
    return row;
}

UiElement makeFishWorldLabel(const UiFishWorldLabelEntry& entry, const UiRect& layerRect)
{
    const UiTheme& theme = defaultUiTheme();
    const bool versus = entry.focused && entry.axolotlVersus;
    const float boxW = versus ? kFishWorldLabelVersusW
                              : (entry.focused ? kFishWorldLabelFocusedW : kFishWorldLabelW);
    const float boxH = versus ? kFishWorldLabelVersusH
                              : (entry.focused ? kFishWorldLabelFocusedH : kFishWorldLabelH);
    const float x = clampLabelCoordinate(
        entry.screenX - boxW * 0.5f, kFishWorldLabelMargin,
        layerRect.width - boxW - kFishWorldLabelMargin);
    const float y = clampLabelCoordinate(
        entry.screenY - boxH - kFishWorldLabelMargin, kFishWorldLabelMargin,
        layerRect.height - boxH - kFishWorldLabelMargin);

    UiElement label{};
    label.id = "fish_world_label_" + std::to_string(entry.id);
    label.kind = UiElementKind::Rect;
    label.rect = UiRect{x, y, boxW, boxH};
    label.enabled = false;
    label.clipChildren = false;

    if (versus)
    {
        // focused-axolotl celebration: 8-bit mexico flag vs england flag, with
        // the label underneath as a typing banner (the caller supplies the
        // progressively longer substring each frame).
        const UiThemeColorRole flagGreen{0.0f, 0.40f, 0.26f};
        const UiThemeColorRole flagWhite{0.95f, 0.95f, 0.93f};
        const UiThemeColorRole flagRed{0.80f, 0.08f, 0.15f};
        const UiThemeColorRole emblemBrown{0.42f, 0.27f, 0.13f};
        const UiThemeColorRole emblemGreen{0.26f, 0.42f, 0.12f};
        const UiThemeColorRole crossRed{0.84f, 0.10f, 0.12f};

        label.style.color = themeColor(theme.panelInk, 0.86f);
        label.children.reserve(11);

        constexpr float kFlagW = 40.0f;
        constexpr float kFlagH = 22.0f;
        constexpr float kFlagY = 6.0f;
        constexpr float kVsGapW = 28.0f;
        const float rowX = (boxW - (kFlagW * 2.0f + kVsGapW)) * 0.5f;
        constexpr float kThird = kFlagW / 3.0f;

        // mexico (left): green/white/red thirds with a tiny emblem.
        label.children.push_back(makeRect(label.id + "_mx_green",
            UiRect{rowX, kFlagY, kThird, kFlagH}, themeColor(flagGreen, 1.0f)));
        label.children.push_back(makeRect(label.id + "_mx_white",
            UiRect{rowX + kThird, kFlagY, kThird, kFlagH}, themeColor(flagWhite, 1.0f)));
        label.children.push_back(makeRect(label.id + "_mx_red",
            UiRect{rowX + kThird * 2.0f, kFlagY, kThird, kFlagH},
            themeColor(flagRed, 1.0f)));
        const float mxEmblemCx = rowX + kFlagW * 0.5f;
        const float mxEmblemCy = kFlagY + kFlagH * 0.5f;
        label.children.push_back(makeRect(label.id + "_mx_emblem_top",
            UiRect{mxEmblemCx - 3.0f, mxEmblemCy - 4.0f, 6.0f, 5.0f},
            themeColor(emblemBrown, 1.0f)));
        label.children.push_back(makeRect(label.id + "_mx_emblem_base",
            UiRect{mxEmblemCx - 4.0f, mxEmblemCy + 1.0f, 8.0f, 3.0f},
            themeColor(emblemGreen, 1.0f)));

        // vs between the flags.
        label.children.push_back(makeText(label.id + "_vs",
            UiRect{rowX + kFlagW, kFlagY + 4.0f, kVsGapW, 14.0f}, "VS",
            theme.captionTextHeight, themeColor(theme.foreground, 0.98f),
            TextLayoutOptions{kVsGapW, 0.0f, TextAlign::Center}));

        // england (right): white field with the st george's cross.
        const float enX = rowX + kFlagW + kVsGapW;
        label.children.push_back(makeRect(label.id + "_en_field",
            UiRect{enX, kFlagY, kFlagW, kFlagH}, themeColor(flagWhite, 1.0f)));
        label.children.push_back(makeRect(label.id + "_en_cross_h",
            UiRect{enX, kFlagY + (kFlagH - 6.0f) * 0.5f, kFlagW, 6.0f},
            themeColor(crossRed, 1.0f)));
        label.children.push_back(makeRect(label.id + "_en_cross_v",
            UiRect{enX + (kFlagW - 6.0f) * 0.5f, kFlagY, 6.0f, kFlagH},
            themeColor(crossRed, 1.0f)));

        // typing banner (may be empty on the first frames).
        label.children.push_back(makeText(label.id + "_text",
            UiRect{0.0f, kFlagY + kFlagH + 4.0f, boxW, 14.0f}, entry.label,
            theme.captionTextHeight, themeColor(theme.foreground, 0.98f),
            TextLayoutOptions{boxW, 0.0f, TextAlign::Center}));

        label.children.push_back(makeRect(label.id + "_pin",
            UiRect{boxW * 0.5f - 2.0f, boxH + 1.0f, 4.0f, 4.0f},
            themeColor(crossRed, 0.95f)));
        return label;
    }

    if (entry.focused)
    {
        // celebration bubble: an 8-bit mexico flag over a "viva mexico!" banner.
        const UiThemeColorRole flagGreen{0.0f, 0.40f, 0.26f};
        const UiThemeColorRole flagWhite{0.95f, 0.95f, 0.93f};
        const UiThemeColorRole flagRed{0.80f, 0.08f, 0.15f};
        const UiThemeColorRole emblemBrown{0.42f, 0.27f, 0.13f};
        const UiThemeColorRole emblemGreen{0.26f, 0.42f, 0.12f};

        label.style.color = themeColor(theme.panelInk, 0.86f);
        label.children.reserve(9);

        constexpr float kFlagW = 42.0f;
        constexpr float kFlagH = 24.0f;
        const float flagX = (boxW - kFlagW) * 0.5f;
        constexpr float kFlagY = 5.0f;
        constexpr float kThird = kFlagW / 3.0f;

        label.children.push_back(makeRect(label.id + "_flag_green",
            UiRect{flagX, kFlagY, kThird, kFlagH}, themeColor(flagGreen, 1.0f)));
        label.children.push_back(makeRect(label.id + "_flag_white",
            UiRect{flagX + kThird, kFlagY, kThird, kFlagH}, themeColor(flagWhite, 1.0f)));
        label.children.push_back(makeRect(label.id + "_flag_red",
            UiRect{flagX + kThird * 2.0f, kFlagY, kThird, kFlagH}, themeColor(flagRed, 1.0f)));

        // tiny central emblem (eagle blob over a wreath base) in the white band.
        const float emblemCx = flagX + kThird * 1.5f;
        const float emblemCy = kFlagY + kFlagH * 0.5f;
        label.children.push_back(makeRect(label.id + "_emblem_top",
            UiRect{emblemCx - 3.0f, emblemCy - 4.0f, 6.0f, 5.0f}, themeColor(emblemBrown, 1.0f)));
        label.children.push_back(makeRect(label.id + "_emblem_base",
            UiRect{emblemCx - 4.0f, emblemCy + 1.0f, 8.0f, 3.0f}, themeColor(emblemGreen, 1.0f)));

        label.children.push_back(makeText(label.id + "_text",
            UiRect{0.0f, kFlagY + kFlagH + 3.0f, boxW, 14.0f}, kVivaMexicoBanner,
            theme.captionTextHeight, themeColor(theme.foreground, 0.98f),
            TextLayoutOptions{boxW, 0.0f, TextAlign::Center}));

        label.children.push_back(makeRect(label.id + "_pin",
            UiRect{boxW * 0.5f - 2.0f, boxH + 1.0f, 4.0f, 4.0f}, themeColor(flagRed, 0.95f)));
        return label;
    }

    label.style.color = themeColor(theme.panelInk, 0.72f);
    label.children.reserve(3);

    label.children.push_back(makeRect(
        label.id + "_accent", UiRect{0.0f, 0.0f, kFishWorldLabelW, 3.0f},
        themeColor(theme.accent, 0.88f)));
    label.children.push_back(makeText(
        label.id + "_text", UiRect{0.0f, 6.0f, kFishWorldLabelW, 14.0f},
        entry.label, theme.captionTextHeight, themeColor(theme.foreground, 0.94f),
        TextLayoutOptions{kFishWorldLabelW, 0.0f, TextAlign::Center}));
    label.children.push_back(makeRect(
        label.id + "_pin",
        UiRect{kFishWorldLabelW * 0.5f - 2.0f, kFishWorldLabelH + 1.0f, 4.0f, 4.0f},
        themeColor(theme.accent, 0.74f)));
    return label;
}

UiElement makeFishWorldLabelLayer()
{
    UiElement layer{};
    layer.id = std::string(kFishWorldLabelLayerId);
    layer.kind = UiElementKind::Container;
    layer.rect = UiRect{0.0f, 0.0f, kDesignViewportW, kDesignViewportH};
    layer.enabled = false;
    layer.clipChildren = true;
    return layer;
}

bool fishWorldLabelChildrenEqual(const std::vector<UiElement>& lhs,
                                 const std::vector<UiElement>& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    for (size_t i = 0; i < lhs.size(); ++i)
    {
        const UiElement& a = lhs[i];
        const UiElement& b = rhs[i];
        if (a.id != b.id || a.rect.x != b.rect.x || a.rect.y != b.rect.y ||
            a.rect.width != b.rect.width || a.rect.height != b.rect.height ||
            a.style.color != b.style.color || a.children.size() != b.children.size())
        {
            return false;
        }
        for (size_t childIndex = 0; childIndex < a.children.size(); ++childIndex)
        {
            const UiElement& childA = a.children[childIndex];
            const UiElement& childB = b.children[childIndex];
            if (childA.id != childB.id || childA.text.value != childB.text.value ||
                childA.rect.x != childB.rect.x || childA.rect.y != childB.rect.y ||
                childA.rect.width != childB.rect.width ||
                childA.rect.height != childB.rect.height ||
                childA.style.color != childB.style.color)
            {
                return false;
            }
        }
    }
    return true;
}

UiElement makeBuildCatalogPanel()
{
    const UiTheme& theme = defaultUiTheme();
    UiElement panel{};
    panel.id = "build_catalog_panel";
    panel.kind = UiElementKind::Rect;
    panel.rect = UiRect{kCatalogPanelX, kCatalogPanelY, kCatalogPanelW,
                        kCatalogPanelH};
    panel.style.color = themeColor(theme.panelInk, 0.99f);
    panel.visible = false;
    panel.enabled = false;
    panel.clipChildren = true;
    panel.children.reserve(8);

    panel.children.push_back(makeRect(
        "build_catalog_accent", UiRect{0.0f, 0.0f, kCatalogPanelW, 10.0f},
        themeColor(theme.accent, 1.0f)));
    panel.children.push_back(makeText(
        "build_catalog_title", UiRect{24.0f, 24.0f, 260.0f, 22.0f},
        "BUILD CATALOG", theme.headingTextHeight, themeColor(theme.foreground, 0.94f)));
    panel.children.push_back(ui::makeButton(
        "build_catalog_close", UiRect{364.0f, 22.0f, 72.0f, 26.0f}, "CLOSE",
        std::string(kCloseBuildCatalogAction)));
    panel.children.push_back(makeText(
        "build_catalog_subtitle", UiRect{24.0f, 58.0f, 300.0f, 18.0f},
        "PLANTS DECOR EQUIPMENT", theme.captionTextHeight,
        themeColor(theme.foreground, 0.72f)));

    UiElement listViewport{};
    listViewport.id = std::string(kBuildCatalogListViewportId);
    listViewport.kind = UiElementKind::Container;
    listViewport.rect = UiRect{kCatalogListX, kCatalogListY,
                               kCatalogListW, kCatalogListH};
    listViewport.clipChildren = true;
    listViewport.children.reserve(1);

    UiElement listContent{};
    listContent.id = std::string(kBuildCatalogListContentId);
    listContent.kind = UiElementKind::Container;
    listContent.rect = UiRect{0.0f, 0.0f, kCatalogListW,
                              kCatalogItemFirstY +
                                  static_cast<float>(
                                      overlaySmokeBuildCatalogItems().size()) *
                                      kCatalogItemSpacingY};
    listContent.clipChildren = false;
    const std::span<const BuildCatalogItemDefinition> catalogItems =
        overlaySmokeBuildCatalogItems();
    listContent.children.reserve(catalogItems.size());
    for (size_t itemIndex = 0; itemIndex < catalogItems.size(); ++itemIndex)
    {
        listContent.children.push_back(
            makeCatalogItemButton(catalogItems[itemIndex], itemIndex));
    }
    listViewport.children.push_back(std::move(listContent));
    panel.children.push_back(std::move(listViewport));

    panel.children.push_back(ui::makeButton(
        std::string(kBuildCatalogCancelId), UiRect{24.0f, 224.0f, 176.0f, 26.0f},
        "CANCEL", std::string(kCancelBuildPlacementAction)));
    UiButtonOptions removeOptions{};
    removeOptions.id = std::string(kBuildCatalogRemoveId);
    removeOptions.rect = UiRect{244.0f, 224.0f, 176.0f, 26.0f};
    removeOptions.label = "REMOVE";
    removeOptions.action = std::string(kSelectRemovePlacementAction);
    removeOptions.actionPayload = std::string(kBuildRemoveKey);
    removeOptions.style = makeCatalogRemoveButtonStyle();
    panel.children.push_back(ui::makeButton(std::move(removeOptions)));
    return panel;
}

UiElement makeBuildGhostPreview()
{
    const UiTheme& theme = defaultUiTheme();
    UiElement preview{};
    preview.id = std::string(kBuildGhostPreviewId);
    preview.kind = UiElementKind::Rect;
    preview.rect = UiRect{600.0f, 332.0f, 96.0f, 96.0f};
    preview.style.color = themeColor(theme.accent, 0.18f);
    preview.visible = false;
    preview.enabled = false;
    preview.clipChildren = false;
    preview.children.reserve(8);

    const uint32_t frameColor = themeColor(theme.accent, 0.68f);
    const uint32_t crossColor = themeColor(theme.foreground, 0.70f);
    preview.children.push_back(makeRect(
        "build_ghost_preview_top", UiRect{0.0f, 0.0f, 96.0f, 4.0f}, frameColor));
    preview.children.push_back(makeRect(
        "build_ghost_preview_bottom", UiRect{0.0f, 92.0f, 96.0f, 4.0f}, frameColor));
    preview.children.push_back(makeRect(
        "build_ghost_preview_left", UiRect{0.0f, 0.0f, 4.0f, 96.0f}, frameColor));
    preview.children.push_back(makeRect(
        "build_ghost_preview_right", UiRect{92.0f, 0.0f, 4.0f, 96.0f}, frameColor));
    preview.children.push_back(makeRect(
        "build_ghost_preview_cross_x", UiRect{30.0f, 46.0f, 36.0f, 4.0f}, crossColor));
    preview.children.push_back(makeRect(
        "build_ghost_preview_cross_y", UiRect{46.0f, 30.0f, 4.0f, 36.0f}, crossColor));
    preview.children.push_back(makeText(
        std::string(kBuildGhostPreviewLabelId),
        UiRect{-36.0f, 106.0f, 168.0f, 20.0f}, "PLACING ITEM", theme.captionTextHeight,
        themeColor(theme.foreground, 0.88f),
        TextLayoutOptions{168.0f, 0.0f, TextAlign::Center}));
    preview.children.push_back(makeText(
        std::string(kBuildGhostPreviewHintId),
        UiRect{-56.0f, 126.0f, 208.0f, 18.0f}, "ROT 0 READY",
        theme.captionTextHeight, themeColor(theme.foreground, 0.72f),
        TextLayoutOptions{208.0f, 0.0f, TextAlign::Center}));

    return preview;
}

bool setElementRect(UiElement& root, std::string_view id, UiRect rect)
{
    UiElement* element = findElementById(root, id);
    if (element == nullptr)
    {
        return false;
    }

    const bool changed = element->rect.x != rect.x || element->rect.y != rect.y ||
                         element->rect.width != rect.width ||
                         element->rect.height != rect.height;
    element->rect = rect;
    return changed;
}

bool setElementText(UiElement& root, std::string_view id, std::string value)
{
    UiElement* element = findElementById(root, id);
    if (element == nullptr || element->text.value == value)
    {
        return false;
    }

    element->text.value = std::move(value);
    return true;
}

bool setElementWidth(UiElement& root, std::string_view id, float width)
{
    UiElement* element = findElementById(root, id);
    if (element == nullptr || element->rect.width == width)
    {
        return false;
    }

    element->rect.width = width;
    return true;
}

bool setElementAvailable(UiElement& root, std::string_view id, bool available)
{
    UiElement* element = findElementById(root, id);
    if (element == nullptr)
    {
        return false;
    }

    const bool changed = element->visible != available || element->enabled != available;
    element->visible = available;
    element->enabled = available;
    return changed;
}

bool isCompactOverlayViewport(float width, float height)
{
    return width < kCompactViewportMinW || height < kCompactViewportMinH;
}

float fishListContentExtent(const UiElement& content)
{
    float extent = 0.0f;
    for (const UiElement& row : content.children)
    {
        extent = std::max(extent, row.rect.y + row.rect.height);
    }
    return extent;
}

float fishListMaxScrollOffset(const UiElement& content)
{
    return std::max(0.0f, fishListContentExtent(content) - content.rect.height);
}

bool clampFishListScrollOffset(UiElement& content)
{
    const float previous = content.scrollOffsetY;
    const float finiteOffset = std::isfinite(previous) ? previous : 0.0f;
    content.scrollOffsetY =
        std::clamp(finiteOffset, 0.0f, fishListMaxScrollOffset(content));
    return content.scrollOffsetY != previous;
}

bool applyFishListPanelHeight(UiTree& tree, bool compact)
{
    UiElement* panel = findElementById(tree.root, kFishListPanelId);
    UiElement* content = findElementById(tree.root, kFishListContentId);
    if (panel == nullptr || content == nullptr)
    {
        return false;
    }

    const float margin = compact ? kCompactHudMargin : kHudMargin;
    const float careY = std::max(
        margin, tree.root.rect.height - margin - kCarePanelH);
    const float maxPanelHeight = std::max(
        kFishListContentTop + kFishRowHeight + kFishListBottomPad,
        careY - kHudPanelGap - margin);
    const float rowsBottom =
        std::max(kFishRowHeight, fishListContentExtent(*content));
    const float rosterHeight =
        kFishListContentTop + rowsBottom + kFishListBottomPad;
    const float panelHeight =
        std::min(std::max(kFishPanelH, rosterHeight), maxPanelHeight);

    bool changed = panel->rect.height != panelHeight;
    panel->rect.height = panelHeight;
    const float contentHeight =
        std::max(0.0f, panelHeight - kFishListContentTop - kFishListBottomPad);
    changed = content->rect.height != contentHeight || changed;
    content->rect.height = contentHeight;
    changed = clampFishListScrollOffset(*content) || changed;
    return changed;
}

bool applyCompactHudVisibility(UiTree& tree, bool compact)
{
    const UiElement* catalog = findElementById(tree.root, "build_catalog_panel");
    const bool catalogOpen = catalog != nullptr && catalog->visible;
    bool changed = setElementAvailable(tree.root, "test_panel",
                                       !compact || !catalogOpen);
    changed = setElementAvailable(tree.root, "secondary_panel",
                                  !compact || !catalogOpen) ||
              changed;
    return changed;
}

float normalizedMetricWidth(float value, float minValue, float maxValue, float maxWidth)
{
    if (maxValue <= minValue || maxWidth <= 0.0f)
    {
        return 0.0f;
    }

    const float normalized =
        std::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
    return maxWidth * normalized;
}

std::string formatPercentLabel(float value)
{
    const int percent =
        static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 100.0f));
    return std::to_string(percent) + "%";
}

std::string formatTemperatureLabel(float temperatureC)
{
    const int degrees =
        static_cast<int>(std::lround(std::clamp(temperatureC, 0.0f, 40.0f)));
    return std::to_string(degrees) + "C";
}

std::string creatureFeedButtonLabel(const UiOverlaySmokeHudStatus& status)
{
    switch (status.creatureFeedState)
    {
    case UiCreatureFeedState::NoCreature:
        return "NO CREATURE";
    case UiCreatureFeedState::CreatureUnavailable:
        return "CREATURE UNAVAILABLE";
    case UiCreatureFeedState::Ready:
        return "FEED";
    case UiCreatureFeedState::NotHungry:
        return "NOT HUNGRY";
    case UiCreatureFeedState::FedFeedback:
        return "FED!";
    case UiCreatureFeedState::Cooldown:
    {
        const float seconds =
            std::ceil(std::max(0.0f, status.creatureFeedCooldownRemaining) * 10.0f) /
            10.0f;
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(1) << "FEED " << seconds << "S";
        return stream.str();
    }
    }
    return "FEED";
}

} // namespace

bool isRuntimeUiScreenRegistered(std::string_view screenId)
{
    return screenId == kOverlaySmokeScreenId || screenId == kPauseScreenId ||
           screenId == kMainMenuScreenId || screenId == kMainMenuOptionsScreenId ||
           screenId == kCollectionCodexScreenId;
}

std::span<const BuildCatalogItemDefinition> overlaySmokeBuildCatalogItems()
{
    return runtimeBuildCatalogItems();
}

float overlaySmokeBuildCatalogMaxScrollOffset()
{
    const float contentHeight =
        kCatalogItemFirstY +
        static_cast<float>(overlaySmokeBuildCatalogItems().size()) *
            kCatalogItemSpacingY;
    return std::max(0.0f, contentHeight - kCatalogListH);
}

float overlaySmokeFishListMaxScrollOffset(const UiTree& tree)
{
    const UiElement* content = findElementById(tree.root, kFishListContentId);
    return content != nullptr ? fishListMaxScrollOffset(*content) : 0.0f;
}

float overlaySmokeFishListScrollOffset(const UiTree& tree)
{
    const UiElement* content = findElementById(tree.root, kFishListContentId);
    if (content == nullptr || !std::isfinite(content->scrollOffsetY))
    {
        return 0.0f;
    }
    return content->scrollOffsetY;
}

bool setOverlaySmokeFishListScrollOffset(UiTree& tree, float scrollOffsetY)
{
    UiElement* content = findElementById(tree.root, kFishListContentId);
    if (content == nullptr)
    {
        return false;
    }

    const float previous = content->scrollOffsetY;
    const float finiteOffset = std::isfinite(scrollOffsetY) ? scrollOffsetY : 0.0f;
    content->scrollOffsetY =
        std::clamp(finiteOffset, 0.0f, fishListMaxScrollOffset(*content));
    return content->scrollOffsetY != previous;
}

bool handleOverlaySmokeFishListWheel(UiTree& tree, float pointerX, float pointerY,
                                     double wheelYOffset)
{
    UiElement* content = findElementById(tree.root, kFishListContentId);
    if (content == nullptr || !content->visible || !content->enabled ||
        !std::isfinite(pointerX) || !std::isfinite(pointerY) ||
        !std::isfinite(wheelYOffset) || wheelYOffset == 0.0)
    {
        return false;
    }

    const UiRect& rect = content->computedRect;
    const bool inside = rect.width > 0.0f && rect.height > 0.0f &&
                        pointerX >= rect.x && pointerY >= rect.y &&
                        pointerX < rect.x + rect.width &&
                        pointerY < rect.y + rect.height;
    if (!inside)
    {
        return false;
    }

    setOverlaySmokeFishListScrollOffset(
        tree, content->scrollOffsetY -
                  static_cast<float>(wheelYOffset) * kFishListWheelStep);
    return true;
}

UiTree makeRuntimeUiScreenTree(std::string_view screenId)
{
    if (screenId == kOverlaySmokeScreenId)
    {
        return makeOverlaySmokeTree();
    }
    if (screenId == kPauseScreenId)
    {
        return makePauseScreenTree();
    }
    if (screenId == kMainMenuScreenId)
    {
        return makeMainMenuScreenTree();
    }
    if (screenId == kMainMenuOptionsScreenId)
    {
        return makeMainMenuOptionsScreenTree();
    }
    if (screenId == kCollectionCodexScreenId)
    {
        return makeCollectionCodexScreenTree();
    }

    return UiTree{};
}

void setOverlaySmokeFishList(UiTree& tree, std::span<const UiFishListEntry> entries)
{
    UiElement* content = findElementById(tree.root, kFishListContentId);
    if (content == nullptr)
    {
        return;
    }

    content->children.clear();
    content->children.reserve(entries.size());
    bool anyFocused = false;
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const UiFishListEntry& entry = entries[index];
        anyFocused = anyFocused || entry.focused;
        UiButtonOptions options{};
        options.id = "fish_row_" + std::to_string(entry.id);
        options.rect = UiRect{0.0f, static_cast<float>(index) * kFishRowStride, 184.0f,
                              kFishRowHeight};
        options.label = entry.label;
        options.action = std::string(kFocusFishAction);
        options.actionPayload = std::to_string(entry.id);
        options.style = makeFishRowButtonStyle(entry.focused);
        content->children.push_back(ui::makeButton(std::move(options)));
    }

    // the standard sidebar still grows to fit ordinary rosters. oversized
    // schools, and every compact viewport, use a clipped scrolling region that
    // stops before the care panel. rebuilding a smaller roster clamps any old
    // offset so the list can never be left on an empty tail.
    applyFishListPanelHeight(
        tree, isCompactOverlayViewport(tree.root.rect.width, tree.root.rect.height));

    UiElement* releaseButton = findElementById(tree.root, kFishFocusReleaseButtonId);
    if (releaseButton != nullptr)
    {
        releaseButton->enabled = anyFocused;
        applyButtonStyle(*releaseButton, makeReleaseButtonStyle(anyFocused));
    }
}

bool setOverlaySmokeFishWorldLabels(UiTree& tree,
                                    std::span<const UiFishWorldLabelEntry> entries)
{
    UiElement* layer = findElementById(tree.root, kFishWorldLabelLayerId);
    if (layer == nullptr)
    {
        return false;
    }

    std::vector<UiElement> nextChildren;
    std::vector<UiRect> acceptedRects;
    nextChildren.reserve(entries.size());
    acceptedRects.reserve(entries.size());

    const auto appendLabels = [&](bool focusedPass) {
        for (const UiFishWorldLabelEntry& entry : entries)
        {
            if (entry.id == 0 || entry.label.empty() || entry.focused != focusedPass)
            {
                continue;
            }

            UiElement candidate = makeFishWorldLabel(entry, layer->rect);
            bool overlaps = false;
            for (const UiRect& accepted : acceptedRects)
            {
                if (rectsOverlapWithPadding(candidate.rect, accepted, 6.0f))
                {
                    overlaps = true;
                    break;
                }
            }
            if (!entry.focused && overlaps)
            {
                continue;
            }

            acceptedRects.push_back(candidate.rect);
            nextChildren.push_back(std::move(candidate));
        }
    };

    appendLabels(true);
    appendLabels(false);

    const bool changed = !fishWorldLabelChildrenEqual(layer->children, nextChildren);
    layer->children = std::move(nextChildren);
    computeLayout(*layer);
    return changed;
}

bool setOverlaySmokeViewport(UiTree& tree, float width, float height)
{
    const float viewportW = std::max(640.0f, width);
    const float viewportH = std::max(360.0f, height);
    const bool compact = isCompactOverlayViewport(viewportW, viewportH);
    const UiElement* previousSmokePanel = findElementById(tree.root, "test_panel");
    const bool wasCompact = previousSmokePanel != nullptr &&
                            previousSmokePanel->rect.width == kCompactHudPanelW;
    const float margin = compact ? kCompactHudMargin : kHudMargin;
    const float smokePanelW = compact ? kCompactHudPanelW : kSmokePanelW;
    bool changed = false;

    const UiRect rootRect{0.0f, 0.0f, viewportW, viewportH};
    if (tree.root.rect.x != rootRect.x || tree.root.rect.y != rootRect.y ||
        tree.root.rect.width != rootRect.width ||
        tree.root.rect.height != rootRect.height)
    {
        tree.root.rect = rootRect;
        changed = true;
    }

    changed = setElementRect(tree.root, "test_panel",
                             UiRect{margin, margin, smokePanelW, kSmokePanelH}) ||
              changed;
    changed = setElementRect(
                  tree.root, "accent_top",
                  UiRect{0.0f, 0.0f, smokePanelW, 12.0f}) ||
              changed;
    changed = setElementRect(
                  tree.root, "accent_left",
                  UiRect{0.0f, 0.0f, 8.0f, kSmokePanelH}) ||
              changed;
    changed = setElementRect(
                  tree.root, "accent_right",
                  UiRect{smokePanelW - 4.0f, 0.0f, 4.0f, kSmokePanelH}) ||
              changed;
    changed = setElementRect(
                  tree.root, "content_stack",
                  compact ? UiRect{16.0f, 18.0f, smokePanelW - 32.0f,
                                   kSmokePanelH - 18.0f}
                          : UiRect{24.0f, 18.0f, smokePanelW - 52.0f,
                                   kSmokePanelH - 18.0f}) ||
              changed;

    const float statusLabelW = compact ? 54.0f : 82.0f;
    const float statusValueW = compact ? 66.0f : 102.0f;
    changed = setElementWidth(tree.root, "status_row_fish_label", statusLabelW) ||
              changed;
    changed = setElementWidth(tree.root, "status_row_time_label", statusLabelW) ||
              changed;
    changed = setElementWidth(tree.root, "status_row_clean_label", statusLabelW) ||
              changed;
    changed = setElementWidth(tree.root, "status_row_fish_value", statusValueW) ||
              changed;
    changed = setElementWidth(tree.root, "status_row_time_value", statusValueW) ||
              changed;
    changed = setElementWidth(tree.root, "status_row_clean_value", statusValueW) ||
              changed;
    changed = setElementWidth(tree.root, "status_row_fish_meter",
                              compact ? 88.0f : 156.0f) ||
              changed;
    changed = setElementWidth(tree.root, "status_row_time_meter",
                              compact ? 88.0f : 132.0f) ||
              changed;
    if (compact != wasCompact)
    {
        UiElement* cleanMeter =
            findElementById(tree.root, "status_row_clean_meter");
        if (cleanMeter != nullptr)
        {
            const float sourceMax = compact ? 128.0f : 88.0f;
            const float targetMax = compact ? 88.0f : 128.0f;
            const float normalizedWidth = sourceMax > 0.0f
                ? std::clamp(cleanMeter->rect.width / sourceMax, 0.0f, 1.0f)
                : 0.0f;
            changed = setElementWidth(tree.root, "status_row_clean_meter",
                                      normalizedWidth * targetMax) ||
                      changed;
        }
    }

    const float catalogPanelX = margin;
    const float catalogPanelY = compact
        ? std::max(margin, (viewportH - kCatalogPanelH) * 0.5f)
        : kCatalogPanelY;
    const float catalogPanelW = compact ? kCompactHudPanelW : kCatalogPanelW;
    const float catalogListX = compact ? kCompactCatalogListX : kCatalogListX;
    const float catalogListW = compact ? kCompactCatalogListW : kCatalogListW;
    changed = setElementRect(
                  tree.root, "build_catalog_panel",
                  UiRect{catalogPanelX, catalogPanelY, catalogPanelW,
                         kCatalogPanelH}) ||
              changed;
    changed = setElementRect(
                  tree.root, "build_catalog_accent",
                  UiRect{0.0f, 0.0f, catalogPanelW, 10.0f}) ||
              changed;
    changed = setElementRect(
                  tree.root, "build_catalog_title",
                  compact ? UiRect{16.0f, 24.0f, 216.0f, 22.0f}
                          : UiRect{24.0f, 24.0f, 260.0f, 22.0f}) ||
              changed;
    changed = setElementRect(
                  tree.root, "build_catalog_close",
                  compact ? UiRect{252.0f, 22.0f, 72.0f, 26.0f}
                          : UiRect{364.0f, 22.0f, 72.0f, 26.0f}) ||
              changed;
    changed = setElementRect(
                  tree.root, "build_catalog_subtitle",
                  compact ? UiRect{16.0f, 58.0f, 308.0f, 18.0f}
                          : UiRect{24.0f, 58.0f, 300.0f, 18.0f}) ||
              changed;
    changed = setElementRect(
                  tree.root, kBuildCatalogListViewportId,
                  UiRect{catalogListX, kCatalogListY, catalogListW,
                         kCatalogListH}) ||
              changed;
    const float catalogContentHeight =
        kCatalogItemFirstY +
        static_cast<float>(overlaySmokeBuildCatalogItems().size()) *
            kCatalogItemSpacingY;
    changed = setElementRect(
                  tree.root, kBuildCatalogListContentId,
                  UiRect{0.0f, 0.0f, catalogListW, catalogContentHeight}) ||
              changed;
    const std::span<const BuildCatalogItemDefinition> catalogItems =
        overlaySmokeBuildCatalogItems();
    for (size_t itemIndex = 0; itemIndex < catalogItems.size(); ++itemIndex)
    {
        const BuildCatalogItemDefinition& item = catalogItems[itemIndex];
        changed = setElementRect(
                      tree.root, item.id,
                      UiRect{kCatalogItemX,
                             kCatalogItemFirstY +
                                 static_cast<float>(itemIndex) *
                                     kCatalogItemSpacingY,
                             compact ? catalogListW : item.rowWidth,
                             kCatalogItemHeight}) ||
                  changed;
    }
    changed = setElementRect(
                  tree.root, kBuildCatalogCancelId,
                  compact ? UiRect{16.0f, 224.0f,
                                   kCompactCatalogFooterButtonW, 26.0f}
                          : UiRect{24.0f, 224.0f, 176.0f, 26.0f}) ||
              changed;
    changed = setElementRect(
                  tree.root, kBuildCatalogRemoveId,
                  compact ? UiRect{178.0f, 224.0f,
                                   kCompactCatalogFooterButtonW, 26.0f}
                          : UiRect{244.0f, 224.0f, 176.0f, 26.0f}) ||
              changed;

    changed = applyFishListPanelHeight(tree, compact) || changed;
    const float fishX = std::max(margin, viewportW - margin - kFishPanelW);
    const UiElement* fishPanel = findElementById(tree.root, kFishListPanelId);
    const float fishPanelH = fishPanel != nullptr ? fishPanel->rect.height : kFishPanelH;
    changed = setElementRect(tree.root, kFishListPanelId,
                             UiRect{fishX, margin, kFishPanelW, fishPanelH}) ||
              changed;
    const float careX =
        std::max(margin, viewportW - margin - kCarePanelW);
    const float careY =
        std::max(margin, viewportH - margin - kCarePanelH);
    changed = setElementRect(
                  tree.root, "primary_creature_care_panel",
                  UiRect{careX, careY, kCarePanelW, kCarePanelH}) ||
              changed;
    changed = setElementRect(tree.root, kFishWorldLabelLayerId,
                             UiRect{0.0f, 0.0f, viewportW, viewportH}) ||
              changed;

    const float secondaryY = compact
        ? std::max(margin, viewportH - margin - kSecondaryPanelH)
        : std::max(kSmokePanelY + kSmokePanelH + 16.0f,
                   viewportH - kHudBottomMargin - kSecondaryPanelH);
    changed = setElementRect(tree.root, "secondary_panel",
                             UiRect{margin, secondaryY, smokePanelW,
                                    kSecondaryPanelH}) ||
              changed;
    changed = setElementRect(
                  tree.root, "secondary_stack",
                  compact ? UiRect{16.0f, 10.0f, smokePanelW - 32.0f, 28.0f}
                          : UiRect{16.0f, 10.0f, kSmokePanelW - 32.0f, 28.0f}) ||
              changed;
    changed = setElementWidth(tree.root, "secondary_group_o2",
                              compact ? 86.0f : 126.0f) ||
              changed;
    changed = setElementWidth(tree.root, "secondary_group_temp",
                              compact ? 96.0f : 150.0f) ||
              changed;
    changed = setElementWidth(tree.root, "secondary_group_flow",
                              compact ? 106.0f : 130.0f) ||
              changed;
    changed = setElementWidth(tree.root, "secondary_label_o2",
                              64.0f) ||
              changed;
    changed = setElementWidth(tree.root, "secondary_label_temp",
                              74.0f) ||
              changed;
    changed = setElementWidth(tree.root, "secondary_label_flow",
                              84.0f) ||
              changed;
    if (compact != wasCompact)
    {
        const auto scaleTelemetryMeter =
            [&tree, compact, &changed](std::string_view id, float standardMax) {
                UiElement* meter = findElementById(tree.root, id);
                if (meter == nullptr)
                {
                    return;
                }
                const float sourceMax =
                    compact ? standardMax : kCompactTelemetryMeterMaxW;
                const float targetMax =
                    compact ? kCompactTelemetryMeterMaxW : standardMax;
                const float normalizedWidth = sourceMax > 0.0f
                    ? std::clamp(meter->rect.width / sourceMax, 0.0f, 1.0f)
                    : 0.0f;
                changed = setElementWidth(tree.root, id,
                                          normalizedWidth * targetMax) ||
                          changed;
            };
        scaleTelemetryMeter("secondary_metric_a", kTelemetryOxygenMaxW);
        scaleTelemetryMeter("secondary_metric_b", kTelemetryTemperatureMaxW);
        scaleTelemetryMeter("secondary_metric_c", kTelemetryFlowMaxW);
    }
    changed = applyCompactHudVisibility(tree, compact) || changed;

    return changed;
}

bool setMainMenuViewport(UiTree& tree, float width, float height)
{
    if (tree.root.id != "ui_main_menu_root" &&
        tree.root.id != "ui_main_menu_options_root")
    {
        return false;
    }

    const float viewportW = std::max(640.0f, width);
    const float viewportH = std::max(360.0f, height);
    const UiRect viewportRect{0.0f, 0.0f, viewportW, viewportH};
    bool changed = false;

    if (tree.root.rect.x != viewportRect.x || tree.root.rect.y != viewportRect.y ||
        tree.root.rect.width != viewportRect.width ||
        tree.root.rect.height != viewportRect.height)
    {
        tree.root.rect = viewportRect;
        changed = true;
    }

    changed = setElementRect(tree.root, "main_menu_backdrop", viewportRect) || changed;
    changed =
        setElementRect(tree.root, "main_menu_options_backdrop", viewportRect) || changed;

    if (tree.root.id == "ui_main_menu_root")
    {
        constexpr float kPanelW = 456.0f;
        constexpr float kPanelH = 320.0f;
        const bool constrained = viewportW < 720.0f || viewportH < 440.0f;
        const float panelX = constrained ? (viewportW - kPanelW) * 0.5f : 72.0f;
        const float panelY = constrained ? (viewportH - kPanelH) * 0.5f : 84.0f;
        changed = setElementRect(
                      tree.root, "main_menu_panel",
                      UiRect{std::max(0.0f, panelX), std::max(0.0f, panelY),
                             kPanelW, kPanelH}) ||
                  changed;
    }
    else
    {
        constexpr float kPanelW = 420.0f;
        constexpr float kPanelH = 300.0f;
        const bool constrained = viewportW < 850.0f || viewportH < 470.0f;
        const float panelX = constrained ? (viewportW - kPanelW) * 0.5f : 430.0f;
        const float panelY = constrained ? (viewportH - kPanelH) * 0.5f : 170.0f;
        changed = setElementRect(
                      tree.root, "main_menu_options_panel",
                      UiRect{std::max(0.0f, panelX), std::max(0.0f, panelY),
                             kPanelW, kPanelH}) ||
                  changed;
    }
    return changed;
}

bool setPauseScreenViewport(UiTree& tree, float width, float height)
{
    if (tree.root.id != "ui_pause_smoke_root")
    {
        return false;
    }

    const float viewportW = std::max(640.0f, width);
    const float viewportH = std::max(360.0f, height);
    const UiRect viewportRect{0.0f, 0.0f, viewportW, viewportH};
    bool changed = false;

    if (tree.root.rect.x != viewportRect.x || tree.root.rect.y != viewportRect.y ||
        tree.root.rect.width != viewportRect.width ||
        tree.root.rect.height != viewportRect.height)
    {
        tree.root.rect = viewportRect;
        changed = true;
    }

    changed = setElementRect(tree.root, kPauseBackdropId, viewportRect) || changed;
    changed =
        setElementRect(tree.root, "pause_panel",
                       UiRect{(viewportW - kPausePanelW) * 0.5f,
                              (viewportH - kPausePanelH) * 0.5f, kPausePanelW,
                              kPausePanelH}) ||
        changed;
    return changed;
}

bool setRuntimeUiOverlayViewport(UiTree& tree, float width, float height)
{
    bool changed = setPauseScreenViewport(tree, width, height);
    changed = setMainMenuViewport(tree, width, height) || changed;
    changed = setCollectionCodexViewport(tree, width, height) || changed;
    return changed;
}

UiTree makePauseScreenTree()
{
    const UiTheme& theme = defaultUiTheme();

    UiElement root{};
    root.id = "ui_pause_smoke_root";
    root.kind = UiElementKind::Container;
    root.rect = UiRect{0.0f, 0.0f, kDesignViewportW, kDesignViewportH};
    root.clipChildren = false;
    root.children.reserve(2);

    root.children.push_back(makeRect(
        std::string(kPauseBackdropId),
        UiRect{0.0f, 0.0f, kDesignViewportW, kDesignViewportH},
        themeColor(theme.panelInk, 0.42f)));

    UiElement panel{};
    panel.id = "pause_panel";
    panel.kind = UiElementKind::Rect;
    panel.rect = UiRect{(kDesignViewportW - kPausePanelW) * 0.5f,
                        (kDesignViewportH - kPausePanelH) * 0.5f,
                        kPausePanelW, kPausePanelH};
    panel.style.color = themeColor(theme.panelInk, 0.96f);
    panel.clipChildren = false;
    panel.children.reserve(5);

    panel.children.push_back(makeRect(
        "pause_accent", UiRect{0.0f, 0.0f, kPausePanelW, 10.0f},
        themeColor(theme.accent, 1.0f)));
    panel.children.push_back(makeText(
        "pause_title", UiRect{32.0f, 32.0f, 256.0f, 28.0f}, "PAUSED",
        theme.headingTextHeight, themeColor(theme.foreground, 0.94f),
        TextLayoutOptions{256.0f, 0.0f, TextAlign::Center}));

    UiButtonOptions resumeOptions{};
    resumeOptions.id = std::string(kPauseResumeButtonId);
    resumeOptions.rect = UiRect{64.0f, 82.0f, 192.0f, 30.0f};
    resumeOptions.label = "RESUME";
    resumeOptions.action = std::string(kResumeGameAction);
    resumeOptions.style = makeMenuButtonStyle(true);
    resumeOptions.pixelHeight = theme.captionTextHeight;
    panel.children.push_back(ui::makeButton(std::move(resumeOptions)));

    UiButtonOptions options{};
    options.id = std::string(kPauseOptionsButtonId);
    options.rect = UiRect{64.0f, 124.0f, 192.0f, 30.0f};
    options.label = "OPTIONS";
    options.action = std::string(kOpenMainMenuOptionsAction);
    options.style = makeMenuButtonStyle(false);
    options.pixelHeight = theme.captionTextHeight;
    panel.children.push_back(ui::makeButton(std::move(options)));

    UiButtonOptions mainMenu{};
    mainMenu.id = std::string(kPauseMainMenuButtonId);
    mainMenu.rect = UiRect{64.0f, 166.0f, 192.0f, 30.0f};
    mainMenu.label = "MAIN MENU";
    mainMenu.action = std::string(kReturnMainMenuAction);
    mainMenu.style = makeMenuButtonStyle(false);
    mainMenu.pixelHeight = theme.captionTextHeight;
    panel.children.push_back(ui::makeButton(std::move(mainMenu)));

    root.children.push_back(std::move(panel));

    UiTree tree{};
    tree.root = std::move(root);
    return tree;
}

UiTree makeMainMenuScreenTree()
{
    const UiTheme& theme = defaultUiTheme();

    UiElement root{};
    root.id = "ui_main_menu_root";
    root.kind = UiElementKind::Container;
    root.rect = UiRect{0.0f, 0.0f, kDesignViewportW, kDesignViewportH};
    root.clipChildren = false;
    root.children.reserve(2);

    root.children.push_back(makeRect(
        "main_menu_backdrop", UiRect{0.0f, 0.0f, kDesignViewportW, kDesignViewportH},
        themeColor(theme.panelInk, 0.30f)));

    UiElement panel{};
    panel.id = "main_menu_panel";
    panel.kind = UiElementKind::Rect;
    panel.rect = UiRect{72.0f, 84.0f, 456.0f, 320.0f};
    panel.style.color = themeColor(theme.panelInk, 0.94f);
    panel.clipChildren = false;
    panel.children.reserve(8);

    panel.children.push_back(makeRect(
        "main_menu_accent_top", UiRect{0.0f, 0.0f, 456.0f, 12.0f},
        themeColor(theme.accent, 1.0f)));
    panel.children.push_back(makeRect(
        "main_menu_accent_left", UiRect{0.0f, 0.0f, 8.0f, 320.0f},
        themeColor(theme.accentAlt, 1.0f)));
    panel.children.push_back(makeText(
        "main_menu_title", UiRect{34.0f, 34.0f, 370.0f, 30.0f},
        "AQUASPHAERA", theme.titleTextHeight,
        themeColor(theme.foreground, 0.95f)));
    panel.children.push_back(makeText(
        "main_menu_subtitle", UiRect{36.0f, 74.0f, 300.0f, 18.0f},
        "FISHBOWL READY", theme.captionTextHeight,
        themeColor(theme.foreground, 0.70f)));
    panel.children.push_back(makeRect(
        "main_menu_rule", UiRect{34.0f, 110.0f, 360.0f, 8.0f},
        themeColor(theme.foreground, 0.78f)));

    UiButtonOptions startOptions{};
    startOptions.id = std::string(kMainMenuStartButtonId);
    startOptions.rect = UiRect{44.0f, 150.0f, 332.0f, 40.0f};
    startOptions.label = "START AQUARIUM";
    startOptions.action = std::string(kStartAquariumAction);
    startOptions.style = makeMenuButtonStyle(true);
    startOptions.pixelHeight = theme.bodyTextHeight;
    panel.children.push_back(ui::makeButton(std::move(startOptions)));

    UiButtonOptions options{};
    options.id = std::string(kMainMenuOptionsButtonId);
    options.rect = UiRect{44.0f, 206.0f, 332.0f, 32.0f};
    options.label = "OPTIONS";
    options.action = std::string(kOpenMainMenuOptionsAction);
    options.style = makeMenuButtonStyle(false);
    options.pixelHeight = theme.captionTextHeight;
    panel.children.push_back(ui::makeButton(std::move(options)));

    panel.children.push_back(makeText(
        "main_menu_build_tag", UiRect{44.0f, 270.0f, 300.0f, 18.0f},
        "AQUARIUM SYSTEM ONLINE", theme.captionTextHeight,
        themeColor(theme.foreground, 0.58f)));

    root.children.push_back(std::move(panel));

    UiTree tree{};
    tree.root = std::move(root);
    return tree;
}

UiTree makeMainMenuOptionsScreenTree()
{
    const UiTheme& theme = defaultUiTheme();

    UiElement root{};
    root.id = "ui_main_menu_options_root";
    root.kind = UiElementKind::Container;
    root.rect = UiRect{0.0f, 0.0f, kDesignViewportW, kDesignViewportH};
    root.clipChildren = false;
    root.children.reserve(2);

    root.children.push_back(makeRect(
        "main_menu_options_backdrop", UiRect{0.0f, 0.0f, kDesignViewportW, kDesignViewportH},
        themeColor(theme.panelInk, 0.48f)));

    UiElement panel{};
    panel.id = "main_menu_options_panel";
    panel.kind = UiElementKind::Rect;
    panel.rect = UiRect{430.0f, 170.0f, 420.0f, 300.0f};
    panel.style.color = themeColor(theme.panelInk, 0.96f);
    panel.clipChildren = false;
    panel.children.reserve(11);

    panel.children.push_back(makeRect(
        "main_menu_options_accent", UiRect{0.0f, 0.0f, 420.0f, 10.0f},
        themeColor(theme.accent, 1.0f)));
    panel.children.push_back(makeText(
        "main_menu_options_title", UiRect{28.0f, 30.0f, 240.0f, 24.0f},
        "OPTIONS", theme.headingTextHeight,
        themeColor(theme.foreground, 0.94f)));
    panel.children.push_back(makeText(
        "main_menu_options_subtitle", UiRect{30.0f, 60.0f, 240.0f, 16.0f},
        "AQUARIUM SETTINGS", theme.captionTextHeight,
        themeColor(theme.foreground, 0.62f)));
    UiButtonOptions labelOptions{};
    labelOptions.id = std::string(kMainMenuOptionLabelsButtonId);
    labelOptions.rect = UiRect{38.0f, 92.0f, 330.0f, 28.0f};
    labelOptions.label = labelsOptionText(true);
    labelOptions.action = std::string(kToggleFishWorldLabelsAction);
    labelOptions.style = makeOptionToggleStyle(true);
    labelOptions.pixelHeight = theme.captionTextHeight;
    panel.children.push_back(ui::makeButton(std::move(labelOptions)));
    panel.children.push_back(makeHudStatusRow(
        "main_menu_option_camera", "CAMERA", "CINEMATIC",
        themeColor(theme.info, 0.92f), 92.0f));
    panel.children.back().rect = UiRect{38.0f, 132.0f, 330.0f, 16.0f};
    panel.children.push_back(makeHudStatusRow(
        "main_menu_option_pixel", "PIXEL", "ON",
        themeColor(theme.neutral, 0.92f), 128.0f));
    panel.children.back().rect = UiRect{38.0f, 162.0f, 330.0f, 16.0f};
    panel.children.push_back(makeHudStatusRow(
        "main_menu_option_build", "BUILD", "READY",
        themeColor(theme.positive, 0.92f), 104.0f));
    panel.children.back().rect = UiRect{38.0f, 192.0f, 330.0f, 16.0f};

    UiButtonOptions backOptions{};
    backOptions.id = std::string(kMainMenuOptionsBackButtonId);
    backOptions.rect = UiRect{140.0f, 238.0f, 140.0f, 32.0f};
    backOptions.label = "BACK";
    backOptions.action = std::string(kCloseMainMenuOptionsAction);
    backOptions.style = makeMenuButtonStyle(false);
    backOptions.pixelHeight = theme.captionTextHeight;
    panel.children.push_back(ui::makeButton(std::move(backOptions)));

    root.children.push_back(std::move(panel));

    UiTree tree{};
    tree.root = std::move(root);
    return tree;
}

bool setMainMenuOptionsState(UiTree& tree, const UiMainMenuOptionsState& state)
{
    if (tree.root.id != "ui_main_menu_options_root")
    {
        return false;
    }

    UiElement* labels = findElementById(tree.root, kMainMenuOptionLabelsButtonId);
    if (labels == nullptr)
    {
        return false;
    }

    bool changed = false;
    const std::string text = labelsOptionText(state.fishWorldLabelsVisible);
    if (labels->text.value != text)
    {
        labels->text.value = text;
        changed = true;
    }

    const UiButtonStyle style =
        makeOptionToggleStyle(state.fishWorldLabelsVisible);
    const uint32_t previousColor = labels->style.color;
    const uint32_t previousTextColor = labels->style.textColor;
    applyButtonStyle(*labels, style);
    changed = changed || previousColor != labels->style.color ||
              previousTextColor != labels->style.textColor;
    return changed;
}

bool setOverlaySmokeHudStatus(UiTree& tree, const UiOverlaySmokeHudStatus& status)
{
    if (tree.root.id != "ui_overlay_smoke_root")
    {
        return false;
    }

    bool changed = false;
    changed = setElementText(tree.root, "status_row_fish_value",
                             std::to_string(status.fishCount)) ||
              changed;
    changed = setElementText(tree.root, "status_row_time_value",
                             status.timeOfDayLabel) ||
              changed;
    changed = setElementText(tree.root, "status_row_clean_value",
                             formatPercentLabel(status.waterCleanliness)) ||
              changed;
    const float cleanlinessMeterMaxW =
        isCompactOverlayViewport(tree.root.rect.width, tree.root.rect.height)
            ? 88.0f
            : 128.0f;
    changed = setElementWidth(
                  tree.root, "status_row_clean_meter",
                  normalizedMetricWidth(status.waterCleanliness, 0.0f, 1.0f,
                                        cleanlinessMeterMaxW)) ||
              changed;
    const UiTheme& theme = defaultUiTheme();
    const bool maintenanceAvailable =
        std::clamp(status.waterCleanliness, 0.0f, 1.0f) <
            kMaintainWaterCleanlinessThreshold ||
        std::clamp(status.waterOxygen, 0.0f, 1.0f) <
            kMaintainWaterOxygenBaseline;
    const uint32_t cleanValueColor =
        status.waterMaintenanceFeedbackActive
            ? themeColor(theme.positive, 0.98f)
            : themeColor(theme.foreground, 0.92f);
    const uint32_t cleanMeterColor =
        status.waterMaintenanceFeedbackActive
            ? themeColor(theme.positive, 0.98f)
            : themeColor(theme.neutral, 0.95f);
    if (UiElement* cleanValue =
            findElementById(tree.root, "status_row_clean_value"))
    {
        changed = changed || cleanValue->style.color != cleanValueColor;
        cleanValue->style.color = cleanValueColor;
    }
    if (UiElement* cleanMeter =
            findElementById(tree.root, "status_row_clean_meter"))
    {
        changed = changed || cleanMeter->style.color != cleanMeterColor;
        cleanMeter->style.color = cleanMeterColor;
    }
    if (UiElement* maintainButton =
            findElementById(tree.root, kMaintainWaterButtonId))
    {
        const bool enabled =
            maintenanceAvailable && !status.waterMaintenanceFeedbackActive;
        const std::string label =
            status.waterMaintenanceFeedbackActive ? "WATER STABLE"
                                                  : "MAINTAIN WATER";
        changed = setElementText(tree.root, kMaintainWaterButtonId, label) ||
                  changed;
        changed = changed || maintainButton->enabled != enabled;
        maintainButton->enabled = enabled;
        const UiButtonStyle style = makeWaterMaintenanceButtonStyle(
            maintenanceAvailable, status.waterMaintenanceFeedbackActive);
        const uint32_t previousColor = maintainButton->style.color;
        const uint32_t previousTextColor = maintainButton->style.textColor;
        const uint32_t previousDisabledColor = maintainButton->style.disabledColor;
        const uint32_t previousDisabledTextColor =
            maintainButton->style.disabledTextColor;
        applyButtonStyle(*maintainButton, style);
        changed = changed || previousColor != maintainButton->style.color ||
                  previousTextColor != maintainButton->style.textColor ||
                  previousDisabledColor != maintainButton->style.disabledColor ||
                  previousDisabledTextColor !=
                      maintainButton->style.disabledTextColor;
    }
    changed = setElementText(tree.root, "secondary_label_o2",
                             "O2 " + formatPercentLabel(status.waterOxygen)) ||
              changed;
    changed = setElementText(tree.root, "secondary_label_temp",
                             "TEMP " + formatTemperatureLabel(status.waterTemperatureC)) ||
              changed;
    changed = setElementText(tree.root, "secondary_label_flow",
                             "FLOW " + formatPercentLabel(status.waterFlow)) ||
              changed;
    const bool compact =
        isCompactOverlayViewport(tree.root.rect.width, tree.root.rect.height);
    changed = setElementWidth(
                  tree.root, "secondary_metric_a",
                  normalizedMetricWidth(status.waterOxygen, 0.0f, 1.0f,
                                        compact ? kCompactTelemetryMeterMaxW
                                                : kTelemetryOxygenMaxW)) ||
              changed;
    changed = setElementWidth(
                  tree.root, "secondary_metric_b",
                  normalizedMetricWidth(status.waterTemperatureC, 0.0f,
                                        kWaterTemperatureMaxC,
                                        compact ? kCompactTelemetryMeterMaxW
                                                : kTelemetryTemperatureMaxW)) ||
              changed;
    changed = setElementWidth(
                  tree.root, "secondary_metric_c",
                  normalizedMetricWidth(status.waterFlow, 0.0f, 1.0f,
                                        compact ? kCompactTelemetryMeterMaxW
                                                : kTelemetryFlowMaxW)) ||
              changed;

    changed = setElementText(tree.root, "care_name", status.primaryCreatureName) ||
              changed;
    const auto setCareMetric = [&](std::string_view rowId, float value) {
        changed = setElementText(tree.root, std::string(rowId) + "_value",
                                 formatPercentLabel(value)) ||
                  changed;
        changed = setElementWidth(
                      tree.root, std::string(rowId) + "_meter",
                      normalizedMetricWidth(value, 0.0f, 1.0f,
                                            kCareHungerMeterMaxW)) ||
                  changed;
    };
    setCareMetric("care_hunger", status.primaryCreatureHunger);
    if (UiElement* feedButton =
            findElementById(tree.root, kFeedPrimaryCreatureButtonId))
    {
        const bool available =
            status.creatureFeedState == UiCreatureFeedState::Ready;
        const bool feedbackActive =
            status.creatureFeedState == UiCreatureFeedState::FedFeedback;
        changed = setElementText(tree.root, kFeedPrimaryCreatureButtonId,
                                 creatureFeedButtonLabel(status)) ||
                  changed;
        changed = changed || feedButton->enabled != available;
        feedButton->enabled = available;
        const UiButtonStyle style =
            makeCreatureFeedButtonStyle(available, feedbackActive);
        const uint32_t previousColor = feedButton->style.color;
        const uint32_t previousTextColor = feedButton->style.textColor;
        const uint32_t previousDisabledColor = feedButton->style.disabledColor;
        const uint32_t previousDisabledTextColor =
            feedButton->style.disabledTextColor;
        applyButtonStyle(*feedButton, style);
        changed = changed || previousColor != feedButton->style.color ||
                  previousTextColor != feedButton->style.textColor ||
                  previousDisabledColor != feedButton->style.disabledColor ||
                  previousDisabledTextColor != feedButton->style.disabledTextColor;
    }
    return changed;
}

UiTree makeOverlaySmokeTree()
{
    UiElement root{};
    root.id = "ui_overlay_smoke_root";
    root.kind = UiElementKind::Container;
    root.rect = UiRect{0.0f, 0.0f, kDesignViewportW, kDesignViewportH};
    root.children.reserve(7);

    UiElement panel{};
    panel.id = "test_panel";
    panel.kind = UiElementKind::Rect;
    panel.rect = UiRect{kSmokePanelX, kSmokePanelY, kSmokePanelW, kSmokePanelH};
    const UiTheme& theme = defaultUiTheme();
    panel.style.color = themeColor(theme.panelInk, 0.96f);
    panel.children.reserve(4);

    panel.children.push_back(makeRect("accent_top", UiRect{0.0f, 0.0f, kSmokePanelW, 12.0f},
                                      themeColor(theme.accent, 1.0f)));
    panel.children.push_back(makeRect("accent_left",
                                      UiRect{0.0f, 0.0f, 8.0f, kSmokePanelH},
                                      themeColor(theme.accentAlt, 1.0f)));
    panel.children.push_back(makeRect("accent_right",
                                      UiRect{kSmokePanelW - 4.0f, 0.0f, 4.0f,
                                             kSmokePanelH},
                                      themeColor(theme.foreground, 0.90f)));

    UiElement contentStack{};
    contentStack.id = "content_stack";
    contentStack.kind = UiElementKind::Container;
    contentStack.rect = UiRect{24.0f, 18.0f, kSmokePanelW - 52.0f,
                               kSmokePanelH - 18.0f};
    contentStack.layoutMode = UiLayoutMode::VerticalStack;
    contentStack.crossAxisAlign = UiCrossAxisAlign::Start;
    contentStack.padding = UiPadding{8.0f, 4.0f, 12.0f, 16.0f};
    contentStack.spacing = 6.0f;
    contentStack.children.reserve(9);
    contentStack.children.push_back(makeText(
        "title", UiRect{0.0f, 0.0f, 0.0f, 18.0f}, "AQUARIUM", theme.titleTextHeight,
        themeColor(theme.foreground, 0.92f)));
    contentStack.children.push_back(makeRect(
        "title_rule", UiRect{0.0f, 0.0f, 0.0f, 8.0f},
        themeColor(theme.foreground, 0.80f)));
    contentStack.children.push_back(makeSpacer("spacer_before_status", 4.0f));

    UiElement statusRows{};
    statusRows.id = "status_rows";
    statusRows.kind = UiElementKind::Container;
    statusRows.rect = UiRect{0.0f, 0.0f, 0.0f, 62.0f};
    statusRows.layoutMode = UiLayoutMode::VerticalStack;
    statusRows.crossAxisAlign = UiCrossAxisAlign::Stretch;
    statusRows.spacing = 5.0f;
    statusRows.clipChildren = false;
    statusRows.children.reserve(3);
    statusRows.children.push_back(
        makeHudStatusRow("status_row_fish", "FISH", "0",
                         themeColor(theme.info, 0.95f), 156.0f));
    statusRows.children.push_back(
        makeHudStatusRow("status_row_time", "TIME", "DAY",
                         themeColor(theme.positive, 0.95f), 132.0f));
    statusRows.children.push_back(
        makeHudStatusRow("status_row_clean", "CLEAN", "100%",
                         themeColor(theme.neutral, 0.95f), 128.0f));
    contentStack.children.push_back(std::move(statusRows));

    contentStack.children.push_back(makeGrowSpacer("content_grow_spacer", 1.0f));
    UiButtonOptions maintainOptions{};
    maintainOptions.id = std::string(kMaintainWaterButtonId);
    maintainOptions.rect = UiRect{0.0f, 0.0f, 0.0f, 26.0f};
    maintainOptions.label = "MAINTAIN WATER";
    maintainOptions.action = std::string(kMaintainWaterAction);
    maintainOptions.style = makeWaterMaintenanceButtonStyle(false, false);
    maintainOptions.enabled = false;
    contentStack.children.push_back(ui::makeButton(std::move(maintainOptions)));
    contentStack.children.push_back(ui::makeButton(
        "catalog_bar", UiRect{0.0f, 0.0f, 0.0f, 28.0f}, "BUILD CATALOG",
        std::string(kOpenBuildCatalogAction)));
    panel.children.push_back(std::move(contentStack));

    UiElement secondaryPanel{};
    secondaryPanel.id = "secondary_panel";
    secondaryPanel.kind = UiElementKind::Rect;
    secondaryPanel.rect = UiRect{kSmokePanelX, kSmokePanelY + 268.0f, kSmokePanelW,
                                 kSecondaryPanelH};
    secondaryPanel.style.color = themeColor(theme.panelInk, 0.86f);
    secondaryPanel.children.reserve(2);
    secondaryPanel.children.push_back(
        makeRect("secondary_accent", UiRect{0.0f, 0.0f, 8.0f, kSecondaryPanelH},
                 themeColor(theme.accent, 0.95f)));

    UiElement secondaryStack{};
    secondaryStack.id = "secondary_stack";
    secondaryStack.kind = UiElementKind::Container;
    secondaryStack.rect = UiRect{16.0f, 10.0f, kSmokePanelW - 32.0f, 28.0f};
    secondaryStack.layoutMode = UiLayoutMode::HorizontalStack;
    secondaryStack.crossAxisAlign = UiCrossAxisAlign::Center;
    secondaryStack.spacing = 10.0f;
    secondaryStack.children.reserve(3);

    // each telemetry metric pairs its caption with its bar in a small horizontal
    // group, so labels sit beside their bars instead of overlapping them.
    const auto makeTelemetryGroup =
        [&theme](const char* groupId, const char* labelId, const char* labelText,
                 float labelWidth, UiElement bar) {
            UiElement group{};
            group.id = groupId;
            group.kind = UiElementKind::Container;
            group.rect = UiRect{0.0f, 0.0f, labelWidth + 6.0f + bar.rect.width, 16.0f};
            group.layoutMode = UiLayoutMode::HorizontalStack;
            group.crossAxisAlign = UiCrossAxisAlign::Center;
            group.spacing = 6.0f;
            group.children.reserve(2);
            group.children.push_back(makeText(
                labelId, UiRect{0.0f, 0.0f, labelWidth, 12.0f}, labelText,
                theme.captionTextHeight, themeColor(theme.foreground, 0.76f)));
            group.children.push_back(std::move(bar));
            return group;
        };

    secondaryStack.children.push_back(makeTelemetryGroup(
        "secondary_group_o2", "secondary_label_o2", "O2 82%", 64.0f,
        makeRect("secondary_metric_a", UiRect{0.0f, 0.0f, kTelemetryOxygenMaxW, 8.0f},
                 themeColor(theme.foreground, 0.75f))));
    secondaryStack.children.push_back(makeTelemetryGroup(
        "secondary_group_temp", "secondary_label_temp", "TEMP 24C", 74.0f,
        makeRect("secondary_metric_b",
                 UiRect{0.0f, 0.0f, kTelemetryTemperatureMaxW, 16.0f},
                 themeColor(theme.info, 0.90f))));
    secondaryStack.children.push_back(makeTelemetryGroup(
        "secondary_group_flow", "secondary_label_flow", "FLOW 45%", 84.0f,
        makeRect("secondary_metric_c", UiRect{0.0f, 0.0f, kTelemetryFlowMaxW, 12.0f},
                 themeColor(theme.positive, 0.90f))));
    secondaryPanel.children.push_back(std::move(secondaryStack));

    UiElement carePanel{};
    carePanel.id = "primary_creature_care_panel";
    carePanel.kind = UiElementKind::Rect;
    carePanel.rect = UiRect{kDesignViewportW - kHudMargin - kCarePanelW,
                            kDesignViewportH - kHudMargin - kCarePanelH,
                            kCarePanelW, kCarePanelH};
    carePanel.style.color = themeColor(theme.panelInk, 0.92f);
    carePanel.children.reserve(3);
    carePanel.children.push_back(makeRect(
        "care_accent", UiRect{0.0f, 0.0f, kCarePanelW, 10.0f},
        themeColor(theme.accentAlt, 1.0f)));

    UiElement careStack{};
    careStack.id = "primary_creature_care_stack";
    careStack.kind = UiElementKind::Container;
    careStack.rect = UiRect{16.0f, 18.0f, kCarePanelW - 32.0f, kCarePanelH - 30.0f};
    careStack.layoutMode = UiLayoutMode::VerticalStack;
    careStack.crossAxisAlign = UiCrossAxisAlign::Stretch;
    careStack.spacing = 6.0f;
    careStack.children.reserve(4);
    careStack.children.push_back(makeText(
        "care_name", UiRect{0.0f, 0.0f, 0.0f, 20.0f}, "NO CREATURE",
        theme.headingTextHeight, themeColor(theme.foreground, 0.92f)));

    UiElement hungerBlock{};
    hungerBlock.id = "care_hunger";
    hungerBlock.kind = UiElementKind::Container;
    hungerBlock.rect = UiRect{0.0f, 0.0f, 0.0f, 40.0f};
    hungerBlock.clipChildren = false;
    hungerBlock.children.reserve(4);
    hungerBlock.children.push_back(makeText(
        "care_hunger_label", UiRect{0.0f, 0.0f, 156.0f, 16.0f}, "HUNGER",
        theme.captionTextHeight, themeColor(theme.foreground, 0.76f)));
    hungerBlock.children.push_back(makeText(
        "care_hunger_value", UiRect{168.0f, 0.0f, 60.0f, 16.0f}, "0%",
        theme.bodyTextHeight, themeColor(theme.foreground, 0.96f),
        TextLayoutOptions{60.0f, 0.0f, TextAlign::Right}));
    hungerBlock.children.push_back(makeRect(
        "care_hunger_meter_track",
        UiRect{0.0f, 24.0f, kCareHungerMeterMaxW, 12.0f},
        themeColor(theme.buttonDisabledInk, 0.78f)));
    hungerBlock.children.push_back(makeRect(
        "care_hunger_meter", UiRect{0.0f, 24.0f, 0.0f, 12.0f},
        themeColor(theme.accentAlt, 0.95f)));
    careStack.children.push_back(std::move(hungerBlock));
    careStack.children.push_back(makeGrowSpacer("care_grow_spacer", 1.0f));
    UiButtonOptions feedOptions{};
    feedOptions.id = std::string(kFeedPrimaryCreatureButtonId);
    feedOptions.rect = UiRect{0.0f, 0.0f, 0.0f, 34.0f};
    feedOptions.label = "NO CREATURE";
    feedOptions.action = std::string(kFeedPrimaryCreatureAction);
    feedOptions.style = makeCreatureFeedButtonStyle(false, false);
    feedOptions.enabled = false;
    feedOptions.pixelHeight = theme.bodyTextHeight;
    careStack.children.push_back(ui::makeButton(std::move(feedOptions)));
    carePanel.children.push_back(std::move(careStack));

    UiElement fishPanel{};
    fishPanel.id = std::string(kFishListPanelId);
    fishPanel.kind = UiElementKind::Rect;
    fishPanel.rect = UiRect{kDesignViewportW - kHudMargin - kFishPanelW,
                            kHudMargin, kFishPanelW, kFishPanelH};
    fishPanel.style.color = themeColor(theme.panelInk, 0.92f);
    fishPanel.clipChildren = true;
    fishPanel.children.reserve(5);
    fishPanel.children.push_back(makeRect(
        "fish_list_accent", UiRect{0.0f, 0.0f, 220.0f, 10.0f},
        themeColor(theme.accent, 1.0f)));
    fishPanel.children.push_back(makeText(
        "fish_list_title", UiRect{16.0f, 20.0f, 40.0f, 18.0f}, "FISH",
        theme.captionTextHeight, themeColor(theme.foreground, 0.92f)));
    UiButtonOptions codexOptions{};
    codexOptions.id = std::string(kCollectionCodexButtonId);
    codexOptions.rect = UiRect{58.0f, 16.0f, 62.0f, 26.0f};
    codexOptions.label = "CODEX";
    codexOptions.action = std::string(kOpenCollectionCodexAction);
    codexOptions.style = makeMenuButtonStyle(false);
    codexOptions.pixelHeight = theme.captionTextHeight;
    fishPanel.children.push_back(ui::makeButton(std::move(codexOptions)));
    UiButtonOptions releaseOptions{};
    releaseOptions.id = std::string(kFishFocusReleaseButtonId);
    releaseOptions.rect = UiRect{126.0f, 16.0f, 82.0f, 26.0f};
    releaseOptions.label = "RELEASE";
    releaseOptions.action = std::string(kReleaseFishFocusAction);
    releaseOptions.enabled = false;
    releaseOptions.style = makeReleaseButtonStyle(false);
    releaseOptions.pixelHeight = theme.captionTextHeight;
    fishPanel.children.push_back(ui::makeButton(std::move(releaseOptions)));
    UiElement fishContent{};
    fishContent.id = std::string(kFishListContentId);
    fishContent.kind = UiElementKind::Container;
    fishContent.rect = UiRect{10.0f, kFishListContentTop, 200.0f,
                              kFishPanelH - kFishListContentTop - kFishListBottomPad};
    fishContent.clipChildren = true;
    fishPanel.children.push_back(std::move(fishContent));

    root.children.push_back(makeFishWorldLabelLayer());
    root.children.push_back(std::move(panel));
    root.children.push_back(std::move(secondaryPanel));
    root.children.push_back(makeBuildCatalogPanel());
    root.children.push_back(makeBuildGhostPreview());
    root.children.push_back(std::move(carePanel));
    root.children.push_back(std::move(fishPanel));

    UiTree tree{};
    tree.root = std::move(root);
    return tree;
}

void setOverlaySmokeBuildCatalogOpen(UiTree& tree, bool open)
{
    UiElement* panel = findElementById(tree.root, "build_catalog_panel");
    if (panel == nullptr)
    {
        return;
    }

    panel->visible = open;
    panel->enabled = open;
    applyCompactHudVisibility(
        tree, isCompactOverlayViewport(tree.root.rect.width, tree.root.rect.height));
}

void setOverlaySmokeBuildCatalogScrollOffset(UiTree& tree, float scrollOffsetY)
{
    UiElement* viewport = findElementById(tree.root, kBuildCatalogListViewportId);
    if (viewport == nullptr)
    {
        return;
    }

    viewport->scrollOffsetY =
        std::clamp(scrollOffsetY, 0.0f, overlaySmokeBuildCatalogMaxScrollOffset());
}

void setOverlaySmokeBuildCatalogSelection(UiTree& tree,
                                          std::string_view selectedCatalogKey)
{
    const std::span<const BuildCatalogItemDefinition> catalogItems =
        overlaySmokeBuildCatalogItems();
    for (size_t itemIndex = 0; itemIndex < catalogItems.size(); ++itemIndex)
    {
        const BuildCatalogItemDefinition& item = catalogItems[itemIndex];
        UiElement* row = findElementById(tree.root, item.id);
        if (row != nullptr)
        {
            applyButtonStyle(*row,
                             makeCatalogItemButtonStyle(
                                 itemIndex, item.key == selectedCatalogKey));
        }
    }

    UiElement* remove = findElementById(tree.root, kBuildCatalogRemoveId);
    if (remove != nullptr)
    {
        applyButtonStyle(*remove,
                         makeCatalogRemoveButtonStyle(
                             selectedCatalogKey == kBuildRemoveKey));
    }
}

void setOverlaySmokeBuildGhostPreview(UiTree& tree, bool visible,
                                      std::string_view itemKey,
                                      int rotationSteps)
{
    UiElement* preview = findElementById(tree.root, kBuildGhostPreviewId);
    if (preview == nullptr)
    {
        return;
    }

    preview->visible = visible;
    preview->enabled = false;

    UiElement* label = findElementById(tree.root, kBuildGhostPreviewLabelId);
    if (label != nullptr)
    {
        label->text.value = buildGhostPreviewLabel(itemKey);
    }

    UiElement* hint = findElementById(tree.root, kBuildGhostPreviewHintId);
    if (hint != nullptr)
    {
        hint->text.value = buildGhostPreviewHint(itemKey, rotationSteps);
    }

    setOverlaySmokeBuildCatalogSelection(tree, visible ? itemKey : std::string_view{});
}

} // namespace ui
