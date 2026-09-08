#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/scene/SceneCatalog.h"

// pure catalog rules shared by the legacy ui and prepared view.
namespace SceneCatalogUi
{

enum class SceneGroup
{
    Primary,
    Aquarium,
    Worlds,
    Rendering,
    Probes,
    Other,
    Invalid,
};

struct SceneGroupDefinition
{
    SceneGroup group;
    const char* label;
};

inline constexpr std::array<SceneGroupDefinition, 7> kSceneGroups{{
    {SceneGroup::Primary, "Primary"},
    {SceneGroup::Aquarium, "Aquarium"},
    {SceneGroup::Worlds, "Worlds"},
    {SceneGroup::Rendering, "Rendering"},
    {SceneGroup::Probes, "Probes"},
    {SceneGroup::Other, "Other"},
    {SceneGroup::Invalid, "Invalid"},
}};

inline std::string lowercaseCopy(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

inline bool containsText(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

inline SceneGroup classifyScene(const SceneCatalogEntry& scene)
{
    if (!scene.valid)
    {
        return SceneGroup::Invalid;
    }

    const std::string key = lowercaseCopy(scene.fileName + " " + scene.displayName);
    if (containsText(key, "probe") || containsText(key, "dense_"))
    {
        return SceneGroup::Probes;
    }
    if (containsText(key, "aquarium"))
    {
        return SceneGroup::Aquarium;
    }
    if (containsText(key, "beach") || containsText(key, "procedural") ||
        containsText(key, "voxel_world") || containsText(key, "voxel_import"))
    {
        return SceneGroup::Worlds;
    }
    if (containsText(key, "glass"))
    {
        return SceneGroup::Rendering;
    }
    if (containsText(key, "empty") || containsText(key, "full_demo") ||
        containsText(key, "obb_test"))
    {
        return SceneGroup::Primary;
    }
    return SceneGroup::Other;
}

inline bool sceneMatchesFilter(const SceneCatalogEntry& scene, const char* filter)
{
    if (filter == nullptr || filter[0] == '\0')
    {
        return true;
    }

    const std::string key =
        lowercaseCopy(scene.fileName + " " + scene.displayName + " " + scene.description);
    return key.find(lowercaseCopy(filter)) != std::string::npos;
}

inline bool isActiveScene(const SceneCatalogEntry& scene, const std::filesystem::path& activePath,
                          const std::string& activeName)
{
    if (!activePath.empty() && scene.path.lexically_normal() == activePath)
    {
        return true;
    }
    return scene.valid && !activeName.empty() && scene.displayName == activeName;
}

inline int countMatchingScenes(const std::vector<SceneCatalogEntry>& scenes, SceneGroup group,
                               const char* filter)
{
    int count = 0;
    for (const SceneCatalogEntry& scene : scenes)
    {
        if (classifyScene(scene) == group && sceneMatchesFilter(scene, filter))
        {
            ++count;
        }
    }
    return count;
}

inline bool groupContainsActiveScene(const std::vector<SceneCatalogEntry>& scenes, SceneGroup group,
                                     const std::filesystem::path& activePath,
                                     const std::string& activeName)
{
    for (const SceneCatalogEntry& scene : scenes)
    {
        if (classifyScene(scene) == group && isActiveScene(scene, activePath, activeName))
        {
            return true;
        }
    }
    return false;
}

} // namespace SceneCatalogUi
