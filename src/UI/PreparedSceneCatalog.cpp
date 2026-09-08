#include "UI/PreparedSceneCatalog.h"

namespace SceneCatalogUi
{

void PreparedSceneCatalog::update(const std::vector<SceneCatalogEntry>& scenes,
                                  uint64_t revision, const char* filter,
                                  const std::filesystem::path& activePath,
                                  const std::string& activeName)
{
    const bool catalogChanged = source_ != &scenes || revision_ != revision;
    if (catalogChanged)
    {
        entries_.clear();
        entries_.reserve(scenes.size());
        for (const SceneCatalogEntry& scene : scenes)
        {
            entries_.push_back({classifyScene(scene),
                lowercaseCopy(scene.fileName + " " + scene.displayName + " " + scene.description),
                scene.path.lexically_normal()});
        }
        source_ = &scenes;
        revision_ = revision;
    }

    const std::string normalizedFilter = lowercaseCopy(filter == nullptr ? "" : filter);
    if (catalogChanged || filter_ != normalizedFilter)
    {
        for (auto& indices : visible_)
        {
            indices.clear();
        }
        for (size_t i = 0; i < entries_.size(); ++i)
        {
            const Entry& entry = entries_[i];
            if (normalizedFilter.empty() || entry.searchKey.find(normalizedFilter) != std::string::npos)
            {
                visible_[static_cast<size_t>(entry.group)].push_back(i);
            }
        }
        filter_ = normalizedFilter;
    }

    if (catalogChanged || activePath_ != activePath || activeName_ != activeName)
    {
        activePath_ = activePath;
        activeName_ = activeName;
        const auto normalizedPath = activePath.lexically_normal();
        activeGroups_.fill(false);
        activeEntries_.assign(scenes.size(), false);
        for (size_t i = 0; i < entries_.size(); ++i)
        {
            // preserve the legacy path or valid-name fallback, including multiple
            // matching names and invalid entries whose path matches the active path.
            const bool active = (!normalizedPath.empty() && entries_[i].normalizedPath == normalizedPath) ||
                (scenes[i].valid && !activeName.empty() && scenes[i].displayName == activeName);
            activeEntries_[i] = active;
            activeGroups_[static_cast<size_t>(entries_[i].group)] |= active;
        }
    }
}

} // namespace SceneCatalogUi
