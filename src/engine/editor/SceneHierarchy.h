#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "engine/editor/SelectionPicking.h"
#include "engine/game/Placeables.h"

namespace engine::editor
{

enum class SceneHierarchyError
{
    None,
    InvalidPlaceable,
    DuplicatePlaceableIdentity,
};

struct SceneHierarchyPlaceable
{
    SelectionTarget target{};
    std::string label{};
    std::string uuid{};
    std::string prototypeSlug{};
    uint32_t prototypeVersion = 0;
    glm::vec3 position{0.0f};
};

struct SceneHierarchySnapshot
{
    std::vector<SceneHierarchyPlaceable> placeables{};
    bool usesImplicitDefaults = false;
    SceneHierarchyError error = SceneHierarchyError::None;
    size_t invalidPlaceableIndex = static_cast<size_t>(-1);

    [[nodiscard]] bool valid() const noexcept
    {
        return error == SceneHierarchyError::None;
    }

    explicit operator bool() const noexcept { return valid(); }
};

// builds an all-or-nothing, renderer-independent view of the effective authored
// placeable collection. rows preserve scene order and use only persistent uuid
// selection identities; runtime volume indices never enter the hierarchy.
[[nodiscard]] SceneHierarchySnapshot buildPlaceableSceneHierarchy(
    std::span<const PlaceableInstance> effectivePlaceables,
    bool usesImplicitDefaults);

[[nodiscard]] const char* sceneHierarchyErrorLabel(
    SceneHierarchyError error) noexcept;

} // namespace engine::editor
