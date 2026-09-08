#include "engine/game/PlaceableEditJournal.h"

#include <algorithm>
#include <unordered_set>

#include "engine/scene/SceneConfig.h"

namespace engine::game
{
namespace
{

bool vec3ExactlyEqual(const glm::vec3& lhs, const glm::vec3& rhs) noexcept
{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool quatExactlyEqual(const glm::quat& lhs, const glm::quat& rhs) noexcept
{
    return lhs.w == rhs.w && lhs.x == rhs.x && lhs.y == rhs.y &&
           lhs.z == rhs.z;
}

bool instanceExactlyEqual(const PlaceableInstance& lhs,
                          const PlaceableInstance& rhs) noexcept
{
    return lhs.uuid == rhs.uuid && lhs.prototypeSlug == rhs.prototypeSlug &&
           lhs.prototypeVersion == rhs.prototypeVersion &&
           vec3ExactlyEqual(lhs.position, rhs.position) &&
           quatExactlyEqual(lhs.rotation, rhs.rotation) &&
           vec3ExactlyEqual(lhs.scale, rhs.scale) && lhs.seed == rhs.seed;
}

bool canonicalInstanceExactlyEqual(const PlaceableInstance& lhs,
                                   const PlaceableInstance& rhs) noexcept
{
    if (lhs.uuid != rhs.uuid || lhs.prototypeSlug != rhs.prototypeSlug ||
        lhs.prototypeVersion != rhs.prototypeVersion || lhs.seed != rhs.seed)
    {
        return false;
    }

    const PlaceableTransformValidation left = validatePlaceableTransform(lhs);
    const PlaceableTransformValidation right = validatePlaceableTransform(rhs);
    return left && right &&
           vec3ExactlyEqual(left.canonical.position, right.canonical.position) &&
           quatExactlyEqual(left.canonical.rotation, right.canonical.rotation) &&
           vec3ExactlyEqual(left.canonical.scale, right.canonical.scale);
}

bool validIdentity(const PlaceableInstance& instance) noexcept
{
    return !instance.uuid.empty() && !instance.prototypeSlug.empty() &&
           instance.prototypeVersion != 0;
}

struct CollectionValidation
{
    PlaceableEditJournalError error = PlaceableEditJournalError::None;
    PlaceableTransformError transformError = PlaceableTransformError::None;
};

CollectionValidation validateCollection(
    std::span<const PlaceableInstance> placeables)
{
    std::unordered_set<std::string> uuids{};
    uuids.reserve(placeables.size());
    for (const PlaceableInstance& instance : placeables)
    {
        if (!validIdentity(instance))
        {
            return {PlaceableEditJournalError::InvalidIdentity,
                    PlaceableTransformError::None};
        }
        if (!uuids.insert(instance.uuid).second)
        {
            return {PlaceableEditJournalError::DuplicateUuid,
                    PlaceableTransformError::None};
        }

        const PlaceableTransformValidation transform =
            validatePlaceableTransform(instance);
        if (!transform)
        {
            return {PlaceableEditJournalError::InvalidTransform,
                    transform.error};
        }
    }
    return {};
}

std::vector<PlaceableInstance>::iterator findByUuid(
    std::vector<PlaceableInstance>& placeables, const std::string& uuid)
{
    return std::find_if(placeables.begin(), placeables.end(),
                        [&](const PlaceableInstance& instance) {
                            return instance.uuid == uuid;
                        });
}

bool commandShapeValid(const PlaceableEditCommand& command) noexcept
{
    switch (command.op)
    {
    case PlaceableEditCommand::Op::Add:
        return !command.before.has_value() && command.after.has_value();
    case PlaceableEditCommand::Op::Remove:
        return command.before.has_value() && !command.after.has_value();
    case PlaceableEditCommand::Op::Move:
    case PlaceableEditCommand::Op::MigratePrototypeVersion:
        return command.before.has_value() && command.after.has_value();
    }
    return false;
}

CollectionValidation canonicalizeCommandInstance(
    const PlaceableInstance& input, PlaceableInstance& output)
{
    if (!validIdentity(input))
    {
        return {PlaceableEditJournalError::InvalidIdentity,
                PlaceableTransformError::None};
    }

    const PlaceableTransformValidation transform =
        validatePlaceableTransform(input);
    if (!transform)
    {
        return {PlaceableEditJournalError::InvalidTransform,
                transform.error};
    }
    output = transform.canonical;
    return {};
}

CollectionValidation materializeImplicitDefaults(
    PlaceableCollectionSnapshot& snapshot,
    std::span<const PlaceableInstance> implicitDefaults)
{
    if (!snapshot.useDefaultPlaceables)
    {
        return {};
    }

    if (snapshot.placeables.empty())
    {
        if (implicitDefaults.empty())
        {
            return {PlaceableEditJournalError::ImplicitDefaultsUnavailable,
                    PlaceableTransformError::None};
        }
        const CollectionValidation defaultsValidation =
            validateCollection(implicitDefaults);
        if (defaultsValidation.error != PlaceableEditJournalError::None)
        {
            return defaultsValidation;
        }
        snapshot.placeables.assign(implicitDefaults.begin(), implicitDefaults.end());
    }
    snapshot.useDefaultPlaceables = false;
    return {};
}

CollectionValidation applyCommand(PlaceableCollectionSnapshot& snapshot,
                                  PlaceableEditCommand& command)
{
    if (!commandShapeValid(command))
    {
        return {PlaceableEditJournalError::InvalidCommandShape,
                PlaceableTransformError::None};
    }

    switch (command.op)
    {
    case PlaceableEditCommand::Op::Add:
    {
        PlaceableInstance after{};
        const CollectionValidation validation =
            canonicalizeCommandInstance(*command.after, after);
        if (validation.error != PlaceableEditJournalError::None)
        {
            return validation;
        }
        if (findByUuid(snapshot.placeables, after.uuid) != snapshot.placeables.end())
        {
            return {PlaceableEditJournalError::TargetAlreadyExists,
                    PlaceableTransformError::None};
        }
        command.after = after;
        snapshot.placeables.push_back(std::move(after));
        return {};
    }
    case PlaceableEditCommand::Op::Remove:
    {
        PlaceableInstance before{};
        const CollectionValidation validation =
            canonicalizeCommandInstance(*command.before, before);
        if (validation.error != PlaceableEditJournalError::None)
        {
            return validation;
        }
        const auto target = findByUuid(snapshot.placeables, before.uuid);
        if (target == snapshot.placeables.end())
        {
            return {PlaceableEditJournalError::TargetNotFound,
                    PlaceableTransformError::None};
        }
        if (!canonicalInstanceExactlyEqual(*target, before))
        {
            return {PlaceableEditJournalError::StaleBeforeState,
                    PlaceableTransformError::None};
        }
        command.before = before;
        snapshot.placeables.erase(target);
        return {};
    }
    case PlaceableEditCommand::Op::Move:
    case PlaceableEditCommand::Op::MigratePrototypeVersion:
    {
        PlaceableInstance before{};
        PlaceableInstance after{};
        CollectionValidation validation =
            canonicalizeCommandInstance(*command.before, before);
        if (validation.error != PlaceableEditJournalError::None)
        {
            return validation;
        }
        validation = canonicalizeCommandInstance(*command.after, after);
        if (validation.error != PlaceableEditJournalError::None)
        {
            return validation;
        }

        if (before.uuid != after.uuid || before.seed != after.seed)
        {
            return {PlaceableEditJournalError::InvalidIdentity,
                    PlaceableTransformError::None};
        }
        if (command.op == PlaceableEditCommand::Op::Move &&
            (before.prototypeSlug != after.prototypeSlug ||
             before.prototypeVersion != after.prototypeVersion))
        {
            return {PlaceableEditJournalError::InvalidIdentity,
                    PlaceableTransformError::None};
        }
        if (command.op == PlaceableEditCommand::Op::MigratePrototypeVersion &&
            before.prototypeSlug != after.prototypeSlug)
        {
            return {PlaceableEditJournalError::InvalidIdentity,
                    PlaceableTransformError::None};
        }

        const auto target = findByUuid(snapshot.placeables, before.uuid);
        if (target == snapshot.placeables.end())
        {
            return {PlaceableEditJournalError::TargetNotFound,
                    PlaceableTransformError::None};
        }
        if (!canonicalInstanceExactlyEqual(*target, before))
        {
            return {PlaceableEditJournalError::StaleBeforeState,
                    PlaceableTransformError::None};
        }

        command.before = before;
        command.after = after;
        *target = std::move(after);
        return {};
    }
    }
    return {PlaceableEditJournalError::InvalidCommandShape,
            PlaceableTransformError::None};
}

} // namespace

PlaceableCollectionSnapshot capturePlaceableCollection(
    const SceneConfig& sceneConfig)
{
    PlaceableCollectionSnapshot snapshot{};
    snapshot.useDefaultPlaceables = sceneConfig.useDefaultPlaceables;
    snapshot.placeables = sceneConfig.placeables;
    return snapshot;
}

bool placeableCollectionExactlyEqual(
    const PlaceableCollectionSnapshot& lhs,
    const PlaceableCollectionSnapshot& rhs) noexcept
{
    if (lhs.useDefaultPlaceables != rhs.useDefaultPlaceables ||
        lhs.placeables.size() != rhs.placeables.size())
    {
        return false;
    }
    for (size_t i = 0; i < lhs.placeables.size(); ++i)
    {
        if (!instanceExactlyEqual(lhs.placeables[i], rhs.placeables[i]))
        {
            return false;
        }
    }
    return true;
}

void applyPlaceableCollectionSnapshot(
    SceneConfig& sceneConfig, const PlaceableCollectionSnapshot& snapshot)
{
    sceneConfig.useDefaultPlaceables = snapshot.useDefaultPlaceables;
    sceneConfig.placeables = snapshot.placeables;
}

PreparedPlaceableEdit PlaceableEditJournal::beginExecute(
    const SceneConfig& sceneConfig, const PlaceableEditCommand& requestedCommand,
    std::span<const PlaceableInstance> implicitDefaults)
{
    if (activeEdit_)
    {
        return reject(PlaceableEditJournalError::PendingEditActive);
    }

    PreparedPlaceableEdit edit{};
    edit.action = PlaceableEditJournalAction::Execute;
    edit.command = requestedCommand;
    edit.before = capturePlaceableCollection(sceneConfig);

    const CollectionValidation currentValidation =
        validateCollection(edit.before.placeables);
    if (currentValidation.error != PlaceableEditJournalError::None)
    {
        return reject(currentValidation.error,
                      currentValidation.transformError);
    }

    edit.after = edit.before;
    const CollectionValidation materialization =
        materializeImplicitDefaults(edit.after, implicitDefaults);
    if (materialization.error != PlaceableEditJournalError::None)
    {
        return reject(materialization.error,
                      materialization.transformError);
    }

    const CollectionValidation commandResult =
        applyCommand(edit.after, edit.command);
    if (commandResult.error != PlaceableEditJournalError::None)
    {
        return reject(commandResult.error, commandResult.transformError);
    }

    const CollectionValidation resultValidation =
        validateCollection(edit.after.placeables);
    if (resultValidation.error != PlaceableEditJournalError::None)
    {
        return reject(resultValidation.error,
                      resultValidation.transformError);
    }
    if (placeableCollectionExactlyEqual(edit.before, edit.after))
    {
        return reject(PlaceableEditJournalError::NoChange);
    }
    return activate(std::move(edit));
}

PreparedPlaceableEdit PlaceableEditJournal::beginUndo(
    const SceneConfig& sceneConfig)
{
    if (activeEdit_)
    {
        return reject(PlaceableEditJournalError::PendingEditActive);
    }
    if (!canUndo())
    {
        return reject(PlaceableEditJournalError::NoUndo);
    }

    const HistoryEntry& entry = history_[cursor_ - 1];
    const PlaceableCollectionSnapshot current =
        capturePlaceableCollection(sceneConfig);
    if (!placeableCollectionExactlyEqual(current, entry.after))
    {
        return reject(PlaceableEditJournalError::SceneCollectionMismatch);
    }

    PreparedPlaceableEdit edit{};
    edit.action = PlaceableEditJournalAction::Undo;
    edit.command = entry.command;
    edit.before = current;
    edit.after = entry.before;
    return activate(std::move(edit));
}

PreparedPlaceableEdit PlaceableEditJournal::beginRedo(
    const SceneConfig& sceneConfig)
{
    if (activeEdit_)
    {
        return reject(PlaceableEditJournalError::PendingEditActive);
    }
    if (!canRedo())
    {
        return reject(PlaceableEditJournalError::NoRedo);
    }

    const HistoryEntry& entry = history_[cursor_];
    const PlaceableCollectionSnapshot current =
        capturePlaceableCollection(sceneConfig);
    if (!placeableCollectionExactlyEqual(current, entry.before))
    {
        return reject(PlaceableEditJournalError::SceneCollectionMismatch);
    }

    PreparedPlaceableEdit edit{};
    edit.action = PlaceableEditJournalAction::Redo;
    edit.command = entry.command;
    edit.before = current;
    edit.after = entry.after;
    return activate(std::move(edit));
}

bool PlaceableEditJournal::apply(SceneConfig& sceneConfig,
                                 const PreparedPlaceableEdit& edit) const
{
    const PreparedPlaceableEdit* authoritative = active(edit);
    if (authoritative == nullptr ||
        !placeableCollectionExactlyEqual(capturePlaceableCollection(sceneConfig),
                                         authoritative->before))
    {
        return false;
    }
    applyPlaceableCollectionSnapshot(sceneConfig, authoritative->after);
    return true;
}

bool PlaceableEditJournal::restore(SceneConfig& sceneConfig,
                                   const PreparedPlaceableEdit& edit) const
{
    const PreparedPlaceableEdit* authoritative = active(edit);
    if (authoritative == nullptr)
    {
        return false;
    }

    const PlaceableCollectionSnapshot current =
        capturePlaceableCollection(sceneConfig);
    if (placeableCollectionExactlyEqual(current, authoritative->before))
    {
        return true;
    }
    if (!placeableCollectionExactlyEqual(current, authoritative->after))
    {
        return false;
    }
    applyPlaceableCollectionSnapshot(sceneConfig, authoritative->before);
    return true;
}

bool PlaceableEditJournal::commit(
    const PreparedPlaceableEdit& edit,
    const SceneConfig& appliedSceneConfig)
{
    const PreparedPlaceableEdit* authoritativePointer = active(edit);
    if (authoritativePointer == nullptr ||
        !placeableCollectionExactlyEqual(
            capturePlaceableCollection(appliedSceneConfig),
            authoritativePointer->after))
    {
        return false;
    }
    const PreparedPlaceableEdit authoritative = *authoritativePointer;

    switch (authoritative.action)
    {
    case PlaceableEditJournalAction::Execute:
        if (cursor_ < history_.size())
        {
            if (savedCursor_ && *savedCursor_ > cursor_)
            {
                savedCursor_.reset();
            }
            history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                           history_.end());
        }
        history_.push_back(
            {authoritative.command, authoritative.before, authoritative.after});
        ++cursor_;
        break;
    case PlaceableEditJournalAction::Undo:
        if (cursor_ == 0 ||
            !placeableCollectionExactlyEqual(history_[cursor_ - 1].after,
                                             authoritative.before) ||
            !placeableCollectionExactlyEqual(history_[cursor_ - 1].before,
                                             authoritative.after))
        {
            return false;
        }
        --cursor_;
        break;
    case PlaceableEditJournalAction::Redo:
        if (cursor_ >= history_.size() ||
            !placeableCollectionExactlyEqual(history_[cursor_].before,
                                             authoritative.before) ||
            !placeableCollectionExactlyEqual(history_[cursor_].after,
                                             authoritative.after))
        {
            return false;
        }
        ++cursor_;
        break;
    }

    activeEdit_.reset();
    return true;
}

bool PlaceableEditJournal::cancel(const PreparedPlaceableEdit& edit) noexcept
{
    if (active(edit) == nullptr)
    {
        return false;
    }
    activeEdit_.reset();
    return true;
}

void PlaceableEditJournal::reset() noexcept
{
    history_.clear();
    cursor_ = 0;
    savedCursor_ = 0;
    activeEdit_.reset();
}

void PlaceableEditJournal::markSaved() noexcept
{
    if (!activeEdit_)
    {
        savedCursor_ = cursor_;
    }
}

PreparedPlaceableEdit PlaceableEditJournal::reject(
    PlaceableEditJournalError error,
    PlaceableTransformError transformError) const
{
    PreparedPlaceableEdit edit{};
    edit.error = error;
    edit.transformError = transformError;
    return edit;
}

PreparedPlaceableEdit PlaceableEditJournal::activate(
    PreparedPlaceableEdit edit)
{
    edit.valid = true;
    edit.error = PlaceableEditJournalError::None;
    edit.ticket = nextTicket_++;
    if (nextTicket_ == 0)
    {
        ++nextTicket_;
    }
    activeEdit_ = edit;
    return edit;
}

const PreparedPlaceableEdit* PlaceableEditJournal::active(
    const PreparedPlaceableEdit& edit) const noexcept
{
    if (!edit.valid || edit.ticket == 0 || !activeEdit_ ||
        activeEdit_->ticket != edit.ticket)
    {
        return nullptr;
    }
    return &*activeEdit_;
}

const char* placeableEditJournalErrorLabel(
    PlaceableEditJournalError error) noexcept
{
    switch (error)
    {
    case PlaceableEditJournalError::None:
        return "none";
    case PlaceableEditJournalError::PendingEditActive:
        return "pending edit active";
    case PlaceableEditJournalError::NoUndo:
        return "no undo";
    case PlaceableEditJournalError::NoRedo:
        return "no redo";
    case PlaceableEditJournalError::InvalidCommandShape:
        return "invalid command shape";
    case PlaceableEditJournalError::InvalidIdentity:
        return "invalid identity";
    case PlaceableEditJournalError::InvalidTransform:
        return "invalid transform";
    case PlaceableEditJournalError::DuplicateUuid:
        return "duplicate UUID";
    case PlaceableEditJournalError::ImplicitDefaultsUnavailable:
        return "implicit defaults unavailable";
    case PlaceableEditJournalError::TargetNotFound:
        return "target not found";
    case PlaceableEditJournalError::TargetAlreadyExists:
        return "target already exists";
    case PlaceableEditJournalError::StaleBeforeState:
        return "stale before state";
    case PlaceableEditJournalError::SceneCollectionMismatch:
        return "scene collection mismatch";
    case PlaceableEditJournalError::NoChange:
        return "no change";
    case PlaceableEditJournalError::InvalidTicket:
        return "invalid ticket";
    }
    return "unknown journal error";
}

} // namespace engine::game
