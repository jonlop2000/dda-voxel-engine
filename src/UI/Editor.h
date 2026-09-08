#pragma once

#include <imgui.h>

struct EditorState;
class EngineFacade;

namespace Editor
{

// initialize the editor system
void Init();

// Shutdown the editor system
void Shutdown();

// Draw all editor panels
// call this after imgui_.beginFrame() and before imgui_.render()
void Draw(EngineFacade& engine);

// get the viewport rect (area not covered by panels)
ImVec4 GetViewportRect(const EditorState& state);

} // namespace Editor
