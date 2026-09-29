#include "UI/Panels/PhysicsSandboxPanel.h"

#include <cmath>
#include <string>
#include <imgui.h>
#include <glm/geometric.hpp>
#include "engine/physics/PhysicsSandbox.h"
#include "engine/physics/BallContact.h"

namespace {
std::string ballLabel(std::size_t index, const engine::physics::PhysicsSandbox& sandbox)
{
    return sandbox.isStressPreset() ? "Ball " + std::to_string(index + 1) :
           std::string(1, static_cast<char>('A' + index));
}

double floorSlipSpeed(const engine::physics::Ball& ball)
{
    const auto contactVelocity = engine::physics::contactPointVelocity(
        ball, {0.0, -ball.radius, 0.0});
    return std::hypot(contactVelocity.x, contactVelocity.z);
}

const char* motionState(const engine::physics::Ball& ball)
{
    const bool centerStopped = glm::length(ball.velocity) <= 1e-8;
    if (centerStopped && glm::length(ball.angularVelocity) > 1e-8) return "Spinning";
    const bool supported = engine::physics::isBallSupportedByFloor(ball);
    if (!supported && !ball.isResting) return "Airborne";
    if (centerStopped) return "Resting";
    return supported && floorSlipSpeed(ball) <= 1e-6 ? "Rolling" : "Sliding";
}
} // namespace

PhysicsSandboxPanelActions drawPhysicsSandboxPanel(
    engine::physics::PhysicsSandbox& sandbox, bool frozen)
{
    PhysicsSandboxPanelActions actions;
    ImGui::SetNextWindowSize(ImVec2(450.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Physics Sandbox"))
    {
        using Preset = engine::physics::PhysicsSandbox::Preset;
        struct PresetOption { Preset value; const char* name; };
        const PresetOption presetOptions[] = {
            {Preset::Default, "Default (three balls)"},
            {Preset::FastCollision, "Fast collision (200 m/s)"},
            {Preset::ThreeBallChain, "Three-ball chain reaction"},
            {Preset::FreeSpin, "Rotation: free spin"},
            {Preset::SlideToRoll, "Rotation: sliding to rolling"},
            {Preset::SpinCollision, "Rotation: spin transfer"},
            {Preset::StressDrop, "Stress: falling balls"},
            {Preset::StressPairs, "Stress: approaching pairs"},
            {Preset::StressStacks, "Stress: settling stacks"}
        };
        const char* selectedPresetName = presetOptions[0].name;
        for (const auto& option : presetOptions)
            if (sandbox.preset() == option.value) selectedPresetName = option.name;
        if (ImGui::BeginCombo("Preset", selectedPresetName))
        {
            for (const auto& option : presetOptions)
            {
                const bool selected = sandbox.preset() == option.value;
                if (ImGui::Selectable(option.name, selected))
                {
                    sandbox.loadPreset(option.value);
                    actions.resetCamera = true;
                    actions.reset = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("Selecting a preset resets and pauses. Physics settings are preserved.");
        if (sandbox.isStressPreset())
        {
            const std::string selectedCount = std::to_string(sandbox.stressBallCount());
            if (ImGui::BeginCombo("Ball count", selectedCount.c_str()))
            {
                for (std::size_t count : {32u, 100u, 250u})
                {
                    const std::string label = std::to_string(count);
                    if (ImGui::Selectable(label.c_str(), count == sandbox.stressBallCount()))
                    {
                        sandbox.setStressBallCount(count);
                        actions.reset = true;
                    }
                }
                ImGui::EndCombo();
            }
            actions.resetCamera |= ImGui::Button("Overview camera");
            ImGui::TextWrapped("Changing count resets and pauses. Watch from above, then Resume.");
            ImGui::TextWrapped("250-ball limit leaves room for the voxel floor and walls.");
            if (sandbox.preset() == Preset::StressPairs)
                ImGui::TextWrapped("Pairs approach at 0.75 m/s each and first collide at 0.333333 s.");
            else if (sandbox.preset() == Preset::StressStacks)
                ImGui::TextWrapped("Four-ball columns settle onto their supports. "
                                   "Resting includes balls supported by other balls.");
            else
                ImGui::TextWrapped("Separated balls fall from four heights, bounce, and settle.");
            ImGui::TextWrapped("These balls have 0.1 m radii, larger than the benchmark fixtures.");
        }
        if (sandbox.preset() == Preset::FreeSpin)
        {
            actions.resetCamera |= ImGui::Button("Close-up camera");
            ImGui::TextWrapped("A (blue): X axis | B (pink): Y axis | C (yellow): Z axis.");
            ImGui::TextWrapped("Watch the light stripe and dark patch. Centers stay fixed.");
            ImGui::TextWrapped("90 degrees per simulated second; one full turn in 4 seconds.");
            ImGui::TextWrapped("This demonstration disables contact rotation and spin resistance.");
        }
        if (sandbox.preset() == Preset::SlideToRoll)
        {
            actions.resetCamera |= ImGui::Button("Rolling camera");
            ImGui::TextWrapped("A: no initial spin | B: already rolling | C: backspin.");
            ImGui::TextWrapped("Watch floor slip approach zero as friction changes speed and spin.");
            ImGui::TextWrapped("For ideal rolling, set rolling resistance to 0, then Reset.");
        }
        if (sandbox.preset() == Preset::SpinCollision)
        {
            actions.resetCamera |= ImGui::Button("Spin-transfer camera");
            ImGui::TextWrapped("Spinning A strikes B off-center. Contact friction transfers spin.");
            ImGui::TextWrapped("Compare ball friction 0 with 0.2; Reset between runs.");
        }
        float speed = static_cast<float>(sandbox.playbackSpeed());
        if (ImGui::SliderFloat("Playback speed", &speed, 0.1f, 1.0f, "%.2fx",
                               ImGuiSliderFlags_AlwaysClamp))
            sandbox.setPlaybackSpeed(speed);
        if (ImGui::Button("Reset"))
        {
            sandbox.reset();
            actions.reset = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(sandbox.isPaused() ? "Resume" : "Pause"))
        {
            sandbox.setPaused(!sandbox.isPaused());
            actions.pauseChanged = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!sandbox.isPaused());
        actions.step = ImGui::Button("Step (0.01 s)");
        ImGui::EndDisabled();
        ImGui::Text("Simulation: %s", sandbox.isPaused() ? "Paused" :
                    (frozen ? "Frozen" : "Running"));
        ImGui::Text("Simulation time: %.3f s", sandbox.elapsedTime());
        ImGui::Text("Balls: %zu | last-step impacts: %zu", sandbox.balls().size(), sandbox.lastStepImpacts());
        ImGui::Text("Last physics step: %.3f ms", sandbox.lastStepMilliseconds());
        ImGui::Text("Previous physics update: %.3f ms (%zu steps)",
                    sandbox.lastUpdateMilliseconds(), sandbox.lastUpdateSteps());
        ImGui::TextDisabled("CPU physics timings; excludes rendering.");
        if (sandbox.isStressPreset())
        {
            std::size_t airborne = 0, sliding = 0, rolling = 0, spinning = 0, resting = 0;
            for (const auto& ball : sandbox.balls())
            {
                const std::string state = motionState(ball);
                if (state == "Airborne") ++airborne;
                else if (state == "Sliding") ++sliding;
                else if (state == "Rolling") ++rolling;
                else if (state == "Spinning") ++spinning;
                else ++resting;
            }
            ImGui::Text("Airborne: %zu | sliding: %zu | rolling: %zu", airborne, sliding, rolling);
            ImGui::Text("Spinning: %zu | resting: %zu", spinning, resting);
        }
        if (sandbox.isStressPreset())
        {
            ImGui::Text("Discarded simulation debt: %.3f s", sandbox.discardedTime());
            ImGui::TextWrapped("At most four steps per frame; overload slows simulation time.");
        }
        double restitution = sandbox.restitution();
        const double minimumRestitution = 0.0;
        const double maximumRestitution = 1.0;
        if (ImGui::SliderScalar("Restitution", ImGuiDataType_Double, &restitution,
                                &minimumRestitution, &maximumRestitution, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp))
        {
            sandbox.setRestitution(restitution);
        }
        ImGui::TextWrapped("Applies to ball and boundary impacts. Reset to replay.");
        double floorFriction = sandbox.floorFriction();
        const double minimumFloorFriction = 0.0;
        const double maximumFloorFriction = 1.0;
        if (ImGui::SliderScalar("Floor friction", ImGuiDataType_Double, &floorFriction,
                                &minimumFloorFriction, &maximumFloorFriction, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp))
        {
            sandbox.setFloorFriction(floorFriction);
        }
        ImGui::TextWrapped("Floor contact friction changes sliding speed and spin when rotation is enabled.");
        const bool freeSpin = sandbox.preset() == Preset::FreeSpin;
        bool contactRotation = sandbox.rotationEnabled() && !freeSpin;
        ImGui::BeginDisabled(freeSpin);
        if (ImGui::Checkbox("Contact rotation", &contactRotation))
            sandbox.setRotationEnabled(contactRotation);
        ImGui::EndDisabled();
        if (!contactRotation && !freeSpin)
            ImGui::TextWrapped("Legacy mode: sliding slows without generating spin.");
        double ballFriction = sandbox.ballFriction();
        double wallFriction = sandbox.wallFriction();
        double rollingResistance = sandbox.rollingResistance();
        ImGui::BeginDisabled(!contactRotation);
        if (ImGui::SliderScalar("Ball friction", ImGuiDataType_Double, &ballFriction,
                                &minimumFloorFriction, &maximumFloorFriction, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp))
            sandbox.setBallFriction(ballFriction);
        if (ImGui::SliderScalar("Wall friction", ImGuiDataType_Double, &wallFriction,
                                &minimumFloorFriction, &maximumFloorFriction, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp))
            sandbox.setWallFriction(wallFriction);
        const double maximumRollingResistance = 0.1;
        if (ImGui::SliderScalar("Rolling resistance", ImGuiDataType_Double, &rollingResistance,
                                &minimumFloorFriction, &maximumRollingResistance, "%.3f",
                                ImGuiSliderFlags_AlwaysClamp))
            sandbox.setRollingResistance(rollingResistance);
        ImGui::EndDisabled();
        ImGui::TextWrapped("Resistance slows supported rolling and twist; 0 allows ideal rolling. "
                           "Controls affect the next step. Reset to replay.");
        if (sandbox.preset() == Preset::FastCollision)
        {
            ImGui::Separator();
            ImGui::TextWrapped("A and B approach at 200 m/s each. C rests off to the side. "
                               "From reset, press Step once to inspect the first impact.");
            ImGui::TextUnformatted("Expected first contact: 0.004500 s (A-B)");
            ImGui::TextWrapped("With restitution 0.80, after one step: "
                               "A has x = -0.980 m, vx = -160 m/s; "
                               "B has x = +0.980 m, vx = +160 m/s.");
            ImGui::Text("Current center x (m): A %.3f | B %.3f",
                        sandbox.balls()[0].position.x,
                        sandbox.balls()[1].position.x);
        }
        if (sandbox.preset() == Preset::ThreeBallChain)
        {
            ImGui::Separator();
            ImGui::TextWrapped("Three equal 1 kg balls. A starts at 200 m/s; B and C start stationary. "
                               "For full horizontal velocity transfer, set restitution to 1.00, "
                               "then Reset and Step once.");
            // the second contact follows the transferred velocity.
            const double secondContactTime = 0.004 + 0.8 / (100.0 * (1.0 + sandbox.restitution()));
            ImGui::TextWrapped("Expected times from reset at the current restitution:");
            ImGui::Text("A-B: 0.004000 s | B-C: %.6f s", secondContactTime);
            if (secondContactTime > engine::physics::PhysicsSandbox::timeStep)
                ImGui::TextWrapped("At this restitution, B-C occurs after the first step.");
            ImGui::TextWrapped("At restitution 1.00, after one step: vx is 0, 0, 200 m/s "
                               "and x is -0.200, 0.800, 1.400 m for A, B, C.");
            ImGui::Text("Current center x (m): A %.3f | B %.3f | C %.3f",
                        sandbox.balls()[0].position.x,
                        sandbox.balls()[1].position.x,
                        sandbox.balls()[2].position.x);
        }
        if (!sandbox.isStressPreset() && ImGui::CollapsingHeader("Rotation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (std::size_t i = 0; i < sandbox.balls().size(); ++i)
            {
                const auto& ball = sandbox.balls()[i];
                ImGui::Text("%s spin (rad/s): %.3f, %.3f, %.3f", ballLabel(i, sandbox).c_str(),
                            ball.angularVelocity.x, ball.angularVelocity.y, ball.angularVelocity.z);
                if (engine::physics::isBallSupportedByFloor(ball))
                    ImGui::Text("  Floor slip: %.4g m/s", floorSlipSpeed(ball));
                else
                    ImGui::TextDisabled("  Floor slip: no floor contact");
                const double rotationalEnergy = 0.5 * engine::physics::ballMomentOfInertia(ball) *
                                                glm::dot(ball.angularVelocity, ball.angularVelocity);
                ImGui::Text("  Kinetic energy: spin %.6f J | total %.6f J", rotationalEnergy,
                            engine::physics::ballKineticEnergy(ball));
                ImGui::Text("  Orientation (w, x, y, z): %.3f, %.3f, %.3f, %.3f",
                            ball.orientation.w, ball.orientation.x,
                            ball.orientation.y, ball.orientation.z);
            }
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Recent ball collisions");
        ImGui::TextDisabled("Contact time; newest first");
        if (ImGui::BeginChild("BallCollisionHistory",
                             ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 4.0f)))
        {
            const auto& collisions = sandbox.recentCollisions();
            if (collisions.empty())
            {
                ImGui::TextDisabled("No ball collisions yet.");
            }
            for (auto event = collisions.rbegin(); event != collisions.rend(); ++event)
            {
                ImGui::Text("%.6f s: %s hit %s", event->simulationTime,
                            ballLabel(event->ballA, sandbox).c_str(),
                            ballLabel(event->ballB, sandbox).c_str());
            }
        }
        ImGui::EndChild();
        if (ImGui::CollapsingHeader("Latest impact momentum", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const auto& collisions = sandbox.recentCollisions();
            if (collisions.empty())
            {
                ImGui::TextDisabled("Waiting for a ball collision.");
            }
            else
            {
                const auto& event = collisions.back();
                const glm::dvec3 momentumChange = event.momentumAfter - event.momentumBefore;
                ImGui::Text("%.6f s: %s + %s (kg*m/s)", event.simulationTime,
                            ballLabel(event.ballA, sandbox).c_str(),
                            ballLabel(event.ballB, sandbox).c_str());
                if (ImGui::BeginTable("ImpactMomentum", 4,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchSame))
                {
                    ImGui::TableSetupColumn("Axis");
                    ImGui::TableSetupColumn("Before");
                    ImGui::TableSetupColumn("After");
                    ImGui::TableSetupColumn("Change");
                    ImGui::TableHeadersRow();
                    const char* axisNames[] = {"x", "y", "z"};
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(axisNames[axis]);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.3f", event.momentumBefore[axis]);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.3f", event.momentumAfter[axis]);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%.2e", momentumChange[axis]);
                    }
                    ImGui::EndTable();
                }
                ImGui::Text("Change magnitude: %.2e kg*m/s", glm::length(momentumChange));
                ImGui::TextWrapped("Change = after - before; expected near zero.");
            }
        }
        if (ImGui::CollapsingHeader("Latest impact energy", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const auto& collisions = sandbox.recentCollisions();
            if (collisions.empty())
            {
                ImGui::TextDisabled("Waiting for a ball collision.");
            }
            else
            {
                const auto& event = collisions.back();
                const double energyLost = event.kineticEnergyBefore - event.kineticEnergyAfter;
                ImGui::Text("%.6f s: %s + %s", event.simulationTime,
                            ballLabel(event.ballA, sandbox).c_str(),
                            ballLabel(event.ballB, sandbox).c_str());
                ImGui::Text("Restitution at impact: %.2f", event.restitution);
                ImGui::TextUnformatted(event.includesRotation ?
                    "Combined translation + rotation energy" : "Combined translation energy");
                ImGui::Text("Before: %.6f J", event.kineticEnergyBefore);
                ImGui::Text("After:  %.6f J", event.kineticEnergyAfter);
                ImGui::Text("Lost:   %.6f J", energyLost);
                ImGui::TextWrapped("Energy lost = before - after.");
            }
        }
        ImGui::Separator();
        if (!sandbox.isStressPreset())
        {
            // show the three-ball teaching table for the original presets.
            const auto& balls = sandbox.balls();
            const auto& ballA = balls[0];
            const auto& ballB = balls[1];
            const auto& ballC = balls[2];
            ImGui::Text("Mass (kg): A %.1f | B %.1f | C %.1f", ballA.mass, ballB.mass, ballC.mass);
            ImGui::TextUnformatted("Velocity / speed (m/s)");
            // compare matching velocity components.
            if (ImGui::BeginTable("BallVelocities", 4,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Quantity");
                ImGui::TableSetupColumn("A (blue)");
                ImGui::TableSetupColumn("B (pink)");
                ImGui::TableSetupColumn("C (yellow)");
                ImGui::TableHeadersRow();
                const char* axisNames[] = {"vx", "vy", "vz"};
                for (int axis = 0; axis < 3; ++axis)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(axisNames[axis]);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f", ballA.velocity[axis]);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.3f", ballB.velocity[axis]);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.3f", ballC.velocity[axis]);
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Speed x/z");
                for (int ballIndex = 0; ballIndex < 3; ++ballIndex)
                {
                    const auto& ball = balls[ballIndex];
                    const double horizontalSpeed = std::hypot(ball.velocity.x, ball.velocity.z);
                    ImGui::TableSetColumnIndex(ballIndex + 1);
                    // keep slow sliding visible with significant digits.
                    ImGui::Text("%.4g", horizontalSpeed);
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("State");
                for (int ballIndex = 0; ballIndex < 3; ++ballIndex)
                {
                    const auto& ball = balls[ballIndex];
                    ImGui::TableSetColumnIndex(ballIndex + 1);
                    ImGui::TextUnformatted(motionState(ball));
                }
                ImGui::EndTable();
            }
            ImGui::TextWrapped("Rolling means floor slip is near zero while the center moves. "
                               "Spinning means the center is stopped while orientation changes. "
                               "Resting means both motion and spin have stopped.");
            ImGui::Separator();
            ImGui::TextUnformatted("Ball A (blue)");
            ImGui::Text("Center height: %.3f m", ballA.position.y);
            ImGui::Text("Horizontal position (x): %.3f m", ballA.position.x);
        }
    }
    ImGui::End();
    return actions;
}
