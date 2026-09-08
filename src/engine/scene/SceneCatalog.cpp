#include "engine/scene/SceneCatalog.h"

#include "engine/scene/SceneConfig.h"
#include "engine/scene/SceneSerializer.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace
{

std::string lowercaseCopy(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

} // namespace

std::vector<SceneCatalogEntry> scanSceneCatalog(const std::filesystem::path& scenesDir)
{
    namespace fs = std::filesystem;

    std::vector<SceneCatalogEntry> scenes;
    std::error_code ec;
    if (!fs::exists(scenesDir, ec) || !fs::is_directory(scenesDir, ec))
    {
        return scenes;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(scenesDir, ec))
    {
        if (ec || !entry.is_regular_file(ec))
        {
            continue;
        }

        const fs::path path = entry.path().lexically_normal();
        if (lowercaseCopy(path.extension().string()) != ".json")
        {
            continue;
        }
        if (lowercaseCopy(path.filename().string()) == "last_used.json")
        {
            continue;
        }

        SceneCatalogEntry scene;
        scene.path = path;
        scene.fileName = path.filename().string();
        scenes.push_back(std::move(scene));
    }

    std::sort(scenes.begin(), scenes.end(),
              [](const SceneCatalogEntry& a, const SceneCatalogEntry& b) {
                  return lowercaseCopy(a.fileName) < lowercaseCopy(b.fileName);
              });

    for (SceneCatalogEntry& scene : scenes)
    {
        SceneConfig cfg;
        SceneLoadDiagnostics diagnostics;
        if (loadSceneConfigFromFile(scene.path, cfg, &diagnostics, SceneSerializerLogMode::Quiet))
        {
            scene.valid = true;
            scene.displayName = cfg.name.empty() ? scene.path.stem().string() : cfg.name;
            scene.description = cfg.description;
        }
        else
        {
            scene.valid = false;
            scene.displayName = scene.path.stem().string();
            scene.errorMessage =
                diagnostics.errorMessage.empty() ? "Failed to load scene" : diagnostics.errorMessage;
        }
    }

    return scenes;
}
