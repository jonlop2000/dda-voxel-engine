#include "UI/SceneCatalogExperiment.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#include "Core/Logger.h"
#include "UI/SceneCatalogUi.h"

namespace SceneCatalogExperiment
{
namespace
{
struct Sample
{
    int64_t startUnixNs;
    int frame;
    double durationUs;
};

SceneCatalogUi::PreparedSceneCatalog prepared;
bool usePrepared = true;
std::string profilePath;
std::vector<Sample> samples;
size_t droppedSamples = 0;
constexpr size_t kMaxSamples = 65536;
} // namespace

void Init()
{
    prepared = {};
    const char* mode = std::getenv("DDA_VOXEL_SCENE_CATALOG_MODE");
    usePrepared = mode == nullptr || std::string(mode) != "legacy";
    if (mode != nullptr && std::string(mode) != "legacy" && std::string(mode) != "prepared")
    {
        logWarning("SceneCatalog", "Unknown DDA_VOXEL_SCENE_CATALOG_MODE; using prepared.");
    }
    const char* output = std::getenv("DDA_VOXEL_SCENE_CATALOG_PROFILE");
    profilePath = output == nullptr ? "" : output;
    samples.clear();
    droppedSamples = 0;
    if (!profilePath.empty())
    {
        samples.reserve(kMaxSamples);
    }
    if (mode != nullptr || !profilePath.empty())
    {
        logInfo("SceneCatalog", std::string("mode=") + (usePrepared ? "prepared" : "legacy") +
            " profile=" + (profilePath.empty() ? "off" : "on"));
    }
}

void Shutdown()
{
    if (!profilePath.empty())
    {
        std::ofstream output(profilePath);
        output << "# mode=" << (usePrepared ? "prepared" : "legacy")
               << " dropped=" << droppedSamples << '\n';
        output << "start_unix_ns,imgui_frame,duration_us\n" << std::fixed << std::setprecision(3);
        for (const Sample& sample : samples)
        {
            output << sample.startUnixNs << ',' << sample.frame << ',' << sample.durationUs << '\n';
        }
        output.close();
        if (!output)
        {
            logError("SceneCatalog", "Could not write catalog profile: " + profilePath);
        }
    }
    prepared = {};
    samples.clear();
    profilePath.clear();
}

void Draw(EngineFacade& engine, const char* filter)
{
    auto* view = usePrepared ? &prepared : nullptr;
    if (profilePath.empty())
    {
        SceneCatalogUi::DrawPanelCatalog(engine, filter, view);
        return;
    }

    // profile the same complete catalog draw in either mode. timestamping and
    // buffered sample storage are outside the timed region; disk I/O is at shutdown.
    const auto unixNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto start = std::chrono::steady_clock::now();
    SceneCatalogUi::DrawPanelCatalog(engine, filter, view);
    const double durationUs = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - start).count();
    if (samples.size() < kMaxSamples)
    {
        samples.push_back({unixNs, ImGui::GetFrameCount(), durationUs});
    }
    else
    {
        ++droppedSamples;
    }
}
} // namespace SceneCatalogExperiment
