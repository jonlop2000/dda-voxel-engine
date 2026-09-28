#pragma once

namespace engine::physics { class PhysicsSandbox; }

struct PhysicsSandboxPanelActions
{
    bool reset = false;
    bool pauseChanged = false;
    bool step = false;
    bool resetCamera = false;
};

PhysicsSandboxPanelActions drawPhysicsSandboxPanel(
    engine::physics::PhysicsSandbox& sandbox, bool frozen);
