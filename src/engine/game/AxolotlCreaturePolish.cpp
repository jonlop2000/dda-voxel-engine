#include "engine/game/AxolotlCreaturePolish.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace engine::game
{

namespace
{
constexpr glm::vec3 kMouthBubbleLocal{16.15f, 4.35f, 5.0f};
constexpr std::array<glm::vec3, 3> kGillFlutterTips{{
    glm::vec3(14.25f, 7.25f, 0.0f),
    glm::vec3(12.45f, 7.10f, 0.0f),
    glm::vec3(10.65f, 6.85f, 0.0f),
}};

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep01(float value)
{
    const float t = clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

float hashToUnit(uint32_t value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return static_cast<float>(value & 0x00ffffffu) /
           static_cast<float>(0x01000000u);
}

glm::vec3 safeNormalizeOr(const glm::vec3& value, const glm::vec3& fallback)
{
    const float len2 = glm::dot(value, value);
    if (len2 <= 1e-10f)
    {
        return fallback;
    }
    return value * glm::inversesqrt(len2);
}

glm::vec3 transformPoint(const glm::mat4& transform, const glm::vec3& point)
{
    return glm::vec3(transform * glm::vec4(point, 1.0f));
}

glm::mat4 centeredBoxModel(const glm::vec3& center, const glm::vec3& size)
{
    return glm::translate(glm::mat4(1.0f), center) *
           glm::scale(glm::mat4(1.0f), size);
}

void appendLocalCube(const glm::mat4& bodyFromLocal, const glm::vec3& localCenter,
                     const glm::vec3& localSize, AxolotlPolishMaterial material,
                     std::vector<AxolotlPolishObject>& outObjects)
{
    AxolotlPolishObject object{};
    object.model = bodyFromLocal * glm::translate(glm::mat4(1.0f), localCenter) *
                   glm::scale(glm::mat4(1.0f), localSize);
    object.material = material;
    outObjects.push_back(object);
}
} // namespace

float axolotlFocusPoseBlend(float focusSeconds)
{
    return smoothstep01(std::max(0.0f, focusSeconds) / 0.55f);
}

AxolotlFocusPoseOffsets axolotlFocusPoseOffsets(float timeSeconds, float pathPhase,
                                                float wagPhase, float focusSeconds)
{
    const float blend = axolotlFocusPoseBlend(focusSeconds);
    return AxolotlFocusPoseOffsets{
        blend * (0.11f + 0.035f * std::sin(timeSeconds * 3.1f + pathPhase)),
        blend * 0.050f * std::sin(timeSeconds * 4.3f + wagPhase),
        blend * 0.075f * std::sin(timeSeconds * 2.6f + wagPhase),
    };
}

float axolotlToeWiggleRadians(float limbCycle, float limbIndex, float pathPhase,
                              float focusSeconds)
{
    const float blend = axolotlFocusPoseBlend(focusSeconds);
    return (0.035f + blend * 0.115f) *
           std::sin(limbCycle * 2.75f + limbIndex * 1.83f + pathPhase);
}

float axolotlPawLiftLocal(float timeSeconds, float limbIndex, float wagPhase,
                          float focusSeconds)
{
    const float blend = axolotlFocusPoseBlend(focusSeconds);
    return blend * 0.08f *
           std::sin(timeSeconds * 10.5f + limbIndex * 1.37f + wagPhase);
}

size_t axolotlCreaturePolishReserve(size_t axolotlCount)
{
    return axolotlCount * (kGillFlutterTips.size() * 4u + 6u);
}

void appendAxolotlCreaturePolishObjects(const AxolotlPolishPose& pose, float timeSeconds,
                                        std::vector<AxolotlPolishObject>& outObjects)
{
    const float focusBlend = pose.focused ? axolotlFocusPoseBlend(pose.focusSeconds) : 0.0f;

    for (int sideSign : {-1, 1})
    {
        const float side = static_cast<float>(sideSign);
        const float outerZ = sideSign < 0 ? -0.30f : 10.30f;
        const float stemZ = sideSign < 0 ? 0.30f : 9.70f;
        for (size_t tipIndex = 0; tipIndex < kGillFlutterTips.size(); ++tipIndex)
        {
            const float tip = static_cast<float>(tipIndex);
            const float wave = std::sin(timeSeconds * (3.75f + tip * 0.42f) +
                                        pose.wagPhase + side * 0.85f + tip * 0.77f);
            const float secondary =
                std::cos(timeSeconds * (4.35f + tip * 0.27f) + pose.pathPhase +
                         side * 0.56f);
            const float flutter = 0.20f + focusBlend * 0.24f;
            const float pulse = 1.0f + 0.10f * wave + 0.08f * focusBlend * secondary;

            glm::vec3 stemLocal = kGillFlutterTips[tipIndex];
            stemLocal.y -= 0.54f + tip * 0.05f;
            stemLocal.z = stemZ + side * wave * flutter * 0.36f;
            appendLocalCube(pose.bodyFromLocal, stemLocal, glm::vec3(0.52f, 0.44f, 0.36f),
                            AxolotlPolishMaterial::GillStem, outObjects);

            glm::vec3 tipLocal = kGillFlutterTips[tipIndex];
            tipLocal.y += secondary * 0.11f;
            tipLocal.z = outerZ + side * wave * flutter;
            appendLocalCube(pose.bodyFromLocal, tipLocal,
                            glm::vec3(0.62f, 0.50f, 0.44f) * pulse,
                            AxolotlPolishMaterial::GillTip, outObjects);
        }
    }

    if (!pose.focused || pose.focusSeconds <= 0.04f)
    {
        return;
    }

    const glm::vec3 mouthWorld = transformPoint(pose.bodyFromLocal, kMouthBubbleLocal);
    const glm::vec3 forwardWorld = safeNormalizeOr(
        transformPoint(pose.bodyFromLocal, kMouthBubbleLocal + glm::vec3(1.0f, 0.0f, 0.0f)) -
            mouthWorld,
        glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 sideWorld = safeNormalizeOr(
        transformPoint(pose.bodyFromLocal, kMouthBubbleLocal + glm::vec3(0.0f, 0.0f, 1.0f)) -
            mouthWorld,
        glm::vec3(0.0f, 0.0f, 1.0f));

    for (uint32_t i = 0; i < 6; ++i)
    {
        const float seed = hashToUnit(i * 747796405u + 102071u);
        const float cycle = timeSeconds * (0.58f + seed * 0.12f) + i * 0.173f;
        const float phase = cycle - std::floor(cycle);
        const float rise = smoothstep01(phase);
        const float wobble =
            std::sin(timeSeconds * (2.05f + seed) + pose.wagPhase + i * 1.71f);
        const glm::vec3 center =
            mouthWorld + forwardWorld * pose.scale * (0.42f + phase * 2.05f) +
            sideWorld * pose.scale * wobble * (0.30f + focusBlend * 0.18f) *
                (1.0f - phase * 0.22f) +
            glm::vec3(0.0f, pose.scale * (0.20f + rise * 2.35f), 0.0f);
        const float cubeScale =
            pose.scale * (0.13f + phase * 0.16f) * (0.70f + focusBlend * 0.30f);

        AxolotlPolishObject object{};
        object.model = centeredBoxModel(center, glm::vec3(cubeScale));
        object.material = AxolotlPolishMaterial::Bubble;
        outObjects.push_back(object);
    }
}

} // namespace engine::game
