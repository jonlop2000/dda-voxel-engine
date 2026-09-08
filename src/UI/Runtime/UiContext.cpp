#include "UI/Runtime/UiContext.h"

#include "UI/Runtime/UiScreens.h"

namespace ui
{
namespace
{

std::string hitElementId(const UiHitResult& hit)
{
    return hit.element != nullptr ? hit.element->id : std::string{};
}

} // namespace

bool UiContext::setActiveScreen(std::string_view screenId)
{
    if (activeScreenId_ != screenId)
    {
        activeScreenId_ = std::string(screenId);
        invalidateActiveTree();
    }

    return activeScreenId_.empty() || activeScreenRegistered();
}

void UiContext::clearActiveScreen()
{
    if (activeScreenId_.empty())
    {
        return;
    }

    activeScreenId_.clear();
    invalidateActiveTree();
}

void UiContext::invalidateActiveTree()
{
    activeTree_ = UiTree{};
    activeTreeBuilt_ = false;
    overlays_.clear();
    tweens_.clear();
    clearPointerState();
}

bool UiContext::pushScreen(std::string_view screenId)
{
    if (screenId.empty() || !isRuntimeUiScreenRegistered(screenId))
    {
        return false;
    }

    UiTree tree = makeRuntimeUiScreenTree(screenId);
    if (tree.root.id.empty())
    {
        return false;
    }

    computeLayout(tree.root);
    OverlayEntry entry{};
    entry.screenId = std::string(screenId);
    entry.tree = std::move(tree);
    overlays_.push_back(std::move(entry));
    clearPointerState();
    return true;
}

bool UiContext::popScreen()
{
    if (overlays_.empty())
    {
        return false;
    }

    overlays_.pop_back();
    clearPointerState();
    return true;
}

const std::string& UiContext::topScreenId() const
{
    return overlays_.empty() ? activeScreenId_ : overlays_.back().screenId;
}

UiTree* UiContext::topTree()
{
    if (!overlays_.empty())
    {
        return &overlays_.back().tree;
    }
    return activeTreeBuilt_ ? &activeTree_ : nullptr;
}

std::vector<UiTree*> UiContext::overlayTrees()
{
    std::vector<UiTree*> trees;
    trees.reserve(overlays_.size());
    for (OverlayEntry& entry : overlays_)
    {
        trees.push_back(&entry.tree);
    }
    return trees;
}

bool UiContext::activeScreenRegistered() const
{
    return isRuntimeUiScreenRegistered(activeScreenId_);
}

UiTree* UiContext::ensureActiveTree()
{
    if (activeScreenId_.empty() || !activeScreenRegistered())
    {
        return nullptr;
    }

    if (!activeTreeBuilt_)
    {
        activeTree_ = makeRuntimeUiScreenTree(activeScreenId_);
        activeTreeBuilt_ = !activeTree_.root.id.empty();
        if (activeTreeBuilt_)
        {
            ++activeTreeRevision_;
        }
    }

    return activeTreeBuilt_ ? &activeTree_ : nullptr;
}

UiTree* UiContext::activeTree()
{
    return activeTreeBuilt_ ? &activeTree_ : nullptr;
}

const UiTree* UiContext::activeTree() const
{
    return activeTreeBuilt_ ? &activeTree_ : nullptr;
}

void UiContext::updatePointerHover(const UiHitResult& hit)
{
    pointerState_.hoveredElementId = hitElementId(hit);
}

void UiContext::handlePointerDown(const UiHitResult& hit)
{
    updatePointerHover(hit);
    pointerState_.pressedElementId = pointerState_.hoveredElementId;
    pointerState_.clickedElementId.clear();
    pointerState_.clickPending = false;
}

bool UiContext::handlePointerUp(const UiHitResult& hit)
{
    updatePointerHover(hit);

    const bool clicked = !pointerState_.pressedElementId.empty() &&
                         pointerState_.pressedElementId == pointerState_.hoveredElementId;
    if (clicked)
    {
        pointerState_.clickedElementId = pointerState_.pressedElementId;
        pointerState_.clickPending = true;
    }
    else
    {
        pointerState_.clickedElementId.clear();
        pointerState_.clickPending = false;
    }

    pointerState_.pressedElementId.clear();
    return clicked;
}

std::string UiContext::consumeClickedElementId()
{
    if (!pointerState_.clickPending)
    {
        return {};
    }

    std::string clicked = pointerState_.clickedElementId;
    pointerState_.clickedElementId.clear();
    pointerState_.clickPending = false;
    return clicked;
}

UiActionEvent UiContext::consumeActionEvent()
{
    if (!pointerState_.clickPending)
    {
        return {};
    }

    UiActionEvent event{};
    event.elementId = pointerState_.clickedElementId;
    pointerState_.clickedElementId.clear();
    pointerState_.clickPending = false;

    // input is modal: clicks can only originate from the top of the screen stack,
    // so the action lookup must use the top tree (overlay or base), not the base.
    const UiTree* tree = topTree();
    const UiElement* element =
        tree != nullptr ? findElementById(tree->root, event.elementId) : nullptr;
    if (element != nullptr)
    {
        event.action = element->action;
        event.payload = element->actionPayload;
    }

    return event;
}

void UiContext::clearPointerState()
{
    pointerState_ = UiPointerState{};
}

} // namespace ui
