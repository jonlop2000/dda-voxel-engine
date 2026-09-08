#pragma once

#include <cstddef>
#include <limits>
#include <optional>

#include "engine/game/PlaceableTransform.h"
#include "engine/scene/SceneConfig.h"

namespace engine::editor
{

enum class SceneDocumentError
{
    None,
    PlaySnapshotActive,
    InvalidPlaceableIdentity,
    DuplicatePlaceableUuid,
    InvalidPlaceableTransform,
};

struct SceneDocumentValidation
{
    static constexpr size_t kNoPlaceable =
        std::numeric_limits<size_t>::max();

    SceneDocumentError error = SceneDocumentError::None;
    size_t placeableIndex = kNoPlaceable;
    engine::game::PlaceableTransformError transformError =
        engine::game::PlaceableTransformError::None;

    [[nodiscard]] bool valid() const noexcept
    {
        return error == SceneDocumentError::None;
    }

    explicit operator bool() const noexcept { return valid(); }
};

// pure cpu document boundary. the authored configuration is never handed to a
// running preview by mutable reference; play always receives a disposable deep
// copy and stopping play returns the unchanged canonical authored value.
class SceneDocument
{
public:
    SceneDocument() = default;
    explicit SceneDocument(SceneConfig authoredConfig);

    [[nodiscard]] SceneDocumentValidation replaceAuthoredConfig(
        SceneConfig authoredConfig);

    [[nodiscard]] const SceneConfig& authoredConfig() const noexcept
    {
        return authoredConfig_;
    }

    [[nodiscard]] bool playSnapshotActive() const noexcept
    {
        return playSnapshot_.has_value();
    }

    // returns false rather than silently replacing an already-running session.
    [[nodiscard]] bool beginPlaySnapshot();

    [[nodiscard]] const SceneConfig* playSnapshot() const noexcept;
    [[nodiscard]] SceneConfig* mutablePlaySnapshot() noexcept;

    // discards every play mutation and returns a copy of the exact canonical
    // authored state that should be projected back into the runtime.
    [[nodiscard]] SceneConfig endPlayAndRestoreAuthored();

private:
    static SceneDocumentValidation canonicalizePlaceables(
        SceneConfig& config);

    SceneConfig authoredConfig_{};
    std::optional<SceneConfig> playSnapshot_{};
};

[[nodiscard]] const char* sceneDocumentErrorLabel(
    SceneDocumentError error) noexcept;

} // namespace engine::editor
