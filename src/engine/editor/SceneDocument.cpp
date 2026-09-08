#include "engine/editor/SceneDocument.h"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace engine::editor
{

SceneDocument::SceneDocument(SceneConfig authoredConfig)
{
    const SceneDocumentValidation validation =
        replaceAuthoredConfig(std::move(authoredConfig));
    if (!validation)
    {
        throw std::invalid_argument("SceneDocument received an invalid placeable collection");
    }
}

SceneDocumentValidation SceneDocument::replaceAuthoredConfig(
    SceneConfig authoredConfig)
{
    if (playSnapshot_)
    {
        SceneDocumentValidation result{};
        result.error = SceneDocumentError::PlaySnapshotActive;
        return result;
    }

    const SceneDocumentValidation validation =
        canonicalizePlaceables(authoredConfig);
    if (!validation)
    {
        return validation;
    }

    authoredConfig_ = std::move(authoredConfig);
    return {};
}

bool SceneDocument::beginPlaySnapshot()
{
    if (playSnapshot_)
    {
        return false;
    }
    playSnapshot_ = authoredConfig_;
    return true;
}

const SceneConfig* SceneDocument::playSnapshot() const noexcept
{
    return playSnapshot_ ? &*playSnapshot_ : nullptr;
}

SceneConfig* SceneDocument::mutablePlaySnapshot() noexcept
{
    return playSnapshot_ ? &*playSnapshot_ : nullptr;
}

SceneConfig SceneDocument::endPlayAndRestoreAuthored()
{
    SceneConfig restored = authoredConfig_;
    playSnapshot_.reset();
    return restored;
}

SceneDocumentValidation SceneDocument::canonicalizePlaceables(
    SceneConfig& config)
{
    std::unordered_set<std::string> placeableUuids{};
    placeableUuids.reserve(config.placeables.size());
    for (size_t index = 0; index < config.placeables.size(); ++index)
    {
        const PlaceableInstance& instance = config.placeables[index];
        if (instance.uuid.empty() || instance.prototypeSlug.empty() ||
            instance.prototypeVersion == 0)
        {
            SceneDocumentValidation result{};
            result.error = SceneDocumentError::InvalidPlaceableIdentity;
            result.placeableIndex = index;
            return result;
        }
        if (!placeableUuids.insert(instance.uuid).second)
        {
            SceneDocumentValidation result{};
            result.error = SceneDocumentError::DuplicatePlaceableUuid;
            result.placeableIndex = index;
            return result;
        }

        const engine::game::PlaceableTransformValidation validation =
            engine::game::validatePlaceableTransform(instance);
        if (!validation)
        {
            SceneDocumentValidation result{};
            result.error = SceneDocumentError::InvalidPlaceableTransform;
            result.placeableIndex = index;
            result.transformError = validation.error;
            return result;
        }
        config.placeables[index] = validation.canonical;
    }
    return {};
}

const char* sceneDocumentErrorLabel(SceneDocumentError error) noexcept
{
    switch (error)
    {
    case SceneDocumentError::None:
        return "none";
    case SceneDocumentError::PlaySnapshotActive:
        return "play snapshot active";
    case SceneDocumentError::InvalidPlaceableIdentity:
        return "invalid placeable identity";
    case SceneDocumentError::DuplicatePlaceableUuid:
        return "duplicate placeable UUID";
    case SceneDocumentError::InvalidPlaceableTransform:
        return "invalid placeable transform";
    }
    return "unknown scene document error";
}

} // namespace engine::editor
