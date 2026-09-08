#include "UI/Runtime/CollectionCodexScreen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "UI/Runtime/UiTheme.h"
#include "UI/Runtime/UiWidgets.h"
#include "engine/game/CollectionCodex.h"

namespace ui
{
namespace
{

constexpr float kDesignViewportW = 1280.0f;
constexpr float kDesignViewportH = 720.0f;
constexpr float kPanelW = 940.0f;
constexpr float kPanelH = 590.0f;
constexpr float kListW = 248.0f;
constexpr float kRowH = 32.0f;
constexpr float kRowStride = 40.0f;
constexpr float kViewportInset = 12.0f;
constexpr float kScrollStep = kRowStride;

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

UiButtonStyle makeSpeciesButtonStyle(size_t index, bool selected)
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
    if (index == 0)
    {
        style.backgroundColor = themeColor(theme.info, 0.88f);
        style.hoverColor = themeColor(theme.infoHover, 0.94f);
    }
    else if (index == 1)
    {
        style.backgroundColor = themeColor(theme.positive, 0.86f);
        style.hoverColor = themeColor(theme.positiveHover, 0.92f);
    }
    else
    {
        style.backgroundColor = themeColor(theme.neutral, 0.86f);
        style.hoverColor = themeColor(theme.neutralHover, 0.92f);
    }
    style.pressedColor = themeColor(theme.accent, 0.95f);
    style.textColor = themeColor(theme.foreground, 0.92f);
    return style;
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

void setElementText(UiElement& root, std::string_view id, std::string value)
{
    UiElement* element = findElementById(root, id);
    if (element != nullptr)
    {
        element->text.value = std::move(value);
    }
}

float mix(float a, float b, float t)
{
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

float finiteExtent(float value)
{
    return std::isfinite(value) ? std::max(1.0f, value) : 1.0f;
}

bool pointInRect(const UiRect& rect, float x, float y)
{
    return std::isfinite(x) && std::isfinite(y) && rect.width > 0.0f &&
           rect.height > 0.0f && x >= rect.x && y >= rect.y &&
           x < rect.x + rect.width && y < rect.y + rect.height;
}

float contentHeight(const UiElement& viewport)
{
    float height = 0.0f;
    for (const UiElement& child : viewport.children)
    {
        if (child.visible)
        {
            height = std::max(height, child.rect.y + child.rect.height);
        }
    }
    return height;
}

float maxScrollOffset(const UiElement& viewport)
{
    return std::max(0.0f, contentHeight(viewport) -
                              std::max(0.0f, viewport.rect.height));
}

bool clampScrollOffset(UiElement& viewport)
{
    const float clamped =
        std::clamp(std::isfinite(viewport.scrollOffsetY)
                       ? viewport.scrollOffsetY
                       : 0.0f,
                   0.0f, maxScrollOffset(viewport));
    const bool changed = viewport.scrollOffsetY != clamped;
    viewport.scrollOffsetY = clamped;
    return changed;
}

bool setRect(UiElement& element, UiRect rect)
{
    const bool changed = element.rect.x != rect.x || element.rect.y != rect.y ||
                         element.rect.width != rect.width ||
                         element.rect.height != rect.height;
    element.rect = rect;
    return changed;
}

bool layoutDetailRequirementRows(UiElement& detail, float textWidth)
{
    static const FontAsset layoutFont = makeDefaultDebugFontAsset();
    constexpr std::string_view prefix = "collection_codex_requirement_";
    float rowY = 174.0f;
    bool changed = false;
    for (UiElement& element : detail.children)
    {
        if (element.id.compare(0, prefix.size(), prefix) != 0)
        {
            continue;
        }
        const TextLayoutResult measured = measureText(
            layoutFont, element.text.value, element.text.pixelHeight,
            TextLayoutOptions{textWidth, 0.0f, TextAlign::Left});
        const float rowHeight = std::max(18.0f, measured.height);
        changed = setRect(element, UiRect{0.0f, rowY, textWidth, rowHeight}) ||
                  changed;
        rowY += rowHeight + 8.0f;
    }
    return changed;
}

bool updateCodexContentWidths(UiElement& root, float listWidth,
                              float detailWidth)
{
    bool changed = false;
    if (UiElement* list = findElementById(root, kCollectionCodexListContentId))
    {
        for (UiElement& row : list->children)
        {
            UiRect rect = row.rect;
            rect.width = listWidth;
            changed = setRect(row, rect) || changed;
        }
    }

    if (UiElement* detail = findElementById(root, kCollectionCodexDetailContentId))
    {
        const float textWidth = std::max(0.0f, detailWidth - 14.0f);
        const float idWidth =
            std::min(detailWidth,
                     std::min(152.0f, std::max(88.0f, detailWidth * 0.32f)));
        for (UiElement& element : detail->children)
        {
            UiRect rect = element.rect;
            if (element.id == "collection_codex_detail_id")
            {
                rect.x = std::max(0.0f, detailWidth - idWidth);
                rect.width = idWidth;
                element.text.layout.maxWidth = idWidth;
            }
            else if (element.id == "collection_codex_detail_name")
            {
                rect.width = std::max(0.0f, detailWidth - idWidth - 2.0f);
            }
            else
            {
                rect.width = textWidth;
            }
            changed = setRect(element, rect) || changed;
        }
        changed = layoutDetailRequirementRows(*detail, textWidth) || changed;
    }
    return changed;
}

bool layoutCodexPanel(UiElement& root, float panelWidth, float panelHeight)
{
    const float widthT = (panelWidth - 616.0f) / (kPanelW - 616.0f);
    const float heightT = (panelHeight - 336.0f) / (kPanelH - 336.0f);
    const float marginX = mix(20.0f, 32.0f, widthT);
    const float titleY = mix(18.0f, 28.0f, heightT);
    const float subtitleY = mix(48.0f, 58.0f, heightT);
    const float bodyY =
        panelWidth < 800.0f ? 114.0f : mix(108.0f, 122.0f, heightT);
    const float advisoryY = std::max(bodyY, panelHeight - 38.0f);
    const float bodyHeight = std::max(0.0f, advisoryY - bodyY - 10.0f);
    const float listWidth =
        std::min(kListW, std::max(132.0f, panelWidth * 0.30f));
    const float columnGap = mix(24.0f, 44.0f, widthT);
    const float detailX = marginX + listWidth + columnGap;
    const float detailWidth = std::max(0.0f, panelWidth - marginX - detailX);
    const float closeRightMargin = marginX;
    const float closeX =
        std::max(marginX, panelWidth - closeRightMargin - 68.0f);
    const float titleWidth = std::max(0.0f, closeX - marginX - 8.0f);

    bool changed = false;
    changed = setElementRect(root, "collection_codex_accent",
                             UiRect{0.0f, 0.0f, panelWidth, 10.0f}) ||
              changed;
    changed = setElementRect(root, "collection_codex_title",
                             UiRect{marginX, titleY,
                                    panelWidth >= kPanelW ? 420.0f : titleWidth,
                                    28.0f}) ||
              changed;
    changed = setElementRect(
                  root, "collection_codex_subtitle",
                  UiRect{marginX + (panelWidth >= kPanelW ? 2.0f : 0.0f),
                         subtitleY,
                         panelWidth >= kPanelW
                             ? 440.0f
                             : std::max(0.0f, panelWidth - marginX * 2.0f),
                         18.0f}) ||
              changed;

    if (panelWidth < 800.0f)
    {
        changed = setElementRect(
                      root, "collection_codex_summary",
                      UiRect{marginX, 70.0f,
                             std::max(0.0f, panelWidth - marginX * 2.0f), 18.0f}) ||
                  changed;
        changed = setElementRect(
                      root, "collection_codex_cleanup",
                      UiRect{marginX, 90.0f,
                             std::max(0.0f, panelWidth - marginX * 2.0f), 18.0f}) ||
                  changed;
    }
    else
    {
        const float summaryY = mix(80.0f, 88.0f, heightT);
        const float summaryWidth = std::min(420.0f, panelWidth * 0.46f);
        const float cleanupX =
            panelWidth >= kPanelW
                ? 490.0f
                : std::max(marginX + summaryWidth + 12.0f,
                           panelWidth - marginX - 410.0f);
        changed = setElementRect(
                      root, "collection_codex_summary",
                      UiRect{marginX + 2.0f, summaryY, summaryWidth, 18.0f}) ||
                  changed;
        changed = setElementRect(
                      root, "collection_codex_cleanup",
                      UiRect{cleanupX, summaryY,
                             panelWidth >= kPanelW
                                 ? 410.0f
                                 : std::max(0.0f,
                                            panelWidth - marginX - cleanupX),
                             18.0f}) ||
                  changed;
    }

    changed = setElementRect(
                  root, kCollectionCodexCloseButtonId,
                  UiRect{closeX, mix(16.0f, 24.0f, heightT), 68.0f, 28.0f}) ||
              changed;
    const float dividerX = marginX + listWidth + (columnGap - 4.0f) * 0.5f;
    changed = setElementRect(root, "collection_codex_divider",
                             UiRect{dividerX, bodyY, 4.0f, bodyHeight}) ||
              changed;
    changed = setElementRect(root, kCollectionCodexListContentId,
                             UiRect{marginX, bodyY, listWidth, bodyHeight}) ||
              changed;
    changed = setElementRect(root, kCollectionCodexDetailContentId,
                             UiRect{detailX, bodyY, detailWidth, bodyHeight}) ||
              changed;
    changed = setElementRect(root, "collection_codex_advisory",
                             UiRect{detailX, advisoryY, detailWidth, 18.0f}) ||
              changed;

    if (UiElement* cleanup = findElementById(root, "collection_codex_cleanup"))
    {
        cleanup->text.layout.maxWidth = cleanup->rect.width;
    }
    if (UiElement* advisory = findElementById(root, "collection_codex_advisory"))
    {
        advisory->text.layout.maxWidth = advisory->rect.width;
    }
    changed = updateCodexContentWidths(root, listWidth, detailWidth) || changed;

    if (UiElement* list = findElementById(root, kCollectionCodexListContentId))
    {
        changed = clampScrollOffset(*list) || changed;
    }
    if (UiElement* detail = findElementById(root, kCollectionCodexDetailContentId))
    {
        changed = clampScrollOffset(*detail) || changed;
    }
    return changed;
}

std::string formatPercent(float value)
{
    const int percent =
        static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 100.0f));
    return std::to_string(percent) + "%";
}

std::string roleLabel(engine::game::SpeciesRoleMask roles)
{
    constexpr std::array roleOrder = {
        engine::game::SpeciesRole::Producer,
        engine::game::SpeciesRole::CleanupCrew,
        engine::game::SpeciesRole::FilterFeeder,
        engine::game::SpeciesRole::Grazer,
        engine::game::SpeciesRole::Predator,
        engine::game::SpeciesRole::Symbiont,
        engine::game::SpeciesRole::Showpiece,
    };
    std::string label;
    for (engine::game::SpeciesRole role : roleOrder)
    {
        if (!engine::game::hasSpeciesRole(roles, role))
        {
            continue;
        }
        if (!label.empty())
        {
            label += " / ";
        }
        label += engine::game::speciesRoleDisplayName(role);
    }
    return label.empty() ? "UNASSIGNED" : label;
}

const char* waterLabel(engine::game::HabitatWaterParameter parameter)
{
    using Parameter = engine::game::HabitatWaterParameter;
    switch (parameter)
    {
    case Parameter::TemperatureC:
        return "TEMP";
    case Parameter::Oxygen:
        return "OXYGEN";
    case Parameter::Flow:
        return "FLOW";
    case Parameter::Cleanliness:
        return "CLEAN";
    }
    return "WATER";
}

std::string waterValue(float value, engine::game::HabitatWaterParameter parameter)
{
    if (parameter == engine::game::HabitatWaterParameter::TemperatureC)
    {
        return std::to_string(static_cast<int>(std::lround(value))) + "C";
    }
    return formatPercent(value);
}

std::string waterRequirementLabel(
    const engine::game::HabitatWaterEvaluation& evaluation)
{
    const bool satisfied = evaluation.status ==
                           engine::game::HabitatRequirementStatus::Satisfied;
    return std::string("WATER ") + waterLabel(evaluation.parameter) + "  " +
           waterValue(evaluation.observed, evaluation.parameter) + " / " +
           waterValue(evaluation.required.minimum, evaluation.parameter) + "-" +
           waterValue(evaluation.required.maximum, evaluation.parameter) +
           (satisfied ? "  READY" : "  NEEDS ATTENTION");
}

std::string decorRequirementLabel(
    const engine::game::HabitatDecorEvaluation& evaluation)
{
    const bool satisfied = evaluation.status ==
                           engine::game::HabitatRequirementStatus::Satisfied;
    return "DECOR " + evaluation.displayName + "  " +
           std::to_string(evaluation.observedCount) + "/" +
           std::to_string(evaluation.requiredCount) +
           (satisfied ? "  READY" : "  NEEDS ATTENTION");
}

std::string tankmateRequirementLabel(
    const engine::game::HabitatTankmateEvaluation& evaluation)
{
    using Rule = engine::game::HabitatTankmateRule;
    const bool satisfied = evaluation.status ==
                           engine::game::HabitatRequirementStatus::Satisfied;
    std::string label;
    switch (evaluation.rule)
    {
    case Rule::MinimumGroupSize:
        label = "GROUP " + evaluation.speciesId + "  " +
                std::to_string(evaluation.observedCount) + "/" +
                std::to_string(evaluation.requiredCount);
        break;
    case Rule::RequiredSpeciesPresent:
        label = "TANKMATE " + evaluation.speciesId + "  " +
                std::to_string(evaluation.observedCount) + "/1";
        break;
    case Rule::IncompatibleSpeciesAbsent:
        label = "AVOID " + evaluation.speciesId + "  " +
                std::to_string(evaluation.observedCount) + " PRESENT";
        break;
    }
    return label + (satisfied ? "  READY" : "  NEEDS ATTENTION");
}

uint32_t requirementColor(engine::game::HabitatRequirementStatus status)
{
    const UiTheme& theme = defaultUiTheme();
    return status == engine::game::HabitatRequirementStatus::Satisfied
               ? themeColor(theme.positive, 0.94f)
               : themeColor(theme.accentAlt, 0.92f);
}

} // namespace

UiTree makeCollectionCodexScreenTree()
{
    const UiTheme& theme = defaultUiTheme();
    UiElement root{};
    root.id = "ui_collection_codex_root";
    root.kind = UiElementKind::Container;
    root.rect = UiRect{0.0f, 0.0f, kDesignViewportW, kDesignViewportH};
    root.clipChildren = false;
    root.children.reserve(2);
    root.children.push_back(makeRect(
        "collection_codex_backdrop",
        UiRect{0.0f, 0.0f, kDesignViewportW, kDesignViewportH},
        themeColor(theme.panelInk, 0.58f)));

    UiElement panel{};
    panel.id = "collection_codex_panel";
    panel.kind = UiElementKind::Rect;
    panel.rect = UiRect{(kDesignViewportW - kPanelW) * 0.5f,
                        (kDesignViewportH - kPanelH) * 0.5f, kPanelW, kPanelH};
    panel.style.color = themeColor(theme.panelInk, 0.97f);
    panel.clipChildren = true;
    panel.children.reserve(10);
    panel.children.push_back(makeRect(
        "collection_codex_accent", UiRect{0.0f, 0.0f, kPanelW, 10.0f},
        themeColor(theme.accent, 1.0f)));
    panel.children.push_back(makeText(
        "collection_codex_title", UiRect{32.0f, 28.0f, 420.0f, 28.0f},
        "COLLECTION CODEX", theme.headingTextHeight,
        themeColor(theme.foreground, 0.96f)));
    panel.children.push_back(makeText(
        "collection_codex_subtitle", UiRect{34.0f, 58.0f, 440.0f, 18.0f},
        "OWNED SPECIES / ROLES / HABITAT", theme.captionTextHeight,
        themeColor(theme.foreground, 0.62f)));
    panel.children.push_back(makeText(
        "collection_codex_summary", UiRect{34.0f, 88.0f, 420.0f, 18.0f},
        "OWNED 0 CREATURES / 0 SPECIES", theme.captionTextHeight,
        themeColor(theme.foreground, 0.88f)));
    panel.children.push_back(makeText(
        "collection_codex_cleanup", UiRect{490.0f, 88.0f, 410.0f, 18.0f},
        "CLEANUP CREW 0 / CLEANING LOAD -0%", theme.captionTextHeight,
        themeColor(theme.positive, 0.92f),
        TextLayoutOptions{410.0f, 0.0f, TextAlign::Right}));

    UiButtonOptions close{};
    close.id = std::string(kCollectionCodexCloseButtonId);
    close.rect = UiRect{840.0f, 24.0f, 68.0f, 28.0f};
    close.label = "CLOSE";
    close.action = std::string(kCloseCollectionCodexAction);
    close.style = makeMenuButtonStyle(false);
    close.pixelHeight = theme.captionTextHeight;
    panel.children.push_back(ui::makeButton(std::move(close)));
    panel.children.push_back(makeRect(
        "collection_codex_divider", UiRect{300.0f, 122.0f, 4.0f, 420.0f},
        themeColor(theme.neutral, 0.72f)));

    UiElement list{};
    list.id = std::string(kCollectionCodexListContentId);
    list.kind = UiElementKind::Container;
    list.rect = UiRect{32.0f, 122.0f, kListW, 420.0f};
    list.clipChildren = true;
    panel.children.push_back(std::move(list));

    UiElement detail{};
    detail.id = std::string(kCollectionCodexDetailContentId);
    detail.kind = UiElementKind::Container;
    detail.rect = UiRect{324.0f, 122.0f, 584.0f, 420.0f};
    detail.clipChildren = true;
    panel.children.push_back(std::move(detail));
    panel.children.push_back(makeText(
        "collection_codex_advisory", UiRect{324.0f, 552.0f, 584.0f, 18.0f},
        "ADVISORY ONLY - HABITAT DOES NOT CONTROL ADDING CREATURES",
        theme.captionTextHeight, themeColor(theme.info, 0.90f),
        TextLayoutOptions{584.0f, 0.0f, TextAlign::Right}));

    root.children.push_back(std::move(panel));
    UiTree tree{};
    tree.root = std::move(root);
    return tree;
}

bool setCollectionCodexViewport(UiTree& tree, float width, float height)
{
    if (tree.root.id != "ui_collection_codex_root")
    {
        return false;
    }
    const float viewportW = finiteExtent(width);
    const float viewportH = finiteExtent(height);
    const UiRect viewportRect{0.0f, 0.0f, viewportW, viewportH};
    const float panelW = std::min(
        kPanelW, std::max(1.0f, viewportW - std::min(viewportW - 1.0f,
                                                     kViewportInset * 2.0f)));
    const float panelH = std::min(
        kPanelH, std::max(1.0f, viewportH - std::min(viewportH - 1.0f,
                                                     kViewportInset * 2.0f)));
    bool changed = false;
    if (tree.root.rect.x != viewportRect.x || tree.root.rect.y != viewportRect.y ||
        tree.root.rect.width != viewportRect.width ||
        tree.root.rect.height != viewportRect.height)
    {
        tree.root.rect = viewportRect;
        changed = true;
    }
    changed = setElementRect(tree.root, "collection_codex_backdrop", viewportRect) ||
              changed;
    changed = setElementRect(
                  tree.root, "collection_codex_panel",
                  UiRect{(viewportW - panelW) * 0.5f,
                         (viewportH - panelH) * 0.5f, panelW, panelH}) ||
              changed;
    changed = layoutCodexPanel(tree.root, panelW, panelH) || changed;
    return changed;
}

bool setCollectionCodexView(UiTree& tree,
                            const engine::game::CollectionCodexView& view,
                            std::string_view selectedSpeciesId)
{
    if (tree.root.id != "ui_collection_codex_root")
    {
        return false;
    }
    UiElement* list = findElementById(tree.root, kCollectionCodexListContentId);
    UiElement* detail = findElementById(tree.root, kCollectionCodexDetailContentId);
    if (list == nullptr || detail == nullptr)
    {
        return false;
    }

    const float previousListOffset = list->scrollOffsetY;
    const float previousDetailOffset = detail->scrollOffsetY;
    std::string previousSelectedSpeciesId;
    if (const UiElement* previousId =
            findElementById(*detail, "collection_codex_detail_id"))
    {
        previousSelectedSpeciesId = previousId->text.value;
    }

    const UiTheme& theme = defaultUiTheme();
    const engine::game::CollectionCodexEntry* selected = nullptr;
    for (const engine::game::CollectionCodexEntry& entry : view.entries)
    {
        if (entry.speciesId == selectedSpeciesId)
        {
            selected = &entry;
            break;
        }
    }
    if (selected == nullptr && !view.entries.empty())
    {
        selected = &view.entries.front();
    }
    list->scrollOffsetY = previousListOffset;
    detail->scrollOffsetY =
        selected != nullptr && previousSelectedSpeciesId == selected->speciesId
            ? previousDetailOffset
            : 0.0f;

    setElementText(tree.root, "collection_codex_summary",
                   "OWNED " + std::to_string(view.totalOwnedCreatures) +
                       " CREATURES / " + std::to_string(view.entries.size()) +
                       " SPECIES");
    setElementText(
        tree.root, "collection_codex_cleanup",
        "CLEANUP CREW " + std::to_string(view.cleanupCrew.creatureCount) +
            " / CLEANING LOAD -" +
            std::to_string(static_cast<int>(std::lround(
                view.cleanupCrew.cleanlinessDecayReduction * 100.0f))) +
            "%");

    list->children.clear();
    list->children.reserve(view.entries.size());
    for (size_t index = 0; index < view.entries.size(); ++index)
    {
        const engine::game::CollectionCodexEntry& entry = view.entries[index];
        UiButtonOptions options{};
        options.id = "collection_codex_species_" + std::to_string(index);
        options.rect = UiRect{0.0f, static_cast<float>(index) * kRowStride,
                              list->rect.width, kRowH};
        options.label = entry.displayName + "  x" +
                        std::to_string(entry.ownedCount);
        options.action = std::string(kSelectCollectionCodexSpeciesAction);
        options.actionPayload = entry.speciesId;
        options.style = makeSpeciesButtonStyle(
            index, selected != nullptr && entry.speciesId == selected->speciesId);
        options.pixelHeight = theme.captionTextHeight;
        list->children.push_back(ui::makeButton(std::move(options)));
    }

    detail->children.clear();
    if (selected == nullptr)
    {
        detail->children.push_back(makeText(
            "collection_codex_empty_title", UiRect{0.0f, 8.0f, 560.0f, 24.0f},
            "NO CREATURES OWNED", theme.headingTextHeight,
            themeColor(theme.foreground, 0.88f)));
        detail->children.push_back(makeText(
            "collection_codex_empty_hint", UiRect{0.0f, 44.0f, 560.0f, 18.0f},
            "OWNED CREATURES WILL APPEAR HERE.", theme.captionTextHeight,
            themeColor(theme.foreground, 0.62f)));
        updateCodexContentWidths(tree.root, list->rect.width, detail->rect.width);
        clampScrollOffset(*list);
        clampScrollOffset(*detail);
        return true;
    }

    detail->children.reserve(10 + selected->habitat.water.size() +
                             selected->habitat.decor.size() +
                             selected->habitat.tankmates.size());
    detail->children.push_back(makeText(
        "collection_codex_detail_name", UiRect{0.0f, 4.0f, 430.0f, 26.0f},
        selected->displayName + "  x" + std::to_string(selected->ownedCount),
        theme.headingTextHeight, themeColor(theme.foreground, 0.96f)));
    detail->children.push_back(makeText(
        "collection_codex_detail_id", UiRect{432.0f, 8.0f, 152.0f, 18.0f},
        selected->speciesId, theme.captionTextHeight,
        themeColor(theme.foreground, 0.54f),
        TextLayoutOptions{152.0f, 0.0f, TextAlign::Right}));
    detail->children.push_back(makeText(
        "collection_codex_detail_description", UiRect{0.0f, 38.0f, 570.0f, 20.0f},
        selected->description, theme.captionTextHeight,
        themeColor(theme.foreground, 0.72f)));
    detail->children.push_back(makeText(
        "collection_codex_detail_roles", UiRect{0.0f, 72.0f, 570.0f, 18.0f},
        "ROLES  " + roleLabel(selected->roles), theme.captionTextHeight,
        themeColor(theme.info, 0.96f)));

    std::string habitatStatus = "HABITAT  UNKNOWN SPECIES";
    uint32_t habitatStatusColor = themeColor(theme.info, 0.92f);
    if (selected->habitat.status ==
        engine::game::HabitatCompatibilityStatus::Compatible)
    {
        habitatStatus = "HABITAT  READY";
        habitatStatusColor = themeColor(theme.positive, 0.96f);
    }
    else if (selected->habitat.status ==
             engine::game::HabitatCompatibilityStatus::NeedsAttention)
    {
        habitatStatus =
            "HABITAT  NEEDS " +
            std::to_string(selected->habitat.unsatisfiedRequirementCount) +
            " ADJUSTMENT" +
            (selected->habitat.unsatisfiedRequirementCount == 1 ? "" : "S");
        habitatStatusColor = themeColor(theme.accentAlt, 0.94f);
    }
    detail->children.push_back(makeText(
        "collection_codex_detail_status", UiRect{0.0f, 102.0f, 570.0f, 18.0f},
        std::move(habitatStatus), theme.captionTextHeight, habitatStatusColor));
    detail->children.push_back(makeText(
        "collection_codex_requirements_title", UiRect{0.0f, 142.0f, 570.0f, 18.0f},
        "CURRENT HABITAT REQUIREMENTS", theme.captionTextHeight,
        themeColor(theme.foreground, 0.86f)));

    float rowY = 174.0f;
    size_t rowIndex = 0;
    const auto appendRequirement = [&](std::string label,
                                       engine::game::HabitatRequirementStatus status) {
        detail->children.push_back(makeText(
            "collection_codex_requirement_" + std::to_string(rowIndex++),
            UiRect{0.0f, rowY, 570.0f, 18.0f}, std::move(label),
            theme.captionTextHeight, requirementColor(status)));
        rowY += 26.0f;
    };
    for (const engine::game::HabitatWaterEvaluation& evaluation :
         selected->habitat.water)
    {
        appendRequirement(waterRequirementLabel(evaluation), evaluation.status);
    }
    for (const engine::game::HabitatDecorEvaluation& evaluation :
         selected->habitat.decor)
    {
        appendRequirement(decorRequirementLabel(evaluation), evaluation.status);
    }
    for (const engine::game::HabitatTankmateEvaluation& evaluation :
         selected->habitat.tankmates)
    {
        appendRequirement(tankmateRequirementLabel(evaluation), evaluation.status);
    }
    if (!selected->knownSpecies())
    {
        appendRequirement("NO REGISTERED REQUIREMENTS AVAILABLE",
                          engine::game::HabitatRequirementStatus::Unsatisfied);
    }
    updateCodexContentWidths(tree.root, list->rect.width, detail->rect.width);
    clampScrollOffset(*list);
    clampScrollOffset(*detail);
    return true;
}

CollectionCodexScrollRegion collectionCodexScrollRegionAt(const UiTree& tree,
                                                           float x, float y)
{
    if (tree.root.id != "ui_collection_codex_root")
    {
        return CollectionCodexScrollRegion::None;
    }

    const UiElement* list =
        findElementById(tree.root, kCollectionCodexListContentId);
    if (list != nullptr && list->visible && list->enabled &&
        pointInRect(list->computedRect, x, y))
    {
        return CollectionCodexScrollRegion::SpeciesList;
    }

    const UiElement* detail =
        findElementById(tree.root, kCollectionCodexDetailContentId);
    if (detail != nullptr && detail->visible && detail->enabled &&
        pointInRect(detail->computedRect, x, y))
    {
        return CollectionCodexScrollRegion::Detail;
    }
    return CollectionCodexScrollRegion::None;
}

bool scrollCollectionCodexAt(UiTree& tree, float x, float y,
                             double wheelYOffset)
{
    if (!std::isfinite(wheelYOffset) || wheelYOffset == 0.0)
    {
        return false;
    }

    const CollectionCodexScrollRegion region =
        collectionCodexScrollRegionAt(tree, x, y);
    std::string_view elementId;
    switch (region)
    {
    case CollectionCodexScrollRegion::SpeciesList:
        elementId = kCollectionCodexListContentId;
        break;
    case CollectionCodexScrollRegion::Detail:
        elementId = kCollectionCodexDetailContentId;
        break;
    case CollectionCodexScrollRegion::None:
        return false;
    }

    UiElement* viewport = findElementById(tree.root, elementId);
    if (viewport == nullptr)
    {
        return false;
    }
    const float nextOffset =
        std::clamp(viewport->scrollOffsetY -
                       static_cast<float>(wheelYOffset) * kScrollStep,
                   0.0f, maxScrollOffset(*viewport));
    if (nextOffset != viewport->scrollOffsetY)
    {
        viewport->scrollOffsetY = nextOffset;
        computeLayout(tree.root);
    }
    return true;
}

} // namespace ui
