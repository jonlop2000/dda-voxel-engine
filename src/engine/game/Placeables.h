#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace PlaceableRenderCapability
{
constexpr uint32_t VoxelVolume = 1u << 0;
constexpr uint32_t Mesh = 1u << 1;
constexpr uint32_t InstancedFoliage = 1u << 2;
} // namespace PlaceableRenderCapability

namespace PlaceableBehaviorCapability
{
constexpr uint32_t AnimatedFoliage = 1u << 0;
constexpr uint32_t GroundCoverPatch = 1u << 1;
} // namespace PlaceableBehaviorCapability

namespace PlaceableInteractionCapability
{
constexpr uint32_t Selectable = 1u << 0;
constexpr uint32_t Removable = 1u << 1;
constexpr uint32_t Pickupable = 1u << 2;
} // namespace PlaceableInteractionCapability

namespace PlaceableMaterialCategory
{
constexpr uint32_t Substrate = 1u << 0;
constexpr uint32_t Rock = 1u << 1;
constexpr uint32_t Decoration = 1u << 2;
} // namespace PlaceableMaterialCategory

struct PlaceableCapabilities
{
    uint32_t render = 0;
    uint32_t behavior = 0;
    uint32_t interaction = 0;
};

struct PlacementRules
{
    uint32_t allowedMaterialCategories = 0;
    float minWaterDepth = 0.05f;
    float maxSlopeDegrees = 35.0f;
    float footprintRadius = 0.5f;
};

struct PlacementHit
{
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    uint8_t materialId = 0;
    float waterDepth = 0.0f;
};

struct PlacementEvaluation
{
    bool valid = false;
    bool hasHit = false;
    PlacementHit hit{};
    bool footprintClear = false;
    std::string rejectReason{};
};

using PlaceableVoxelBuilder = std::vector<uint8_t> (*)(const glm::ivec3& dims,
                                                       uint32_t seed);

struct PlaceablePrototype
{
    std::string slug{};
    uint32_t version = 1;
    PlaceableCapabilities capabilities{};
    PlacementRules placement{};
    glm::ivec3 voxelDims{0};
    glm::vec3 voxelScale{1.0f};
    PlaceableVoxelBuilder buildVoxelVolume = nullptr;
};

struct PlaceableInstance
{
    std::string uuid{};
    std::string prototypeSlug{};
    uint32_t prototypeVersion = 1;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    uint32_t seed = 0;
};

struct PlaceablePreview
{
    bool active = false;
    std::string prototypeSlug{};
    uint32_t prototypeVersion = 1;
    uint32_t seed = 0;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    float footprintRadius = 0.0f;
    PlacementEvaluation evaluation{};
};

struct PlaceableEditCommand
{
    enum class Op
    {
        Add,
        Remove,
        Move,
        MigratePrototypeVersion,
    };

    Op op = Op::Add;
    std::optional<PlaceableInstance> before{};
    std::optional<PlaceableInstance> after{};
};

class PlaceableVoxelDataCache
{
public:
    const std::vector<uint8_t>& getOrBuild(const PlaceablePrototype& prototype,
                                           uint32_t seed)
    {
        const auto key = std::make_tuple(prototype.slug, prototype.version, seed);
        auto [it, inserted] = entries_.try_emplace(key);
        if (inserted)
        {
            if (prototype.buildVoxelVolume != nullptr)
            {
                it->second = prototype.buildVoxelVolume(prototype.voxelDims, seed);
            }

            const size_t expectedSize =
                static_cast<size_t>(prototype.voxelDims.x) *
                static_cast<size_t>(prototype.voxelDims.y) *
                static_cast<size_t>(prototype.voxelDims.z);
            if (it->second.size() != expectedSize)
            {
                it->second.assign(expectedSize, 0u);
            }
        }
        return it->second;
    }

    void clear() { entries_.clear(); }

private:
    std::map<std::tuple<std::string, uint32_t, uint32_t>, std::vector<uint8_t>> entries_{};
};
