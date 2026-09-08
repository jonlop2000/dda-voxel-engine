#pragma once

#include <imgui.h>

class EngineFacade;

namespace ScenePanel
{

void Init();
void Shutdown();
ImVec2 Draw(EngineFacade& engine, ImVec2 position, ImVec2 size);

} // namespace ScenePanel
