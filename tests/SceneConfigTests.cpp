#include "engine/scene/SceneCatalog.h"
#include "engine/scene/SceneConfig.h"
#include "engine/scene/ScenePresentationProfile.h"
#include "engine/scene/ScenePresentationProfileVariants.h"
#include "Core/RuntimeContentManifest.h"
#include "engine/game/OfflineCare.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace
{

struct CurrentPathGuard
{
    fs::path previousPath;

    explicit CurrentPathGuard(const fs::path& newPath)
        : previousPath(fs::current_path())
    {
        fs::current_path(newPath);
    }

    ~CurrentPathGuard()
    {
        std::error_code ec;
        fs::current_path(previousPath, ec);
    }
};

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const auto seed = std::chrono::steady_clock::now()
                              .time_since_epoch()
                              .count();
        for (unsigned int attempt = 0; attempt < 64; ++attempt)
        {
            const fs::path candidate =
                fs::temp_directory_path() /
                ("dda-voxel-scene-config-" + std::to_string(seed) + "-" +
                 std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(candidate, error))
            {
                path_ = candidate;
                return;
            }
            if (error)
            {
                throw std::runtime_error(
                    "Failed to create SceneConfig test temporary directory: " +
                    error.message());
            }
        }
        throw std::runtime_error(
            "Could not reserve a unique SceneConfig test temporary directory");
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_{};
};

bool nearlyEqual(float a, float b, float epsilon = 1e-5f)
{
    return std::fabs(a - b) <= epsilon;
}

void writeTextFile(const fs::path& path, const std::string& contents)
{
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to create test file: " + path.string());
    }

    file << contents;
    file.flush();
    require(static_cast<bool>(file), "Failed to write test file: " + path.string());
}

void testRoundTripSaveLoad(const fs::path& tempDir)
{
    SceneConfig saved = SceneConfig::glassFocus();
    saved.version = SceneConfig::kCurrentFormatVersion;
    saved.useWaterV2 = true;
    saved.loadCloudScene = true;
    saved.cloudSeed = 99;
    saved.cloudAltitude = 180.0f;
    saved.cloudWindSpeed = 1.75f;
    saved.cloudWindDirection = {0.6f, 0.8f};
    saved.cloudCoverage = 0.55f;
    saved.cloudScale = 64.0f;
    saved.cloudShadowStrength = 0.42f;
    saved.environmentWind.direction = {0.6f, 0.8f};
    saved.environmentWind.speed = 2.25f;
    saved.environmentWind.strength = 1.35f;
    saved.environmentWind.gustStrength = 0.45f;
    saved.environmentWind.gustFrequencyHz = 0.3f;
    saved.environmentWind.turbulenceStrength = 0.55f;
    saved.environmentWind.verticalLift = 0.25f;
    saved.environmentTime.enabled = true;
    saved.environmentTime.cycleEnabled = true;
    saved.environmentTime.timeOfDayHours = 18.5f;
    saved.environmentTime.dayLengthMinutes = 24.0f;
    saved.windborneParticles.enabled = true;
    saved.windborneParticles.amount = 0.42f;
    saved.windborneParticles.leafFraction = 0.58f;
    saved.windborneParticles.scale = 1.25f;
    saved.windborneParticles.visibilityDistance = 48.0f;
    saved.skyPreset = 4;
    saved.skyColor = {0.15f, 0.2f, 0.35f};
    saved.loadStarScene = true;
    saved.starSeed = 321;
    saved.starAltitude = 250.0f;
    saved.starDensity = 0.08f;
    saved.cameraPosition = {2.0f, 4.0f, 6.0f};
    saved.cameraYaw = 1.25f;
    saved.cameraPitch = -0.45f;
    saved.worldSeed = 9001;
    saved.worldDimsX = 12;
    saved.worldDimsY = 4;
    saved.worldDimsZ = 10;
    saved.useDefaultPlaceables = false;
    PlaceableInstance savedPlaceable{};
    savedPlaceable.uuid = "00000000-0000-4000-8000-00000000test";
    savedPlaceable.prototypeSlug = "eelgrass";
    savedPlaceable.prototypeVersion = 1;
    savedPlaceable.position = {1.0f, 2.0f, 3.0f};
    savedPlaceable.rotation = {0.9238795f, 0.0f, 0.3826834f, 0.0f};
    savedPlaceable.scale = {1.0f, 1.1f, 0.9f};
    savedPlaceable.seed = 42u;
    saved.placeables.push_back(savedPlaceable);
    saved.gameState.lastPlayedUtc = "2026-06-19T04:30:00Z";
    saved.gameState.water.temperatureC = 25.5f;
    saved.gameState.water.oxygen = 0.76f;
    saved.gameState.water.flow = 0.38f;
    saved.gameState.water.cleanliness = 0.91f;
    saved.gameState.progression.coins = 125u;
    saved.gameState.progression.discovery = 7u;
    saved.gameState.progression.careMilestone = 3u;
    saved.gameState.progression.tankTier = 2u;
    CreatureInstance savedCreature{};
    savedCreature.uuid = "creature-primary-0001";
    savedCreature.speciesId = "starter_fish";
    savedCreature.displayName = "Pixel";
    savedCreature.primary = true;
    savedCreature.bond = 0.35f;
    savedCreature.vitality = 0.88f;
    savedCreature.needs.hunger = 0.25f;
    savedCreature.needs.cleanliness = 0.90f;
    savedCreature.needs.happiness = 0.80f;
    savedCreature.needs.health = 0.95f;
    saved.gameState.creatures.push_back(savedCreature);
    saved.name = "round_trip_scene";
    saved.description = "Round trip test";

    const fs::path scenePath = tempDir / "round_trip.json";
    require(saved.saveToFile(scenePath), "saveToFile should succeed for round-trip test");
    require(fs::exists(scenePath), "Saved scene file should exist");
    require(!fs::exists(scenePath.string() + ".tmp"),
            "Atomic save temp file should not remain after save");

    SceneConfig loaded;
    require(loaded.loadFromFile(scenePath), "loadFromFile should succeed for round-trip test");

    require(loaded.version == SceneConfig::kCurrentFormatVersion, "Scene version should round-trip");
    require(loaded.loadGlassTestScene == saved.loadGlassTestScene, "loadGlassTestScene mismatch");
    require(loaded.useMeshTankGlass == saved.useMeshTankGlass, "useMeshTankGlass mismatch");
    require(loaded.useWaterV2 == saved.useWaterV2, "useWaterV2 mismatch");
    require(loaded.cloudSeed == saved.cloudSeed, "cloudSeed mismatch");
    require(nearlyEqual(loaded.cloudAltitude, saved.cloudAltitude), "cloudAltitude mismatch");
    require(nearlyEqual(loaded.cloudWindSpeed, saved.cloudWindSpeed), "cloudWindSpeed mismatch");
    require(nearlyEqual(loaded.cloudWindDirection.x, saved.cloudWindDirection.x),
            "cloudWindDirection.x mismatch");
    require(nearlyEqual(loaded.cloudWindDirection.y, saved.cloudWindDirection.y),
            "cloudWindDirection.y mismatch");
    require(nearlyEqual(loaded.cloudCoverage, saved.cloudCoverage), "cloudCoverage mismatch");
    require(nearlyEqual(loaded.cloudScale, saved.cloudScale), "cloudScale mismatch");
    require(nearlyEqual(loaded.cloudShadowStrength, saved.cloudShadowStrength),
            "cloudShadowStrength mismatch");
    require(nearlyEqual(loaded.environmentWind.direction.x,
                        saved.environmentWind.direction.x),
            "environmentWind.direction.x mismatch");
    require(nearlyEqual(loaded.environmentWind.direction.y,
                        saved.environmentWind.direction.y),
            "environmentWind.direction.y mismatch");
    require(nearlyEqual(loaded.environmentWind.speed, saved.environmentWind.speed),
            "environmentWind.speed mismatch");
    require(nearlyEqual(loaded.environmentWind.strength,
                        saved.environmentWind.strength),
            "environmentWind.strength mismatch");
    require(nearlyEqual(loaded.environmentWind.gustStrength,
                        saved.environmentWind.gustStrength),
            "environmentWind.gustStrength mismatch");
    require(nearlyEqual(loaded.environmentWind.gustFrequencyHz,
                        saved.environmentWind.gustFrequencyHz),
            "environmentWind.gustFrequencyHz mismatch");
    require(nearlyEqual(loaded.environmentWind.turbulenceStrength,
                        saved.environmentWind.turbulenceStrength),
            "environmentWind.turbulenceStrength mismatch");
    require(nearlyEqual(loaded.environmentWind.verticalLift,
                        saved.environmentWind.verticalLift),
            "environmentWind.verticalLift mismatch");
    require(engine::scene::environmentTimeSettingsEquivalent(
                loaded.environmentTime, saved.environmentTime),
            "environmentTime mismatch");
    require(engine::scene::windborneParticleSettingsEquivalent(
                loaded.windborneParticles, saved.windborneParticles),
            "windborneParticles mismatch");
    require(loaded.skyPreset == saved.skyPreset, "skyPreset mismatch");
    require(nearlyEqual(loaded.skyColor.x, saved.skyColor.x), "skyColor.x mismatch");
    require(nearlyEqual(loaded.skyColor.y, saved.skyColor.y), "skyColor.y mismatch");
    require(nearlyEqual(loaded.skyColor.z, saved.skyColor.z), "skyColor.z mismatch");
    require(loaded.starSeed == saved.starSeed, "starSeed mismatch");
    require(nearlyEqual(loaded.starAltitude, saved.starAltitude), "starAltitude mismatch");
    require(nearlyEqual(loaded.starDensity, saved.starDensity), "starDensity mismatch");
    require(nearlyEqual(loaded.cameraPosition.x, saved.cameraPosition.x),
            "cameraPosition.x mismatch");
    require(nearlyEqual(loaded.cameraPosition.y, saved.cameraPosition.y),
            "cameraPosition.y mismatch");
    require(nearlyEqual(loaded.cameraPosition.z, saved.cameraPosition.z),
            "cameraPosition.z mismatch");
    require(nearlyEqual(loaded.cameraYaw, saved.cameraYaw), "cameraYaw mismatch");
    require(nearlyEqual(loaded.cameraPitch, saved.cameraPitch), "cameraPitch mismatch");
    require(loaded.worldSeed == saved.worldSeed, "worldSeed mismatch");
    require(loaded.worldDimsX == saved.worldDimsX, "worldDimsX mismatch");
    require(loaded.worldDimsY == saved.worldDimsY, "worldDimsY mismatch");
    require(loaded.worldDimsZ == saved.worldDimsZ, "worldDimsZ mismatch");
    require(loaded.useDefaultPlaceables == saved.useDefaultPlaceables,
            "useDefaultPlaceables mismatch");
    require(loaded.placeables.size() == 1, "placeables size mismatch");
    require(loaded.placeables[0].uuid == savedPlaceable.uuid, "placeable uuid mismatch");
    require(loaded.placeables[0].prototypeSlug == savedPlaceable.prototypeSlug,
            "placeable prototypeSlug mismatch");
    require(loaded.placeables[0].prototypeVersion == savedPlaceable.prototypeVersion,
            "placeable prototypeVersion mismatch");
    require(nearlyEqual(loaded.placeables[0].position.x, savedPlaceable.position.x),
            "placeable position.x mismatch");
    require(nearlyEqual(loaded.placeables[0].position.y, savedPlaceable.position.y),
            "placeable position.y mismatch");
    require(nearlyEqual(loaded.placeables[0].position.z, savedPlaceable.position.z),
            "placeable position.z mismatch");
    require(nearlyEqual(loaded.placeables[0].rotation.w, savedPlaceable.rotation.w),
            "placeable rotation.w mismatch");
    require(nearlyEqual(loaded.placeables[0].rotation.x, savedPlaceable.rotation.x),
            "placeable rotation.x mismatch");
    require(nearlyEqual(loaded.placeables[0].rotation.y, savedPlaceable.rotation.y),
            "placeable rotation.y mismatch");
    require(nearlyEqual(loaded.placeables[0].rotation.z, savedPlaceable.rotation.z),
            "placeable rotation.z mismatch");
    require(nearlyEqual(loaded.placeables[0].scale.x, savedPlaceable.scale.x),
            "placeable scale.x mismatch");
    require(nearlyEqual(loaded.placeables[0].scale.y, savedPlaceable.scale.y),
            "placeable scale.y mismatch");
    require(nearlyEqual(loaded.placeables[0].scale.z, savedPlaceable.scale.z),
            "placeable scale.z mismatch");
    require(loaded.placeables[0].seed == savedPlaceable.seed, "placeable seed mismatch");
    require(loaded.gameState.lastPlayedUtc == saved.gameState.lastPlayedUtc,
            "gameState lastPlayedUtc mismatch");
    require(nearlyEqual(loaded.gameState.water.temperatureC,
                        saved.gameState.water.temperatureC),
            "gameState water.temperatureC mismatch");
    require(nearlyEqual(loaded.gameState.water.oxygen, saved.gameState.water.oxygen),
            "gameState water.oxygen mismatch");
    require(nearlyEqual(loaded.gameState.water.flow, saved.gameState.water.flow),
            "gameState water.flow mismatch");
    require(nearlyEqual(loaded.gameState.water.cleanliness,
                        saved.gameState.water.cleanliness),
            "gameState water.cleanliness mismatch");
    require(loaded.gameState.progression.coins == saved.gameState.progression.coins,
            "gameState progression.coins mismatch");
    require(loaded.gameState.progression.discovery ==
                saved.gameState.progression.discovery,
            "gameState progression.discovery mismatch");
    require(loaded.gameState.progression.careMilestone ==
                saved.gameState.progression.careMilestone,
            "gameState progression.careMilestone mismatch");
    require(loaded.gameState.progression.tankTier == saved.gameState.progression.tankTier,
            "gameState progression.tankTier mismatch");
    require(loaded.gameState.creatures.size() == 1, "gameState creatures size mismatch");
    require(loaded.gameState.creatures[0].uuid == savedCreature.uuid,
            "creature uuid mismatch");
    require(loaded.gameState.creatures[0].speciesId == savedCreature.speciesId,
            "creature speciesId mismatch");
    require(loaded.gameState.creatures[0].displayName == savedCreature.displayName,
            "creature displayName mismatch");
    require(loaded.gameState.creatures[0].primary == savedCreature.primary,
            "creature primary mismatch");
    require(nearlyEqual(loaded.gameState.creatures[0].bond, savedCreature.bond),
            "creature bond mismatch");
    require(nearlyEqual(loaded.gameState.creatures[0].vitality, savedCreature.vitality),
            "creature vitality mismatch");
    require(nearlyEqual(loaded.gameState.creatures[0].needs.hunger,
                        savedCreature.needs.hunger),
            "creature needs.hunger mismatch");
    require(nearlyEqual(loaded.gameState.creatures[0].needs.cleanliness,
                        savedCreature.needs.cleanliness),
            "creature needs.cleanliness mismatch");
    require(nearlyEqual(loaded.gameState.creatures[0].needs.happiness,
                        savedCreature.needs.happiness),
            "creature needs.happiness mismatch");
    require(nearlyEqual(loaded.gameState.creatures[0].needs.health,
                        savedCreature.needs.health),
            "creature needs.health mismatch");
    require(loaded.name == saved.name, "name mismatch");
    require(loaded.description == saved.description, "description mismatch");
}

void testOfflineCareSaveReloadIsIdempotent(const fs::path& tempDir)
{
    SceneConfig saved = SceneConfig::aquariumTest();
    saved.gameState.lastPlayedUtc = "2026-08-13T10:00:00Z";
    CreatureInstance primary{};
    primary.uuid = "offline-primary";
    primary.primary = true;
    primary.needs.hunger = 0.20f;
    saved.gameState.creatures.push_back(primary);
    const auto now =
        engine::game::parseCareUtcTimestamp("2026-08-13T12:00:00Z");
    require(now.has_value(), "Fixed offline integration UTC should parse");
    const auto applied =
        engine::game::reconcileOfflineCare(saved.gameState, *now);
    require(applied.appliedSeconds == 7200,
            "Pre-save offline reconciliation should consume the fixed gap");
    const GameState consumed = saved.gameState;

    const fs::path scenePath = tempDir / "offline_care_consumed.json";
    require(saved.saveToFile(scenePath),
            "Consumed offline state should serialize through the current schema");
    SceneConfig loaded{};
    require(loaded.loadFromFile(scenePath),
            "Consumed offline state should reload through the current schema");
    const auto repeated =
        engine::game::reconcileOfflineCare(loaded.gameState, *now);
    require(repeated.timestampStatus ==
                engine::game::OfflineCareTimestampStatus::Current &&
                repeated.appliedSeconds == 0 && !repeated.stateChanged() &&
                loaded.gameState.lastPlayedUtc == consumed.lastPlayedUtc &&
                nearlyEqual(loaded.gameState.water.cleanliness,
                            consumed.water.cleanliness) &&
                nearlyEqual(loaded.gameState.water.oxygen,
                            consumed.water.oxygen) &&
                nearlyEqual(loaded.gameState.creatures.front().needs.hunger,
                            consumed.creatures.front().needs.hunger),
            "Reconcile-save-reload at the same UTC must not apply care twice");
}

void testLegacyVersionlessLoad(const fs::path& tempDir)
{
    const fs::path scenePath = tempDir / "legacy_scene.json";
    writeTextFile(scenePath,
                  "{\n"
                  "  \"name\": \"legacy_scene\",\n"
                  "  \"loadVoxelWorld\": false,\n"
                  "  \"worldDimsX\": 16,\n"
                  "  \"worldDimsY\": 3,\n"
                  "  \"worldDimsZ\": 12,\n"
                  "  \"cloudWindDirection\": [0.0, 2.0]\n"
                  "}\n");

    SceneConfig loaded;
    require(loaded.loadFromFile(scenePath), "Versionless legacy scene should load");
    require(loaded.version == SceneConfig::kCurrentFormatVersion,
            "Legacy scene should upgrade to current in-memory version");
    require(loaded.name == "legacy_scene", "Legacy scene name mismatch");
    require(!loaded.loadVoxelWorld, "Legacy scene loadVoxelWorld mismatch");
    require(loaded.worldDimsX == 16, "Legacy scene worldDimsX mismatch");
    require(loaded.worldDimsY == 3, "Legacy scene worldDimsY mismatch");
    require(loaded.worldDimsZ == 12, "Legacy scene worldDimsZ mismatch");
    require(nearlyEqual(loaded.cloudWindDirection.x, 0.0f),
            "Legacy scene cloudWindDirection.x mismatch");
    require(nearlyEqual(loaded.cloudWindDirection.y, 1.0f),
            "Legacy scene cloudWindDirection.y should be normalized");
    require(loaded.gameState.creatures.empty(),
            "Legacy scene should default to an empty creature list");
    require(nearlyEqual(loaded.gameState.water.temperatureC, 24.0f),
            "Legacy scene should default game water temperature");
    require(loaded.gameState.progression.tankTier == 1,
            "Legacy scene should default progression tank tier");
    const engine::scene::EnvironmentWindSettings defaultWind =
        engine::scene::defaultEnvironmentWindSettings();
    require(engine::scene::environmentWindSettingsEquivalent(
                loaded.environmentWind, defaultWind),
            "Versionless scene should receive the production wind default");
    require(engine::scene::windborneParticleSettingsEquivalent(
                loaded.windborneParticles,
                engine::scene::defaultWindborneParticleSettings()) &&
                !loaded.windborneParticles.enabled,
            "Versionless scene should keep windborne particles compatibility-off");
    require(!loaded.environmentTime.enabled,
            "Versionless scene should keep time of day compatibility-off");
}

void testLoadFailureDoesNotMutateConfig(const fs::path& tempDir)
{
    const fs::path scenePath = tempDir / "invalid_scene.json";
    writeTextFile(scenePath,
                  "{\n"
                  "  \"version\": 1,\n"
                  "  \"loadTestFloor\": false,\n"
                  "  \"worldDimsX\": \"bad\"\n"
                  "}\n");

    SceneConfig cfg = SceneConfig::obbTest();
    const SceneConfig original = cfg;
    require(!cfg.loadFromFile(scenePath), "Invalid scene should fail to load");

    require(cfg.version == original.version, "Failed load should preserve version");
    require(cfg.loadTestFloor == original.loadTestFloor,
            "Failed load should preserve earlier fields");
    require(cfg.worldDimsX == original.worldDimsX, "Failed load should preserve worldDimsX");
    require(cfg.name == original.name, "Failed load should preserve name");
}

void testFutureVersionRejected(const fs::path& tempDir)
{
    const fs::path scenePath = tempDir / "future_scene.json";
    writeTextFile(scenePath,
                  "{\n"
                  "  \"version\": 999,\n"
                  "  \"name\": \"future_scene\"\n"
                  "}\n");

    SceneConfig cfg = SceneConfig::fullDemo();
    const SceneConfig original = cfg;
    require(!cfg.loadFromFile(scenePath), "Unsupported future version should fail to load");
    require(cfg.name == original.name, "Future-version rejection should not mutate config");
    require(cfg.loadVoxelWorld == original.loadVoxelWorld,
            "Future-version rejection should preserve config state");
}

void testVersionedScenesUseFrozenDefaults(const fs::path& tempDir)
{
    const fs::path scenePath = tempDir / "partial_v1_scene.json";
    writeTextFile(scenePath,
                  "{\n"
                  "  \"version\": 1,\n"
                  "  \"name\": \"partial_v1_scene\"\n"
                  "}\n");

    SceneConfig loaded;
    require(loaded.loadFromFile(scenePath), "Partial versioned scene should load");
    require(loaded.version == SceneConfig::kCurrentFormatVersion,
            "Partial versioned scene should upgrade to current in-memory version");
    require(loaded.loadCloudScene, "Frozen v1 default for loadCloudScene should remain true");
    require(nearlyEqual(loaded.cloudCoverage, 0.4f),
            "Frozen v1 default for cloudCoverage should remain stable");
    require(loaded.loadStarScene, "Frozen v1 default for loadStarScene should remain true");
    require(loaded.worldDimsX == 8, "Frozen v1 default for worldDimsX should remain stable");
    require(loaded.worldDimsY == 2, "Frozen v1 default for worldDimsY should remain stable");
    require(loaded.worldDimsZ == 8, "Frozen v1 default for worldDimsZ should remain stable");
    require(loaded.useDefaultPlaceables,
            "Frozen v1 default for useDefaultPlaceables should remain true");
    require(loaded.placeables.empty(), "Frozen v1 default for placeables should be empty");
    require(loaded.gameState.lastPlayedUtc.empty(),
            "Frozen v1 default for gameState.lastPlayedUtc should be empty");
    require(nearlyEqual(loaded.gameState.water.oxygen, 0.82f),
            "Frozen v1 default for gameState.water.oxygen should remain stable");
    require(loaded.gameState.creatures.empty(),
            "Frozen v1 default for gameState.creatures should be empty");
    require(!loaded.presentationProfile.has_value(),
            "Frozen v1 default for presentationProfile should be absent");
    require(engine::scene::environmentWindSettingsEquivalent(
                loaded.environmentWind,
                engine::scene::defaultEnvironmentWindSettings()),
            "Frozen v1 scenes should receive the production wind default");
    require(!loaded.windborneParticles.enabled,
            "Frozen v1 scenes should keep windborne particles compatibility-off");
    require(!loaded.environmentTime.enabled,
            "Frozen v1 scenes should keep time of day compatibility-off");

    const fs::path v7Path = tempDir / "partial_v7_scene.json";
    writeTextFile(v7Path,
                  "{\n"
                  "  \"version\": 7,\n"
                  "  \"name\": \"partial_v7_scene\"\n"
                  "}\n");
    SceneConfig v7;
    require(v7.loadFromFile(v7Path), "Partial schema-v7 scene should load");
    require(v7.version == SceneConfig::kCurrentFormatVersion,
            "Schema-v7 scene should upgrade to current in-memory version");
    require(engine::scene::environmentWindSettingsEquivalent(
                v7.environmentWind,
                engine::scene::defaultEnvironmentWindSettings()),
            "Schema-v7 scene should receive the production wind default");

    const fs::path v8Path = tempDir / "partial_v8_scene.json";
    writeTextFile(v8Path,
                  "{\n"
                  "  \"version\": 8,\n"
                  "  \"name\": \"partial_v8_scene\",\n"
                  "  \"environmentWind\": {\n"
                  "    \"direction\": [-0.6, 0.8],\n"
                  "    \"speed\": 1.75,\n"
                  "    \"strength\": 0.85\n"
                  "  },\n"
                  "  \"windborneParticles\": {\n"
                  "    \"enabled\": true,\n"
                  "    \"amount\": 1.0\n"
                  "  }\n"
                  "}\n");
    SceneConfig v8;
    require(v8.loadFromFile(v8Path), "Partial schema-v8 scene should load");
    require(v8.version == SceneConfig::kCurrentFormatVersion,
            "Schema-v8 scene should upgrade to current in-memory version");
    require(nearlyEqual(v8.environmentWind.direction.x, -0.6f) &&
                nearlyEqual(v8.environmentWind.direction.y, 0.8f) &&
                nearlyEqual(v8.environmentWind.speed, 1.75f) &&
                nearlyEqual(v8.environmentWind.strength, 0.85f),
            "Schema-v8 scene should preserve its authored environment wind");
    require(engine::scene::windborneParticleSettingsEquivalent(
                v8.windborneParticles,
                engine::scene::defaultWindborneParticleSettings()) &&
                !v8.windborneParticles.enabled,
            "Schema-v8 scene must ignore future-looking particle authoring");

    const fs::path v9Path = tempDir / "partial_v9_scene.json";
    writeTextFile(v9Path,
                  "{\n"
                  "  \"version\": 9,\n"
                  "  \"name\": \"partial_v9_scene\",\n"
                  "  \"environmentTime\": {\n"
                  "    \"enabled\": true,\n"
                  "    \"cycleEnabled\": true,\n"
                  "    \"timeOfDayHours\": 18.0\n"
                  "  }\n"
                  "}\n");
    SceneConfig v9;
    require(v9.loadFromFile(v9Path), "Partial schema-v9 scene should load");
    require(!v9.environmentTime.enabled && !v9.environmentTime.cycleEnabled &&
                nearlyEqual(v9.environmentTime.timeOfDayHours, 12.0f),
            "Schema-v9 scene must ignore future-looking time-of-day authoring");

    const fs::path currentPath = tempDir / "partial_current_scene.json";
    writeTextFile(currentPath,
                  std::string("{\n") +
                      "  \"version\": " +
                      std::to_string(SceneConfig::kCurrentFormatVersion) + ",\n" +
                      "  \"name\": \"partial_current_scene\"\n" +
                      "}\n");
    SceneConfig current;
    require(current.loadFromFile(currentPath),
            "Partial current-version scene should load");
    require(!current.loadCloudScene,
            "Current scene schema must use the cloudless product default");
    require(!current.environmentTime.enabled,
            "Current scene schema must keep time of day opt-in");
}

void testEnvironmentTimeValidation(const fs::path& tempDir)
{
    const fs::path scenePath = tempDir / "environment_time_validation.json";
    writeTextFile(scenePath,
                  std::string("{\n") +
                      "  \"version\": " +
                      std::to_string(SceneConfig::kCurrentFormatVersion) + ",\n"
                      "  \"name\": \"environment_time_validation\",\n"
                      "  \"environmentTime\": {\n"
                      "    \"enabled\": true,\n"
                      "    \"cycleEnabled\": true,\n"
                      "    \"timeOfDayHours\": -49.0,\n"
                      "    \"dayLengthMinutes\": 999.0\n"
                      "  }\n"
                      "}\n");

    SceneConfig loaded;
    require(loaded.loadFromFile(scenePath),
            "Scene with out-of-range environment time should load after validation");
    require(loaded.environmentTime.enabled && loaded.environmentTime.cycleEnabled,
            "Environment time validation should preserve enablement");
    require(nearlyEqual(loaded.environmentTime.timeOfDayHours, 23.0f),
            "Environment time should wrap into the supported 24-hour range");
    require(nearlyEqual(loaded.environmentTime.dayLengthMinutes, 240.0f),
            "Environment day length should clamp to its supported maximum");
}

void testEnvironmentWindValidation(const fs::path& tempDir)
{
    const fs::path scenePath = tempDir / "environment_wind_validation.json";
    writeTextFile(scenePath,
                  std::string("{\n") +
                      "  \"version\": " +
                      std::to_string(SceneConfig::kCurrentFormatVersion) + ",\n"
                      "  \"name\": \"environment_wind_validation\",\n"
                      "  \"environmentWind\": {\n"
                      "    \"direction\": [0.0, 0.0],\n"
                      "    \"speed\": -4.0,\n"
                      "    \"strength\": 99.0,\n"
                      "    \"gustStrength\": -1.0,\n"
                      "    \"gustFrequencyHz\": 99.0,\n"
                      "    \"turbulenceStrength\": 2.0,\n"
                      "    \"verticalLift\": -99.0\n"
                      "  }\n"
                      "}\n");

    SceneConfig loaded;
    require(loaded.loadFromFile(scenePath),
            "Scene with out-of-range environment wind should load after validation");
    require(nearlyEqual(loaded.environmentWind.direction.x, 0.8f) &&
                nearlyEqual(loaded.environmentWind.direction.y, 0.6f),
            "Zero-length environment wind direction should use the production default");
    require(nearlyEqual(loaded.environmentWind.speed, 0.0f),
            "Environment wind speed should clamp to its supported minimum");
    require(nearlyEqual(loaded.environmentWind.strength, 2.0f),
            "Environment wind strength should clamp to its supported maximum");
    require(nearlyEqual(loaded.environmentWind.gustStrength, 0.0f),
            "Environment wind gust strength should clamp to its supported minimum");
    require(nearlyEqual(loaded.environmentWind.gustFrequencyHz, 2.0f),
            "Environment wind gust frequency should clamp to its supported maximum");
    require(nearlyEqual(loaded.environmentWind.turbulenceStrength, 1.0f),
            "Environment wind turbulence should clamp to its supported maximum");
    require(nearlyEqual(loaded.environmentWind.verticalLift, -2.0f),
            "Environment wind vertical lift should clamp to its supported minimum");
}

void testWindborneParticleValidation(const fs::path& tempDir)
{
    const fs::path scenePath = tempDir / "windborne_particle_validation.json";
    writeTextFile(scenePath,
                  std::string("{\n") +
                      "  \"version\": " +
                      std::to_string(SceneConfig::kCurrentFormatVersion) + ",\n"
                      "  \"name\": \"windborne_particle_validation\",\n"
                      "  \"windborneParticles\": {\n"
                      "    \"enabled\": true,\n"
                      "    \"amount\": -2.0,\n"
                      "    \"leafFraction\": 3.0,\n"
                      "    \"scale\": 9.0,\n"
                      "    \"visibilityDistance\": 2.0\n"
                      "  }\n"
                      "}\n");

    SceneConfig loaded;
    require(loaded.loadFromFile(scenePath),
            "Scene with out-of-range windborne particles should load after validation");
    require(loaded.windborneParticles.enabled,
            "Windborne particle validation should preserve enablement");
    require(nearlyEqual(loaded.windborneParticles.amount, 0.0f),
            "Windborne particle amount should clamp to its supported minimum");
    require(nearlyEqual(loaded.windborneParticles.leafFraction, 1.0f),
            "Windborne leaf fraction should clamp to its supported maximum");
    require(nearlyEqual(loaded.windborneParticles.scale, 1.75f),
            "Windborne particle scale should clamp to its supported maximum");
    require(nearlyEqual(loaded.windborneParticles.visibilityDistance, 8.0f),
            "Windborne visibility should clamp to its supported minimum");
}

void testPlaceableTransformValidation(const fs::path& tempDir)
{
    const fs::path scenePath = tempDir / "placeable_transform_validation.json";
    writeTextFile(scenePath,
                  std::string("{\n") +
                      "  \"version\": " +
                      std::to_string(SceneConfig::kCurrentFormatVersion) + ",\n"
                      "  \"name\": \"placeable_transform_validation\",\n"
                      "  \"useDefaultPlaceables\": false,\n"
                      "  \"placeables\": [\n"
                      "    { \"uuid\": \"valid\", \"prototypeSlug\": \"eelgrass\", "
                      "\"position\": [1, 2, 3], \"rotation\": [-2, 0, 0, 0], "
                      "\"scale\": [1, 2, 0.5] },\n"
                      "    { \"uuid\": \"zero-scale\", \"prototypeSlug\": \"eelgrass\", "
                      "\"scale\": [1, 0, 1] },\n"
                      "    { \"uuid\": \"version-zero\", \"prototypeSlug\": \"eelgrass\", "
                      "\"prototypeVersion\": 0 },\n"
                      "    { \"uuid\": \"valid\", \"prototypeSlug\": \"eelgrass\" }\n"
                      "  ]\n"
                      "}\n");

    SceneConfig loaded;
    require(loaded.loadFromFile(scenePath),
            "Scene placeable transforms should load through validation");
    require(loaded.placeables.size() == 1 && loaded.placeables[0].uuid == "valid",
            "Invalid transforms, invalid identities, and duplicate placeables should be removed deterministically");
    const PlaceableInstance& placeable = loaded.placeables[0];
    require(nearlyEqual(placeable.rotation.w, 1.0f) &&
                nearlyEqual(placeable.rotation.x, 0.0f) &&
                nearlyEqual(placeable.rotation.y, 0.0f) &&
                nearlyEqual(placeable.rotation.z, 0.0f),
            "Placeable rotation should be normalized with a canonical sign");
    require(nearlyEqual(placeable.scale.x, 1.0f) &&
                nearlyEqual(placeable.scale.y, 2.0f) &&
                nearlyEqual(placeable.scale.z, 0.5f),
            "Validation should preserve positive nonuniform placeable scale");
}

void testExperimentalCloudsAreOptInForNewScenes()
{
    require(!SceneConfig{}.loadCloudScene,
            "New scene configs should not enable experimental voxel clouds by default");
    require(!SceneConfig::empty().loadCloudScene,
            "Empty preset should keep experimental voxel clouds disabled");
    require(!SceneConfig::obbTest().loadCloudScene,
            "OBB preset should keep experimental voxel clouds disabled");
    require(!SceneConfig::voxelWorld().loadCloudScene,
            "Voxel-world preset should keep experimental voxel clouds disabled");
    require(!SceneConfig::proceduralWorld().loadCloudScene,
            "Procedural-world preset should keep experimental voxel clouds disabled");
    require(!SceneConfig::voxelImport().loadCloudScene,
            "Voxel-import preset should keep experimental voxel clouds disabled");
    require(!SceneConfig::fullDemo().loadCloudScene,
            "Full demo should not silently opt into an experimental cloud renderer");
    require(!SceneConfig::aquariumTest().loadCloudScene,
            "Aquarium preset should keep experimental voxel clouds disabled");
    require(!SceneConfig::glassFocus().loadCloudScene,
            "Glass preset should keep experimental voxel clouds disabled");
}

engine::scene::ScenePresentationProfile makeFullyNonDefaultProfile()
{
    engine::scene::ScenePresentationProfile profile{};
    profile.name = "unit-test-full-delta";

    profile.lighting.skyPreset = 2;
    profile.lighting.skyColor = {0.31f, 0.07f, 0.55f};
    profile.lighting.pointLightsEnabled = false;
    profile.lighting.lightCount = 7;
    profile.lighting.sunElevation = 12.5f;
    profile.lighting.sunAzimuth = 301.0f;
    profile.lighting.sunColor = {0.9f, 0.4f, 0.2f};
    profile.lighting.sunIntensity = 1.35f;

    profile.shadows.csmEnabled = false;
    profile.shadows.useDdaShadows = true;
    profile.shadows.requestedSunAngularRadius = 0.055f;
    profile.shadows.maxShadowDistance = 64.0f;
    profile.shadows.normalBias = 0.09f;
    profile.shadows.maxSteps = 96;
    profile.shadows.ddaSunSampleCount = 6;
    profile.shadows.foliageOpacity = 0.8f;
    profile.shadows.temporalBlendAlpha = 0.2f;
    profile.shadows.temporalDepthReject = 0.005f;
    profile.shadows.temporalNormalRejectDot = 0.8f;
    profile.shadows.temporalClampSharpness = 0.5f;
    profile.shadows.spatialFilterRadius = 2;
    profile.shadows.spatialDepthSigma = 0.008f;
    profile.shadows.spatialValueSigma = 0.4f;
    profile.shadows.spatialNormalPower = 24.0f;
    profile.shadows.postDenoiseRadius = 2;
    profile.shadows.postDenoiseDepthSigma = 0.009f;
    profile.shadows.postDenoiseValueSigma = 0.3f;
    profile.shadows.postDenoiseNormalPower = 16.0f;
    profile.shadows.localLightShadowsEnabled = false;
    profile.shadows.localShadowCastingLightCount = 2;
    profile.shadows.localTemporalBlendAlpha = 0.11f;
    profile.shadows.localTemporalDepthReject = 0.03f;
    profile.shadows.localBlurEnabled = false;
    profile.shadows.localBlurSigma = 2.25f;
    profile.shadows.terminatorSoftness = 0.4f;
    profile.shadows.terminatorMode = 0;
    profile.shadows.csmDitherEnabled = false;

    profile.ambientOcclusion.enabled = false;
    profile.ambientOcclusion.distanceMode =
        engine::render::AmbientOcclusionDistanceMode::ProjectedScreenRadius;
    profile.ambientOcclusion.projectedRadiusPixels = 448.0f;
    profile.ambientOcclusion.projectedMinimumWorldDistance = 1.75f;
    profile.ambientOcclusion.maxDistance = 14.0f;
    profile.ambientOcclusion.stepSize = 0.22f;
    profile.ambientOcclusion.intensity = 0.9f;
    profile.ambientOcclusion.contribution = 0.55f;
    profile.ambientOcclusion.bias = 0.02f;
    profile.ambientOcclusion.rayCount = 5;
    profile.ambientOcclusion.temporalBlendAlpha = 0.17f;
    profile.ambientOcclusion.temporalDepthReject = 0.06f;
    profile.ambientOcclusion.temporalNormalRejectDot = 0.7f;

    profile.postFx.tonemapEnabled = false;
    profile.postFx.exposure = 1.4f;
    profile.postFx.highlightRecovery = 0.31f;
    profile.postFx.bloomEnabled = false;
    profile.postFx.bloomThreshold = 0.85f;
    profile.postFx.bloomKnee = 0.65f;
    profile.postFx.bloomIntensity = 0.21f;
    profile.postFx.bloomSigma = 3.5f;
    profile.postFx.vignetteStrength = 0.33f;
    profile.postFx.grainStrength = 0.08f;
    profile.postFx.colorGradeEnabled = true;
    profile.postFx.colorGradeStrength = 0.45f;
    profile.postFx.colorGradeSaturation = 0.82f;
    profile.postFx.colorGradeContrast = 1.12f;
    profile.postFx.colorGradeTemperature = 0.19f;
    profile.postFx.pixelizationEnabled = true;
    profile.postFx.pixelizationBlockSize = 3.0f;
    profile.postFx.pixelizationStrength = 0.52f;
    profile.postFx.pixelizationEdgeFocus = 0.61f;
    profile.postFx.depthOfFieldEnabled = true;
    profile.postFx.depthOfFieldFocusDistance = 22.0f;
    profile.postFx.depthOfFieldFocusRange = 9.5f;
    profile.postFx.depthOfFieldBlurStrength = 0.42f;
    profile.postFx.taaEnabled = true;
    profile.postFx.jitterEnabled = true;
    profile.postFx.taaSimilarityThreshold = 0.24f;
    profile.postFx.taaVelocityScale = 18.0f;
    profile.postFx.taaBlendMin = 0.02f;
    profile.postFx.taaBlendMax = 0.35f;
    profile.postFx.taaSharpen = 0.1f;
    profile.postFx.taaDepthEdgeThreshold = 0.17f;
    profile.postFx.taaCrossFrameDepthThreshold = 0.05f;
    profile.postFx.taaColorVarianceThreshold = 0.28f;
    profile.postFx.taaSoftEdgeStrength = 0.37f;
    profile.postFx.fxaaEnabled = false;

    profile.voxelSurface.materialDetailStrength = 0.66f;
    profile.voxelSurface.normalEdgeSmoothing = 0.2f;
    profile.voxelSurface.pixelEdgeShadowStrength = 0.15f;

    profile.voxelCellVariation.masterStrength = 0.91f;
    profile.voxelCellVariation.genericAmplitude = 0.11f;
    profile.voxelCellVariation.gravelAmplitude = 0.22f;
    profile.voxelCellVariation.plantAmplitude = 0.33f;
    profile.voxelCellVariation.stoneAmplitude = 0.44f;
    profile.voxelCellVariation.woodAmplitude = 0.55f;

    profile.hemisphereAmbient.strength = 0.73f;
    profile.hemisphereAmbient.skyTint = {0.62f, 0.83f, 1.17f};
    profile.hemisphereAmbient.groundTint = {1.21f, 0.79f, 0.51f};

    profile.atmosphere.density = 0.013f;
    profile.atmosphere.heightFalloff = 0.071f;
    profile.atmosphere.baseHeight = 1.75f;
    profile.atmosphere.sunPhaseStrength = 0.42f;
    profile.atmosphere.sunPhaseExponent = 5.5f;

    profile.paintedSky.strength = 0.83f;
    profile.paintedSky.horizonTint = {1.21f, 0.91f, 0.71f};
    profile.paintedSky.zenithTint = {0.61f, 0.81f, 1.31f};
    profile.paintedSky.lowerHemisphereTint = {0.72f, 0.66f, 0.94f};
    profile.paintedSky.gradientExponent = 0.57f;
    profile.paintedSky.horizonBandStrength = 0.38f;
    profile.paintedSky.horizonBandExponent = 6.0f;
    profile.paintedSky.sunDiscAngularRadius = 0.016f;
    profile.paintedSky.sunDiscSoftness = 0.003f;
    profile.paintedSky.sunDiscIntensity = 1.7f;
    profile.paintedSky.sunHaloIntensity = 0.48f;
    profile.paintedSky.sunHaloExponent = 34.0f;

    profile.paintedClouds.strength = 0.91f;
    profile.paintedClouds.coverage = 0.57f;
    profile.paintedClouds.opacity = 0.81f;
    profile.paintedClouds.softness = 0.12f;
    profile.paintedClouds.altitude = 123.0f;
    profile.paintedClouds.worldScale = 144.0f;
    profile.paintedClouds.detailStrength = 0.52f;
    profile.paintedClouds.lightTint = {1.14f, 1.07f, 0.93f};
    profile.paintedClouds.shadowTint = {0.51f, 0.62f, 0.79f};
    profile.paintedClouds.silverLiningStrength = 0.27f;
    profile.paintedClouds.horizonFadeStart = 0.06f;
    profile.paintedClouds.horizonFadeEnd = 0.24f;

    profile.water.stylizedMode = false;
    profile.water.absorption = 0.09f;
    profile.water.refract = 0.041f;
    profile.water.depthScale = 6.5f;
    profile.water.shallowBias = 0.35f;
    profile.water.depthToMeters = 11.0f;
    profile.water.specIntensity = 2.1f;
    profile.water.specPower = 96.0f;
    profile.water.waveScale = 0.13f;
    profile.water.waveAmp = 0.44f;
    profile.water.crestHighlightsEnabled = false;
    profile.water.crestThreshold = 0.52f;
    profile.water.crestSoftness = 0.11f;
    profile.water.crestIntensity = 0.47f;
    profile.water.foamDepthThreshold = 0.66f;
    profile.water.foamOpacity = 0.51f;
    profile.water.foamScale = 0.27f;
    profile.water.distortionDepthScale = 0.21f;
    profile.water.edgeFadeDepth = 0.61f;
    profile.water.bandHardness = 0.71f;
    profile.water.reflectionStrength = 0.19f;
    profile.water.fresnelBias = 0.021f;
    profile.water.causticsEnabled = true;
    profile.water.causticsIntensity = 0.81f;
    profile.water.causticsScale = 0.21f;
    profile.water.causticsSpeed = 0.41f;
    profile.water.causticsBanding = 0.51f;
    profile.water.causticsDepthFade = 26.0f;
    profile.water.particlesPlanned = false;
    profile.water.particlesPlannedDensity = 0.22f;
    profile.water.particlesPlannedDrift = 0.31f;
    profile.water.particlesPlannedScale = 0.11f;
    profile.water.foamEmitterEnabled = true;
    profile.water.foamEmitterIntensity = 0.52f;
    profile.water.foamEmitterRadius = 4.4f;
    profile.water.foamEmitterOffsetX = 1.2f;
    profile.water.foamEmitterOffsetZ = -1.7f;
    profile.water.foamEmitterScale = 0.31f;
    profile.water.foamEmitterSpread = 0.41f;
    profile.water.gradientStrength = 0.12f;
    profile.water.planarReflectionEnabled = true;
    profile.water.planarObliqueClipEnabled = true;
    profile.water.planarStrength = 0.52f;

    profile.glass.tint = {0.51f, 0.61f, 0.71f};
    profile.glass.reflection = {0.11f, 0.21f, 0.31f};
    profile.glass.absorption = 0.31f;
    profile.glass.thicknessScale = 0.81f;
    profile.glass.refract = 0.021f;
    profile.glass.ior = 1.31f;
    profile.glass.bubbleScale = 15.5f;
    profile.glass.bubbleIntensity = 0.21f;
    profile.glass.bubbleThicknessGate = 0.26f;
    profile.glass.bubbleChromaticSplit = 0.051f;
    profile.glass.iridescentStrength = 0.41f;
    profile.glass.iridescentFilmThickness = 2.6f;
    profile.glass.iridescentFrequency = 3.1f;
    profile.glass.voxelGlassRefractEnabled = false;
    profile.glass.voxelGlassAbsorption = 0.21f;
    profile.glass.voxelGlassRefractStrength = 0.031f;
    profile.glass.voxelGlassIor = 1.41f;
    profile.glass.voxelGlassReflectStrength = 0.31f;
    profile.glass.voxelGlassTint = {0.91f, 0.81f, 0.71f};
    profile.glass.voxelGlassReflectionColor = {0.41f, 0.51f, 0.61f};

    return profile;
}

void testPresentationProfileRoundTrip(const fs::path& tempDir)
{
    const engine::scene::ScenePresentationProfile profile = makeFullyNonDefaultProfile();
    const engine::scene::ScenePresentationProfile defaults{};
    require(profile.lighting != defaults.lighting && profile.shadows != defaults.shadows &&
                profile.ambientOcclusion != defaults.ambientOcclusion &&
                profile.postFx != defaults.postFx &&
                profile.voxelSurface != defaults.voxelSurface &&
                profile.voxelCellVariation != defaults.voxelCellVariation &&
                profile.hemisphereAmbient != defaults.hemisphereAmbient &&
                profile.atmosphere != defaults.atmosphere &&
                profile.paintedSky != defaults.paintedSky &&
                profile.paintedClouds != defaults.paintedClouds &&
                profile.water != defaults.water && profile.glass != defaults.glass,
            "Round-trip fixture must differ from schema defaults in every bucket");

    SceneConfig saved = SceneConfig::aquariumTest();
    saved.name = "presentation_profile_round_trip";
    saved.presentationProfile = profile;

    const fs::path scenePath = tempDir / "presentation_profile_round_trip.json";
    require(saved.saveToFile(scenePath), "Scene with presentation profile should save");

    SceneConfig loaded;
    require(loaded.loadFromFile(scenePath), "Scene with presentation profile should load");
    require(loaded.version == SceneConfig::kCurrentFormatVersion,
            "Profile round trip should keep the current scene version");
    require(loaded.presentationProfile.has_value(),
            "Serialized presentation profile should survive load");
    require(*loaded.presentationProfile == profile,
            "Every presentation profile field should round-trip exactly");
}

void testPresentationProfileLegacyAbsentAndUnsupported(const fs::path& tempDir)
{
    // a v4-era scene has no profile key and must load with an absent profile.
    const fs::path legacyPath = tempDir / "presentation_profile_legacy.json";
    writeTextFile(legacyPath,
                  "{\n"
                  "  \"version\": 4,\n"
                  "  \"name\": \"legacy_no_profile\"\n"
                  "}\n");
    SceneConfig legacy;
    require(legacy.loadFromFile(legacyPath), "v4 scene without profile should load");
    require(!legacy.presentationProfile.has_value(),
            "v4 scene should keep an absent presentation profile");

    // absent profiles stay absent through save: the key is omitted entirely.
    const fs::path resavePath = tempDir / "presentation_profile_resave.json";
    require(legacy.saveToFile(resavePath), "Legacy resave should succeed");
    std::ifstream resaved(resavePath);
    require(resaved.is_open(), "Resaved scene should be readable");
    std::stringstream resavedContents;
    resavedContents << resaved.rdbuf();
    require(resavedContents.str().find("presentationProfile") == std::string::npos,
            "Absent profile should not write a presentationProfile key");

    // a future profile version is dropped with a warning; the scene still loads.
    const fs::path futurePath = tempDir / "presentation_profile_future.json";
    writeTextFile(futurePath,
                  "{\n"
                  "  \"version\": 6,\n"
                  "  \"name\": \"future_profile\",\n"
                  "  \"presentationProfile\": { \"version\": 99 }\n"
                  "}\n");
    SceneConfig future;
    require(future.loadFromFile(futurePath),
            "Scene with future profile version should still load");
    require(!future.presentationProfile.has_value(),
            "Future profile version should be dropped, not partially applied");

    // a malformed non-object profile is dropped; the scene still loads.
    const fs::path malformedPath = tempDir / "presentation_profile_malformed.json";
    writeTextFile(malformedPath,
                  "{\n"
                  "  \"version\": 6,\n"
                  "  \"name\": \"malformed_profile\",\n"
                  "  \"presentationProfile\": 12\n"
                  "}\n");
    SceneConfig malformed;
    require(malformed.loadFromFile(malformedPath),
            "Scene with malformed profile should still load");
    require(!malformed.presentationProfile.has_value(),
            "Malformed profile should be dropped");

    // a partial profile applies present fields over the scene's named base.
    const fs::path partialPath = tempDir / "presentation_profile_partial.json";
    writeTextFile(partialPath,
                  "{\n"
                  "  \"version\": 6,\n"
                  "  \"name\": \"aquarium_sunroof\",\n"
                  "  \"loadAquariumTest\": true,\n"
                  "  \"presentationProfile\": {\n"
                  "    \"version\": 0,\n"
                  "    \"postFx\": { \"taaEnabled\": true, \"exposure\": 1.65 },\n"
                  "    \"ambientOcclusion\": {\n"
                  "      \"distanceMode\": \"projected-screen-radius\",\n"
                  "      \"projectedRadiusPixels\": 128.0,\n"
                  "      \"projectedMinimumWorldDistance\": 0.25\n"
                  "    },\n"
                  "    \"voxelCellVariation\": { \"masterStrength\": 1.0 },\n"
                  "    \"hemisphereAmbient\": { \"strength\": 1.0 },\n"
                  "    \"atmosphere\": { \"density\": 1.0 }\n"
                  "  }\n"
                  "}\n");
    SceneConfig partial;
    require(partial.loadFromFile(partialPath), "Scene with partial profile should load");
    require(partial.presentationProfile.has_value(), "Partial profile should be present");
    require(partial.presentationProfile->postFx.taaEnabled,
            "Partial profile should apply present fields");
    require(nearlyEqual(partial.presentationProfile->postFx.exposure, 1.65f),
            "Partial profile should apply each present override");
    const engine::scene::ScenePresentationProfile sunroofBase =
        engine::scene::namedBaseScenePresentationProfile(partial);
    require(partial.presentationProfile->name == sunroofBase.name &&
                nearlyEqual(partial.presentationProfile->lighting.sunElevation,
                            sunroofBase.lighting.sunElevation) &&
                nearlyEqual(partial.presentationProfile->postFx.highlightRecovery,
                            sunroofBase.postFx.highlightRecovery) &&
                partial.presentationProfile->water == sunroofBase.water &&
                partial.presentationProfile->glass == sunroofBase.glass,
            "Absent partial-profile fields must retain the named scene base");
    require(partial.presentationProfile->version ==
                engine::scene::ScenePresentationProfile::kCurrentVersion &&
                partial.presentationProfile->voxelCellVariation ==
                    engine::scene::SceneVoxelCellVariationProfile{} &&
                partial.presentationProfile->hemisphereAmbient ==
                    engine::scene::SceneHemisphereAmbientProfile{} &&
                partial.presentationProfile->atmosphere ==
                    engine::scene::SceneAtmosphereProfile{} &&
                partial.presentationProfile->paintedSky ==
                    engine::scene::ScenePaintedSkyProfile{} &&
                partial.presentationProfile->paintedClouds ==
                    engine::scene::ScenePaintedCloudProfile{} &&
                partial.presentationProfile->ambientOcclusion.distanceMode ==
                    engine::render::AmbientOcclusionDistanceMode::
                        AuthoredWorldDistance &&
                nearlyEqual(partial.presentationProfile->ambientOcclusion
                                .projectedRadiusPixels,
                            512.0f) &&
                nearlyEqual(partial.presentationProfile->ambientOcclusion
                                .projectedMinimumWorldDistance,
                            2.0f),
            "Profile v0 must upgrade through v6 with new renderer fields compatibility-off");

    // profile v1 also predates selectable ao distance modes. even if it contains
    // lookalike future fields, it must preserve authored world distance.
    const fs::path v1Path = tempDir / "presentation_profile_v1_ao_upgrade.json";
    writeTextFile(v1Path,
                  "{\n"
                  "  \"version\": 6,\n"
                  "  \"name\": \"v1_ao_upgrade\",\n"
                  "  \"presentationProfile\": {\n"
                  "    \"version\": 1,\n"
                  "    \"ambientOcclusion\": {\n"
                  "      \"distanceMode\": \"projected-screen-radius\",\n"
                  "      \"projectedRadiusPixels\": 96.0,\n"
                  "      \"projectedMinimumWorldDistance\": 0.1,\n"
                  "      \"maxDistance\": 4.5\n"
                  "    }\n"
                  "  }\n"
                  "}\n");
    SceneConfig v1;
    require(v1.loadFromFile(v1Path), "Profile v1 scene should load");
    require(v1.presentationProfile.has_value() &&
                v1.presentationProfile->ambientOcclusion.distanceMode ==
                    engine::render::AmbientOcclusionDistanceMode::
                        AuthoredWorldDistance &&
                nearlyEqual(v1.presentationProfile->ambientOcclusion.maxDistance,
                            4.5f) &&
                nearlyEqual(v1.presentationProfile->ambientOcclusion
                                .projectedRadiusPixels,
                            512.0f) &&
                nearlyEqual(v1.presentationProfile->ambientOcclusion
                                .projectedMinimumWorldDistance,
                            2.0f),
            "Profile v1 must load authored AO values while upgrading the v2 mode compatibility-off");

    // profile v4 predates the painted sky. it must not inherit the new named
    // landscape base or honor lookalike future fields.
    const fs::path v4Path = tempDir / "presentation_profile_v4_sky_upgrade.json";
    writeTextFile(v4Path,
                  "{\n"
                  "  \"version\": 9,\n"
                  "  \"name\": \"nature_pond_probe\",\n"
                  "  \"loadProceduralWorld\": true,\n"
                  "  \"presentationProfile\": {\n"
                  "    \"version\": 4,\n"
                  "    \"paintedSky\": { \"strength\": 1.0 }\n"
                  "  }\n"
                  "}\n");
    SceneConfig v4;
    require(v4.loadFromFile(v4Path), "Profile v4 sky-upgrade scene should load");
    require(v4.presentationProfile.has_value() &&
                v4.presentationProfile->paintedSky ==
                    engine::scene::ScenePaintedSkyProfile{},
            "Profile v4 must preserve the exact flat-sky compatibility path");

    // profile v5 predates cloud-001 and must ignore lookalike cloud data.
    const fs::path v5Path = tempDir / "presentation_profile_v5_cloud_upgrade.json";
    writeTextFile(v5Path,
                  "{\n"
                  "  \"version\": 9,\n"
                  "  \"name\": \"nature_pond_probe\",\n"
                  "  \"loadProceduralWorld\": true,\n"
                  "  \"presentationProfile\": {\n"
                  "    \"version\": 5,\n"
                  "    \"paintedClouds\": { \"strength\": 1.0 }\n"
                  "  }\n"
                  "}\n");
    SceneConfig v5;
    require(v5.loadFromFile(v5Path), "Profile v5 cloud-upgrade scene should load");
    require(v5.presentationProfile.has_value() &&
                v5.presentationProfile->paintedClouds ==
                    engine::scene::ScenePaintedCloudProfile{},
            "Profile v5 must preserve the exact cloud-free compatibility path");
}

void testGameStateValidation(const fs::path& tempDir)
{
    const fs::path scenePath = tempDir / "game_state_validation.json";
    writeTextFile(scenePath,
                  std::string("{\n") +
                      "  \"version\": " +
                      std::to_string(SceneConfig::kCurrentFormatVersion) + ",\n"
                      "  \"name\": \"game_state_validation\",\n"
                      "  \"gameState\": {\n"
                      "    \"lastPlayedUtc\": \"2026-06-19T05:00:00Z\",\n"
                      "    \"water\": {\n"
                      "      \"temperatureC\": 72.0,\n"
                      "      \"oxygen\": -0.5,\n"
                      "      \"flow\": 1.5,\n"
                      "      \"cleanliness\": 0.4\n"
                      "    },\n"
                      "    \"progression\": {\n"
                      "      \"coins\": 25,\n"
                      "      \"discovery\": 4,\n"
                      "      \"careMilestone\": 2,\n"
                      "      \"tankTier\": 0\n"
                      "    },\n"
                      "    \"creatures\": [\n"
                      "      {\n"
                      "        \"uuid\": \"creature-validated-0001\",\n"
                      "        \"speciesId\": \"starter_fish\",\n"
                      "        \"displayName\": \"Clamped\",\n"
                      "        \"primary\": true,\n"
                      "        \"bond\": 2.0,\n"
                      "        \"vitality\": -1.0,\n"
                      "        \"needs\": {\n"
                      "          \"hunger\": 1.5,\n"
                      "          \"cleanliness\": -0.2,\n"
                      "          \"happiness\": 0.7,\n"
                      "          \"health\": 2.0\n"
                      "        }\n"
                      "      },\n"
                      "      {\n"
                      "        \"uuid\": \"\",\n"
                      "        \"speciesId\": \"starter_fish\"\n"
                      "      }\n"
                      "    ]\n"
                      "  }\n"
                      "}\n");

    SceneConfig loaded;
    require(loaded.loadFromFile(scenePath), "Scene with game state should load");
    require(nearlyEqual(loaded.gameState.water.temperatureC, 40.0f),
            "Water temperature should clamp to supported range");
    require(nearlyEqual(loaded.gameState.water.oxygen, 0.0f),
            "Water oxygen should clamp to supported range");
    require(nearlyEqual(loaded.gameState.water.flow, 1.0f),
            "Water flow should clamp to supported range");
    require(nearlyEqual(loaded.gameState.water.cleanliness, 0.4f),
            "Water cleanliness should preserve in-range values");
    require(loaded.gameState.progression.coins == 25u,
            "Progression coins should load");
    require(loaded.gameState.progression.discovery == 4u,
            "Progression discovery should load");
    require(loaded.gameState.progression.careMilestone == 2u,
            "Progression care milestone should load");
    require(loaded.gameState.progression.tankTier == 1u,
            "Progression tank tier should clamp to one or higher");
    require(loaded.gameState.creatures.size() == 1,
            "Invalid creature entries should be filtered");
    const CreatureInstance& creature = loaded.gameState.creatures[0];
    require(creature.uuid == "creature-validated-0001", "Creature uuid should load");
    require(creature.primary, "Creature primary flag should load");
    require(nearlyEqual(creature.bond, 1.0f), "Creature bond should clamp");
    require(nearlyEqual(creature.vitality, 0.0f), "Creature vitality should clamp");
    require(nearlyEqual(creature.needs.hunger, 1.0f),
            "Creature hunger should clamp");
    require(nearlyEqual(creature.needs.cleanliness, 0.0f),
            "Creature cleanliness should clamp");
    require(nearlyEqual(creature.needs.happiness, 0.7f),
            "Creature happiness should preserve in-range values");
    require(nearlyEqual(creature.needs.health, 1.0f), "Creature health should clamp");
}

void testCreatureRosterValidation(const fs::path& tempDir)
{
    const fs::path scenePath = tempDir / "creature_roster_validation.json";
    writeTextFile(scenePath,
                  std::string("{\n") +
                      "  \"version\": " +
                      std::to_string(SceneConfig::kCurrentFormatVersion) + ",\n"
                      "  \"name\": \"creature_roster_validation\",\n"
                      "  \"gameState\": {\n"
                      "    \"creatures\": [\n"
                      "      { \"uuid\": \"primary-a\", \"speciesId\": \"starter_fish\", \"primary\": true },\n"
                      "      { \"uuid\": \"primary-a\", \"speciesId\": \"starter_fish\", \"primary\": true },\n"
                      "      { \"uuid\": \"secondary-c\", \"speciesId\": \"axolotl\", \"primary\": true }\n"
                      "    ]\n"
                      "  }\n"
                      "}\n");

    SceneConfig loaded;
    require(loaded.loadFromFile(scenePath),
            "Scene with an ambiguous creature roster should load after repair");
    require(loaded.gameState.creatures.size() == 3,
            "Creature roster repair should preserve valid saved creatures");
    require(loaded.gameState.creatures[0].primary &&
                !loaded.gameState.creatures[1].primary &&
                !loaded.gameState.creatures[2].primary,
            "The first explicit primary should remain the only primary");
    require(loaded.gameState.creatures[1].uuid == "primary-a-duplicate-2",
            "Duplicate saved UUIDs should receive a deterministic repair");
}

void testSceneCatalogTracksAuthoredScenes(const fs::path& tempDir)
{
    const fs::path catalogDir = tempDir / "catalog";
    fs::create_directories(catalogDir);

    writeTextFile(catalogDir / "valid_scene.json",
                  "{\n"
                  "  \"version\": 1,\n"
                  "  \"name\": \"catalog_valid\",\n"
                  "  \"description\": \"Valid catalog scene\"\n"
                  "}\n");
    writeTextFile(catalogDir / "broken_scene.json",
                  "{\n"
                  "  \"version\": 999,\n"
                  "  \"name\": \"broken_scene\"\n"
                  "}\n");
    writeTextFile(catalogDir / "last_used.json",
                  "{\n"
                  "  \"version\": 1,\n"
                  "  \"name\": \"runtime_scratch\"\n"
                  "}\n");
    writeTextFile(catalogDir / "notes.txt", "not a scene\n");

    const std::vector<SceneCatalogEntry> scenes = scanSceneCatalog(catalogDir);
    require(scenes.size() == 2, "Catalog should include authored JSON scenes except last_used.json");
    require(scenes[0].fileName == "broken_scene.json", "Catalog should sort authored scenes by filename");
    require(scenes[1].fileName == "valid_scene.json", "Catalog sort order mismatch");

    const SceneCatalogEntry* validScene = nullptr;
    const SceneCatalogEntry* brokenScene = nullptr;
    for (const SceneCatalogEntry& scene : scenes)
    {
        if (scene.fileName == "valid_scene.json")
        {
            validScene = &scene;
        }
        else if (scene.fileName == "broken_scene.json")
        {
            brokenScene = &scene;
        }
    }

    require(validScene != nullptr, "Valid authored scene should be present in catalog");
    require(validScene->valid, "Valid authored scene should be marked loadable");
    require(validScene->displayName == "catalog_valid", "Catalog should use scene metadata name");
    require(validScene->description == "Valid catalog scene",
            "Catalog should preserve scene description metadata");

    require(brokenScene != nullptr, "Broken authored scene should still appear in catalog");
    require(!brokenScene->valid, "Broken authored scene should be marked invalid");
    require(!brokenScene->errorMessage.empty(), "Broken authored scene should report an error");
}

void testRuntimeContentManifestValidation(const fs::path& tempDir)
{
    const fs::path runtimeRoot = tempDir / "runtime_content_ok";
    const fs::path shaderDir = runtimeRoot / "shaders";
    const fs::path assetDir = runtimeRoot / "assets";
    fs::create_directories(shaderDir);
    fs::create_directories(assetDir / "textures");
    fs::create_directories(assetDir / "meshes");

    writeTextFile(shaderDir / "runtime_content_manifest.json",
                  "{\n"
                  "  \"shader_includes\": [],\n"
                  "  \"shaders\": [\n"
                  "    { \"source\": \"fullscreen.vert\", \"output\": \"fullscreen.vert.spv\" },\n"
                  "    { \"source\": \"ao/ao_ray.comp\", \"output\": \"ao_ray.comp.spv\", \"required\": false }\n"
                  "  ],\n"
                  "  \"asset_checks\": [\n"
                  "    {\n"
                  "      \"type\": \"file\",\n"
                  "      \"path\": \"textures/blue_noise_128_rg.png\",\n"
                  "      \"required\": true,\n"
                  "      \"description\": \"Blue noise\"\n"
                  "    },\n"
                  "    {\n"
                  "      \"type\": \"extension_search\",\n"
                  "      \"directory\": \"meshes\",\n"
                  "      \"extension\": \".obj\",\n"
                  "      \"required\": false,\n"
                  "      \"description\": \"Optional mesh\"\n"
                  "    }\n"
                  "  ]\n"
                  "}\n");
    writeTextFile(shaderDir / "fullscreen.vert.spv", "spv");
    writeTextFile(shaderDir / "ao_ray.comp.spv", "spv");
    writeTextFile(assetDir / "textures" / "blue_noise_128_rg.png", "png");
    writeTextFile(assetDir / "meshes" / "sample.obj", "obj");

    CurrentPathGuard pathGuard(runtimeRoot);
    RuntimeContentManifest manifest;
    std::string error;
    require(loadRuntimeContentManifest(shaderDir / "runtime_content_manifest.json", manifest, &error),
            "Runtime content manifest should parse");
    require(manifest.shaders.size() == 2, "Runtime manifest should expose shader outputs");
    require(manifest.assetChecks.size() == 2, "Runtime manifest should expose asset checks");

    const std::string argv0 = (runtimeRoot / "voxel_aquarium.exe").string();
    const RuntimeContentValidationReport report =
        validateRuntimeContentManifest(manifest, assetDir, argv0.c_str());
    require(report.errors.empty(), "Runtime content validation should pass for complete content");
    require(report.warnings.empty(), "Runtime content validation should not warn when optional assets exist");
}

void testRuntimeContentManifestReportsMissingRequiredContent(const fs::path& tempDir)
{
    const fs::path runtimeRoot = tempDir / "runtime_content_missing";
    const fs::path shaderDir = runtimeRoot / "shaders";
    const fs::path assetDir = runtimeRoot / "assets";
    fs::create_directories(shaderDir);
    fs::create_directories(assetDir / "textures");

    writeTextFile(shaderDir / "runtime_content_manifest.json",
                  "{\n"
                  "  \"shader_includes\": [],\n"
                  "  \"shaders\": [\n"
                  "    { \"source\": \"fullscreen.vert\", \"output\": \"fullscreen.vert.spv\" },\n"
                  "    { \"source\": \"ao/ao_ray.comp\", \"output\": \"ao_ray.comp.spv\", \"required\": false }\n"
                  "  ],\n"
                  "  \"asset_checks\": [\n"
                  "    {\n"
                  "      \"type\": \"file\",\n"
                  "      \"path\": \"textures/blue_noise_128_rg.png\",\n"
                  "      \"required\": true,\n"
                  "      \"description\": \"Blue noise\"\n"
                  "    },\n"
                  "    {\n"
                  "      \"type\": \"extension_search\",\n"
                  "      \"directory\": \"meshes\",\n"
                  "      \"extension\": \".obj\",\n"
                  "      \"required\": false,\n"
                  "      \"description\": \"Optional mesh\"\n"
                  "    }\n"
                  "  ]\n"
                  "}\n");

    CurrentPathGuard pathGuard(runtimeRoot);
    RuntimeContentManifest manifest;
    std::string error;
    require(loadRuntimeContentManifest(shaderDir / "runtime_content_manifest.json", manifest, &error),
            "Runtime content manifest should parse for missing-content test");

    const std::string argv0 = (runtimeRoot / "voxel_aquarium.exe").string();
    const RuntimeContentValidationReport report =
        validateRuntimeContentManifest(manifest, assetDir, argv0.c_str());
    require(report.errors.size() == 2,
            "Missing required shader and asset should both be reported as errors; got " +
                std::to_string(report.errors.size()));
    require(report.warnings.size() == 2,
            "Missing optional shader and asset should both be reported as warnings; got " +
                std::to_string(report.warnings.size()));
    bool optionalShaderWarningFound = false;
    for (const std::string& warning : report.warnings)
    {
        if (warning.find("ao_ray.comp.spv") != std::string::npos)
        {
            optionalShaderWarningFound = true;
            break;
        }
    }
    require(optionalShaderWarningFound, "Missing optional shader should be reported as a warning");
}

} // namespace

int main()
{
    try
    {
        TemporaryDirectory temporaryDirectory;
        const fs::path& tempDir = temporaryDirectory.path();
        testRoundTripSaveLoad(tempDir);
        testOfflineCareSaveReloadIsIdempotent(tempDir);
        testLegacyVersionlessLoad(tempDir);
        testLoadFailureDoesNotMutateConfig(tempDir);
        testFutureVersionRejected(tempDir);
        testVersionedScenesUseFrozenDefaults(tempDir);
        testEnvironmentWindValidation(tempDir);
        testEnvironmentTimeValidation(tempDir);
        testWindborneParticleValidation(tempDir);
        testPlaceableTransformValidation(tempDir);
        testExperimentalCloudsAreOptInForNewScenes();
        testPresentationProfileRoundTrip(tempDir);
        testPresentationProfileLegacyAbsentAndUnsupported(tempDir);
        testGameStateValidation(tempDir);
        testCreatureRosterValidation(tempDir);
        testSceneCatalogTracksAuthoredScenes(tempDir);
        testRuntimeContentManifestValidation(tempDir);
        testRuntimeContentManifestReportsMissingRequiredContent(tempDir);
        std::cout << "SceneConfig tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "SceneConfig test failure: " << e.what() << "\n";
        return 1;
    }
}
