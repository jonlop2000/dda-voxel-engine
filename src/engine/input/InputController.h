#pragma once

#include <cstdint>

#include "App/Input.h"
#include "App/InputRouter.h"
#include "engine/input/WindowCoordinates.h"
#if VOXEL_WITH_RUNTIME_UI
#include "UI/Runtime/UiElement.h"
#endif

struct GLFWwindow;

namespace engine::input
{

class InputController
{
public:
    class Delegate
    {
    public:
        virtual ~Delegate() = default;

        virtual void onInputFramebufferResized() = 0;
        virtual void onInputUserInteraction(double timeSeconds) = 0;
        virtual void enterRuntimeUiPlayPreview(const char* reason, bool startAtMainMenu) = 0;
        virtual void exitRuntimeUiPlayPreview(const char* reason) = 0;
        virtual bool releaseFishFocus() = 0;
        virtual bool toggleRuntimeUiPauseScreen() = 0;

#if VOXEL_WITH_EDITOR
        virtual bool handleEditorWorldPointerPress(double x, double y,
                                                   int mods) = 0;
#endif

#if VOXEL_WITH_RUNTIME_UI
        virtual ui::UiHitResult refreshRuntimeUiPointerCapture(double x, double y) = 0;
        virtual bool handleRuntimeUiScroll(double x, double y, double yOffset) = 0;
        virtual bool cancelRuntimeUiPendingPlacement(const char* source,
                                                     const char* automationScript,
                                                     uint64_t automationFrame) = 0;
        virtual bool rotateRuntimeUiPendingPlacement(const char* source,
                                                     const char* automationScript,
                                                     uint64_t automationFrame) = 0;
        virtual bool confirmRuntimeUiPendingPlacement(const char* source,
                                                      const char* automationScript,
                                                      uint64_t automationFrame) = 0;
        virtual void handleRuntimeUiPointerDown(const ui::UiHitResult& hit) = 0;
        virtual void handleRuntimeUiPointerUp(const ui::UiHitResult& hit) = 0;
#endif
    };

    void attach(GLFWwindow* window, Delegate& delegate);
    void detach();
    void updateMouseCapture();

    Input& input() { return input_; }
    const Input& input() const { return input_; }
    InputRouter& router() { return router_; }
    const InputRouter& router() const { return router_; }
    [[nodiscard]] FramebufferPoint framebufferPoint(double x, double y,
                                                     uint32_t framebufferWidth,
                                                     uint32_t framebufferHeight) const;
    bool handleReservedKey(int key, int action, int mods = 0);

private:
    void noteUserInteraction();

    static InputController* fromWindow(GLFWwindow* window);
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void windowFocusCallback(GLFWwindow* window, int focused);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    GLFWwindow* window_ = nullptr;
    Delegate* delegate_ = nullptr;
    Input input_{};
    InputRouter router_{};
    AppMode captureAppMode_ = AppMode::Game;
};

} // namespace engine::input
