#pragma once

#include <cstddef>
#include <vector>

#include <glm/mat4x4.hpp>

namespace engine::game
{

enum class AxolotlPolishMaterial
{
    GillStem,
    GillTip,
    Bubble,
};

struct AxolotlFocusPoseOffsets
{
    float rollRadians = 0.0f;
    float pitchRadians = 0.0f;
    float bodyYawRadians = 0.0f;
};

struct AxolotlPolishPose
{
    glm::mat4 bodyFromLocal{1.0f};
    float scale = 1.0f;
    float pathPhase = 0.0f;
    float wagPhase = 0.0f;
    bool focused = false;
    float focusSeconds = 0.0f;
};

struct AxolotlPolishObject
{
    glm::mat4 model{1.0f};
    AxolotlPolishMaterial material = AxolotlPolishMaterial::GillTip;
};

float axolotlFocusPoseBlend(float focusSeconds);
AxolotlFocusPoseOffsets axolotlFocusPoseOffsets(float timeSeconds, float pathPhase,
                                                float wagPhase, float focusSeconds);
float axolotlToeWiggleRadians(float limbCycle, float limbIndex, float pathPhase,
                              float focusSeconds);
float axolotlPawLiftLocal(float timeSeconds, float limbIndex, float wagPhase,
                          float focusSeconds);

size_t axolotlCreaturePolishReserve(size_t axolotlCount);
void appendAxolotlCreaturePolishObjects(const AxolotlPolishPose& pose, float timeSeconds,
                                        std::vector<AxolotlPolishObject>& outObjects);

} // namespace engine::game
