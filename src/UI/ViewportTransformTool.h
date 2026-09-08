#pragma once

#include <imgui.h>

class EngineFacade;

namespace ViewportTransformTool
{

void Draw(EngineFacade& engine, const ImVec4& viewportRect);

} // namespace ViewportTransformTool
