#include "App/Input.h"

#include <algorithm>
#include <array>
#include <iterator>

#include "Core/Logger.h"
#include <GLFW/glfw3.h>

namespace
{
constexpr float kExposureStep = 0.1f;
constexpr float kExposureMin = 0.05f;
constexpr float kExposureMax = 8.0f;
constexpr int kWaterDebugModes = 24;
constexpr int kGlassDebugModes = 10;
constexpr int kPostDebugModes = 8;
}

void Input::handleKey(int key, int action)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
    {
        return;
    }

    bool changed = false;

    if (action == GLFW_PRESS && key == GLFW_KEY_LEFT)
    {
        renderShowcasePreviousRequested_ = true;
    }
    else if (action == GLFW_PRESS && key == GLFW_KEY_RIGHT)
    {
        renderShowcaseNextRequested_ = true;
    }
    else if (action == GLFW_PRESS && key == GLFW_KEY_SPACE)
    {
        renderShowcasePauseToggleRequested_ = true;
    }
    else if (editMode_ && key >= GLFW_KEY_1 && key <= GLFW_KEY_6)
    {
        const int idx = key - GLFW_KEY_1;
        static const BlockId kEditBlocks[] = {BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE,
                                              BLOCK_LOG, BLOCK_LEAF, BLOCK_WATER};
        if (idx >= 0 && idx < static_cast<int>(std::size(kEditBlocks)))
        {
            selectedBlock_ = kEditBlocks[idx];
            changed = true;
        }
    }
    else if (key == GLFW_KEY_0)
    {
        changed = gbufferMode_ != 0;
        gbufferMode_ = 0;
    }
    else if (key == GLFW_KEY_1)
    {
        changed = gbufferMode_ != 1;
        gbufferMode_ = 1;
    }
    else if (key == GLFW_KEY_2)
    {
        changed = gbufferMode_ != 2;
        gbufferMode_ = 2;
    }
    else if (key == GLFW_KEY_3)
    {
        changed = gbufferMode_ != 3;
        gbufferMode_ = 3;
    }
    else if (key == GLFW_KEY_4)
    {
        changed = gbufferMode_ != 4;
        gbufferMode_ = 4;
    }
    else if (key == GLFW_KEY_5)
    {
        changed = gbufferMode_ != 5;
        gbufferMode_ = 5;
    }
    else if (key == GLFW_KEY_6)
    {
        changed = gbufferMode_ != 6;
        gbufferMode_ = 6;
    }
    else if (key == GLFW_KEY_7)
    {
        changed = gbufferMode_ != 7;
        gbufferMode_ = 7;
    }
    else if (key == GLFW_KEY_LEFT_BRACKET)
    {
        const int prev = lightCount_;
        setLightCount(lightCount_ - 1);
        changed = lightCount_ != prev;
    }
    else if (key == GLFW_KEY_RIGHT_BRACKET)
    {
        const int prev = lightCount_;
        setLightCount(lightCount_ + 1);
        changed = lightCount_ != prev;
    }
    else if (key == GLFW_KEY_H)
    {
        heatmap_ = !heatmap_;
        if (heatmap_)
        {
            cascadeDebug_ = false;
        }
        changed = true;
    }
    else if (key == GLFW_KEY_C)
    {
        cascadeDebug_ = !cascadeDebug_;
        if (cascadeDebug_)
        {
            heatmap_ = false;
        }
        changed = true;
    }
    else if (key == GLFW_KEY_L)
    {
        lightsEnabled_ = !lightsEnabled_;
        changed = true;
    }
    else if (key == GLFW_KEY_T)
    {
        tonemapEnabled_ = !tonemapEnabled_;
        changed = true;
    }
    else if (key == GLFW_KEY_B)
    {
        bloomEnabled_ = !bloomEnabled_;
        changed = true;
    }
    else if (key == GLFW_KEY_MINUS)
    {
        const float prev = exposure_;
        setExposure(exposure_ - kExposureStep);
        changed = exposure_ != prev;
    }
    else if (key == GLFW_KEY_EQUAL)
    {
        const float prev = exposure_;
        setExposure(exposure_ + kExposureStep);
        changed = exposure_ != prev;
    }
    else if (key == GLFW_KEY_M)
    {
        if (action == GLFW_PRESS)
        {
            mouseCaptured_ = !mouseCaptured_;
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F1)
    {
        if (action == GLFW_PRESS)
        {
            voxelWorldEnabled_ = !voxelWorldEnabled_;
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F2)
    {
        if (action == GLFW_PRESS)
        {
            voxelRegenRequested_ = true;
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F3)
    {
        if (action == GLFW_PRESS)
        {
            chunkBoundsEnabled_ = !chunkBoundsEnabled_;
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F12)
    {
        if (action == GLFW_PRESS)
        {
            volumeBoundsEnabled_ = !volumeBoundsEnabled_;
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F4)
    {
        if (action == GLFW_PRESS)
        {
            voxelMeshingFrozen_ = !voxelMeshingFrozen_;
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F5)
    {
        if (action == GLFW_PRESS)
        {
            editMode_ = !editMode_;
            if (editMode_)
            {
                mouseLookHeld_ = false;
            }
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F6)
    {
        if (action == GLFW_PRESS)
        {
            waterEnabled_ = !waterEnabled_;
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F7)
    {
        if (action == GLFW_PRESS)
        {
            waterDebugMode_ = (waterDebugMode_ + 1) % kWaterDebugModes;
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F8)
    {
        if (action == GLFW_PRESS)
        {
            glassEnabled_ = !glassEnabled_;
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F9)
    {
        if (action == GLFW_PRESS)
        {
            glassDebugMode_ = (glassDebugMode_ + 1) % kGlassDebugModes;
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F10)
    {
        if (action == GLFW_PRESS)
        {
            postDebugMode_ = (postDebugMode_ + 1) % kPostDebugModes;
            changed = true;
        }
    }
    else if (key == GLFW_KEY_F11)
    {
        if (action == GLFW_PRESS)
        {
            metricsCaptureRequested_ = true;
        }
    }
    else if (key == GLFW_KEY_R)
    {
        if (action == GLFW_PRESS)
        {
            resetCameraRequested_ = true;
            logDebug("Input", "Camera reset requested.");
        }
    }

    if (changed)
    {
        logDebugState();
    }
}

void Input::updateFromWindow(GLFWwindow* window)
{
    if (window == nullptr)
    {
        return;
    }

    moveForward_ = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    moveBackward_ = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    moveLeft_ = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    moveRight_ = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    moveDown_ = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
    moveUp_ = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    sprint_ = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
              glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
}

void Input::handleMouseMove(double xpos, double ypos)
{
    if (!hasMouse_)
    {
        lastMouseX_ = xpos;
        lastMouseY_ = ypos;
        hasMouse_ = true;
        return;
    }

    const double dx = xpos - lastMouseX_;
    const double dy = ypos - lastMouseY_;
    lastMouseX_ = xpos;
    lastMouseY_ = ypos;

    if (mouseLookActive())
    {
        mouseDeltaX_ += dx;
        mouseDeltaY_ += dy;
    }
}

void Input::handleMouseButton(int button, int action)
{
    if (editMode_ && action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT)
    {
        removeRequested_ = true;
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        if (editMode_)
        {
            if (action == GLFW_PRESS)
            {
                placeRequested_ = true;
            }
        }
        else
        {
            mouseLookHeld_ = (action == GLFW_PRESS);
        }
    }
}

int Input::gbufferMode() const
{
    return gbufferMode_;
}

int Input::lightCount() const
{
    return lightCount_;
}

bool Input::heatmap() const
{
    return heatmap_;
}

bool Input::cascadeDebug() const
{
    return cascadeDebug_;
}

bool Input::lightsEnabled() const
{
    return lightsEnabled_;
}

bool Input::tonemapEnabled() const
{
    return tonemapEnabled_;
}

bool Input::bloomEnabled() const
{
    return bloomEnabled_;
}

bool Input::waterEnabled() const
{
    return waterEnabled_;
}

int Input::waterDebugMode() const
{
    return waterDebugMode_;
}

bool Input::glassEnabled() const
{
    return glassEnabled_;
}

int Input::glassDebugMode() const
{
    return glassDebugMode_;
}

int Input::postDebugMode() const
{
    return postDebugMode_;
}

float Input::exposure() const
{
    return exposure_;
}

bool Input::mouseCaptured() const
{
    return mouseCaptured_;
}

bool Input::mouseLookActive() const
{
    return mouseCaptured_ || mouseLookHeld_;
}

bool Input::voxelWorldEnabled() const
{
    return voxelWorldEnabled_;
}

bool Input::chunkBoundsEnabled() const
{
    return chunkBoundsEnabled_;
}

bool Input::volumeBoundsEnabled() const
{
    return volumeBoundsEnabled_;
}

bool Input::voxelMeshingFrozen() const
{
    return voxelMeshingFrozen_;
}

bool Input::editModeEnabled() const
{
    return editMode_;
}

BlockId Input::selectedBlock() const
{
    return selectedBlock_;
}

bool Input::consumeBlockRemove()
{
    const bool requested = removeRequested_;
    removeRequested_ = false;
    return requested;
}

bool Input::consumeBlockPlace()
{
    const bool requested = placeRequested_;
    placeRequested_ = false;
    return requested;
}

void Input::releaseMouseLook()
{
    mouseCaptured_ = false;
    mouseLookHeld_ = false;
    hasMouse_ = false;
    mouseDeltaX_ = 0.0;
    mouseDeltaY_ = 0.0;
}

bool Input::consumeResetCamera()
{
    const bool requested = resetCameraRequested_;
    resetCameraRequested_ = false;
    return requested;
}

bool Input::consumeVoxelRegen()
{
    const bool requested = voxelRegenRequested_;
    voxelRegenRequested_ = false;
    return requested;
}

bool Input::consumeMetricsCapture()
{
    const bool requested = metricsCaptureRequested_;
    metricsCaptureRequested_ = false;
    return requested;
}

bool Input::consumeRenderShowcasePrevious()
{
    const bool requested = renderShowcasePreviousRequested_;
    renderShowcasePreviousRequested_ = false;
    return requested;
}

bool Input::consumeRenderShowcaseNext()
{
    const bool requested = renderShowcaseNextRequested_;
    renderShowcaseNextRequested_ = false;
    return requested;
}

bool Input::consumeRenderShowcasePauseToggle()
{
    const bool requested = renderShowcasePauseToggleRequested_;
    renderShowcasePauseToggleRequested_ = false;
    return requested;
}

Input::MouseDelta Input::consumeMouseDelta()
{
    MouseDelta delta{};
    delta.x = static_cast<float>(mouseDeltaX_);
    delta.y = static_cast<float>(mouseDeltaY_);
    mouseDeltaX_ = 0.0;
    mouseDeltaY_ = 0.0;
    return delta;
}

void Input::syncMousePosition(double xpos, double ypos)
{
    lastMouseX_ = xpos;
    lastMouseY_ = ypos;
    mouseDeltaX_ = 0.0;
    mouseDeltaY_ = 0.0;
    hasMouse_ = true;
}

bool Input::moveForward() const
{
    return moveForward_;
}

bool Input::moveBackward() const
{
    return moveBackward_;
}

bool Input::moveLeft() const
{
    return moveLeft_;
}

bool Input::moveRight() const
{
    return moveRight_;
}

bool Input::moveUp() const
{
    return moveUp_;
}

bool Input::moveDown() const
{
    return moveDown_;
}

bool Input::sprint() const
{
    return sprint_;
}

void Input::setMaxLights(int maxLights)
{
    maxLights_ = std::max(0, maxLights);
    if (lightCount_ > maxLights_)
    {
        lightCount_ = maxLights_;
    }
}

void Input::setLightCount(int count)
{
    if (maxLights_ <= 0)
    {
        lightCount_ = 0;
        return;
    }
    lightCount_ = std::clamp(count, 0, maxLights_);
}

void Input::setTonemapEnabled(bool enabled)
{
    if (tonemapEnabled_ == enabled)
    {
        return;
    }
    tonemapEnabled_ = enabled;
    logDebugState();
}

void Input::setExposure(float exposure)
{
    exposure_ = std::clamp(exposure, kExposureMin, kExposureMax);
}

void Input::setBloomEnabled(bool enabled)
{
    if (bloomEnabled_ == enabled)
    {
        return;
    }
    bloomEnabled_ = enabled;
    logDebugState();
}

void Input::setLightsEnabled(bool enabled)
{
    if (lightsEnabled_ == enabled)
    {
        return;
    }
    lightsEnabled_ = enabled;
    logDebugState();
}

void Input::setWaterEnabled(bool enabled)
{
    if (waterEnabled_ == enabled)
    {
        return;
    }
    waterEnabled_ = enabled;
    logDebugState();
}

void Input::setWaterDebugMode(int mode)
{
    const int clamped = std::clamp(mode, 0, kWaterDebugModes - 1);
    if (waterDebugMode_ == clamped)
    {
        return;
    }
    waterDebugMode_ = clamped;
    logDebugState();
}

void Input::setGlassEnabled(bool enabled)
{
    if (glassEnabled_ == enabled)
    {
        return;
    }
    glassEnabled_ = enabled;
    logDebugState();
}

void Input::setGlassDebugMode(int mode)
{
    const int clamped = std::clamp(mode, 0, kGlassDebugModes - 1);
    if (glassDebugMode_ == clamped)
    {
        return;
    }
    glassDebugMode_ = clamped;
    logDebugState();
}

void Input::setPostDebugMode(int mode)
{
    const int clamped = std::clamp(mode, 0, kPostDebugModes - 1);
    if (postDebugMode_ == clamped)
    {
        return;
    }
    postDebugMode_ = clamped;
    logDebugState();
}

void Input::logDebugState() const
{
    logDebug("Input",
             makeLogMessage("ViewMode=", gbufferMode_,
                            " LightsEnabled=", (lightsEnabled_ ? "On" : "Off"),
                            " LightCount=", lightCount_,
                            " Heatmap=", (heatmap_ ? "On" : "Off"),
                            " CascadeDebug=", (cascadeDebug_ ? "On" : "Off"),
                            " VoxelWorld=", (voxelWorldEnabled_ ? "On" : "Off"),
                            " ChunkBounds=", (chunkBoundsEnabled_ ? "On" : "Off"),
                            " VolumeBounds=", (volumeBoundsEnabled_ ? "On" : "Off"),
                            " FreezeMesh=", (voxelMeshingFrozen_ ? "On" : "Off"),
                            " EditMode=", (editMode_ ? "On" : "Off"),
                            " Block=", selectedBlock_,
                            " Tonemap=", (tonemapEnabled_ ? "On" : "Off"),
                            " Bloom=", (bloomEnabled_ ? "On" : "Off"),
                            " Water=", (waterEnabled_ ? "On" : "Off"),
                            " WaterDebug=", waterDebugMode_,
                            " Glass=", (glassEnabled_ ? "On" : "Off"),
                            " GlassDebug=", glassDebugMode_,
                            " PostDebug=", postDebugMode_,
                            " Exposure=", exposure_,
                            " MouseCapture=", (mouseCaptured_ ? "On" : "Off")));
}
