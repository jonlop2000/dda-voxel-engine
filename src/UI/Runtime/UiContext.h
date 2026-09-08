#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "UI/Runtime/UiElement.h"
#include "UI/Runtime/UiTween.h"

namespace ui
{

struct UiPointerState
{
    std::string hoveredElementId{};
    std::string pressedElementId{};
    std::string clickedElementId{};
    bool clickPending = false;
};

struct UiActionEvent
{
    std::string elementId{};
    std::string action{};
    std::string payload{};
};

class UiContext
{
public:
    // unknown non-empty ids are stored for diagnostics but return false and do not
    // build a tree until a screen factory is registered for that id.
    bool setActiveScreen(std::string_view screenId);
    void clearActiveScreen();
    void invalidateActiveTree();

    const std::string& activeScreenId() const { return activeScreenId_; }
    bool hasActiveScreen() const { return !activeScreenId_.empty(); }
    bool activeScreenRegistered() const;
    bool activeTreeBuilt() const { return activeTreeBuilt_; }
    const UiPointerState& pointerState() const { return pointerState_; }

    UiTree* ensureActiveTree();
    UiTree* activeTree();
    const UiTree* activeTree() const;

    void updatePointerHover(const UiHitResult& hit);
    void handlePointerDown(const UiHitResult& hit);
    bool handlePointerUp(const UiHitResult& hit);
    std::string consumeClickedElementId();
    UiActionEvent consumeActionEvent();
    void clearPointerState();

    UiTweenSet& tweens() { return tweens_; }
    const UiTweenSet& tweens() const { return tweens_; }

    // increments each time the active tree is (re)built; bindings use it to know a
    // fresh tree needs a full re-apply.
    uint64_t activeTreeRevision() const { return activeTreeRevision_; }

    // screen stack: overlay screens layered above the base active screen.
    // input is modal — the top screen owns pointer/keys; lower screens still paint.
    // overlay trees are laid out once at push (they are static; dynamic overlays can
    // re-layout through the same dirty-flag path later).
    bool pushScreen(std::string_view screenId);
    bool popScreen();
    size_t overlayCount() const { return overlays_.size(); }
    const std::string& topScreenId() const;
    UiTree* topTree();
    std::vector<UiTree*> overlayTrees();

private:
    struct OverlayEntry
    {
        std::string screenId{};
        UiTree tree{};
    };

    std::string activeScreenId_{};
    UiTree activeTree_{};
    std::vector<OverlayEntry> overlays_{};
    UiPointerState pointerState_{};
    UiTweenSet tweens_{};
    uint64_t activeTreeRevision_ = 0;
    bool activeTreeBuilt_ = false;
};

} // namespace ui
