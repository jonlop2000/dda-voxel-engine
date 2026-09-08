#include "engine/render/FrameCharacterization.h"

#include "Core/Logger.h"

#include <cstdlib>
#include <string>

namespace engine::render
{
namespace
{

struct RuntimeFrameCharacterizationCapture
{
    bool enabled = false;
    bool active = false;
    bool logged = false;
    bool sawRecordPasses = false;
    bool sawEndFrame = false;
    std::vector<std::string> frameLoop{};
    std::vector<std::string> handshake{};
    std::vector<std::string> passOrder{};
};

struct RuntimePassLifecycleCharacterizationCapture
{
    bool enabled = false;
    bool active = false;
    std::string phase{};
    std::vector<std::string> events{};
};

bool environmentFlagEnabled(const char* name)
{
#if defined(_WIN32)
    char* value = nullptr;
    size_t valueLength = 0;
    if (_dupenv_s(&value, &valueLength, name) != 0 || value == nullptr)
    {
        return false;
    }
    const bool enabled = valueLength > 1 && !(value[0] == '0' && value[1] == '\0');
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
#endif
}

RuntimeFrameCharacterizationCapture& runtimeCapture()
{
    static RuntimeFrameCharacterizationCapture capture{
        environmentFlagEnabled("DDA_VOXEL_CHARACTERIZE_FRAME")};
    return capture;
}

RuntimePassLifecycleCharacterizationCapture& runtimePassLifecycleCapture()
{
    static RuntimePassLifecycleCharacterizationCapture capture{
        environmentFlagEnabled("DDA_VOXEL_CHARACTERIZE_FRAME")};
    return capture;
}

std::string joinOrder(const std::vector<std::string>& values)
{
    std::string result;
    for (std::string_view value : values)
    {
        if (!result.empty())
        {
            result.push_back('>');
        }
        result.append(value);
    }
    return result;
}

void logRuntimeFrameCharacterization(RuntimeFrameCharacterizationCapture& capture)
{
    logInfo("Characterization", makeLogMessage("runtime_handshake=", joinOrder(capture.handshake)));
    logInfo("Characterization", makeLogMessage("runtime_frame_loop=", joinOrder(capture.frameLoop)));
    logInfo("Characterization", makeLogMessage("pass_order=", joinOrder(capture.passOrder)));
    capture.logged = true;
    capture.active = false;
}

}  // namespace

void beginRuntimeFrameLoopCharacterization()
{
    RuntimeFrameCharacterizationCapture& capture = runtimeCapture();
    if (!capture.enabled || capture.logged)
    {
        return;
    }

    capture.active = true;
    capture.sawRecordPasses = false;
    capture.sawEndFrame = false;
    capture.frameLoop.clear();
    capture.handshake.clear();
    capture.passOrder.clear();
}

void traceRuntimeFrameLoopStep(std::string_view step)
{
    RuntimeFrameCharacterizationCapture& capture = runtimeCapture();
    if (capture.active && !capture.logged)
    {
        capture.frameLoop.emplace_back(step);
    }
}

void finishRuntimeFrameCharacterization()
{
    RuntimeFrameCharacterizationCapture& capture = runtimeCapture();
    if (!capture.active || capture.logged)
    {
        return;
    }

    if (!capture.sawRecordPasses || !capture.sawEndFrame)
    {
        capture.active = false;
        return;
    }

    logRuntimeFrameCharacterization(capture);
}

void beginRuntimeFrameCharacterization()
{
    RuntimeFrameCharacterizationCapture& capture = runtimeCapture();
    if (!capture.enabled || capture.logged)
    {
        return;
    }

    if (!capture.active)
    {
        capture.active = true;
        capture.sawRecordPasses = false;
        capture.sawEndFrame = false;
        capture.frameLoop.clear();
        capture.handshake.clear();
        capture.passOrder.clear();
    }
    traceRuntimeFrameCharacterizationStep("beginFrame");
    traceRuntimeFrameLoopStep("renderer.beginFrame");
}

void traceRuntimeFrameCharacterizationStep(std::string_view step)
{
    RuntimeFrameCharacterizationCapture& capture = runtimeCapture();
    if (capture.active && !capture.logged)
    {
        capture.handshake.emplace_back(step);
    }
}

void recordRuntimeFrameCharacterizationPasses(const std::vector<std::string_view>& passNames)
{
    RuntimeFrameCharacterizationCapture& capture = runtimeCapture();
    if (!capture.active || capture.logged)
    {
        return;
    }

    if (!capture.sawRecordPasses)
    {
        traceRuntimeFrameCharacterizationStep("recordPasses");
        traceRuntimeFrameLoopStep("recordPasses");
        capture.sawRecordPasses = true;
    }
    for (std::string_view name : passNames)
    {
        capture.passOrder.emplace_back(name);
    }
}

void endRuntimeFrameCharacterization()
{
    RuntimeFrameCharacterizationCapture& capture = runtimeCapture();
    if (!capture.active || capture.logged)
    {
        return;
    }

    traceRuntimeFrameCharacterizationStep("endFrame");
    traceRuntimeFrameLoopStep("renderer.endFrame");
    capture.sawEndFrame = true;
}

void beginRuntimePassLifecycleCharacterization(std::string_view phase)
{
    RuntimePassLifecycleCharacterizationCapture& capture = runtimePassLifecycleCapture();
    if (!capture.enabled)
    {
        return;
    }

    capture.active = true;
    capture.phase.assign(phase);
    capture.events.clear();
}

void traceRuntimePassLifecycleEvent(std::string_view passName)
{
    RuntimePassLifecycleCharacterizationCapture& capture = runtimePassLifecycleCapture();
    if (capture.active)
    {
        capture.events.emplace_back(passName);
    }
}

void endRuntimePassLifecycleCharacterization()
{
    RuntimePassLifecycleCharacterizationCapture& capture = runtimePassLifecycleCapture();
    if (!capture.active)
    {
        return;
    }

    logInfo("Characterization",
            makeLogMessage(capture.phase, "_order=", joinOrder(capture.events)));
    capture.active = false;
}

}  // namespace engine::render
