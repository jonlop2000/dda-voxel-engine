#include "engine/editor/SelectionPicking.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

namespace
{

using engine::editor::SelectionBounds;
using engine::editor::SelectionCandidate;
using engine::editor::SelectionPickError;
using engine::editor::SelectionRay;
using engine::editor::SelectionTarget;
using engine::editor::SelectionTargetKind;

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 1e-5f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

bool vec3NearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs,
                     float epsilon = 1e-5f)
{
    return nearlyEqual(lhs.x, rhs.x, epsilon) &&
           nearlyEqual(lhs.y, rhs.y, epsilon) &&
           nearlyEqual(lhs.z, rhs.z, epsilon);
}

glm::mat4 translated(const glm::vec3& position)
{
    return glm::translate(glm::mat4(1.0f), position);
}

SelectionCandidate candidateAt(SelectionTarget target,
                               const glm::vec3& position,
                               uint32_t priority = 0)
{
    SelectionCandidate candidate{};
    candidate.target = std::move(target);
    candidate.worldFromLocal = translated(position);
    candidate.priority = priority;
    return candidate;
}

const std::string& placeableUuid(const SelectionTarget& target)
{
    return std::get<engine::editor::PlaceableSelection>(target.value).uuid;
}

void testTypedStableIdentity()
{
    const SelectionTarget terrainA =
        SelectionTarget::terrainCell("main-terrain", {2, -1, 7});
    const SelectionTarget terrainACopy =
        SelectionTarget::terrainCell("main-terrain", {2, -1, 7});
    const SelectionTarget terrainOtherGrid =
        SelectionTarget::terrainCell("interior-terrain", {2, -1, 7});
    const SelectionTarget volume =
        SelectionTarget::voxelVolume(12u, 3u);
    const SelectionTarget placeable =
        SelectionTarget::placeable("placeable-42");
    const SelectionTarget rebuiltVolume =
        SelectionTarget::voxelVolume(13u, 3u);

    require(terrainA.valid() && volume.valid() && placeable.valid(),
            "Non-empty typed persistent identities should validate");
    require(terrainA.kind() == SelectionTargetKind::TerrainCell &&
                volume.kind() == SelectionTargetKind::VoxelVolume &&
                placeable.kind() == SelectionTargetKind::Placeable,
            "Selection identity factories should retain their target kind");
    require(engine::editor::selectionTargetEqual(terrainA, terrainACopy) &&
                !engine::editor::selectionTargetEqual(terrainA,
                                                      terrainOtherGrid) &&
                !engine::editor::selectionTargetEqual(volume, placeable) &&
                !engine::editor::selectionTargetEqual(volume,
                                                       rebuiltVolume),
            "Typed identity equality should include kind, grid/cell, and volume revision");
    require(!SelectionTarget{}.valid() &&
                !SelectionTarget::terrainCell("", {0, 0, 0}).valid() &&
                !SelectionTarget::voxelVolume(0u, 0u).valid() &&
                !SelectionTarget::placeable("").valid(),
            "Missing persistent identity data should fail closed");

    std::vector<SelectionTarget> ordered = {
        SelectionTarget::terrainCell("grid", {1, 0, 0}), placeable, volume,
        SelectionTarget::terrainCell("grid", {0, 1, 0}),
        SelectionTarget::terrainCell("grid", {0, 0, 1}),
    };
    std::sort(ordered.begin(), ordered.end(),
              engine::editor::selectionTargetLess);
    require(ordered[0].kind() == SelectionTargetKind::TerrainCell &&
                ordered[1].kind() == SelectionTargetKind::TerrainCell &&
                ordered[2].kind() == SelectionTargetKind::TerrainCell &&
                ordered[3].kind() == SelectionTargetKind::VoxelVolume &&
                ordered[4].kind() == SelectionTargetKind::Placeable,
            "Target ordering should be total and kind-stable");
}

void testViewportRayUnprojection()
{
    const auto vulkanIdentity = engine::editor::selectionRayFromViewport(
        {25.0f, 25.0f}, {100u, 100u}, glm::mat4(1.0f), 50.0f);
    require(vulkanIdentity.valid() &&
                vec3NearlyEqual(vulkanIdentity.ray.origin,
                                {-0.5f, -0.5f, 0.0f}) &&
                vec3NearlyEqual(vulkanIdentity.ray.direction,
                                {0.0f, 0.0f, 1.0f}) &&
                nearlyEqual(vulkanIdentity.ray.maxDistance, 50.0f),
            "Viewport unprojection should use Camera-flipped top-left Y and Vulkan Z [0,1]");

    glm::mat4 perspective =
        glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 1.0f, 11.0f);
    perspective[1][1] *= -1.0f;
    const auto perspectiveRay = engine::editor::selectionRayFromViewport(
        {50.0f, 50.0f}, {100u, 100u}, glm::inverse(perspective), 25.0f);
    require(perspectiveRay.valid() &&
                vec3NearlyEqual(perspectiveRay.ray.origin,
                                {0.0f, 0.0f, -1.0f}, 2e-5f) &&
                vec3NearlyEqual(perspectiveRay.ray.direction,
                                {0.0f, 0.0f, -1.0f}, 2e-5f),
            "Perspective unprojection should divide near/far homogeneous points");

    const glm::vec4 worldPoint(1.0f, 0.75f, -5.0f, 1.0f);
    const glm::vec4 clipPoint = perspective * worldPoint;
    const glm::vec3 ndcPoint = glm::vec3(clipPoint) / clipPoint.w;
    const glm::vec2 roundTripCursor(
        (ndcPoint.x * 0.5f + 0.5f) * 200.0f,
        (ndcPoint.y * 0.5f + 0.5f) * 200.0f);
    const auto cameraRoundTrip = engine::editor::selectionRayFromViewport(
        roundTripCursor, {200u, 200u}, glm::inverse(perspective), 25.0f);
    const glm::vec3 directionToPoint =
        glm::normalize(glm::vec3(worldPoint) - cameraRoundTrip.ray.origin);
    require(cameraRoundTrip.valid() &&
                vec3NearlyEqual(cameraRoundTrip.ray.direction,
                                directionToPoint, 2e-5f) &&
                cameraRoundTrip.ray.direction.y > 0.0f,
            "Camera-style flipped projection should round-trip an off-center world point without a second Y flip");

    const glm::vec3 cameraPosition(3.0f, 2.0f, 5.0f);
    const glm::mat4 view = glm::lookAtRH(
        cameraPosition, glm::vec3(-1.0f, 1.0f, -2.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec4 transformedWorldPoint(-0.25f, 1.6f, -3.0f, 1.0f);
    const glm::mat4 viewProjection = perspective * view;
    const glm::vec4 transformedClip = viewProjection * transformedWorldPoint;
    const glm::vec3 transformedNdc =
        glm::vec3(transformedClip) / transformedClip.w;
    const glm::uvec2 transformedExtent(320u, 180u);
    const glm::vec2 transformedCursor(
        (transformedNdc.x * 0.5f + 0.5f) * transformedExtent.x,
        (transformedNdc.y * 0.5f + 0.5f) * transformedExtent.y);
    const auto transformedCameraRay =
        engine::editor::selectionRayFromViewport(
            transformedCursor, transformedExtent,
            glm::inverse(viewProjection), 50.0f);
    const glm::vec3 transformedDirection = glm::normalize(
        glm::vec3(transformedWorldPoint) - transformedCameraRay.ray.origin);
    require(transformedCameraRay.valid() &&
                vec3NearlyEqual(transformedCameraRay.ray.direction,
                                transformedDirection, 3e-5f),
            "A translated and rotated camera should round-trip proj * view in engine order");

    glm::mat4 orthographic =
        glm::orthoRH_ZO(-4.0f, 4.0f, -2.0f, 2.0f, 2.0f, 12.0f);
    orthographic[1][1] *= -1.0f;
    const auto orthographicRay = engine::editor::selectionRayFromViewport(
        {25.0f, 25.0f}, {100u, 100u}, glm::inverse(orthographic), 25.0f);
    require(orthographicRay.valid() &&
                vec3NearlyEqual(orthographicRay.ray.origin,
                                {-2.0f, 1.0f, -2.0f}, 2e-5f) &&
                vec3NearlyEqual(orthographicRay.ray.direction,
                                {0.0f, 0.0f, -1.0f}, 2e-5f),
            "Orthographic unprojection should preserve the cursor-specific near origin");

    auto invalid = engine::editor::selectionRayFromViewport(
        glm::vec2(0.0f), {0u, 100u}, glm::mat4(1.0f), 10.0f);
    require(invalid.error ==
                engine::editor::SelectionRayBuildError::InvalidViewportExtent,
            "A zero viewport dimension should fail closed");
    invalid = engine::editor::selectionRayFromViewport(
        {-0.1f, 0.0f}, {100u, 100u}, glm::mat4(1.0f), 10.0f);
    require(invalid.error ==
                engine::editor::SelectionRayBuildError::CursorOutsideViewport,
            "A cursor outside the viewport should fail closed");
    invalid = engine::editor::selectionRayFromViewport(
        {std::numeric_limits<float>::quiet_NaN(), 0.0f}, {100u, 100u},
        glm::mat4(1.0f), 10.0f);
    require(invalid.error ==
                engine::editor::SelectionRayBuildError::NonFiniteCursor,
            "A non-finite cursor should fail closed");

    glm::mat4 badMatrix(1.0f);
    badMatrix[1][2] = std::numeric_limits<float>::infinity();
    invalid = engine::editor::selectionRayFromViewport(
        glm::vec2(50.0f), {100u, 100u}, badMatrix, 10.0f);
    require(invalid.error == engine::editor::SelectionRayBuildError::
                                 NonFiniteInverseViewProjection,
            "A non-finite inverse view-projection should fail closed");
    invalid = engine::editor::selectionRayFromViewport(
        glm::vec2(50.0f), {100u, 100u}, glm::mat4(0.0f), 10.0f);
    require(invalid.error == engine::editor::SelectionRayBuildError::
                                 InvalidInverseViewProjection,
            "A singular inverse view-projection should fail closed");

    glm::mat4 zeroNearW(1.0f);
    zeroNearW[2][2] = 0.0f;
    zeroNearW[3][3] = 0.0f;
    zeroNearW[2][3] = 1.0f;
    zeroNearW[3][2] = 1.0f;
    invalid = engine::editor::selectionRayFromViewport(
        glm::vec2(50.0f), {100u, 100u}, zeroNearW, 10.0f);
    require(invalid.error ==
                engine::editor::SelectionRayBuildError::InvalidUnprojection,
            "A finite nonsingular matrix with zero near W should fail closed");
    invalid = engine::editor::selectionRayFromViewport(
        glm::vec2(50.0f), {100u, 100u}, glm::mat4(1.0f),
        std::numeric_limits<float>::infinity());
    require(invalid.error ==
                engine::editor::SelectionRayBuildError::InvalidMaxDistance,
            "An invalid generated-ray range should fail closed");
}

void testNearestHitAndInputOrderIndependence()
{
    const SelectionRay ray{{0.0f, 0.0f, 0.0f},
                           {0.0f, 0.0f, -7.0f}, 20.0f};
    const SelectionCandidate far = candidateAt(
        SelectionTarget::voxelVolume(7u, 4u), {0.0f, 0.0f, -5.0f});
    const SelectionCandidate near = candidateAt(
        SelectionTarget::placeable("near-placeable"), {0.0f, 0.0f, -2.0f});

    for (const std::vector<SelectionCandidate>& candidates :
         {std::vector<SelectionCandidate>{far, near},
          std::vector<SelectionCandidate>{near, far}})
    {
        const auto result = engine::editor::pickSelection(ray, candidates);
        require(result.valid() && result.hit.has_value(),
                "A ray crossing candidates should produce a valid hit");
        require(result.hit->target.kind() == SelectionTargetKind::Placeable &&
                    placeableUuid(result.hit->target) == "near-placeable",
                "Nearest geometry should win independently of candidate order");
        require(nearlyEqual(result.hit->distance, 1.5f) &&
                    vec3NearlyEqual(result.hit->worldPosition,
                                    {0.0f, 0.0f, -1.5f}) &&
                    vec3NearlyEqual(result.hit->worldNormal,
                                    {0.0f, 0.0f, 1.0f}),
                "Ray direction should normalize and report world distance/face data");
    }
}

void testTransformedBoundsAndInsideHit()
{
    SelectionCandidate rotated{};
    rotated.target = SelectionTarget::voxelVolume(9u, 2u);
    rotated.localBounds = SelectionBounds{{-1.0f, -0.5f, -0.5f},
                                           {1.0f, 0.5f, 0.5f}};
    rotated.worldFromLocal =
        glm::translate(glm::mat4(1.0f), {0.0f, 5.0f, 0.0f}) *
        glm::rotate(glm::mat4(1.0f), 1.57079632679f,
                    {0.0f, 0.0f, 1.0f}) *
        glm::scale(glm::mat4(1.0f), {2.0f, 1.0f, 0.5f});

    const SelectionRay ray{{0.0f, 10.0f, 0.0f},
                           {0.0f, -1.0f, 0.0f}, 20.0f};
    const auto result = engine::editor::pickSelection(ray, {&rotated, 1});
    require(result.valid() && result.hit.has_value(),
            "A ray should intersect a rotated nonuniformly scaled local bound");
    require(nearlyEqual(result.hit->distance, 3.0f, 2e-5f) &&
                vec3NearlyEqual(result.hit->worldPosition,
                                {0.0f, 7.0f, 0.0f}, 2e-5f) &&
                vec3NearlyEqual(result.hit->worldNormal,
                                {0.0f, 1.0f, 0.0f}, 2e-5f),
            "Transformed picking should preserve exact OBB entry and world normal");

    const SelectionRay outsideNarrowSide{{0.75f, 10.0f, 0.0f},
                                         {0.0f, -1.0f, 0.0f}, 20.0f};
    const auto miss =
        engine::editor::pickSelection(outsideNarrowSide, {&rotated, 1});
    require(miss.valid() && !miss.hit.has_value(),
            "Picking should test the transformed local bound, not a widened sphere");

    SelectionCandidate enclosing = candidateAt(
        SelectionTarget::terrainCell("main", {0, 0, 0}), {0.0f, 0.0f, 0.0f});
    const auto inside = engine::editor::pickSelection(
        SelectionRay{glm::vec3(0.0f), {1.0f, 0.0f, 0.0f}, 1.0f},
        {&enclosing, 1});
    require(inside.valid() && inside.hit.has_value() &&
                nearlyEqual(inside.hit->distance, 0.0f) &&
                vec3NearlyEqual(inside.hit->worldPosition,
                                glm::vec3(0.0f)) &&
                vec3NearlyEqual(inside.hit->worldNormal,
                                glm::vec3(0.0f)),
            "A ray starting inside a bound should select at distance zero without inventing a face");
}

void testDeterministicTieBreaksAndCornerNormal()
{
    SelectionCandidate lowPriority = candidateAt(
        SelectionTarget::placeable("a-low-priority"), {0.0f, 0.0f, -3.0f}, 2);
    SelectionCandidate highPriority = candidateAt(
        SelectionTarget::placeable("z-high-priority"), {0.0f, 0.0f, -3.0f}, 7);
    const SelectionRay ray{glm::vec3(0.0f), {0.0f, 0.0f, -1.0f}, 10.0f};

    for (const std::vector<SelectionCandidate>& candidates :
         {std::vector<SelectionCandidate>{lowPriority, highPriority},
          std::vector<SelectionCandidate>{highPriority, lowPriority}})
    {
        const auto result = engine::editor::pickSelection(ray, candidates);
        require(result.valid() && result.hit.has_value() &&
                    placeableUuid(result.hit->target) == "z-high-priority",
                "Higher explicit priority should break exact geometry ties");
    }

    lowPriority.priority = 7;
    const auto stableIdentityTie = engine::editor::pickSelection(
        ray, std::vector<SelectionCandidate>{highPriority, lowPriority});
    require(stableIdentityTie.valid() && stableIdentityTie.hit.has_value() &&
                placeableUuid(stableIdentityTie.hit->target) ==
                    "a-low-priority",
            "Stable identity should break equal-distance/equal-priority ties");

    SelectionCandidate corner{};
    corner.target = SelectionTarget::terrainCell("main", {0, 0, 0});
    corner.localBounds =
        SelectionBounds{glm::vec3(0.0f), glm::vec3(1.0f)};
    const auto cornerHit = engine::editor::pickSelection(
        SelectionRay{{-1.0f, -1.0f, 0.5f}, {1.0f, 1.0f, 0.0f}, 5.0f},
        {&corner, 1});
    require(cornerHit.valid() && cornerHit.hit.has_value() &&
                vec3NearlyEqual(cornerHit.hit->worldNormal,
                                {-1.0f, 0.0f, 0.0f}),
            "An edge/corner entry tie should deterministically prefer the X face");
}

void testRangeMissAndFiniteRayFailures()
{
    const SelectionCandidate target = candidateAt(
        SelectionTarget::placeable("target"), {0.0f, 0.0f, -3.0f});
    const auto rangeMiss = engine::editor::pickSelection(
        SelectionRay{glm::vec3(0.0f), {0.0f, 0.0f, -1.0f}, 2.0f},
        {&target, 1});
    require(rangeMiss.valid() && !rangeMiss.hit.has_value(),
            "A target beyond maxDistance should be a valid miss");

    SelectionRay invalid{};
    invalid.origin.x = std::numeric_limits<float>::quiet_NaN();
    require(engine::editor::pickSelection(invalid, {&target, 1}).error ==
                SelectionPickError::NonFiniteRay,
            "A non-finite ray origin should fail closed");
    invalid = {};
    invalid.direction.z = std::numeric_limits<float>::infinity();
    require(engine::editor::pickSelection(invalid, {&target, 1}).error ==
                SelectionPickError::NonFiniteRay,
            "A non-finite ray direction should fail closed");
    invalid = {};
    invalid.direction = glm::vec3(0.0f);
    require(engine::editor::pickSelection(invalid, {&target, 1}).error ==
                SelectionPickError::DegenerateRayDirection,
            "A zero direction should fail closed");
    invalid = {};
    invalid.maxDistance = -1.0f;
    require(engine::editor::pickSelection(invalid, {&target, 1}).error ==
                SelectionPickError::InvalidMaxDistance,
            "A negative ray range should fail closed");
    invalid.maxDistance = std::numeric_limits<float>::infinity();
    require(engine::editor::pickSelection(invalid, {&target, 1}).error ==
                SelectionPickError::InvalidMaxDistance,
            "A non-finite ray range should fail closed");

    invalid = {};
    invalid.direction = {std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max(), 0.0f};
    const auto largeFiniteDirection =
        engine::editor::pickSelection(invalid, std::span<const SelectionCandidate>{});
    require(largeFiniteDirection.valid(),
            "A large but finite nonzero direction should normalize without overflow");
}

void testCandidateValidationFailsClosed()
{
    const SelectionRay ray{glm::vec3(0.0f), {0.0f, 0.0f, -1.0f}, 10.0f};
    const SelectionCandidate valid = candidateAt(
        SelectionTarget::placeable("valid"), {0.0f, 0.0f, -2.0f});

    SelectionCandidate invalid = valid;
    invalid.target = {};
    auto result = engine::editor::pickSelection(ray, {&invalid, 1});
    require(result.error == SelectionPickError::InvalidTarget &&
                !result.hit.has_value() && result.invalidCandidateIndex == 0,
            "An invalid identity should fail before returning a hit");

    std::vector<SelectionCandidate> duplicate = {valid, valid};
    duplicate[1].worldFromLocal = translated({0.0f, 0.0f, -7.0f});
    result = engine::editor::pickSelection(ray, duplicate);
    require(result.error == SelectionPickError::DuplicateTarget &&
                !result.hit.has_value() && result.invalidCandidateIndex == 1,
            "A duplicated stable identity should reject the complete candidate set");

    invalid = valid;
    invalid.localBounds.localMin.x = 1.0f;
    invalid.localBounds.localMax.x = -1.0f;
    require(engine::editor::pickSelection(ray, {&invalid, 1}).error ==
                SelectionPickError::InvalidBounds,
            "Reversed local bounds should fail closed");
    invalid = valid;
    invalid.localBounds.localMax.y =
        std::numeric_limits<float>::quiet_NaN();
    require(engine::editor::pickSelection(ray, {&invalid, 1}).error ==
                SelectionPickError::InvalidBounds,
            "Non-finite local bounds should fail closed");

    invalid = valid;
    invalid.worldFromLocal[2][1] =
        std::numeric_limits<float>::infinity();
    require(engine::editor::pickSelection(ray, {&invalid, 1}).error ==
                SelectionPickError::NonFiniteTransform,
            "A non-finite transform should fail closed");
    invalid = valid;
    invalid.worldFromLocal[0][3] = 0.1f;
    require(engine::editor::pickSelection(ray, {&invalid, 1}).error ==
                SelectionPickError::NonAffineTransform,
            "A perspective transform should not be approximated as affine");
    invalid = valid;
    invalid.worldFromLocal[0][0] = 0.0f;
    require(engine::editor::pickSelection(ray, {&invalid, 1}).error ==
                SelectionPickError::NonInvertibleTransform,
            "A collapsed transformed bound should fail closed");
    invalid = valid;
    invalid.worldFromLocal[0][0] = 1e-40f;
    require(engine::editor::pickSelection(ray, {&invalid, 1}).error ==
                SelectionPickError::NonInvertibleTransform,
            "A transform requiring a non-finite float inverse should fail closed");
    invalid = valid;
    invalid.localBounds =
        SelectionBounds{glm::vec3(-2.0f), glm::vec3(2.0f)};
    invalid.worldFromLocal[0][0] = std::numeric_limits<float>::max();
    require(engine::editor::pickSelection(ray, {&invalid, 1}).error ==
                SelectionPickError::NonFiniteWorldBounds,
            "A transform whose world bounds overflow should fail closed");

    invalid = valid;
    invalid.localBounds.localMin.z = 1.0f;
    invalid.localBounds.localMax.z = -1.0f;
    result = engine::editor::pickSelection(
        ray, std::vector<SelectionCandidate>{valid, invalid});
    require(result.error == SelectionPickError::InvalidBounds &&
                !result.hit.has_value() && result.invalidCandidateIndex == 1,
            "Later malformed data should suppress an earlier geometric hit");
}

void testErrorLabelsAreComplete()
{
    const std::vector<SelectionPickError> errors = {
        SelectionPickError::None,
        SelectionPickError::NonFiniteRay,
        SelectionPickError::DegenerateRayDirection,
        SelectionPickError::InvalidMaxDistance,
        SelectionPickError::InvalidTarget,
        SelectionPickError::DuplicateTarget,
        SelectionPickError::InvalidBounds,
        SelectionPickError::NonFiniteTransform,
        SelectionPickError::NonAffineTransform,
        SelectionPickError::NonInvertibleTransform,
        SelectionPickError::NonFiniteWorldBounds,
    };
    for (const SelectionPickError error : errors)
    {
        require(std::string(engine::editor::selectionPickErrorLabel(error)) !=
                    "unknown",
                "Every public picking error should have a runtime label");
    }


    const std::vector<engine::editor::SelectionRayBuildError> rayErrors = {
        engine::editor::SelectionRayBuildError::None,
        engine::editor::SelectionRayBuildError::NonFiniteCursor,
        engine::editor::SelectionRayBuildError::CursorOutsideViewport,
        engine::editor::SelectionRayBuildError::InvalidViewportExtent,
        engine::editor::SelectionRayBuildError::NonFiniteInverseViewProjection,
        engine::editor::SelectionRayBuildError::InvalidInverseViewProjection,
        engine::editor::SelectionRayBuildError::InvalidUnprojection,
        engine::editor::SelectionRayBuildError::DegenerateRay,
        engine::editor::SelectionRayBuildError::InvalidMaxDistance,
    };
    for (const engine::editor::SelectionRayBuildError error : rayErrors)
    {
        require(std::string(engine::editor::selectionRayBuildErrorLabel(error)) !=
                    "unknown",
                "Every public viewport-ray error should have a runtime label");
    }
}

} // namespace

int main()
{
    try
    {
        testTypedStableIdentity();
        testViewportRayUnprojection();
        testNearestHitAndInputOrderIndependence();
        testTransformedBoundsAndInsideHit();
        testDeterministicTieBreaksAndCornerNormal();
        testRangeMissAndFiniteRayFailures();
        testCandidateValidationFailsClosed();
        testErrorLabelsAreComplete();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Editor selection/picking tests failed: " << error.what()
                  << '\n';
        return 1;
    }

    std::cout << "Editor selection/picking tests passed\n";
    return 0;
}
