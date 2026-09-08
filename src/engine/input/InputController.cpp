#include "engine/input/InputController.h"

#include <GLFW/glfw3.h>

namespace engine::input
{

void InputController::attach(GLFWwindow* window, Delegate& delegate)
{
    window_ = window;
    delegate_ = &delegate;

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);
    glfwSetWindowFocusCallback(window_, windowFocusCallback);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetCursorPosCallback(window_, cursorPosCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetScrollCallback(window_, scrollCallback);
}

void InputController::detach()
{
    if (window_ != nullptr && glfwGetWindowUserPointer(window_) == this)
    {
        input_.releaseMouseLook();
        updateMouseCapture();
        glfwSetFramebufferSizeCallback(window_, nullptr);
        glfwSetWindowFocusCallback(window_, nullptr);
        glfwSetKeyCallback(window_, nullptr);
        glfwSetCursorPosCallback(window_, nullptr);
        glfwSetMouseButtonCallback(window_, nullptr);
        glfwSetScrollCallback(window_, nullptr);
        glfwSetWindowUserPointer(window_, nullptr);
    }
    window_ = nullptr;
    delegate_ = nullptr;
    router_.setCameraMouseCapture(false);
}

void InputController::updateMouseCapture()
{
    if (window_ == nullptr)
    {
        return;
    }
    const bool modeChanged = router_.cameraMouseCapture() &&
                             captureAppMode_ != router_.appMode();
    if (glfwGetWindowAttrib(window_, GLFW_FOCUSED) != GLFW_TRUE || modeChanged ||
        router_.routeKey(0, 0) != InputOwner::Game)
    {
        input_.releaseMouseLook();
    }
    captureAppMode_ = router_.appMode();
    router_.setCameraMouseCapture(input_.mouseLookActive());

    // use GLFW's unbounded relative cursor for held look as well as m capture.
    // apply transitions in callbacks, before a later event can reach an editor edge.
    const int mode = input_.mouseLookActive() ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
    if (glfwGetInputMode(window_, GLFW_CURSOR) != mode)
    {
        glfwSetInputMode(window_, GLFW_CURSOR, mode);
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window_, &x, &y);
        input_.syncMousePosition(x, y);
    }
}

FramebufferPoint InputController::framebufferPoint(double x, double y,
                                                   uint32_t framebufferWidth,
                                                   uint32_t framebufferHeight) const
{
    if (window_ == nullptr)
    {
        return windowToFramebuffer(x, y, 0, 0, static_cast<int>(framebufferWidth),
                                   static_cast<int>(framebufferHeight));
    }

    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowSize(window_, &windowWidth, &windowHeight);
    return windowToFramebuffer(x, y, windowWidth, windowHeight,
                               static_cast<int>(framebufferWidth),
                               static_cast<int>(framebufferHeight));
}

void InputController::noteUserInteraction()
{
    if (delegate_ != nullptr)
    {
        delegate_->onInputUserInteraction(glfwGetTime());
    }
}

bool InputController::handleReservedKey(int key, int action, int mods)
{
#if VOXEL_WITH_EDITOR
    if (delegate_ != nullptr && action == GLFW_PRESS && key == GLFW_KEY_F5)
    {
        if (router_.appMode() == AppMode::PlayPreview)
        {
            delegate_->exitRuntimeUiPlayPreview("F5");
            return true;
        }
        if (router_.appMode() == AppMode::Editor)
        {
            const bool startAtMainMenu = (mods & GLFW_MOD_SHIFT) != 0;
            delegate_->enterRuntimeUiPlayPreview(startAtMainMenu ? "Shift+F5" : "F5",
                                                 startAtMainMenu);
            return true;
        }
    }
#else
    (void)key;
    (void)action;
    (void)mods;
#endif
    return false;
}

InputController* InputController::fromWindow(GLFWwindow* window)
{
    return reinterpret_cast<InputController*>(glfwGetWindowUserPointer(window));
}

void InputController::framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    (void)width;
    (void)height;
    InputController* controller = fromWindow(window);
    if (controller != nullptr && controller->delegate_ != nullptr)
    {
        controller->delegate_->onInputFramebufferResized();
    }
}

void InputController::keyCallback(GLFWwindow* window, int key, int scancode, int action,
                                  int mods)
{
    (void)scancode;
    InputController* controller = fromWindow(window);
    if (controller == nullptr || controller->delegate_ == nullptr)
    {
        return;
    }
    controller->noteUserInteraction();
    controller->updateMouseCapture();
    if (controller->handleReservedKey(key, action, mods))
    {
        controller->updateMouseCapture();
        return;
    }
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS &&
        controller->input_.mouseLookActive())
    {
        controller->input_.releaseMouseLook();
        controller->updateMouseCapture();
        return;
    }
    const InputOwner owner = controller->router_.routeKey(key, action);
#if VOXEL_WITH_RUNTIME_UI
    // a modal runtime screen owns the key before any gameplay shortcut is
    // evaluated. escape remains the ui-level back/close command; every other key
    // is consumed so hidden placement, focus, and debug state cannot be mutated.
    if (owner == InputOwner::RuntimeUi)
    {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        {
            controller->delegate_->toggleRuntimeUiPauseScreen();
            double xpos = 0.0;
            double ypos = 0.0;
            glfwGetCursorPos(window, &xpos, &ypos);
            controller->delegate_->refreshRuntimeUiPointerCapture(xpos, ypos);
        }
        return;
    }
#endif
    if (owner != InputOwner::Game)
    {
        return;
    }
#if VOXEL_WITH_RUNTIME_UI
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS &&
        controller->delegate_->cancelRuntimeUiPendingPlacement("escape", nullptr, 0))
    {
        return;
    }
    if (key == GLFW_KEY_R && action == GLFW_PRESS &&
        controller->delegate_->rotateRuntimeUiPendingPlacement("keyboard", nullptr, 0))
    {
        return;
    }
#endif
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS &&
        controller->delegate_->releaseFishFocus())
    {
        return;
    }
#if VOXEL_WITH_RUNTIME_UI
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS &&
        controller->delegate_->toggleRuntimeUiPauseScreen())
    {
        double xpos = 0.0;
        double ypos = 0.0;
        glfwGetCursorPos(window, &xpos, &ypos);
        controller->delegate_->refreshRuntimeUiPointerCapture(xpos, ypos);
        return;
    }
#endif
    controller->input_.handleKey(key, action);
    controller->updateMouseCapture();
}

void InputController::windowFocusCallback(GLFWwindow* window, int focused)
{
    InputController* controller = fromWindow(window);
    if (controller != nullptr && focused == GLFW_FALSE)
    {
        controller->input_.releaseMouseLook();
        controller->updateMouseCapture();
    }
}

void InputController::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    InputController* controller = fromWindow(window);
    if (controller == nullptr || controller->delegate_ == nullptr)
    {
        return;
    }
    controller->noteUserInteraction();
    controller->updateMouseCapture();
#if VOXEL_WITH_RUNTIME_UI
    if (!controller->input_.mouseLookActive())
    {
        controller->delegate_->refreshRuntimeUiPointerCapture(xpos, ypos);
    }
#endif
    if (controller->router_.routePointerMove(xpos, ypos) == InputOwner::Game)
    {
        controller->input_.handleMouseMove(xpos, ypos);
    }
    else
    {
        controller->input_.syncMousePosition(xpos, ypos);
    }
}

void InputController::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    InputController* controller = fromWindow(window);
    if (controller == nullptr || controller->delegate_ == nullptr)
    {
        return;
    }
    controller->noteUserInteraction();
    controller->updateMouseCapture();
    double xpos = 0.0;
    double ypos = 0.0;
    glfwGetCursorPos(window, &xpos, &ypos);
#if VOXEL_WITH_RUNTIME_UI
    const ui::UiHitResult hit = controller->input_.mouseLookActive()
                                   ? ui::UiHitResult{}
                                   : controller->delegate_->refreshRuntimeUiPointerCapture(xpos, ypos);
#endif
    const InputOwner owner = controller->router_.routePointerButton(button, action);
#if VOXEL_WITH_EDITOR
    if (owner == InputOwner::Game &&
        !controller->input_.mouseLookActive() &&
        controller->router_.appMode() == AppMode::Editor &&
        button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS &&
        controller->delegate_->handleEditorWorldPointerPress(xpos, ypos, mods))
    {
        return;
    }
#else
    (void)mods;
#endif
#if VOXEL_WITH_RUNTIME_UI
    if (owner == InputOwner::Game && button == GLFW_MOUSE_BUTTON_LEFT &&
        !controller->input_.mouseLookActive() &&
        action == GLFW_PRESS && hit.element == nullptr &&
        controller->delegate_->confirmRuntimeUiPendingPlacement("left-click", nullptr, 0))
    {
        controller->delegate_->refreshRuntimeUiPointerCapture(xpos, ypos);
        return;
    }
#endif
    const bool routeRightMouseReleaseToGame =
        button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE &&
        controller->input_.mouseLookActive();
    if (owner == InputOwner::Game || routeRightMouseReleaseToGame)
    {
        controller->input_.handleMouseButton(button, action);
        controller->updateMouseCapture();
    }
#if VOXEL_WITH_RUNTIME_UI
    else if (owner == InputOwner::RuntimeUi && button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if (action == GLFW_PRESS)
        {
            controller->delegate_->handleRuntimeUiPointerDown(hit);
        }
        else if (action == GLFW_RELEASE)
        {
            controller->delegate_->handleRuntimeUiPointerUp(hit);
            controller->delegate_->refreshRuntimeUiPointerCapture(xpos, ypos);
        }
    }
#endif
}

void InputController::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    (void)xoffset;
    InputController* controller = fromWindow(window);
    if (controller == nullptr || controller->delegate_ == nullptr)
    {
        return;
    }
    controller->noteUserInteraction();
    controller->updateMouseCapture();
    if (controller->input_.mouseLookActive())
    {
        return;
    }
#if VOXEL_WITH_RUNTIME_UI
    double xpos = 0.0;
    double ypos = 0.0;
    glfwGetCursorPos(window, &xpos, &ypos);
    controller->delegate_->refreshRuntimeUiPointerCapture(xpos, ypos);
    if (controller->delegate_->handleRuntimeUiScroll(xpos, ypos, yoffset))
    {
        return;
    }
#endif
}

} // namespace engine::input
