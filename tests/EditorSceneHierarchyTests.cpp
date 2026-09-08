#include "engine/editor/SceneHierarchy.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

PlaceableInstance placeable(std::string uuid, std::string slug, float x)
{
    PlaceableInstance instance{};
    instance.uuid = std::move(uuid);
    instance.prototypeSlug = std::move(slug);
    instance.prototypeVersion = 1;
    instance.position = {x, 2.0f, 3.0f};
    instance.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    instance.scale = glm::vec3(1.0f);
    instance.seed = 7;
    return instance;
}

void testStableAuthoredRows()
{
    const std::vector<PlaceableInstance> source{
        placeable("plant-a", "ribbon_kelp", -2.0f),
        placeable("plant-b", "red-ludwigia", 4.0f),
    };
    const auto hierarchy =
        engine::editor::buildPlaceableSceneHierarchy(source, false);

    require(hierarchy && !hierarchy.usesImplicitDefaults &&
                hierarchy.placeables.size() == 2,
            "An explicit valid collection should produce one row per placeable");
    require(hierarchy.placeables[0].label == "Ribbon Kelp #1" &&
                hierarchy.placeables[1].label == "Red Ludwigia #2",
            "Hierarchy labels should be readable and preserve scene order");
    require(hierarchy.placeables[0].uuid == "plant-a" &&
                hierarchy.placeables[0].position == source[0].position &&
                engine::editor::selectionTargetEqual(
                    hierarchy.placeables[0].target,
                    engine::editor::SelectionTarget::placeable("plant-a")),
            "A hierarchy row should retain only the authored UUID selection identity");
}

void testImplicitDefaultsAndEmptyScene()
{
    const std::vector<PlaceableInstance> defaults{
        placeable("default-a", "eelgrass", 1.0f),
    };
    const auto implicit =
        engine::editor::buildPlaceableSceneHierarchy(defaults, true);
    require(implicit && implicit.usesImplicitDefaults &&
                implicit.placeables.size() == 1,
            "Effective defaults should be identified without changing their UUID rows");

    const auto empty = engine::editor::buildPlaceableSceneHierarchy({}, false);
    require(empty && empty.placeables.empty(),
            "A scene with no authorable placeables should be a valid empty hierarchy");
}

void testAllOrNothingValidation()
{
    std::vector<PlaceableInstance> malformed{
        placeable("plant-a", "eelgrass", 1.0f),
        placeable("plant-b", "eelgrass", 2.0f),
    };
    malformed[1].scale.y = 0.0f;
    auto hierarchy =
        engine::editor::buildPlaceableSceneHierarchy(malformed, false);
    require(!hierarchy && hierarchy.placeables.empty() &&
                hierarchy.error ==
                    engine::editor::SceneHierarchyError::InvalidPlaceable &&
                hierarchy.invalidPlaceableIndex == 1,
            "Malformed authored data should reject the complete hierarchy");

    malformed[1] = placeable("plant-a", "forked_sprig", 2.0f);
    hierarchy = engine::editor::buildPlaceableSceneHierarchy(malformed, false);
    require(!hierarchy && hierarchy.placeables.empty() &&
                hierarchy.error == engine::editor::SceneHierarchyError::
                                       DuplicatePlaceableIdentity &&
                hierarchy.invalidPlaceableIndex == 1,
            "Duplicate persistent UUIDs should reject the complete hierarchy");
}

} // namespace

int main()
{
    try
    {
        testStableAuthoredRows();
        testImplicitDefaultsAndEmptyScene();
        testAllOrNothingValidation();
        std::cout << "Editor scene hierarchy tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Editor scene hierarchy tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
