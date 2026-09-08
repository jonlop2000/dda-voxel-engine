#include "engine/editor/PlaceableAuthoringCommands.h"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "engine/game/PlaceableTransform.h"

namespace engine::editor
{
namespace
{

constexpr size_t kMaxIdentityAttempts = 4096;

uint64_t mixBits(uint64_t value) noexcept
{
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

std::string formatUuid(uint64_t high, uint64_t low)
{
    std::ostringstream stream{};
    stream << std::hex << std::setfill('0') << std::nouppercase;
    stream << std::setw(8) << static_cast<uint32_t>(high >> 32u) << '-';
    stream << std::setw(4) << static_cast<uint16_t>(high >> 16u) << '-';
    stream << std::setw(4)
           << static_cast<uint16_t>((high & 0x0fffull) | 0x4000ull) << '-';
    stream << std::setw(4)
           << static_cast<uint16_t>(((low >> 48u) & 0x3fffull) | 0x8000ull)
           << '-';
    stream << std::setw(12) << (low & 0x0000ffffffffffffull);
    return stream.str();
}

std::optional<std::string> allocateIdentity(
    std::span<const PlaceableInstance> placeables, uint64_t entropy)
{
    std::unordered_set<std::string> occupied{};
    occupied.reserve(placeables.size());
    for (const PlaceableInstance& placeable : placeables)
    {
        occupied.insert(placeable.uuid);
    }

    for (size_t attempt = 0; attempt < kMaxIdentityAttempts; ++attempt)
    {
        const uint64_t high = mixBits(
            entropy ^ (static_cast<uint64_t>(attempt) *
                       0xd1b54a32d192ed03ull));
        const uint64_t low = mixBits(high ^ 0x94d049bb133111ebull);
        std::string candidate = formatUuid(high, low);
        if (!occupied.contains(candidate))
        {
            return candidate;
        }
    }
    return std::nullopt;
}

uint32_t seedFor(const PlaceableInstance& placeable, uint64_t entropy) noexcept
{
    uint64_t hash = mixBits(entropy ^ 0xcbf29ce484222325ull);
    for (const char character : placeable.uuid)
    {
        hash = mixBits(hash ^ static_cast<unsigned char>(character));
    }
    const auto appendPosition = [&hash](float value) {
        hash = mixBits(
            hash ^ static_cast<uint64_t>(std::bit_cast<uint32_t>(value)));
    };
    appendPosition(placeable.position.x);
    appendPosition(placeable.position.y);
    appendPosition(placeable.position.z);
    const uint32_t seed = static_cast<uint32_t>(mixBits(hash));
    return seed == 0 ? 1u : seed;
}

PlaceableAuthoringCommandResult reject(
    PlaceableAuthoringCommandError error,
    size_t invalidIndex = static_cast<size_t>(-1),
    TransformSnapError snapError = TransformSnapError::None,
    engine::game::PlaceableTransformError transformError =
        engine::game::PlaceableTransformError::None)
{
    PlaceableAuthoringCommandResult result{};
    result.error = error;
    result.invalidPlaceableIndex = invalidIndex;
    result.snapError = snapError;
    result.transformError = transformError;
    return result;
}

PlaceableAuthoringCommandResult validateCollection(
    std::span<const PlaceableInstance> placeables)
{
    std::unordered_set<std::string> identities{};
    identities.reserve(placeables.size());
    for (size_t index = 0; index < placeables.size(); ++index)
    {
        const PlaceableInstance& placeable = placeables[index];
        if (placeable.uuid.empty() || placeable.prototypeSlug.empty() ||
            placeable.prototypeVersion == 0)
        {
            return reject(
                PlaceableAuthoringCommandError::InvalidExistingIdentity, index);
        }
        if (!identities.insert(placeable.uuid).second)
        {
            return reject(
                PlaceableAuthoringCommandError::DuplicateExistingIdentity,
                index);
        }
        const engine::game::PlaceableTransformValidation transform =
            engine::game::validatePlaceableTransform(placeable);
        if (!transform)
        {
            return reject(
                PlaceableAuthoringCommandError::InvalidExistingTransform,
                index, TransformSnapError::None, transform.error);
        }
    }
    return {};
}

const PlaceableInstance* findByUuid(
    std::span<const PlaceableInstance> placeables, const std::string& uuid)
{
    const auto found = std::find_if(
        placeables.begin(), placeables.end(),
        [&](const PlaceableInstance& placeable) {
            return placeable.uuid == uuid;
        });
    return found == placeables.end() ? nullptr : &*found;
}

PlaceableAuthoringCommandResult addResult(PlaceableInstance instance)
{
    PlaceableEditCommand command{};
    command.op = PlaceableEditCommand::Op::Add;
    command.after = instance;

    PlaceableAuthoringCommandResult result{};
    result.command = std::move(command);
    result.affectedPlaceable = std::move(instance);
    return result;
}

} // namespace

PlaceableAuthoringCommandResult buildCreatePlaceableCommand(
    std::span<const PlaceableInstance> effectivePlaceables,
    const PlaceablePrototype& prototype,
    const PlaceableCreateRequest& request)
{
    const PlaceableAuthoringCommandResult collection =
        validateCollection(effectivePlaceables);
    if (collection.error != PlaceableAuthoringCommandError::None)
    {
        return collection;
    }
    if (prototype.slug.empty() || prototype.version == 0)
    {
        return reject(PlaceableAuthoringCommandError::InvalidPrototype);
    }
    if (!request.identityEntropy)
    {
        return reject(PlaceableAuthoringCommandError::MissingIdentityEntropy);
    }

    const std::optional<std::string> uuid =
        allocateIdentity(effectivePlaceables, *request.identityEntropy);
    if (!uuid)
    {
        return reject(PlaceableAuthoringCommandError::IdentityExhausted);
    }

    PlaceableInstance instance{};
    instance.uuid = *uuid;
    instance.prototypeSlug = prototype.slug;
    instance.prototypeVersion = prototype.version;
    instance.position = request.proposedTransform.translation;
    instance.rotation = request.proposedTransform.rotation;
    instance.scale = request.proposedTransform.scale;

    const PlaceableSnapResult snapped = snapPlaceableInstance(
        instance, placeableSnapProfile(request.surfaceY, request.snapScale),
        TransformChannel::All);
    if (!snapped)
    {
        return reject(
            PlaceableAuthoringCommandError::InvalidRequestedTransform,
            static_cast<size_t>(-1), snapped.error, snapped.transformError);
    }
    instance = snapped.canonical;
    instance.seed = seedFor(instance, *request.identityEntropy);
    return addResult(std::move(instance));
}

PlaceableAuthoringCommandResult buildDuplicatePlaceableCommand(
    std::span<const PlaceableInstance> effectivePlaceables,
    const PlaceableDuplicateRequest& request)
{
    if (request.sourceUuid.empty())
    {
        return reject(PlaceableAuthoringCommandError::InvalidTargetIdentity);
    }
    const PlaceableAuthoringCommandResult collection =
        validateCollection(effectivePlaceables);
    if (collection.error != PlaceableAuthoringCommandError::None)
    {
        return collection;
    }
    const PlaceableInstance* source =
        findByUuid(effectivePlaceables, request.sourceUuid);
    if (source == nullptr)
    {
        return reject(PlaceableAuthoringCommandError::TargetNotFound);
    }
    if (!request.identityEntropy)
    {
        return reject(PlaceableAuthoringCommandError::MissingIdentityEntropy);
    }

    const std::optional<std::string> uuid =
        allocateIdentity(effectivePlaceables, *request.identityEntropy);
    if (!uuid)
    {
        return reject(PlaceableAuthoringCommandError::IdentityExhausted);
    }

    PlaceableInstance duplicate = *source;
    duplicate.uuid = *uuid;
    duplicate.position += request.translationOffset;
    const PlaceableSnapResult snapped = snapPlaceableInstance(
        duplicate, placeableSnapProfile(request.surfaceY, false),
        TransformChannel::Translation);
    if (!snapped)
    {
        return reject(
            PlaceableAuthoringCommandError::InvalidRequestedTransform,
            static_cast<size_t>(-1), snapped.error, snapped.transformError);
    }
    return addResult(snapped.canonical);
}

PlaceableAuthoringCommandResult buildDeletePlaceableCommand(
    std::span<const PlaceableInstance> effectivePlaceables,
    const PlaceableDeleteRequest& request)
{
    if (request.targetUuid.empty())
    {
        return reject(PlaceableAuthoringCommandError::InvalidTargetIdentity);
    }
    const PlaceableAuthoringCommandResult collection =
        validateCollection(effectivePlaceables);
    if (collection.error != PlaceableAuthoringCommandError::None)
    {
        return collection;
    }
    const PlaceableInstance* target =
        findByUuid(effectivePlaceables, request.targetUuid);
    if (target == nullptr)
    {
        return reject(PlaceableAuthoringCommandError::TargetNotFound);
    }

    const engine::game::PlaceableTransformValidation canonical =
        engine::game::validatePlaceableTransform(*target);
    PlaceableEditCommand command{};
    command.op = PlaceableEditCommand::Op::Remove;
    command.before = canonical.canonical;

    PlaceableAuthoringCommandResult result{};
    result.command = std::move(command);
    result.affectedPlaceable = canonical.canonical;
    return result;
}

const char* placeableAuthoringCommandErrorLabel(
    PlaceableAuthoringCommandError error) noexcept
{
    switch (error)
    {
    case PlaceableAuthoringCommandError::None:
        return "none";
    case PlaceableAuthoringCommandError::InvalidExistingIdentity:
        return "invalid existing placeable identity";
    case PlaceableAuthoringCommandError::DuplicateExistingIdentity:
        return "duplicate existing placeable identity";
    case PlaceableAuthoringCommandError::InvalidExistingTransform:
        return "invalid existing placeable transform";
    case PlaceableAuthoringCommandError::InvalidPrototype:
        return "invalid placeable prototype";
    case PlaceableAuthoringCommandError::MissingIdentityEntropy:
        return "missing placeable identity entropy";
    case PlaceableAuthoringCommandError::InvalidTargetIdentity:
        return "invalid target identity";
    case PlaceableAuthoringCommandError::TargetNotFound:
        return "target not found";
    case PlaceableAuthoringCommandError::InvalidRequestedTransform:
        return "invalid requested transform";
    case PlaceableAuthoringCommandError::IdentityExhausted:
        return "placeable identity allocation exhausted";
    }
    return "unknown placeable authoring command error";
}

} // namespace engine::editor
