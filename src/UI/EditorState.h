#pragma once

#include <cstdint>
#include <string>

#include <glm/vec2.hpp>

#include "engine/editor/ViewportToolSession.h"

struct EditorState
{
    bool enabled = true;

    bool leftPanelVisible = true;
    bool rightPanelVisible = true;
    bool statusBarVisible = true;
    bool fishPanelVisible = false;

    float leftPanelWidth = 280.0f;
    float rightPanelWidth = 320.0f;

    bool profilerOverlayVisible = true;
    bool profilerGraphVisible = true;
    bool profilerOverlayShowPasses = true;
    int profilerHistoryFrames = 180;

    engine::editor::ViewportToolSession viewportTool{};
    std::string viewportToolStatus{"Click a placeable to select it."};

    int placeableAuthoringPrototypeIndex = 0;
    glm::vec2 placeableAuthoringCreateXZ{0.0f};
    glm::vec2 placeableAuthoringDuplicateOffsetXZ{3.25f, 0.0f};
    uint64_t placeableAuthoringSerial = 0;
};
