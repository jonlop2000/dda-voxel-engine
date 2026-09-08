#pragma once

#include <cstdint>

#include "engine/voxel/VoxelTypes.h"

struct GLFWwindow;

class Input
{
public:
    void handleKey(int key, int action);
    void updateFromWindow(GLFWwindow* window);
    void handleMouseMove(double xpos, double ypos);
    void handleMouseButton(int button, int action);
    void releaseMouseLook();

    struct MouseDelta
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    int gbufferMode() const;
    int lightCount() const;
    bool heatmap() const;
    bool cascadeDebug() const;
    bool lightsEnabled() const;
    bool tonemapEnabled() const;
    bool bloomEnabled() const;
    bool waterEnabled() const;
    int waterDebugMode() const;
    bool glassEnabled() const;
    int glassDebugMode() const;
    int postDebugMode() const;
    float exposure() const;
    bool mouseCaptured() const;
    bool mouseLookActive() const;
    bool voxelWorldEnabled() const;
    bool chunkBoundsEnabled() const;
    bool volumeBoundsEnabled() const;
    bool voxelMeshingFrozen() const;
    bool editModeEnabled() const;
    BlockId selectedBlock() const;
    bool consumeBlockRemove();
    bool consumeBlockPlace();
    bool consumeResetCamera();
    bool consumeVoxelRegen();
    bool consumeMetricsCapture();
    bool consumeRenderShowcasePrevious();
    bool consumeRenderShowcaseNext();
    bool consumeRenderShowcasePauseToggle();
    MouseDelta consumeMouseDelta();
    void syncMousePosition(double xpos, double ypos);
    void setTonemapEnabled(bool enabled);
    void setExposure(float exposure);
    void setBloomEnabled(bool enabled);
    void setLightsEnabled(bool enabled);
    void setWaterEnabled(bool enabled);
    void setWaterDebugMode(int mode);
    void setGlassEnabled(bool enabled);
    void setGlassDebugMode(int mode);
    void setPostDebugMode(int mode);

    bool moveForward() const;
    bool moveBackward() const;
    bool moveLeft() const;
    bool moveRight() const;
    bool moveUp() const;
    bool moveDown() const;
    bool sprint() const;
    void setMaxLights(int maxLights);
    void setLightCount(int count);

private:
    void logDebugState() const;

    int gbufferMode_ = 0;
    int lightCount_ = 16;
    int maxLights_ = 0;
    bool heatmap_ = false;
    bool lightsEnabled_ = true;
    bool tonemapEnabled_ = true;
    bool bloomEnabled_ = true;
    bool waterEnabled_ = true;
    int waterDebugMode_ = 0;
    bool glassEnabled_ = true;
    int glassDebugMode_ = 0;
    int postDebugMode_ = 0;
    float exposure_ = 1.0f;
    bool cascadeDebug_ = false;
    bool mouseCaptured_ = false;
    bool mouseLookHeld_ = false;
    bool resetCameraRequested_ = false;
    bool voxelWorldEnabled_ = true;
    bool voxelRegenRequested_ = false;
    bool chunkBoundsEnabled_ = false;
    bool volumeBoundsEnabled_ = false;
    bool voxelMeshingFrozen_ = false;
    bool editMode_ = false;
    BlockId selectedBlock_ = BLOCK_DIRT;
    bool removeRequested_ = false;
    bool placeRequested_ = false;
    bool moveForward_ = false;
    bool moveBackward_ = false;
    bool moveLeft_ = false;
    bool moveRight_ = false;
    bool moveUp_ = false;
    bool moveDown_ = false;
    bool sprint_ = false;
    bool metricsCaptureRequested_ = false;
    bool renderShowcasePreviousRequested_ = false;
    bool renderShowcaseNextRequested_ = false;
    bool renderShowcasePauseToggleRequested_ = false;
    bool hasMouse_ = false;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    double mouseDeltaX_ = 0.0;
    double mouseDeltaY_ = 0.0;
};
