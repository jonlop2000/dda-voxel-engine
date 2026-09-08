#include "engine/game/PlaceableRuntime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

#include "engine/game/FoliageCatalog.h"
#include "engine/scene/SceneConfig.h"

namespace engine::game
{
namespace
{

bool samePlaceable(const std::optional<PlaceableInstance>& lhs,
                   const std::optional<PlaceableInstance>& rhs)
{
    if (lhs.has_value() != rhs.has_value())
    {
        return false;
    }
    if (!lhs)
    {
        return true;
    }
    return lhs->uuid == rhs->uuid && lhs->prototypeSlug == rhs->prototypeSlug &&
           lhs->prototypeVersion == rhs->prototypeVersion && lhs->seed == rhs->seed &&
           placeableTransformEquivalent(*lhs, *rhs);
}

bool sameCommand(const PlaceableEditCommand& lhs, const PlaceableEditCommand& rhs)
{
    return lhs.op == rhs.op && samePlaceable(lhs.before, rhs.before) &&
           samePlaceable(lhs.after, rhs.after);
}

} // namespace

PlaceableRuntime::PendingEdit PlaceableRuntime::beginCommitPreview(
    const PlaceablePreview& preview)
{
    if (!preview.active || !preview.evaluation.valid)
    {
        editStatus_ = preview.evaluation.rejectReason.empty()
                          ? "No valid foliage preview to place."
                          : "Cannot place: " + preview.evaluation.rejectReason;
        return {};
    }

    PlaceableInstance instance =
        createInstance(preview.prototypeSlug, preview.prototypeVersion,
                       preview.evaluation.hit.position);

    PendingEdit edit{};
    edit.valid = true;
    edit.command.op = PlaceableEditCommand::Op::Add;
    edit.command.after = instance;
    edit.completion = CompletionAction::PushUndo;
    edit.successStatus = "Placed " + instance.prototypeSlug + ".";
    edit.affectedPlaceable = instance;
    return edit;
}

PlaceableRuntime::PendingEdit PlaceableRuntime::beginRemoveAtPreview(
    const SceneConfig& sceneConfig, const PlaceablePreview& preview)
{
    PlaceableInstance target{};
    if (!findAtPreview(sceneConfig, preview, target))
    {
        editStatus_ = "No foliage under the preview footprint.";
        return {};
    }

    PendingEdit edit{};
    edit.valid = true;
    edit.command.op = PlaceableEditCommand::Op::Remove;
    edit.command.before = target;
    edit.completion = CompletionAction::PushUndo;
    edit.successStatus = "Removed " + target.prototypeSlug + ".";
    edit.affectedPlaceable = target;
    return edit;
}

PlaceableRuntime::PendingEdit PlaceableRuntime::beginUndoLastEdit()
{
    const PlaceableEditCommand* command = editJournal_.nextUndoCommand();
    if (command == nullptr)
    {
        editStatus_ = "No placeable edit to undo.";
        return {};
    }

    PendingEdit edit{};
    edit.valid = true;
    edit.command = *command;
    edit.undo = true;
    edit.completion = CompletionAction::PopUndo;
    edit.successStatus = "Undid last placeable edit.";
    return edit;
}

PlaceableRuntime::PendingEdit PlaceableRuntime::beginRedoLastEdit()
{
    const PlaceableEditCommand* command = editJournal_.nextRedoCommand();
    if (command == nullptr)
    {
        editStatus_ = "No placeable edit to redo.";
        return {};
    }

    PendingEdit edit{};
    edit.valid = true;
    edit.command = *command;
    edit.redo = true;
    edit.completion = CompletionAction::AdvanceRedo;
    edit.successStatus = "Redid last placeable edit.";
    return edit;
}

PlaceableRuntime::ApplyResult PlaceableRuntime::applyEditCommand(
    SceneConfig& sceneConfig, const PlaceableEditCommand& command, bool undo,
    bool redo)
{
    if (!sceneConfig.loadAquariumTest)
    {
        editStatus_ = "Placeable edits require an aquarium scene.";
        return {};
    }
    if (undo && redo)
    {
        editStatus_ = "A placeable edit cannot be both undo and redo.";
        return {};
    }

    PreparedPlaceableEdit prepared{};
    if (redo)
    {
        prepared = editJournal_.beginRedo(sceneConfig);
    }
    else if (undo)
    {
        prepared = editJournal_.beginUndo(sceneConfig);
    }
    else
    {
        prepared = editJournal_.beginExecute(
            sceneConfig, command,
            FoliageCatalog::defaultAquariumHeroFoliageInstances());
    }
    if (!prepared.valid)
    {
        editStatus_ = std::string("Placeable edit rejected: ") +
                      placeableEditJournalErrorLabel(prepared.error) + ".";
        return {};
    }
    if ((undo || redo) && !sameCommand(prepared.command, command))
    {
        (void)editJournal_.cancel(prepared);
        editStatus_ = "Placeable edit rejected: history command mismatch.";
        return {};
    }
    if (!editJournal_.apply(sceneConfig, prepared))
    {
        (void)editJournal_.cancel(prepared);
        editStatus_ = "Placeable edit rejected: scene changed during apply.";
        return {};
    }
    pendingJournalEdit_ = prepared;

    ApplyResult result{};
    result.accepted = true;
    result.effects.sceneRebuildRequired = true;
    result.effects.clearPreview = true;
    result.effects.placeablesChanged = true;
    return result;
}

bool PlaceableRuntime::completeEdit(const PendingEdit& edit,
                                    SceneConfig& sceneConfig)
{
    if (!pendingJournalEdit_)
    {
        editStatus_ = "Placeable edit failed final validation.";
        return false;
    }
    if (!edit.valid ||
        !sameCommand(edit.command, pendingJournalEdit_->command) ||
        edit.undo != (pendingJournalEdit_->action == PlaceableEditJournalAction::Undo) ||
        edit.redo != (pendingJournalEdit_->action == PlaceableEditJournalAction::Redo))
    {
        return rejectPendingCompletion(sceneConfig);
    }
    if (!editJournal_.commit(*pendingJournalEdit_, sceneConfig))
    {
        return rejectPendingCompletion(sceneConfig);
    }
    pendingJournalEdit_.reset();
    editStatus_ = edit.successStatus;
    return true;
}

bool PlaceableRuntime::completeExternalEdit(const PlaceableEditCommand& command,
                                            std::string successStatus,
                                            SceneConfig& sceneConfig)
{
    if (!pendingJournalEdit_ || !sameCommand(pendingJournalEdit_->command, command) ||
        !editJournal_.commit(*pendingJournalEdit_, sceneConfig))
    {
        return rejectPendingCompletion(sceneConfig);
    }
    pendingJournalEdit_.reset();
    editStatus_ = std::move(successStatus);
    return true;
}

void PlaceableRuntime::failEditRebuild()
{
    if (pendingJournalEdit_)
    {
        (void)editJournal_.cancel(*pendingJournalEdit_);
        pendingJournalEdit_.reset();
    }
    editStatus_ = "Placeable edit failed; scene was restored.";
}

PlaceableInstance PlaceableRuntime::createInstance(std::string prototypeSlug,
                                                   uint32_t prototypeVersion,
                                                   const glm::vec3& position)
{
    ++editSerial_;

    PlaceableInstance instance{};
    instance.uuid = makePlaceableUuid(editSerial_);
    instance.prototypeSlug = std::move(prototypeSlug);
    instance.prototypeVersion = prototypeVersion;
    instance.position = position;
    instance.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    instance.scale = glm::vec3(1.0f);
    instance.seed = seedForPlaceableInstance(instance.uuid, instance.position,
                                              editSerial_);
    return instance;
}

bool PlaceableRuntime::findAtPreview(const SceneConfig& sceneConfig,
                                     const PlaceablePreview& preview,
                                     PlaceableInstance& outInstance) const
{
    if (!sceneConfig.loadAquariumTest || !preview.evaluation.hasHit)
    {
        return false;
    }

    const glm::vec3 hit = preview.evaluation.hit.position;
    const std::vector<PlaceableInstance> instances =
        FoliageCatalog::aquariumHeroFoliageInstances(&sceneConfig.placeables,
                                                     sceneConfig.useDefaultPlaceables);

    bool found = false;
    float bestDistSq = std::numeric_limits<float>::max();
    PlaceableInstance best{};
    for (const PlaceableInstance& instance : instances)
    {
        const FoliagePrototype* prototype = FoliageCatalog::findPrototype(
            instance.prototypeSlug, instance.prototypeVersion);
        const float radius = prototype != nullptr
                                 ? prototype->placeable.placement.footprintRadius
                                 : 1.5f;
        const float selectRadius = std::max(radius, 1.0f);
        const float dx = hit.x - instance.position.x;
        const float dz = hit.z - instance.position.z;
        const float distSq = dx * dx + dz * dz;
        if (distSq <= selectRadius * selectRadius && distSq < bestDistSq)
        {
            best = instance;
            bestDistSq = distSq;
            found = true;
        }
    }

    if (!found)
    {
        return false;
    }

    outInstance = best;
    return true;
}

void PlaceableRuntime::resetEdits()
{
    editJournal_.reset();
    pendingJournalEdit_.reset();
    editStatus_.clear();
}

void PlaceableRuntime::markSceneSaved()
{
    editJournal_.markSaved();
}

void PlaceableRuntime::setEditStatus(std::string status)
{
    editStatus_ = std::move(status);
}

bool PlaceableRuntime::beginIsolatedEditSession()
{
    if (suspendedEditSession_ || pendingJournalEdit_ ||
        editJournal_.hasPendingEdit())
    {
        return false;
    }

    suspendedEditSession_.emplace(
        EditSessionState{editJournal_, editSerial_, editStatus_});
    editJournal_.reset();
    editSerial_ = 0;
    editStatus_.clear();
    return true;
}

bool PlaceableRuntime::endIsolatedEditSession()
{
    if (!suspendedEditSession_ || pendingJournalEdit_ ||
        editJournal_.hasPendingEdit())
    {
        return false;
    }

    editJournal_ = std::move(suspendedEditSession_->journal);
    editSerial_ = suspendedEditSession_->serial;
    editStatus_ = std::move(suspendedEditSession_->status);
    suspendedEditSession_.reset();
    return true;
}

bool PlaceableRuntime::rejectPendingCompletion(SceneConfig& sceneConfig)
{
    if (pendingJournalEdit_)
    {
        // a command mismatch can be detected after its authoritative transition
        // has already projected successfully. roll that exact transition back,
        // but do not overwrite a collection another owner changed meanwhile.
        (void)editJournal_.restore(sceneConfig, *pendingJournalEdit_);
        (void)editJournal_.cancel(*pendingJournalEdit_);
        pendingJournalEdit_.reset();
    }
    editStatus_ = "Placeable edit failed final validation.";
    return false;
}

uint64_t PlaceableRuntime::mixPlaceableBits(uint64_t value)
{
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

std::string PlaceableRuntime::makePlaceableUuid(uint64_t serial)
{
    const uint64_t now =
        static_cast<uint64_t>(std::chrono::high_resolution_clock::now()
                                  .time_since_epoch()
                                  .count());
    const uint64_t a = mixPlaceableBits(now ^ (serial * 0x9e3779b97f4a7c15ull));
    const uint64_t b = mixPlaceableBits(a ^ 0xd1b54a32d192ed03ull);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::nouppercase;
    oss << std::setw(8) << static_cast<uint32_t>(a >> 32u) << "-";
    oss << std::setw(4) << static_cast<uint16_t>(a >> 16u) << "-";
    oss << std::setw(4) << static_cast<uint16_t>((a & 0x0fffull) | 0x4000ull) << "-";
    oss << std::setw(4)
        << static_cast<uint16_t>(((b >> 48u) & 0x3fffull) | 0x8000ull) << "-";
    oss << std::setw(12) << (b & 0x0000ffffffffffffull);
    return oss.str();
}

uint32_t PlaceableRuntime::seedForPlaceableInstance(const std::string& uuid,
                                                    const glm::vec3& position,
                                                    uint64_t serial)
{
    uint64_t hash = 1469598103934665603ull;
    auto append = [&hash](uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    for (const char c : uuid)
    {
        append(static_cast<uint8_t>(c));
    }
    append(static_cast<uint64_t>(
        static_cast<int64_t>(std::round(position.x * 1000.0f))));
    append(static_cast<uint64_t>(
        static_cast<int64_t>(std::round(position.y * 1000.0f))));
    append(static_cast<uint64_t>(
        static_cast<int64_t>(std::round(position.z * 1000.0f))));
    append(serial);
    return static_cast<uint32_t>(mixPlaceableBits(hash));
}

} // namespace engine::game
