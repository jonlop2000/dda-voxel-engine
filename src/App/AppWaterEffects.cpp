#include "App/App.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "App/AppSceneVolume.h"
#include "engine/scene/SunroofLivingWater.h"
#include "engine/voxel/WaterVolume.h"

namespace
{
constexpr float kTau = 6.28318530718f;

float hashToUnit(uint32_t value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return static_cast<float>(value & 0x00ffffffu) /
           static_cast<float>(0x00ffffffu);
}
} // namespace

void App::rebuildWaterFoamObjects(float timeSeconds)
{
    const size_t previousCount = waterFoamObjects_.size();
    waterFoamObjects_.clear();

    const bool drawReady =
        waterFoamMaterial_.set != VK_NULL_HANDLE && cubeMesh_.vbo != VK_NULL_HANDLE;
    const bool sunroofLivingWaterScene =
        isSunroofAquariumScene(sceneConfig()) && waterSettings_.useWaterV2_ &&
        input_.waterEnabled() && drawReady && !waterVolumeMgr_.volumes().empty();
    if (sunroofLivingWaterScene)
    {
        const engine::render::SunroofLivingWaterVfxSettings& livingWater =
            waterSettings_.sunroofLivingWaterVfx_;
        if (livingWater.enabled && livingWater.bubbleStreamsEnabled)
        {
            const engine::WaterVolume& volume = waterVolumeMgr_.volumes().front();
            const engine::scene::SunroofBubbleControls bubbleControls{
                livingWater.bubbleAmount, livingWater.bubbleSize,
                livingWater.bubbleRiseSpeed, livingWater.bubbleDrift};
            const std::vector<engine::scene::SunroofBubbleMote> bubbles =
                engine::scene::buildSunroofBubbleMotes(
                    volume.boundsMin, volume.boundsMax, volume.surfaceHeight,
                    timeSeconds, bubbleControls);
            waterFoamObjects_.reserve(bubbles.size());
            for (const engine::scene::SunroofBubbleMote& bubble : bubbles)
            {
                RenderObject renderObject{};
                renderObject.model =
                    glm::translate(glm::mat4(1.0f), bubble.center) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(bubble.scale));
                renderObject.mesh = &cubeMesh_;
                renderObject.material = &waterFoamMaterial_;
                waterFoamObjects_.push_back(renderObject);
            }
        }
        appendAxolotlCreaturePolishObjects(timeSeconds);
        // the overlay remains bounded at 27 bubble cubes. amount can only select
        // a deterministic subset, so the thermal ceiling stays camera-independent.
        sceneObjectsDirty_ = true;
        return;
    }

    const bool enabled = waterSettings_.useWaterV2_ && input_.waterEnabled() &&
                         waterSettings_.waterFoamEmitterEnabled_ &&
                         waterSettings_.waterFoamEmitterIntensity_ > 0.01f &&
                         drawReady && !waterVolumeMgr_.volumes().empty();
    if (!enabled)
    {
        appendAxolotlCreaturePolishObjects(timeSeconds);
        if (previousCount > 0 || !waterFoamObjects_.empty())
        {
            sceneObjectsDirty_ = true;
        }
        return;
    }

    const engine::WaterVolume& volume = waterVolumeMgr_.volumes().front();
    const glm::vec3 boundsMin = volume.boundsMin;
    const glm::vec3 boundsMax = volume.boundsMax;
    const float surfaceHeight = volume.surfaceHeight;
    if (boundsMax.x <= boundsMin.x || boundsMax.z <= boundsMin.z ||
        surfaceHeight <= boundsMin.y + 0.10f)
    {
        appendAxolotlCreaturePolishObjects(timeSeconds);
        if (previousCount > 0 || !waterFoamObjects_.empty())
        {
            sceneObjectsDirty_ = true;
        }
        return;
    }

    const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
    const glm::vec3 halfExtent = (boundsMax - boundsMin) * 0.5f;
    const glm::vec2 emitterCenter(
        center.x + waterSettings_.waterFoamEmitterOffsetX_ * halfExtent.x,
        center.z + waterSettings_.waterFoamEmitterOffsetZ_ * halfExtent.z);
    const float intensity =
        std::clamp(waterSettings_.waterFoamEmitterIntensity_, 0.0f, 1.0f);
    const float radius =
        std::clamp(waterSettings_.waterFoamEmitterRadius_, 0.25f, 12.0f);
    const float scaleControl =
        std::clamp(waterSettings_.waterFoamEmitterScale_, 0.0f, 1.0f);
    const float spread =
        std::clamp(waterSettings_.waterFoamEmitterSpread_, 0.0f, 1.0f);
    const float waterDepth = std::max(0.05f, surfaceHeight - boundsMin.y);
    const float verticalSpan =
        std::min(waterDepth - 0.05f, 0.55f + spread * 2.10f + radius * 0.08f);
    const uint32_t candidateCount = static_cast<uint32_t>(std::clamp(
        26.0f + intensity * 102.0f + std::min(radius, 8.0f) * 5.0f,
        16.0f, 168.0f));

    waterFoamObjects_.reserve(candidateCount);
    for (uint32_t i = 0; i < candidateCount; ++i)
    {
        const float r0 = hashToUnit(i * 747796405u + 2891336453u);
        const float r1 = hashToUnit(i * 1597334677u + 3812015801u);
        const float r2 = hashToUnit(i * 1247477963u + 917711u);
        const float r3 = hashToUnit(i * 2654435761u + 1013904223u);
        const float r4 = hashToUnit(i * 2246822519u + 3266489917u);
        const float r5 = hashToUnit(i * 668265263u + 1442695041u);
        const float r6 = hashToUnit(i * 374761393u + 31337u);

        const float angle = r0 * kTau +
                            timeSeconds * (0.030f + spread * 0.055f) *
                                (0.45f + r4 * 0.75f);
        const float coreChance = std::lerp(0.86f, 0.68f, spread);
        const bool coreParticle = r6 < coreChance;
        const float coreRadial =
            radius * (0.03f + std::pow(r1, 2.85f) * 0.34f);
        const float haloT = std::pow(r1, 1.10f);
        const float haloRadial = radius * (0.34f + haloT * 0.60f);
        const float haloKeep =
            std::lerp(0.30f, 0.55f, intensity) * (1.0f - haloT * 0.58f);
        if (!coreParticle && r5 > haloKeep)
        {
            continue;
        }
        const float radial = coreParticle ? coreRadial : haloRadial;
        const float radialNorm = std::clamp(radial / radius, 0.0f, 1.0f);
        const float depthBias = coreParticle ? 1.35f : 2.40f;
        const float depth =
            0.08f + std::pow(r2, depthBias) * verticalSpan *
                        (coreParticle ? 0.78f : 0.62f);
        const float bob =
            std::sin(timeSeconds * (0.65f + spread * 1.15f) + r3 * kTau) *
            (0.035f + spread * 0.080f);
        const glm::vec2 swirl(std::cos(angle), std::sin(angle));
        const glm::vec2 drift(std::sin(timeSeconds * 0.31f + r5 * kTau),
                              std::cos(timeSeconds * 0.27f + r4 * kTau));
        const float driftScale = coreParticle ? 0.020f : 0.045f;

        glm::vec3 position(
            emitterCenter.x + swirl.x * radial +
                drift.x * radius * spread * driftScale,
            surfaceHeight - depth + bob,
            emitterCenter.y + swirl.y * radial +
                drift.y * radius * spread * driftScale);
        const float outerScaleFade = std::lerp(1.0f, 0.42f, radialNorm);
        const float cubeScale =
            (0.08f + scaleControl * 0.26f) * (0.65f + r5 * 0.85f) *
            (coreParticle ? 1.0f : outerScaleFade);
        const float halfScale = cubeScale * 0.5f;
        position.x = std::clamp(position.x, boundsMin.x + halfScale,
                                boundsMax.x - halfScale);
        position.y = std::clamp(position.y, boundsMin.y + halfScale,
                                surfaceHeight - halfScale * 1.25f);
        position.z = std::clamp(position.z, boundsMin.z + halfScale,
                                boundsMax.z - halfScale);

        RenderObject foam{};
        foam.model = glm::translate(glm::mat4(1.0f), position) *
                     glm::scale(glm::mat4(1.0f), glm::vec3(cubeScale));
        foam.mesh = &cubeMesh_;
        foam.material = &waterFoamMaterial_;
        waterFoamObjects_.push_back(foam);
    }

    appendAxolotlCreaturePolishObjects(timeSeconds);
    if (waterFoamObjects_.size() != previousCount || !waterFoamObjects_.empty())
    {
        sceneObjectsDirty_ = true;
    }
}
