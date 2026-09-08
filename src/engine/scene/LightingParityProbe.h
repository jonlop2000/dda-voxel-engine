#pragma once

#include <array>

#include "engine/render/RenderSettings.h"

struct SceneConfig;

namespace engine::scene
{

// returns true only for the dedicated lighting-parity scene variants. the
// authored light remains transient scene-probe state; it is not part of the
// persistent presentation profile.
[[nodiscard]] bool applyLightingParityProbeAreaLights(
    const SceneConfig& sceneConfig, engine::render::LightingSettings& lighting);

[[nodiscard]] const std::array<engine::render::EditableAreaLight, 1>&
lightingParityProbeAreaLights();

} // namespace engine::scene
