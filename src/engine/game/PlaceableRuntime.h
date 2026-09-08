#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "engine/game/PlaceableEditJournal.h"
#include "engine/game/Placeables.h"

struct SceneConfig;

namespace engine::game
{

// owns the transactional state and command mechanics for placeable editing.
class PlaceableRuntime
{
public:
    struct EditEffects
    {
        bool sceneRebuildRequired = false;
        bool clearPreview = false;
        bool placeablesChanged = false;
    };

    struct ApplyResult
    {
        bool accepted = false;
        EditEffects effects{};
    };

    enum class CompletionAction
    {
        None,
        PushUndo,
        PopUndo,
        AdvanceRedo,
    };

    struct PendingEdit
    {
        bool valid = false;
        PlaceableEditCommand command{};
        bool undo = false;
        bool redo = false;
        CompletionAction completion = CompletionAction::None;
        std::string successStatus{};
        std::optional<PlaceableInstance> affectedPlaceable{};
    };

    PendingEdit beginCommitPreview(const PlaceablePreview& preview);
    PendingEdit beginRemoveAtPreview(const SceneConfig& sceneConfig,
                                     const PlaceablePreview& preview);
    PendingEdit beginUndoLastEdit();
    PendingEdit beginRedoLastEdit();
    ApplyResult applyEditCommand(SceneConfig& sceneConfig,
                                 const PlaceableEditCommand& command,
                                 bool undo, bool redo = false);
    bool completeEdit(const PendingEdit& edit, SceneConfig& sceneConfig);
    bool completeExternalEdit(const PlaceableEditCommand& command,
                              std::string successStatus,
                              SceneConfig& sceneConfig);
    void failEditRebuild();

    PlaceableInstance createInstance(std::string prototypeSlug,
                                     uint32_t prototypeVersion,
                                     const glm::vec3& position);
    bool findAtPreview(const SceneConfig& sceneConfig,
                       const PlaceablePreview& preview,
                       PlaceableInstance& outInstance) const;

    void resetEdits();
    void markSceneSaved();
    void setEditStatus(std::string status);
    bool beginIsolatedEditSession();
    bool endIsolatedEditSession();

    bool hasUndo() const { return editJournal_.canUndo(); }
    bool hasRedo() const { return editJournal_.canRedo(); }
    bool sceneDirty() const { return editJournal_.dirty(); }
    const std::string& editStatus() const { return editStatus_; }

private:
    struct EditSessionState
    {
        PlaceableEditJournal journal{};
        uint64_t serial = 0;
        std::string status{};
    };

    static uint64_t mixPlaceableBits(uint64_t value);
    static std::string makePlaceableUuid(uint64_t serial);
    static uint32_t seedForPlaceableInstance(const std::string& uuid,
                                             const glm::vec3& position,
                                             uint64_t serial);
    bool rejectPendingCompletion(SceneConfig& sceneConfig);

    PlaceableEditJournal editJournal_{};
    std::optional<PreparedPlaceableEdit> pendingJournalEdit_{};
    uint64_t editSerial_ = 0;
    std::string editStatus_{};
    std::optional<EditSessionState> suspendedEditSession_{};
};

} // namespace engine::game
