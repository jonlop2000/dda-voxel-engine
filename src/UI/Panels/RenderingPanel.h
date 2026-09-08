#pragma once

#include <imgui.h>

class EngineFacade;

namespace RenderingPanel
{

void Init();
void Shutdown();
ImVec2 Draw(EngineFacade& engine, ImVec2 position, ImVec2 size);

} // namespace RenderingPanel
