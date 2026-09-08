#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "engine/game/GameState.h"
#include "engine/game/Placeables.h"
#include "engine/scene/EnvironmentTime.h"
#include "engine/scene/EnvironmentWind.h"
#include "engine/scene/ScenePresentationProfile.h"
#include "engine/scene/WindborneParticleSettings.h"

struct SceneConfig
{
    static constexpr uint32_t kCurrentFormatVersion = 10;

    // persisted scene schema version.
    uint32_t version = kCurrentFormatVersion;

    // content flags - what to load at startup
    bool loadTestFloor = true;
    bool loadVoxelWorld = true;
    bool loadOBBVolumes = true;
    bool loadGlassPanel = true;
    bool loadAquariumTest = false;
    bool loadGlassTestScene = false;
    bool loadProceduralWorld = false;
    bool loadVoxelImport = false;
    bool useMeshTankGlass = false;

    // runtime toggles
    bool enableWater = true;
    bool useWaterV2 = false;
    bool enableGlass = true;
    bool enablePointLights = true;

    // shared production wind authority for foliage and environmental motion.
    // schema v8 persists this independently from the legacy cloud drift controls.
    engine::scene::EnvironmentWindSettings environmentWind{};

    // scene-owned deterministic environmental clock. schema v10 introduces
    // this as explicit opt-in state; schemas v0-v9 remain compatibility-off.
    engine::scene::EnvironmentTimeSettings environmentTime{};

    // bounded airborne ambient effect. schema v9 adds authoring while schemas
    // v0-v8 stay compatibility-off. motion comes exclusively from environmentWind.
    engine::scene::WindborneParticleSettings windborneParticles{};

    // experimental voxel-cloud settings. new scenes opt in explicitly; versioned
    // files retain their frozen historical defaults during deserialization.
    bool loadCloudScene = false;
    uint32_t cloudSeed = 42;
    float cloudAltitude = 100.0f;
    float cloudWindSpeed = 0.5f;   // units per second for drift animation
    glm::vec2 cloudWindDirection{0.0f, 1.0f};  // normalized xz direction (default: +z)
    float cloudCoverage = 0.4f;    // 0.0 = sparse, 1.0 = dense (default 0.4 for realistic spacing)
    float cloudScale = 48.0f;      // noise scale - larger = bigger cloud formations with more gaps
    float cloudShadowStrength = 0.35f; // 0.0 = no cloud sun attenuation, 1.0 = strongest attenuation

    // sky settings
    int skyPreset = 0;             // 0=day, 1=sunset, 2=night, 3=dawn, 4=custom
    glm::vec3 skyColor{0.09f, 0.29f, 0.88f};  // default blue sky

    // star settings (visible mainly at night)
    bool loadStarScene = true;
    uint32_t starSeed = 123;
    float starAltitude = 150.0f;   // higher than clouds
    float starDensity = 0.02f;     // very sparse (0.01-0.1)

    // camera
    glm::vec3 cameraPosition{0.0f, 5.0f, 10.0f};
    float cameraYaw = 0.0f;
    float cameraPitch = -0.2f;

    // voxel world settings
    uint32_t worldSeed = 1337;
    int worldDimsX = 8;
    int worldDimsY = 2;
    int worldDimsZ = 8;

    // Player/placeable content. older authored aquarium scenes leave this empty and use
    // catalog-provided defaults until the editor writes explicit placeables.
    bool useDefaultPlaceables = true;
    std::vector<PlaceableInstance> placeables{};

    // saved game state used by the runtime care loop and persisted with the scene.
    GameState gameState{};

    // complete persisted look state (schema v5+; profile v1 fields require scene v6).
    // absent means the scene relies on the
    // scene-name default helpers; when present it is the final authority after them.
    std::optional<engine::scene::ScenePresentationProfile> presentationProfile{};

    // scene metadata
    std::string name = "default";
    std::string description;

    // Load/save from json file
    bool loadFromFile(const std::filesystem::path& path);
    bool saveToFile(const std::filesystem::path& path) const;

    // factory methods for common scene presets
    static SceneConfig empty();
    static SceneConfig obbTest();
    static SceneConfig voxelWorld();
    static SceneConfig proceduralWorld();
    static SceneConfig voxelImport();
    static SceneConfig fullDemo();
    static SceneConfig aquariumTest();
    static SceneConfig glassFocus();
};

// resolves scene file path from name or path
std::filesystem::path resolveScenePath(const std::filesystem::path& scenesDir,
                                       const std::string& sceneArg);
