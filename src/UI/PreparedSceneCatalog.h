#pragma once

#include <cstdint>

#include "UI/SceneCatalogModel.h"

namespace SceneCatalogUi
{

// ui-owned derived data. entries are indices into the current catalog, never
// retained references. the owner must advance revision on every catalog refresh.
class PreparedSceneCatalog
{
public:
    void update(const std::vector<SceneCatalogEntry>& scenes, uint64_t revision,
                const char* filter, const std::filesystem::path& activePath,
                const std::string& activeName);

    const std::vector<size_t>& visible(SceneGroup group) const
    {
        return visible_[static_cast<size_t>(group)];
    }
    bool groupIsActive(SceneGroup group) const
    {
        return activeGroups_[static_cast<size_t>(group)];
    }
    bool isActive(size_t index) const { return activeEntries_[index]; }

private:
    struct Entry
    {
        SceneGroup group;
        std::string searchKey;
        std::filesystem::path normalizedPath;
    };

    const std::vector<SceneCatalogEntry>* source_ = nullptr;
    uint64_t revision_ = 0;
    std::vector<Entry> entries_;
    std::array<std::vector<size_t>, kSceneGroups.size()> visible_;
    std::array<bool, kSceneGroups.size()> activeGroups_{};
    std::vector<bool> activeEntries_;
    std::string filter_;
    std::filesystem::path activePath_;
    std::string activeName_;
};

} // namespace SceneCatalogUi
