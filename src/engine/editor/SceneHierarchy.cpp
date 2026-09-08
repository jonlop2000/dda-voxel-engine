#include "engine/editor/SceneHierarchy.h"

#include <cctype>
#include <set>
#include <utility>

#include "engine/game/PlaceableTransform.h"

namespace engine::editor
{
namespace
{

std::string displayName(std::string_view slug)
{
    std::string result{};
    result.reserve(slug.size());
    bool capitalize = true;
    for (const char character : slug)
    {
        if (character == '_' || character == '-')
        {
            if (!result.empty() && result.back() != ' ')
            {
                result.push_back(' ');
            }
            capitalize = true;
            continue;
        }

        const unsigned char byte = static_cast<unsigned char>(character);
        result.push_back(capitalize
                             ? static_cast<char>(std::toupper(byte))
                             : character);
        capitalize = character == ' ';
    }
    return result;
}

SceneHierarchySnapshot errorSnapshot(SceneHierarchyError error,
                                     size_t index,
                                     bool usesImplicitDefaults)
{
    SceneHierarchySnapshot result{};
    result.usesImplicitDefaults = usesImplicitDefaults;
    result.error = error;
    result.invalidPlaceableIndex = index;
    return result;
}

} // namespace

SceneHierarchySnapshot buildPlaceableSceneHierarchy(
    std::span<const PlaceableInstance> effectivePlaceables,
    bool usesImplicitDefaults)
{
    SceneHierarchySnapshot result{};
    result.usesImplicitDefaults = usesImplicitDefaults;
    result.placeables.reserve(effectivePlaceables.size());

    std::set<std::string> identities{};
    for (size_t index = 0; index < effectivePlaceables.size(); ++index)
    {
        const PlaceableInstance& placeable = effectivePlaceables[index];
        if (placeable.uuid.empty() || placeable.prototypeSlug.empty() ||
            placeable.prototypeVersion == 0 ||
            !engine::game::validatePlaceableTransform(placeable))
        {
            return errorSnapshot(SceneHierarchyError::InvalidPlaceable, index,
                                 usesImplicitDefaults);
        }
        if (!identities.insert(placeable.uuid).second)
        {
            return errorSnapshot(
                SceneHierarchyError::DuplicatePlaceableIdentity, index,
                usesImplicitDefaults);
        }

        SceneHierarchyPlaceable row{};
        row.target = SelectionTarget::placeable(placeable.uuid);
        row.label = displayName(placeable.prototypeSlug) + " #" +
                    std::to_string(index + 1);
        row.uuid = placeable.uuid;
        row.prototypeSlug = placeable.prototypeSlug;
        row.prototypeVersion = placeable.prototypeVersion;
        row.position = placeable.position;
        result.placeables.push_back(std::move(row));
    }
    return result;
}

const char* sceneHierarchyErrorLabel(SceneHierarchyError error) noexcept
{
    switch (error)
    {
    case SceneHierarchyError::None:
        return "none";
    case SceneHierarchyError::InvalidPlaceable:
        return "invalid authored placeable";
    case SceneHierarchyError::DuplicatePlaceableIdentity:
        return "duplicate authored placeable identity";
    }
    return "unknown scene hierarchy error";
}

} // namespace engine::editor
