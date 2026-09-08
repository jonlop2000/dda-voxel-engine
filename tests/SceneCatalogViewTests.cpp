#include "UI/PreparedSceneCatalog.h"

#include <iostream>
#include <stdexcept>

namespace
{
using namespace SceneCatalogUi;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

std::vector<SceneCatalogEntry> fixture()
{
    return {
        {"scenes/./full_demo.json", "full_demo.json", "Full Demo", "Starter", true, ""},
        {"scenes/pond/../nature_probe.json", "nature_probe.json", "Nature Pond", "Water garden", true, ""},
        {"scenes/aquarium.json", "aquarium.json", "Tank", "Fish", true, ""},
        {"scenes/beach.json", "beach.json", "Beach", "Nature coast", true, ""},
        {"scenes/glass.json", "glass.json", "Glass", "Transmission", true, ""},
        {"scenes/other.json", "other.json", "Nature Pond", "Fallback duplicate", true, ""},
        {"scenes/broken.json", "broken.json", "Nature Pond", "Broken nature", false, "bad JSON"},
        {"scenes/dense.json", "dense_AQUARIUM.json", "Dense", "Probe takes precedence", true, ""},
        {"scenes/obb.json", "obb_test.json", "OBB", "Primary", true, ""},
        {"scenes/upper.json", "PROCEDURAL.json", "World", "Uppercase file", true, ""},
        {"scenes/invalid_probe.json", "invalid_probe.json", "Bad probe", "", false, "malformed"},
        {"scenes/odd.json", "odd.json", "Caf\xc3\xa9", "UTF-8 text", true, ""},
    };
}

void compare(PreparedSceneCatalog& view, const std::vector<SceneCatalogEntry>& scenes,
             uint64_t revision, const char* filter,
             const std::filesystem::path& activePath, const std::string& activeName)
{
    view.update(scenes, revision, filter, activePath, activeName);
    const auto normalizedPath = activePath.lexically_normal();
    for (const auto& definition : kSceneGroups)
    {
        std::vector<size_t> expected;
        for (size_t i = 0; i < scenes.size(); ++i)
        {
            if (classifyScene(scenes[i]) == definition.group && sceneMatchesFilter(scenes[i], filter))
                expected.push_back(i);
            require(view.isActive(i) == isActiveScene(scenes[i], normalizedPath, activeName),
                    "Prepared active selection must preserve path OR valid-name matching");
        }
        require(view.visible(definition.group) == expected, "Prepared ordering/filter indices differ");
        require(static_cast<int>(expected.size()) == countMatchingScenes(scenes, definition.group, filter),
                "Prepared group count differs");
        require(view.groupIsActive(definition.group) ==
                    groupContainsActiveScene(scenes, definition.group, normalizedPath, activeName),
                "Active groups must include matching entries even when the filter hides them");
    }
}

void testRulesAndParity()
{
    auto scenes = fixture();
    PreparedSceneCatalog view;
    compare(view, scenes, 0, nullptr, {}, "");
    const std::array<std::vector<size_t>, 7> expected{{{0, 8}, {2}, {3, 9}, {4}, {1, 7}, {5, 11}, {6, 10}}};
    const std::array<std::string, 7> labels{{"Primary", "Aquarium", "Worlds", "Rendering", "Probes", "Other", "Invalid"}};
    for (size_t i = 0; i < kSceneGroups.size(); ++i)
    {
        require(view.visible(kSceneGroups[i].group) == expected[i], "Authored grouping/ordering changed");
        require(kSceneGroups[i].label == labels[i], "Group label/order changed");
    }
    for (const char* filter : {"", "nature", "NATURE", "water GARDEN", "json Nature", "no matches", " ", "Caf\xc3\xa9"})
    {
        for (const auto& path : {std::filesystem::path{}, std::filesystem::path{"scenes/./nature_probe.json"},
                                std::filesystem::path{"scenes/broken.json"}, std::filesystem::path{"missing.json"}})
        {
            for (const std::string name : {"", "Nature Pond", "Tank", "nature pond"})
                compare(view, scenes, 0, filter, path, name);
        }
    }
    compare(view, scenes, 0, "no matches", "scenes/broken.json", "Nature Pond");
    require(view.isActive(1) && view.isActive(5) && view.isActive(6), "Preserve multiple-name plus invalid-path matches");
    require(view.groupIsActive(SceneGroup::Probes) && view.groupIsActive(SceneGroup::Other) &&
            view.groupIsActive(SceneGroup::Invalid), "Filtered active groups must retain default-open semantics");
    compare(view, scenes, 0, "nature", {}, "Nature Pond");
    require(!view.isActive(6), "Invalid entry must not match by display-name fallback");
}

void testRefreshAndLifetime()
{
    auto scenes = fixture();
    PreparedSceneCatalog view;
    compare(view, scenes, 1, "nature", "scenes/nature_probe.json", "Nature Pond");
    // same vector object, same size, changed contents: revision is the invalidation key.
    scenes[1] = {"scenes/replaced.json", "glass_replaced.json", "Replaced", "New nature", false, "invalid"};
    compare(view, scenes, 2, "nature", "scenes/replaced.json", "Replaced");
    require(view.visible(SceneGroup::Probes).empty(), "Refresh must discard old classifications");
    require(view.visible(SceneGroup::Invalid) == std::vector<size_t>({1, 6}), "Refresh must update validity/filter results");
    scenes[1].valid = true;
    scenes[1].description = "No longer matches";
    compare(view, scenes, 3, "nature", "scenes/replaced.json", "Replaced");
    require(view.groupIsActive(SceneGroup::Rendering), "Validity refresh must update active group");
    scenes.erase(scenes.begin());
    compare(view, scenes, 4, "", {}, "Tank");
    scenes.clear();
    compare(view, scenes, 5, "nature", {}, "Tank");
    scenes = fixture();
    compare(view, scenes, 6, "nature", {}, "Tank");

    auto otherCatalog = fixture();
    otherCatalog[0].valid = false;
    compare(view, otherCatalog, 6, "", {}, "Full Demo");
    require(view.visible(SceneGroup::Primary) == std::vector<size_t>({8}), "New catalog identity must invalidate equal revision");
    view = {};
    compare(view, otherCatalog, 0, nullptr, {}, "");
}
} // namespace

int main()
{
    try
    {
        testRulesAndParity();
        testRefreshAndLifetime();
        std::cout << "Scene catalog prepared/legacy behavior tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
