#include "engine/scene/SceneConfig.h"

#include "engine/scene/SceneSerializer.h"

bool SceneConfig::loadFromFile(const std::filesystem::path& path)
{
    return loadSceneConfigFromFile(path, *this);
}

bool SceneConfig::saveToFile(const std::filesystem::path& path) const
{
    return saveSceneConfigToFile(path, *this);
}

SceneConfig SceneConfig::empty()
{
    SceneConfig cfg;
    cfg.name = "empty";
    cfg.description = "Empty scene with nothing loaded";
    cfg.loadTestFloor = false;
    cfg.loadVoxelWorld = false;
    cfg.loadOBBVolumes = false;
    cfg.loadGlassPanel = false;
    cfg.enableWater = false;
    cfg.enableGlass = false;
    cfg.enablePointLights = false;
    cfg.loadProceduralWorld = false;
    cfg.loadVoxelImport = false;
    cfg.useMeshTankGlass = false;
    return cfg;
}

SceneConfig SceneConfig::obbTest()
{
    SceneConfig cfg;
    cfg.name = "obb_test";
    cfg.description = "OBB volumes only for testing Day 3+ features";
    cfg.loadTestFloor = true;
    cfg.loadVoxelWorld = false;
    cfg.loadOBBVolumes = true;
    cfg.loadGlassPanel = false;
    cfg.enableWater = false;
    cfg.enableGlass = false;
    cfg.enablePointLights = true;
    cfg.worldDimsX = 1;
    cfg.worldDimsY = 1;
    cfg.worldDimsZ = 1;
    cfg.loadProceduralWorld = false;
    cfg.loadVoxelImport = false;
    cfg.useMeshTankGlass = false;
    return cfg;
}

SceneConfig SceneConfig::voxelWorld()
{
    SceneConfig cfg;
    cfg.name = "voxel_world";
    cfg.description = "Voxel world with terrain generation";
    cfg.loadTestFloor = false;
    cfg.loadVoxelWorld = true;
    cfg.loadOBBVolumes = false;
    cfg.loadGlassPanel = false;
    cfg.enableWater = true;
    cfg.enableGlass = false;
    cfg.enablePointLights = true;
    cfg.loadProceduralWorld = false;
    cfg.loadVoxelImport = false;
    cfg.useMeshTankGlass = false;
    return cfg;
}

SceneConfig SceneConfig::proceduralWorld()
{
    SceneConfig cfg;
    cfg.name = "procedural_world";
    cfg.description = "Procedural voxel volumes with terrain + caves";
    cfg.loadTestFloor = false;
    cfg.loadVoxelWorld = false;
    cfg.loadOBBVolumes = false;
    cfg.loadGlassPanel = false;
    cfg.loadAquariumTest = false;
    cfg.loadProceduralWorld = true;
    cfg.loadVoxelImport = false;
    cfg.useMeshTankGlass = false;
    cfg.enableWater = false;
    cfg.enableGlass = false;
    cfg.enablePointLights = true;
    cfg.cameraPosition = glm::vec3(0.0f, 30.0f, 60.0f);
    cfg.cameraYaw = 0.0f;
    cfg.cameraPitch = -0.35f;
    cfg.worldDimsX = 1;
    cfg.worldDimsY = 1;
    cfg.worldDimsZ = 1;
    return cfg;
}

SceneConfig SceneConfig::voxelImport()
{
    SceneConfig cfg;
    cfg.name = "voxel_import";
    cfg.description = "Imported mesh voxelized into DDA volumes";
    cfg.loadTestFloor = false;
    cfg.loadVoxelWorld = false;
    cfg.loadOBBVolumes = false;
    cfg.loadGlassPanel = false;
    cfg.loadAquariumTest = false;
    cfg.loadProceduralWorld = false;
    cfg.loadVoxelImport = true;
    cfg.useMeshTankGlass = false;
    cfg.enableWater = false;
    cfg.enableGlass = false;
    cfg.enablePointLights = true;
    cfg.cameraPosition = glm::vec3(0.0f, 20.0f, 45.0f);
    cfg.cameraYaw = 0.0f;
    cfg.cameraPitch = -0.3f;
    cfg.worldDimsX = 1;
    cfg.worldDimsY = 1;
    cfg.worldDimsZ = 1;
    return cfg;
}

SceneConfig SceneConfig::fullDemo()
{
    SceneConfig cfg;
    cfg.name = "full_demo";
    cfg.description = "Full demo with all features enabled";
    cfg.loadTestFloor = true;
    cfg.loadVoxelWorld = true;
    cfg.loadOBBVolumes = true;
    cfg.loadGlassPanel = true;
    cfg.enableWater = true;
    cfg.enableGlass = true;
    cfg.enablePointLights = true;
    cfg.loadProceduralWorld = false;
    cfg.loadVoxelImport = false;
    cfg.useMeshTankGlass = false;
    return cfg;
}

SceneConfig SceneConfig::aquariumTest()
{
    SceneConfig cfg;
    cfg.name = "aquarium_test";
    cfg.description = "Aquarium tank scene (outer room shell removed)";
    cfg.loadTestFloor = false;
    cfg.loadVoxelWorld = false;
    cfg.loadOBBVolumes = false;
    cfg.loadGlassPanel = false;
    cfg.loadAquariumTest = true;
    cfg.loadProceduralWorld = false;
    cfg.loadVoxelImport = false;
    cfg.useMeshTankGlass = true;
    cfg.enableWater = true;
    cfg.useWaterV2 = true;
    cfg.enableGlass = true;
    cfg.enablePointLights = true;
    cfg.cameraPosition = glm::vec3(0.0f, 20.0f, 50.0f);
    cfg.cameraYaw = 0.0f;
    cfg.cameraPitch = -0.3f;
    cfg.worldDimsX = 1;
    cfg.worldDimsY = 1;
    cfg.worldDimsZ = 1;
    return cfg;
}

SceneConfig SceneConfig::glassFocus()
{
    SceneConfig cfg;
    cfg.name = "glass_focus";
    cfg.description = "Clean glass test scene with various voxel glass structures";
    cfg.loadTestFloor = false;
    cfg.loadVoxelWorld = false;
    cfg.loadOBBVolumes = false;
    cfg.loadGlassPanel = false;
    cfg.loadAquariumTest = false;
    cfg.loadGlassTestScene = true;
    cfg.loadProceduralWorld = false;
    cfg.loadVoxelImport = false;
    cfg.useMeshTankGlass = false;
    cfg.enableWater = false;
    cfg.enableGlass = true;
    cfg.enablePointLights = true;
    cfg.cameraPosition = glm::vec3(0.0f, 8.0f, 20.0f);
    cfg.cameraYaw = 0.0f;
    cfg.cameraPitch = -0.15f;
    cfg.worldSeed = 1337;
    cfg.worldDimsX = 1;
    cfg.worldDimsY = 1;
    cfg.worldDimsZ = 1;
    return cfg;
}

std::filesystem::path resolveScenePath(const std::filesystem::path& scenesDir,
                                       const std::string& sceneArg)
{
    namespace fs = std::filesystem;

    if (sceneArg.empty())
    {
        return scenesDir / "last_used.json";
    }

    fs::path argPath(sceneArg);
    if (argPath.is_absolute() && fs::exists(argPath))
    {
        return argPath;
    }

    if (argPath.extension() == ".json")
    {
        return scenesDir / argPath;
    }

    return scenesDir / (sceneArg + ".json");
}
