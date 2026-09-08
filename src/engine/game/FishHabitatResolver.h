#pragma once

#include <optional>

#include "engine/game/FishHabitat.h"

struct SceneConfig;

namespace engine::game
{

bool sceneSupportsFishHabitat(const SceneConfig& sceneConfig);
std::optional<FishHabitat> resolveFishHabitat(const SceneConfig& sceneConfig);

} // namespace engine::game
