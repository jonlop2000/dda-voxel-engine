#pragma once

#include <imgui.h>

class EngineFacade;

namespace StatusBar
{

void Init();
void Shutdown();
void Draw(EngineFacade& engine, ImVec2 position, ImVec2 size);

} // namespace StatusBar
