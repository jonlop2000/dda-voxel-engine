#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "engine/game/PlaceableTransform.h"

struct SceneConfig;

namespace engine::game
{

struct PlaceableCollectionSnapshot
{
    bool useDefaultPlaceables = true;
    std::vector<PlaceableInstance> placeables{};
};

[[nodiscard]] PlaceableCollectionSnapshot capturePlaceableCollection(
    const SceneConfig& sceneConfig);
[[nodiscard]] bool placeableCollectionExactlyEqual(
    const PlaceableCollectionSnapshot& lhs,
    const PlaceableCollectionSnapshot& rhs) noexcept;
void applyPlaceableCollectionSnapshot(
    SceneConfig& sceneConfig, const PlaceableCollectionSnapshot& snapshot);

enum class PlaceableEditJournalAction
{
    Execute,
    Undo,
    Redo,
};

enum class PlaceableEditJournalError
{
    None,
    PendingEditActive,
    NoUndo,
    NoRedo,
    InvalidCommandShape,
    InvalidIdentity,
    InvalidTransform,
    DuplicateUuid,
    ImplicitDefaultsUnavailable,
    TargetNotFound,
    TargetAlreadyExists,
    StaleBeforeState,
    SceneCollectionMismatch,
    NoChange,
    InvalidTicket,
};

struct PreparedPlaceableEdit
{
    bool valid = false;
    uint64_t ticket = 0;
    PlaceableEditJournalAction action = PlaceableEditJournalAction::Execute;
    PlaceableEditJournalError error = PlaceableEditJournalError::None;
    PlaceableTransformError transformError = PlaceableTransformError::None;
    PlaceableEditCommand command{};
    PlaceableCollectionSnapshot before{};
    PlaceableCollectionSnapshot after{};
};

// owns reversible placeable history, but never owns renderer state. begin* prepares
// an exact before/after collection transition. the caller applies it to a candidate
// SceneConfig, projects/rebuilds that candidate, and only then calls commit(). a
// failed projection can restore() and cancel() without advancing history.
class PlaceableEditJournal
{
public:
    [[nodiscard]] PreparedPlaceableEdit beginExecute(
        const SceneConfig& sceneConfig, const PlaceableEditCommand& command,
        std::span<const PlaceableInstance> implicitDefaults = {});
    [[nodiscard]] PreparedPlaceableEdit beginUndo(
        const SceneConfig& sceneConfig);
    [[nodiscard]] PreparedPlaceableEdit beginRedo(
        const SceneConfig& sceneConfig);

    [[nodiscard]] bool apply(SceneConfig& sceneConfig,
                             const PreparedPlaceableEdit& edit) const;
    [[nodiscard]] bool restore(SceneConfig& sceneConfig,
                               const PreparedPlaceableEdit& edit) const;
    [[nodiscard]] bool commit(const PreparedPlaceableEdit& edit,
                              const SceneConfig& appliedSceneConfig);
    [[nodiscard]] bool cancel(const PreparedPlaceableEdit& edit) noexcept;

    void reset() noexcept;
    void markSaved() noexcept;

    [[nodiscard]] bool hasPendingEdit() const noexcept
    {
        return activeEdit_.has_value();
    }
    [[nodiscard]] bool canUndo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept
    {
        return cursor_ < history_.size();
    }
    [[nodiscard]] const PlaceableEditCommand* nextUndoCommand() const noexcept
    {
        return canUndo() ? &history_[cursor_ - 1].command : nullptr;
    }
    [[nodiscard]] const PlaceableEditCommand* nextRedoCommand() const noexcept
    {
        return canRedo() ? &history_[cursor_].command : nullptr;
    }
    [[nodiscard]] bool dirty() const noexcept
    {
        return !savedCursor_.has_value() || cursor_ != *savedCursor_;
    }
    [[nodiscard]] size_t cursor() const noexcept { return cursor_; }
    [[nodiscard]] size_t size() const noexcept { return history_.size(); }

private:
    struct HistoryEntry
    {
        PlaceableEditCommand command{};
        PlaceableCollectionSnapshot before{};
        PlaceableCollectionSnapshot after{};
    };

    [[nodiscard]] PreparedPlaceableEdit reject(
        PlaceableEditJournalError error,
        PlaceableTransformError transformError =
            PlaceableTransformError::None) const;
    [[nodiscard]] PreparedPlaceableEdit activate(
        PreparedPlaceableEdit edit);
    [[nodiscard]] const PreparedPlaceableEdit* active(
        const PreparedPlaceableEdit& edit) const noexcept;

    std::vector<HistoryEntry> history_{};
    size_t cursor_ = 0;
    std::optional<size_t> savedCursor_{0};
    std::optional<PreparedPlaceableEdit> activeEdit_{};
    uint64_t nextTicket_ = 1;
};

[[nodiscard]] const char* placeableEditJournalErrorLabel(
    PlaceableEditJournalError error) noexcept;

} // namespace engine::game
