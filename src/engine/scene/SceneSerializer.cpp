#include "engine/scene/SceneSerializer.h"

#include "Core/Logger.h"
#include "engine/scene/SceneConfig.h"
#include "engine/scene/ScenePresentationProfile.h"
#include "engine/scene/ScenePresentationProfileVariants.h"
#include "engine/game/CreatureCareState.h"
#include "engine/game/PlaceableTransform.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

using json = nlohmann::json;

namespace
{

constexpr uint32_t kLegacySceneFormatVersion = 0;

bool shouldLog(SceneSerializerLogMode logMode)
{
    return logMode == SceneSerializerLogMode::Default;
}

void logSceneSerializerMessage(LogSeverity severity, std::string_view message,
                               SceneSerializerLogMode logMode)
{
    if (!shouldLog(logMode))
    {
        return;
    }

    logMessage(severity, "Scene", message);
}

void assignError(SceneLoadDiagnostics* diagnostics, const std::string& error)
{
    if (diagnostics != nullptr)
    {
        diagnostics->errorMessage = error;
    }
}

SceneConfig makeSceneDefaultsForVersion(uint32_t version)
{
    SceneConfig cfg;

    // freeze schema defaults per version so omitted fields in older scene files
    // do not drift when the in-code defaults evolve later.
    switch (version)
    {
        case kLegacySceneFormatVersion:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
            cfg.version = SceneConfig::kCurrentFormatVersion;
            cfg.loadTestFloor = true;
            cfg.loadVoxelWorld = true;
            cfg.loadOBBVolumes = true;
            cfg.loadGlassPanel = true;
            cfg.loadAquariumTest = false;
            cfg.loadGlassTestScene = false;
            cfg.loadProceduralWorld = false;
            cfg.loadVoxelImport = false;
            cfg.useMeshTankGlass = false;

            cfg.enableWater = true;
            cfg.useWaterV2 = false;
            cfg.enableGlass = true;
            cfg.enablePointLights = true;

            cfg.loadCloudScene = true;
            cfg.cloudSeed = 42;
            cfg.cloudAltitude = 100.0f;
            cfg.cloudWindSpeed = 0.5f;
            cfg.cloudWindDirection = glm::vec2(0.0f, 1.0f);
            cfg.cloudCoverage = 0.4f;
            cfg.cloudScale = 48.0f;
            cfg.cloudShadowStrength = 0.35f;

            cfg.skyPreset = 0;
            cfg.skyColor = glm::vec3(0.09f, 0.29f, 0.88f);

            cfg.loadStarScene = true;
            cfg.starSeed = 123;
            cfg.starAltitude = 150.0f;
            cfg.starDensity = 0.02f;

            cfg.cameraPosition = glm::vec3(0.0f, 5.0f, 10.0f);
            cfg.cameraYaw = 0.0f;
            cfg.cameraPitch = -0.2f;

            cfg.worldSeed = 1337;
            cfg.worldDimsX = 8;
            cfg.worldDimsY = 2;
            cfg.worldDimsZ = 8;
            cfg.useDefaultPlaceables = true;
            cfg.placeables.clear();
            cfg.gameState.lastPlayedUtc.clear();
            cfg.gameState.water.temperatureC = 24.0f;
            cfg.gameState.water.oxygen = 0.82f;
            cfg.gameState.water.flow = 0.45f;
            cfg.gameState.water.cleanliness = 1.0f;
            cfg.gameState.progression.coins = 0;
            cfg.gameState.progression.discovery = 0;
            cfg.gameState.progression.careMilestone = 0;
            cfg.gameState.progression.tankTier = 1;
            cfg.gameState.creatures.clear();
            cfg.presentationProfile.reset();

            cfg.name = "default";
            cfg.description.clear();
            return cfg;
        case 7:
        case 8:
        case 9:
        case 10:
            return cfg;
        default:
            return cfg;
    }
}

template <typename T>
T getOr(const json& j, const std::string& key, const T& defaultValue)
{
    if (j.contains(key))
    {
        return j[key].get<T>();
    }
    return defaultValue;
}

glm::vec2 getVec2Or(const json& j, const std::string& key, const glm::vec2& defaultValue)
{
    if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
    {
        return glm::vec2(j[key][0].get<float>(), j[key][1].get<float>());
    }
    return defaultValue;
}

glm::vec3 getVec3Or(const json& j, const std::string& key, const glm::vec3& defaultValue)
{
    if (j.contains(key) && j[key].is_array() && j[key].size() >= 3)
    {
        return glm::vec3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
    }
    return defaultValue;
}

engine::scene::EnvironmentWindSettings getEnvironmentWindOr(
    const json& j, const std::string& key,
    const engine::scene::EnvironmentWindSettings& defaultValue)
{
    if (!j.contains(key) || !j[key].is_object())
    {
        return defaultValue;
    }

    const json& entry = j[key];
    engine::scene::EnvironmentWindSettings wind = defaultValue;
    wind.direction = getVec2Or(entry, "direction", wind.direction);
    wind.speed = getOr(entry, "speed", wind.speed);
    wind.strength = getOr(entry, "strength", wind.strength);
    wind.gustStrength = getOr(entry, "gustStrength", wind.gustStrength);
    wind.gustFrequencyHz =
        getOr(entry, "gustFrequencyHz", wind.gustFrequencyHz);
    wind.turbulenceStrength =
        getOr(entry, "turbulenceStrength", wind.turbulenceStrength);
    wind.verticalLift = getOr(entry, "verticalLift", wind.verticalLift);
    return wind;
}

engine::scene::EnvironmentTimeSettings getEnvironmentTimeOr(
    const json& j, const std::string& key,
    const engine::scene::EnvironmentTimeSettings& defaultValue)
{
    if (!j.contains(key) || !j[key].is_object())
    {
        return defaultValue;
    }

    const json& entry = j[key];
    engine::scene::EnvironmentTimeSettings time = defaultValue;
    time.enabled = getOr(entry, "enabled", time.enabled);
    time.cycleEnabled = getOr(entry, "cycleEnabled", time.cycleEnabled);
    time.timeOfDayHours =
        getOr(entry, "timeOfDayHours", time.timeOfDayHours);
    time.dayLengthMinutes =
        getOr(entry, "dayLengthMinutes", time.dayLengthMinutes);
    return time;
}

engine::scene::WindborneParticleSettings getWindborneParticlesOr(
    const json& j, const std::string& key,
    const engine::scene::WindborneParticleSettings& defaultValue)
{
    if (!j.contains(key) || !j[key].is_object())
    {
        return defaultValue;
    }

    const json& entry = j[key];
    engine::scene::WindborneParticleSettings particles = defaultValue;
    particles.enabled = getOr(entry, "enabled", particles.enabled);
    particles.amount = getOr(entry, "amount", particles.amount);
    particles.leafFraction =
        getOr(entry, "leafFraction", particles.leafFraction);
    particles.scale = getOr(entry, "scale", particles.scale);
    particles.visibilityDistance =
        getOr(entry, "visibilityDistance", particles.visibilityDistance);
    return particles;
}

glm::quat getQuatOr(const json& j, const std::string& key, const glm::quat& defaultValue)
{
    if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
    {
        return glm::quat(j[key][0].get<float>(), j[key][1].get<float>(),
                         j[key][2].get<float>(), j[key][3].get<float>());
    }
    return defaultValue;
}

std::vector<PlaceableInstance> getPlaceablesOr(const json& j, const std::string& key,
                                               const std::vector<PlaceableInstance>& defaultValue)
{
    if (!j.contains(key) || !j[key].is_array())
    {
        return defaultValue;
    }

    std::vector<PlaceableInstance> placeables{};
    placeables.reserve(j[key].size());
    for (const json& entry : j[key])
    {
        if (!entry.is_object())
        {
            continue;
        }

        PlaceableInstance instance{};
        instance.uuid = getOr(entry, "uuid", instance.uuid);
        instance.prototypeSlug = getOr(entry, "prototypeSlug", instance.prototypeSlug);
        instance.prototypeVersion =
            getOr(entry, "prototypeVersion", instance.prototypeVersion);
        instance.position = getVec3Or(entry, "position", instance.position);
        instance.rotation = getQuatOr(entry, "rotation", instance.rotation);
        instance.scale = getVec3Or(entry, "scale", instance.scale);
        instance.seed = getOr(entry, "seed", instance.seed);

        if (!instance.uuid.empty() && !instance.prototypeSlug.empty())
        {
            placeables.push_back(std::move(instance));
        }
    }
    return placeables;
}

CreatureNeeds getCreatureNeedsOr(const json& j, const std::string& key,
                                 const CreatureNeeds& defaultValue)
{
    if (!j.contains(key) || !j[key].is_object())
    {
        return defaultValue;
    }

    const json& entry = j[key];
    CreatureNeeds needs = defaultValue;
    needs.hunger = getOr(entry, "hunger", needs.hunger);
    needs.cleanliness = getOr(entry, "cleanliness", needs.cleanliness);
    needs.happiness = getOr(entry, "happiness", needs.happiness);
    needs.health = getOr(entry, "health", needs.health);
    return needs;
}

WaterState getWaterStateOr(const json& j, const std::string& key,
                           const WaterState& defaultValue)
{
    if (!j.contains(key) || !j[key].is_object())
    {
        return defaultValue;
    }

    const json& entry = j[key];
    WaterState water = defaultValue;
    water.temperatureC = getOr(entry, "temperatureC", water.temperatureC);
    water.oxygen = getOr(entry, "oxygen", water.oxygen);
    water.flow = getOr(entry, "flow", water.flow);
    water.cleanliness = getOr(entry, "cleanliness", water.cleanliness);
    return water;
}

ProgressionState getProgressionStateOr(const json& j, const std::string& key,
                                       const ProgressionState& defaultValue)
{
    if (!j.contains(key) || !j[key].is_object())
    {
        return defaultValue;
    }

    const json& entry = j[key];
    ProgressionState progression = defaultValue;
    progression.coins = getOr(entry, "coins", progression.coins);
    progression.discovery = getOr(entry, "discovery", progression.discovery);
    progression.careMilestone = getOr(entry, "careMilestone", progression.careMilestone);
    progression.tankTier = getOr(entry, "tankTier", progression.tankTier);
    return progression;
}

std::vector<CreatureInstance> getCreatureInstancesOr(
    const json& j, const std::string& key, const std::vector<CreatureInstance>& defaultValue)
{
    if (!j.contains(key) || !j[key].is_array())
    {
        return defaultValue;
    }

    std::vector<CreatureInstance> creatures{};
    creatures.reserve(j[key].size());
    for (const json& entry : j[key])
    {
        if (!entry.is_object())
        {
            continue;
        }

        CreatureInstance creature{};
        creature.uuid = getOr(entry, "uuid", creature.uuid);
        creature.speciesId = getOr(entry, "speciesId", creature.speciesId);
        creature.displayName = getOr(entry, "displayName", creature.displayName);
        creature.primary = getOr(entry, "primary", creature.primary);
        creature.bond = getOr(entry, "bond", creature.bond);
        creature.vitality = getOr(entry, "vitality", creature.vitality);
        creature.needs = getCreatureNeedsOr(entry, "needs", creature.needs);

        if (!creature.uuid.empty() && !creature.speciesId.empty())
        {
            creatures.push_back(std::move(creature));
        }
    }
    return creatures;
}

GameState getGameStateOr(const json& j, const std::string& key,
                         const GameState& defaultValue)
{
    if (!j.contains(key) || !j[key].is_object())
    {
        return defaultValue;
    }

    const json& entry = j[key];
    GameState gameState = defaultValue;
    gameState.lastPlayedUtc = getOr(entry, "lastPlayedUtc", gameState.lastPlayedUtc);
    gameState.water = getWaterStateOr(entry, "water", gameState.water);
    gameState.progression = getProgressionStateOr(entry, "progression", gameState.progression);
    gameState.creatures = getCreatureInstancesOr(entry, "creatures", gameState.creatures);
    return gameState;
}

std::optional<engine::scene::ScenePresentationProfile> getPresentationProfileOr(
    const json& j, const std::string& key, const std::filesystem::path& path,
    const engine::scene::ScenePresentationProfile& namedBase,
    SceneSerializerLogMode logMode)
{
    if (!j.contains(key))
    {
        return std::nullopt;
    }
    if (!j[key].is_object())
    {
        logSceneSerializerMessage(
            LogSeverity::Warning,
            std::string("Ignoring malformed non-object 'presentationProfile' in ") +
                path.string(),
            logMode);
        return std::nullopt;
    }

    const json& entry = j[key];
    const int rawProfileVersion = getOr(entry, "version", 0);
    if (rawProfileVersion < 0 ||
        static_cast<uint32_t>(rawProfileVersion) >
            engine::scene::ScenePresentationProfile::kCurrentVersion)
    {
        logSceneSerializerMessage(
            LogSeverity::Warning,
            std::string("Ignoring 'presentationProfile' with unsupported version ") +
                std::to_string(rawProfileVersion) + " in " + path.string() +
                " (supported: " +
                std::to_string(engine::scene::ScenePresentationProfile::kCurrentVersion) +
                "); the scene keeps its named-base look",
            logMode);
        return std::nullopt;
    }

    // a serialized profile is an override layer. missing fields retain the
    // scene's named base rather than resetting to global schema defaults.
    engine::scene::ScenePresentationProfile profile = namedBase;
    profile.version = engine::scene::ScenePresentationProfile::kCurrentVersion;
    profile.name = getOr(entry, "name", profile.name);

    // profile v0 predates these buckets. upgrade it with the exact compatibility
    // values even if a future named base enables soft profile v1; this prevents
    // an old serialized override from silently inheriting newly introduced look.
    if (rawProfileVersion == 0)
    {
        profile.voxelCellVariation = {};
        profile.hemisphereAmbient = {};
        profile.atmosphere = {};
    }
    // profile v2 introduces projected-distance ao. older profiles must retain
    // their authored world-distance behavior even if a named base changes.
    if (rawProfileVersion < 2)
    {
        profile.ambientOcclusion.distanceMode =
            engine::render::AmbientOcclusionDistanceMode::AuthoredWorldDistance;
        profile.ambientOcclusion.projectedRadiusPixels = 512.0f;
        profile.ambientOcclusion.projectedMinimumWorldDistance = 2.0f;
    }
    // profile v3 introduces the soft-edge reconstruction control. an older
    // serialized profile is an authored compatibility snapshot, so it must not
    // silently inherit the newly softened named-base presentation.
    if (rawProfileVersion < 3)
    {
        profile.postFx.taaSoftEdgeStrength = 0.0f;
    }
    // profile v4 owns the widened palette and painted material response. keep
    // older authored profiles on the exact historical spread and pbr path.
    if (rawProfileVersion < 4)
    {
        profile.voxelSurface.cavityStrength = 1.0f;
        profile.voxelSurface.paintedMaterialStrength = 0.0f;
        profile.voxelCellVariation.hueSpread = 0.018f;
        profile.voxelCellVariation.saturationSpread = 0.12f;
        profile.voxelCellVariation.valueSpread = 0.10f;
        profile.voxelCellVariation.paletteFamilyStrength = 0.0f;
    }
    // profile v5 introduces the painted-sky layer. older authored profiles
    // keep the exact flat-sky compatibility path even when their named base
    // now selects the new outdoor sky candidate.
    if (rawProfileVersion < 5)
    {
        profile.paintedSky = {};
    }
    // profile v6 introduces the production painted-cloud layer. older authored
    // snapshots remain cloud-free even when their named base advances.
    if (rawProfileVersion < 6)
    {
        profile.paintedClouds = {};
    }

    if (entry.contains("lighting") && entry["lighting"].is_object())
    {
        const json& section = entry["lighting"];
        auto& lighting = profile.lighting;
        lighting.skyPreset = getOr(section, "skyPreset", lighting.skyPreset);
        lighting.skyColor = getVec3Or(section, "skyColor", lighting.skyColor);
        lighting.pointLightsEnabled =
            getOr(section, "pointLightsEnabled", lighting.pointLightsEnabled);
        lighting.lightCount = getOr(section, "lightCount", lighting.lightCount);
        lighting.sunElevation = getOr(section, "sunElevation", lighting.sunElevation);
        lighting.sunAzimuth = getOr(section, "sunAzimuth", lighting.sunAzimuth);
        lighting.sunColor = getVec3Or(section, "sunColor", lighting.sunColor);
        lighting.sunIntensity = getOr(section, "sunIntensity", lighting.sunIntensity);
    }

    if (entry.contains("shadows") && entry["shadows"].is_object())
    {
        const json& section = entry["shadows"];
        auto& shadows = profile.shadows;
        shadows.csmEnabled = getOr(section, "csmEnabled", shadows.csmEnabled);
        shadows.useDdaShadows = getOr(section, "useDdaShadows", shadows.useDdaShadows);
        shadows.requestedSunAngularRadius =
            getOr(section, "requestedSunAngularRadius", shadows.requestedSunAngularRadius);
        shadows.maxShadowDistance =
            getOr(section, "maxShadowDistance", shadows.maxShadowDistance);
        shadows.normalBias = getOr(section, "normalBias", shadows.normalBias);
        shadows.maxSteps = getOr(section, "maxSteps", shadows.maxSteps);
        shadows.ddaSunSampleCount =
            getOr(section, "ddaSunSampleCount", shadows.ddaSunSampleCount);
        shadows.foliageOpacity = getOr(section, "foliageOpacity", shadows.foliageOpacity);
        shadows.temporalBlendAlpha =
            getOr(section, "temporalBlendAlpha", shadows.temporalBlendAlpha);
        shadows.temporalDepthReject =
            getOr(section, "temporalDepthReject", shadows.temporalDepthReject);
        shadows.temporalNormalRejectDot =
            getOr(section, "temporalNormalRejectDot", shadows.temporalNormalRejectDot);
        shadows.temporalClampSharpness =
            getOr(section, "temporalClampSharpness", shadows.temporalClampSharpness);
        shadows.spatialFilterRadius =
            getOr(section, "spatialFilterRadius", shadows.spatialFilterRadius);
        shadows.spatialDepthSigma =
            getOr(section, "spatialDepthSigma", shadows.spatialDepthSigma);
        shadows.spatialValueSigma =
            getOr(section, "spatialValueSigma", shadows.spatialValueSigma);
        shadows.spatialNormalPower =
            getOr(section, "spatialNormalPower", shadows.spatialNormalPower);
        shadows.postDenoiseRadius =
            getOr(section, "postDenoiseRadius", shadows.postDenoiseRadius);
        shadows.postDenoiseDepthSigma =
            getOr(section, "postDenoiseDepthSigma", shadows.postDenoiseDepthSigma);
        shadows.postDenoiseValueSigma =
            getOr(section, "postDenoiseValueSigma", shadows.postDenoiseValueSigma);
        shadows.postDenoiseNormalPower =
            getOr(section, "postDenoiseNormalPower", shadows.postDenoiseNormalPower);
        shadows.localLightShadowsEnabled =
            getOr(section, "localLightShadowsEnabled", shadows.localLightShadowsEnabled);
        shadows.localShadowCastingLightCount = getOr(section, "localShadowCastingLightCount",
                                                     shadows.localShadowCastingLightCount);
        shadows.localTemporalBlendAlpha =
            getOr(section, "localTemporalBlendAlpha", shadows.localTemporalBlendAlpha);
        shadows.localTemporalDepthReject =
            getOr(section, "localTemporalDepthReject", shadows.localTemporalDepthReject);
        shadows.localBlurEnabled = getOr(section, "localBlurEnabled", shadows.localBlurEnabled);
        shadows.localBlurSigma = getOr(section, "localBlurSigma", shadows.localBlurSigma);
        shadows.terminatorSoftness =
            getOr(section, "terminatorSoftness", shadows.terminatorSoftness);
        shadows.terminatorMode = getOr(section, "terminatorMode", shadows.terminatorMode);
        shadows.csmDitherEnabled = getOr(section, "csmDitherEnabled", shadows.csmDitherEnabled);
    }

    if (entry.contains("ambientOcclusion") && entry["ambientOcclusion"].is_object())
    {
        const json& section = entry["ambientOcclusion"];
        auto& ao = profile.ambientOcclusion;
        ao.enabled = getOr(section, "enabled", ao.enabled);
        if (rawProfileVersion >= 2)
        {
            const std::string distanceMode = getOr(
                section, "distanceMode",
                std::string(engine::render::ambientOcclusionDistanceModeName(
                    ao.distanceMode)));
            if (const auto parsed =
                    engine::render::ambientOcclusionDistanceModeFromName(distanceMode))
            {
                ao.distanceMode = *parsed;
            }
            ao.projectedRadiusPixels = getOr(
                section, "projectedRadiusPixels", ao.projectedRadiusPixels);
            ao.projectedMinimumWorldDistance = getOr(
                section, "projectedMinimumWorldDistance",
                ao.projectedMinimumWorldDistance);
        }
        ao.maxDistance = getOr(section, "maxDistance", ao.maxDistance);
        ao.stepSize = getOr(section, "stepSize", ao.stepSize);
        ao.intensity = getOr(section, "intensity", ao.intensity);
        ao.contribution = getOr(section, "contribution", ao.contribution);
        ao.bias = getOr(section, "bias", ao.bias);
        ao.rayCount = getOr(section, "rayCount", ao.rayCount);
        ao.temporalBlendAlpha = getOr(section, "temporalBlendAlpha", ao.temporalBlendAlpha);
        ao.temporalDepthReject = getOr(section, "temporalDepthReject", ao.temporalDepthReject);
        ao.temporalNormalRejectDot =
            getOr(section, "temporalNormalRejectDot", ao.temporalNormalRejectDot);
    }

    if (entry.contains("postFx") && entry["postFx"].is_object())
    {
        const json& section = entry["postFx"];
        auto& postFx = profile.postFx;
        postFx.tonemapEnabled = getOr(section, "tonemapEnabled", postFx.tonemapEnabled);
        postFx.exposure = getOr(section, "exposure", postFx.exposure);
        postFx.highlightRecovery =
            getOr(section, "highlightRecovery", postFx.highlightRecovery);
        postFx.bloomEnabled = getOr(section, "bloomEnabled", postFx.bloomEnabled);
        postFx.bloomThreshold = getOr(section, "bloomThreshold", postFx.bloomThreshold);
        postFx.bloomKnee = getOr(section, "bloomKnee", postFx.bloomKnee);
        postFx.bloomIntensity = getOr(section, "bloomIntensity", postFx.bloomIntensity);
        postFx.bloomSigma = getOr(section, "bloomSigma", postFx.bloomSigma);
        postFx.vignetteStrength = getOr(section, "vignetteStrength", postFx.vignetteStrength);
        postFx.grainStrength = getOr(section, "grainStrength", postFx.grainStrength);
        postFx.colorGradeEnabled =
            getOr(section, "colorGradeEnabled", postFx.colorGradeEnabled);
        postFx.colorGradeStrength =
            getOr(section, "colorGradeStrength", postFx.colorGradeStrength);
        postFx.colorGradeSaturation =
            getOr(section, "colorGradeSaturation", postFx.colorGradeSaturation);
        postFx.colorGradeContrast =
            getOr(section, "colorGradeContrast", postFx.colorGradeContrast);
        postFx.colorGradeTemperature =
            getOr(section, "colorGradeTemperature", postFx.colorGradeTemperature);
        postFx.pixelizationEnabled =
            getOr(section, "pixelizationEnabled", postFx.pixelizationEnabled);
        postFx.pixelizationBlockSize =
            getOr(section, "pixelizationBlockSize", postFx.pixelizationBlockSize);
        postFx.pixelizationStrength =
            getOr(section, "pixelizationStrength", postFx.pixelizationStrength);
        postFx.pixelizationEdgeFocus =
            getOr(section, "pixelizationEdgeFocus", postFx.pixelizationEdgeFocus);
        postFx.depthOfFieldEnabled =
            getOr(section, "depthOfFieldEnabled", postFx.depthOfFieldEnabled);
        postFx.depthOfFieldFocusDistance =
            getOr(section, "depthOfFieldFocusDistance", postFx.depthOfFieldFocusDistance);
        postFx.depthOfFieldFocusRange =
            getOr(section, "depthOfFieldFocusRange", postFx.depthOfFieldFocusRange);
        postFx.depthOfFieldBlurStrength =
            getOr(section, "depthOfFieldBlurStrength", postFx.depthOfFieldBlurStrength);
        postFx.taaEnabled = getOr(section, "taaEnabled", postFx.taaEnabled);
        postFx.jitterEnabled = getOr(section, "jitterEnabled", postFx.jitterEnabled);
        postFx.taaSimilarityThreshold =
            getOr(section, "taaSimilarityThreshold", postFx.taaSimilarityThreshold);
        postFx.taaVelocityScale =
            getOr(section, "taaVelocityScale", postFx.taaVelocityScale);
        postFx.taaBlendMin = getOr(section, "taaBlendMin", postFx.taaBlendMin);
        postFx.taaBlendMax = getOr(section, "taaBlendMax", postFx.taaBlendMax);
        postFx.taaSharpen = getOr(section, "taaSharpen", postFx.taaSharpen);
        postFx.taaDepthEdgeThreshold =
            getOr(section, "taaDepthEdgeThreshold", postFx.taaDepthEdgeThreshold);
        postFx.taaCrossFrameDepthThreshold = getOr(section, "taaCrossFrameDepthThreshold",
                                                   postFx.taaCrossFrameDepthThreshold);
        postFx.taaColorVarianceThreshold = getOr(section, "taaColorVarianceThreshold",
                                                 postFx.taaColorVarianceThreshold);
        postFx.taaSoftEdgeStrength =
            getOr(section, "taaSoftEdgeStrength", postFx.taaSoftEdgeStrength);
        postFx.fxaaEnabled = getOr(section, "fxaaEnabled", postFx.fxaaEnabled);
    }

    if (entry.contains("voxelSurface") && entry["voxelSurface"].is_object())
    {
        const json& section = entry["voxelSurface"];
        auto& voxelSurface = profile.voxelSurface;
        voxelSurface.materialDetailStrength =
            getOr(section, "materialDetailStrength", voxelSurface.materialDetailStrength);
        voxelSurface.normalEdgeSmoothing =
            getOr(section, "normalEdgeSmoothing", voxelSurface.normalEdgeSmoothing);
        voxelSurface.pixelEdgeShadowStrength =
            getOr(section, "pixelEdgeShadowStrength", voxelSurface.pixelEdgeShadowStrength);
        voxelSurface.cavityStrength =
            getOr(section, "cavityStrength", voxelSurface.cavityStrength);
        voxelSurface.paintedMaterialStrength =
            getOr(section, "paintedMaterialStrength", voxelSurface.paintedMaterialStrength);
    }

    if (rawProfileVersion >= 1 && entry.contains("voxelCellVariation") &&
        entry["voxelCellVariation"].is_object())
    {
        const json& section = entry["voxelCellVariation"];
        auto& variation = profile.voxelCellVariation;
        variation.masterStrength =
            getOr(section, "masterStrength", variation.masterStrength);
        variation.genericAmplitude =
            getOr(section, "genericAmplitude", variation.genericAmplitude);
        variation.gravelAmplitude =
            getOr(section, "gravelAmplitude", variation.gravelAmplitude);
        variation.plantAmplitude =
            getOr(section, "plantAmplitude", variation.plantAmplitude);
        variation.stoneAmplitude =
            getOr(section, "stoneAmplitude", variation.stoneAmplitude);
        variation.woodAmplitude =
            getOr(section, "woodAmplitude", variation.woodAmplitude);
        variation.hueSpread = getOr(section, "hueSpread", variation.hueSpread);
        variation.saturationSpread =
            getOr(section, "saturationSpread", variation.saturationSpread);
        variation.valueSpread = getOr(section, "valueSpread", variation.valueSpread);
        variation.paletteFamilyStrength =
            getOr(section, "paletteFamilyStrength", variation.paletteFamilyStrength);
    }

    if (rawProfileVersion >= 1 && entry.contains("hemisphereAmbient") &&
        entry["hemisphereAmbient"].is_object())
    {
        const json& section = entry["hemisphereAmbient"];
        auto& hemisphere = profile.hemisphereAmbient;
        hemisphere.strength = getOr(section, "strength", hemisphere.strength);
        hemisphere.skyTint = getVec3Or(section, "skyTint", hemisphere.skyTint);
        hemisphere.groundTint =
            getVec3Or(section, "groundTint", hemisphere.groundTint);
    }

    if (rawProfileVersion >= 1 && entry.contains("atmosphere") &&
        entry["atmosphere"].is_object())
    {
        const json& section = entry["atmosphere"];
        auto& atmosphere = profile.atmosphere;
        atmosphere.density = getOr(section, "density", atmosphere.density);
        atmosphere.heightFalloff =
            getOr(section, "heightFalloff", atmosphere.heightFalloff);
        atmosphere.baseHeight =
            getOr(section, "baseHeight", atmosphere.baseHeight);
        atmosphere.sunPhaseStrength =
            getOr(section, "sunPhaseStrength", atmosphere.sunPhaseStrength);
        atmosphere.sunPhaseExponent =
            getOr(section, "sunPhaseExponent", atmosphere.sunPhaseExponent);
    }

    if (rawProfileVersion >= 5 && entry.contains("paintedSky") &&
        entry["paintedSky"].is_object())
    {
        const json& section = entry["paintedSky"];
        auto& sky = profile.paintedSky;
        sky.strength = getOr(section, "strength", sky.strength);
        sky.horizonTint = getVec3Or(section, "horizonTint", sky.horizonTint);
        sky.zenithTint = getVec3Or(section, "zenithTint", sky.zenithTint);
        sky.lowerHemisphereTint = getVec3Or(
            section, "lowerHemisphereTint", sky.lowerHemisphereTint);
        sky.gradientExponent =
            getOr(section, "gradientExponent", sky.gradientExponent);
        sky.horizonBandStrength =
            getOr(section, "horizonBandStrength", sky.horizonBandStrength);
        sky.horizonBandExponent =
            getOr(section, "horizonBandExponent", sky.horizonBandExponent);
        sky.sunDiscAngularRadius = getOr(
            section, "sunDiscAngularRadius", sky.sunDiscAngularRadius);
        sky.sunDiscSoftness =
            getOr(section, "sunDiscSoftness", sky.sunDiscSoftness);
        sky.sunDiscIntensity =
            getOr(section, "sunDiscIntensity", sky.sunDiscIntensity);
        sky.sunHaloIntensity =
            getOr(section, "sunHaloIntensity", sky.sunHaloIntensity);
        sky.sunHaloExponent =
            getOr(section, "sunHaloExponent", sky.sunHaloExponent);
    }

    if (rawProfileVersion >= 6 && entry.contains("paintedClouds") &&
        entry["paintedClouds"].is_object())
    {
        const json& section = entry["paintedClouds"];
        auto& clouds = profile.paintedClouds;
        clouds.strength = getOr(section, "strength", clouds.strength);
        clouds.coverage = getOr(section, "coverage", clouds.coverage);
        clouds.opacity = getOr(section, "opacity", clouds.opacity);
        clouds.softness = getOr(section, "softness", clouds.softness);
        clouds.altitude = getOr(section, "altitude", clouds.altitude);
        clouds.worldScale = getOr(section, "worldScale", clouds.worldScale);
        clouds.detailStrength =
            getOr(section, "detailStrength", clouds.detailStrength);
        clouds.lightTint = getVec3Or(section, "lightTint", clouds.lightTint);
        clouds.shadowTint = getVec3Or(section, "shadowTint", clouds.shadowTint);
        clouds.silverLiningStrength = getOr(
            section, "silverLiningStrength", clouds.silverLiningStrength);
        clouds.horizonFadeStart =
            getOr(section, "horizonFadeStart", clouds.horizonFadeStart);
        clouds.horizonFadeEnd =
            getOr(section, "horizonFadeEnd", clouds.horizonFadeEnd);
    }

    if (entry.contains("water") && entry["water"].is_object())
    {
        const json& section = entry["water"];
        auto& water = profile.water;
        water.stylizedMode = getOr(section, "stylizedMode", water.stylizedMode);
        water.absorption = getOr(section, "absorption", water.absorption);
        water.refract = getOr(section, "refract", water.refract);
        water.depthScale = getOr(section, "depthScale", water.depthScale);
        water.shallowBias = getOr(section, "shallowBias", water.shallowBias);
        water.depthToMeters = getOr(section, "depthToMeters", water.depthToMeters);
        water.specIntensity = getOr(section, "specIntensity", water.specIntensity);
        water.specPower = getOr(section, "specPower", water.specPower);
        water.waveScale = getOr(section, "waveScale", water.waveScale);
        water.waveAmp = getOr(section, "waveAmp", water.waveAmp);
        water.crestHighlightsEnabled =
            getOr(section, "crestHighlightsEnabled", water.crestHighlightsEnabled);
        water.crestThreshold = getOr(section, "crestThreshold", water.crestThreshold);
        water.crestSoftness = getOr(section, "crestSoftness", water.crestSoftness);
        water.crestIntensity = getOr(section, "crestIntensity", water.crestIntensity);
        water.foamDepthThreshold =
            getOr(section, "foamDepthThreshold", water.foamDepthThreshold);
        water.foamOpacity = getOr(section, "foamOpacity", water.foamOpacity);
        water.foamScale = getOr(section, "foamScale", water.foamScale);
        water.distortionDepthScale =
            getOr(section, "distortionDepthScale", water.distortionDepthScale);
        water.edgeFadeDepth = getOr(section, "edgeFadeDepth", water.edgeFadeDepth);
        water.bandHardness = getOr(section, "bandHardness", water.bandHardness);
        water.reflectionStrength =
            getOr(section, "reflectionStrength", water.reflectionStrength);
        water.fresnelBias = getOr(section, "fresnelBias", water.fresnelBias);
        water.causticsEnabled = getOr(section, "causticsEnabled", water.causticsEnabled);
        water.causticsIntensity =
            getOr(section, "causticsIntensity", water.causticsIntensity);
        water.causticsScale = getOr(section, "causticsScale", water.causticsScale);
        water.causticsSpeed = getOr(section, "causticsSpeed", water.causticsSpeed);
        water.causticsBanding = getOr(section, "causticsBanding", water.causticsBanding);
        water.causticsDepthFade =
            getOr(section, "causticsDepthFade", water.causticsDepthFade);
        water.particlesPlanned = getOr(section, "particlesPlanned", water.particlesPlanned);
        water.particlesPlannedDensity =
            getOr(section, "particlesPlannedDensity", water.particlesPlannedDensity);
        water.particlesPlannedDrift =
            getOr(section, "particlesPlannedDrift", water.particlesPlannedDrift);
        water.particlesPlannedScale =
            getOr(section, "particlesPlannedScale", water.particlesPlannedScale);
        water.foamEmitterEnabled =
            getOr(section, "foamEmitterEnabled", water.foamEmitterEnabled);
        water.foamEmitterIntensity =
            getOr(section, "foamEmitterIntensity", water.foamEmitterIntensity);
        water.foamEmitterRadius =
            getOr(section, "foamEmitterRadius", water.foamEmitterRadius);
        water.foamEmitterOffsetX =
            getOr(section, "foamEmitterOffsetX", water.foamEmitterOffsetX);
        water.foamEmitterOffsetZ =
            getOr(section, "foamEmitterOffsetZ", water.foamEmitterOffsetZ);
        water.foamEmitterScale = getOr(section, "foamEmitterScale", water.foamEmitterScale);
        water.foamEmitterSpread =
            getOr(section, "foamEmitterSpread", water.foamEmitterSpread);
        water.gradientStrength = getOr(section, "gradientStrength", water.gradientStrength);
        water.planarReflectionEnabled =
            getOr(section, "planarReflectionEnabled", water.planarReflectionEnabled);
        water.planarObliqueClipEnabled =
            getOr(section, "planarObliqueClipEnabled", water.planarObliqueClipEnabled);
        water.planarStrength = getOr(section, "planarStrength", water.planarStrength);
    }

    if (entry.contains("glass") && entry["glass"].is_object())
    {
        const json& section = entry["glass"];
        auto& glass = profile.glass;
        glass.tint = getVec3Or(section, "tint", glass.tint);
        glass.reflection = getVec3Or(section, "reflection", glass.reflection);
        glass.absorption = getOr(section, "absorption", glass.absorption);
        glass.thicknessScale = getOr(section, "thicknessScale", glass.thicknessScale);
        glass.refract = getOr(section, "refract", glass.refract);
        glass.ior = getOr(section, "ior", glass.ior);
        glass.bubbleScale = getOr(section, "bubbleScale", glass.bubbleScale);
        glass.bubbleIntensity = getOr(section, "bubbleIntensity", glass.bubbleIntensity);
        glass.bubbleThicknessGate =
            getOr(section, "bubbleThicknessGate", glass.bubbleThicknessGate);
        glass.bubbleChromaticSplit =
            getOr(section, "bubbleChromaticSplit", glass.bubbleChromaticSplit);
        glass.iridescentStrength =
            getOr(section, "iridescentStrength", glass.iridescentStrength);
        glass.iridescentFilmThickness =
            getOr(section, "iridescentFilmThickness", glass.iridescentFilmThickness);
        glass.iridescentFrequency =
            getOr(section, "iridescentFrequency", glass.iridescentFrequency);
        glass.voxelGlassRefractEnabled =
            getOr(section, "voxelGlassRefractEnabled", glass.voxelGlassRefractEnabled);
        glass.voxelGlassAbsorption =
            getOr(section, "voxelGlassAbsorption", glass.voxelGlassAbsorption);
        glass.voxelGlassRefractStrength =
            getOr(section, "voxelGlassRefractStrength", glass.voxelGlassRefractStrength);
        glass.voxelGlassIor = getOr(section, "voxelGlassIor", glass.voxelGlassIor);
        glass.voxelGlassReflectStrength =
            getOr(section, "voxelGlassReflectStrength", glass.voxelGlassReflectStrength);
        glass.voxelGlassTint = getVec3Or(section, "voxelGlassTint", glass.voxelGlassTint);
        glass.voxelGlassReflectionColor =
            getVec3Or(section, "voxelGlassReflectionColor", glass.voxelGlassReflectionColor);
    }

    return profile;
}

json makePresentationProfileJson(const engine::scene::ScenePresentationProfile& profile)
{
    json entry;
    entry["version"] = engine::scene::ScenePresentationProfile::kCurrentVersion;
    entry["name"] = profile.name;

    entry["lighting"] = {
        {"skyPreset", profile.lighting.skyPreset},
        {"skyColor",
         {profile.lighting.skyColor.x, profile.lighting.skyColor.y,
          profile.lighting.skyColor.z}},
        {"pointLightsEnabled", profile.lighting.pointLightsEnabled},
        {"lightCount", profile.lighting.lightCount},
        {"sunElevation", profile.lighting.sunElevation},
        {"sunAzimuth", profile.lighting.sunAzimuth},
        {"sunColor",
         {profile.lighting.sunColor.x, profile.lighting.sunColor.y,
          profile.lighting.sunColor.z}},
        {"sunIntensity", profile.lighting.sunIntensity},
    };

    entry["shadows"] = {
        {"csmEnabled", profile.shadows.csmEnabled},
        {"useDdaShadows", profile.shadows.useDdaShadows},
        {"requestedSunAngularRadius", profile.shadows.requestedSunAngularRadius},
        {"maxShadowDistance", profile.shadows.maxShadowDistance},
        {"normalBias", profile.shadows.normalBias},
        {"maxSteps", profile.shadows.maxSteps},
        {"ddaSunSampleCount", profile.shadows.ddaSunSampleCount},
        {"foliageOpacity", profile.shadows.foliageOpacity},
        {"temporalBlendAlpha", profile.shadows.temporalBlendAlpha},
        {"temporalDepthReject", profile.shadows.temporalDepthReject},
        {"temporalNormalRejectDot", profile.shadows.temporalNormalRejectDot},
        {"temporalClampSharpness", profile.shadows.temporalClampSharpness},
        {"spatialFilterRadius", profile.shadows.spatialFilterRadius},
        {"spatialDepthSigma", profile.shadows.spatialDepthSigma},
        {"spatialValueSigma", profile.shadows.spatialValueSigma},
        {"spatialNormalPower", profile.shadows.spatialNormalPower},
        {"postDenoiseRadius", profile.shadows.postDenoiseRadius},
        {"postDenoiseDepthSigma", profile.shadows.postDenoiseDepthSigma},
        {"postDenoiseValueSigma", profile.shadows.postDenoiseValueSigma},
        {"postDenoiseNormalPower", profile.shadows.postDenoiseNormalPower},
        {"localLightShadowsEnabled", profile.shadows.localLightShadowsEnabled},
        {"localShadowCastingLightCount", profile.shadows.localShadowCastingLightCount},
        {"localTemporalBlendAlpha", profile.shadows.localTemporalBlendAlpha},
        {"localTemporalDepthReject", profile.shadows.localTemporalDepthReject},
        {"localBlurEnabled", profile.shadows.localBlurEnabled},
        {"localBlurSigma", profile.shadows.localBlurSigma},
        {"terminatorSoftness", profile.shadows.terminatorSoftness},
        {"terminatorMode", profile.shadows.terminatorMode},
        {"csmDitherEnabled", profile.shadows.csmDitherEnabled},
    };

    entry["ambientOcclusion"] = {
        {"enabled", profile.ambientOcclusion.enabled},
        {"distanceMode",
         std::string(engine::render::ambientOcclusionDistanceModeName(
             profile.ambientOcclusion.distanceMode))},
        {"projectedRadiusPixels", profile.ambientOcclusion.projectedRadiusPixels},
        {"projectedMinimumWorldDistance",
         profile.ambientOcclusion.projectedMinimumWorldDistance},
        {"maxDistance", profile.ambientOcclusion.maxDistance},
        {"stepSize", profile.ambientOcclusion.stepSize},
        {"intensity", profile.ambientOcclusion.intensity},
        {"contribution", profile.ambientOcclusion.contribution},
        {"bias", profile.ambientOcclusion.bias},
        {"rayCount", profile.ambientOcclusion.rayCount},
        {"temporalBlendAlpha", profile.ambientOcclusion.temporalBlendAlpha},
        {"temporalDepthReject", profile.ambientOcclusion.temporalDepthReject},
        {"temporalNormalRejectDot", profile.ambientOcclusion.temporalNormalRejectDot},
    };

    entry["postFx"] = {
        {"tonemapEnabled", profile.postFx.tonemapEnabled},
        {"exposure", profile.postFx.exposure},
        {"highlightRecovery", profile.postFx.highlightRecovery},
        {"bloomEnabled", profile.postFx.bloomEnabled},
        {"bloomThreshold", profile.postFx.bloomThreshold},
        {"bloomKnee", profile.postFx.bloomKnee},
        {"bloomIntensity", profile.postFx.bloomIntensity},
        {"bloomSigma", profile.postFx.bloomSigma},
        {"vignetteStrength", profile.postFx.vignetteStrength},
        {"grainStrength", profile.postFx.grainStrength},
        {"colorGradeEnabled", profile.postFx.colorGradeEnabled},
        {"colorGradeStrength", profile.postFx.colorGradeStrength},
        {"colorGradeSaturation", profile.postFx.colorGradeSaturation},
        {"colorGradeContrast", profile.postFx.colorGradeContrast},
        {"colorGradeTemperature", profile.postFx.colorGradeTemperature},
        {"pixelizationEnabled", profile.postFx.pixelizationEnabled},
        {"pixelizationBlockSize", profile.postFx.pixelizationBlockSize},
        {"pixelizationStrength", profile.postFx.pixelizationStrength},
        {"pixelizationEdgeFocus", profile.postFx.pixelizationEdgeFocus},
        {"depthOfFieldEnabled", profile.postFx.depthOfFieldEnabled},
        {"depthOfFieldFocusDistance", profile.postFx.depthOfFieldFocusDistance},
        {"depthOfFieldFocusRange", profile.postFx.depthOfFieldFocusRange},
        {"depthOfFieldBlurStrength", profile.postFx.depthOfFieldBlurStrength},
        {"taaEnabled", profile.postFx.taaEnabled},
        {"jitterEnabled", profile.postFx.jitterEnabled},
        {"taaSimilarityThreshold", profile.postFx.taaSimilarityThreshold},
        {"taaVelocityScale", profile.postFx.taaVelocityScale},
        {"taaBlendMin", profile.postFx.taaBlendMin},
        {"taaBlendMax", profile.postFx.taaBlendMax},
        {"taaSharpen", profile.postFx.taaSharpen},
        {"taaDepthEdgeThreshold", profile.postFx.taaDepthEdgeThreshold},
        {"taaCrossFrameDepthThreshold", profile.postFx.taaCrossFrameDepthThreshold},
        {"taaColorVarianceThreshold", profile.postFx.taaColorVarianceThreshold},
        {"taaSoftEdgeStrength", profile.postFx.taaSoftEdgeStrength},
        {"fxaaEnabled", profile.postFx.fxaaEnabled},
    };

    entry["voxelSurface"] = {
        {"materialDetailStrength", profile.voxelSurface.materialDetailStrength},
        {"normalEdgeSmoothing", profile.voxelSurface.normalEdgeSmoothing},
        {"pixelEdgeShadowStrength", profile.voxelSurface.pixelEdgeShadowStrength},
        {"cavityStrength", profile.voxelSurface.cavityStrength},
        {"paintedMaterialStrength", profile.voxelSurface.paintedMaterialStrength},
    };

    entry["voxelCellVariation"] = {
        {"masterStrength", profile.voxelCellVariation.masterStrength},
        {"genericAmplitude", profile.voxelCellVariation.genericAmplitude},
        {"gravelAmplitude", profile.voxelCellVariation.gravelAmplitude},
        {"plantAmplitude", profile.voxelCellVariation.plantAmplitude},
        {"stoneAmplitude", profile.voxelCellVariation.stoneAmplitude},
        {"woodAmplitude", profile.voxelCellVariation.woodAmplitude},
        {"hueSpread", profile.voxelCellVariation.hueSpread},
        {"saturationSpread", profile.voxelCellVariation.saturationSpread},
        {"valueSpread", profile.voxelCellVariation.valueSpread},
        {"paletteFamilyStrength", profile.voxelCellVariation.paletteFamilyStrength},
    };

    entry["hemisphereAmbient"] = {
        {"strength", profile.hemisphereAmbient.strength},
        {"skyTint", {profile.hemisphereAmbient.skyTint.x,
                     profile.hemisphereAmbient.skyTint.y,
                     profile.hemisphereAmbient.skyTint.z}},
        {"groundTint", {profile.hemisphereAmbient.groundTint.x,
                        profile.hemisphereAmbient.groundTint.y,
                        profile.hemisphereAmbient.groundTint.z}},
    };

    entry["atmosphere"] = {
        {"density", profile.atmosphere.density},
        {"heightFalloff", profile.atmosphere.heightFalloff},
        {"baseHeight", profile.atmosphere.baseHeight},
        {"sunPhaseStrength", profile.atmosphere.sunPhaseStrength},
        {"sunPhaseExponent", profile.atmosphere.sunPhaseExponent},
    };

    entry["paintedSky"] = {
        {"strength", profile.paintedSky.strength},
        {"horizonTint",
         {profile.paintedSky.horizonTint.x, profile.paintedSky.horizonTint.y,
          profile.paintedSky.horizonTint.z}},
        {"zenithTint",
         {profile.paintedSky.zenithTint.x, profile.paintedSky.zenithTint.y,
          profile.paintedSky.zenithTint.z}},
        {"lowerHemisphereTint",
         {profile.paintedSky.lowerHemisphereTint.x,
          profile.paintedSky.lowerHemisphereTint.y,
          profile.paintedSky.lowerHemisphereTint.z}},
        {"gradientExponent", profile.paintedSky.gradientExponent},
        {"horizonBandStrength", profile.paintedSky.horizonBandStrength},
        {"horizonBandExponent", profile.paintedSky.horizonBandExponent},
        {"sunDiscAngularRadius", profile.paintedSky.sunDiscAngularRadius},
        {"sunDiscSoftness", profile.paintedSky.sunDiscSoftness},
        {"sunDiscIntensity", profile.paintedSky.sunDiscIntensity},
        {"sunHaloIntensity", profile.paintedSky.sunHaloIntensity},
        {"sunHaloExponent", profile.paintedSky.sunHaloExponent},
    };

    entry["paintedClouds"] = {
        {"strength", profile.paintedClouds.strength},
        {"coverage", profile.paintedClouds.coverage},
        {"opacity", profile.paintedClouds.opacity},
        {"softness", profile.paintedClouds.softness},
        {"altitude", profile.paintedClouds.altitude},
        {"worldScale", profile.paintedClouds.worldScale},
        {"detailStrength", profile.paintedClouds.detailStrength},
        {"lightTint",
         {profile.paintedClouds.lightTint.x,
          profile.paintedClouds.lightTint.y,
          profile.paintedClouds.lightTint.z}},
        {"shadowTint",
         {profile.paintedClouds.shadowTint.x,
          profile.paintedClouds.shadowTint.y,
          profile.paintedClouds.shadowTint.z}},
        {"silverLiningStrength",
         profile.paintedClouds.silverLiningStrength},
        {"horizonFadeStart", profile.paintedClouds.horizonFadeStart},
        {"horizonFadeEnd", profile.paintedClouds.horizonFadeEnd},
    };

    entry["water"] = {
        {"stylizedMode", profile.water.stylizedMode},
        {"absorption", profile.water.absorption},
        {"refract", profile.water.refract},
        {"depthScale", profile.water.depthScale},
        {"shallowBias", profile.water.shallowBias},
        {"depthToMeters", profile.water.depthToMeters},
        {"specIntensity", profile.water.specIntensity},
        {"specPower", profile.water.specPower},
        {"waveScale", profile.water.waveScale},
        {"waveAmp", profile.water.waveAmp},
        {"crestHighlightsEnabled", profile.water.crestHighlightsEnabled},
        {"crestThreshold", profile.water.crestThreshold},
        {"crestSoftness", profile.water.crestSoftness},
        {"crestIntensity", profile.water.crestIntensity},
        {"foamDepthThreshold", profile.water.foamDepthThreshold},
        {"foamOpacity", profile.water.foamOpacity},
        {"foamScale", profile.water.foamScale},
        {"distortionDepthScale", profile.water.distortionDepthScale},
        {"edgeFadeDepth", profile.water.edgeFadeDepth},
        {"bandHardness", profile.water.bandHardness},
        {"reflectionStrength", profile.water.reflectionStrength},
        {"fresnelBias", profile.water.fresnelBias},
        {"causticsEnabled", profile.water.causticsEnabled},
        {"causticsIntensity", profile.water.causticsIntensity},
        {"causticsScale", profile.water.causticsScale},
        {"causticsSpeed", profile.water.causticsSpeed},
        {"causticsBanding", profile.water.causticsBanding},
        {"causticsDepthFade", profile.water.causticsDepthFade},
        {"particlesPlanned", profile.water.particlesPlanned},
        {"particlesPlannedDensity", profile.water.particlesPlannedDensity},
        {"particlesPlannedDrift", profile.water.particlesPlannedDrift},
        {"particlesPlannedScale", profile.water.particlesPlannedScale},
        {"foamEmitterEnabled", profile.water.foamEmitterEnabled},
        {"foamEmitterIntensity", profile.water.foamEmitterIntensity},
        {"foamEmitterRadius", profile.water.foamEmitterRadius},
        {"foamEmitterOffsetX", profile.water.foamEmitterOffsetX},
        {"foamEmitterOffsetZ", profile.water.foamEmitterOffsetZ},
        {"foamEmitterScale", profile.water.foamEmitterScale},
        {"foamEmitterSpread", profile.water.foamEmitterSpread},
        {"gradientStrength", profile.water.gradientStrength},
        {"planarReflectionEnabled", profile.water.planarReflectionEnabled},
        {"planarObliqueClipEnabled", profile.water.planarObliqueClipEnabled},
        {"planarStrength", profile.water.planarStrength},
    };

    entry["glass"] = {
        {"tint", {profile.glass.tint.x, profile.glass.tint.y, profile.glass.tint.z}},
        {"reflection",
         {profile.glass.reflection.x, profile.glass.reflection.y,
          profile.glass.reflection.z}},
        {"absorption", profile.glass.absorption},
        {"thicknessScale", profile.glass.thicknessScale},
        {"refract", profile.glass.refract},
        {"ior", profile.glass.ior},
        {"bubbleScale", profile.glass.bubbleScale},
        {"bubbleIntensity", profile.glass.bubbleIntensity},
        {"bubbleThicknessGate", profile.glass.bubbleThicknessGate},
        {"bubbleChromaticSplit", profile.glass.bubbleChromaticSplit},
        {"iridescentStrength", profile.glass.iridescentStrength},
        {"iridescentFilmThickness", profile.glass.iridescentFilmThickness},
        {"iridescentFrequency", profile.glass.iridescentFrequency},
        {"voxelGlassRefractEnabled", profile.glass.voxelGlassRefractEnabled},
        {"voxelGlassAbsorption", profile.glass.voxelGlassAbsorption},
        {"voxelGlassRefractStrength", profile.glass.voxelGlassRefractStrength},
        {"voxelGlassIor", profile.glass.voxelGlassIor},
        {"voxelGlassReflectStrength", profile.glass.voxelGlassReflectStrength},
        {"voxelGlassTint",
         {profile.glass.voxelGlassTint.x, profile.glass.voxelGlassTint.y,
          profile.glass.voxelGlassTint.z}},
        {"voxelGlassReflectionColor",
         {profile.glass.voxelGlassReflectionColor.x,
          profile.glass.voxelGlassReflectionColor.y,
          profile.glass.voxelGlassReflectionColor.z}},
    };

    return entry;
}

bool parseSceneFormatVersion(const json& j, const std::filesystem::path& path,
                             uint32_t& outVersion, SceneLoadDiagnostics* diagnostics,
                             SceneSerializerLogMode logMode)
{
    if (!j.contains("version"))
    {
        outVersion = kLegacySceneFormatVersion;
        if (diagnostics != nullptr)
        {
            diagnostics->loadedLegacyVersion = true;
        }
        return true;
    }

    if (!j["version"].is_number_integer())
    {
        assignError(diagnostics, "Scene file has invalid 'version' field");
        logSceneSerializerMessage(
            LogSeverity::Error,
            std::string("Scene file has invalid 'version' field: ") + path.string(), logMode);
        return false;
    }

    const int rawVersion = j["version"].get<int>();
    if (rawVersion < 0)
    {
        assignError(diagnostics, "Scene file has negative schema version");
        logSceneSerializerMessage(
            LogSeverity::Error,
            std::string("Scene file has negative schema version: ") + path.string(), logMode);
        return false;
    }

    outVersion = static_cast<uint32_t>(rawVersion);
    if (outVersion > SceneConfig::kCurrentFormatVersion)
    {
        assignError(diagnostics, "Scene file format version is newer than supported");
        logSceneSerializerMessage(
            LogSeverity::Error,
            std::string("Scene file format version ") + std::to_string(outVersion) +
                " is newer than supported version " +
                std::to_string(SceneConfig::kCurrentFormatVersion) + ": " + path.string(),
            logMode);
        return false;
    }

    return true;
}

void validateSceneConfig(SceneConfig& cfg, const std::filesystem::path& path,
                         SceneSerializerLogMode logMode)
{
    const bool emitWarnings = shouldLog(logMode);

    auto clampIntField = [&](const char* field, int& value, int minValue, int maxValue) {
        const int clamped = std::clamp(value, minValue, maxValue);
        if (clamped != value && emitWarnings)
        {
            logWarning("Scene", std::string("Clamped scene field '") + std::string(field) + "' from " +
                                    std::to_string(value) + " to " + std::to_string(clamped) +
                                    " in " + path.string());
        }
        value = clamped;
    };

    auto clampFloatField = [&](const char* field, float& value, float minValue, float maxValue) {
        const float clamped = std::clamp(value, minValue, maxValue);
        if (std::abs(clamped - value) > 1e-6f && emitWarnings)
        {
            logWarning("Scene", std::string("Clamped scene field '") + std::string(field) + "' from " +
                                    std::to_string(value) + " to " + std::to_string(clamped) +
                                    " in " + path.string());
        }
        value = clamped;
    };

    clampIntField("worldDimsX", cfg.worldDimsX, 1, 1024);
    clampIntField("worldDimsY", cfg.worldDimsY, 1, 1024);
    clampIntField("worldDimsZ", cfg.worldDimsZ, 1, 1024);
    clampIntField("skyPreset", cfg.skyPreset, 0, 4);
    clampFloatField("cloudCoverage", cfg.cloudCoverage, 0.0f, 1.0f);
    clampFloatField("cloudShadowStrength", cfg.cloudShadowStrength, 0.0f, 1.0f);
    clampFloatField("starDensity", cfg.starDensity, 0.0f, 1.0f);
    clampFloatField("gameState.water.temperatureC", cfg.gameState.water.temperatureC, 0.0f,
                    40.0f);
    clampFloatField("gameState.water.oxygen", cfg.gameState.water.oxygen, 0.0f, 1.0f);
    clampFloatField("gameState.water.flow", cfg.gameState.water.flow, 0.0f, 1.0f);
    clampFloatField("gameState.water.cleanliness", cfg.gameState.water.cleanliness, 0.0f,
                    1.0f);

    const engine::scene::EnvironmentWindSettings sanitizedWind =
        engine::scene::sanitizeEnvironmentWindSettings(cfg.environmentWind);
    if (!engine::scene::environmentWindSettingsEquivalent(cfg.environmentWind,
                                                           sanitizedWind) &&
        emitWarnings)
    {
        logWarning("Scene", std::string("Sanitized environmentWind settings in ") +
                                path.string());
    }
    cfg.environmentWind = sanitizedWind;

    const engine::scene::EnvironmentTimeSettings sanitizedTime =
        engine::scene::sanitizeEnvironmentTimeSettings(cfg.environmentTime);
    if (!engine::scene::environmentTimeSettingsEquivalent(
            cfg.environmentTime, sanitizedTime) && emitWarnings)
    {
        logWarning("Scene",
                   std::string("Sanitized environmentTime settings in ") +
                       path.string());
    }
    cfg.environmentTime = sanitizedTime;

    const engine::scene::WindborneParticleSettings sanitizedParticles =
        engine::scene::sanitizeWindborneParticleSettings(
            cfg.windborneParticles);
    if (!engine::scene::windborneParticleSettingsEquivalent(
            cfg.windborneParticles, sanitizedParticles) &&
        emitWarnings)
    {
        logWarning("Scene",
                   std::string("Sanitized windborneParticles settings in ") +
                       path.string());
    }
    cfg.windborneParticles = sanitizedParticles;

    if (cfg.gameState.progression.tankTier == 0)
    {
        cfg.gameState.progression.tankTier = 1;
    }

    const float windLength = glm::length(cfg.cloudWindDirection);
    if (windLength < 1e-4f)
    {
        if (emitWarnings)
        {
            logWarning("Scene", std::string("Replacing zero-length cloudWindDirection in ") +
                                    path.string() + " with default forward direction.");
        }
        cfg.cloudWindDirection = glm::vec2(0.0f, 1.0f);
    }
    else if (std::abs(windLength - 1.0f) > 1e-3f)
    {
        if (emitWarnings)
        {
            logWarning("Scene", std::string("Normalized cloudWindDirection in ") + path.string());
        }
        cfg.cloudWindDirection /= windLength;
    }

    std::unordered_set<std::string> placeableUuids{};
    size_t removedPlaceableCount = 0;
    cfg.placeables.erase(
        std::remove_if(cfg.placeables.begin(), cfg.placeables.end(),
                       [&](PlaceableInstance& instance) {
                           const engine::game::PlaceableTransformValidation transform =
                               engine::game::validatePlaceableTransform(instance);
                           const bool invalidIdentity =
                               instance.uuid.empty() || instance.prototypeSlug.empty() ||
                               instance.prototypeVersion == 0;
                           const bool duplicateUuid =
                               !invalidIdentity && transform.valid() &&
                               !placeableUuids.insert(instance.uuid).second;
                           if (invalidIdentity || duplicateUuid || !transform.valid())
                           {
                               ++removedPlaceableCount;
                               return true;
                           }
                           instance = transform.canonical;
                           return false;
                       }),
        cfg.placeables.end());
    if (removedPlaceableCount > 0 && emitWarnings)
    {
        logWarning("Scene", makeLogMessage("Removed ", removedPlaceableCount,
                                           " invalid or duplicate placeable(s) from ",
                                           path.string()));
    }

    cfg.gameState.creatures.erase(
        std::remove_if(cfg.gameState.creatures.begin(), cfg.gameState.creatures.end(),
                       [](const CreatureInstance& creature) {
                           return creature.uuid.empty() || creature.speciesId.empty();
                       }),
        cfg.gameState.creatures.end());

    for (CreatureInstance& creature : cfg.gameState.creatures)
    {
        clampFloatField("gameState.creatures.bond", creature.bond, 0.0f, 1.0f);
        clampFloatField("gameState.creatures.vitality", creature.vitality, 0.0f, 1.0f);
        clampFloatField("gameState.creatures.needs.hunger", creature.needs.hunger, 0.0f,
                        1.0f);
        clampFloatField("gameState.creatures.needs.cleanliness", creature.needs.cleanliness,
                        0.0f, 1.0f);
        clampFloatField("gameState.creatures.needs.happiness", creature.needs.happiness,
                        0.0f, 1.0f);
        clampFloatField("gameState.creatures.needs.health", creature.needs.health, 0.0f,
                        1.0f);
    }

    const engine::game::CreatureRosterNormalizationResult creatureNormalization =
        engine::game::normalizeCreatureRoster(cfg.gameState);
    if (creatureNormalization.changed && emitWarnings)
    {
        logWarning(
            "Scene",
            std::string("Normalized creature roster in ") + path.string() +
                " (primary=" + creatureNormalization.primaryUuid +
                ", extra primary flags=" +
                std::to_string(creatureNormalization.clearedExtraPrimaryFlags) +
                ", duplicate UUIDs=" +
                std::to_string(creatureNormalization.repairedDuplicateUuids) + ").");
    }
}

std::filesystem::path makeAtomicWriteTempPath(const std::filesystem::path& path)
{
    std::filesystem::path tempPath = path;
    tempPath += ".tmp";
    return tempPath;
}

bool replaceFileAtomically(const std::filesystem::path& sourcePath,
                           const std::filesystem::path& destinationPath,
                           std::string& outError)
{
#if defined(_WIN32)
    if (MoveFileExW(sourcePath.c_str(), destinationPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return true;
    }

    outError = std::system_category().message(static_cast<int>(GetLastError()));
    return false;
#else
    std::error_code ec;
    std::filesystem::rename(sourcePath, destinationPath, ec);
    if (!ec)
    {
        return true;
    }

    outError = ec.message();
    return false;
#endif
}

} // namespace

bool loadSceneConfigFromFile(const std::filesystem::path& path, SceneConfig& outConfig,
                             SceneLoadDiagnostics* diagnostics,
                             SceneSerializerLogMode logMode)
{
    if (diagnostics != nullptr)
    {
        diagnostics->loadedLegacyVersion = false;
        diagnostics->errorMessage.clear();
    }

    if (!std::filesystem::exists(path))
    {
        assignError(diagnostics, "Scene file not found");
        logSceneSerializerMessage(
            LogSeverity::Warning, std::string("Scene file not found: ") + path.string(), logMode);
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        assignError(diagnostics, "Failed to open scene file");
        logSceneSerializerMessage(
            LogSeverity::Error, std::string("Failed to open scene file: ") + path.string(),
            logMode);
        return false;
    }

    try
    {
        json j = json::parse(file);
        uint32_t loadedVersion = kLegacySceneFormatVersion;
        if (!parseSceneFormatVersion(j, path, loadedVersion, diagnostics, logMode))
        {
            return false;
        }
        if (loadedVersion == kLegacySceneFormatVersion)
        {
            logSceneSerializerMessage(
                LogSeverity::Warning,
                std::string("Loading legacy versionless scene file: ") + path.string() +
                    " (will be upgraded to version " +
                    std::to_string(SceneConfig::kCurrentFormatVersion) + " on next save)",
                logMode);
        }

        SceneConfig loadedConfig = makeSceneDefaultsForVersion(loadedVersion);
        loadedConfig.version = SceneConfig::kCurrentFormatVersion;

        loadedConfig.loadTestFloor = getOr(j, "loadTestFloor", loadedConfig.loadTestFloor);
        loadedConfig.loadVoxelWorld = getOr(j, "loadVoxelWorld", loadedConfig.loadVoxelWorld);
        loadedConfig.loadOBBVolumes = getOr(j, "loadOBBVolumes", loadedConfig.loadOBBVolumes);
        loadedConfig.loadGlassPanel = getOr(j, "loadGlassPanel", loadedConfig.loadGlassPanel);
        loadedConfig.loadAquariumTest = getOr(j, "loadAquariumTest", loadedConfig.loadAquariumTest);
        loadedConfig.loadGlassTestScene =
            getOr(j, "loadGlassTestScene", loadedConfig.loadGlassTestScene);
        loadedConfig.loadProceduralWorld =
            getOr(j, "loadProceduralWorld", loadedConfig.loadProceduralWorld);
        loadedConfig.loadVoxelImport = getOr(j, "loadVoxelImport", loadedConfig.loadVoxelImport);
        loadedConfig.useMeshTankGlass =
            getOr(j, "useMeshTankGlass", loadedConfig.useMeshTankGlass);

        loadedConfig.enableWater = getOr(j, "enableWater", loadedConfig.enableWater);
        loadedConfig.useWaterV2 = getOr(j, "useWaterV2", loadedConfig.useWaterV2);
        loadedConfig.enableGlass = getOr(j, "enableGlass", loadedConfig.enableGlass);
        loadedConfig.enablePointLights =
            getOr(j, "enablePointLights", loadedConfig.enablePointLights);

        // environment wind became authored scene state in schema v8. older schemas
        // retain the compatibility default even if a future-looking key is present.
        if (loadedVersion >= 8)
        {
            loadedConfig.environmentWind = getEnvironmentWindOr(
                j, "environmentWind", loadedConfig.environmentWind);
        }
        // the environmental clock became authored scene state in schema v10.
        // earlier schemas stay exactly compatibility-off even when a future
        // environmentTime object is present.
        if (loadedVersion >= 10)
        {
            loadedConfig.environmentTime = getEnvironmentTimeOr(
                j, "environmentTime", loadedConfig.environmentTime);
        }
        // windborne particles became authored scene state in schema v9. earlier
        // schemas remain compatibility-off even if a future-looking key exists.
        if (loadedVersion >= 9)
        {
            loadedConfig.windborneParticles = getWindborneParticlesOr(
                j, "windborneParticles", loadedConfig.windborneParticles);
        }

        loadedConfig.loadCloudScene = getOr(j, "loadCloudScene", loadedConfig.loadCloudScene);
        loadedConfig.cloudSeed = getOr(j, "cloudSeed", loadedConfig.cloudSeed);
        loadedConfig.cloudAltitude = getOr(j, "cloudAltitude", loadedConfig.cloudAltitude);
        loadedConfig.cloudWindSpeed = getOr(j, "cloudWindSpeed", loadedConfig.cloudWindSpeed);
        loadedConfig.cloudWindDirection =
            getVec2Or(j, "cloudWindDirection", loadedConfig.cloudWindDirection);
        loadedConfig.cloudCoverage = getOr(j, "cloudCoverage", loadedConfig.cloudCoverage);
        loadedConfig.cloudScale = getOr(j, "cloudScale", loadedConfig.cloudScale);
        loadedConfig.cloudShadowStrength =
            getOr(j, "cloudShadowStrength", loadedConfig.cloudShadowStrength);

        loadedConfig.skyPreset = getOr(j, "skyPreset", loadedConfig.skyPreset);
        loadedConfig.skyColor = getVec3Or(j, "skyColor", loadedConfig.skyColor);

        loadedConfig.loadStarScene = getOr(j, "loadStarScene", loadedConfig.loadStarScene);
        loadedConfig.starSeed = getOr(j, "starSeed", loadedConfig.starSeed);
        loadedConfig.starAltitude = getOr(j, "starAltitude", loadedConfig.starAltitude);
        loadedConfig.starDensity = getOr(j, "starDensity", loadedConfig.starDensity);

        loadedConfig.cameraPosition = getVec3Or(j, "cameraPosition", loadedConfig.cameraPosition);
        loadedConfig.cameraYaw = getOr(j, "cameraYaw", loadedConfig.cameraYaw);
        loadedConfig.cameraPitch = getOr(j, "cameraPitch", loadedConfig.cameraPitch);

        loadedConfig.worldSeed = getOr(j, "worldSeed", loadedConfig.worldSeed);
        loadedConfig.worldDimsX = getOr(j, "worldDimsX", loadedConfig.worldDimsX);
        loadedConfig.worldDimsY = getOr(j, "worldDimsY", loadedConfig.worldDimsY);
        loadedConfig.worldDimsZ = getOr(j, "worldDimsZ", loadedConfig.worldDimsZ);
        loadedConfig.useDefaultPlaceables =
            getOr(j, "useDefaultPlaceables", loadedConfig.useDefaultPlaceables);
        loadedConfig.placeables = getPlaceablesOr(j, "placeables", loadedConfig.placeables);
        loadedConfig.gameState = getGameStateOr(j, "gameState", loadedConfig.gameState);
        loadedConfig.name = getOr(j, "name", loadedConfig.name);
        loadedConfig.description = getOr(j, "description", loadedConfig.description);
        validateSceneConfig(loadedConfig, path, logMode);
        loadedConfig.presentationProfile = getPresentationProfileOr(
            j, "presentationProfile", path,
            engine::scene::namedBaseScenePresentationProfile(loadedConfig), logMode);

        outConfig = std::move(loadedConfig);

        logSceneSerializerMessage(LogSeverity::Info,
                                  std::string("Loaded scene: ") + outConfig.name + " from " +
                                      path.string(),
                                  logMode);
        return true;
    }
    catch (const json::exception& e)
    {
        assignError(diagnostics, e.what());
        logSceneSerializerMessage(
            LogSeverity::Error, std::string("JSON parse error in scene file: ") + e.what(),
            logMode);
        return false;
    }
}

bool saveSceneConfigToFile(const std::filesystem::path& path, const SceneConfig& config,
                           std::string* outError, SceneSerializerLogMode logMode)
{
    if (outError != nullptr)
    {
        outError->clear();
    }

    json j;
    j["version"] = SceneConfig::kCurrentFormatVersion;

    j["loadTestFloor"] = config.loadTestFloor;
    j["loadVoxelWorld"] = config.loadVoxelWorld;
    j["loadOBBVolumes"] = config.loadOBBVolumes;
    j["loadGlassPanel"] = config.loadGlassPanel;
    j["loadAquariumTest"] = config.loadAquariumTest;
    j["loadGlassTestScene"] = config.loadGlassTestScene;
    j["loadProceduralWorld"] = config.loadProceduralWorld;
    j["loadVoxelImport"] = config.loadVoxelImport;
    j["useMeshTankGlass"] = config.useMeshTankGlass;

    j["enableWater"] = config.enableWater;
    j["useWaterV2"] = config.useWaterV2;
    j["enableGlass"] = config.enableGlass;
    j["enablePointLights"] = config.enablePointLights;

    const engine::scene::EnvironmentWindSettings environmentWind =
        engine::scene::sanitizeEnvironmentWindSettings(config.environmentWind);
    j["environmentWind"] = {
        {"direction", {environmentWind.direction.x, environmentWind.direction.y}},
        {"speed", environmentWind.speed},
        {"strength", environmentWind.strength},
        {"gustStrength", environmentWind.gustStrength},
        {"gustFrequencyHz", environmentWind.gustFrequencyHz},
        {"turbulenceStrength", environmentWind.turbulenceStrength},
        {"verticalLift", environmentWind.verticalLift},
    };

    const engine::scene::EnvironmentTimeSettings environmentTime =
        engine::scene::sanitizeEnvironmentTimeSettings(config.environmentTime);
    j["environmentTime"] = {
        {"enabled", environmentTime.enabled},
        {"cycleEnabled", environmentTime.cycleEnabled},
        {"timeOfDayHours", environmentTime.timeOfDayHours},
        {"dayLengthMinutes", environmentTime.dayLengthMinutes},
    };

    const engine::scene::WindborneParticleSettings windborneParticles =
        engine::scene::sanitizeWindborneParticleSettings(
            config.windborneParticles);
    j["windborneParticles"] = {
        {"enabled", windborneParticles.enabled},
        {"amount", windborneParticles.amount},
        {"leafFraction", windborneParticles.leafFraction},
        {"scale", windborneParticles.scale},
        {"visibilityDistance", windborneParticles.visibilityDistance},
    };

    j["loadCloudScene"] = config.loadCloudScene;
    j["cloudSeed"] = config.cloudSeed;
    j["cloudAltitude"] = config.cloudAltitude;
    j["cloudWindSpeed"] = config.cloudWindSpeed;
    j["cloudWindDirection"] = {config.cloudWindDirection.x, config.cloudWindDirection.y};
    j["cloudCoverage"] = config.cloudCoverage;
    j["cloudScale"] = config.cloudScale;
    j["cloudShadowStrength"] = config.cloudShadowStrength;

    j["skyPreset"] = config.skyPreset;
    j["skyColor"] = {config.skyColor.x, config.skyColor.y, config.skyColor.z};

    j["loadStarScene"] = config.loadStarScene;
    j["starSeed"] = config.starSeed;
    j["starAltitude"] = config.starAltitude;
    j["starDensity"] = config.starDensity;

    j["cameraPosition"] = {config.cameraPosition.x, config.cameraPosition.y, config.cameraPosition.z};
    j["cameraYaw"] = config.cameraYaw;
    j["cameraPitch"] = config.cameraPitch;

    j["worldSeed"] = config.worldSeed;
    j["worldDimsX"] = config.worldDimsX;
    j["worldDimsY"] = config.worldDimsY;
    j["worldDimsZ"] = config.worldDimsZ;
    j["useDefaultPlaceables"] = config.useDefaultPlaceables;

    j["placeables"] = json::array();
    for (const PlaceableInstance& instance : config.placeables)
    {
        json entry;
        entry["uuid"] = instance.uuid;
        entry["prototypeSlug"] = instance.prototypeSlug;
        entry["prototypeVersion"] = instance.prototypeVersion;
        entry["position"] = {instance.position.x, instance.position.y, instance.position.z};
        entry["rotation"] = {instance.rotation.w, instance.rotation.x, instance.rotation.y,
                             instance.rotation.z};
        entry["scale"] = {instance.scale.x, instance.scale.y, instance.scale.z};
        entry["seed"] = instance.seed;
        j["placeables"].push_back(std::move(entry));
    }

    json gameState;
    gameState["lastPlayedUtc"] = config.gameState.lastPlayedUtc;
    gameState["water"] = {
        {"temperatureC", config.gameState.water.temperatureC},
        {"oxygen", config.gameState.water.oxygen},
        {"flow", config.gameState.water.flow},
        {"cleanliness", config.gameState.water.cleanliness},
    };
    gameState["progression"] = {
        {"coins", config.gameState.progression.coins},
        {"discovery", config.gameState.progression.discovery},
        {"careMilestone", config.gameState.progression.careMilestone},
        {"tankTier", config.gameState.progression.tankTier},
    };
    gameState["creatures"] = json::array();
    for (const CreatureInstance& creature : config.gameState.creatures)
    {
        json entry;
        entry["uuid"] = creature.uuid;
        entry["speciesId"] = creature.speciesId;
        entry["displayName"] = creature.displayName;
        entry["primary"] = creature.primary;
        entry["bond"] = creature.bond;
        entry["vitality"] = creature.vitality;
        entry["needs"] = {
            {"hunger", creature.needs.hunger},
            {"cleanliness", creature.needs.cleanliness},
            {"happiness", creature.needs.happiness},
            {"health", creature.needs.health},
        };
        gameState["creatures"].push_back(std::move(entry));
    }
    j["gameState"] = std::move(gameState);

    if (config.presentationProfile.has_value())
    {
        j["presentationProfile"] = makePresentationProfileJson(*config.presentationProfile);
    }

    j["name"] = config.name;
    j["description"] = config.description;

    try
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }

        const std::filesystem::path tempPath = makeAtomicWriteTempPath(path);
        std::ofstream file(tempPath, std::ios::out | std::ios::trunc);
        if (!file.is_open())
        {
            if (outError != nullptr)
            {
                *outError = "Failed to create temporary scene file";
            }
            logSceneSerializerMessage(
                LogSeverity::Error,
                std::string("Failed to create temporary scene file: ") + tempPath.string(),
                logMode);
            return false;
        }

        file << j.dump(2);
        file.flush();
        if (!file)
        {
            file.close();
            std::error_code cleanupEc;
            std::filesystem::remove(tempPath, cleanupEc);
            if (outError != nullptr)
            {
                *outError = "Failed while writing temporary scene file";
            }
            logSceneSerializerMessage(
                LogSeverity::Error,
                std::string("Failed while writing temporary scene file: ") + tempPath.string(),
                logMode);
            return false;
        }
        file.close();

        std::string replaceError;
        if (!replaceFileAtomically(tempPath, path, replaceError))
        {
            std::error_code cleanupEc;
            std::filesystem::remove(tempPath, cleanupEc);
            if (outError != nullptr)
            {
                *outError = replaceError;
            }
            logSceneSerializerMessage(
                LogSeverity::Error,
                std::string("Failed to atomically replace scene file: ") + path.string() + " (" +
                    replaceError + ")",
                logMode);
            return false;
        }

        logSceneSerializerMessage(LogSeverity::Info,
                                  std::string("Saved scene to: ") + path.string(), logMode);
        return true;
    }
    catch (const std::exception& e)
    {
        if (outError != nullptr)
        {
            *outError = e.what();
        }
        logSceneSerializerMessage(LogSeverity::Error,
                                  std::string("Failed to save scene: ") + e.what(), logMode);
        return false;
    }
}
