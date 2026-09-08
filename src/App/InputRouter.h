#pragma once

enum class AppMode
{
    Editor,
    PlayPreview,
    Game,
};

enum class InputOwner
{
    None,
    System,
    Editor,
    RuntimeUi,
    Game,
};

class InputRouter
{
public:
    void setAppMode(AppMode mode) { appMode_ = mode; }
    AppMode appMode() const { return appMode_; }

    void setEditorCapture(bool wantsMouse, bool wantsKeyboard)
    {
        editorWantsMouse_ = wantsMouse;
        editorWantsKeyboard_ = wantsKeyboard;
    }

    void setRuntimeUiCapture(bool wantsPointer, bool wantsKeyboard)
    {
        runtimeUiWantsPointer_ = wantsPointer;
        runtimeUiWantsKeyboard_ = wantsKeyboard;
    }

    // modal runtime screens own both input channels independently of whether the
    // cursor currently intersects a hittable element. keeping modality separate
    // from hover/focus capture also prevents a pointer refresh from accidentally
    // releasing keyboard ownership while an overlay is open.
    void setRuntimeUiModalCapture(bool modal) { runtimeUiModalCapture_ = modal; }
    bool runtimeUiModalCapture() const { return runtimeUiModalCapture_; }

    // a camera gesture keeps pointer ownership across editor/HUD boundaries.
    // modal screens and keyboard focus still take priority.
    void setCameraMouseCapture(bool captured) { cameraMouseCapture_ = captured; }
    bool cameraMouseCapture() const { return cameraMouseCapture_; }

    InputOwner routeKey(int key, int action) const
    {
        (void)key;
        (void)action;

        if (runtimeUiModalCapture_ || runtimeUiWantsKeyboard_)
        {
            return InputOwner::RuntimeUi;
        }
        if (appMode_ == AppMode::Editor && editorWantsKeyboard_)
        {
            return InputOwner::Editor;
        }
        return InputOwner::Game;
    }

    InputOwner routePointerMove(double x, double y) const
    {
        (void)x;
        (void)y;
        return routePointer();
    }

    InputOwner routePointerButton(int button, int action) const
    {
        (void)button;
        (void)action;
        return routePointer();
    }

    InputOwner routePolledGameInput() const
    {
        if (runtimeUiModalCapture_ || runtimeUiWantsKeyboard_ ||
            (!cameraMouseCapture_ && runtimeUiWantsPointer_))
        {
            return InputOwner::RuntimeUi;
        }
        if (appMode_ == AppMode::Editor && editorWantsKeyboard_)
        {
            return InputOwner::Editor;
        }
        return routePointer();
    }

    static const char* label(AppMode mode)
    {
        switch (mode)
        {
        case AppMode::Editor:
            return "editor";
        case AppMode::PlayPreview:
            return "play-preview";
        case AppMode::Game:
            return "game";
        }
        return "unknown";
    }

    static const char* label(InputOwner owner)
    {
        switch (owner)
        {
        case InputOwner::None:
            return "none";
        case InputOwner::System:
            return "system";
        case InputOwner::Editor:
            return "editor";
        case InputOwner::RuntimeUi:
            return "runtime-ui";
        case InputOwner::Game:
            return "game";
        }
        return "unknown";
    }

private:
    InputOwner routePointer() const
    {
        if (runtimeUiModalCapture_)
        {
            return InputOwner::RuntimeUi;
        }
        if (cameraMouseCapture_)
        {
            return InputOwner::Game;
        }
        if (runtimeUiWantsPointer_)
        {
            return InputOwner::RuntimeUi;
        }
        if (appMode_ == AppMode::Editor && editorWantsMouse_)
        {
            return InputOwner::Editor;
        }
        return InputOwner::Game;
    }

    AppMode appMode_ = AppMode::Game;
    bool editorWantsMouse_ = false;
    bool editorWantsKeyboard_ = false;
    bool runtimeUiWantsPointer_ = false;
    bool runtimeUiWantsKeyboard_ = false;
    bool runtimeUiModalCapture_ = false;
    bool cameraMouseCapture_ = false;
};
