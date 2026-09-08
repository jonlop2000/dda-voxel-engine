#include "engine/input/InputController.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

struct Delegate final : engine::input::InputController::Delegate
{
    InputRouter* router = nullptr;
    int pointerRefreshes = 0;
    int worldPresses = 0;
    int scrolls = 0;
    int pauses = 0;
    void onInputFramebufferResized() override {}
    void onInputUserInteraction(double) override {}
    void enterRuntimeUiPlayPreview(const char*, bool) override
    {
        router->setAppMode(AppMode::PlayPreview);
    }
    void exitRuntimeUiPlayPreview(const char*) override { router->setAppMode(AppMode::Editor); }
    bool releaseFishFocus() override { return false; }
    bool toggleRuntimeUiPauseScreen() override { ++pauses; return true; }
#if VOXEL_WITH_EDITOR
    bool handleEditorWorldPointerPress(double, double, int) override
    {
        ++worldPresses;
        return true;
    }
#endif
#if VOXEL_WITH_RUNTIME_UI
    ui::UiHitResult refreshRuntimeUiPointerCapture(double, double) override
    {
        ++pointerRefreshes;
        return {};
    }
    bool handleRuntimeUiScroll(double, double, double) override { ++scrolls; return true; }
    bool cancelRuntimeUiPendingPlacement(const char*, const char*, uint64_t) override { return false; }
    bool rotateRuntimeUiPendingPlacement(const char*, const char*, uint64_t) override { return false; }
    bool confirmRuntimeUiPendingPlacement(const char*, const char*, uint64_t) override { return false; }
    void handleRuntimeUiPointerDown(const ui::UiHitResult&) override {}
    void handleRuntimeUiPointerUp(const ui::UiHitResult&) override {}
#endif
};

struct Fixture
{
    GLFWwindow* window = nullptr;
    engine::input::InputController controller;
    Delegate delegate;
    GLFWmousebuttonfun buttonCallback = nullptr;
    GLFWcursorposfun moveCallback = nullptr;
    GLFWkeyfun keyCallback = nullptr;
    GLFWscrollfun scrollCallback = nullptr;

    Fixture()
    {
        window = glfwCreateWindow(1280, 720, "headless input contract", nullptr, nullptr);
        require(window != nullptr, "Null-platform window creation failed");
        delegate.router = &controller.router();
        controller.attach(window, delegate);
        buttonCallback = glfwSetMouseButtonCallback(window, nullptr);
        glfwSetMouseButtonCallback(window, buttonCallback);
        moveCallback = glfwSetCursorPosCallback(window, nullptr);
        glfwSetCursorPosCallback(window, moveCallback);
        keyCallback = glfwSetKeyCallback(window, nullptr);
        glfwSetKeyCallback(window, keyCallback);
        scrollCallback = glfwSetScrollCallback(window, nullptr);
        glfwSetScrollCallback(window, scrollCallback);
        move(640.0, 360.0);
    }
    ~Fixture()
    {
        controller.detach();
        glfwDestroyWindow(window);
    }
    void button(int action) { buttonCallback(window, GLFW_MOUSE_BUTTON_RIGHT, action, 0); }
    void key(int key, int action = GLFW_PRESS) { keyCallback(window, key, 0, action, 0); }
    void move(double x, double y)
    {
        glfwSetCursorPos(window, x, y);
        moveCallback(window, x, y);
    }
    void expectCapture(bool active)
    {
        require(controller.input().mouseLookActive() == active, "Unexpected camera look state");
        require(controller.router().cameraMouseCapture() == active, "Camera ownership disagrees with look state");
        require(glfwGetInputMode(window, GLFW_CURSOR) ==
                    (active ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL),
                "Native cursor mode disagrees with camera look state");
    }
    void expectDelta(float x, float y)
    {
        const auto delta = controller.input().consumeMouseDelta();
        require(std::abs(delta.x - x) < 0.01f && std::abs(delta.y - y) < 0.01f,
                "Camera delta was lost, accumulated while released, or jumped on capture");
    }
};

void testHeldLookCrossesEditorAndWindowEdges()
{
    Fixture f;
    f.controller.router().setAppMode(AppMode::Editor);
    f.button(GLFW_PRESS);
    f.expectCapture(true);
    require(!f.controller.input().mouseCaptured(), "Held look must not toggle persistent M capture");
    f.expectDelta(0, 0);
    f.controller.router().setEditorCapture(true, false);
    f.controller.router().setRuntimeUiCapture(true, false);
    const int refreshes = f.delegate.pointerRefreshes;
    f.move(150, 360);
    f.expectDelta(-490, 0);
    f.move(-3000, 4000);
    f.expectDelta(-3150, 3640);
    f.move(6000, -5000);
    f.expectDelta(9000, -9000);
    f.expectCapture(true);
    require(f.controller.router().routePolledGameInput() == InputOwner::Game,
            "Editor/HUD hover must not interrupt a camera gesture");
    require(f.delegate.pointerRefreshes == refreshes, "Virtual captured positions must not hit-test runtime UI");
    f.buttonCallback(f.window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    require(f.delegate.worldPresses == 0, "Camera capture must not activate editor world selection");
    f.scrollCallback(f.window, 0, 1);
    require(f.delegate.scrolls == 0, "Camera capture must not scroll runtime UI");
    f.button(GLFW_RELEASE);
    f.expectCapture(false);
    f.expectDelta(0, 0);
    require(f.controller.router().routePolledGameInput() == InputOwner::RuntimeUi,
            "Releasing camera look must restore normal UI priority");
    f.move(640, 360);
    f.expectDelta(0, 0);
    f.controller.router().setRuntimeUiCapture(false, false);
    require(f.controller.router().routePolledGameInput() == InputOwner::Editor,
            "Editor hover must resume after camera release");
}

void testToggleAndHeldLookCompose()
{
    Fixture f;
    f.key(GLFW_KEY_M);
    f.expectCapture(true);
    f.key(GLFW_KEY_M, GLFW_REPEAT);
    f.expectCapture(true);
    f.button(GLFW_PRESS);
    f.key(GLFW_KEY_M);
    f.expectCapture(true);
    f.button(GLFW_RELEASE);
    f.expectCapture(false);
    f.button(GLFW_PRESS);
    f.key(GLFW_KEY_M);
    f.button(GLFW_RELEASE);
    f.expectCapture(true);
    f.key(GLFW_KEY_M);
    f.expectCapture(false);
}

void testEscapeAndFocusLossReleaseWithoutRecapture()
{
    Fixture f;
    f.button(GLFW_PRESS);
    f.move(800, 420);
    f.key(GLFW_KEY_ESCAPE);
    f.expectCapture(false);
    f.expectDelta(0, 0);
    require(f.delegate.pauses == 0, "First Escape must release the camera before opening a pause screen");
    f.move(100, 50);
    f.expectDelta(0, 0);
    f.button(GLFW_RELEASE);
    f.button(GLFW_PRESS);
    f.expectCapture(true);
    f.expectDelta(0, 0);
    f.move(110, 55);
    f.expectDelta(10, 5);
    f.key(GLFW_KEY_M);
    GLFWwindow* other = glfwCreateWindow(100, 100, "headless focus target", nullptr, nullptr);
    require(other != nullptr, "Focus target creation failed");
    f.expectCapture(false);
    f.expectDelta(0, 0);
    glfwFocusWindow(f.window);
    glfwDestroyWindow(other);
    f.controller.updateMouseCapture();
    f.expectCapture(false);
    f.move(900, 500);
    f.expectDelta(0, 0);
}

void testUiOwnershipAndModeTransitions()
{
    Fixture f;
    f.controller.router().setAppMode(AppMode::Editor);
    f.controller.router().setEditorCapture(true, false);
    f.button(GLFW_PRESS);
    f.expectCapture(false);
    f.controller.router().setEditorCapture(false, true);
    f.key(GLFW_KEY_M);
    f.expectCapture(false);
    f.button(GLFW_PRESS);
    f.expectCapture(false);
    f.controller.router().setEditorCapture(false, false);
    f.button(GLFW_PRESS);
    f.expectCapture(true);
    f.controller.router().setRuntimeUiModalCapture(true);
    f.controller.updateMouseCapture();
    f.expectCapture(false);
    require(f.controller.router().routePointerMove(0, 0) == InputOwner::RuntimeUi,
            "A modal screen must keep pointer ownership");
    f.controller.router().setRuntimeUiModalCapture(false);
    f.controller.updateMouseCapture();
    f.expectCapture(false);
    f.key(GLFW_KEY_M);
    f.controller.router().setRuntimeUiCapture(false, true);
    f.controller.updateMouseCapture();
    f.expectCapture(false);
    f.controller.router().setRuntimeUiCapture(false, false);
    f.key(GLFW_KEY_M);
    f.controller.router().setAppMode(AppMode::Game);
    f.controller.updateMouseCapture();
    f.expectCapture(false);
#if VOXEL_WITH_EDITOR
    f.controller.router().setAppMode(AppMode::Editor);
    f.key(GLFW_KEY_M);
    f.key(GLFW_KEY_F5);
    f.expectCapture(false);
    require(f.controller.router().appMode() == AppMode::PlayPreview,
            "F5 must retain the existing mode transition");
#endif
}

void testLegacyBlockEditingAndDetach()
{
    Fixture f;
    f.key(GLFW_KEY_F5); // Game mode's legacy block edit binding.
    f.button(GLFW_PRESS);
    f.expectCapture(false);
    require(f.controller.input().consumeBlockPlace(), "Block editing must retain right-click placement");
    f.key(GLFW_KEY_M);
    f.expectCapture(true);
    f.controller.detach();
    f.expectCapture(false);
    require(glfwGetWindowUserPointer(f.window) == nullptr, "Detach must clear callback ownership");
}
} // namespace

int main()
{
    // real GLFW state and installed callbacks, without an os window or gpu.
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit())
    {
        std::cerr << "GLFW null platform initialization failed\n";
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    try
    {
        testHeldLookCrossesEditorAndWindowEdges();
        testToggleAndHeldLookCompose();
        testEscapeAndFocusLossReleaseWithoutRecapture();
        testUiOwnershipAndModeTransitions();
        testLegacyBlockEditingAndDetach();
        glfwTerminate();
        std::cout << "Input controller capture tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        glfwTerminate();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
