#include "engine/scene/AquariumScene.h"
#include "engine/scene/CloudScene.h"
#include "engine/game/FoliageCatalog.h"
#include "engine/game/AxolotlBellyFloat.h"
#include "engine/game/AxolotlCreaturePolish.h"
#include "engine/game/AxolotlModel.h"
#include "engine/game/CollectionCodex.h"
#include "engine/game/FishCelebration.h"
#include "engine/game/FishTypes.h"
#include "engine/game/GameStateSimulation.h"
#include "engine/game/GameRuntime.h"
#include "engine/game/SpeciesIds.h"
#include "engine/input/WindowCoordinates.h"
#include "App/InputRouter.h"
#include "engine/render/gpu/DescriptorAllocator.h"
#include "engine/render/gpu/DescriptorLayoutCache.h"
#include "engine/render/gpu/SamplerCache.h"
#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/FrameCharacterization.h"
#include "engine/render/FoliageMotionHistory.h"
#include "engine/render/FoliagePresentation.h"
#include "engine/render/FoliageVoxelGeometry.h"
#include "engine/render/WindborneParticleVoxelGeometry.h"
#include "engine/performance/AutomationProfileSession.h"
#include "engine/render/HemisphereAmbient.h"
#include "engine/render/AmbientOcclusionQualityTier.h"
#include "engine/render/AuxiliaryRayResolution.h"
#include "engine/render/PassRegistry.h"
#include "engine/render/Renderer.h"
#include "engine/render/RendererCapabilities.h"
#include "engine/render/RenderQualityPreset.h"
#include "engine/render/RenderPipelineShowcase.h"
#include "engine/render/RenderResolution.h"
#include "engine/render/RenderSettings.h"
#include "engine/render/SunShadowSamplingPolicy.h"
#include "engine/render/voxel/TerrainShadowColumnEligibility.h"
#include "engine/render/voxel/VoxelLightingOcclusionPolicy.h"
#include "engine/render/voxel/VoxelAlignedLayerTraversal.h"
#include "engine/render/voxel/VoxelRasterFaceSelection.h"
#include "engine/render/passes/GlassPass.h"
#include "engine/render/passes/GBufferPass.h"
#include "engine/render/passes/LightingPass.h"
#include "engine/render/passes/AOPass.h"
#include "engine/render/passes/CompositePass.h"
#include "engine/render/passes/VoxelGlassRefractPass.h"
#include "engine/scene/EnvironmentSettings.h"
#include "engine/scene/WindborneParticleField.h"
#include "engine/scene/LightingParityProbe.h"
#include "engine/scene/NaturePondFoliageDistribution.h"
#include "engine/scene/NaturePondGlassDome.h"
#include "engine/scene/NaturePondGenerator.h"
#include "engine/scene/NaturePondSunroofProbe.h"
#include "engine/scene/ProceduralWorldMaterials.h"
#include "engine/scene/TankGlassBuilder.h"
#include "engine/scene/SceneManager.h"
#include "engine/scene/SceneObjectBuilder.h"
#include "engine/scene/WorldStateView.h"
#include "engine/voxel/VoxelSystem.h"
#include "engine/voxel/VoxelCellVariation.h"
#include "Core/JobSystem.h"
#include "engine/render/FrameContext.h"
#include "engine/render/VulkanContext.h"
#include "engine/render/passes/PassCreateInfo.h"
#include "engine/voxel/ChunkGrid.h"
#include "engine/voxel/Raycast.h"
#include "engine/voxel/VoxelMath.h"
#include "engine/voxel/VoxelTypes.h"
#include "engine/voxel/WorldGen.h"
#include "UI/Runtime/UiContext.h"
#include "UI/Runtime/BuildPlacement.h"
#include "UI/Runtime/UiActions.h"
#include "UI/Runtime/UiDebugFont.h"
#include "UI/Runtime/UiElement.h"
#include "UI/Runtime/UiScreenControllers.h"
#include "UI/Runtime/UiScreens.h"
#include "UI/Runtime/UiBinding.h"
#include "UI/Runtime/UiTheme.h"
#include "UI/Runtime/UiTween.h"
#include "UI/Runtime/UiTypes.h"
#include "UI/Runtime/UiWidgets.h"
#include "engine/voxel/ModularGlassMesher.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace
{

class ReservedTemporaryDirectory
{
public:
    explicit ReservedTemporaryDirectory(std::string_view prefix)
    {
        const auto seed = std::chrono::steady_clock::now()
                              .time_since_epoch()
                              .count();
        for (unsigned int attempt = 0; attempt < 64; ++attempt)
        {
            const std::filesystem::path candidate =
                std::filesystem::temp_directory_path() /
                (std::string(prefix) + "-" + std::to_string(seed) + "-" +
                 std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error))
            {
                path_ = candidate;
                return;
            }
            if (error)
            {
                throw std::runtime_error(
                    "Failed to reserve test temporary directory: " +
                    error.message());
            }
        }
        throw std::runtime_error(
            "Could not reserve a unique test temporary directory");
    }

    ~ReservedTemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    ReservedTemporaryDirectory(const ReservedTemporaryDirectory&) = delete;
    ReservedTemporaryDirectory& operator=(
        const ReservedTemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_{};
};

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

bool nearlyEqual(float a, float b, float epsilon = 1e-5f)
{
    return std::fabs(a - b) <= epsilon;
}

bool equals(std::string_view lhs, std::string_view rhs)
{
    return lhs.size() == rhs.size() && lhs.compare(rhs) == 0;
}

template <size_t N>
bool orderEquals(const std::vector<std::string>& observed,
                 const std::array<std::string_view, N>& expected)
{
    if (observed.size() != expected.size())
    {
        return false;
    }

    for (size_t i = 0; i < expected.size(); ++i)
    {
        if (!equals(observed[i], expected[i]))
        {
            return false;
        }
    }

    return true;
}

template <size_t N>
std::vector<std::string> copyOrder(const std::array<std::string_view, N>& expected)
{
    std::vector<std::string> result;
    result.reserve(expected.size());
    for (std::string_view name : expected)
    {
        result.emplace_back(name);
    }
    return result;
}

size_t indexOf(const std::vector<std::string>& values, std::string_view name)
{
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (equals(values[i], name))
        {
            return i;
        }
    }

    throw std::runtime_error(std::string("Expected order entry not found: ") +
                             std::string(name));
}

void requireBefore(const std::vector<std::string>& values,
                   std::string_view earlier,
                   std::string_view later,
                   const std::string& message)
{
    require(indexOf(values, earlier) < indexOf(values, later), message);
}

void testWindowCoordinatesMapToFramebufferPixels()
{
    using engine::input::windowToFramebuffer;

    const auto identity = windowToFramebuffer(320.0, 180.0, 1280, 720, 1280, 720);
    require(nearlyEqual(identity.x, 320.0f) && nearlyEqual(identity.y, 180.0f),
            "Equal window and framebuffer sizes should preserve cursor coordinates");

    const auto retina = windowToFramebuffer(320.0, 180.0, 1280, 720, 2560, 1440);
    require(nearlyEqual(retina.x, 640.0f) && nearlyEqual(retina.y, 360.0f),
            "Retina cursor coordinates should scale to framebuffer pixels");

    const auto nonUniform = windowToFramebuffer(100.0, 100.0, 800, 600, 1600, 900);
    require(nearlyEqual(nonUniform.x, 200.0f) && nearlyEqual(nonUniform.y, 150.0f),
            "Each framebuffer axis should use its own content scale");

    const auto unavailable = windowToFramebuffer(12.0, 34.0, 0, 0, 0, 0);
    require(nearlyEqual(unavailable.x, 12.0f) && nearlyEqual(unavailable.y, 34.0f),
            "Unavailable dimensions should safely preserve cursor coordinates");
}

void testInputRouterHonorsRuntimeUiModalCapture()
{
    InputRouter router;
    router.setAppMode(AppMode::Game);

    require(router.routeKey(0, 0) == InputOwner::Game &&
                router.routePointerMove(0.0, 0.0) == InputOwner::Game &&
                router.routePolledGameInput() == InputOwner::Game,
            "Uncaptured game input should remain game-owned");

    router.setRuntimeUiCapture(true, false);
    require(router.routePointerButton(0, 0) == InputOwner::RuntimeUi &&
                router.routePolledGameInput() == InputOwner::RuntimeUi &&
                router.routeKey(0, 0) == InputOwner::Game,
            "A non-modal HUD hit should capture pointer input without stealing keys");

    router.setRuntimeUiModalCapture(true);
    router.setRuntimeUiCapture(false, false);
    require(router.runtimeUiModalCapture() &&
                router.routeKey(0, 0) == InputOwner::RuntimeUi &&
                router.routePointerMove(0.0, 0.0) == InputOwner::RuntimeUi &&
                router.routePolledGameInput() == InputOwner::RuntimeUi,
            "Modal ownership should survive pointer refresh and own every input channel");

    router.setRuntimeUiModalCapture(false);
    router.setRuntimeUiCapture(false, false);
    require(router.routeKey(0, 0) == InputOwner::Game &&
                router.routePointerButton(0, 0) == InputOwner::Game,
            "Clearing modal capture should restore gameplay routing");
}

void testAutomationProfileSessionUsesSteadyStateWindow()
{
    using engine::performance::AutomationProfileSession;

    AutomationProfileSession session;
    session.configure(2, 3);
    const std::array<GpuProfilerSample, 2> firstPasses = {
        GpuProfilerSample{"Frame", 30.0},
        GpuProfilerSample{"Voxel DDA", 12.0},
    };

    const auto waiting = session.consume(false, 99.0, 99.0, firstPasses);
    require(!waiting.readinessReached && session.sampledFrameCount() == 0,
            "Profile samples must wait for explicit scene readiness");

    const auto warmupOne = session.consume(true, 80.0, 70.0, firstPasses);
    const auto warmupTwo = session.consume(true, 60.0, 50.0, firstPasses);
    require(warmupOne.readinessReached && !warmupOne.sampleAccepted &&
                warmupTwo.warmupCompleted && !warmupTwo.sampleAccepted &&
                session.consumedWarmupFrames() == 2,
            "Warm-up frames must be consumed without entering the measured window");

    const std::array<GpuProfilerSample, 2> sampleOne = {
        GpuProfilerSample{"Frame", 30.0},
        GpuProfilerSample{"Voxel DDA", 12.0},
    };
    const std::array<GpuProfilerSample, 2> sampleTwo = {
        GpuProfilerSample{"Frame", 10.0},
        GpuProfilerSample{"Voxel DDA", 6.0},
    };
    const std::array<GpuProfilerSample, 2> sampleThree = {
        GpuProfilerSample{"Frame", 20.0},
        GpuProfilerSample{"Voxel DDA", 9.0},
    };
    const auto acceptedOne = session.consume(true, 10.0, 30.0, sampleOne);
    session.consume(true, 20.0, 10.0, sampleTwo);
    const auto acceptedThree = session.consume(true, 30.0, 20.0, sampleThree);

    require(acceptedOne.samplingStarted && acceptedOne.sampleAccepted &&
                acceptedThree.samplingCompleted && session.samplingComplete(),
            "The requested sample count must define a bounded measured window");

    const auto summary = session.summary();
    require(summary.warmupFrames == 2 && summary.sampledFrames == 3,
            "The summary must report warm-up and measured frame counts separately");
    require(std::fabs(summary.cpu.averageMs - 20.0) < 1e-8 &&
                std::fabs(summary.cpu.medianMs - 20.0) < 1e-8 &&
                std::fabs(summary.cpu.p95Ms - 29.0) < 1e-8 &&
                std::fabs(summary.cpu.p99Ms - 29.8) < 1e-8 &&
                std::fabs(summary.gpu.averageMs - 20.0) < 1e-8,
            "The profile summary must calculate stable average and percentile values");
    require(summary.averagePasses.size() == 2 &&
                summary.averagePasses[0].label == "Frame" &&
                std::fabs(summary.averagePasses[0].ms - 20.0) < 1e-8 &&
                summary.averagePasses[1].label == "Voxel DDA" &&
                std::fabs(summary.averagePasses[1].ms - 9.0) < 1e-8,
            "Per-pass totals must be averaged only across accepted sample frames");

    const auto ignoredAfterCompletion =
        session.consume(true, 1.0, 1.0, sampleOne);
    require(!ignoredAfterCompletion.sampleAccepted &&
                session.sampledFrameCount() == 3,
            "A completed profile window must reject later frames");
}

bool sameWorldBlocks(const ChunkGrid& lhs, const ChunkGrid& rhs)
{
    const glm::ivec3 lhsDims = lhs.dims();
    const glm::ivec3 rhsDims = rhs.dims();
    const glm::ivec3 lhsOrigin = lhs.origin();
    const glm::ivec3 rhsOrigin = rhs.origin();
    if (lhsDims.x != rhsDims.x || lhsDims.y != rhsDims.y || lhsDims.z != rhsDims.z ||
        lhsOrigin.x != rhsOrigin.x || lhsOrigin.y != rhsOrigin.y || lhsOrigin.z != rhsOrigin.z)
    {
        return false;
    }

    const std::vector<Chunk>& lhsChunks = lhs.chunks();
    const std::vector<Chunk>& rhsChunks = rhs.chunks();
    if (lhsChunks.size() != rhsChunks.size())
    {
        return false;
    }

    for (size_t i = 0; i < lhsChunks.size(); ++i)
    {
        if (lhsChunks[i].coord.x != rhsChunks[i].coord.x ||
            lhsChunks[i].coord.y != rhsChunks[i].coord.y ||
            lhsChunks[i].coord.z != rhsChunks[i].coord.z)
        {
            return false;
        }

        if (lhsChunks[i].blocks != rhsChunks[i].blocks)
        {
            return false;
        }
    }

    return true;
}

void testFloorDivisionHelpers()
{
    require(floorDiv(33, 32) == 1, "floorDiv should preserve positive quotient");
    require(floorMod(33, 32) == 1, "floorMod should preserve positive remainder");
    require(floorDiv(-1, 32) == -1, "floorDiv should round down for negative numerators");
    require(floorMod(-1, 32) == 31, "floorMod should stay positive for negative numerators");
    require(floorDiv(-33, 32) == -2, "floorDiv should handle negative chunk crossing");
    require(floorMod(-33, 32) == 31, "floorMod should wrap negative chunk crossing");
}

void testWorldGenerationIsDeterministic()
{
    ChunkGrid first;
    ChunkGrid second;
    first.create(glm::ivec3(2, 1, 2), glm::ivec3(-1, 0, -1));
    second.create(glm::ivec3(2, 1, 2), glm::ivec3(-1, 0, -1));

    const WorldGenStats firstStats = generateWorld(first, 1337u);
    const WorldGenStats secondStats = generateWorld(second, 1337u);

    require(firstStats.treesPlaced == secondStats.treesPlaced,
            "World generation tree placement should be deterministic");
    require(firstStats.foliagePlaced == secondStats.foliagePlaced,
            "World generation foliage placement should be deterministic");
    require(firstStats.minHeight == secondStats.minHeight,
            "World generation minimum height should be deterministic");
    require(firstStats.maxHeight == secondStats.maxHeight,
            "World generation maximum height should be deterministic");
    require(nearlyEqual(firstStats.avgHeight, secondStats.avgHeight),
            "World generation average height should be deterministic");
    require(sameWorldBlocks(first, second),
            "World generation should produce identical chunk contents for the same seed");
}

void testWorldGenerationVariesWithSeed()
{
    ChunkGrid first;
    ChunkGrid second;
    first.create(glm::ivec3(2, 1, 2), glm::ivec3(0, 0, 0));
    second.create(glm::ivec3(2, 1, 2), glm::ivec3(0, 0, 0));

    generateWorld(first, 42u);
    generateWorld(second, 84u);

    require(!sameWorldBlocks(first, second),
            "Different world generation seeds should not produce identical chunk contents");
}

void testSamplerCachePresetCreateInfos()
{
    const VkSamplerCreateInfo clampLinear =
        engine::render::samplerCreateInfo(engine::render::SamplerPreset::ClampLinear);
    require(clampLinear.magFilter == VK_FILTER_LINEAR,
            "ClampLinear should use linear magnification filtering");
    require(clampLinear.minFilter == VK_FILTER_LINEAR,
            "ClampLinear should use linear minification filtering");
    require(clampLinear.mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR,
            "ClampLinear should use linear mip filtering");
    require(clampLinear.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
                clampLinear.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
                clampLinear.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            "ClampLinear should clamp every texture axis to edge");
    require(clampLinear.maxAnisotropy == 1.0f,
            "ClampLinear should preserve the existing explicit maxAnisotropy");

    const VkSamplerCreateInfo clampLinearNearestMip =
        engine::render::samplerCreateInfo(engine::render::SamplerPreset::ClampLinearNearestMip);
    require(clampLinearNearestMip.magFilter == VK_FILTER_LINEAR,
            "ClampLinearNearestMip should use linear magnification filtering");
    require(clampLinearNearestMip.minFilter == VK_FILTER_LINEAR,
            "ClampLinearNearestMip should use linear minification filtering");
    require(clampLinearNearestMip.mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST,
            "ClampLinearNearestMip should preserve nearest mip filtering");
    require(clampLinearNearestMip.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
                clampLinearNearestMip.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
                clampLinearNearestMip.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            "ClampLinearNearestMip should clamp every texture axis to edge");
    require(clampLinearNearestMip.maxAnisotropy == 0.0f,
            "ClampLinearNearestMip should preserve the existing default maxAnisotropy");

    const VkSamplerCreateInfo shadowCompare =
        engine::render::samplerCreateInfo(engine::render::SamplerPreset::ShadowCompareNearest);
    require(shadowCompare.magFilter == VK_FILTER_NEAREST &&
                shadowCompare.minFilter == VK_FILTER_NEAREST,
            "ShadowCompareNearest should use nearest filtering");
    require(shadowCompare.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER &&
                shadowCompare.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER &&
                shadowCompare.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            "ShadowCompareNearest should clamp every texture axis to border");
    require(shadowCompare.borderColor == VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
            "ShadowCompareNearest should preserve the white border color");
    require(shadowCompare.compareEnable == VK_TRUE &&
                shadowCompare.compareOp == VK_COMPARE_OP_LESS_OR_EQUAL,
            "ShadowCompareNearest should enable depth comparison");
}

void testDescriptorLayoutCacheKeys()
{
    VkDescriptorSetLayoutBinding sampled{};
    sampled.binding = 0;
    sampled.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampled.descriptorCount = 1;
    sampled.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding frameUbo{};
    frameUbo.binding = 1;
    frameUbo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameUbo.descriptorCount = 1;
    frameUbo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ordered[] = {sampled, frameUbo};
    VkDescriptorSetLayoutCreateInfo orderedInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    orderedInfo.bindingCount = 2;
    orderedInfo.pBindings = ordered;

    VkDescriptorSetLayoutBinding reversed[] = {frameUbo, sampled};
    VkDescriptorSetLayoutCreateInfo reversedInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    reversedInfo.bindingCount = 2;
    reversedInfo.pBindings = reversed;

    engine::render::DescriptorLayoutKey orderedKey{};
    engine::render::DescriptorLayoutKey reversedKey{};
    require(engine::render::makeDescriptorLayoutKey(orderedInfo, orderedKey),
            "Descriptor layout cache should accept simple binding arrays");
    require(engine::render::makeDescriptorLayoutKey(reversedInfo, reversedKey),
            "Descriptor layout cache should accept reversed binding arrays");
    require(orderedKey == reversedKey,
            "Descriptor layout keys should canonicalize binding order");
    require(engine::render::DescriptorLayoutKeyHash{}(orderedKey) ==
                engine::render::DescriptorLayoutKeyHash{}(reversedKey),
            "Equivalent descriptor layout keys should hash identically");

    VkDescriptorSetLayoutBinding changedStage = sampled;
    changedStage.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo changedInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    changedInfo.bindingCount = 1;
    changedInfo.pBindings = &changedStage;
    engine::render::DescriptorLayoutKey changedKey{};
    require(engine::render::makeDescriptorLayoutKey(changedInfo, changedKey),
            "Descriptor layout cache should key a changed binding");
    require(!(changedKey == orderedKey),
            "Descriptor layout keys should include shader stage flags");

    VkDescriptorSetLayoutCreateInfo emptyInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    engine::render::DescriptorLayoutKey emptyKey{};
    require(engine::render::makeDescriptorLayoutKey(emptyInfo, emptyKey) &&
                emptyKey.bindings.empty(),
            "Descriptor layout cache should support empty descriptor set layouts");

    VkDescriptorSetLayoutCreateInfo invalidInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    invalidInfo.pNext = &orderedInfo;
    require(!engine::render::makeDescriptorLayoutKey(invalidInfo, emptyKey),
            "Descriptor layout cache should reject unsupported pNext chains");

    VkDescriptorSetLayoutCreateInfo nullBindingsInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    nullBindingsInfo.bindingCount = 1;
    require(!engine::render::makeDescriptorLayoutKey(nullBindingsInfo, emptyKey),
            "Descriptor layout cache should reject missing binding arrays");
}

void testDescriptorAllocatorKeys()
{
    VkDescriptorPoolSize sampled{};
    sampled.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampled.descriptorCount = 2;

    VkDescriptorPoolSize uniform{};
    uniform.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniform.descriptorCount = 4;

    VkDescriptorPoolSize ordered[] = {sampled, uniform};
    VkDescriptorPoolSize reversed[] = {uniform, sampled};

    engine::render::DescriptorPoolKey orderedKey{};
    engine::render::DescriptorPoolKey reversedKey{};
    require(engine::render::makeDescriptorPoolKey(ordered, 6, 0, orderedKey),
            "Descriptor allocator should accept simple pool sizes");
    require(engine::render::makeDescriptorPoolKey(reversed, 6, 0, reversedKey),
            "Descriptor allocator should accept reversed pool sizes");
    require(orderedKey == reversedKey,
            "Descriptor allocator keys should canonicalize pool-size order");
    require(engine::render::DescriptorPoolKeyHash{}(orderedKey) ==
                engine::render::DescriptorPoolKeyHash{}(reversedKey),
            "Equivalent descriptor allocator keys should hash identically");

    VkDescriptorPoolSize moreSampled = sampled;
    moreSampled.descriptorCount = 3;
    VkDescriptorPoolSize duplicated[] = {sampled, moreSampled, uniform};
    engine::render::DescriptorPoolKey duplicatedKey{};
    require(engine::render::makeDescriptorPoolKey(duplicated, 6, 0, duplicatedKey),
            "Descriptor allocator should accept duplicated descriptor types");

    uint32_t combinedSampledCount = 0;
    for (const engine::render::DescriptorPoolSizeKey& size : duplicatedKey.sizes)
    {
        if (size.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        {
            combinedSampledCount = size.descriptorCount;
        }
    }
    require(combinedSampledCount == 5,
            "Descriptor allocator keys should combine duplicate descriptor types");

    engine::render::DescriptorPoolKey flaggedKey{};
    require(engine::render::makeDescriptorPoolKey(
                ordered, 6, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, flaggedKey),
            "Descriptor allocator should key pool create flags");
    require(!(orderedKey == flaggedKey),
            "Descriptor allocator keys should include pool create flags");

    VkDescriptorPoolSize zeroCount = sampled;
    zeroCount.descriptorCount = 0;
    require(!engine::render::makeDescriptorPoolKey(
                std::span<const VkDescriptorPoolSize>(&zeroCount, 1), 1, 0, flaggedKey),
            "Descriptor allocator should reject zero descriptor counts");
    require(!engine::render::makeDescriptorPoolKey(
                std::span<const VkDescriptorPoolSize>(), 1, 0, flaggedKey),
            "Descriptor allocator should reject empty pool sizes");
    require(!engine::render::makeDescriptorPoolKey(ordered, 0, 0, flaggedKey),
            "Descriptor allocator should reject zero max set counts");

    engine::render::DescriptorAllocator allocator{};
    require(allocator.poolCount() == 0,
            "Fresh descriptor allocator should not own pools before Vulkan allocation");
    require(allocator.resetPools(),
            "Resetting an empty descriptor allocator should be a no-op");
}

void testUniqueAllocationOwnershipContract()
{
    using engine::render::UniqueBuffer;
    using engine::render::UniqueImage;

    static_assert(!std::is_copy_constructible_v<UniqueBuffer>);
    static_assert(!std::is_copy_assignable_v<UniqueBuffer>);
    static_assert(std::is_nothrow_move_constructible_v<UniqueBuffer>);
    static_assert(std::is_nothrow_move_assignable_v<UniqueBuffer>);
    static_assert(!std::is_copy_constructible_v<UniqueImage>);
    static_assert(!std::is_copy_assignable_v<UniqueImage>);
    static_assert(std::is_nothrow_move_constructible_v<UniqueImage>);
    static_assert(std::is_nothrow_move_assignable_v<UniqueImage>);

    UniqueBuffer buffer{};
    require(!buffer && buffer.get() == VK_NULL_HANDLE,
            "A default VMA buffer owner should be empty");

    UniqueBuffer movedBuffer{std::move(buffer)};
    require(!buffer && !movedBuffer,
            "Moving an empty VMA buffer owner should preserve an empty source and target");
    movedBuffer.reset();
    require(!movedBuffer, "Resetting an empty VMA buffer owner should be a no-op");

    UniqueImage image{};
    require(!image && image.get() == VK_NULL_HANDLE,
            "A default VMA image owner should be empty");

    UniqueImage moved{std::move(image)};
    require(!image && !moved,
            "Moving an empty VMA image owner should preserve an empty source and target");
    moved.reset();
    require(!moved, "Resetting an empty VMA image owner should be a no-op");
}

void testRenderSettingsDefaultsAndIndependence()
{
    engine::render::LightingSettings lighting{};
    require(lighting.pointLightsEnabled_ && lighting.lightCount_ == 32 &&
                lighting.areaLights_.empty(),
            "Lighting settings should preserve current local-light defaults");
    require(nearlyEqual(lighting.sunElevation_, 55.0f) &&
                nearlyEqual(lighting.sunAzimuth_, 135.0f) &&
                nearlyEqual(lighting.sunIntensity_, 2.5f),
            "Lighting settings should preserve current sun defaults");

    engine::render::ShadowSettings shadows{};
    require(shadows.csmEnabled_ && shadows.localLightShadowsEnabled_ &&
                shadows.localShadowBlurEnabled_,
            "Shadow settings should preserve current enabled defaults");
    require(!shadows.useDDAShadows_ && shadows.shadowResetHistory_ &&
                shadows.localShadowResetHistory_,
            "Shadow settings should preserve DDA-disabled and reset defaults");

    engine::render::AmbientOcclusionSettings ao{};
    require(ao.aoEnabled_ && ao.aoResetHistory_,
            "AO settings should default to enabled with history reset requested");
    require(nearlyEqual(ao.aoContribution_, 0.25f) && ao.aoRayCount_ == 2,
            "AO settings should preserve current default contribution and ray count");
    require(ao.aoDistanceMode_ ==
                engine::render::AmbientOcclusionDistanceMode::AuthoredWorldDistance &&
                nearlyEqual(ao.aoProjectedRadiusPixels_, 512.0f) &&
                nearlyEqual(ao.aoProjectedMinDistance_, 2.0f),
            "AO settings must default to authored distance with validated projected values dormant");

    engine::render::PostFxSettings post{};
    require(post.tonemapEnabled_ && post.bloomEnabled_ && post.fxaaEnabled_,
            "PostFX settings should preserve current enabled defaults");
    require(!post.taaEnabled_ && !post.jitterEnabled_ &&
                post.taaResetHistory_,
            "PostFX TAA settings should preserve current disabled/reset defaults");

    engine::render::WaterSettings water{};
    require(water.waterEnabled_ && water.waterStylizedMode_ &&
                nearlyEqual(water.waterLevel_, 10.0f),
            "Water settings should preserve current enabled stylized defaults");
    require(!water.useWaterV2_ && water.waterParticlesPlanned_ &&
                !water.waterFoamEmitterEnabled_,
            "Water settings should preserve current V2/VFX defaults");

    engine::render::GlassSettings glass{};
    require(!glass.glassEnabled_ && glass.voxelGlassRefractEnabled_,
            "Glass settings should preserve current classic/voxel glass defaults");
    require(nearlyEqual(glass.glassIor_, 1.52f) &&
                nearlyEqual(glass.voxelGlassIOR_, 1.5f),
            "Glass settings should preserve current IOR defaults");

    engine::render::EditableAreaLight light{};
    light.shape = LightShape::Sphere;
    lighting.areaLights_.push_back(light);
    lighting.lightCount_ = 1;
    shadows.useDDAShadows_ = true;
    shadows.shadowDebugMode_ = 2;
    shadows.localShadowCastingLightCount_ = 1;
    ao.aoEnabled_ = false;
    ao.aoDistanceMode_ =
        engine::render::AmbientOcclusionDistanceMode::ProjectedScreenRadius;
    ao.aoNormalRejectDot_ = 0.7f;
    post.tonemapEnabled_ = false;
    post.exposure_ = 2.0f;
    post.taaEnabled_ = true;
    water.useWaterV2_ = true;
    water.waterLevel_ = 12.0f;
    glass.glassEnabled_ = true;
    glass.voxelGlassRefractStrength_ = 0.04f;
    require(lighting.areaLights_.size() == 1 && lighting.lightCount_ == 1 &&
                lighting.areaLights_[0].shape == LightShape::Sphere,
            "Lighting settings should own editable area lights");
    require(shadows.useDDAShadows_ && shadows.shadowDebugMode_ == 2 &&
                shadows.localShadowCastingLightCount_ == 1,
            "Shadow settings should be directly mutable plain data");
    require(!ao.aoEnabled_ &&
                ao.aoDistanceMode_ ==
                    engine::render::AmbientOcclusionDistanceMode::
                        ProjectedScreenRadius &&
                nearlyEqual(ao.aoNormalRejectDot_, 0.7f),
            "AO settings should be directly mutable plain data");
    require(!post.tonemapEnabled_ && nearlyEqual(post.exposure_, 2.0f) &&
                post.taaEnabled_,
            "PostFX settings should be directly mutable plain data");
    require(water.useWaterV2_ && nearlyEqual(water.waterLevel_, 12.0f),
            "Water settings should be directly mutable plain data");
    require(glass.glassEnabled_ &&
                nearlyEqual(glass.voxelGlassRefractStrength_, 0.04f),
            "Glass settings should be directly mutable plain data");

    engine::render::VoxelDebugSettings voxelDebug{};
    require(voxelDebug.voxelVisible_ && voxelDebug.proceduralFishEnabled_ &&
                voxelDebug.axolotlEnabled_ && voxelDebug.proceduralFishCount_ == 6,
            "Voxel debug settings should preserve current visibility/roster defaults");
    require(voxelDebug.voxelDdaSkipEnabled_ && voxelDebug.voxelDdaSkipMip_ == 3 &&
                nearlyEqual(voxelDebug.voxelHeatmapMax_, 128.0f) &&
                !voxelDebug.voxelEditMode_,
            "Voxel debug settings should preserve current DDA/heatmap defaults");

    engine::render::DiagnosticsSettings diagnostics{};
    require(diagnostics.viewMode_ == 0 && !diagnostics.gpuProfilerEnabled_ &&
                !diagnostics.pixelInspectEnabled_ && !diagnostics.voxelMetricsEnabled_,
            "Diagnostics settings should default to final view with probes off");
    require(diagnostics.inspectPixel_ == glm::ivec2(-1, -1) &&
                nearlyEqual(diagnostics.lastInspectDepth_, 1.0f) &&
                diagnostics.voxelMetricsSampleStride_ == 4 &&
                diagnostics.voxelMetricsHistoryLength_ == 120,
            "Diagnostics settings should preserve inspect/metrics defaults");

    engine::render::FramePacingSettings pacing{};
    require(nearlyEqual(pacing.maxFpsLimit_, 60.0f) &&
                pacing.focusedIdleThrottleEnabled_ && pacing.backgroundThrottleEnabled_,
            "Frame pacing settings should preserve current throttle defaults");
    require(nearlyEqual(pacing.focusedIdleFpsLimit_, 15.0f) &&
                nearlyEqual(pacing.focusedIdleDelaySeconds_, 0.35f) &&
                nearlyEqual(pacing.backgroundFpsLimit_, 30.0f),
            "Frame pacing settings should preserve current throttle limits");

    voxelDebug.voxelFreezeCamera_ = true;
    voxelDebug.proceduralFishCount_ = 7;
    diagnostics.viewMode_ = 3;
    diagnostics.pixelInspectEnabled_ = true;
    pacing.maxFpsLimit_ = 120.0f;
    require(voxelDebug.voxelFreezeCamera_ && voxelDebug.proceduralFishCount_ == 7 &&
                diagnostics.viewMode_ == 3 && diagnostics.pixelInspectEnabled_ &&
                nearlyEqual(pacing.maxFpsLimit_, 120.0f),
            "Debug/diagnostics/pacing settings should be directly mutable plain data");
}

void testRenderResolutionContract()
{
    using namespace engine::render;

    const RenderResolutionSettings defaults{};
    require(nearlyEqual(defaults.scale, 1.0f) &&
                defaults.upscaleMode == SpatialUpscaleMode::EdgeAdaptive &&
                nearlyEqual(defaults.sharpness, 0.20f),
            "Render resolution should default to native output and the edge-adaptive filter");

    const VkExtent2D presentation{2560u, 1440u};
    const VkExtent2D native = internalRenderExtent(presentation, 1.0f);
    const VkExtent2D quality = internalRenderExtent(presentation, kQualityRenderScale);
    const VkExtent2D performance =
        internalRenderExtent(presentation, kPerformanceRenderScale);
    require(native.width == 2560u && native.height == 1440u,
            "Native render scale should preserve the presentation extent");
    require(quality.width == 1920u && quality.height == 1080u,
            "Quality render scale should resolve native Retina output from 1920x1080");
    require(performance.width == 1707u && performance.height == 960u,
            "Performance render scale should preserve the two-thirds pixel contract");
    require(!requiresSpatialUpscale(native, presentation) &&
                requiresSpatialUpscale(quality, presentation),
            "Spatial upscaling should run only when internal and presentation extents differ");

    RenderResolutionSettings invalid{};
    invalid.scale = std::numeric_limits<float>::quiet_NaN();
    invalid.sharpness = 4.0f;
    invalid.edgeStrength = -2.0f;
    invalid.upscaleMode = static_cast<SpatialUpscaleMode>(99u);
    const RenderResolutionSettings sanitized =
        sanitizeRenderResolutionSettings(invalid);
    require(nearlyEqual(sanitized.scale, 1.0f) &&
                nearlyEqual(sanitized.sharpness, 1.0f) &&
                nearlyEqual(sanitized.edgeStrength, 0.0f) &&
                sanitized.upscaleMode == SpatialUpscaleMode::EdgeAdaptive,
            "Render resolution sanitization should fail closed to portable settings");
    require(nearlyEqual(sanitizeRenderScale(0.1f), kMinimumRenderScale) &&
                nearlyEqual(sanitizeRenderScale(2.0f), kMaximumRenderScale),
            "Render scale should stay inside the supported performance-quality range");
}

void testAuxiliaryRayResolutionContract()
{
    using namespace engine::render;

    const ShadowSettings defaultShadows{};
    const AmbientOcclusionSettings defaultAo{};
    require(kAdaptiveAuxiliaryRayResolutionProductDefault &&
                defaultShadows.adaptiveRayResolution_ &&
                defaultAo.adaptiveRayResolution_,
            "Qualified adaptive auxiliary rays should be the product default for both consumers");

    const VkExtent2D sceneExtent{1920u, 1080u};
    const VkExtent2D native = auxiliaryRayExtent(sceneExtent, 1.0f);
    const VkExtent2D quality = auxiliaryRayExtent(sceneExtent, kQualityAuxiliaryRayScale);
    const VkExtent2D half = auxiliaryRayExtent(sceneExtent, kHalfResolutionAuxiliaryRayScale);
    require(native.width == 1920u && native.height == 1080u &&
                quality.width == 1440u && quality.height == 810u &&
                half.width == 960u && half.height == 540u,
            "Auxiliary ray extents should scale independently from the scene target");

    const VkExtent2D rounded = auxiliaryRayExtent({5u, 3u}, 0.5f);
    require(rounded.width == 3u && rounded.height == 2u,
            "Auxiliary ray extents should use stable nearest-dimension rounding");
    require(nearlyEqual(sanitizeAuxiliaryRayScale(0.1f), kMinimumAuxiliaryRayScale) &&
                nearlyEqual(sanitizeAuxiliaryRayScale(2.0f), kMaximumAuxiliaryRayScale) &&
                nearlyEqual(sanitizeAuxiliaryRayScale(
                                std::numeric_limits<float>::quiet_NaN()),
                            1.0f),
            "Auxiliary ray scales should fail closed to the portable native tier");
    require(auxiliaryRayExtent({}, 0.5f).width == 0u &&
                std::string(auxiliaryRayScaleName(0.5f)) == "Half" &&
                std::string(auxiliaryRayScaleName(0.75f)) == "Quality" &&
                std::string(auxiliaryRayScaleName(1.0f)) == "Native",
            "Auxiliary ray scale labels and zero-extent behavior should remain stable");
    require(effectiveAmbientOcclusionRayCount(2u, 1.0f) == 2u &&
                effectiveAmbientOcclusionRayCount(2u, 0.75f) == 2u &&
                effectiveAmbientOcclusionRayCount(2u, 0.5f) == 4u &&
                effectiveAmbientOcclusionRayCount(1u, 0.5f) == 2u &&
                effectiveAmbientOcclusionRayCount(4u, 0.5f) == 4u,
            "Half-resolution AO should trade part of its pixel savings for a stable sample count");
    require(effectiveSunShadowSampleCount(4u, 1.0f, 0.052f) == 4u &&
                effectiveSunShadowSampleCount(4u, 0.75f, 0.052f) == 6u &&
                effectiveSunShadowSampleCount(4u, 0.5f, 0.052f) == 8u,
            "Soft sun shadows should spend 4/6/8 samples at Native/Quality/Half");
    require(effectiveSunShadowSampleCount(2u, 0.5f, 0.052f, true) == 4u &&
                effectiveSunShadowSampleCount(2u, 0.5f, 0.052f, false) == 2u,
            "The isolated fixed-budget route should bypass reduced-resolution compensation");
    require(effectiveSunShadowSampleCount(4u, 1.0f, 0.0f) == 4u &&
                effectiveSunShadowSampleCount(4u, 0.75f, 0.0f) == 4u &&
                effectiveSunShadowSampleCount(4u, 0.5f, 0.0f) == 4u &&
                effectiveSunShadowSampleCount(8u, 0.5f, 0.052f) ==
                    kMaximumSunShadowSampleCount &&
                effectiveSunShadowSampleCount(
                    4u, std::numeric_limits<float>::quiet_NaN(), 0.052f) == 4u &&
                effectiveSunShadowSampleCount(
                    4u, 0.5f, std::numeric_limits<float>::quiet_NaN()) == 4u,
            "Sun-shadow compensation should preserve point suns, clamp high counts, and fail closed");
    require(useSunShadowNeighborhoodClamp(false, 0.052f) &&
                !useSunShadowNeighborhoodClamp(true, 0.052f),
            "Only reconstructed soft sun shadows should bypass neighborhood clamping");
    require(useSunShadowNeighborhoodClamp(false, 0.0f) &&
                useSunShadowNeighborhoodClamp(true, 0.0f) &&
                useSunShadowNeighborhoodClamp(
                    true, std::numeric_limits<float>::quiet_NaN()),
            "Point-sun and invalid shadow inputs should retain the conservative clamp");
    require(nearlyEqual(effectiveAmbientOcclusionTemporalBlendAlpha(0.08f, 1.0f),
                        0.08f) &&
                nearlyEqual(
                    effectiveAmbientOcclusionTemporalBlendAlpha(0.08f, 0.75f),
                    0.08f) &&
                nearlyEqual(
                    effectiveAmbientOcclusionTemporalBlendAlpha(0.08f, 0.5f),
                    0.04f) &&
                nearlyEqual(effectiveAmbientOcclusionTemporalBlendAlpha(
                                std::numeric_limits<float>::quiet_NaN(), 0.5f),
                            0.04f),
            "Half-resolution AO should retain twice as much valid temporal history");
    require(!useAmbientOcclusionSpatialFilter(1.0f) &&
                !useAmbientOcclusionSpatialFilter(0.75f) &&
                useAmbientOcclusionSpatialFilter(0.5f),
            "Only Half-resolution AO should pay for reconstruction-aware bilateral filtering");

    AdaptiveAuxiliaryRayState adaptiveState{};
    AdaptiveAuxiliaryRayConfig adaptiveConfig{};
    AdaptiveAuxiliaryRayDecision decision =
        resolveAdaptiveAuxiliaryRayResolution(sceneExtent, 1.0f, 0.20f, false,
                                               adaptiveConfig, adaptiveState);
    require(decision.tier == AdaptiveAuxiliaryRayTier::Native &&
                decision.activeExtent.width == sceneExtent.width && decision.changed,
            "Low projected coverage should preserve native auxiliary rays");
    decision = resolveAdaptiveAuxiliaryRayResolution(
        sceneExtent, 1.0f, 0.50f, false, adaptiveConfig, adaptiveState);
    require(decision.tier == AdaptiveAuxiliaryRayTier::Quality &&
                decision.activeExtent.width == 1440u && decision.changed,
            "High projected coverage should enter the quality auxiliary tier");
    decision = resolveAdaptiveAuxiliaryRayResolution(
        sceneExtent, 1.0f, 0.70f, false, adaptiveConfig, adaptiveState);
    require(decision.tier == AdaptiveAuxiliaryRayTier::Half &&
                decision.activeExtent.width == 960u && decision.changed,
            "Very high projected coverage should enter the half auxiliary tier");
    decision = resolveAdaptiveAuxiliaryRayResolution(
        sceneExtent, 1.0f, 0.63f, false, adaptiveConfig, adaptiveState);
    require(decision.tier == AdaptiveAuxiliaryRayTier::Half && !decision.changed,
            "Coverage hysteresis should prevent adjacent-frame tier oscillation");
    decision = resolveAdaptiveAuxiliaryRayResolution(
        sceneExtent, 1.0f, 0.20f, true, adaptiveConfig, adaptiveState);
    require(decision.tier == AdaptiveAuxiliaryRayTier::Half &&
                decision.reason == AdaptiveAuxiliaryRayReason::CameraInsideVolume,
            "Camera-inside views should use the dedicated half-resolution auxiliary path");

    AdaptiveAuxiliaryRayState fixedState{};
    AdaptiveAuxiliaryRayConfig fixedConfig{};
    fixedConfig.enabled = false;
    decision = resolveAdaptiveAuxiliaryRayResolution(
        sceneExtent, 1.0f, 1.0f, true, fixedConfig, fixedState);
    require(decision.tier == AdaptiveAuxiliaryRayTier::Native &&
                decision.reason == AdaptiveAuxiliaryRayReason::Fixed &&
                decision.activeExtent.width == sceneExtent.width,
            "Disabling adaptation should preserve the explicit fixed-Native override");

    AdaptiveAuxiliaryRayState adaptiveAoState{};
    const AdaptiveAuxiliaryRayConfig adaptiveAoConfig =
        ambientOcclusionAdaptiveRayConfig(true);
    decision = resolveAdaptiveAuxiliaryRayResolution(
        sceneExtent, 1.0f, 0.58f, false, adaptiveAoConfig, adaptiveAoState);
    require(decision.tier == AdaptiveAuxiliaryRayTier::Native &&
                decision.activeExtent.width == sceneExtent.width,
            "Adaptive AO should skip the unprofitable intermediate Quality tier");
    decision = resolveAdaptiveAuxiliaryRayResolution(
        sceneExtent, 1.0f, 0.70f, false, adaptiveAoConfig, adaptiveAoState);
    require(decision.tier == AdaptiveAuxiliaryRayTier::Half &&
                decision.activeExtent.width == 960u,
            "Adaptive AO should still enter the profitable Half tier at high coverage");

    AdaptiveAuxiliaryRayState cappedState{};
    decision = resolveAdaptiveAuxiliaryRayResolution(
        sceneExtent, 0.5f, 0.0f, false, adaptiveConfig, cappedState);
    require(decision.activeExtent.width == 960u &&
                nearlyEqual(decision.effectiveScale, 0.5f),
            "The configured auxiliary scale should remain a hard upper quality limit");

    ProjectedCoverageAccumulator coverage{};
    coverage.includeBounds(glm::mat4(1.0f), glm::vec3(-1.0f, -1.0f, 0.0f),
                           glm::vec3(1.0f, 1.0f, 0.5f), false);
    require(nearlyEqual(coverage.coverage(), 1.0f),
            "Projected voxel coverage should conservatively cover the viewport");
    ProjectedCoverageAccumulator insideCoverage{};
    insideCoverage.includeBounds(glm::mat4(1.0f), glm::vec3(0.0f),
                                 glm::vec3(1.0f), true);
    require(insideCoverage.coveredCellCount() == 16u * 9u,
            "Near-plane and camera-inside bounds should conservatively select full coverage");
}

void testRenderQualityPresetContract()
{
    using namespace engine::render;

    const RenderQualityPresetSettings native =
        renderQualityPresetSettings(RenderQualityPreset::Native);
    const RenderQualityPresetSettings quality =
        renderQualityPresetSettings(RenderQualityPreset::Quality);
    const RenderQualityPresetSettings performance =
        renderQualityPresetSettings(RenderQualityPreset::Performance);
    require(nearlyEqual(native.renderScale, 1.0f) &&
                nearlyEqual(native.shadowRayScale, 1.0f) &&
                nearlyEqual(native.aoRayScale, 1.0f),
            "Native quality preset should preserve native scene and auxiliary targets");
    require(nearlyEqual(quality.renderScale, 0.75f) &&
                nearlyEqual(quality.shadowRayScale, 0.5f) &&
                nearlyEqual(quality.aoRayScale, 0.5f),
            "Quality preset should retain a 1080p scene with half-resolution rays on Retina");
    require(nearlyEqual(performance.renderScale, 0.60f) &&
                nearlyEqual(performance.shadowRayScale, 0.5f) &&
                nearlyEqual(performance.aoRayScale, 0.5f),
            "Performance preset should preserve the qualified M1 scale tuple");

    const VkExtent2D presentation{2560u, 1440u};
    const VkExtent2D qualityScene =
        internalRenderExtent(presentation, quality.renderScale);
    const VkExtent2D qualityAux =
        auxiliaryRayExtent(qualityScene, quality.aoRayScale);
    const VkExtent2D performanceScene =
        internalRenderExtent(presentation, performance.renderScale);
    const VkExtent2D performanceAux =
        auxiliaryRayExtent(performanceScene, performance.aoRayScale);
    require(qualityScene.width == 1920u && qualityScene.height == 1080u &&
                qualityAux.width == 960u && qualityAux.height == 540u,
            "Quality preset should derive the exact native-Retina target hierarchy");
    require(performanceScene.width == 1536u && performanceScene.height == 864u &&
                performanceAux.width == 768u && performanceAux.height == 432u,
            "Performance preset should derive the exact native-Retina target hierarchy");

    for (const RenderQualityPreset preset : kRenderQualityPresets)
    {
        const RenderQualityPresetSettings settings =
            renderQualityPresetSettings(preset);
        require(parseRenderQualityPreset(renderQualityPresetId(preset)) == preset &&
                    matchRenderQualityPreset(settings.renderScale,
                                             settings.shadowRayScale,
                                             settings.aoRayScale) == preset &&
                    std::string_view(renderQualityPresetLabel(preset)).size() > 0,
                "Every quality preset should round-trip through its ID, tuple, and label");
    }
    require(!parseRenderQualityPreset("m1-magic").has_value(),
            "Quality preset IDs should reject platform magic");
    require(matchRenderQualityPreset(0.60f, 0.50f, 0.50f) ==
                RenderQualityPreset::Performance &&
                !matchRenderQualityPreset(0.60f, 0.75f, 0.50f).has_value() &&
                !matchRenderQualityPreset(
                     std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f)
                     .has_value(),
            "Preset matching should reject custom and non-finite policy tuples");
}

void testSunShadowSamplingPolicyContract()
{
    using namespace engine::render;

    const SunShadowSamplingDecision full = resolveSunShadowSampling(
        SunShadowSamplingMode::FullPerFrame, 4);
    require(full.authoredSampleCount == 4u && full.effectiveSampleCount == 4u &&
                !full.temporallyAmortized && full.compensateReducedResolution,
            "Full sun-shadow sampling should preserve all authored samples");

    const SunShadowSamplingDecision candidate = resolveSunShadowSampling(
        SunShadowSamplingMode::TemporalTwoOfFour, 4);
    require(candidate.authoredSampleCount == 4u &&
                candidate.effectiveSampleCount == 2u &&
                candidate.temporallyAmortized &&
                candidate.compensateReducedResolution,
            "Temporal sun-shadow policy should consume two samples from an authored four");

    const SunShadowSamplingDecision uncompensated = resolveSunShadowSampling(
        SunShadowSamplingMode::TemporalTwoOfFourUncompensated, 4);
    require(uncompensated.authoredSampleCount == 4u &&
                uncompensated.effectiveSampleCount == 2u &&
                uncompensated.temporallyAmortized &&
                !uncompensated.compensateReducedResolution,
            "The isolated temporal route should keep a fixed two-sample budget");

    const SunShadowSamplingDecision nonFour = resolveSunShadowSampling(
        SunShadowSamplingMode::TemporalTwoOfFour, 3);
    const SunShadowSamplingDecision invalid = resolveSunShadowSampling(
        SunShadowSamplingMode::TemporalTwoOfFour, -5);
    const SunShadowSamplingDecision uncompensatedNonFour = resolveSunShadowSampling(
        SunShadowSamplingMode::TemporalTwoOfFourUncompensated, 3);
    const SunShadowSamplingDecision oversized = resolveSunShadowSampling(
        SunShadowSamplingMode::FullPerFrame, 13);
    require(nonFour.effectiveSampleCount == 3u && !nonFour.temporallyAmortized &&
                invalid.authoredSampleCount == 1u &&
                invalid.effectiveSampleCount == 1u && !invalid.temporallyAmortized &&
                uncompensatedNonFour.effectiveSampleCount == 3u &&
                !uncompensatedNonFour.temporallyAmortized &&
                uncompensatedNonFour.compensateReducedResolution &&
                oversized.authoredSampleCount == kMaximumSunShadowSampleCount &&
                oversized.effectiveSampleCount == kMaximumSunShadowSampleCount,
            "Sun-shadow sampling should fail closed for non-four and out-of-range counts");

    require(parseSunShadowSamplingMode("full") ==
                SunShadowSamplingMode::FullPerFrame &&
                parseSunShadowSamplingMode("temporal-2") ==
                    SunShadowSamplingMode::TemporalTwoOfFour &&
                parseSunShadowSamplingMode("temporal-2-uncompensated") ==
                    SunShadowSamplingMode::TemporalTwoOfFourUncompensated &&
                !parseSunShadowSamplingMode("adaptive-magic").has_value() &&
                std::string_view(sunShadowSamplingModeId(
                    SunShadowSamplingMode::FullPerFrame)) == "full" &&
                std::string_view(sunShadowSamplingModeId(
                    SunShadowSamplingMode::TemporalTwoOfFourUncompensated)) ==
                    "temporal-2-uncompensated",
            "Sun-shadow sampling IDs should be stable and invalid IDs should fail closed");

    require(engine::performance::parseFixedSceneTimeSeconds("1.25") == 1.25 &&
                engine::performance::parseFixedSceneTimeSeconds("0") == 0.0 &&
                !engine::performance::parseFixedSceneTimeSeconds("-1").has_value() &&
                !engine::performance::parseFixedSceneTimeSeconds("nan").has_value() &&
                !engine::performance::parseFixedSceneTimeSeconds("1.0junk").has_value(),
            "Fixed benchmark scene time should accept only strict finite non-negative values");
}

void testAmbientOcclusionPushConstantsPreserveLiveControls()
{
    AOPass::FrameInputs inputs{};
    inputs.invViewProj = glm::mat4(2.0f);
    inputs.cameraPosition = glm::vec3(1.0f, 2.0f, 3.0f);
    inputs.maxDistance = 5.5f;
    inputs.stepSize = 0.18f;
    inputs.intensity = 0.88f;
    inputs.bias = 0.045f;
    inputs.extent = {768u, 432u};
    inputs.volumeCount = 37u;
    inputs.frameIndex = 41u;
    inputs.rayCount = 2u;
    inputs.projectedDistanceScale = 0.411f;
    inputs.projectedMinDistance = 2.0f;

    const AOPass::PushConstants pc = AOPass::buildPushConstants(inputs);
    require(nearlyEqual(pc.invViewProj[0][0], 2.0f) &&
                nearlyEqual(pc.camPos.x, 1.0f) && nearlyEqual(pc.camPos.y, 2.0f) &&
                nearlyEqual(pc.camPos.z, 3.0f) && nearlyEqual(pc.camPos.w, 1.0f),
            "AO push constants should preserve the live camera transform");
    require(nearlyEqual(pc.aoParams.x, inputs.maxDistance) &&
                nearlyEqual(pc.aoParams.y, inputs.stepSize) &&
                nearlyEqual(pc.aoParams.z, inputs.intensity) &&
                nearlyEqual(pc.aoParams.w, inputs.bias),
            "AO reach, sample step, intensity, and bias should reach the GPU contract exactly");
    require(pc.resolution == glm::ivec2(768, 432) &&
                pc.volumeCount == 37u && pc.frameIndex == 41u && pc.rayCount == 2u &&
                nearlyEqual(pc.projectedDistanceScale, 0.411f) &&
                nearlyEqual(pc.projectedMinDistance, 2.0f),
            "AO extent, routing, sample count, and projected tier should reach the GPU contract");

    inputs.maxDistance = 1.25f;
    inputs.intensity = 1.6f;
    const AOPass::PushConstants edited = AOPass::buildPushConstants(inputs);
    require(nearlyEqual(edited.aoParams.x, 1.25f) &&
                nearlyEqual(edited.aoParams.z, 1.6f) &&
                !nearlyEqual(edited.aoParams.x, pc.aoParams.x) &&
                !nearlyEqual(edited.aoParams.z, pc.aoParams.z),
            "Live AO reach and intensity edits should produce distinct GPU parameters");
}

void testProjectedAoDistanceTierContract()
{
    using engine::render::AmbientOcclusionDistanceMode;
    using engine::render::ProjectedAoDistanceTier;
    require(engine::render::ambientOcclusionDistanceModeFromName(
                "authored-world-distance") ==
                AmbientOcclusionDistanceMode::AuthoredWorldDistance &&
                engine::render::ambientOcclusionDistanceModeFromName(
                    "projected-screen-radius") ==
                    AmbientOcclusionDistanceMode::ProjectedScreenRadius &&
                !engine::render::ambientOcclusionDistanceModeFromName("invalid") &&
                engine::render::ambientOcclusionDistanceModeName(
                    AmbientOcclusionDistanceMode::ProjectedScreenRadius) ==
                    "projected-screen-radius",
            "AO distance mode names must round-trip and reject unknown names");

    const ProjectedAoDistanceTier disabled{};
    require(!disabled.enabled() &&
                nearlyEqual(engine::render::projectedAoDistanceScale(
                                disabled, glm::radians(60.0f), 1440.0f),
                            0.0f),
            "The projected AO tier must default to a strict no-op");

    const ProjectedAoDistanceTier authored =
        engine::render::projectedAoDistanceTier(
            AmbientOcclusionDistanceMode::AuthoredWorldDistance, 512.0f, 2.0f);
    const ProjectedAoDistanceTier projected =
        engine::render::projectedAoDistanceTier(
            AmbientOcclusionDistanceMode::ProjectedScreenRadius, 512.0f, 2.0f);
    require(!authored.enabled() && projected.enabled() &&
                nearlyEqual(projected.radiusPixels, 512.0f) &&
                nearlyEqual(projected.minimumWorldDistance, 2.0f),
            "Only the explicit projected-screen-radius mode may enable the tier");
    const ProjectedAoDistanceTier invalidMinimum{512.0f, -1.0f};
    require(!invalidMinimum.enabled(),
            "A projected AO tier with a negative minimum distance must be disabled");
    const ProjectedAoDistanceTier override{384.0f, 1.5f};
    require(engine::render::resolveProjectedAoDistanceTier(projected, disabled)
                    .radiusPixels == projected.radiusPixels &&
                engine::render::resolveProjectedAoDistanceTier(projected, override)
                    .radiusPixels == override.radiusPixels,
            "A valid automation tier must override, not mutate, the configured tier");

    const ProjectedAoDistanceTier tier{384.0f, 2.0f};
    const float scale = engine::render::projectedAoDistanceScale(
        tier, glm::radians(60.0f), 1440.0f);
    require(tier.enabled() && nearlyEqual(scale, 0.307920f, 0.00001f),
            "The AO tier should convert its screen radius through vertical FOV and height");
    require(nearlyEqual(engine::render::projectedAoTraceDistance(
                            5.5f, 4.0f, scale, tier.minimumWorldDistance),
                        2.0f) &&
                nearlyEqual(engine::render::projectedAoTraceDistance(
                                5.5f, 10.0f, scale, tier.minimumWorldDistance),
                            3.07920f, 0.0001f) &&
                nearlyEqual(engine::render::projectedAoTraceDistance(
                                5.5f, 30.0f, scale, tier.minimumWorldDistance),
                            5.5f),
            "Projected AO reach should retain contact range, grow with distance, and cap at the authored maximum");
    require(nearlyEqual(engine::render::projectedAoTraceDistance(
                            5.5f, 10.0f, 0.0f, tier.minimumWorldDistance),
                        5.5f),
            "A disabled projected scale must preserve the authored AO distance exactly");

    require(nearlyEqual(engine::render::ambientOcclusionDistanceFalloff(0.2f, 1.0f),
                        0.64f) &&
                nearlyEqual(engine::render::ambientOcclusionDistanceFalloff(1.0f, 1.0f),
                            0.0f) &&
                nearlyEqual(engine::render::ambientOcclusionDistanceFalloff(0.0f, 1.0f),
                            1.0f) &&
                nearlyEqual(engine::render::ambientOcclusionDistanceFalloff(0.2f, 2.0f),
                            0.81f),
            "AO distance falloff must give trace reach bounded, visible spatial authority");
    require(nearlyEqual(engine::render::ambientOcclusionDistanceFalloff(-1.0f, 1.0f),
                        0.0f) &&
                nearlyEqual(engine::render::ambientOcclusionDistanceFalloff(0.2f, 0.0f),
                            0.0f),
            "AO distance falloff must fail closed for invalid hit or reach values");
}

void testVoxelCellVariationContract()
{
    const engine::VoxelCellVariationSettings defaults{};
    require(!defaults.enabled(),
            "Voxel-cell variation must default to the compatibility-off state");
    require(nearlyEqual(
                engine::voxelCellVariationEffectiveStrength(
                    defaults, engine::VoxelMaterialCategory::Plant),
                0.0f),
            "Default plant variation strength must be mathematically zero");

    const glm::vec3 authoredColor(0.35f, 0.68f, 0.24f);
    const glm::ivec3 voxel(12, -3, 41);
    const glm::vec3 unchanged = engine::applyVoxelCellAlbedoVariation(
        authoredColor, engine::VoxelMaterialCategory::Plant, voxel, 7u, defaults);
    require(unchanged == authoredColor,
            "All-zero variation must return the authored color exactly");

    require(engine::voxelCellVariationPresetFromName("off") ==
                engine::VoxelCellVariationPreset::Disabled &&
                engine::voxelCellVariationPresetFromName("soft-v0") ==
                    engine::VoxelCellVariationPreset::SoftOutdoorV0 &&
                engine::voxelCellVariationPresetFromName("painted-v0") ==
                    engine::VoxelCellVariationPreset::PaintedOutdoorV0 &&
                !engine::voxelCellVariationPresetFromName("unknown"),
            "Voxel-cell variation preset parsing should be explicit");

    const engine::VoxelCellVariationSettings soft =
        engine::makeVoxelCellVariationSettings(
            engine::VoxelCellVariationPreset::SoftOutdoorV0);
    require(soft.enabled() && nearlyEqual(soft.masterStrength, 1.0f),
            "Soft outdoor variation should be an enabled explicit preset");
    require(soft.plantAmplitude > soft.woodAmplitude &&
                soft.woodAmplitude > soft.stoneAmplitude &&
                soft.stoneAmplitude > soft.genericAmplitude,
            "Soft outdoor category amplitudes should remain conservative and ordered");
    require(nearlyEqual(
                engine::voxelCellVariationEffectiveStrength(
                    soft, engine::VoxelMaterialCategory::Fish),
                0.0f),
            "Unowned aquarium categories must retain zero variation");

    const engine::VoxelCellVariationSettings painted =
        engine::makeVoxelCellVariationSettings(
            engine::VoxelCellVariationPreset::PaintedOutdoorV0);
    require(painted.enabled() && painted.hueSpread > soft.hueSpread * 3.0f &&
                painted.saturationSpread > soft.saturationSpread * 2.0f &&
                painted.valueSpread > soft.valueSpread * 2.0f &&
                painted.paletteFamilyStrength > 0.75f,
            "Painted outdoor variation must own an intentionally broader stable palette");
    const glm::vec3 paintedColor = engine::applyVoxelCellAlbedoVariation(
        authoredColor, engine::VoxelMaterialCategory::Plant, voxel, 7u, painted);
    const glm::vec3 softColor = engine::applyVoxelCellAlbedoVariation(
        authoredColor, engine::VoxelMaterialCategory::Plant, voxel, 7u, soft);
    require(paintedColor != softColor,
            "Painted palette families must produce a distinct color treatment");

    const engine::VoxelCellVariationSignal first =
        engine::voxelCellVariationSignal(voxel, 7u);
    const engine::VoxelCellVariationSignal repeated =
        engine::voxelCellVariationSignal(voxel, 7u);
    const engine::VoxelCellVariationSignal adjacent =
        engine::voxelCellVariationSignal(voxel + glm::ivec3(1, 0, 0), 7u);
    const engine::VoxelCellVariationSignal otherVolume =
        engine::voxelCellVariationSignal(voxel, 8u);
    require(first == repeated,
            "Integer-cell variation must be deterministic for the same cell and volume");
    require(first != adjacent,
            "Adjacent integer cells should not reuse one variation signal");
    require(first != otherVolume,
            "Stable volume key must decorrelate equal local coordinates");
    require(std::abs(first.hue) <= 1.0f &&
                std::abs(first.saturation) <= 1.0f &&
                std::abs(first.value) <= 1.0f,
            "Variation signals must remain normalized");

    const glm::vec3 varied = engine::applyVoxelCellAlbedoVariation(
        authoredColor, engine::VoxelMaterialCategory::Plant, voxel, 7u, soft);
    const glm::vec3 variedAgain = engine::applyVoxelCellAlbedoVariation(
        authoredColor, engine::VoxelMaterialCategory::Plant, voxel, 7u, soft);
    require(varied == variedAgain && glm::length(varied - authoredColor) > 1e-5f,
            "Enabled variation must be stable and alter responsive categories");

    const glm::vec3 fishUnchanged = engine::applyVoxelCellAlbedoVariation(
        authoredColor, engine::VoxelMaterialCategory::Fish, voxel, 7u, soft);
    require(fishUnchanged == authoredColor,
            "Aquarium fish must remain unchanged by the outdoor preset");
}

void testHemisphereAmbientContract()
{
    using engine::render::HemisphereAmbientPreset;
    using engine::render::HemisphereAmbientSettings;

    const auto nearlyEqualVec3 = [](const glm::vec3& lhs, const glm::vec3& rhs) {
        return nearlyEqual(lhs.x, rhs.x) && nearlyEqual(lhs.y, rhs.y) &&
               nearlyEqual(lhs.z, rhs.z);
    };

    const glm::vec3 legacyAmbient(0.18f, 0.42f, 0.76f);
    HemisphereAmbientSettings defaults{};
    require(!defaults.enabled(),
            "Hemisphere ambient must default to the compatibility-off state");

    // strength zero must bypass tint evaluation exactly, even if stale live
    // tint values remain in the bucket.
    defaults.skyTint = glm::vec3(0.2f, 1.8f, 0.4f);
    defaults.groundTint = glm::vec3(1.7f, 0.1f, 0.9f);
    const glm::vec3 exactCompatibility =
        engine::render::evaluateHemisphereAmbientColor(
            legacyAmbient, glm::vec3(0.3f, -0.7f, 0.2f), defaults);
    require(exactCompatibility == legacyAmbient,
            "Zero-strength hemisphere ambient must return the legacy color exactly");

    require(engine::render::hemisphereAmbientPresetFromName("off") ==
                HemisphereAmbientPreset::Disabled &&
                engine::render::hemisphereAmbientPresetFromName("warm-cool-v0") ==
                    HemisphereAmbientPreset::WarmCoolOutdoorV0 &&
                !engine::render::hemisphereAmbientPresetFromName("unknown"),
            "Hemisphere ambient preset parsing should be explicit");

    const HemisphereAmbientSettings candidate =
        engine::render::makeHemisphereAmbientSettings(
            HemisphereAmbientPreset::WarmCoolOutdoorV0);
    require(candidate.enabled() && nearlyEqual(candidate.strength, 1.0f),
            "Warm/cool outdoor ambient should be an enabled explicit preset");
    require(nearlyEqualVec3(candidate.skyTint, glm::vec3(0.72f, 1.00f, 1.32f)) &&
                nearlyEqualVec3(candidate.groundTint, glm::vec3(1.35f, 0.85f, 0.55f)),
            "The visually calibrated warm/cool preset values must remain explicit");
    require(candidate.skyTint.b > candidate.skyTint.r &&
                candidate.groundTint.r > candidate.groundTint.b,
            "The candidate should encode cool sky and warm ground separation");

    const glm::vec3 up = engine::render::evaluateHemisphereAmbientColor(
        legacyAmbient, glm::vec3(0.0f, 1.0f, 0.0f), candidate);
    const glm::vec3 down = engine::render::evaluateHemisphereAmbientColor(
        legacyAmbient, glm::vec3(0.0f, -1.0f, 0.0f), candidate);
    const glm::vec3 side = engine::render::evaluateHemisphereAmbientColor(
        legacyAmbient, glm::vec3(1.0f, 0.0f, 0.0f), candidate);
    require(nearlyEqualVec3(up, legacyAmbient * candidate.skyTint) &&
                nearlyEqualVec3(down, legacyAmbient * candidate.groundTint),
            "Up/down normals must select the authored sky/ground ambient tints");
    require(nearlyEqualVec3(side, (up + down) * 0.5f),
            "A horizontal normal must receive the hemisphere midpoint");

    HemisphereAmbientSettings equalTones{};
    equalTones.strength = 1.0f;
    const glm::vec3 equalToneResult =
        engine::render::evaluateHemisphereAmbientColor(
            legacyAmbient, glm::vec3(-0.4f, 0.2f, 0.8f), equalTones);
    require(nearlyEqualVec3(equalToneResult, legacyAmbient),
            "Equal unit hemisphere tones must reproduce flat legacy ambient");

    const glm::vec3 normalizedResult =
        engine::render::evaluateHemisphereAmbientColor(
            legacyAmbient, glm::vec3(0.0f, 1.0f, 0.0f), candidate);
    const glm::vec3 scaledNormalResult =
        engine::render::evaluateHemisphereAmbientColor(
            legacyAmbient, glm::vec3(0.0f, 12.0f, 0.0f), candidate);
    require(nearlyEqualVec3(normalizedResult, scaledNormalResult),
            "Hemisphere orientation must not depend on normal magnitude");
}

void testGBufferFrameUboBuilder()
{
    GBufferPass::FrameUboInputs in{};
    in.view = glm::mat4(2.0f);
    in.proj = glm::mat4(3.0f);
    in.viewProj = glm::mat4(4.0f);
    in.viewProjUnjittered = glm::mat4(5.0f);
    in.prevViewProjUnjittered = glm::mat4(6.0f);
    in.prevViewProj = glm::mat4(7.0f);
    in.invViewProjUnjittered = glm::mat4(8.0f);
    in.cameraWorld = glm::vec3(1.0f, 2.0f, 3.0f);
    in.renderExtent = VkExtent2D{1920u, 1080u};

    const GBufferPass::FrameUbo ubo = GBufferPass::buildFrameUbo(in);
    require(nearlyEqual(ubo.view[0][0], 2.0f) &&
                nearlyEqual(ubo.proj[0][0], 3.0f) &&
                nearlyEqual(ubo.viewProj[0][0], 4.0f) &&
                nearlyEqual(ubo.viewProjUnjittered[0][0], 5.0f) &&
                nearlyEqual(ubo.prevViewProjUnjittered[0][0], 6.0f) &&
                nearlyEqual(ubo.prevViewProj[0][0], 7.0f) &&
                nearlyEqual(ubo.invViewProjUnjittered[0][0], 8.0f),
            "GBufferPass frame UBO builder should preserve matrix inputs");
    require(nearlyEqual(ubo.cameraWorld.x, 1.0f) &&
                nearlyEqual(ubo.cameraWorld.y, 2.0f) &&
                nearlyEqual(ubo.cameraWorld.z, 3.0f) &&
                nearlyEqual(ubo.cameraWorld.w, 1.0f),
            "GBufferPass frame UBO builder should pack camera position as a point");
    require(nearlyEqual(ubo.renderSize.x, 1920.0f) &&
                nearlyEqual(ubo.renderSize.y, 1080.0f) &&
                nearlyEqual(ubo.invRenderSize.x, 1.0f / 1920.0f) &&
                nearlyEqual(ubo.invRenderSize.y, 1.0f / 1080.0f),
            "GBufferPass frame UBO builder should derive render size and inverse size");

    in.renderExtent = VkExtent2D{0u, 0u};
    const GBufferPass::FrameUbo zeroExtentUbo = GBufferPass::buildFrameUbo(in);
    require(nearlyEqual(zeroExtentUbo.renderSize.x, 0.0f) &&
                nearlyEqual(zeroExtentUbo.renderSize.y, 0.0f) &&
                nearlyEqual(zeroExtentUbo.invRenderSize.x, 1.0f) &&
                nearlyEqual(zeroExtentUbo.invRenderSize.y, 1.0f),
            "GBufferPass frame UBO builder should preserve the legacy zero-extent fallback");
}

void testLightingFrameUboBuilder()
{
    LightingPass::FrameUboInputs in{};
    in.invViewProj = glm::mat4(2.0f);
    in.viewProj = glm::mat4(3.0f);
    in.view = glm::mat4(4.0f);
    in.lightViewProj[0] = glm::mat4(5.0f);
    in.lightViewProj[1] = glm::mat4(6.0f);
    in.cameraPosition = glm::vec3(1.0f, 2.0f, 3.0f);
    in.lightDirection = glm::vec3(0.2f, -0.8f, 0.4f);
    in.sunAngularRadius = 0.05f;
    in.sunColor = glm::vec3(0.8f, 0.7f, 0.6f);
    in.sunIntensity = 2.5f;
    in.cascadeSplits = glm::vec4(10.0f, 20.0f, 40.0f, 80.0f);
    in.shadowExtent = VkExtent2D{4096u, 2048u};
    in.waterLevel = 12.0f;
    in.animationTime = 3.5f;
    in.waterCausticsScale = 0.2f;
    in.waterCausticsSpeed = 0.4f;
    in.waterCausticsIntensity = 0.6f;
    in.waterCausticsBanding = 0.8f;
    in.waterCausticsDepthFade = 16.0f;
    in.sceneAtmosphere = engine::render::makeSceneAtmosphereSettings(
        engine::render::SceneAtmospherePreset::OutdoorHazeV0);
    in.paintedSky = engine::render::makePaintedSkySettings(
        engine::render::PaintedSkyPreset::SoftDayV0);
    in.paintedClouds = engine::render::makePaintedCloudSettings(
        engine::render::PaintedCloudPreset::SoftDayV0);
    in.paintedCloudFrame.windDirection = glm::vec2(-0.6f, 0.8f);
    in.paintedCloudFrame.windSpeed = 2.0f;
    in.paintedCloudFrame.windStrength = 0.5f;
    in.paintedCloudFrame.animationTime = 3.5f;
    in.paintedCloudFrame.worldSeed = 2701u;

    LightingPass::FrameUbo ubo = LightingPass::buildFrameUbo(in);
    require(nearlyEqual(ubo.invViewProj[0][0], 2.0f) &&
                nearlyEqual(ubo.viewProj[0][0], 3.0f) &&
                nearlyEqual(ubo.view[0][0], 4.0f) &&
                nearlyEqual(ubo.lightViewProj[0][0][0], 5.0f) &&
                nearlyEqual(ubo.lightViewProj[1][0][0], 6.0f),
            "LightingPass frame UBO builder should preserve matrix inputs");
    require(nearlyEqual(ubo.camPos.x, 1.0f) &&
                nearlyEqual(ubo.camPos.y, 2.0f) &&
                nearlyEqual(ubo.camPos.z, 3.0f) &&
                nearlyEqual(ubo.camPos.w, 1.0f),
            "LightingPass frame UBO builder should pack camera position as a point");
    require(nearlyEqual(ubo.lightDir.x, 0.2f) &&
                nearlyEqual(ubo.lightDir.y, -0.8f) &&
                nearlyEqual(ubo.lightDir.z, 0.4f) &&
                nearlyEqual(ubo.lightDir.w, 0.05f),
            "LightingPass frame UBO builder should pack light direction and sun radius");
    require(nearlyEqual(ubo.lightColor.x, 0.8f) &&
                nearlyEqual(ubo.lightColor.y, 0.7f) &&
                nearlyEqual(ubo.lightColor.z, 0.6f) &&
                nearlyEqual(ubo.lightColor.w, 2.5f),
            "LightingPass frame UBO builder should pack sun color and intensity");
    require(nearlyEqual(ubo.cascadeSplits.x, 10.0f) &&
                nearlyEqual(ubo.cascadeSplits.y, 20.0f) &&
                nearlyEqual(ubo.cascadeSplits.z, 40.0f) &&
                nearlyEqual(ubo.cascadeSplits.w, 80.0f) &&
                nearlyEqual(ubo.shadowMapSize.x, 4096.0f) &&
                nearlyEqual(ubo.shadowMapSize.y, 2048.0f) &&
                nearlyEqual(ubo.shadowMapSize.z, 1.0f / 4096.0f) &&
                nearlyEqual(ubo.shadowMapSize.w, 1.0f / 2048.0f),
            "LightingPass frame UBO builder should derive cascade and shadow map data");
    require(nearlyEqual(ubo.caustics0.x, 12.0f) &&
                nearlyEqual(ubo.caustics0.y, 3.5f) &&
                nearlyEqual(ubo.caustics0.z, 0.2f) &&
                nearlyEqual(ubo.caustics0.w, 0.4f) &&
                nearlyEqual(ubo.caustics1.x, 0.6f) &&
                nearlyEqual(ubo.caustics1.y, 0.8f) &&
                nearlyEqual(ubo.caustics1.z, 16.0f) &&
                nearlyEqual(ubo.caustics1.w, 1.0f),
            "LightingPass frame UBO builder should pack caustics settings");
    require(nearlyEqual(ubo.atmosphere0.x, in.sceneAtmosphere.density) &&
                nearlyEqual(ubo.atmosphere0.y, in.sceneAtmosphere.heightFalloff) &&
                nearlyEqual(ubo.atmosphere0.z, in.sceneAtmosphere.baseHeight) &&
                nearlyEqual(ubo.atmosphere0.w,
                            in.sceneAtmosphere.sunPhaseStrength) &&
                nearlyEqual(ubo.atmosphere1.x,
                            in.sceneAtmosphere.sunPhaseExponent),
            "LightingPass frame UBO builder should pack the scene-atmosphere settings");
    require(nearlyEqual(ubo.atmosphere1.y, -in.lightDirection.x) &&
                nearlyEqual(ubo.atmosphere1.z, -in.lightDirection.y) &&
                nearlyEqual(ubo.atmosphere1.w, -in.lightDirection.z),
            "LightingPass must convert light travel into the atmosphere API's direction toward the sun");
    const engine::render::PaintedSkyGpuData paintedSky =
        engine::render::packPaintedSkyGpuData(in.paintedSky);
    require(ubo.paintedSky0 == paintedSky.sky0 &&
                ubo.paintedSky1 == paintedSky.sky1 &&
                ubo.paintedSky2 == paintedSky.sky2 &&
                ubo.paintedSky3 == paintedSky.sky3 &&
                ubo.paintedSky4 == paintedSky.sky4,
            "LightingPass frame UBO builder should pack the painted-sky contract");
    const engine::render::PaintedCloudGpuData paintedClouds =
        engine::render::packPaintedCloudGpuData(in.paintedClouds,
                                                 in.paintedCloudFrame);
    require(ubo.paintedCloud0 == paintedClouds.cloud0 &&
                ubo.paintedCloud1 == paintedClouds.cloud1 &&
                ubo.paintedCloud2 == paintedClouds.cloud2 &&
                ubo.paintedCloud3 == paintedClouds.cloud3 &&
                ubo.paintedCloud4 == paintedClouds.cloud4,
            "LightingPass frame UBO builder should pack the painted-cloud contract");

    in.waterCausticsEnabled = false;
    ubo = LightingPass::buildFrameUbo(in);
    require(nearlyEqual(ubo.caustics1.w, 0.0f),
            "LightingPass frame UBO builder should encode disabled caustics");
}

void testLightingProjectionJitterNormalization()
{
    const VkExtent2D renderExtent{1600u, 900u};
    const glm::vec2 jitterPixels(0.25f, -0.375f);
    const glm::vec2 jitterUv = LightingPass::normalizedProjectionJitterUv(
        jitterPixels, renderExtent);
    require(nearlyEqual(jitterUv.x, 0.25f / 1600.0f) &&
                nearlyEqual(jitterUv.y, -0.375f / 900.0f),
            "LightingPass must normalize projection jitter against the active render extent");

    const glm::vec2 zeroExtent = LightingPass::normalizedProjectionJitterUv(
        glm::vec2(0.25f, -0.375f), VkExtent2D{0u, 0u});
    require(zeroExtent == glm::vec2(0.0f),
            "LightingPass projection-jitter normalization must fail safe for a zero extent");

    const float aspect = static_cast<float>(renderExtent.width) /
                         static_cast<float>(renderExtent.height);
    const glm::mat4 projection =
        glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
    glm::mat4 jitteredProjection = projection;
    jitteredProjection[2][0] += 2.0f * jitterUv.x;
    jitteredProjection[2][1] += 2.0f * jitterUv.y;

    const auto reconstructDirection = [](const glm::mat4& inverseProjection,
                                         const glm::vec2& uv) {
        glm::vec4 view = inverseProjection *
                         glm::vec4(uv * 2.0f - glm::vec2(1.0f), 1.0f, 1.0f);
        view /= view.w;
        return glm::normalize(glm::vec3(view));
    };
    const glm::vec2 rasterUv(0.37f, 0.62f);
    const glm::vec3 expected =
        reconstructDirection(glm::inverse(projection), rasterUv);
    const glm::vec3 stabilized = reconstructDirection(
        glm::inverse(jitteredProjection), rasterUv - jitterUv);
    const glm::vec3 stillJittered = reconstructDirection(
        glm::inverse(jitteredProjection), rasterUv);
    require(glm::distance(expected, stabilized) < 1e-5f &&
                glm::distance(expected, stillJittered) > 1e-5f,
            "Lighting background UV correction must recover the unjittered ray with the correct sign");
}

void testCompositeAtmosphereUboBuilder()
{
    const engine::render::SceneAtmosphereSettings atmosphere =
        engine::render::makeSceneAtmosphereSettings(
            engine::render::SceneAtmospherePreset::OutdoorHazeV0);
    const CompositePass::AtmosphereUbo ubo =
        CompositePass::buildAtmosphereUbo(atmosphere, 3u);
    require(ubo.atmosphere0 ==
                    engine::render::packSceneAtmosphereParameters(atmosphere) &&
                nearlyEqual(ubo.atmosphere1.x, 3.0f) &&
                nearlyEqual(ubo.atmosphere1.y, 0.0f) &&
                nearlyEqual(ubo.atmosphere1.z, 0.0f) &&
                nearlyEqual(ubo.atmosphere1.w, 0.0f),
            "CompositePass must pack shared atmosphere parameters and the active water count");

    const CompositePass::AtmosphereUbo disabled =
        CompositePass::buildAtmosphereUbo({}, 0u);
    require(disabled.atmosphere0 == glm::vec4(0.0f) &&
                disabled.atmosphere1 == glm::vec4(0.0f),
            "CompositePass compatibility-off atmosphere UBO must remain exactly zero");
}

void testGlassFrameUboBuilder()
{
    GlassPass::FrameUboInputs in{};
    in.view = glm::mat4(2.0f);
    in.viewProjUnjittered = glm::mat4(3.0f);
    in.cameraPosition = glm::vec3(1.0f, 2.0f, 3.0f);
    in.tint = glm::vec3(0.2f, 0.4f, 0.6f);
    in.absorption = 0.7f;
    in.refractStrength = 0.08f;
    in.ior = 1.44f;
    in.debugMode = 4;
    in.renderExtent = VkExtent2D{1280u, 720u};
    in.thicknessDebugScale = 0.9f;
    in.refractDebugScale = 0.12f;
    in.thicknessScale = 1.5f;
    in.iridescentStrength = 0.3f;
    in.iridescentFilmThickness = 2.4f;
    in.iridescentFrequency = 5.0f;
    in.bubbleScale = 9.0f;
    in.bubbleIntensity = 0.25f;
    in.bubbleThicknessGate = 0.15f;
    in.bubbleChromaticSplit = 0.02f;
    in.reflectionColor = glm::vec3(0.1f, 0.2f, 0.3f);
    in.waterLevel = 12.5f;
    in.animationTime = 6.0f;
    in.waterWaveScale = 1.7f;
    in.waterWaveAmplitude = 0.4f;
    in.waterlineRippleEnabled = true;
    in.waterVolume.active = true;
    in.waterVolume.boundsMin = glm::vec3(-1.0f, 0.0f, 2.0f);
    in.waterVolume.boundsMax = glm::vec3(3.0f, 4.0f, 5.0f);
    in.waterVolume.surfaceHeight = 11.0f;
    in.waterVolume.shape = 2.0f;
    in.sunDirectionToSun = glm::vec3(-0.2f, 0.8f, -0.4f);
    in.sunColor = glm::vec3(2.0f, 1.5f, 1.0f);
    in.sceneAtmosphere.density = 0.012f;
    in.sceneAtmosphere.heightFalloff = 0.08f;
    in.sceneAtmosphere.baseHeight = 1.5f;
    in.sceneAtmosphere.sunPhaseStrength = 0.6f;
    in.activeWaterVolumeCount = 3;

    GlassPass::FrameUbo ubo = GlassPass::buildFrameUbo(in);
    require(nearlyEqual(ubo.view[0][0], 2.0f) &&
                nearlyEqual(ubo.viewProj[0][0], 3.0f),
            "GlassPass frame UBO builder should preserve the stable projection input");
    require(nearlyEqual(ubo.camPos.x, 1.0f) &&
                nearlyEqual(ubo.camPos.y, 2.0f) &&
                nearlyEqual(ubo.camPos.z, 3.0f) &&
                nearlyEqual(ubo.camPos.w, 1.0f),
            "GlassPass frame UBO builder should pack camera position as a point");
    require(nearlyEqual(ubo.tint.x, 0.2f) && nearlyEqual(ubo.tint.y, 0.4f) &&
                nearlyEqual(ubo.tint.z, 0.6f) && nearlyEqual(ubo.tint.w, 1.0f),
            "GlassPass frame UBO builder should pack tint");
    require(nearlyEqual(ubo.params0.x, 0.7f) &&
                nearlyEqual(ubo.params0.y, 0.08f) &&
                nearlyEqual(ubo.params0.z, 1.44f) &&
                nearlyEqual(ubo.params0.w, 4.0f),
            "GlassPass frame UBO builder should pack absorption/refraction/debug");
    require(nearlyEqual(ubo.params1.x, 1.0f / 1280.0f) &&
                nearlyEqual(ubo.params1.y, 1.0f / 720.0f) &&
                nearlyEqual(ubo.params1.z, 0.9f) &&
                nearlyEqual(ubo.params1.w, 0.12f),
            "GlassPass frame UBO builder should derive render inverse size");
    require(nearlyEqual(ubo.params2.x, 1.5f) &&
                nearlyEqual(ubo.params2.y, 0.3f) &&
                nearlyEqual(ubo.params2.z, 2.4f) &&
                nearlyEqual(ubo.params2.w, 5.0f) &&
                nearlyEqual(ubo.params3.x, 9.0f) &&
                nearlyEqual(ubo.params3.y, 0.25f) &&
                nearlyEqual(ubo.params3.z, 0.15f) &&
                nearlyEqual(ubo.params3.w, 0.02f),
            "GlassPass frame UBO builder should pack detail parameters");
    require(nearlyEqual(ubo.reflectionColor.x, 0.1f) &&
                nearlyEqual(ubo.reflectionColor.y, 0.2f) &&
                nearlyEqual(ubo.reflectionColor.z, 0.3f) &&
                nearlyEqual(ubo.reflectionColor.w, 1.0f),
            "GlassPass frame UBO builder should pack reflection color");
    require(nearlyEqual(ubo.waterWave0.x, 12.5f) &&
                nearlyEqual(ubo.waterWave0.y, 6.0f) &&
                nearlyEqual(ubo.waterWave0.z, 1.7f) &&
                nearlyEqual(ubo.waterWave0.w, 0.4f) &&
                nearlyEqual(ubo.waterWave1.x, 0.011f) &&
                nearlyEqual(ubo.waterWave1.y, 2.1f) &&
                nearlyEqual(ubo.waterWave1.z, 1.35f) &&
                nearlyEqual(ubo.waterWave1.w, 1.0f),
            "GlassPass frame UBO builder should pack waterline ripple data");
    require(nearlyEqual(ubo.waterVolume0.x, -1.0f) &&
                nearlyEqual(ubo.waterVolume0.y, 0.0f) &&
                nearlyEqual(ubo.waterVolume0.z, 2.0f) &&
                nearlyEqual(ubo.waterVolume0.w, 11.0f) &&
                nearlyEqual(ubo.waterVolume1.x, 3.0f) &&
                nearlyEqual(ubo.waterVolume1.y, 4.0f) &&
                nearlyEqual(ubo.waterVolume1.z, 5.0f) &&
                nearlyEqual(ubo.waterVolume1.w, 2.0f),
            "GlassPass frame UBO builder should pack active water volume metadata");
    require(nearlyEqual(ubo.sunDirToSun.x, -0.2f) &&
                nearlyEqual(ubo.sunDirToSun.y, 0.8f) &&
                nearlyEqual(ubo.sunDirToSun.z, -0.4f) &&
                nearlyEqual(ubo.sunDirToSun.w, 0.0f) &&
                nearlyEqual(ubo.sunColor.x, 2.0f) &&
                nearlyEqual(ubo.sunColor.y, 1.5f) &&
                nearlyEqual(ubo.sunColor.z, 1.0f) &&
                nearlyEqual(ubo.sunColor.w, 1.0f),
            "GlassPass frame UBO builder should pack sun data");
    require(nearlyEqual(ubo.atmosphere0.x, 0.012f) &&
                nearlyEqual(ubo.atmosphere0.y, 0.08f) &&
                nearlyEqual(ubo.atmosphere0.z, 1.5f) &&
                nearlyEqual(ubo.atmosphere0.w, 0.6f) &&
                nearlyEqual(ubo.atmosphere1.x, 3.0f),
            "GlassPass frame UBO builder should pack shared atmosphere and water count");

    in.renderExtent = VkExtent2D{0u, 0u};
    in.waterlineRippleEnabled = false;
    ubo = GlassPass::buildFrameUbo(in);
    require(nearlyEqual(ubo.params1.x, 1.0f) &&
                nearlyEqual(ubo.params1.y, 1.0f) &&
                nearlyEqual(ubo.waterWave1.w, 0.0f) &&
                nearlyEqual(ubo.waterVolume0.w, 0.0f) &&
                nearlyEqual(ubo.waterVolume1.w, 0.0f),
            "GlassPass frame UBO builder should preserve disabled waterline fallbacks");
}

void testVoxelGlassFrameUboBuilder()
{
    VoxelGlassRefractPass::FrameUboInputs in{};
    in.invViewProj = glm::mat4(2.0f);
    in.cameraPosition = glm::vec3(1.0f, 2.0f, 3.0f);
    in.tint = glm::vec3(0.7f, 0.8f, 0.9f);
    in.absorption = 0.21f;
    in.refractStrength = 0.032f;
    in.ior = 1.47f;
    in.debugMode = 2;
    in.renderExtent = VkExtent2D{1600u, 900u};
    in.maxThickness = 18.0f;
    in.reflectionStrength = 0.42f;
    in.reflectionColor = glm::vec3(0.11f, 0.22f, 0.33f);
    in.sceneAtmosphere.density = 0.015f;
    in.sceneAtmosphere.heightFalloff = 0.07f;
    in.sceneAtmosphere.baseHeight = -2.0f;
    in.sceneAtmosphere.sunPhaseStrength = 0.5f;
    in.activeWaterVolumeCount = 8;

    VoxelGlassRefractPass::FrameUbo ubo =
        VoxelGlassRefractPass::buildFrameUbo(in);
    require(nearlyEqual(ubo.invViewProj[0][0], 2.0f) &&
                nearlyEqual(ubo.camPos.x, 1.0f) &&
                nearlyEqual(ubo.camPos.y, 2.0f) &&
                nearlyEqual(ubo.camPos.z, 3.0f) &&
                nearlyEqual(ubo.camPos.w, 1.0f),
            "VoxelGlassRefractPass frame UBO builder should pack camera inputs");
    require(nearlyEqual(ubo.tint.x, 0.7f) && nearlyEqual(ubo.tint.y, 0.8f) &&
                nearlyEqual(ubo.tint.z, 0.9f) &&
                nearlyEqual(ubo.params0.x, 0.21f) &&
                nearlyEqual(ubo.params0.y, 0.032f) &&
                nearlyEqual(ubo.params0.z, 1.47f) &&
                nearlyEqual(ubo.params0.w, 2.0f),
            "VoxelGlassRefractPass frame UBO builder should pack optical controls");
    require(nearlyEqual(ubo.params1.x, 1.0f / 1600.0f) &&
                nearlyEqual(ubo.params1.y, 1.0f / 900.0f) &&
                nearlyEqual(ubo.params1.z, 18.0f) &&
                nearlyEqual(ubo.params1.w, 0.42f) &&
                nearlyEqual(ubo.reflectionColor.x, 0.11f) &&
                nearlyEqual(ubo.reflectionColor.y, 0.22f) &&
                nearlyEqual(ubo.reflectionColor.z, 0.33f),
            "VoxelGlassRefractPass frame UBO builder should pack render and reflection controls");
    require(nearlyEqual(ubo.atmosphere0.x, 0.015f) &&
                nearlyEqual(ubo.atmosphere0.y, 0.07f) &&
                nearlyEqual(ubo.atmosphere0.z, -2.0f) &&
                nearlyEqual(ubo.atmosphere0.w, 0.5f) &&
                nearlyEqual(ubo.atmosphere1.x, 8.0f),
            "VoxelGlassRefractPass must use the shared atmosphere packing contract");

    in.renderExtent = VkExtent2D{0u, 0u};
    in.sceneAtmosphere = {};
    in.activeWaterVolumeCount = 0;
    ubo = VoxelGlassRefractPass::buildFrameUbo(in);
    require(nearlyEqual(ubo.params1.x, 1.0f) &&
                nearlyEqual(ubo.params1.y, 1.0f) &&
                nearlyEqual(ubo.atmosphere0.x, 0.0f) &&
                nearlyEqual(ubo.atmosphere1.x, 0.0f),
            "VoxelGlassRefractPass should preserve zero-extent and disabled-atmosphere fallbacks");
}

void testPassRegistryOrderAndGating()
{
    engine::render::PassRegistry registry{};
    std::vector<std::string> observedOrder;

    registry.add("gbuffer", [&](const engine::render::RenderPassContext&) {
        observedOrder.push_back("gbuffer");
    });
    registry.add("water", [&](const engine::render::RenderPassContext&) {
        observedOrder.push_back("water");
    }, [](const engine::render::FrameInputs& inputs) {
        return inputs.renderWater;
    });
    registry.add("bloom", [&](const engine::render::RenderPassContext&) {
        observedOrder.push_back("bloom");
    }, [](const engine::render::FrameInputs& inputs) {
        return inputs.runBloom;
    });

    const std::vector<std::string_view> names = registry.names();
    require(names.size() == 3, "PassRegistry should preserve every inserted pass name");
    require(names[0] == "gbuffer" && names[1] == "water" && names[2] == "bloom",
            "PassRegistry names should preserve insertion order");

    VulkanContext ctx{};
    FrameContext frame{};
    engine::render::FrameInputs inputs{};
    inputs.renderWater = false;
    inputs.runBloom = true;
    const std::vector<std::string_view> enabledNamesWithoutWater =
        registry.enabledNames(inputs);
    require(enabledNamesWithoutWater.size() == 2 && enabledNamesWithoutWater[0] == "gbuffer" &&
                enabledNamesWithoutWater[1] == "bloom",
            "PassRegistry enabledNames should preserve enabled insertion order");
    registry.recordAll(engine::render::RenderPassContext{ctx, frame, inputs});
    require(observedOrder.size() == 2 && observedOrder[0] == "gbuffer" &&
                observedOrder[1] == "bloom",
            "PassRegistry should skip disabled passes while preserving remaining order");

    observedOrder.clear();
    inputs.renderWater = true;
    registry.recordAll(engine::render::RenderPassContext{ctx, frame, inputs});
    require(observedOrder.size() == 3 && observedOrder[0] == "gbuffer" &&
                observedOrder[1] == "water" && observedOrder[2] == "bloom",
            "PassRegistry should record enabled passes in insertion order");
}

void testRenderPipelineShowcaseContract()
{
    const auto stages = engine::render::renderPipelineShowcaseStages();
    require(stages.size() == 18,
            "Render showcase should keep the documented 18-stage capture sequence");
    require(equals(stages.front().title, "G-BUFFER / ALBEDO") &&
                stages.front().compositeMode == 1,
            "Render showcase should begin with the first visible surface buffer");
    require(stages[4].shadowDebugMode == 1 && stages[5].shadowDebugMode == 2 &&
                stages[4].compositeMode == 17 && stages[5].compositeMode == 17,
            "Render showcase should expose raw and resolved DDA sun shadows");
    require(equals(stages.back().title, "FINAL COMPOSITE") &&
                stages.back().compositeMode == 0,
            "Render showcase should finish on the normal final composite");
    require(stages[12].compositeMode == 15 && stages[16].compositeMode == 16,
            "Render showcase should expose pre-post and temporal-resolve views");

    engine::render::RenderPipelineShowcase showcase;
    showcase.configure(true, 2.0, false, false);
    require(showcase.enabled() && !showcase.paused() && showcase.stageIndex() == 0 &&
                nearlyEqual(static_cast<float>(showcase.secondsPerStage()), 2.0f),
            "Render showcase should start at the first timed stage");
    require(!showcase.update(1.0) && showcase.stageIndex() == 0 &&
                nearlyEqual(showcase.stageProgress(), 0.5f),
            "Render showcase should report progress without advancing early");
    require(showcase.update(1.0) && showcase.stageIndex() == 1,
            "Render showcase should advance at the configured boundary");
    require(showcase.next() && showcase.stageIndex() == 2,
            "Render showcase manual next should advance and reset time");
    require(showcase.previous() && showcase.stageIndex() == 1,
            "Render showcase manual previous should move back");

    showcase.togglePaused();
    require(showcase.paused() && !showcase.update(4.0) && showcase.stageIndex() == 1,
            "A paused render showcase should not advance");
    showcase.togglePaused();
    while (showcase.next())
    {
    }
    require(showcase.stageIndex() + 1 == showcase.stageCount(),
            "Manual stepping should stop on the final showcase stage");
    require(!showcase.update(1.0) && !showcase.completed(),
            "The final stage should remain visible for its complete duration");
    require(!showcase.update(1.0) && showcase.completed() &&
                nearlyEqual(showcase.stageProgress(), 1.0f),
            "A non-looping showcase should hold the completed final frame");

    showcase.configure(true, 0.25, true, false);
    require(showcase.update(0.25 * static_cast<double>(showcase.stageCount())) &&
                showcase.stageIndex() == 0 && !showcase.completed(),
            "A looping showcase should wrap from final composite to the first G-buffer view");
}

void testFramePassRecordOrderCharacterization()
{
    engine::render::PassRegistry registry{};
    std::vector<std::string> observedOrder;

    for (std::string_view name : engine::render::kPassRecordOrder)
    {
        registry.add(name, [&observedOrder, name](const engine::render::RenderPassContext&) {
            observedOrder.emplace_back(name);
        });
    }

    VulkanContext ctx{};
    FrameContext frame{};
    engine::render::FrameInputs inputs{};
    registry.recordAll(engine::render::RenderPassContext{ctx, frame, inputs});

    require(orderEquals(observedOrder, engine::render::kPassRecordOrder),
            "Frame pass characterization should record the committed pass order");
    requireBefore(observedOrder, "gbuffer", "lighting",
                  "Frame characterization should keep G-buffer before lighting");
    requireBefore(observedOrder, "lighting", "bloom",
                  "Frame characterization should keep lighting before post effects");
    requireBefore(observedOrder, "bloom", "taa",
                  "Frame characterization should keep bloom before TAA");
    requireBefore(observedOrder, "taa", "spatial-upscale",
                  "Frame characterization should keep TAA before spatial reconstruction");
    requireBefore(observedOrder, "spatial-upscale", "composite-ui",
                  "Frame characterization should keep native UI after scene upscaling");

    std::vector<std::string> perturbedOrder = observedOrder;
    std::swap(perturbedOrder[indexOf(perturbedOrder, "bloom")],
              perturbedOrder[indexOf(perturbedOrder, "taa")]);
    require(!orderEquals(perturbedOrder, engine::render::kPassRecordOrder),
            "Frame pass characterization should detect a bloom/TAA perturbation");
}

void testFrameHandshakeOrderCharacterization()
{
    std::vector<std::string> observedOrder =
        copyOrder(engine::render::kFrameHandshakeOrder);

    require(orderEquals(observedOrder, engine::render::kFrameHandshakeOrder),
            "Frame characterization should preserve the committed handshake order");
    requireBefore(observedOrder, "beginFrame", "applyPendingVoxelMeshes",
                  "Frame handshake should apply pending voxel meshes after beginFrame");
    requireBefore(observedOrder, "applyPendingVoxelMeshes", "cameraMatricesAndJitter",
                  "Frame handshake should refresh voxel meshes before matrices");
    requireBefore(observedOrder, "visibilityAndSceneRebuilds", "updateLights",
                  "Frame handshake should rebuild visibility before lighting updates");
    requireBefore(observedOrder, "updateLights", "recordPasses",
                  "Frame handshake should update lights before pass recording");
    requireBefore(observedOrder, "recordPasses", "endFrame",
                  "Frame handshake should end the frame after pass recording");

    std::vector<std::string> perturbedOrder = observedOrder;
    std::swap(perturbedOrder[indexOf(perturbedOrder, "updateLights")],
              perturbedOrder[indexOf(perturbedOrder, "recordPasses")]);
    require(!orderEquals(perturbedOrder, engine::render::kFrameHandshakeOrder),
            "Frame characterization should detect update/record perturbations");
}

void testRendererOwnedFrameLoopTraceCharacterization()
{
    std::vector<std::string> observedOrder =
        copyOrder(engine::render::kRendererOwnedFrameLoopTrace);

    require(orderEquals(observedOrder, engine::render::kRendererOwnedFrameLoopTrace),
            "Renderer-owned frame-loop trace should preserve the committed shell order");
    requireBefore(observedOrder, "drawFrame.begin", "updateFrame.begin",
                  "Frame-loop trace should enter update through drawFrame");
    requireBefore(observedOrder, "updateFrame.end", "recordFrame.begin",
                  "Frame-loop trace should keep update before record");
    requireBefore(observedOrder, "recordFrame.begin", "renderer.beginFrame",
                  "Frame-loop trace should acquire only after record starts");
    requireBefore(observedOrder, "renderer.beginFrame", "applyPendingVoxelMeshes",
                  "Frame-loop trace should keep GPU upload after frame acquire");
    requireBefore(observedOrder, "visibilityAndSceneRebuilds", "updateLights",
                  "Frame-loop trace should rebuild visibility before light updates");
    requireBefore(observedOrder, "recordPasses", "finishFrameTail.begin",
                  "Frame-loop trace should finish pass recording before frame tail");
    requireBefore(observedOrder, "renderer.endFrame", "postFrameAccounting",
                  "Frame-loop trace should keep App accounting after renderer endFrame");
    requireBefore(observedOrder, "renderer.advanceFrame", "drawFrame.end",
                  "Frame-loop trace should advance renderer state before drawFrame exits");

    std::vector<std::string> perturbedOrder = observedOrder;
    std::swap(perturbedOrder[indexOf(perturbedOrder, "updateFrame.end")],
              perturbedOrder[indexOf(perturbedOrder, "recordFrame.begin")]);
    require(!orderEquals(perturbedOrder, engine::render::kRendererOwnedFrameLoopTrace),
            "Renderer-owned frame-loop trace should detect update/record seam drift");
}

class CharacterizationPass final : public engine::render::IRenderPass
{
public:
    CharacterizationPass(std::string name, std::vector<std::string>& events, bool enabled)
        : name_(std::move(name)), events_(events), enabled_(enabled)
    {
    }

    const char* name() const override { return name_.c_str(); }

    void create(VulkanContext&, const PassCreateInfo&) override { recordEvent("create"); }
    void onResize(VulkanContext&, const PassCreateInfo&) override { recordEvent("resize"); }
    void record(const engine::render::RenderPassContext&) override { recordEvent("record"); }
    void destroy(VulkanContext&) override { recordEvent("destroy"); }
    bool enabled(const engine::render::FrameInputs&) const override { return enabled_; }

private:
    void recordEvent(std::string_view phase)
    {
        std::string event{phase};
        event.push_back(':');
        event += name_;
        events_.push_back(std::move(event));
    }

    std::string name_;
    std::vector<std::string>& events_;
    bool enabled_ = true;
};

void testFramePassLifecycleOrderCharacterization()
{
    std::vector<std::string> events;
    CharacterizationPass alpha{"alpha", events, true};
    CharacterizationPass beta{"beta", events, false};

    engine::render::PassRegistry registry{};
    registry.add(&alpha);
    registry.add(&beta);

    VulkanContext ctx{};
    PassCreateInfo createInfo{};
    FrameContext frame{};
    engine::render::FrameInputs inputs{};

    registry.createAll(ctx, createInfo);
    registry.onResizeAll(ctx, createInfo);
    registry.recordAll(engine::render::RenderPassContext{ctx, frame, inputs});
    registry.destroyAll(ctx);

    const std::vector<std::string> expectedEvents = {
        "create:alpha",
        "create:beta",
        "resize:alpha",
        "resize:beta",
        "record:alpha",
        "destroy:alpha",
        "destroy:beta",
    };
    require(events == expectedEvents,
            "Frame lifecycle characterization should preserve create/resize/record/destroy order");

    std::vector<std::string> perturbedEvents = expectedEvents;
    std::swap(perturbedEvents[0], perturbedEvents[1]);
    require(perturbedEvents != expectedEvents,
            "Frame lifecycle characterization should detect lifecycle perturbations");
}

void testRendererCapabilityPredicates()
{
    using namespace engine::render;

    require(isDdaShadowsAvailable(true, true),
            "DDA shadows should be available when ray and resolve passes are available");
    require(!isDdaShadowsAvailable(false, true),
            "DDA shadows should require the shadow ray pass");
    require(!isDdaShadowsAvailable(true, false),
            "DDA shadows should require the temporal resolve pass");
    require(canUseDdaShadows(true, true, true),
            "DDA shadows should be usable when enabled and available");
    require(!canUseDdaShadows(false, true, true),
            "DDA shadows should not be usable when disabled");

    require(isAmbientOcclusionAvailable(true, true),
            "Ambient occlusion should be available when AO and resolve passes are available");
    require(!isAmbientOcclusionAvailable(false, true),
            "Ambient occlusion should require the AO ray pass");
    require(!canUseAmbientOcclusion(true, true, false),
            "Ambient occlusion should not be usable without temporal resolve");

    require(isLocalLightShadowsAvailable(true, true),
            "Local light shadows should be available when ray and resolve passes are available");
    require(!canUseLocalLightShadows(false, true, true),
            "Local light shadows should not be usable when disabled");
    require(!canUseLocalLightShadows(true, false, true),
            "Local light shadows should require the local ray pass");

    require(isLocalShadowBlurAvailable(true, true, true),
            "Local shadow blur should be available when blur and local shadows are available");
    require(!isLocalShadowBlurAvailable(true, true, false),
            "Local shadow blur should require local shadow resolve");
    require(!canUseLocalShadowBlur(false, true, true, true),
            "Local shadow blur should not be usable when disabled");

    Renderer renderer;
    Renderer::CapabilityState state{};
    state.useDdaShadows = true;
    state.ambientOcclusionEnabled = true;
    state.localLightShadowsEnabled = true;
    state.localShadowBlurEnabled = true;
    state.shadowRayPassAvailable = true;
    state.shadowTemporalResolveAvailable = true;
    state.aoPassAvailable = true;
    state.aoTemporalResolveAvailable = true;
    state.localLightShadowPassAvailable = true;
    state.localLightShadowResolveAvailable = true;
    state.localShadowBlurAvailable = true;

    require(renderer.canUseDdaShadows(state),
            "Renderer should own the live DDA shadow capability query");
    require(renderer.canUseAmbientOcclusion(state),
            "Renderer should own the live ambient-occlusion capability query");
    require(renderer.canUseLocalLightShadows(state),
            "Renderer should own the live local-light shadow capability query");
    require(renderer.canUseLocalShadowBlur(state),
            "Renderer should own the live local-shadow blur capability query");
    state.shadowTemporalResolveAvailable = false;
    require(!renderer.canUseDdaShadows(state),
            "Renderer DDA capability query should honor unavailable resolve pass");
    state.shadowTemporalResolveAvailable = true;
    state.localShadowBlurEnabled = false;
    require(!renderer.canUseLocalShadowBlur(state),
            "Renderer local-shadow blur query should honor the feature toggle");
}

void testRendererFrameLifecycleBindingContract()
{
    engine::render::Renderer renderer;
    require(!renderer.isFrameLifecycleBound(),
            "Renderer should start without App frame-lifecycle bindings");

    VulkanContext ctx{};
    bool framebufferResized = false;
    bool swapchainRecreateRequested = false;
    int recreateCount = 0;

    engine::render::Renderer::FrameLifecycleBindings bindings{};
    bindings.context = &ctx;
    bindings.framebufferResized = &framebufferResized;
    bindings.swapchainRecreateRequested = &swapchainRecreateRequested;
    bindings.recreateSwapchain = [](void* user) {
        ++(*static_cast<int*>(user));
    };
    bindings.recreateSwapchainUser = &recreateCount;

    require(renderer.bindFrameLifecycle(bindings),
            "Renderer should accept a complete App frame-lifecycle binding set");
    require(renderer.isFrameLifecycleBound(),
            "Renderer should report a complete frame-lifecycle binding set");
    renderer.recreateSwapchain();
    require(recreateCount == 1,
            "Renderer recreateSwapchain should dispatch through the bound recreation callback");
    require(renderer.currentFrameIndex() == 0,
            "Renderer should own frame sync state starting at frame zero");
    renderer.advanceFrame();
    require(renderer.currentFrameIndex() == 1,
            "Renderer should advance its owned frame sync state");

    engine::render::Renderer invalidRenderer;
    bindings.context = nullptr;
    require(!invalidRenderer.bindFrameLifecycle(bindings),
            "Renderer should reject incomplete frame-lifecycle bindings");
    require(!invalidRenderer.isFrameLifecycleBound(),
            "Renderer should not become bound after rejected lifecycle bindings");

    engine::render::Renderer callbackRenderer;
    engine::render::Renderer::FrameRunCallbacks callbacks{};
    require(!callbackRenderer.runFrameLifecycle(callbacks),
            "Renderer should reject a frame run without lifecycle callbacks");
    callbacks.record = [](void*, FrameContext&) {};
    require(!callbackRenderer.runFrameLifecycle(callbacks),
            "Renderer should reject a frame run without an after-end callback");
}

void testRendererPassLifecycleBindingContract()
{
    engine::render::Renderer renderer;
    require(!renderer.isPassLifecycleBound(),
            "Renderer should start without App pass-lifecycle bindings");

    struct Counts
    {
        int create = 0;
        int destroy = 0;
    } counts;

    engine::render::Renderer::PassLifecycleBindings bindings{};
    bindings.createPasses = [](void* user) { static_cast<Counts*>(user)->create++; };
    bindings.destroyPasses = [](void* user) { static_cast<Counts*>(user)->destroy++; };
    bindings.user = &counts;

    require(renderer.bindPassLifecycle(bindings),
            "Renderer should accept complete App pass-lifecycle bindings");
    require(renderer.isPassLifecycleBound(),
            "Renderer should report a complete pass-lifecycle binding set");
    renderer.createPasses();
    renderer.destroyPasses();
    require(counts.create == 1 && counts.destroy == 1,
            "Renderer pass lifecycle should dispatch through the bound callbacks");

    engine::render::Renderer invalidRenderer;
    bindings.destroyPasses = nullptr;
    require(!invalidRenderer.bindPassLifecycle(bindings),
            "Renderer should reject incomplete pass-lifecycle bindings");
    require(!invalidRenderer.isPassLifecycleBound(),
            "Renderer should not become bound after rejected pass lifecycle bindings");
}

void testRendererOwnsPassResourceBundle()
{
    engine::render::Renderer renderer;
    engine::render::RendererPassResources& passes = renderer.passes();
    passes.reflectionExtent = VkExtent2D{320, 180};
    passes.availability.aoPass = false;
    passes.depthHistory.index = 1;

    const engine::render::Renderer& constRenderer = renderer;
    require(&constRenderer.passes() == &passes,
            "Renderer should expose a stable renderer-owned pass resource bundle");
    require(constRenderer.passes().reflectionExtent.width == 320 &&
                constRenderer.passes().reflectionExtent.height == 180,
            "Renderer pass resource state should persist on the renderer");
    require(!constRenderer.passes().availability.aoPass,
            "Renderer should own pass availability state with its pass resources");
    require(constRenderer.passes().depthHistory.index == 1,
            "Renderer should own temporal depth-history state with its pass resources");

    passes.resetAvailability();
    require(constRenderer.passes().availability.aoPass,
            "Renderer pass-resource availability reset should restore default availability");
}

void testWorldGenerationPopulatesTerrainAndResetsChunkFlags()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(1, 1, 1), glm::ivec3(0, 0, 0));

    std::vector<Chunk>& chunks = grid.chunks();
    require(!chunks.empty(), "Test grid should contain at least one chunk");
    chunks[0].dirty.store(false);
    chunks[0].meshing.store(true);
    chunks[0].highPriority.store(true);
    chunks[0].lastAppliedBuildId = 7;
    chunks[0].nextBuildId = 99;

    const WorldGenStats stats = generateWorld(grid, 7u);

    require(stats.minHeight >= 0, "Single-layer world min height should stay in chunk bounds");
    require(stats.maxHeight < Chunk::SY,
            "Single-layer world max height should stay in chunk bounds");
    require(stats.avgHeight >= static_cast<float>(stats.minHeight),
            "Average world height should be at least the minimum height");
    require(stats.avgHeight <= static_cast<float>(stats.maxHeight),
            "Average world height should be at most the maximum height");

    bool foundSolidBlock = false;
    for (const Chunk& chunk : chunks)
    {
        require(chunk.dirty.load(), "World generation should mark chunks dirty");
        require(!chunk.meshing.load(), "World generation should clear meshing flags");
        require(!chunk.highPriority.load(), "World generation should clear priority flags");
        require(chunk.nextBuildId == chunk.lastAppliedBuildId + 1,
                "World generation should reset the next build id from the applied build id");
        require(!chunk.pending.has_value(), "World generation should clear pending mesh state");

        for (BlockId block : chunk.blocks)
        {
            if (block != BLOCK_AIR)
            {
                foundSolidBlock = true;
                break;
            }
        }
    }

    require(foundSolidBlock, "World generation should produce non-air terrain");
}

void testVoxelSystemDirtyNeighborsMarksChunkBoundaries()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(2, 1, 1), glm::ivec3(0, 0, 0));

    for (Chunk& chunk : grid.chunks())
    {
        chunk.dirty.store(false);
        chunk.highPriority.store(false);
    }

    engine::voxel::markVoxelEditDirtyNeighbors(
        grid, glm::ivec3(Chunk::SX, 4, 4), true);

    const Chunk* left = grid.getChunk(glm::ivec3(0, 0, 0));
    const Chunk* right = grid.getChunk(glm::ivec3(1, 0, 0));
    require(left != nullptr && right != nullptr,
            "VoxelSystem dirty-neighbor test should have adjacent chunks");
    require(left->dirty.load() && left->highPriority.load(),
            "VoxelSystem should mark the negative-x neighbor dirty for boundary edits");
    require(right->dirty.load() && right->highPriority.load(),
            "VoxelSystem should mark the edited chunk dirty for boundary edits");
}

void testVoxelSystemApplyVoxelEditPlacesAndRemoves()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(1, 1, 1), glm::ivec3(0, 0, 0));

    RayHit hit{};
    hit.hit = true;
    hit.voxel = glm::ivec3(1, 1, 1);
    hit.prevVoxel = glm::ivec3(1, 1, 2);

    require(engine::voxel::applyVoxelEdit(grid, hit, true, BLOCK_AIR),
            "VoxelSystem place edit should fall back from air selection to dirt");
    require(grid.getVoxelWorld(1, 1, 2) == BLOCK_DIRT,
            "VoxelSystem place edit should set the target block");

    require(!engine::voxel::applyVoxelEdit(grid, hit, true, BLOCK_STONE),
            "VoxelSystem place edit should reject occupied targets");

    hit.voxel = hit.prevVoxel;
    require(engine::voxel::applyVoxelEdit(grid, hit, false, BLOCK_STONE),
            "VoxelSystem remove edit should clear a non-air block");
    require(grid.getVoxelWorld(1, 1, 2) == BLOCK_AIR,
            "VoxelSystem remove edit should clear hit.voxel");
    require(!engine::voxel::applyVoxelEdit(grid, hit, false, BLOCK_STONE),
            "VoxelSystem remove edit should reject empty targets");

    const Chunk* chunk = grid.getChunk(glm::ivec3(0, 0, 0));
    require(chunk != nullptr && chunk->dirty.load() && chunk->highPriority.load(),
            "VoxelSystem edits should mark affected chunks high-priority dirty");
}

void testVoxelSystemCreatesGridAndGeneratesWorldState()
{
    ChunkGrid grid;
    const engine::voxel::WorldGridCreateResult createResult =
        engine::voxel::createWorldGrid(grid, glm::ivec3(2, 1, 1));

    require(grid.origin() == glm::ivec3(-1, 0, 0),
            "VoxelSystem should center created world grids on x/z");
    require(createResult.chunkCoords.size() == grid.chunks().size(),
            "VoxelSystem grid creation should return one render coord per chunk");
    require(createResult.chunkCoords.front() == glm::ivec3(-1, 0, 0),
            "VoxelSystem grid creation should expose the first chunk coord");

    const engine::voxel::WorldGenerationResult generation =
        engine::voxel::generateWorldState(grid, 11u);
    require(generation.waterLevel >= 0.0f &&
                generation.waterLevel <= static_cast<float>(Chunk::SY - 1),
            "VoxelSystem world generation should derive a chunk-bounded water level");

    engine::voxel::clearWorldGrid(grid);
    require(grid.chunks().empty(), "VoxelSystem clear should empty the grid chunks");
}

void testVoxelSystemSchedulesAndConsumesChunkMeshes()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(2, 1, 1), glm::ivec3(0, 0, 0));
    require(grid.chunks().size() == 2,
            "VoxelSystem meshing test should start with two chunks");

    for (Chunk& chunk : grid.chunks())
    {
        chunk.dirty.store(true);
        chunk.meshing.store(false);
        chunk.highPriority.store(false);
    }
    grid.chunks()[1].highPriority.store(true);

    JobSystem jobs;
    jobs.start(1);
    const size_t scheduled = engine::voxel::scheduleDirtyChunkMeshing(
        grid, jobs, glm::vec3(0.0f), 1);
    jobs.waitIdle();
    jobs.stop();

    require(scheduled == 1,
            "VoxelSystem should respect the per-frame mesh scheduling limit");
    require(!grid.chunks()[1].dirty.load() && !grid.chunks()[1].meshing.load(),
            "VoxelSystem should schedule the high-priority chunk first");
    require(grid.chunks()[0].dirty.load(),
            "VoxelSystem should leave unscheduled chunks dirty");

    std::vector<engine::voxel::PendingChunkMesh> pending =
        engine::voxel::consumePendingChunkMeshes(grid);
    require(pending.size() == 1,
            "VoxelSystem should expose one pending mesh after one scheduled job");
    require(pending[0].chunkIndex == 1,
            "VoxelSystem should preserve the pending mesh chunk index");
    require(grid.chunks()[1].lastAppliedBuildId == pending[0].mesh.buildId,
            "VoxelSystem should advance the applied build id when consuming meshes");
    require(!grid.chunks()[1].pending.has_value(),
            "VoxelSystem should clear consumed pending mesh state");

    pending = engine::voxel::consumePendingChunkMeshes(grid);
    require(pending.empty(),
            "VoxelSystem should not emit stale pending meshes twice");
}

void testVoxelSystemChunkGridReadQueries()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(2, 1, 1), glm::ivec3(-1, 0, 2));
    require(grid.setVoxelWorld(-Chunk::SX + 1, 1, Chunk::SZ * 2 + 1,
                               BLOCK_STONE),
            "VoxelSystem read query test should seed a raycast block");
    for (Chunk& chunk : grid.chunks())
    {
        chunk.dirty.store(false);
    }
    grid.chunks()[1].dirty.store(true);

    const engine::voxel::ChunkGridSummary summary =
        engine::voxel::summarizeChunkGrid(grid);
    require(summary.chunkCount == 2,
            "VoxelSystem chunk summary should report chunk count");
    require(summary.dirtyChunks == 1,
            "VoxelSystem chunk summary should count dirty chunks");
    require(engine::voxel::containsChunk(grid, glm::ivec3(-1, 0, 2)),
            "VoxelSystem chunk containment should find owned chunks");
    require(!engine::voxel::containsChunk(grid, glm::ivec3(1, 0, 2)),
            "VoxelSystem chunk containment should reject out-of-bounds chunks");

    const std::vector<glm::ivec3> coords = engine::voxel::chunkCoords(grid);
    require(coords.size() == 2,
            "VoxelSystem chunk coords should mirror grid chunk count");
    require(std::find(coords.begin(), coords.end(), glm::ivec3(-1, 0, 2)) !=
                coords.end(),
            "VoxelSystem chunk coords should include the origin chunk");
    require(std::find(coords.begin(), coords.end(), glm::ivec3(0, 0, 2)) !=
                coords.end(),
            "VoxelSystem chunk coords should include adjacent chunks");

    const std::vector<engine::voxel::ChunkBounds> bounds =
        engine::voxel::chunkBounds(grid);
    require(bounds.size() == 2,
            "VoxelSystem chunk bounds should mirror grid chunk count");
    const auto originBounds =
        std::find_if(bounds.begin(), bounds.end(),
                     [](const engine::voxel::ChunkBounds& entry) {
                         return entry.coord == glm::ivec3(-1, 0, 2);
                     });
    require(originBounds != bounds.end(),
            "VoxelSystem chunk bounds should include chunk coordinates");
    require(originBounds->min ==
                glm::vec3(-static_cast<float>(Chunk::SX), 0.0f,
                          static_cast<float>(Chunk::SZ * 2)),
            "VoxelSystem chunk bounds should expose world-space min bounds");
    require(originBounds->max ==
                originBounds->min +
                    glm::vec3(static_cast<float>(Chunk::SX),
                              static_cast<float>(Chunk::SY),
                              static_cast<float>(Chunk::SZ)),
            "VoxelSystem chunk bounds should expose world-space max bounds");

    const RayHit hit = engine::voxel::raycastVoxels(
        grid, glm::vec3(-static_cast<float>(Chunk::SX) + 1.5f, 1.5f,
                        static_cast<float>(Chunk::SZ * 2) + 4.5f),
        glm::vec3(0.0f, 0.0f, -1.0f), 8.0f);
    require(hit.hit,
            "VoxelSystem voxel raycast query should report grid ray hits");
    require(hit.voxel == glm::ivec3(-Chunk::SX + 1, 1, Chunk::SZ * 2 + 1),
            "VoxelSystem voxel raycast query should expose the hit voxel");
}

void testVoxelSystemRaycastPlacementProbeModes()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(1, 1, 1), glm::ivec3(0, 0, 0));
    require(grid.setVoxelWorld(1, 1, 1, BLOCK_STONE),
            "VoxelSystem placement probe test should seed a solid block");

    const engine::voxel::PlacementProbeResult placement =
        engine::voxel::raycastPlacementProbe(
            grid, {glm::vec3(1.5f, 1.5f, 4.5f),
                   glm::vec3(0.0f, 0.0f, -1.0f), 8.0f, false});
    require(placement.rayHit && placement.hasPlacementCandidate,
            "VoxelSystem placement probe should report voxel ray hits");
    require(placement.hitVoxel == glm::ivec3(1, 1, 1),
            "VoxelSystem placement probe should expose the hit voxel");
    require(placement.hitNormal == glm::ivec3(0, 0, 1),
            "VoxelSystem placement probe should derive the hit normal");
    require(placement.placementVoxel == glm::ivec3(1, 1, 2),
            "VoxelSystem placement probe should place in the previous voxel");
    require(placement.snappedToGrid,
            "VoxelSystem placement probe should be grid-snapped");

    const engine::voxel::PlacementProbeResult removal =
        engine::voxel::raycastPlacementProbe(
            grid, {glm::vec3(1.5f, 1.5f, 4.5f),
                   glm::vec3(0.0f, 0.0f, -1.0f), 8.0f, true});
    require(removal.rayHit && removal.placementVoxel == glm::ivec3(1, 1, 1),
            "VoxelSystem removal probe should target the hit voxel");
}

void testVoxelSystemFallbackPlacementProbeModes()
{
    const engine::voxel::PlacementFallbackProbeConfig config{
        glm::vec3(2.2f, 3.0f, -1.2f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        64.0f,
        8.0f,
        1.0f,
        false,
    };

    const engine::voxel::PlacementProbeResult fallback =
        engine::voxel::fallbackPlacementProbe(config);
    require(!fallback.rayHit && fallback.hasPlacementCandidate,
            "VoxelSystem fallback probe should create a non-ray-hit candidate");
    require(fallback.placementVoxel == glm::ivec3(2, 1, -2),
            "VoxelSystem fallback probe should floor the plane intersection");
    require(nearlyEqual(fallback.distance, 2.0f),
            "VoxelSystem fallback probe should prefer an in-range plane distance");
    require(fallback.hitNormal == glm::ivec3(0),
            "VoxelSystem fallback probe should not synthesize a normal by default");

    engine::voxel::PlacementFallbackProbeConfig syntheticConfig = config;
    syntheticConfig.syntheticSurface = true;
    const engine::voxel::PlacementProbeResult synthetic =
        engine::voxel::fallbackPlacementProbe(syntheticConfig);
    require(synthetic.rayHit && synthetic.hitNormal == glm::ivec3(0, 1, 0),
            "VoxelSystem synthetic fallback should behave like an upward surface hit");
    require(synthetic.hitVoxel == glm::ivec3(2, 0, -2),
            "VoxelSystem synthetic fallback should expose the synthetic surface voxel");
}

void testVoxelSystemCommitBlockPlacementAndRemoval()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(2, 1, 1), glm::ivec3(0, 0, 0));

    for (Chunk& chunk : grid.chunks())
    {
        chunk.dirty.store(false);
        chunk.highPriority.store(false);
    }

    const std::vector<glm::ivec3> cells{
        glm::ivec3(Chunk::SX, 1, 1),
        glm::ivec3(Chunk::SX + 1, 1, 1),
    };
    require(engine::voxel::commitBlockPlacement(grid, cells, BLOCK_STONE),
            "VoxelSystem block placement commit should place empty cells");
    require(grid.getVoxelWorld(Chunk::SX, 1, 1) == BLOCK_STONE &&
                grid.getVoxelWorld(Chunk::SX + 1, 1, 1) == BLOCK_STONE,
            "VoxelSystem block placement commit should set all footprint cells");

    const Chunk* left = grid.getChunk(glm::ivec3(0, 0, 0));
    const Chunk* right = grid.getChunk(glm::ivec3(1, 0, 0));
    require(left != nullptr && right != nullptr,
            "VoxelSystem block placement test should have adjacent chunks");
    require(left->dirty.load() && left->highPriority.load() &&
                right->dirty.load() && right->highPriority.load(),
            "VoxelSystem block placement commit should mark affected neighbors dirty");

    require(!engine::voxel::commitBlockPlacement(grid, cells, BLOCK_DIRT),
            "VoxelSystem block placement commit should reject occupied cells");
    require(!engine::voxel::commitBlockPlacement(grid, cells, BLOCK_AIR),
            "VoxelSystem block placement commit should reject air block placement");

    for (Chunk& chunk : grid.chunks())
    {
        chunk.dirty.store(false);
        chunk.highPriority.store(false);
    }

    require(engine::voxel::commitBlockRemoval(grid, cells.front(), BLOCK_STONE),
            "VoxelSystem block removal commit should remove the expected block");
    require(grid.getVoxelWorld(Chunk::SX, 1, 1) == BLOCK_AIR,
            "VoxelSystem block removal commit should clear the target voxel");
    require(left->dirty.load() && left->highPriority.load() &&
                right->dirty.load() && right->highPriority.load(),
            "VoxelSystem block removal commit should mark affected neighbors dirty");
    require(!engine::voxel::commitBlockRemoval(grid, cells.front(), BLOCK_STONE),
            "VoxelSystem block removal commit should reject already-empty targets");
    require(!engine::voxel::commitBlockRemoval(grid, cells.back(), BLOCK_DIRT),
            "VoxelSystem block removal commit should reject unexpected block ids");
}

void testVoxelSystemExpandGridToIncludeChunkPreservesBlocks()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(1, 1, 1), glm::ivec3(0, 0, 0));
    require(grid.setVoxelWorld(1, 2, 3, BLOCK_DIRT),
            "VoxelSystem grid expansion test should seed a preserved block");

    const engine::voxel::GridExpansionResult expansion =
        engine::voxel::expandGridToIncludeChunk(grid, glm::ivec3(3, 0, -2), 1);
    require(expansion.expanded,
            "VoxelSystem grid expansion should expand for out-of-bounds chunks");
    require(grid.inBounds(glm::ivec3(3, 0, -2)),
            "VoxelSystem grid expansion should include the target chunk");
    require(grid.getVoxelWorld(1, 2, 3) == BLOCK_DIRT,
            "VoxelSystem grid expansion should preserve existing blocks");
    require(expansion.chunkCoords.size() == grid.chunks().size(),
            "VoxelSystem grid expansion should return render coords for every chunk");

    const engine::voxel::GridExpansionResult noExpansion =
        engine::voxel::expandGridToIncludeChunk(grid, glm::ivec3(3, 0, -2), 1);
    require(!noExpansion.expanded && noExpansion.chunkCoords.empty(),
            "VoxelSystem grid expansion should no-op for already-owned chunks");
}

void testWaterHeartbeatDriftsCareStateAndClampsInput()
{
    WaterState water{};
    water.temperatureC = 24.0f;
    water.oxygen = 0.82f;
    water.flow = 0.45f;
    water.cleanliness = 1.0f;

    WaterHeartbeatConfig config{};
    config.maxStepSeconds = 60.0f;
    config.cleanlinessDecayPerSecond = 0.01f;
    config.oxygenDecayPerSecond = 0.02f;
    config.oxygenFloor = 0.4f;

    require(updateWaterHeartbeat(water, 10.0f, config),
            "Water heartbeat should report changed care state for positive dt");
    require(nearlyEqual(water.cleanliness, 0.9f),
            "Water heartbeat should decay cleanliness by configured rate");
    require(nearlyEqual(water.oxygen, 0.62f),
            "Water heartbeat should decay oxygen by configured rate");
    require(nearlyEqual(water.temperatureC, 24.0f),
            "Water heartbeat should leave temperature stable for now");
    require(nearlyEqual(water.flow, 0.45f),
            "Water heartbeat should leave flow stable for now");

    require(updateWaterHeartbeat(water, 100.0f, config),
            "Water heartbeat should keep changing until it reaches floors");
    require(nearlyEqual(water.cleanliness, 0.3f),
            "Water heartbeat should cap large dt by max step seconds");
    require(nearlyEqual(water.oxygen, 0.4f),
            "Water heartbeat should respect the oxygen floor");

    water.temperatureC = 100.0f;
    water.oxygen = -1.0f;
    water.flow = 5.0f;
    water.cleanliness = -2.0f;
    require(updateWaterHeartbeat(water, 0.0f, config),
            "Water heartbeat should clamp invalid saved values even without elapsed time");
    require(nearlyEqual(water.temperatureC, 40.0f),
            "Water heartbeat should clamp temperature to the serializer range");
    require(nearlyEqual(water.oxygen, 0.0f),
            "Water heartbeat should clamp oxygen to the serializer range");
    require(nearlyEqual(water.flow, 1.0f),
            "Water heartbeat should clamp flow to the serializer range");
    require(nearlyEqual(water.cleanliness, 0.0f),
            "Water heartbeat should clamp cleanliness to the serializer range");
}

class EngineCoreFixedCareClock final : public engine::game::CareUtcClock
{
public:
    explicit EngineCoreFixedCareClock(engine::game::CareUtcTimePoint value)
        : value_(value)
    {
    }

    engine::game::CareUtcTimePoint now() const override { return value_; }

private:
    engine::game::CareUtcTimePoint value_;
};

static_assert(
    !std::is_constructible_v<engine::game::GameRuntime,
                             EngineCoreFixedCareClock&&>,
    "GameRuntime must reject temporary injected clocks that would dangle");

void testGameRuntimeOfflineCareUsesInjectedClockWithoutLiveBinding()
{
    const auto now =
        engine::game::parseCareUtcTimestamp("2026-08-13T12:00:00Z");
    require(now.has_value(), "Engine-core fixed care UTC should parse");
    const EngineCoreFixedCareClock clock{*now};
    engine::game::GameRuntime runtime{clock};
    GameState state{};
    state.lastPlayedUtc = "2026-08-13T11:00:00Z";
    CreatureInstance primary{};
    primary.uuid = "offline-runtime-primary";
    primary.primary = true;
    state.creatures.push_back(primary);

    const auto result = runtime.reconcileOfflineCare(state);
    require(result.appliedSeconds == 3600 && result.primaryCreatureFound &&
                result.creatureChanged &&
                runtime.primaryCreatureRuntimeId() == 0 &&
                state.creatures.front().needs.hunger > 0.0f,
            "Offline runtime reconciliation should use injected UTC and persistent primary state without a live id");
    state.lastPlayedUtc.clear();
    runtime.stampLastPlayedUtc(state);
    require(state.lastPlayedUtc == "2026-08-13T12:00:00Z",
            "Runtime save stamping should use the same injected UTC source");
}

void testGameRuntimeHeartbeatAndWaterMaintenance()
{
    engine::game::GameRuntime emptyRuntime;
    GameState emptyState{};
    require(emptyRuntime.feedPrimaryCreature(emptyState).status ==
                engine::game::PrimaryCreatureFeedStatus::NoPrimaryCreature,
            "Feeding should reject an empty persistent creature roster");

    engine::game::GameRuntime runtime;
    SceneConfig scene{};
    scene.gameState.water.temperatureC = 24.0f;
    scene.gameState.water.oxygen = 0.70f;
    scene.gameState.water.flow = 0.45f;
    scene.gameState.water.cleanliness = 0.50f;

    const std::array liveCreatures = {
        engine::game::LiveCreatureSlot{37,
                                       engine::game::kStarterFishSpeciesId},
    };
    const engine::game::PrimaryCreatureSyncResult bound =
        runtime.synchronizePrimaryCreature(scene.gameState, liveCreatures);
    require(bound.liveBound() && runtime.primaryCreatureRuntimeId() == 37 &&
                runtime.primaryCreatureUuid() == bound.creatureUuid,
            "Game runtime should retain the primary persistent-to-live binding");
    CreatureInstance cleanupCreature{};
    cleanupCreature.uuid = "heartbeat-cleanup-snail";
    cleanupCreature.speciesId =
        std::string(engine::game::kRamshornSnailSpeciesId);
    cleanupCreature.displayName = "Pebble";
    scene.gameState.creatures.push_back(cleanupCreature);
    CreatureInstance& primary = scene.gameState.creatures.front();
    primary.needs.hunger = 0.10f;
    const CreatureNeeds pausedNeeds = primary.needs;

    const engine::game::GameRuntime::HeartbeatResult inactive =
        runtime.updateGameStateHeartbeat(scene, 10.0f, false);
    require(!inactive.changed && !inactive.shouldLog,
            "Inactive game runtime heartbeat should not mutate care state");
    require(nearlyEqual(scene.gameState.water.cleanliness, 0.50f),
            "Inactive game runtime heartbeat should leave cleanliness unchanged");
    require(nearlyEqual(primary.needs.hunger, pausedNeeds.hunger) &&
                nearlyEqual(primary.needs.cleanliness,
                            pausedNeeds.cleanliness) &&
                nearlyEqual(primary.needs.happiness, pausedNeeds.happiness) &&
                nearlyEqual(primary.needs.health, pausedNeeds.health),
            "Inactive game runtime heartbeat should freeze creature needs");

    const engine::game::GameRuntime::HeartbeatResult active =
        runtime.updateGameStateHeartbeat(scene, 15.0f, true);
    require(active.changed && active.waterChanged && active.creatureChanged &&
                active.primaryCreatureFound && active.cleanupCrewCount == 1 &&
                nearlyEqual(active.cleanlinessDecayReduction, 0.20f) &&
                active.shouldLog,
            "Active game runtime heartbeat should update bound care state and request logging");
    require(nearlyEqual(scene.gameState.water.cleanliness, 0.492f),
            "Active cleanup crew should reduce the five-second cleanliness decay by 20 percent");
    require(primary.needs.hunger > pausedNeeds.hunger,
            "Active game runtime heartbeat should increase primary hunger");

    engine::game::WaterMaintenanceConfig config{};
    config.cleanlinessThreshold = 0.95f;
    config.oxygenBaseline = 0.82f;
    config.feedbackSeconds = 1.0f;

    const engine::game::WaterMaintenanceResult maintained =
        runtime.maintainWater(scene.gameState.water, config);
    require(maintained.maintained,
            "Game runtime water maintenance should accept dirty or low-oxygen water");
    require(nearlyEqual(scene.gameState.water.cleanliness, 1.0f),
            "Game runtime water maintenance should restore cleanliness");
    require(nearlyEqual(scene.gameState.water.oxygen, 0.82f),
            "Game runtime water maintenance should restore oxygen to the baseline");

    engine::game::WaterHudTelemetry telemetry = runtime.waterHudTelemetry(scene);
    require(telemetry.maintenanceFeedbackActive &&
                telemetry.cleanupCrewCount == 1 &&
                nearlyEqual(telemetry.cleanlinessDecayReduction, 0.20f),
            "Game runtime telemetry should expose maintenance and cleanup effects");
    runtime.tickGameplayActionFeedback(1.25f);
    telemetry = runtime.waterHudTelemetry(scene);
    require(!telemetry.maintenanceFeedbackActive,
            "Game runtime should expire maintenance feedback after its timer elapses");

    primary.needs.hunger = 0.80f;
    engine::game::PrimaryCreatureFeedConfig feedConfig{};
    feedConfig.hungerReduction = 0.30f;
    feedConfig.minimumHunger = 0.01f;
    feedConfig.bondGain = 0.02f;
    feedConfig.cooldownSeconds = 1.25f;
    feedConfig.feedbackSeconds = 0.80f;
    const engine::game::PrimaryCreatureFeedResult fed =
        runtime.feedPrimaryCreature(scene.gameState, feedConfig);
    require(fed.fed() && nearlyEqual(fed.hungerBefore, 0.80f) &&
                nearlyEqual(fed.hungerAfter, 0.50f) &&
                nearlyEqual(primary.needs.hunger, 0.50f) &&
                nearlyEqual(fed.bondBefore, 0.0f) &&
                nearlyEqual(fed.bondAfter, 0.02f) &&
                nearlyEqual(primary.bond, 0.02f),
            "Game runtime feeding should update hunger and bounded bond on the primary");

    engine::game::PrimaryCreatureCareTelemetry careTelemetry =
        runtime.primaryCreatureCareTelemetry(scene.gameState, feedConfig);
    require(careTelemetry.hasPrimary && careTelemetry.liveBound &&
                careTelemetry.displayName == primary.displayName &&
                nearlyEqual(careTelemetry.hunger, 0.50f) &&
                nearlyEqual(careTelemetry.bond, 0.02f) &&
                nearlyEqual(careTelemetry.vitality, primary.vitality) &&
                careTelemetry.feedFeedbackActive &&
                careTelemetry.feedAvailability ==
                    engine::game::PrimaryCreatureFeedAvailability::CooldownActive,
            "Care telemetry should expose read-only primary needs and feed feedback");

    const engine::game::CreatureVitalityPresentation lively =
        runtime.primaryCreaturePresentation(scene.gameState);
    require(lively.pathWobbleScale > 1.0f && lively.bodyMotionScale > 1.0f &&
                lively.tailMotionScale > 1.0f,
            "The bound primary should receive the derived vitality motion response");

    const engine::game::PrimaryCreatureFeedResult spammed =
        runtime.feedPrimaryCreature(scene.gameState, feedConfig);
    require(spammed.status ==
                engine::game::PrimaryCreatureFeedStatus::CooldownActive &&
                nearlyEqual(primary.needs.hunger, 0.50f),
            "The feed cooldown should reject rapid repeated commands");
    runtime.tickGameplayActionFeedback(0.90f);
    careTelemetry = runtime.primaryCreatureCareTelemetry(scene.gameState, feedConfig);
    require(!careTelemetry.feedFeedbackActive &&
                careTelemetry.feedAvailability ==
                    engine::game::PrimaryCreatureFeedAvailability::CooldownActive,
            "Feed success feedback should expire before the anti-spam cooldown");
    runtime.tickGameplayActionFeedback(0.40f);
    careTelemetry = runtime.primaryCreatureCareTelemetry(scene.gameState, feedConfig);
    require(careTelemetry.feedAvailability ==
                engine::game::PrimaryCreatureFeedAvailability::Available,
            "The feed command should become available when its cooldown expires");

    const std::array<engine::game::LiveCreatureSlot, 0> noLiveCreatures{};
    const engine::game::PrimaryCreatureSyncResult unbound =
        runtime.synchronizePrimaryCreature(scene.gameState, noLiveCreatures);
    require(!unbound.liveBound() && runtime.primaryCreatureRuntimeId() == 0 &&
                runtime.primaryCreatureUuid() == bound.creatureUuid,
            "Removing live creatures should clear only the disposable runtime binding");
    const float hungerBeforeRejectedFeed = primary.needs.hunger;
    const engine::game::PrimaryCreatureFeedResult rejectedFeed =
        runtime.feedPrimaryCreature(scene.gameState, feedConfig);
    require(rejectedFeed.status ==
                engine::game::PrimaryCreatureFeedStatus::NoLivePrimaryCreature &&
                nearlyEqual(primary.needs.hunger, hungerBeforeRejectedFeed),
            "Feeding must reject a saved primary with no current live binding");
    const CreatureNeeds unboundNeeds = primary.needs;
    const engine::game::GameRuntime::HeartbeatResult withoutLivePrimary =
        runtime.updateGameStateHeartbeat(scene, 5.0f, true);
    require(withoutLivePrimary.waterChanged &&
                !withoutLivePrimary.creatureChanged &&
                !withoutLivePrimary.primaryCreatureFound &&
                withoutLivePrimary.cleanupCrewCount == 1 &&
                nearlyEqual(withoutLivePrimary.cleanlinessDecayReduction,
                            0.20f) &&
                nearlyEqual(primary.needs.hunger, unboundNeeds.hunger),
            "Cleanup remains persistent-roster driven while an unbound primary must not receive session-time decay");
}

void testGameRuntimePlaceableEditRoundTrip()
{
    engine::game::GameRuntime runtime;
    SceneConfig scene{};
    scene.loadAquariumTest = true;
    scene.useDefaultPlaceables = false;

    PlaceableInstance instance{};
    instance.uuid = "test-placeable-1";
    instance.prototypeSlug = "eelgrass";
    instance.prototypeVersion = 1;
    instance.position = glm::vec3(1.0f, 2.0f, 3.0f);
    instance.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    instance.scale = glm::vec3(1.0f);
    instance.seed = 42u;

    PlaceableEditCommand add{};
    add.op = PlaceableEditCommand::Op::Add;
    add.after = instance;

    const engine::game::GameRuntime::PlaceableApplyResult addResult =
        runtime.applyPlaceableEditCommand(scene, add, false);
    require(addResult.accepted && addResult.effects.sceneRebuildRequired,
            "Game runtime should apply placeable add commands and request a rebuild");
    require(scene.placeables.size() == 1 && scene.placeables[0].uuid == instance.uuid,
            "Game runtime should upsert added placeables into scene config");
    require(runtime.completeExternalPlaceableEdit(add, "Placed eelgrass.", scene),
            "A projected external placeable edit should commit its journal entry");
    require(runtime.hasPlaceableUndo(),
            "Game runtime should own the placeable undo stack after a completed edit");
    require(runtime.placeableSceneDirty(),
            "Game runtime should track unsaved placeable edits");

    const engine::game::GameRuntime::PendingPlaceableEdit undo =
        runtime.beginUndoLastPlaceableEdit();
    require(undo.valid && undo.undo,
            "Game runtime should produce a reverse command for the last placeable edit");
    PlaceableEditCommand mismatchedUndo = undo.command;
    mismatchedUndo.after->uuid = "different-history-entry";
    const engine::game::GameRuntime::PlaceableApplyResult mismatchedUndoResult =
        runtime.applyPlaceableEditCommand(scene, mismatchedUndo, true);
    require(!mismatchedUndoResult.accepted && scene.placeables.size() == 1 &&
                runtime.hasPlaceableUndo(),
            "Game runtime should reject a same-op command that does not match history");
    const engine::game::GameRuntime::PlaceableApplyResult undoResult =
        runtime.applyPlaceableEditCommand(scene, undo.command, undo.undo);
    require(undoResult.accepted,
            "Game runtime should apply placeable undo commands");
    require(runtime.completePlaceableEdit(undo, scene),
            "A projected undo should commit its journal cursor change");
    require(scene.placeables.empty(),
            "Game runtime should remove the added placeable when undoing");
    require(!runtime.hasPlaceableUndo(),
            "Game runtime should pop the undo command after a completed undo");
    require(runtime.placeableEditStatus() == "Undid last placeable edit.",
            "Game runtime should own placeable edit status text");

    const engine::game::GameRuntime::PendingPlaceableEdit redo =
        runtime.beginRedoLastPlaceableEdit();
    require(redo.valid && redo.redo,
            "Game runtime should expose the reversible redo command");
    const engine::game::GameRuntime::PlaceableApplyResult redoResult =
        runtime.applyPlaceableEditCommand(scene, redo.command, redo.undo, redo.redo);
    require(redoResult.accepted,
            "Game runtime should apply placeable redo commands");
    require(runtime.completePlaceableEdit(redo, scene),
            "A projected redo should commit its journal cursor change");
    require(scene.placeables.size() == 1 &&
                scene.placeables[0].uuid == instance.uuid &&
                runtime.hasPlaceableUndo() && !runtime.hasPlaceableRedo(),
            "Redo should restore the exact placeable and advance history");

    runtime.markPlaceableSceneSaved();
    PlaceableInstance rejectedInstance = instance;
    rejectedInstance.uuid = "test-placeable-rejected-finalize";
    rejectedInstance.position.x += 4.0f;
    PlaceableEditCommand rejectedAdd{};
    rejectedAdd.op = PlaceableEditCommand::Op::Add;
    rejectedAdd.after = rejectedInstance;
    const engine::game::GameRuntime::PlaceableApplyResult rejectedAddResult =
        runtime.applyPlaceableEditCommand(scene, rejectedAdd, false);
    require(rejectedAddResult.accepted && scene.placeables.size() == 2,
            "Finalization rollback coverage should begin after a projected add");

    PlaceableEditCommand mismatchedCompletion = rejectedAdd;
    mismatchedCompletion.after->position.z += 2.0f;
    require(!runtime.completeExternalPlaceableEdit(
                mismatchedCompletion, "Should not commit.", scene) &&
                scene.placeables.size() == 1 &&
                scene.placeables[0].uuid == instance.uuid &&
                runtime.hasPlaceableUndo() && !runtime.hasPlaceableRedo() &&
                !runtime.placeableSceneDirty(),
            "A mismatched finalization should restore the applied collection without advancing history or dirty state");

    const engine::game::GameRuntime::PlaceableApplyResult retryResult =
        runtime.applyPlaceableEditCommand(scene, rejectedAdd, false);
    require(retryResult.accepted &&
                runtime.completeExternalPlaceableEdit(
                    rejectedAdd, "Placed retry.", scene) &&
                scene.placeables.size() == 2 && runtime.placeableSceneDirty(),
            "A rejected finalization should release its ticket so the edit can be retried");
}

void testGameRuntimePlaceableEditSessionIsolation()
{
    engine::game::GameRuntime runtime;
    SceneConfig authored{};
    authored.loadAquariumTest = true;
    authored.useDefaultPlaceables = false;

    const auto makeInstance = [](const char* uuid, float x, uint32_t seed) {
        PlaceableInstance instance{};
        instance.uuid = uuid;
        instance.prototypeSlug = "eelgrass";
        instance.prototypeVersion = 1;
        instance.position = glm::vec3(x, 2.0f, 3.0f);
        instance.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        instance.scale = glm::vec3(1.0f);
        instance.seed = seed;
        return instance;
    };
    const auto commitAdd = [&](SceneConfig& scene, const PlaceableInstance& instance) {
        PlaceableEditCommand add{};
        add.op = PlaceableEditCommand::Op::Add;
        add.after = instance;
        const engine::game::GameRuntime::PlaceableApplyResult applied =
            runtime.applyPlaceableEditCommand(scene, add, false);
        return applied.accepted &&
               runtime.completeExternalPlaceableEdit(add, "Placed isolated test.",
                                                     scene);
    };

    require(commitAdd(authored, makeInstance("authored-a", 1.0f, 10u)),
            "Authored isolation fixture should commit its savepoint edit");
    runtime.markPlaceableSceneSaved();
    require(commitAdd(authored, makeInstance("authored-b", 2.0f, 20u)),
            "Authored isolation fixture should commit its post-save edit");
    const engine::game::GameRuntime::PendingPlaceableEdit authoredUndo =
        runtime.beginUndoLastPlaceableEdit();
    require(authoredUndo.valid &&
                runtime.applyPlaceableEditCommand(authored, authoredUndo.command,
                                                  true).accepted &&
                runtime.completePlaceableEdit(authoredUndo, authored),
            "Authored isolation fixture should return to its saved cursor");
    const std::string authoredStatus = runtime.placeableEditStatus();
    require(runtime.hasPlaceableUndo() && runtime.hasPlaceableRedo() &&
                !runtime.placeableSceneDirty() && authored.placeables.size() == 1,
            "Authored history should begin preview with undo, redo, and savepoint state");

    require(runtime.beginIsolatedPlaceableEditSession() &&
                !runtime.beginIsolatedPlaceableEditSession() &&
                !runtime.hasPlaceableUndo() && !runtime.hasPlaceableRedo() &&
                !runtime.placeableSceneDirty() &&
                runtime.placeableEditStatus().empty(),
            "Preview should begin with one fresh, isolated placeable edit session");
    SceneConfig play = authored;
    require(commitAdd(play, makeInstance("play-only", 9.0f, 90u)) &&
                runtime.hasPlaceableUndo() && runtime.placeableSceneDirty(),
            "Preview edits should advance only the disposable history");

    // reloadScene() resets the live journal before the editor restores its
    // suspended authored session.
    runtime.resetPlaceableEdits();
    require(!runtime.hasPlaceableUndo() && !runtime.hasPlaceableRedo() &&
                !runtime.placeableSceneDirty() &&
                runtime.endIsolatedPlaceableEditSession() &&
                !runtime.endIsolatedPlaceableEditSession(),
            "Resetting disposable preview edits must leave the suspended authored session restorable exactly once");
    require(runtime.hasPlaceableUndo() && runtime.hasPlaceableRedo() &&
                !runtime.placeableSceneDirty() &&
                runtime.placeableEditStatus() == authoredStatus,
            "Exiting preview should restore authored undo, redo, status, and savepoint state");

    const engine::game::GameRuntime::PendingPlaceableEdit authoredRedo =
        runtime.beginRedoLastPlaceableEdit();
    require(authoredRedo.valid &&
                runtime.applyPlaceableEditCommand(authored, authoredRedo.command,
                                                  authoredRedo.undo,
                                                  authoredRedo.redo).accepted &&
                runtime.completePlaceableEdit(authoredRedo, authored) &&
                authored.placeables.size() == 2 && runtime.hasPlaceableUndo() &&
                !runtime.hasPlaceableRedo() && runtime.placeableSceneDirty(),
            "Restored authored redo snapshots should remain executable and dirty relative to the original savepoint");
}

void testFishCelebrationLifecycle()
{
    engine::game::FishCelebration celebration;
    require(!celebration.active(), "celebration starts idle");
    require(celebration.bodyWiggle(1.0f, 0.0f) == 0.0f, "no wiggle while idle");

    celebration.trigger();
    require(celebration.active(), "trigger activates the celebration");

    celebration.update(0.1f);
    const float earlyBlend = celebration.blend();
    require(earlyBlend > 0.0f && earlyBlend < 1.0f, "blend ramps in over the turn window");
    require(celebration.pathTimeOffset() > 0.0f, "path clock freezes while active");
    require(celebration.bodyWiggle(1.0f, 0.0f) != 0.0f, "wiggle is non-zero while active");

    for (int i = 0; i < 20; ++i)
    {
        celebration.update(0.1f);
    }
    require(nearlyEqual(celebration.blend(), 1.0f), "blend holds at 1 mid-celebration");

    // camera directly along +z from the fish: camDir=(0,0,1) -> yaw=atan2(-1,0)=-pi/2.
    const engine::game::FishCelebration::Facing facing =
        celebration.faceCamera(0.0f, 0.0f, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 5.0f));
    require(nearlyEqual(facing.yaw, -1.5707963f),
            "fish faces the camera at full blend");

    for (int i = 0; i < 60; ++i)
    {
        celebration.update(0.1f);
    }
    require(!celebration.active(), "celebration expires after its duration");
    require(celebration.blend() == 0.0f, "no blend once idle");
    const float frozenOffset = celebration.pathTimeOffset();
    celebration.update(0.1f);
    require(nearlyEqual(celebration.pathTimeOffset(), frozenOffset),
            "path offset stops accumulating once idle");

    celebration.setEnabled(false);
    celebration.trigger();
    require(!celebration.active(), "a disabled celebration does not trigger");

    celebration.setEnabled(true);
    celebration.reset();
    celebration.trigger();
    celebration.update(10.0f);
    require(!celebration.active(), "a large hitch should not stretch celebration duration");
    require(nearlyEqual(celebration.pathTimeOffset(),
                        celebration.config().durationSeconds),
            "a large hitch only freezes the path for the configured duration");
}

void testAxolotlBellyFloatLifecycle()
{
    engine::game::AxolotlBellyFloat bellyFloat;
    require(!bellyFloat.enabled(), "belly float starts disabled by default");
    require(!bellyFloat.active(), "belly float starts upright");

    // Disabled: focus on the axolotl does nothing.
    bellyFloat.update(0.5f, true);
    require(!bellyFloat.active(), "a disabled belly float never rolls");
    require(bellyFloat.activeSeconds() > 0.0f &&
                !bellyFloat.typedVersusBanner().empty(),
            "focused axolotl typing banner advances even when belly roll is disabled");
    bellyFloat.update(0.1f, false);
    require(bellyFloat.activeSeconds() == 0.0f,
            "releasing axolotl focus resets the typing banner clock");

    bellyFloat.setEnabled(true);
    const float rollSeconds = bellyFloat.config().rollSeconds;
    bellyFloat.update(rollSeconds * 0.5f, true);
    require(bellyFloat.active(), "focusing the axolotl starts the roll");
    require(nearlyEqual(bellyFloat.activeSeconds(), rollSeconds * 0.5f),
            "the celebration clock accumulates while focused");
    require(bellyFloat.blend() > 0.0f && bellyFloat.blend() < 1.0f,
            "the roll eases in rather than snapping");
    require(bellyFloat.rollRadians() > 0.0f && bellyFloat.rollRadians() < 3.14159f,
            "mid-roll sits between upright and belly-up");
    const float midBlend = bellyFloat.blend();
    const float midActiveSeconds = bellyFloat.activeSeconds();
    bellyFloat.update(-3.0f, true);
    require(nearlyEqual(bellyFloat.blend(), midBlend) &&
                nearlyEqual(bellyFloat.activeSeconds(), midActiveSeconds),
            "negative frame deltas do not rewind or advance the belly float");

    bellyFloat.update(rollSeconds, true);
    require(nearlyEqual(bellyFloat.blend(), 1.0f), "the roll settles fully belly-up");
    require(nearlyEqual(bellyFloat.rollRadians(), 3.14159265f, 1e-4f),
            "belly-up is a half-turn of roll");

    // holding focus keeps it belly-up indefinitely (not a timed one-shot).
    bellyFloat.update(30.0f, true);
    require(nearlyEqual(bellyFloat.blend(), 1.0f), "focus holds the float indefinitely");

    // releasing focus rights the axolotl smoothly and resets the clock.
    bellyFloat.update(rollSeconds * 0.5f, false);
    require(bellyFloat.blend() > 0.0f && bellyFloat.blend() < 1.0f,
            "releasing focus eases the roll back");
    require(bellyFloat.activeSeconds() == 0.0f,
            "releasing focus resets the celebration clock");
    bellyFloat.update(rollSeconds, false);
    require(!bellyFloat.active(), "the axolotl rights itself after release");

    // disabling mid-float eases out instead of snapping.
    bellyFloat.update(rollSeconds, true);
    bellyFloat.setEnabled(false);
    require(bellyFloat.active(), "disabling mid-float keeps the pose for easing");
    bellyFloat.update(rollSeconds * 0.5f, true);
    require(bellyFloat.blend() < 1.0f, "a disabled float eases back upright");

    bellyFloat.reset();
    require(!bellyFloat.active(), "reset snaps upright");
}

void testAxolotlCreaturePolishFocusDetails()
{
    engine::game::AxolotlPolishPose pose{};
    pose.scale = 0.5f;
    pose.pathPhase = 0.25f;
    pose.wagPhase = 0.5f;

    std::vector<engine::game::AxolotlPolishObject> objects;
    engine::game::appendAxolotlCreaturePolishObjects(pose, 1.0f, objects);
    require(objects.size() == 12, "idle axolotl polish emits only frill flutter cubes");

    pose.focused = true;
    pose.focusSeconds = 0.8f;
    objects.clear();
    engine::game::appendAxolotlCreaturePolishObjects(pose, 1.0f, objects);
    size_t bubbles = 0;
    for (const engine::game::AxolotlPolishObject& object : objects)
    {
        if (object.material == engine::game::AxolotlPolishMaterial::Bubble)
        {
            ++bubbles;
        }
    }
    require(objects.size() == 18 && bubbles == 6,
            "focused axolotl polish adds a six-bubble mouth trail");

    const engine::game::AxolotlFocusPoseOffsets idlePose =
        engine::game::axolotlFocusPoseOffsets(1.0f, 0.25f, 0.5f, 0.0f);
    require(nearlyEqual(idlePose.rollRadians, 0.0f) &&
                nearlyEqual(idlePose.pitchRadians, 0.0f) &&
                nearlyEqual(idlePose.bodyYawRadians, 0.0f),
            "idle axolotl focus pose offsets are zero");

    const engine::game::AxolotlFocusPoseOffsets focusPose =
        engine::game::axolotlFocusPoseOffsets(1.0f, 0.25f, 0.5f, 0.8f);
    require(std::abs(focusPose.rollRadians) > 0.05f,
            "focused axolotl gets a visible head/body tilt");

    const float idleToe =
        engine::game::axolotlToeWiggleRadians(0.5f, 0.0f, 0.25f, 0.0f);
    const float focusedToe =
        engine::game::axolotlToeWiggleRadians(0.5f, 0.0f, 0.25f, 0.8f);
    require(std::abs(focusedToe) > std::abs(idleToe),
            "focused axolotl toe wiggle is stronger than idle swim wiggle");
}

void testFishTypesDefaults()
{
    engine::game::FishHandle handle{};
    require(handle.id == 0 && handle.index == -1 && !handle.isAxolotl,
            "Default fish handle should be the invalid sentinel");

    engine::game::FishFocusSettings focus{};
    require(nearlyEqual(focus.fishFocusOrbitDistance_, 12.3f) &&
                nearlyEqual(focus.fishFocusHeightOffset_, 2.5f) &&
                nearlyEqual(focus.fishFocusPositionDamping_, 4.8f) &&
                nearlyEqual(focus.fishFocusRotationDamping_, 6.3f),
            "Fish focus settings should preserve the QA-baked framing defaults");
    require(focus.fishShakeEnabled_ &&
                nearlyEqual(focus.fishShakePositionAmplitude_, 0.05f) &&
                nearlyEqual(focus.fishShakeFrequency_, 0.6f) &&
                nearlyEqual(focus.fishShakeSettleDecay_, 0.8f),
            "Fish focus settings should preserve the QA-baked shake defaults");

    focus.fishShakeEnabled_ = false;
    focus.fishFocusOrbitDistance_ = 8.0f;
    require(!focus.fishShakeEnabled_ && nearlyEqual(focus.fishFocusOrbitDistance_, 8.0f),
            "Fish focus settings should be directly mutable plain data");
}

void testGameRuntimeCelebrationExclusivity()
{
    engine::game::GameRuntime runtime;
    require(runtime.fishCelebration().enabled(),
            "Viva Mexico starts enabled by default");
    require(!runtime.axolotlBellyFloat().enabled(),
            "belly float starts disabled by default");

    runtime.setAxolotlBellyFloatEnabled(true);
    require(runtime.axolotlBellyFloat().enabled(), "belly float enables on request");
    require(!runtime.fishCelebration().enabled(),
            "enabling the belly float disables Viva Mexico");

    runtime.setFishCelebrationEnabled(true);
    require(runtime.fishCelebration().enabled(), "Viva Mexico re-enables on request");
    require(!runtime.axolotlBellyFloat().enabled(),
            "re-enabling Viva Mexico disables the belly float");

    runtime.setFishCelebrationEnabled(false);
    require(!runtime.fishCelebration().enabled() &&
                !runtime.axolotlBellyFloat().enabled(),
            "disabling a celebration does not enable the other");
}

void testAxolotlBodySculptGeometry()
{
    const engine::game::AxolotlPaletteBandIds bands{
        {1, 2, 3, 4}, {5, 6}, {7, 8}, {10, 11, 12, 13}, 9};
    const std::vector<uint8_t> data = engine::game::buildAxolotlBodyVoxelData(bands);
    const glm::ivec3 dims = engine::game::kAxolotlBodyVolumeDims;
    require(data.size() == static_cast<size_t>(dims.x) * dims.y * dims.z,
            "axolotl body data matches its volume dimensions");

    auto at = [&](int x, int y, int z) {
        return data[static_cast<size_t>(x) + static_cast<size_t>(y) * dims.x +
                    static_cast<size_t>(z) * dims.x * dims.y];
    };

    size_t filled = 0;
    size_t eyeCount = 0;
    size_t gillCount = 0;
    bool gillOnNegativeZ = false;
    bool gillOnPositiveZ = false;
    for (int x = 0; x < dims.x; ++x)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int z = 0; z < dims.z; ++z)
            {
                const uint8_t id = at(x, y, z);
                if (id == 0)
                {
                    continue;
                }
                ++filled;
                require(at(x, y, dims.z - 1 - z) == id,
                        "axolotl body sculpt is mirror-symmetric across Z");
                if (id == bands.eye)
                {
                    ++eyeCount;
                }
                if (id == bands.gill[0] || id == bands.gill[1])
                {
                    ++gillCount;
                    require(x >= 10, "gill fans attach behind the head");
                    gillOnNegativeZ = gillOnNegativeZ || z <= 1;
                    gillOnPositiveZ = gillOnPositiveZ || z >= dims.z - 2;
                }
            }
        }
    }
    require(filled > 200, "axolotl body sculpt has substantial volume");
    require(eyeCount == 8, "axolotl has two chunky 2x2 black eyes");
    require(gillCount == 20, "axolotl has ten gill voxels per side");
    require(gillOnNegativeZ && gillOnPositiveZ, "gill fans flare out on both sides");

    // the face is on the front of the head: each eye is an inset 2x2 patch on
    // the flat face, with centered nostrils and a dark smile.
    for (const int eyeZ0 : {2, 6})
    {
        for (int y = 4; y <= 5; ++y)
        {
            require(at(15, y, eyeZ0) == bands.eye &&
                        at(15, y, eyeZ0 + 1) == bands.eye,
                    "axolotl eye patches sit on the front face");
        }
    }
    require(at(15, 4, 4) == bands.detail[3] && at(15, 4, 5) == bands.detail[3],
            "axolotl face carries centered dark nostrils");
    for (int z = 2; z <= 7; ++z)
    {
        require(at(15, 3, z) == bands.detail[3],
                "axolotl face carries the dark-rose smile");
    }
    require(at(14, 3, 3) == bands.detail[3] && at(14, 3, 6) == bands.detail[3],
            "axolotl smile wraps slightly for oblique views");

    // cheek blush dots on both sides of the head.
    require(at(14, 4, 2) == bands.detail[1] && at(14, 4, 7) == bands.detail[1] &&
                at(13, 4, 2) == bands.detail[1] && at(13, 4, 7) == bands.detail[1],
            "axolotl has blush dots on both cheeks");

    require(at(3, 1, 3) != bands.detail[2] && at(10, 1, 6) != bands.detail[2],
            "axolotl body leaves feet to separate animated limb volumes");

    // dorsal fin ridge runs along the spine.
    size_t ridgeCount = 0;
    for (int x = 2; x <= 8; ++x)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            if (at(x, y, 4) == bands.fin[0] || at(x, y, 4) == bands.fin[1])
            {
                ++ridgeCount;
            }
        }
    }
    require(ridgeCount == 7, "dorsal fin ridge runs along the spine");

    // the detail band is actually used for freckles and the cream belly.
    size_t freckleCount = 0;
    size_t creamCount = 0;
    for (size_t i = 0; i < data.size(); ++i)
    {
        freckleCount += data[i] == bands.detail[0] ? 1 : 0;
        creamCount += data[i] == bands.detail[2] ? 1 : 0;
    }
    require(freckleCount >= 4, "back and head carry rosy freckles");
    require(creamCount > 8, "belly and feet use the cream shade");
}

void testAxolotlTailSculptGeometry()
{
    const engine::game::AxolotlPaletteBandIds bands{
        {1, 2, 3, 4}, {5, 6}, {7, 8}, {10, 11, 12, 13}, 9};
    const std::vector<uint8_t> data = engine::game::buildAxolotlTailVoxelData(bands);
    const glm::ivec3 dims = engine::game::kAxolotlTailVolumeDims;
    require(data.size() == static_cast<size_t>(dims.x) * dims.y * dims.z,
            "axolotl tail data matches its volume dimensions");

    auto at = [&](int x, int y, int z) {
        return data[static_cast<size_t>(x) + static_cast<size_t>(y) * dims.x +
                    static_cast<size_t>(z) * dims.x * dims.y];
    };

    size_t filled = 0;
    bool hasMembrane = false;
    bool hasCore = false;
    size_t rootColumn = 0;
    size_t tipColumn = 0;
    for (int x = 0; x < dims.x; ++x)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int z = 0; z < dims.z; ++z)
            {
                const uint8_t id = at(x, y, z);
                if (id == 0)
                {
                    continue;
                }
                ++filled;
                require(id != bands.eye, "tail has no eye voxels");
                require(at(x, y, dims.z - 1 - z) == id,
                        "axolotl tail sculpt is mirror-symmetric across Z");
                hasMembrane = hasMembrane || id == bands.fin[0] || id == bands.fin[1];
                hasCore = hasCore || id == bands.body[1] || id == bands.body[2];
                rootColumn += x == dims.x - 1 ? 1 : 0;
                tipColumn += x == 0 ? 1 : 0;
            }
        }
    }
    require(filled > 60, "axolotl tail sculpt has substantial volume");
    require(hasMembrane && hasCore, "tail blade has both muscle core and fin membrane");
    require(tipColumn > 0, "tail reaches its tip column");
    require(rootColumn > tipColumn, "tail blade is tallest at the body joint");
}

void testAxolotlLimbSculptGeometry()
{
    const engine::game::AxolotlPaletteBandIds bands{
        {1, 2, 3, 4}, {5, 6}, {7, 8}, {10, 11, 12, 13}, 9};
    const std::vector<uint8_t> data = engine::game::buildAxolotlLimbVoxelData(bands);
    const glm::ivec3 dims = engine::game::kAxolotlLimbVolumeDims;
    require(data.size() == static_cast<size_t>(dims.x) * dims.y * dims.z,
            "axolotl limb data matches its volume dimensions");

    auto at = [&](int x, int y, int z) {
        return data[static_cast<size_t>(x) + static_cast<size_t>(y) * dims.x +
                    static_cast<size_t>(z) * dims.x * dims.y];
    };

    size_t filled = 0;
    size_t creamCount = 0;
    for (int x = 0; x < dims.x; ++x)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int z = 0; z < dims.z; ++z)
            {
                const uint8_t id = at(x, y, z);
                if (id == 0)
                {
                    continue;
                }
                ++filled;
                require(id != bands.eye, "limbs have no eye voxels");
                require(id != bands.gill[0] && id != bands.gill[1],
                        "limbs have no gill voxels");
                require(at(x, y, dims.z - 1 - z) == id,
                        "axolotl limb sculpt is mirror-symmetric across Z");
                creamCount += id == bands.detail[2] ? 1 : 0;
            }
        }
    }

    require(filled == 13, "axolotl limb is a compact connected paddle volume");
    require(creamCount == 5, "axolotl limb has a splayed cream mitten foot");
    require(at(0, 4, 0) == bands.body[2] && at(0, 4, 1) == bands.body[2] &&
                at(0, 4, 2) == bands.body[2] && at(1, 4, 1) == bands.body[1],
            "axolotl limb has a body-colored attachment cap");
    require(at(1, 3, 0) == bands.body[1] && at(1, 3, 1) == bands.body[1] &&
                at(1, 3, 2) == bands.body[1] && at(1, 2, 1) == bands.body[1],
            "axolotl limb has a stubby pink upper section");
    require(at(1, 1, 1) == bands.detail[2] && at(2, 1, 1) == bands.detail[2] &&
                at(3, 1, 1) == bands.detail[2] && at(2, 1, 0) == bands.detail[2] &&
                at(2, 1, 2) == bands.detail[2],
            "axolotl limb has forward and side toes");
}

void testFoliageCatalogDefaultsAndColorVariants()
{
    const std::vector<PlaceableInstance>& defaults =
        FoliageCatalog::defaultAquariumHeroFoliageInstances();
    require(defaults.size() >= 24,
            "default aquarium foliage should include the expanded hero plant set");

    auto countBand = [](const std::vector<uint8_t>& data, uint8_t base, uint8_t count) {
        size_t total = 0;
        for (uint8_t id : data)
        {
            if (id >= base && id < base + count)
            {
                ++total;
            }
        }
        return total;
    };

    auto buildPrototype = [](std::string_view slug) {
        const FoliagePrototype* prototype = FoliageCatalog::findPrototype(slug, 1);
        require(prototype != nullptr, "foliage color prototype should be registered");
        require(prototype->placeable.buildVoxelVolume != nullptr,
                "foliage color prototype should build voxel data");
        return prototype->placeable.buildVoxelVolume(prototype->placeable.voxelDims, 9u);
    };

    const std::vector<uint8_t> red = buildPrototype("red_ludwigia");
    const std::vector<uint8_t> gold = buildPrototype("golden_crypt");
    const std::vector<uint8_t> teal = buildPrototype("teal_fanwort");

    require(countBand(red, AquariumMaterial::FoliageAccentBand::Base,
                      AquariumMaterial::FoliageAccentBand::Count) > 0,
            "red ludwigia should use the warm foliage accent band");
    require(countBand(gold, AquariumMaterial::GrassBand::Base,
                      AquariumMaterial::GrassBand::Count) > 0,
            "golden crypt should use the warmer grass foliage band");
    require(countBand(teal, AquariumMaterial::AlgaeBand::Base,
                      AquariumMaterial::AlgaeBand::Count) > 0,
            "teal fanwort should use the blue-green algae foliage band");
}

void testFoliageMotionHistoryContract()
{
    const engine::game::FoliageArchetype& meadow =
        engine::game::foliageArchetype(engine::game::FoliageForm::MeadowBlade);
    const engine::game::FoliageArchetype& reed =
        engine::game::foliageArchetype(engine::game::FoliageForm::Reed);
    require(nearlyEqual(meadow.halfWidthMeters, 0.035f) &&
                nearlyEqual(meadow.sway.maxTipDisplacementMeters, 0.035f) &&
                nearlyEqual(meadow.sway.angularSpeedRadiansPerSecond, 0.52f) &&
                nearlyEqual(meadow.sway.bendExponent, 1.85f) &&
                nearlyEqual(meadow.sway.rootRigidity, 0.28f) &&
                nearlyEqual(reed.halfWidthMeters, 0.045f) &&
                nearlyEqual(reed.sway.maxTipDisplacementMeters, 0.055f) &&
                nearlyEqual(reed.sway.angularSpeedRadiansPerSecond, 0.38f) &&
                nearlyEqual(reed.sway.bendExponent, 2.15f) &&
                nearlyEqual(reed.sway.rootRigidity, 0.22f),
            "Foliage archetypes should retain their authored shape and sway profiles");

    engine::render::FoliageMotionHistory history;
    history.beginFrame(2.0f, 1.0f);
    require(nearlyEqual(history.currentTimeSeconds(), 2.0f) &&
                nearlyEqual(history.previousTimeSeconds(), 2.0f) &&
                nearlyEqual(history.currentSwayStrength(), 1.0f) &&
                nearlyEqual(history.previousSwayStrength(), 1.0f),
            "The first foliage frame should collapse motion to its current deformation");
    history.completeFrame();

    history.beginFrame(2.016f, 1.0f);
    require(nearlyEqual(history.previousTimeSeconds(), 2.0f) &&
                nearlyEqual(history.previousSwayStrength(), 1.0f),
            "Foliage motion should pair the next frame with committed deformation state");
    history.completeFrame();

    history.beginFrame(10.0f, 0.0f);
    require(nearlyEqual(history.currentTimeSeconds(), 10.0f) &&
                nearlyEqual(history.previousTimeSeconds(), 2.016f) &&
                nearlyEqual(history.currentSwayStrength(), 0.0f) &&
                nearlyEqual(history.previousSwayStrength(), 1.0f),
            "Time jumps and sway changes should emit honest endpoint motion");

    history.reset();
    history.beginFrame(11.0f, 0.0f);
    require(nearlyEqual(history.currentTimeSeconds(),
                        history.previousTimeSeconds()) &&
                nearlyEqual(history.currentSwayStrength(),
                            history.previousSwayStrength()),
            "An explicit scene reset should safely collapse first-frame motion");
}

void testFoliageVoxelGeometryContract()
{
    using engine::game::FoliageBladeFlower;
    using engine::game::FoliageBladeGroundCoverOnly;
    using engine::game::FoliageBladeInstance;
    using engine::game::FoliageMorphology;
    using engine::game::FoliagePatchKind;
    using engine::render::FoliageGpuPrimitive;
    using engine::render::FoliageVoxelPrimitiveRole;

    require(std::string_view(engine::render::kFoliageVoxelTopologyName) ==
                    "foliage-meadow-volume-v10" &&
                engine::render::kFoliageVoxelVerticesPerPrimitive == 36u,
            "Foliage patch topology identity and closed-cube draw width should remain versioned");

    constexpr std::array<glm::vec3, 6> kExpectedNormals = {
        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f)};
    for (uint32_t face = 0; face < kExpectedNormals.size(); ++face)
    {
        for (uint32_t faceVertex = 0; faceVertex < 6u; ++faceVertex)
        {
            const engine::render::FoliageUnitCubeVertex vertex =
                engine::render::decodeFoliageUnitCubeVertex(face * 6u + faceVertex);
            require(std::isfinite(vertex.positionSign.x) &&
                        std::isfinite(vertex.positionSign.y) &&
                        std::isfinite(vertex.positionSign.z) &&
                        glm::abs(vertex.positionSign) == glm::vec3(1.0f) &&
                        vertex.normal == kExpectedNormals[face],
                    "Every procedural cube vertex should decode to a finite signed corner and exact face normal");
        }
        for (uint32_t triangle = 0; triangle < 2u; ++triangle)
        {
            const uint32_t first = face * 6u + triangle * 3u;
            const glm::vec3 a =
                engine::render::decodeFoliageUnitCubeVertex(first).positionSign;
            const glm::vec3 b =
                engine::render::decodeFoliageUnitCubeVertex(first + 1u).positionSign;
            const glm::vec3 c =
                engine::render::decodeFoliageUnitCubeVertex(first + 2u).positionSign;
            require(glm::cross(b - a, c - a) == 4.0f * kExpectedNormals[face],
                    "Every procedural cube triangle should retain exact outward winding");
        }
    }
    const engine::render::FoliageUnitCubeVertex invalidVertex =
        engine::render::decodeFoliageUnitCubeVertex(
            engine::render::kFoliageVoxelVerticesPerPrimitive);
    require(invalidVertex.positionSign == glm::vec3(0.0f) &&
                invalidVertex.normal == glm::vec3(0.0f),
            "Out-of-range foliage topology indices should fail closed");

    FoliageBladeInstance flower{};
    flower.stableId = engine::game::foliageStableId(2701u, 10, 12);
    flower.stablePatchId = flower.stableId;
    flower.rootWorld = glm::vec3(1.25f, 2.0f, -0.75f);
    flower.patchRootWorld = flower.rootWorld;
    flower.heightMeters = 0.6f;
    flower.cellSizeMeters = 0.1f;
    flower.patchRadiusMeters = 0.60f;
    flower.restLeanMeters = 0.1f;
    flower.yawRadians = 1.57079632679489661923f;
    flower.phaseRadians = 0.35f;
    flower.randomSeed = 0x12345678u;
    flower.patchSeed = flower.randomSeed;
    flower.stemMaterialId = NaturePondMaterial::Foliage::Base;
    flower.secondaryMaterialId = flower.stemMaterialId;
    flower.tipMaterialId = NaturePondMaterial::Flower::Base;
    flower.flags = FoliageBladeFlower;
    flower.patchKind = FoliagePatchKind::FlowerCluster;
    flower.morphology = engine::game::foliageMorphologyForPatch(
        flower.patchKind, flower.patchSeed);

    std::array<FoliageBladeInstance, 1> plants = {flower};
    const engine::game::FoliagePatchLayout layout =
        engine::game::buildFoliagePatchLayout(plants);
    const engine::render::FoliageVoxelGeometry geometry =
        engine::render::buildFoliageVoxelGeometry(plants);
    const engine::render::FoliageVoxelGeometry repeat =
        engine::render::buildFoliageVoxelGeometry(plants);
    const auto samePrimitive = [](const FoliageGpuPrimitive& lhs,
                                  const FoliageGpuPrimitive& rhs) {
        return lhs.centerMotionT == rhs.centerMotionT &&
               lhs.halfExtentPhase == rhs.halfExtentPhase &&
               lhs.materialSeedFlags == rhs.materialSeedFlags &&
               lhs.swayProfile == rhs.swayProfile;
    };
    require(layout.valid && layout.sourcePlantCount == 1u &&
                layout.sourcePlants.size() == 1u &&
                layout.sourcePlants.front().stableId == flower.stableId &&
                layout.patches.size() == 1u &&
                layout.patches.front().sourcePlantOffset == 0u &&
                layout.patches.front().sourcePlantCount == 1u &&
                layout.patches.front().kind == FoliagePatchKind::FlowerCluster &&
                geometry.semanticInstanceCount == 1u &&
                geometry.patchCount == 1u &&
                geometry.patchKindCounts[static_cast<size_t>(
                    FoliagePatchKind::FlowerCluster)] == 1u &&
                geometry.primitives.size() == repeat.primitives.size() &&
                geometry.primitives.size() > 12u,
            "An isolated flower should become one deterministic, multi-stem flower colony");
    for (size_t index = 0; index < geometry.primitives.size(); ++index)
    {
        require(samePrimitive(geometry.primitives[index], repeat.primitives[index]),
                "Foliage patch expansion should be byte-value deterministic");
    }

    const engine::game::FoliagePatchInstance& flowerPatch =
        layout.patches.front();
    const FoliageMorphology flowerMorphology =
        engine::game::foliageFlowerMorphology(flowerPatch.randomSeed);
    require((flowerMorphology == FoliageMorphology::DaisyFlower ||
             flowerMorphology == FoliageMorphology::SpikeFlower) &&
                geometry.morphologyPatchCounts[static_cast<size_t>(
                    flowerMorphology)] == 1u,
            "Flower patches should deterministically resolve one explicit visual species");
    require(engine::game::foliagePatchDisplacement(
                flowerPatch, 0.0f, 0.0f) == glm::vec3(0.0f) &&
                engine::game::foliagePatchDisplacement(
                    flowerPatch, 0.0f, 17.25f) == glm::vec3(0.0f),
            "The active patch sway reference should keep roots exactly anchored");
    require(engine::game::foliagePatchDisplacement(
                flowerPatch, 1.0f, 2.0f,
                engine::scene::defaultEnvironmentWindSettings(), 0.0f) ==
                engine::game::foliagePatchDisplacement(
                    flowerPatch, 1.0f, 19.0f,
                    engine::scene::defaultEnvironmentWindSettings(), 0.0f),
            "Disabled patch sway should produce identical deformation endpoints");
    for (float timeSeconds : {0.0f, 1.25f, 7.5f, 31.0f})
    {
        const glm::vec3 displacement =
            engine::game::foliagePatchDisplacement(
                flowerPatch, 1.0f, timeSeconds);
        require(glm::length(glm::vec2(displacement.x, displacement.z)) <=
                    engine::game::foliagePatchConservativeDisplacement(
                        flowerPatch) +
                        1e-5f,
                "Active patch sway should remain inside its CPU reference bound");
    }

    std::array<FoliageBladeInstance, 2> duplicateIdentityPlants = {
        flower, flower};
    duplicateIdentityPlants[1].rootWorld.x += flower.cellSizeMeters;
    const engine::game::FoliagePatchLayout duplicateLayout =
        engine::game::buildFoliagePatchLayout(duplicateIdentityPlants);
    const engine::render::FoliageVoxelGeometry duplicateGeometry =
        engine::render::buildFoliageVoxelGeometry(duplicateIdentityPlants);
    require(!duplicateLayout.valid && duplicateLayout.patches.empty() &&
                duplicateGeometry.semanticInstanceCount == 2u &&
                duplicateGeometry.primitives.empty(),
            "Duplicate semantic foliage identities should fail patch expansion closed");

    size_t flowerPrimitiveCount = 0u;
    size_t anchoredPrimitiveCount = 0u;
    std::set<std::tuple<int64_t, int64_t, int64_t>> occupiedCenters;
    for (const FoliageGpuPrimitive& primitive : geometry.primitives)
    {
        const uint32_t role = primitive.materialSeedFlags.w;
        const bool stem =
            role == static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Stem);
        const bool leaf =
            role == static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf);
        const bool blossom =
            role == static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Flower);
        const bool groundCover =
            role == static_cast<uint32_t>(
                        FoliageVoxelPrimitiveRole::GroundCover);
        const float maximumHalfExtent = groundCover
                                            ? flower.cellSizeMeters * 1.50f
                                            : flower.cellSizeMeters * 0.75f;
        flowerPrimitiveCount += blossom ? 1u : 0u;
        anchoredPrimitiveCount += primitive.centerMotionT.w == 0.0f ? 1u : 0u;
        require((stem || leaf || blossom || groundCover) &&
                    primitive.materialSeedFlags.y ==
                        static_cast<uint32_t>(flowerMorphology) &&
                    std::isfinite(primitive.centerMotionT.x) &&
                    std::isfinite(primitive.centerMotionT.y) &&
                    std::isfinite(primitive.centerMotionT.z) &&
                    primitive.centerMotionT.w >= 0.0f &&
                    primitive.centerMotionT.w <= 1.0f &&
                    glm::all(glm::greaterThan(
                        glm::vec3(primitive.halfExtentPhase), glm::vec3(0.0f))) &&
                    glm::all(glm::lessThanEqual(
                        glm::vec3(primitive.halfExtentPhase),
                        glm::vec3(maximumHalfExtent))) &&
                    primitive.halfExtentPhase.w == flower.phaseRadians &&
                    std::isfinite(primitive.swayProfile.x) &&
                    std::isfinite(primitive.swayProfile.y) &&
                    std::isfinite(primitive.swayProfile.z) &&
                    std::isfinite(primitive.swayProfile.w),
                "Patch primitives should retain finite geometry, role, archetype, and motion data");
        const bool validMaterial =
            blossom ? primitive.materialSeedFlags.x == flower.tipMaterialId
                     : primitive.materialSeedFlags.x == flower.stemMaterialId;
        require(validMaterial,
                "Flower colony roles should retain the authored plant and accent material families");
        const auto centerKey = std::make_tuple(
            static_cast<int64_t>(std::llround(primitive.centerMotionT.x * 10000.0f)),
            static_cast<int64_t>(std::llround(primitive.centerMotionT.y * 10000.0f)),
            static_cast<int64_t>(std::llround(primitive.centerMotionT.z * 10000.0f)));
        require(occupiedCenters.insert(centerKey).second,
                "Foliage patch expansion should coalesce duplicate primitive centers");
    }
    const size_t expectedCompactHeadSize =
        flowerMorphology == FoliageMorphology::DaisyFlower ? 5u : 3u;
    require(flowerPrimitiveCount == expectedCompactHeadSize &&
                anchoredPrimitiveCount >= 1u,
            "An isolated flower should retain one compact species-specific head and anchored support geometry");

    const auto carpetGeometryAtPitch = [&](float pitch) {
        FoliageBladeInstance carpet = flower;
        carpet.stableId = engine::game::foliageCarpetStableId(2701u, 10, 12);
        carpet.heightMeters = pitch;
        carpet.cellSizeMeters = pitch;
        carpet.restLeanMeters = 0.0f;
        carpet.flags = FoliageBladeGroundCoverOnly;
        const std::array<FoliageBladeInstance, 1> carpetPlants = {carpet};
        return engine::render::buildFoliageVoxelGeometry(carpetPlants);
    };
    const engine::render::FoliageVoxelGeometry referenceCarpet =
        carpetGeometryAtPitch(0.10f);
    const engine::render::FoliageVoxelGeometry balancedCarpet =
        carpetGeometryAtPitch(0.20f);
    const engine::render::FoliageVoxelGeometry coarseCarpet =
        carpetGeometryAtPitch(0.25f);
    const auto groundCoverOnlyRoles = [](const auto& carpetGeometry) {
        return carpetGeometry.patchCount == 1u &&
               std::all_of(
                   carpetGeometry.primitives.begin(),
                   carpetGeometry.primitives.end(),
                   [](const FoliageGpuPrimitive& primitive) {
                       const auto role = static_cast<FoliageVoxelPrimitiveRole>(
                           primitive.materialSeedFlags.w);
                       return role == FoliageVoxelPrimitiveRole::Stem ||
                              role ==
                                  FoliageVoxelPrimitiveRole::GroundCover;
                   });
    };
    require(groundCoverOnlyRoles(referenceCarpet) &&
                groundCoverOnlyRoles(balancedCarpet) &&
                groundCoverOnlyRoles(coarseCarpet) &&
                referenceCarpet.primitives.size() >=
                    balancedCarpet.primitives.size() &&
                balancedCarpet.primitives.size() >
                    coarseCarpet.primitives.size() &&
                coarseCarpet.primitives.size() >= 6u &&
                coarseCarpet.primitives.size() <= 8u,
            "Oriented carpet-only tufts should stay valid, skip species forms, and never add geometry at coarser detail tiers");
    const auto broadRootLeafCount = [](const auto& carpetGeometry) {
        return static_cast<size_t>(std::count_if(
            carpetGeometry.primitives.begin(),
            carpetGeometry.primitives.end(),
            [](const FoliageGpuPrimitive& primitive) {
                return primitive.materialSeedFlags.w ==
                           static_cast<uint32_t>(
                               FoliageVoxelPrimitiveRole::GroundCover) &&
                       primitive.halfExtentPhase.y <= 0.025f &&
                       std::max(primitive.halfExtentPhase.x,
                                primitive.halfExtentPhase.z) >= 0.055f;
            }));
    };
    require(broadRootLeafCount(referenceCarpet) >= 3u &&
                broadRootLeafCount(balancedCarpet) >= 3u &&
                broadRootLeafCount(coarseCarpet) >= 2u,
            "Every V9 carpet tier should retain an overlapping low leaf layer instead of collapsing to isolated skinny bars");

    const auto orientedSlabContract = [](const auto& carpetGeometry) {
        std::set<uint32_t> yawOctants;
        const bool allOriented = std::all_of(
            carpetGeometry.primitives.begin(),
            carpetGeometry.primitives.end(),
            [&yawOctants](const FoliageGpuPrimitive& primitive) {
                const uint32_t encodedSeed = primitive.materialSeedFlags.z;
                yawOctants.insert(
                    (encodedSeed & engine::render::kFoliageVoxelYawOctantMask) >>
                    engine::render::kFoliageVoxelYawOctantShift);
                return (encodedSeed &
                        engine::render::kFoliageVoxelOrientedSlabFlag) != 0u &&
                       primitive.halfExtentPhase.x >
                           primitive.halfExtentPhase.z;
            });
        return allOriented && yawOctants.size() >= 2u;
    };
    require(orientedSlabContract(referenceCarpet) &&
                orientedSlabContract(balancedCarpet) &&
                orientedSlabContract(coarseCarpet),
            "Every V9 meadow primitive should encode an anisotropic slab and each tuft should fan across multiple yaw directions");

    const auto geometrySignature = [](const engine::render::FoliageVoxelGeometry& value) {
        std::vector<std::tuple<int64_t, int64_t, int64_t, uint32_t>> result;
        result.reserve(value.primitives.size());
        for (const FoliageGpuPrimitive& primitive : value.primitives)
        {
            result.emplace_back(
                static_cast<int64_t>(std::llround(primitive.centerMotionT.x * 10000.0f)),
                static_cast<int64_t>(std::llround(primitive.centerMotionT.y * 10000.0f)),
                static_cast<int64_t>(std::llround(primitive.centerMotionT.z * 10000.0f)),
                primitive.materialSeedFlags.w);
        }
        return result;
    };
    const auto originalSignature = geometrySignature(geometry);
    bool seedVariesTopology = false;
    for (uint32_t seedOffset = 1u; seedOffset <= 32u && !seedVariesTopology;
         ++seedOffset)
    {
        plants[0].patchSeed = flower.patchSeed + seedOffset;
        plants[0].morphology = engine::game::foliageMorphologyForPatch(
            plants[0].patchKind, plants[0].patchSeed);
        seedVariesTopology =
            geometrySignature(engine::render::buildFoliageVoxelGeometry(plants)) !=
            originalSignature;
    }
    require(seedVariesTopology,
            "Foliage patch seeds should vary colony topology without changing the draw contract");

    using engine::render::FoliagePaletteResponse;
    const glm::vec3 sourceColor(0.22f, 0.62f, 0.16f);
    const auto raw = engine::render::makeFoliagePresentationSettings(
        FoliagePaletteResponse::Raw);
    const auto soft = engine::render::makeFoliagePresentationSettings(
        FoliagePaletteResponse::SoftValueV1);
    const auto botanical = engine::render::makeFoliagePresentationSettings(
        FoliagePaletteResponse::BotanicalDepthV2);
    const auto meadow = engine::render::makeFoliagePresentationSettings(
        FoliagePaletteResponse::MeadowVolumeV3);
    const auto pastel = engine::render::makeFoliagePresentationSettings(
        FoliagePaletteResponse::PastelLightV4);
    const glm::vec3 rawColor = engine::render::applyFoliagePaletteResponse(
        sourceColor, static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf), raw);
    const glm::vec3 softLeaf = engine::render::applyFoliagePaletteResponse(
        sourceColor, static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf), soft);
    const glm::vec3 softFlower = engine::render::applyFoliagePaletteResponse(
        sourceColor, static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Flower), soft);
    const glm::vec3 softGroundCover =
        engine::render::applyFoliagePaletteResponse(
            sourceColor,
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::GroundCover),
            soft);
    require(rawColor == sourceColor && softLeaf != sourceColor &&
                glm::all(glm::greaterThanEqual(softLeaf, glm::vec3(0.0f))) &&
                glm::all(glm::lessThanEqual(softLeaf, glm::vec3(1.0f))) &&
                glm::dot(softLeaf, glm::vec3(0.2126f, 0.7152f, 0.0722f)) <
                    glm::dot(sourceColor,
                             glm::vec3(0.2126f, 0.7152f, 0.0722f)) &&
                glm::dot(softGroundCover,
                         glm::vec3(0.2126f, 0.7152f, 0.0722f)) <
                    glm::dot(sourceColor,
                             glm::vec3(0.2126f, 0.7152f, 0.0722f)) &&
                glm::length(softFlower - sourceColor) <
                    glm::length(softLeaf - sourceColor) &&
                glm::length(softGroundCover - sourceColor) <
                    glm::length(softLeaf - sourceColor),
            "Soft foliage presentation should be bounded, deterministic, exact-off, separate foliage value from terrain, and retain flower and ground-cover color depth");
    const glm::vec3 sideFace(1.0f, 0.0f, 0.0f);
    const glm::vec3 botanicalBase =
        engine::render::applyFoliagePaletteResponse(
            sourceColor,
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf),
            botanical, NaturePondMaterial::Foliage::Base, 0.0f, sideFace);
    const glm::vec3 botanicalTip =
        engine::render::applyFoliagePaletteResponse(
            sourceColor,
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf),
            botanical, NaturePondMaterial::Foliage::Base, 1.0f, sideFace);
    const glm::vec3 botanicalVariant =
        engine::render::applyFoliagePaletteResponse(
            sourceColor,
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf),
            botanical, NaturePondMaterial::Foliage::Base + 1u, 0.0f,
            sideFace);
    const glm::vec3 meadowBase =
        engine::render::applyFoliagePaletteResponse(
            sourceColor,
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::GroundCover),
            meadow, NaturePondMaterial::Foliage::Base, 0.0f, sideFace);
    const glm::vec3 meadowTip =
        engine::render::applyFoliagePaletteResponse(
            sourceColor,
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf), meadow,
            NaturePondMaterial::Foliage::Base + 2u, 1.0f, sideFace);
    const glm::vec3 pastelBase =
        engine::render::applyFoliagePaletteResponse(
            sourceColor,
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::GroundCover),
            pastel, NaturePondMaterial::Foliage::Base, 0.0f, sideFace);
    const glm::vec3 pastelTip =
        engine::render::applyFoliagePaletteResponse(
            sourceColor,
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf), pastel,
            NaturePondMaterial::Foliage::Base + 2u, 1.0f, sideFace);
    const glm::vec3 pastelFineVariant =
        engine::render::applyFoliagePaletteResponse(
            sourceColor,
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf), pastel,
            NaturePondMaterial::Foliage::Base + 7u, 1.0f, sideFace);
    const glm::vec3 rawContextColor =
        engine::render::applyFoliagePaletteResponse(
            sourceColor,
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf), raw,
            NaturePondMaterial::Foliage::Base + 2u, 1.0f, sideFace);
    constexpr glm::vec3 kLuma(0.2126f, 0.7152f, 0.0722f);
    require(engine::render::parseFoliagePaletteResponse("raw") ==
                    FoliagePaletteResponse::Raw &&
                engine::render::parseFoliagePaletteResponse("soft-value-v1") ==
                    FoliagePaletteResponse::SoftValueV1 &&
                engine::render::parseFoliagePaletteResponse(
                    "botanical-depth-v2") ==
                    FoliagePaletteResponse::BotanicalDepthV2 &&
                engine::render::foliagePaletteResponseName(
                    FoliagePaletteResponse::BotanicalDepthV2) ==
                    "botanical-depth-v2" &&
                engine::render::parseFoliagePaletteResponse(
                    "meadow-volume-v3") ==
                    FoliagePaletteResponse::MeadowVolumeV3 &&
                engine::render::foliagePaletteResponseName(
                    FoliagePaletteResponse::MeadowVolumeV3) ==
                    "meadow-volume-v3" &&
                engine::render::parseFoliagePaletteResponse(
                    "pastel-light-v4") ==
                    FoliagePaletteResponse::PastelLightV4 &&
                engine::render::foliagePaletteResponseName(
                    FoliagePaletteResponse::PastelLightV4) ==
                    "pastel-light-v4" &&
                engine::render::parseFoliagePaletteResponse("") ==
                    FoliagePaletteResponse::PastelLightV4 &&
                rawContextColor == sourceColor &&
                glm::dot(botanicalBase, kLuma) < glm::dot(softLeaf, kLuma) &&
                glm::dot(botanicalTip, kLuma) >
                    glm::dot(botanicalBase, kLuma) &&
                botanicalVariant != botanicalBase &&
                engine::render::applyFoliageRoughnessResponse(
                    0.88f,
                    static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf),
                    raw) == 0.88f &&
                engine::render::applyFoliageRoughnessResponse(
                    0.88f,
                    static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf),
                    botanical) < 0.82f,
            "Botanical depth V2 should keep raw exact while adding clump hue, base-to-tip value hierarchy, and foliage-local roughness separation");
    require(glm::dot(meadowBase, kLuma) < glm::dot(meadowTip, kLuma) &&
                meadowTip.y > meadowTip.x && meadowTip.y > meadowTip.z &&
                meadowTip.y - meadowTip.z >
                    botanicalTip.y - botanicalTip.z &&
                meadow.meadowVolumeStrength == 1.0f &&
                botanical.meadowVolumeStrength == 0.0f,
            "Meadow volume V3 should preserve a dark underlayer while producing warmer green tips without changing the archived V2 response");
    require(glm::dot(pastelBase, kLuma) > glm::dot(meadowBase, kLuma) &&
                glm::dot(pastelTip, kLuma) > glm::dot(meadowTip, kLuma) &&
                pastel.luminanceContrast < meadow.luminanceContrast &&
                pastel.faceValueContrast < meadow.faceValueContrast &&
                pastel.sideNormalUpBias > meadow.sideNormalUpBias &&
                pastel.softMaterialStrength > 0.8f &&
                pastelFineVariant != pastelTip &&
                meadow.softMaterialStrength == 0.0f &&
                engine::render::softenFoliageShadowVisibility(0.0f, pastel) >
                    engine::render::softenFoliageShadowVisibility(0.0f, meadow) &&
                engine::render::evaluateFoliageWrappedDiffuse(-0.25f, pastel) >
                    engine::render::evaluateFoliageWrappedDiffuse(-0.25f, meadow),
            "Pastel light V4 should lift the meadow underlayer, preserve distinct fine palette families, compress face contrast, wrap back-facing diffuse light, and soften plant shadows while preserving V3 as a control");
    const glm::vec3 sideNormal = engine::render::softenFoliageFaceNormal(
        glm::vec3(1.0f, 0.0f, 0.0f),
        static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf), soft);
    require(engine::render::softenFoliageFaceNormal(
                glm::vec3(1.0f, 0.0f, 0.0f),
                static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf), raw) ==
                glm::vec3(1.0f, 0.0f, 0.0f) &&
                sideNormal.y > 0.0f && nearlyEqual(glm::length(sideNormal), 1.0f) &&
                engine::render::softenFoliageFaceNormal(
                    glm::vec3(0.0f, 1.0f, 0.0f),
                    static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf), soft) ==
                    glm::vec3(0.0f, 1.0f, 0.0f),
            "Soft normal response should lift only side faces and retain an exact raw path");
}

void testFoliageMorphologyContract()
{
    using engine::game::FoliageBladeFlower;
    using engine::game::FoliageBladeInstance;
    using engine::game::FoliageBladeAquatic;
    using engine::game::FoliageBladeReed;
    using engine::game::FoliageBladeTreeCanopy;
    using engine::game::FoliageMorphology;
    using engine::render::FoliageGpuPrimitive;
    using engine::render::FoliageVoxelGeometry;
    using engine::render::FoliageVoxelPrimitiveRole;

    constexpr std::array<FoliageMorphology, 7> kMorphologies = {
        FoliageMorphology::GrassTuft,
        FoliageMorphology::BranchingShrub,
        FoliageMorphology::DaisyFlower,
        FoliageMorphology::SpikeFlower,
        FoliageMorphology::Reed,
        FoliageMorphology::WaterLily,
        FoliageMorphology::TreeCanopySprig};
    constexpr std::array<std::string_view, 7> kNames = {
        "grass-tuft", "branching-shrub", "daisy-flower", "spike-flower",
        "reed", "water-lily", "tree-canopy-sprig"};
    constexpr std::array<engine::game::FoliageSwayProfile, 7> kSway = {
        engine::game::FoliageSwayProfile{0.026f, 0.46f, 1.90f, 0.30f},
        engine::game::FoliageSwayProfile{0.014f, 0.27f, 2.30f, 0.56f},
        engine::game::FoliageSwayProfile{0.022f, 0.43f, 2.00f, 0.38f},
        engine::game::FoliageSwayProfile{0.026f, 0.35f, 2.15f, 0.32f},
        engine::game::FoliageSwayProfile{0.038f, 0.34f, 2.20f, 0.24f},
        engine::game::FoliageSwayProfile{0.010f, 0.18f, 1.35f, 0.05f},
        engine::game::FoliageSwayProfile{0.026f, 1.15f, 1.20f, 0.05f}};
    constexpr std::array<std::pair<uint32_t, uint32_t>, 7> kBudgets = {
        std::pair{12u, 3072u}, std::pair{12u, 3072u},
        std::pair{10u, 3072u}, std::pair{10u, 3072u},
        std::pair{10u, 768u}, std::pair{3u, 512u},
        std::pair{3u, 1024u}};

    for (size_t index = 0; index < kMorphologies.size(); ++index)
    {
        const engine::game::FoliageMorphologyArchetype& archetype =
            engine::game::foliageMorphologyArchetype(kMorphologies[index]);
        require(archetype.morphology == kMorphologies[index] &&
                    engine::game::foliageMorphologyName(kMorphologies[index]) ==
                        kNames[index] &&
                    nearlyEqual(archetype.sway.maxTipDisplacementMeters,
                                kSway[index].maxTipDisplacementMeters) &&
                    nearlyEqual(archetype.sway.angularSpeedRadiansPerSecond,
                                kSway[index].angularSpeedRadiansPerSecond) &&
                    nearlyEqual(archetype.sway.bendExponent,
                                kSway[index].bendExponent) &&
                    nearlyEqual(archetype.sway.rootRigidity,
                                kSway[index].rootRigidity) &&
                    archetype.minimumPrimitivesPerPatch ==
                        kBudgets[index].first &&
                    archetype.maximumPrimitivesPerPatch ==
                        kBudgets[index].second,
                "Foliage morphology metadata should retain explicit motion and primitive budgets");
    }

    struct MorphologySample
    {
        engine::game::FoliagePatchInstance patch{};
        FoliageVoxelGeometry geometry{};
    };
    const auto findSample = [](FoliageMorphology requested) {
        for (uint32_t attempt = 0; attempt < 512u; ++attempt)
        {
            const size_t sourceCount =
                requested == FoliageMorphology::BranchingShrub ? 3u : 1u;
            std::vector<FoliageBladeInstance> plants(sourceCount);
            const uint32_t patchSeed =
                0x12345678u + attempt * 0x9e3779b9u;
            const engine::game::FoliagePatchKind patchKind =
                requested == FoliageMorphology::BranchingShrub
                    ? engine::game::FoliagePatchKind::Shrub
                : requested == FoliageMorphology::Reed
                    ? engine::game::FoliagePatchKind::ReedCluster
                : requested == FoliageMorphology::WaterLily
                    ? engine::game::FoliagePatchKind::WaterLilyCluster
                : requested == FoliageMorphology::TreeCanopySprig
                    ? engine::game::FoliagePatchKind::TreeCanopyCluster
                : requested == FoliageMorphology::DaisyFlower ||
                          requested == FoliageMorphology::SpikeFlower
                    ? engine::game::FoliagePatchKind::FlowerCluster
                    : engine::game::FoliagePatchKind::GrassTuft;
            const FoliageMorphology resolved =
                engine::game::foliageMorphologyForPatch(patchKind, patchSeed);
            for (size_t source = 0; source < sourceCount; ++source)
            {
                FoliageBladeInstance& plant = plants[source];
                plant.stableId = engine::game::foliageStableId(
                    0x45a1u, static_cast<int>(attempt * 8u + source), 37);
                plant.stablePatchId = engine::game::foliageStableId(
                    0x45a1u, static_cast<int>(attempt * 8u), 37);
                plant.rootWorld =
                    glm::vec3(12.25f + static_cast<float>(source) * 0.05f,
                              2.0f,
                              -8.25f + static_cast<float>(source) * 0.04f);
                plant.patchRootWorld = glm::vec3(12.25f, 2.0f, -8.25f);
                plant.heightMeters =
                    requested == FoliageMorphology::Reed
                        ? 1.05f
                    : requested == FoliageMorphology::TreeCanopySprig
                        ? 0.32f
                        : 0.68f;
                plant.cellSizeMeters = 0.10f;
                plant.patchRadiusMeters = 0.65f;
                plant.yawRadians = 0.35f;
                plant.phaseRadians = 0.42f;
                plant.randomSeed =
                    patchSeed +
                    static_cast<uint32_t>(source) * 0x85ebca6bu;
                plant.patchSeed = patchSeed;
                plant.stemMaterialId = NaturePondMaterial::Foliage::Base;
                plant.secondaryMaterialId = plant.stemMaterialId;
                plant.tipMaterialId = NaturePondMaterial::Flower::Base;
                plant.patchKind = patchKind;
                plant.morphology = resolved;
                if (requested == FoliageMorphology::DaisyFlower ||
                    requested == FoliageMorphology::SpikeFlower)
                {
                    plant.flags |= FoliageBladeFlower;
                }
                if (requested == FoliageMorphology::Reed)
                {
                    plant.flags |= FoliageBladeReed;
                }
                if (requested == FoliageMorphology::WaterLily)
                {
                    plant.flags |= FoliageBladeAquatic |
                                   FoliageBladeFlower;
                    plant.heightMeters = 0.10f;
                }
                if (requested == FoliageMorphology::TreeCanopySprig)
                {
                    plant.flags |= FoliageBladeTreeCanopy;
                    plant.tipMaterialId = 0u;
                }
            }

            const engine::game::FoliagePatchLayout layout =
                engine::game::buildFoliagePatchLayout(plants);
            const FoliageVoxelGeometry geometry =
                engine::render::buildFoliageVoxelGeometry(plants);
            if (layout.valid && layout.patches.size() == 1u &&
                geometry.patchCount == 1u &&
                geometry.morphologyPatchCounts[static_cast<size_t>(requested)] ==
                    1u)
            {
                return std::optional<MorphologySample>(
                    MorphologySample{layout.patches.front(), geometry});
            }
        }
        return std::optional<MorphologySample>{};
    };

    std::array<MorphologySample, 7> samples{};
    for (size_t morphologyIndex = 0;
         morphologyIndex < kMorphologies.size(); ++morphologyIndex)
    {
        const std::optional<MorphologySample> sample =
            findSample(kMorphologies[morphologyIndex]);
        require(sample.has_value(),
                std::string("Focused morphology fixture should resolve ") +
                    std::string(kNames[morphologyIndex]));
        samples[morphologyIndex] = *sample;

        const engine::game::FoliageMorphologyArchetype& archetype =
            engine::game::foliageMorphologyArchetype(
                kMorphologies[morphologyIndex]);
        const float resolvedCpuAmplitude =
            engine::game::foliagePatchResolvedMaxTipDisplacementMeters(
                sample->patch);
        const float conservativeCpuBound =
            engine::game::foliagePatchConservativeDisplacement(
                sample->patch);
        require(sample->geometry.primitives.size() >=
                    archetype.minimumPrimitivesPerPatch &&
                    sample->geometry.primitives.size() <=
                        archetype.maximumPrimitivesPerPatch &&
                    nearlyEqual(
                        resolvedCpuAmplitude,
                        engine::game::
                            foliageMorphologyResolvedMaxTipDisplacementMeters(
                                sample->patch.morphology,
                                sample->patch.randomSeed)) &&
                    nearlyEqual(conservativeCpuBound,
                                resolvedCpuAmplitude * 1.02f),
                "Focused morphology geometry should stay inside its authored primitive budget");

        size_t anchoredCount = 0u;
        for (const FoliageGpuPrimitive& primitive : sample->geometry.primitives)
        {
            anchoredCount += primitive.centerMotionT.w == 0.0f ? 1u : 0u;
            require(primitive.materialSeedFlags.y ==
                            static_cast<uint32_t>(kMorphologies[morphologyIndex]) &&
                        nearlyEqual(primitive.swayProfile.x,
                                    resolvedCpuAmplitude) &&
                        nearlyEqual(primitive.swayProfile.y,
                                    archetype.sway.angularSpeedRadiansPerSecond) &&
                        nearlyEqual(primitive.swayProfile.z,
                                    archetype.sway.bendExponent) &&
                        nearlyEqual(primitive.swayProfile.w,
                                    archetype.sway.rootRigidity),
                    "Every primitive should carry its morphology and resolved species motion");
        }
        require(anchoredCount > 0u,
                "Every foliage silhouette should retain exact root anchors");

        for (float swayStrength : {0.0f, 0.65f, 1.0f, 1.5f})
        {
            const float scaledCpuBound = conservativeCpuBound * swayStrength;
            for (float normalizedHeight : {0.0f, 0.25f, 0.5f, 1.0f})
            {
                for (float timeSeconds : {0.0f, 1.25f, 7.5f, 31.0f})
                {
                    const glm::vec3 displacement =
                        engine::game::foliagePatchDisplacement(
                            sample->patch, normalizedHeight, timeSeconds,
                            engine::scene::defaultEnvironmentWindSettings(),
                            swayStrength);
                    require(glm::length(glm::vec2(displacement.x,
                                                  displacement.z)) <=
                                scaledCpuBound + 1e-5f,
                            "CPU morphology sway should stay inside the exact bound uploaded to every GPU primitive");
                }
            }
        }
    }

    const auto roleCount = [](const MorphologySample& sample,
                              FoliageVoxelPrimitiveRole role) {
        return static_cast<size_t>(std::count_if(
            sample.geometry.primitives.begin(), sample.geometry.primitives.end(),
            [role](const FoliageGpuPrimitive& primitive) {
                return primitive.materialSeedFlags.w ==
                       static_cast<uint32_t>(role);
            }));
    };
    const auto horizontalSpan = [](const MorphologySample& sample) {
        glm::vec2 minimum(std::numeric_limits<float>::max());
        glm::vec2 maximum(std::numeric_limits<float>::lowest());
        for (const FoliageGpuPrimitive& primitive : sample.geometry.primitives)
        {
            const glm::vec2 center(primitive.centerMotionT.x,
                                   primitive.centerMotionT.z);
            minimum = glm::min(minimum, center);
            maximum = glm::max(maximum, center);
        }
        const glm::vec2 span = maximum - minimum;
        return std::max(span.x, span.y);
    };
    const auto verticalSpan = [](const MorphologySample& sample) {
        float minimum = std::numeric_limits<float>::max();
        float maximum = std::numeric_limits<float>::lowest();
        for (const FoliageGpuPrimitive& primitive : sample.geometry.primitives)
        {
            minimum = std::min(minimum, primitive.centerMotionT.y);
            maximum = std::max(maximum, primitive.centerMotionT.y);
        }
        return maximum - minimum;
    };

    const MorphologySample& grass = samples[0];
    require(roleCount(grass, FoliageVoxelPrimitiveRole::Stem) >= 4u &&
                roleCount(grass, FoliageVoxelPrimitiveRole::Leaf) >= 4u &&
                roleCount(grass,
                          FoliageVoxelPrimitiveRole::GroundCover) >= 4u,
            "Grass topology should read as layered short cover with a rooted multi-blade fan");

    const MorphologySample& shrub = samples[1];
    require(roleCount(shrub, FoliageVoxelPrimitiveRole::Stem) >= 6u &&
                roleCount(shrub, FoliageVoxelPrimitiveRole::Leaf) >= 18u &&
                roleCount(shrub,
                          FoliageVoxelPrimitiveRole::GroundCover) >= 6u &&
                horizontalSpan(shrub) > verticalSpan(shrub),
            "Shrub topology should remain low, broad, grounded, and mound-like");

    const MorphologySample& daisy = samples[2];
    std::map<int64_t, size_t> daisyFlowersByHeight;
    for (const FoliageGpuPrimitive& primitive : daisy.geometry.primitives)
    {
        if (primitive.materialSeedFlags.w ==
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Flower))
        {
            ++daisyFlowersByHeight[static_cast<int64_t>(std::llround(
                primitive.centerMotionT.y * 10000.0f))];
        }
    }
    const size_t maximumDaisyHeadWidth = std::max_element(
        daisyFlowersByHeight.begin(), daisyFlowersByHeight.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.second < rhs.second;
        })->second;
    require(roleCount(daisy, FoliageVoxelPrimitiveRole::Flower) >= 5u &&
                maximumDaisyHeadWidth >= 5u,
            "Daisy topology should contain a compact radial five-block flower head");

    const MorphologySample& spike = samples[3];
    std::set<int64_t> spikeFlowerHeights;
    float spikeFlowerMinimum = std::numeric_limits<float>::max();
    float spikeFlowerMaximum = std::numeric_limits<float>::lowest();
    for (const FoliageGpuPrimitive& primitive : spike.geometry.primitives)
    {
        if (primitive.materialSeedFlags.w ==
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Flower))
        {
            spikeFlowerHeights.insert(static_cast<int64_t>(std::llround(
                primitive.centerMotionT.y * 10000.0f)));
            spikeFlowerMinimum =
                std::min(spikeFlowerMinimum, primitive.centerMotionT.y);
            spikeFlowerMaximum =
                std::max(spikeFlowerMaximum, primitive.centerMotionT.y);
        }
    }
    require(roleCount(spike, FoliageVoxelPrimitiveRole::Flower) >= 3u &&
                spikeFlowerHeights.size() == 3u &&
                spikeFlowerMaximum - spikeFlowerMinimum >=
                    spike.patch.cellSizeMeters * 0.75f &&
                spikeFlowerMaximum - spikeFlowerMinimum <=
                    spike.patch.cellSizeMeters,
            "Spike-flower topology should carry a compact three-level floret accent instead of an oversized tower");

    const MorphologySample& reed = samples[4];
    const bool hasElongatedReedLeaf = std::any_of(
        reed.geometry.primitives.begin(), reed.geometry.primitives.end(),
        [&reed](const FoliageGpuPrimitive& primitive) {
            return std::max(primitive.halfExtentPhase.x,
                            primitive.halfExtentPhase.z) >=
                   reed.patch.cellSizeMeters * 0.40f;
        });
    require(verticalSpan(reed) >= reed.patch.cellSizeMeters * 4.5f &&
                roleCount(reed, FoliageVoxelPrimitiveRole::Stem) >= 4u &&
                roleCount(reed,
                          FoliageVoxelPrimitiveRole::GroundCover) >= 2u &&
                hasElongatedReedLeaf,
            "Reed topology should remain tall, thin, grounded, and recognizable by lateral blades");

    const MorphologySample& waterLily = samples[5];
    require(roleCount(waterLily,
                      FoliageVoxelPrimitiveRole::GroundCover) >= 3u &&
                roleCount(waterLily,
                          FoliageVoxelPrimitiveRole::Flower) >= 5u &&
                roleCount(waterLily, FoliageVoxelPrimitiveRole::Stem) == 0u &&
                horizontalSpan(waterLily) > verticalSpan(waterLily) * 2.0f,
            "Water-lily topology should read as a broad floating pad with a compact blossom rather than upright terrain foliage");

    const MorphologySample& treeCanopy = samples[6];
    const size_t anchoredTreeLeaves = static_cast<size_t>(std::count_if(
        treeCanopy.geometry.primitives.begin(),
        treeCanopy.geometry.primitives.end(),
        [](const FoliageGpuPrimitive& primitive) {
            return primitive.centerMotionT.w == 0.0f;
        }));
    const bool hasLeafShapedSlab = std::any_of(
        treeCanopy.geometry.primitives.begin(),
        treeCanopy.geometry.primitives.end(),
        [](const FoliageGpuPrimitive& primitive) {
            return std::max(primitive.halfExtentPhase.x,
                            primitive.halfExtentPhase.z) >
                   primitive.halfExtentPhase.y * 1.6f;
        });
    require(treeCanopy.geometry.primitives.size() == 3u &&
                roleCount(treeCanopy, FoliageVoxelPrimitiveRole::Leaf) == 3u &&
                anchoredTreeLeaves == 1u && hasLeafShapedSlab,
            "Tree-canopy topology should emit one rooted and two wind-responsive overlapping voxel leaves per sprig");
}

void testNaturePondGenerationContract()
{
    constexpr uint32_t kSeed = 2701u;
    const NaturePondBuild implicitReference = buildNaturePond(kSeed);
    const NaturePondBuild reference =
        buildNaturePond(kSeed, NaturePondDetailTier::Reference10Cm);

    require(reference.seed == kSeed && reference.volumes.size() == 6,
            "Nature pond should retain its seed and emit six policy-owned volumes");
    require(reference.detailVoxelPitch == 0.10f &&
                implicitReference.volumes[1].voxels == reference.volumes[1].voxels &&
                implicitReference.volumes[2].voxels == reference.volumes[2].voxels,
            "Implicit nature pond generation should retain the 0.10 m reference tier");
    require(reference.layout.fishHabitat.isValid(),
            "Nature pond fish habitat should have positive bounds and an interior center");
    require(reference.layout.fishHabitat.boundsMax.y < reference.layout.pondSurfaceHeight,
            "Nature pond fish habitat should remain below the water surface");
    require(reference.layout.pondBoundsMin.y < reference.layout.fishHabitat.boundsMin.y &&
                reference.layout.pondBoundsMax.y > reference.layout.fishHabitat.boundsMax.y,
            "Nature pond water bounds should contain the safe fish habitat");
    const auto& cameraAnchors = naturePondCameraAnchors();
    require(cameraAnchors[0].name == "meadow_overview" &&
                cameraAnchors[1].name == "pond_bank_waterline" &&
                cameraAnchors[2].name == "underwater_fish",
            "Nature pond should own the three named visual-QA camera anchors");
    require(cameraAnchors[2].position.y > reference.layout.fishHabitat.boundsMin.y &&
                cameraAnchors[2].position.y < reference.layout.pondSurfaceHeight,
            "Nature pond underwater camera should sit inside the pond water column");

    const auto& tiers = naturePondDetailTiers();
    require(tiers[0].sceneName == "nature_pond_probe" &&
                tiers[1].sceneName == "nature_pond_20cm_probe" &&
                tiers[2].sceneName == "nature_pond_25cm_probe",
            "Nature pond tier scene routing should be explicit and stable");
    require(naturePondDetailTierForSceneName("nature_pond_20cm_probe") ==
                NaturePondDetailTier::Balanced20Cm &&
                naturePondDetailTierForSceneName("nature_pond_25cm_bank_probe") ==
                    NaturePondDetailTier::Coarse25Cm &&
                naturePondDetailTierForSceneName(
                    NaturePondLightingParity::LitSceneName) ==
                    NaturePondDetailTier::Reference10Cm &&
                naturePondDetailTierForSceneName(
                    NaturePondAtmosphere::StarSceneName) ==
                    NaturePondDetailTier::Reference10Cm &&
                isNaturePondLightingParityProbeName(
                    NaturePondLightingParity::UnlitSceneName) &&
                !naturePondDetailTierForSceneName("nature_pond_unknown_probe").has_value(),
            "Nature pond tier routing should recognize only authored scene names");

    std::array<NaturePondBuild, 3> builds{
        buildNaturePond(kSeed, NaturePondDetailTier::Reference10Cm),
        buildNaturePond(kSeed, NaturePondDetailTier::Balanced20Cm),
        buildNaturePond(kSeed, NaturePondDetailTier::Coarse25Cm),
    };
    const std::array<glm::ivec3, 3> expectedDetailDims{
        glm::ivec3(240, 50, 200), glm::ivec3(120, 25, 100), glm::ivec3(96, 20, 80)};

    auto countBand = [](const std::vector<uint8_t>& voxels, uint8_t base, uint8_t count) {
        size_t result = 0;
        for (uint8_t voxel : voxels)
        {
            result += voxel >= base && voxel < static_cast<uint8_t>(base + count) ? 1u : 0u;
        }
        return result;
    };
    const auto sameFoliageInstance = [](const engine::game::FoliageBladeInstance& lhs,
                                        const engine::game::FoliageBladeInstance& rhs) {
        return lhs.stableId == rhs.stableId &&
               lhs.stablePatchId == rhs.stablePatchId &&
               lhs.rootWorld == rhs.rootWorld &&
               lhs.patchRootWorld == rhs.patchRootWorld &&
               lhs.heightMeters == rhs.heightMeters &&
               lhs.cellSizeMeters == rhs.cellSizeMeters &&
               lhs.patchRadiusMeters == rhs.patchRadiusMeters &&
               lhs.restLeanMeters == rhs.restLeanMeters &&
               lhs.yawRadians == rhs.yawRadians &&
               lhs.phaseRadians == rhs.phaseRadians &&
               lhs.randomSeed == rhs.randomSeed &&
               lhs.patchSeed == rhs.patchSeed &&
               lhs.stemMaterialId == rhs.stemMaterialId &&
               lhs.secondaryMaterialId == rhs.secondaryMaterialId &&
               lhs.tipMaterialId == rhs.tipMaterialId &&
               lhs.flags == rhs.flags && lhs.patchKind == rhs.patchKind &&
               lhs.morphology == rhs.morphology;
    };
    const auto sameFoliageInstances = [&sameFoliageInstance](
        const std::vector<engine::game::FoliageBladeInstance>& lhs,
        const std::vector<engine::game::FoliageBladeInstance>& rhs) {
        if (lhs.size() != rhs.size())
        {
            return false;
        }
        for (size_t index = 0; index < lhs.size(); ++index)
        {
            if (!sameFoliageInstance(lhs[index], rhs[index]))
            {
                return false;
            }
        }
        return true;
    };
    const auto sameFoliageSequence = [&](const NaturePondBuild& lhs,
                                         const NaturePondBuild& rhs) {
        return sameFoliageInstances(lhs.foliageInstances,
                                    rhs.foliageInstances);
    };
    const auto equivalentFoliageInstance = [](
        const engine::game::FoliageBladeInstance& lhs,
        const engine::game::FoliageBladeInstance& rhs) {
        return lhs.stableId == rhs.stableId &&
               lhs.stablePatchId == rhs.stablePatchId &&
               glm::all(glm::epsilonEqual(lhs.rootWorld, rhs.rootWorld,
                                          1e-5f)) &&
               glm::all(glm::epsilonEqual(lhs.patchRootWorld,
                                          rhs.patchRootWorld, 1e-5f)) &&
               nearlyEqual(lhs.heightMeters, rhs.heightMeters) &&
               nearlyEqual(lhs.cellSizeMeters, rhs.cellSizeMeters) &&
               nearlyEqual(lhs.patchRadiusMeters, rhs.patchRadiusMeters) &&
               nearlyEqual(lhs.restLeanMeters, rhs.restLeanMeters) &&
               nearlyEqual(lhs.yawRadians, rhs.yawRadians) &&
               nearlyEqual(lhs.phaseRadians, rhs.phaseRadians) &&
               lhs.randomSeed == rhs.randomSeed &&
               lhs.patchSeed == rhs.patchSeed &&
               lhs.stemMaterialId == rhs.stemMaterialId &&
               lhs.secondaryMaterialId == rhs.secondaryMaterialId &&
               lhs.tipMaterialId == rhs.tipMaterialId &&
               lhs.flags == rhs.flags && lhs.patchKind == rhs.patchKind &&
               lhs.morphology == rhs.morphology;
    };
    const auto candidateCoordinate = [](uint64_t stableId,
                                        uint32_t shift) {
        const uint32_t packed =
            static_cast<uint32_t>((stableId >> shift) & 0xffffu);
        return packed <= 0x7fffu ? static_cast<int>(packed)
                                 : static_cast<int>(packed) - 0x10000;
    };
    const auto carpetCandidateCoordinate = [](uint64_t stableId,
                                              uint32_t shift) {
        constexpr uint32_t kCoordinateMask = 0x7ffu;
        constexpr int kCoordinateBias = 512;
        const uint32_t packed = static_cast<uint32_t>(stableId);
        return static_cast<int>((packed >> shift) & kCoordinateMask) -
               kCoordinateBias;
    };

    for (size_t tierIndex = 0; tierIndex < builds.size(); ++tierIndex)
    {
        const NaturePondBuild& build = builds[tierIndex];
        const NaturePondBuild repeat = buildNaturePond(kSeed, build.detailTier);
        const NaturePondBuild alternate = buildNaturePond(kSeed + 1u, build.detailTier);
        require(build.volumes.size() == 6 &&
                    build.volumes[1].dims == expectedDetailDims[tierIndex] &&
                    build.volumes[2].dims == expectedDetailDims[tierIndex],
                "Each nature pond tier should use its expected packed-detail lattice");
        require(build.volumes[0].role == NaturePondVolumeRole::TerrainOpaque &&
                    build.volumes[1].role == NaturePondVolumeRole::OpaqueDetails &&
                    build.volumes[2].role == NaturePondVolumeRole::FoliageTranslucentMeadow &&
                    build.volumes[3].role == NaturePondVolumeRole::HeroTrunkOpaque &&
                    build.volumes[4].role ==
                        NaturePondVolumeRole::HeroCanopyTranslucent &&
                    build.volumes[5].role ==
                        NaturePondVolumeRole::HeroCanopyOcclusionProxy,
                "Nature pond volume ordering should preserve lighting ownership by layer");

        for (size_t volumeIndex = 0; volumeIndex < build.volumes.size(); ++volumeIndex)
        {
            const NaturePondVolumeData& volume = build.volumes[volumeIndex];
            const NaturePondVolumeData& repeatedVolume = repeat.volumes[volumeIndex];
            const size_t expectedSize = static_cast<size_t>(volume.dims.x) *
                                        static_cast<size_t>(volume.dims.y) *
                                        static_cast<size_t>(volume.dims.z);
            require(volume.voxels.size() == expectedSize,
                    "Nature pond volume data should match its declared dimensions");
            require(volume.name == repeatedVolume.name && volume.dims == repeatedVolume.dims &&
                        volume.position == repeatedVolume.position &&
                        volume.scale == repeatedVolume.scale &&
                        volume.role == repeatedVolume.role &&
                        volume.voxels == repeatedVolume.voxels,
                    "Every nature pond tier should be byte-deterministic for a fixed seed");
        }

        const glm::vec3 packedExtent = glm::vec3(build.volumes[1].dims) * build.volumes[1].scale;
        require(nearlyEqual(packedExtent.x, 24.0f) && nearlyEqual(packedExtent.y, 5.0f) &&
                    nearlyEqual(packedExtent.z, 20.0f),
                "All nature pond tiers should cover the same packed-detail world extent");
        const NaturePondVolumeData& trunk = build.volumes[3];
        const NaturePondVolumeData& canopy = build.volumes[4];
        const NaturePondVolumeData& proxy = build.volumes[5];
        const glm::vec3 trunkMax =
            trunk.position + glm::vec3(trunk.dims) * trunk.scale;
        const glm::vec3 trunkExtent = glm::vec3(trunk.dims) * trunk.scale;
        const glm::vec3 canopyMax =
            canopy.position + glm::vec3(canopy.dims) * canopy.scale;
        const glm::vec3 proxyMax =
            proxy.position + glm::vec3(proxy.dims) * proxy.scale;
        require(trunkMax.y >= canopy.position.y &&
                    glm::all(glm::greaterThanEqual(proxy.position, canopy.position)) &&
                    glm::all(glm::lessThanEqual(proxyMax, canopyMax)),
                "Hero trunk, canopy, and coarse proxy should form one contained tree");
        require(trunkExtent.x >= 4.4f && trunkExtent.z >= 3.6f,
                "The hero trunk volume should retain room for its root flare and crown forks");
        require(build.volumes[0].voxels == reference.volumes[0].voxels,
                "Nature pond tier comparison should keep the coarse terrain shell identical");
        require(build.volumes[5].position == reference.volumes[5].position &&
                    build.volumes[5].dims == reference.volumes[5].dims &&
                    build.volumes[5].voxels == reference.volumes[5].voxels,
                "The coarse canopy proxy should stay tier-invariant for stable AO mass");
        require(build.volumes[2].voxels != alternate.volumes[2].voxels,
                "Nature pond foliage should vary when the authored world seed changes");
        require(sameFoliageSequence(build, repeat),
                "Semantic foliage instances should be ordered and deterministic");
        require(!sameFoliageSequence(build, alternate),
                "Semantic foliage instances should vary with the authored world seed");
        require(build.heroCanopyFoliageInstances.size() == 63u &&
                    sameFoliageInstances(
                        build.heroCanopyFoliageInstances,
                        repeat.heroCanopyFoliageInstances) &&
                    !sameFoliageInstances(
                        build.heroCanopyFoliageInstances,
                        alternate.heroCanopyFoliageInstances),
                "Every tier should retain 63 deterministic, seed-varying hero-canopy sprigs");
        require(build.terrainOccupiedVoxels > 120000 &&
                    build.opaqueDetailOccupiedVoxels > 20 &&
                    build.foliageOccupiedVoxels > 500 &&
                    build.heroTrunkOccupiedVoxels > 20 &&
                    build.heroCanopyOccupiedVoxels > 100 &&
                    build.heroCanopyProxyOccupiedVoxels > 10,
                "Every nature pond tier should retain substantial authored content");
        require(countBand(build.volumes[1].voxels, 208, 6) > 0 &&
                    countBand(build.volumes[1].voxels, 218, 4) > 0 &&
                    countBand(build.volumes[2].voxels, 222, 8) > 0 &&
                    countBand(build.volumes[2].voxels, NaturePondMaterial::Flower::Base,
                              NaturePondMaterial::Flower::Count) > 0 &&
                    countBand(build.volumes[3].voxels, 218, 4) > 0 &&
                    countBand(build.volumes[4].voxels, 222, 8) > 0 &&
                    countBand(build.volumes[5].voxels, 222, 8) > 0,
                "Every nature pond tier should preserve policy-separated hero content");
    }

    require(builds[1].opaqueDetailOccupiedVoxels < builds[0].opaqueDetailOccupiedVoxels &&
                builds[2].opaqueDetailOccupiedVoxels < builds[0].opaqueDetailOccupiedVoxels &&
                builds[1].foliageOccupiedVoxels < builds[0].foliageOccupiedVoxels &&
                builds[2].foliageOccupiedVoxels < builds[0].foliageOccupiedVoxels,
            "Coarser pond tiers should reduce occupied packed-detail voxels");

    require(reference.terrainOccupiedVoxels == 167152u &&
                reference.opaqueDetailOccupiedVoxels == 3353u &&
                reference.foliageOccupiedVoxels == 8234u &&
                reference.heroTrunkOccupiedVoxels == 3435u &&
                reference.heroCanopyOccupiedVoxels == 40720u &&
                reference.heroCanopyProxyOccupiedVoxels == 108u,
            "Nature pond generated-volume counts should retain the measured floral-life/tree-sculpt baseline (foliage=" +
                std::to_string(reference.foliageOccupiedVoxels) +
                ", trunk=" +
                std::to_string(reference.heroTrunkOccupiedVoxels) +
                ", canopy=" +
                std::to_string(reference.heroCanopyOccupiedVoxels) + ")");

    require(reference.heroCanopyFoliageInstances.size() == 63u &&
                sameFoliageInstances(
                    reference.heroCanopyFoliageInstances,
                    implicitReference.heroCanopyFoliageInstances),
            "The reference hero canopy should retain its exact deterministic 7x9 sprig field");
    std::unordered_set<uint64_t> canopySprigIds;
    std::unordered_set<uint64_t> canopyPatchIds;
    const NaturePondVolumeData& referenceCanopy = reference.volumes[4];
    const glm::vec3 referenceCanopyMax =
        referenceCanopy.position +
        glm::vec3(referenceCanopy.dims) * referenceCanopy.scale;
    for (const engine::game::FoliageBladeInstance& sprig :
         reference.heroCanopyFoliageInstances)
    {
        const uint32_t packedId = static_cast<uint32_t>(sprig.stableId);
        const uint32_t lobeIndex = (packedId >> 16u) & 0xffu;
        const uint32_t sprigIndex = packedId & 0xffffu;
        bool attachedToOccupiedCanopy = false;
        const glm::ivec3 rootCell = glm::ivec3(glm::floor(
            (sprig.rootWorld - referenceCanopy.position) /
            referenceCanopy.scale));
        for (int z = rootCell.z - 2; z <= rootCell.z + 2; ++z)
        {
            for (int y = rootCell.y - 2; y <= rootCell.y + 2; ++y)
            {
                for (int x = rootCell.x - 2; x <= rootCell.x + 2; ++x)
                {
                    if (x < 0 || y < 0 || z < 0 ||
                        x >= referenceCanopy.dims.x ||
                        y >= referenceCanopy.dims.y ||
                        z >= referenceCanopy.dims.z)
                    {
                        continue;
                    }
                    const size_t index =
                        static_cast<size_t>(x) +
                        static_cast<size_t>(referenceCanopy.dims.x) *
                            (static_cast<size_t>(y) +
                             static_cast<size_t>(referenceCanopy.dims.y) *
                                 static_cast<size_t>(z));
                    if (referenceCanopy.voxels[index] == 0u)
                    {
                        continue;
                    }
                    const glm::vec3 occupiedCenter =
                        referenceCanopy.position +
                        (glm::vec3(x, y, z) + glm::vec3(0.5f)) *
                            referenceCanopy.scale;
                    attachedToOccupiedCanopy =
                        attachedToOccupiedCanopy ||
                        glm::distance(sprig.rootWorld, occupiedCenter) <=
                            glm::length(referenceCanopy.scale) * 1.05f;
                }
            }
        }
        require(canopySprigIds.insert(sprig.stableId).second &&
                    canopyPatchIds.insert(sprig.stablePatchId).second ==
                        (sprigIndex == 0u) &&
                    lobeIndex < 7u && sprigIndex < 9u &&
                    sprig.stableId ==
                        engine::game::foliageTreeCanopyStableId(
                            kSeed, lobeIndex, sprigIndex) &&
                    sprig.stablePatchId ==
                        engine::game::foliageTreeCanopyPatchStableId(
                            kSeed, lobeIndex) &&
                    sprig.flags ==
                        engine::game::FoliageBladeTreeCanopy &&
                    sprig.patchKind ==
                        engine::game::FoliagePatchKind::TreeCanopyCluster &&
                    sprig.morphology ==
                        engine::game::FoliageMorphology::TreeCanopySprig &&
                    sprig.cellSizeMeters == reference.detailVoxelPitch &&
                    sprig.heightMeters == 0.32f &&
                    sprig.tipMaterialId == 0u &&
                    sprig.stemMaterialId >=
                        NaturePondMaterial::Foliage::Base &&
                    sprig.stemMaterialId <
                        NaturePondMaterial::Foliage::Base +
                            NaturePondMaterial::Foliage::Count &&
                    (sprig.stemMaterialId -
                         NaturePondMaterial::Foliage::Base) % 4u <= 1u &&
                    attachedToOccupiedCanopy &&
                    glm::all(glm::greaterThanEqual(
                        sprig.rootWorld,
                        referenceCanopy.position - glm::vec3(0.30f))) &&
                    glm::all(glm::lessThanEqual(
                        sprig.rootWorld,
                        referenceCanopyMax + glm::vec3(0.30f))),
                "Each hero-canopy sprig should retain its lobe-owned identity, palette, and bounded surface root");
    }
    require(canopySprigIds.size() == 63u && canopyPatchIds.size() == 7u,
            "Hero-canopy sprigs should form seven coherent wind patches without identity collisions");

    const engine::render::FoliageVoxelGeometry canopySprigGeometry =
        engine::render::buildFoliageVoxelGeometry(
            reference.heroCanopyFoliageInstances);
    size_t anchoredCanopyLeaves = 0u;
    size_t movingCanopyLeaves = 0u;
    for (const engine::render::FoliageGpuPrimitive& primitive :
         canopySprigGeometry.primitives)
    {
        anchoredCanopyLeaves +=
            primitive.centerMotionT.w == 0.0f ? 1u : 0u;
        movingCanopyLeaves +=
            primitive.centerMotionT.w > 0.0f ? 1u : 0u;
        require(primitive.materialSeedFlags.y ==
                        static_cast<uint32_t>(
                            engine::game::FoliageMorphology::
                                TreeCanopySprig) &&
                    primitive.materialSeedFlags.w ==
                        static_cast<uint32_t>(
                            engine::render::FoliageVoxelPrimitiveRole::Leaf) &&
                    primitive.halfExtentPhase.y <= 0.035f &&
                    std::max(primitive.halfExtentPhase.x,
                             primitive.halfExtentPhase.z) >= 0.055f &&
                    std::max(primitive.halfExtentPhase.x,
                             primitive.halfExtentPhase.z) <= 0.073f &&
                    nearlyEqual(primitive.swayProfile.y, 1.15f) &&
                    nearlyEqual(primitive.swayProfile.z, 1.20f) &&
                    nearlyEqual(primitive.swayProfile.w, 0.05f),
                "Hero-canopy geometry should remain compact, voxel-like, and carry its restrained WIND-001 sway profile");
    }
    require(canopySprigGeometry.semanticInstanceCount == 63u &&
                canopySprigGeometry.patchCount == 7u &&
                canopySprigGeometry.patchKindCounts[static_cast<size_t>(
                    engine::game::FoliagePatchKind::TreeCanopyCluster)] == 7u &&
                canopySprigGeometry.morphologyPatchCounts[static_cast<size_t>(
                    engine::game::FoliageMorphology::TreeCanopySprig)] == 7u &&
                canopySprigGeometry.primitives.size() == 189u &&
                anchoredCanopyLeaves == 63u && movingCanopyLeaves == 126u,
            "TREE-002 should add exactly 189 leaf primitives with one anchor and two moving leaves per sprig (measured primitives/anchored/moving=" +
                std::to_string(canopySprigGeometry.primitives.size()) + "/" +
                std::to_string(anchoredCanopyLeaves) + "/" +
                std::to_string(movingCanopyLeaves) + ")");

    require(reference.foliageInstances.size() == 3079u,
            "The floral-life reference pond should retain its measured semantic foliage count (measured=" +
                std::to_string(reference.foliageInstances.size()) + ")");
    size_t accentInstanceCount = 0;
    size_t carpetInstanceCount = 0;
    size_t reedInstanceCount = 0;
    size_t flowerInstanceCount = 0;
    size_t reedFlowerInstanceCount = 0;
    size_t tippedAccentInstanceCount = 0;
    size_t aquaticInstanceCount = 0;
    size_t floweringAquaticInstanceCount = 0;
    std::array<bool, NaturePondMaterial::Flower::Count>
        flowerPaletteCoverage{};
    const NaturePondVolumeData& referenceFoliageVolume = reference.volumes[2];
    constexpr uint32_t kKnownFoliageFlags =
        engine::game::FoliageBladeReed | engine::game::FoliageBladeFlower |
        engine::game::FoliageBladeGroundCoverOnly |
        engine::game::FoliageBladeAquatic;
    std::unordered_set<uint64_t> referenceFoliageIds;
    referenceFoliageIds.reserve(reference.foliageInstances.size());
    for (const engine::game::FoliageBladeInstance& instance :
         reference.foliageInstances)
    {
        const bool reed =
            (instance.flags & engine::game::FoliageBladeReed) != 0u;
        const bool flower =
            (instance.flags & engine::game::FoliageBladeFlower) != 0u;
        const bool carpet =
            (instance.flags &
             engine::game::FoliageBladeGroundCoverOnly) != 0u;
        const bool aquatic =
            (instance.flags & engine::game::FoliageBladeAquatic) != 0u;
        accentInstanceCount += carpet ? 0u : 1u;
        carpetInstanceCount += carpet ? 1u : 0u;
        reedInstanceCount += reed ? 1u : 0u;
        flowerInstanceCount += flower ? 1u : 0u;
        reedFlowerInstanceCount += reed && flower ? 1u : 0u;
        aquaticInstanceCount += aquatic ? 1u : 0u;
        floweringAquaticInstanceCount += aquatic && flower ? 1u : 0u;
        tippedAccentInstanceCount +=
            !carpet && instance.tipMaterialId != 0u ? 1u : 0u;
        if (instance.tipMaterialId >= NaturePondMaterial::Flower::Base &&
            instance.tipMaterialId < NaturePondMaterial::Flower::Base +
                                         NaturePondMaterial::Flower::Count)
        {
            flowerPaletteCoverage[instance.tipMaterialId -
                                  NaturePondMaterial::Flower::Base] = true;
        }
        require(referenceFoliageIds.insert(instance.stableId).second,
                "Every semantic foliage instance should have a unique stable ID");
        require((instance.stableId >> 32u) == kSeed &&
                    (instance.stablePatchId >> 32u) == kSeed &&
                    (instance.flags & ~kKnownFoliageFlags) == 0u,
                "Foliage identities should retain their world seed and known flags");

        const int candidateX =
            carpet || aquatic
                ? carpetCandidateCoordinate(instance.stableId, 11u)
                   : candidateCoordinate(instance.stableId, 16u);
        const int candidateZ =
            carpet || aquatic
                ? carpetCandidateCoordinate(instance.stableId, 0u)
                   : candidateCoordinate(instance.stableId, 0u);
        const std::optional<engine::scene::NaturePondFoliagePlacement>
            placement =
                aquatic
                    ? engine::scene::sampleNaturePondWaterFloraDistribution(
                          kSeed, candidateX, candidateZ, 1000u)
                : carpet
                    ? engine::scene::sampleNaturePondMeadowCarpetDistribution(
                          kSeed, candidateX, candidateZ, 1000u)
                    : engine::scene::sampleNaturePondFoliageDistribution(
                          kSeed, candidateX, candidateZ, 1000u);
        require(placement.has_value(),
                "Every reference semantic plant should originate from its explicit ecology sampler");
        const float expectedRootX =
            -11.95f + 0.30f * static_cast<float>(candidateX);
        const float expectedRootZ =
            -9.95f + 0.30f * static_cast<float>(candidateZ);
        const float expectedPatchRootX =
            -11.95f + 0.30f *
                          static_cast<float>(
                              placement->patchAnchorCandidateX);
        const float expectedPatchRootZ =
            -9.95f + 0.30f *
                          static_cast<float>(
                              placement->patchAnchorCandidateZ);
        const float expectedPatchRadius =
            0.30f * static_cast<float>(std::max(
                        placement->patchRadiusCandidateX,
                        placement->patchRadiusCandidateZ)) +
            0.15f;
        const engine::game::FoliageMorphology expectedMorphology =
            engine::game::foliageMorphologyForPatch(
                placement->kind, placement->patchSeed);
        require(instance.stablePatchId == placement->stablePatchId &&
                    instance.patchSeed == placement->patchSeed &&
                    instance.randomSeed == placement->plantSeed &&
                    instance.stableId ==
                        (aquatic
                             ? engine::game::foliageAquaticStableId(
                                   kSeed, candidateX, candidateZ)
                         : carpet
                             ? engine::game::foliageCarpetStableId(
                                   kSeed, candidateX, candidateZ)
                             : engine::game::foliageStableId(
                                   kSeed, candidateX, candidateZ)) &&
                    instance.patchKind == placement->kind &&
                    instance.morphology == expectedMorphology &&
                    reed ==
                        (!carpet && placement->kind ==
                         engine::game::FoliagePatchKind::ReedCluster) &&
                    ((aquatic &&
                      placement->kind ==
                          engine::game::FoliagePatchKind::WaterLilyCluster) ||
                     (!aquatic &&
                      flower ==
                          (!carpet &&
                           placement->kind ==
                               engine::game::FoliagePatchKind::
                                   FlowerCluster))) &&
                    nearlyEqual(instance.rootWorld.x, expectedRootX, 1e-4f) &&
                    nearlyEqual(instance.rootWorld.z, expectedRootZ, 1e-4f) &&
                    nearlyEqual(instance.patchRootWorld.x,
                                expectedPatchRootX, 1e-4f) &&
                    nearlyEqual(instance.patchRootWorld.z,
                                expectedPatchRootZ, 1e-4f) &&
                    nearlyEqual(instance.patchRadiusMeters,
                                expectedPatchRadius, 1e-4f) &&
                    (aquatic || nearlyEqual(
                        (instance.patchRootWorld.y -
                         referenceFoliageVolume.position.y) /
                            referenceFoliageVolume.scale.y,
                        std::round(
                            (instance.patchRootWorld.y -
                             referenceFoliageVolume.position.y) /
                            referenceFoliageVolume.scale.y),
                        1e-4f)),
                "Semantic foliage should preserve sampler-owned patch ID, root, radius, seed, kind, and morphology");

        const glm::vec3 localRoot =
            (instance.rootWorld - referenceFoliageVolume.position) /
            referenceFoliageVolume.scale;
        const float heightCells =
            instance.heightMeters / referenceFoliageVolume.scale.y;
        const float leanCells =
            instance.restLeanMeters / referenceFoliageVolume.scale.x;
        require(nearlyEqual(localRoot.x - 0.5f,
                            std::round(localRoot.x - 0.5f), 1e-4f) &&
                    (aquatic || nearlyEqual(
                         localRoot.y, std::round(localRoot.y), 1e-4f)) &&
                    nearlyEqual(localRoot.z - 0.5f,
                                std::round(localRoot.z - 0.5f), 1e-4f) &&
                    (aquatic || nearlyEqual(
                         heightCells, std::round(heightCells), 1e-4f)) &&
                    nearlyEqual(leanCells, std::round(leanCells), 1e-4f) &&
                    std::abs(std::round(leanCells)) <= 1.0f,
                "Semantic foliage should preserve legacy lattice placement and lean");
        require(instance.heightMeters > 0.0f &&
                    nearlyEqual(instance.cellSizeMeters,
                                reference.detailVoxelPitch) &&
                    instance.stemMaterialId >= NaturePondMaterial::Foliage::Base &&
                    instance.stemMaterialId < NaturePondMaterial::Foliage::Base +
                                                   NaturePondMaterial::Foliage::Count,
                "Every semantic foliage instance should retain a valid authored shape and stem");
        const bool flowerPatch =
            placement->kind ==
                engine::game::FoliagePatchKind::FlowerCluster ||
            placement->kind ==
                engine::game::FoliagePatchKind::WaterLilyCluster;
        require((flowerPatch &&
                 instance.tipMaterialId >= NaturePondMaterial::Flower::Base &&
                 instance.tipMaterialId < NaturePondMaterial::Flower::Base +
                                                  NaturePondMaterial::Flower::Count) ||
                    (!flowerPatch && instance.tipMaterialId == 0u),
                "Patch identity and tip materials should share one generator authority");

        const glm::vec3 rootAtTimeA = engine::game::foliageDisplacement(
            instance, 0.0f, 0.0f);
        const glm::vec3 rootAtTimeB = engine::game::foliageDisplacement(
            instance, 0.0f, 17.25f);
        require(rootAtTimeA == glm::vec3(0.0f) && rootAtTimeB == glm::vec3(0.0f),
                "Foliage deformation should keep every root exactly anchored");

        const glm::vec3 frozenCurrent = engine::game::foliageDisplacement(
            instance, 1.0f, 6.5f,
            engine::scene::defaultEnvironmentWindSettings(), 0.0f);
        const glm::vec3 frozenPrevious = engine::game::foliageDisplacement(
            instance, 1.0f, 19.0f,
            engine::scene::defaultEnvironmentWindSettings(), 0.0f);
        require(frozenCurrent == frozenPrevious,
                "Disabled sway should produce identical current and previous geometry");

        for (float timeSeconds : {0.0f, 1.25f, 7.5f, 31.0f})
        {
            const glm::vec3 displacement = engine::game::foliageDisplacement(
                instance, 1.0f, timeSeconds);
            require(glm::length(glm::vec2(displacement.x, displacement.z)) <=
                        engine::game::foliageConservativeDisplacement(instance) + 1e-5f,
                    "Animated foliage should remain inside its conservative displacement bound");
        }
    }
    require(accentInstanceCount == 590u && carpetInstanceCount == 2489u &&
                reedInstanceCount == 53u && flowerInstanceCount == 203u &&
                reedFlowerInstanceCount == 0u &&
                aquaticInstanceCount == 47u &&
                floweringAquaticInstanceCount == 21u &&
                tippedAccentInstanceCount ==
                    flowerInstanceCount - floweringAquaticInstanceCount +
                        aquaticInstanceCount,
            "Reference ecology should preserve its terrestrial and aquatic floral-life accents above substantial ground cover (measured flowers/aquatic/blooming-aquatic=" +
                std::to_string(flowerInstanceCount) + "/" +
                std::to_string(aquaticInstanceCount) + "/" +
                std::to_string(floweringAquaticInstanceCount) + ")");
    require(std::all_of(flowerPaletteCoverage.begin(),
                        flowerPaletteCoverage.end(),
                        [](bool covered) { return covered; }),
            "Reference ecology should exercise every paired floral palette shade");

    const auto& densitySteps = naturePondDensitySteps();
    require(densitySteps[0].id == "baseline" &&
                densitySteps[1].id == "expanded-coverage" &&
                densitySteps[2].id == "expanded-dense" &&
                densitySteps[0].patchExtentMeters == glm::vec2(24.0f, 20.0f) &&
                densitySteps[1].patchExtentMeters == glm::vec2(30.0f, 25.0f) &&
                densitySteps[2].scatterDensityMultiplier == 1.5f,
            "Nature pond density steps should preserve their measured axis contract");
    require(naturePondDensityStepForSceneName(
                "nature_pond_density_base_bank_probe") ==
                NaturePondDensityStep::Baseline &&
                naturePondDensityStepForSceneName(
                    "nature_pond_density_coverage_probe") ==
                    NaturePondDensityStep::ExpandedCoverage &&
                naturePondDensityStepForSceneName(
                    "nature_pond_density_dense_bank_probe") ==
                    NaturePondDensityStep::ExpandedCoverageDenseScatter &&
                isNaturePondDensityProbeName("nature_pond_density_base_probe") &&
                !isNaturePondDensityProbeName("nature_pond_probe"),
            "Density probe scene routing should be explicit and isolated");

    const NaturePondBuild densityBaseline = buildNaturePond(
        kSeed, NaturePondDetailTier::Reference10Cm,
        NaturePondBuildOptions{false, NaturePondDensityStep::Baseline});
    const NaturePondBuild expandedCoverage = buildNaturePond(
        kSeed, NaturePondDetailTier::Reference10Cm,
        NaturePondBuildOptions{false, NaturePondDensityStep::ExpandedCoverage});
    const NaturePondBuild expandedCoverageRepeat = buildNaturePond(
        kSeed, NaturePondDetailTier::Reference10Cm,
        NaturePondBuildOptions{false, NaturePondDensityStep::ExpandedCoverage});
    const NaturePondBuild expandedDense = buildNaturePond(
        kSeed, NaturePondDetailTier::Reference10Cm,
        NaturePondBuildOptions{
            false, NaturePondDensityStep::ExpandedCoverageDenseScatter});

    const engine::render::FoliageVoxelGeometry referenceVoxelGeometry =
        engine::render::buildFoliageVoxelGeometry(reference.foliageInstances);
    const engine::render::FoliageVoxelGeometry expandedCoverageVoxelGeometry =
        engine::render::buildFoliageVoxelGeometry(
            expandedCoverage.foliageInstances);
    const engine::render::FoliageVoxelGeometry denseVoxelGeometry =
        engine::render::buildFoliageVoxelGeometry(expandedDense.foliageInstances);
    const auto renderFoliageInstances = [](
        const NaturePondBuild& build) {
        std::vector<engine::game::FoliageBladeInstance> result =
            build.foliageInstances;
        result.insert(result.end(),
                      build.heroCanopyFoliageInstances.begin(),
                      build.heroCanopyFoliageInstances.end());
        return result;
    };
    const std::vector<engine::game::FoliageBladeInstance>
        referenceRenderFoliage = renderFoliageInstances(reference);
    const std::vector<engine::game::FoliageBladeInstance>
        expandedRenderFoliage = renderFoliageInstances(expandedCoverage);
    const std::vector<engine::game::FoliageBladeInstance>
        denseRenderFoliage = renderFoliageInstances(expandedDense);
    const engine::render::FoliageVoxelGeometry referenceRenderGeometry =
        engine::render::buildFoliageVoxelGeometry(referenceRenderFoliage);
    const engine::render::FoliageVoxelGeometry expandedRenderGeometry =
        engine::render::buildFoliageVoxelGeometry(expandedRenderFoliage);
    const engine::render::FoliageVoxelGeometry denseRenderGeometry =
        engine::render::buildFoliageVoxelGeometry(denseRenderFoliage);
    require(referenceRenderFoliage.size() == 3142u &&
                referenceRenderGeometry.patchCount == 75u &&
                referenceRenderGeometry.primitives.size() == 47843u &&
                expandedRenderFoliage.size() == 5655u &&
                expandedRenderGeometry.patchCount == 112u &&
                expandedRenderGeometry.primitives.size() == 86721u &&
                denseRenderFoliage.size() == 6962u &&
                denseRenderGeometry.patchCount == 121u &&
                denseRenderGeometry.primitives.size() == 108482u &&
                referenceRenderGeometry.primitives.size() *
                        sizeof(engine::render::FoliageGpuPrimitive) ==
                    3061952u,
            "The one-batch render population should append the exact bounded TREE-002 sprig budget to every density tier");
    const auto validatePondVoxelGeometry = [&referenceVoxelGeometry,
                                             &expandedCoverageVoxelGeometry,
                                             &denseVoxelGeometry](
        const NaturePondBuild& build,
        const engine::render::FoliageVoxelGeometry& geometry,
        size_t expectedSemanticCount, size_t expectedPatchCount,
        size_t expectedPrimitiveCount, uint64_t expectedFallbackVoxels) {
        require(build.foliageInstances.size() == expectedSemanticCount &&
                    geometry.semanticInstanceCount == expectedSemanticCount &&
                    build.foliageOccupiedVoxels == expectedFallbackVoxels,
                    "Pond patch geometry should preserve its measured floral-life source and fallback counts (measured=" +
                        std::to_string(build.foliageInstances.size()) + "/" +
                        std::to_string(build.foliageOccupiedVoxels) + ")");
        const size_t patchKindCount = std::accumulate(
            geometry.patchKindCounts.begin(), geometry.patchKindCounts.end(),
            size_t{0});
        const size_t morphologyCount = std::accumulate(
            geometry.morphologyPatchCounts.begin(),
            geometry.morphologyPatchCounts.end(), size_t{0});
        require(geometry.patchCount == expectedPatchCount &&
                    patchKindCount == geometry.patchCount &&
                    morphologyCount == geometry.patchCount &&
                    geometry.primitives.size() == expectedPrimitiveCount &&
                    geometry.primitives.size() >
                        geometry.semanticInstanceCount,
                    "Pond patch geometry should retain its exact deterministic V9 patch and primitive counts (measured baseline/expanded/dense=" +
                        std::to_string(referenceVoxelGeometry.primitives.size()) +
                        "/" +
                        std::to_string(expandedCoverageVoxelGeometry.primitives.size()) +
                        "/" +
                        std::to_string(denseVoxelGeometry.primitives.size()) +
                        "; current patches=" +
                        std::to_string(geometry.patchCount) + ")");

        for (const engine::game::FoliageBladeInstance& plant :
             build.foliageInstances)
        {
            require(std::isfinite(plant.cellSizeMeters) &&
                        plant.cellSizeMeters > 0.0f &&
                        nearlyEqual(plant.cellSizeMeters,
                                    build.detailVoxelPitch),
                    "Every semantic plant should preserve its authored voxel cell size");
        }

        std::set<std::tuple<int64_t, int64_t, int64_t>> occupiedCenters;
        std::array<size_t,
                   static_cast<size_t>(
                       engine::game::FoliageMorphology::Count)>
            primitiveMorphologyCounts{};
        std::array<size_t,
                   static_cast<size_t>(
                       engine::game::FoliageMorphology::Count)>
            anchoredMorphologyCounts{};
        std::array<uint32_t,
                   static_cast<size_t>(
                       engine::game::FoliageMorphology::Count)>
            roleMasks{};
        for (const engine::render::FoliageGpuPrimitive& primitive :
             geometry.primitives)
        {
            const uint32_t morphology = primitive.materialSeedFlags.y;
            require(morphology < primitiveMorphologyCounts.size(),
                    "Every pond primitive should identify a known foliage morphology");
            ++primitiveMorphologyCounts[morphology];
            anchoredMorphologyCounts[morphology] +=
                primitive.centerMotionT.w == 0.0f ? 1u : 0u;
            roleMasks[morphology] |= 1u << primitive.materialSeedFlags.w;
            const auto role = static_cast<engine::render::FoliageVoxelPrimitiveRole>(
                primitive.materialSeedFlags.w);
            const float maximumHalfExtent =
                role == engine::render::FoliageVoxelPrimitiveRole::GroundCover
                    ? build.detailVoxelPitch * 1.50f
                    : build.detailVoxelPitch * 0.75f;
            const bool flower =
                role == engine::render::FoliageVoxelPrimitiveRole::Flower;
            const bool stemOrBranch =
                role == engine::render::FoliageVoxelPrimitiveRole::Stem ||
                role == engine::render::FoliageVoxelPrimitiveRole::Leaf ||
                role == engine::render::FoliageVoxelPrimitiveRole::GroundCover;
            const bool validMaterial =
                (flower &&
                 primitive.materialSeedFlags.x >=
                     NaturePondMaterial::Flower::Base &&
                 primitive.materialSeedFlags.x <
                     NaturePondMaterial::Flower::Base +
                         NaturePondMaterial::Flower::Count) ||
                (stemOrBranch &&
                 primitive.materialSeedFlags.x >=
                     NaturePondMaterial::Foliage::Base &&
                 primitive.materialSeedFlags.x <
                     NaturePondMaterial::Foliage::Base +
                         NaturePondMaterial::Foliage::Count);
            require((flower || stemOrBranch) && validMaterial &&
                        std::isfinite(primitive.centerMotionT.x) &&
                        std::isfinite(primitive.centerMotionT.y) &&
                        std::isfinite(primitive.centerMotionT.z) &&
                        primitive.centerMotionT.w >= 0.0f &&
                        primitive.centerMotionT.w <= 1.0f &&
                        glm::all(glm::greaterThan(
                            glm::vec3(primitive.halfExtentPhase),
                            glm::vec3(0.0f))) &&
                        glm::all(glm::lessThanEqual(
                            glm::vec3(primitive.halfExtentPhase),
                            glm::vec3(maximumHalfExtent))) &&
                        std::isfinite(primitive.halfExtentPhase.w) &&
                        std::isfinite(primitive.swayProfile.x) &&
                        std::isfinite(primitive.swayProfile.y) &&
                        std::isfinite(primitive.swayProfile.z) &&
                        std::isfinite(primitive.swayProfile.w),
                    "Generated pond patch primitives should retain valid extents, roles, materials, and motion data");

            const auto centerKey = std::make_tuple(
                static_cast<int64_t>(
                    std::llround(primitive.centerMotionT.x * 10000.0f)),
                static_cast<int64_t>(
                    std::llround(primitive.centerMotionT.y * 10000.0f)),
                static_cast<int64_t>(
                    std::llround(primitive.centerMotionT.z * 10000.0f)));
            require(occupiedCenters.insert(centerKey).second,
                    "Generated pond patch primitives should not retain duplicate centers");
        }
        constexpr uint32_t kLayeredPlantMask =
            (1u << static_cast<uint32_t>(
                 engine::render::FoliageVoxelPrimitiveRole::Stem)) |
            (1u << static_cast<uint32_t>(
                 engine::render::FoliageVoxelPrimitiveRole::Leaf)) |
            (1u << static_cast<uint32_t>(
                 engine::render::FoliageVoxelPrimitiveRole::GroundCover));
        constexpr uint32_t kFlowerMask =
            1u << static_cast<uint32_t>(
                engine::render::FoliageVoxelPrimitiveRole::Flower);
        for (size_t morphology = 0;
             morphology < geometry.morphologyPatchCounts.size(); ++morphology)
        {
            if (morphology == static_cast<size_t>(
                                  engine::game::FoliageMorphology::
                                      TreeCanopySprig))
            {
                require(primitiveMorphologyCounts[morphology] == 0u &&
                            anchoredMorphologyCounts[morphology] == 0u &&
                            roleMasks[morphology] == 0u,
                        "Terrain ecology geometry should not synthesize hero-canopy sprigs");
                continue;
            }
            const bool waterLily =
                morphology == static_cast<size_t>(
                                  engine::game::FoliageMorphology::WaterLily);
            require(primitiveMorphologyCounts[morphology] > 0u &&
                        anchoredMorphologyCounts[morphology] > 0u &&
                        (waterLily
                             ? (roleMasks[morphology] &
                                (1u << static_cast<uint32_t>(
                                     engine::render::
                                         FoliageVoxelPrimitiveRole::
                                             GroundCover))) != 0u
                             : (roleMasks[morphology] & kLayeredPlantMask) ==
                                   kLayeredPlantMask),
                    "Every authored terrain foliage morphology should emit its required anchored topology");
        }
        require((roleMasks[static_cast<size_t>(
                     engine::game::FoliageMorphology::DaisyFlower)] &
                 kFlowerMask) != 0u &&
                    (roleMasks[static_cast<size_t>(
                         engine::game::FoliageMorphology::SpikeFlower)] &
                     kFlowerMask) != 0u &&
                    (roleMasks[static_cast<size_t>(
                         engine::game::FoliageMorphology::WaterLily)] &
                     kFlowerMask) != 0u,
                "Terrestrial flower species and flowering water lilies should emit a distinct blossom role");
    };
    validatePondVoxelGeometry(
        reference, referenceVoxelGeometry, 3079u, 68u, 47654u, 8234u);
    validatePondVoxelGeometry(
        expandedCoverage, expandedCoverageVoxelGeometry, 5592u, 105u,
        86532u, 15165u);
    validatePondVoxelGeometry(
        expandedDense, denseVoxelGeometry, 6899u, 114u, 108293u, 19086u);

    const engine::game::FoliagePatchLayout baselineDensityPatchLayout =
        engine::game::buildFoliagePatchLayout(
            expandedCoverage.foliageInstances);
    const engine::game::FoliagePatchLayout densePatchLayout =
        engine::game::buildFoliagePatchLayout(expandedDense.foliageInstances);
    require(baselineDensityPatchLayout.valid && densePatchLayout.valid &&
                baselineDensityPatchLayout.patches.size() <
                    densePatchLayout.patches.size() &&
                densePatchLayout.patches.size() == 114u,
            "Baseline-density and dense ecology should retain a strict stable-patch superset");

    std::map<uint64_t, const engine::game::FoliagePatchInstance*>
        baselineDensityPatchesById;
    std::map<uint64_t, const engine::game::FoliagePatchInstance*>
        densePatchesById;
    for (const engine::game::FoliagePatchInstance& patch :
         baselineDensityPatchLayout.patches)
    {
        require(baselineDensityPatchesById
                    .emplace(patch.stableId, &patch)
                    .second,
                "Baseline-density ecology should retain unique stable patch identities");
    }
    for (const engine::game::FoliagePatchInstance& patch :
         densePatchLayout.patches)
    {
        require(densePatchesById.emplace(patch.stableId, &patch).second,
                "Dense ecology should retain unique stable patch identities");
    }

    const auto groupByStablePatch = [](
        const std::vector<engine::game::FoliageBladeInstance>& plants) {
        std::map<uint64_t, std::vector<engine::game::FoliageBladeInstance>>
            result;
        for (const engine::game::FoliageBladeInstance& plant : plants)
        {
            result[plant.stablePatchId].push_back(plant);
        }
        return result;
    };
    const auto baselineDensityPlantsByPatch =
        groupByStablePatch(expandedCoverage.foliageInstances);
    const auto densePlantsByPatch =
        groupByStablePatch(expandedDense.foliageInstances);
    size_t expandedPatchMembershipCount = 0u;
    for (const auto& [stablePatchId, baselinePatch] :
         baselineDensityPatchesById)
    {
        const auto densePatchFound = densePatchesById.find(stablePatchId);
        require(densePatchFound != densePatchesById.end(),
                "Dense ecology should retain every baseline stable patch identity");
        const engine::game::FoliagePatchInstance& densePatch =
            *densePatchFound->second;
        require(baselinePatch->stableId == densePatch.stableId &&
                    baselinePatch->randomSeed == densePatch.randomSeed &&
                    baselinePatch->kind == densePatch.kind &&
                    baselinePatch->morphology == densePatch.morphology,
                "Dense ecology should not reseed or relabel a baseline patch");
        require(baselinePatch->rootWorld == densePatch.rootWorld,
                "Dense ecology should not move baseline patch " +
                    std::to_string(stablePatchId) + " root (baseline=" +
                    std::to_string(baselinePatch->rootWorld.x) + "," +
                    std::to_string(baselinePatch->rootWorld.y) + "," +
                    std::to_string(baselinePatch->rootWorld.z) +
                    "; dense=" + std::to_string(densePatch.rootWorld.x) +
                    "," + std::to_string(densePatch.rootWorld.y) + "," +
                    std::to_string(densePatch.rootWorld.z) + ")");
        require(baselinePatch->radiusMeters == densePatch.radiusMeters,
                "Dense ecology should not resize a baseline patch");
        require(baselinePatch->heightMeters == densePatch.heightMeters &&
                    baselinePatch->cellSizeMeters == densePatch.cellSizeMeters &&
                    baselinePatch->yawRadians == densePatch.yawRadians,
                "Dense ecology should not reshape a baseline patch");
        require(baselinePatch->phaseRadians == densePatch.phaseRadians &&
                    baselinePatch->primaryMaterialId ==
                        densePatch.primaryMaterialId &&
                    baselinePatch->secondaryMaterialId ==
                        densePatch.secondaryMaterialId &&
                    baselinePatch->accentMaterialId ==
                        densePatch.accentMaterialId,
                "Dense ecology should not recolor or retime a baseline patch");
        require(densePatch.sourcePlantCount >=
                    baselinePatch->sourcePlantCount,
                "Dense ecology should not remove baseline patch membership");
        expandedPatchMembershipCount +=
            densePatch.sourcePlantCount > baselinePatch->sourcePlantCount
                ? 1u
                : 0u;

        const auto baselinePlantsFound =
            baselineDensityPlantsByPatch.find(stablePatchId);
        const auto densePlantsFound = densePlantsByPatch.find(stablePatchId);
        require(baselinePlantsFound !=
                        baselineDensityPlantsByPatch.end() &&
                    densePlantsFound != densePlantsByPatch.end(),
                "Stable patch layouts should retain their semantic member groups");
        const engine::render::FoliageVoxelGeometry baselinePatchGeometry =
            engine::render::buildFoliageVoxelGeometry(
                baselinePlantsFound->second);
        const engine::render::FoliageVoxelGeometry densePatchGeometry =
            engine::render::buildFoliageVoxelGeometry(
                densePlantsFound->second);
        require(baselinePatchGeometry.patchCount == 1u &&
                    densePatchGeometry.patchCount == 1u &&
                    baselinePatchGeometry.patchKindCounts ==
                        densePatchGeometry.patchKindCounts &&
                    baselinePatchGeometry.morphologyPatchCounts ==
                        densePatchGeometry.morphologyPatchCounts &&
                    baselinePatchGeometry.primitives.size() <=
                        densePatchGeometry.primitives.size(),
                "Dense membership should add grounded plant forms without reclassifying an existing patch");
        for (const engine::render::FoliageGpuPrimitive& baselinePrimitive :
             baselinePatchGeometry.primitives)
        {
            const auto found = std::find_if(
                densePatchGeometry.primitives.begin(),
                densePatchGeometry.primitives.end(),
                [&baselinePrimitive](
                    const engine::render::FoliageGpuPrimitive& densePrimitive) {
                    return baselinePrimitive.centerMotionT ==
                               densePrimitive.centerMotionT &&
                           baselinePrimitive.halfExtentPhase ==
                               densePrimitive.halfExtentPhase &&
                           baselinePrimitive.materialSeedFlags ==
                               densePrimitive.materialSeedFlags &&
                           baselinePrimitive.swayProfile ==
                               densePrimitive.swayProfile;
                });
            require(found != densePatchGeometry.primitives.end(),
                    "Dense membership should preserve every baseline anchored primitive byte-for-value");
        }
    }
    require(densePatchesById.size() >
                    baselineDensityPatchesById.size() &&
                expandedPatchMembershipCount > 0u,
            "Dense ecology should be a strict baseline-density patch superset with additional membership");

    std::vector<engine::game::FoliageBladeInstance> reversedFoliage =
        reference.foliageInstances;
    std::reverse(reversedFoliage.begin(), reversedFoliage.end());
    const engine::render::FoliageVoxelGeometry reversedVoxelGeometry =
        engine::render::buildFoliageVoxelGeometry(reversedFoliage);
    require(reversedVoxelGeometry.semanticInstanceCount ==
                    referenceVoxelGeometry.semanticInstanceCount &&
                reversedVoxelGeometry.patchCount ==
                    referenceVoxelGeometry.patchCount &&
                reversedVoxelGeometry.patchKindCounts ==
                    referenceVoxelGeometry.patchKindCounts &&
                reversedVoxelGeometry.morphologyPatchCounts ==
                    referenceVoxelGeometry.morphologyPatchCounts &&
                reversedVoxelGeometry.primitives.size() ==
                    referenceVoxelGeometry.primitives.size(),
            "Patch coalescing should be independent of semantic input order");
    for (size_t primitiveIndex = 0;
         primitiveIndex < referenceVoxelGeometry.primitives.size();
         ++primitiveIndex)
    {
        const engine::render::FoliageGpuPrimitive& lhs =
            referenceVoxelGeometry.primitives[primitiveIndex];
        const engine::render::FoliageGpuPrimitive& rhs =
            reversedVoxelGeometry.primitives[primitiveIndex];
        require(lhs.centerMotionT == rhs.centerMotionT &&
                    lhs.halfExtentPhase == rhs.halfExtentPhase &&
                    lhs.materialSeedFlags == rhs.materialSeedFlags &&
                    lhs.swayProfile == rhs.swayProfile,
                "Reversed semantic input should retain byte-value patch geometry");
    }

    const engine::scene::EnvironmentWindSettings& windSettings =
        engine::scene::defaultEnvironmentWindSettings();
    const engine::scene::EnvironmentWindWaveShape& windShape =
        engine::scene::defaultEnvironmentWindWaveShape();
    const glm::vec2 normalizedWind = windSettings.direction;
    const glm::vec2 crossWind(-normalizedWind.y, normalizedWind.x);
    std::unordered_map<uint64_t, const engine::game::FoliageBladeInstance*>
        referenceFoliageByPatch;
    referenceFoliageByPatch.reserve(referenceVoxelGeometry.patchCount);
    size_t sharedPatchPlants = 0u;
    for (const engine::game::FoliageBladeInstance& instance :
         reference.foliageInstances)
    {
        const auto [found, inserted] = referenceFoliageByPatch.emplace(
            instance.stablePatchId, &instance);
        if (!inserted)
        {
            const engine::game::FoliageBladeInstance& authored = *found->second;
            require(instance.stablePatchId == authored.stablePatchId &&
                        instance.patchRootWorld == authored.patchRootWorld &&
                        instance.cellSizeMeters == authored.cellSizeMeters &&
                        instance.patchRadiusMeters ==
                            authored.patchRadiusMeters &&
                        instance.yawRadians == authored.yawRadians &&
                        instance.phaseRadians == authored.phaseRadians &&
                        instance.patchSeed == authored.patchSeed &&
                        instance.stemMaterialId ==
                            authored.stemMaterialId &&
                        instance.secondaryMaterialId ==
                            authored.secondaryMaterialId &&
                        instance.tipMaterialId == authored.tipMaterialId &&
                        instance.patchKind == authored.patchKind &&
                        instance.morphology == authored.morphology,
                    "Plants in one ecology patch should share authored root, radius, seed, palette, motion, kind, and morphology");
            ++sharedPatchPlants;
        }
    }
    require(referenceFoliageByPatch.size() == referenceVoxelGeometry.patchCount &&
                sharedPatchPlants > 400u,
            "Reference foliage should retain substantial coherent patch membership");

    std::vector<const engine::game::FoliageBladeInstance*> patchRepresentatives;
    patchRepresentatives.reserve(referenceFoliageByPatch.size());
    for (const auto& [stablePatchId, representative] : referenceFoliageByPatch)
    {
        (void)stablePatchId;
        patchRepresentatives.push_back(representative);
    }
    size_t coherentNeighborPatches = 0u;
    for (size_t first = 0; first < patchRepresentatives.size(); ++first)
    {
        for (size_t second = first + 1; second < patchRepresentatives.size();
             ++second)
        {
            const engine::game::FoliageBladeInstance& instance =
                *patchRepresentatives[first];
            const engine::game::FoliageBladeInstance& other =
                *patchRepresentatives[second];
            const glm::vec2 rootDelta(
                other.patchRootWorld.x - instance.patchRootWorld.x,
                other.patchRootWorld.z - instance.patchRootWorld.z);
            if (glm::length(rootDelta) > 3.1f)
            {
                continue;
            }
            const float expectedSpatialDelta =
                glm::dot(rootDelta, normalizedWind) *
                    windShape.alongPhaseRadiansPerMeter +
                glm::dot(rootDelta, crossWind) *
                    windShape.crossPhaseRadiansPerMeter;
            const float residual = other.phaseRadians - instance.phaseRadians -
                                   expectedSpatialDelta;
            require(std::abs(residual) <= 0.081f,
                    "Neighboring ecology patches should retain a coherent spatial wind phase with only bounded seed jitter");
            ++coherentNeighborPatches;
        }
    }
    require(coherentNeighborPatches > 20u,
            "Reference foliage should retain a substantial coherent patch-neighbor sample");

    require(densityBaseline.patchExtentMeters == glm::vec2(24.0f, 20.0f) &&
                densityBaseline.scatterDensityMultiplier == 1.0f &&
                densityBaseline.volumes.size() == reference.volumes.size(),
            "Density baseline should describe the accepted reference footprint");
    for (size_t volumeIndex = 0; volumeIndex < reference.volumes.size(); ++volumeIndex)
    {
        require(densityBaseline.volumes[volumeIndex].voxels ==
                    reference.volumes[volumeIndex].voxels,
                "Explicit density baseline must be byte-identical to accepted content");
        require(expandedCoverage.volumes[volumeIndex].voxels ==
                    expandedCoverageRepeat.volumes[volumeIndex].voxels,
                "Expanded density coverage must remain byte-deterministic");
    }
    require(sameFoliageSequence(densityBaseline, reference) &&
                sameFoliageSequence(expandedCoverage, expandedCoverageRepeat),
            "Density probes should preserve deterministic semantic foliage extraction");

    std::unordered_map<uint64_t, const engine::game::FoliageBladeInstance*>
        expandedFoliageById;
    std::unordered_map<uint64_t, const engine::game::FoliageBladeInstance*>
        denseFoliageById;
    expandedFoliageById.reserve(expandedCoverage.foliageInstances.size());
    denseFoliageById.reserve(expandedDense.foliageInstances.size());
    for (const engine::game::FoliageBladeInstance& instance :
         expandedCoverage.foliageInstances)
    {
        require(expandedFoliageById.emplace(instance.stableId, &instance).second,
                "Expanded density coverage should retain unique foliage identities");
    }
    for (const engine::game::FoliageBladeInstance& instance :
         expandedDense.foliageInstances)
    {
        require(denseFoliageById.emplace(instance.stableId, &instance).second,
                "Dense ecology should retain unique foliage identities");
    }
    for (const engine::game::FoliageBladeInstance& baselineInstance :
         reference.foliageInstances)
    {
        const auto expandedFound =
            expandedFoliageById.find(baselineInstance.stableId);
        const auto denseFound =
            denseFoliageById.find(baselineInstance.stableId);
        require(expandedFound != expandedFoliageById.end() &&
                    denseFound != denseFoliageById.end(),
                "Expanded coverage and dense scatter should retain every reference semantic identity");
        require(equivalentFoliageInstance(*expandedFound->second,
                                          baselineInstance) &&
                    equivalentFoliageInstance(*denseFound->second,
                                              baselineInstance),
                "Coverage and density increases should preserve every baseline semantic field within lattice tolerance");
    }
    for (const engine::game::FoliageBladeInstance& baselineDensityInstance :
         expandedCoverage.foliageInstances)
    {
        const auto denseFound =
            denseFoliageById.find(baselineDensityInstance.stableId);
        require(denseFound != denseFoliageById.end(),
                "Dense scatter should retain every baseline-density semantic identity");
        require(sameFoliageInstance(*denseFound->second,
                                    baselineDensityInstance),
                "Density increases should preserve every baseline semantic field exactly");
    }
    require(expandedFoliageById.size() > reference.foliageInstances.size() &&
                denseFoliageById.size() > expandedFoliageById.size(),
            "Coverage and density steps should form strict semantic foliage supersets");

    require(expandedCoverage.volumes[0].dims == glm::ivec3(120, 30, 100) &&
                expandedCoverage.volumes[1].dims == glm::ivec3(300, 50, 250) &&
                expandedCoverage.volumes[2].dims == glm::ivec3(300, 50, 250) &&
                expandedDense.volumes[0].dims == expandedCoverage.volumes[0].dims &&
                expandedDense.volumes[1].dims == expandedCoverage.volumes[1].dims &&
                expandedCoverage.patchExtentMeters == glm::vec2(30.0f, 25.0f) &&
                expandedDense.patchExtentMeters == expandedCoverage.patchExtentMeters,
            "Coverage and scatter steps should isolate extent from density");

    const auto requireEmbeddedReference = [](const NaturePondVolumeData& inner,
                                             const NaturePondVolumeData& outer) {
        const glm::ivec3 offset = glm::ivec3(glm::round(
            (inner.position - outer.position) / inner.scale));
        for (int z = 0; z < inner.dims.z; ++z)
        {
            for (int y = 0; y < inner.dims.y; ++y)
            {
                for (int x = 0; x < inner.dims.x; ++x)
                {
                    const size_t innerIndex = static_cast<size_t>(x) +
                        static_cast<size_t>(inner.dims.x) *
                            (static_cast<size_t>(y) +
                             static_cast<size_t>(inner.dims.y) *
                                 static_cast<size_t>(z));
                    const int outerX = x + offset.x;
                    const int outerY = y + offset.y;
                    const int outerZ = z + offset.z;
                    const size_t outerIndex = static_cast<size_t>(outerX) +
                        static_cast<size_t>(outer.dims.x) *
                            (static_cast<size_t>(outerY) +
                             static_cast<size_t>(outer.dims.y) *
                                 static_cast<size_t>(outerZ));
                    require(inner.voxels[innerIndex] == outer.voxels[outerIndex],
                            "Expanded coverage should embed baseline cells unchanged: " +
                                inner.name + " at (" + std::to_string(x) + "," +
                                std::to_string(y) + "," + std::to_string(z) + ")");
                }
            }
        }
    };
    requireEmbeddedReference(reference.volumes[0], expandedCoverage.volumes[0]);
    requireEmbeddedReference(reference.volumes[1], expandedCoverage.volumes[1]);
    requireEmbeddedReference(reference.volumes[2], expandedCoverage.volumes[2]);

    require(expandedCoverage.terrainOccupiedVoxels >
                reference.terrainOccupiedVoxels &&
                expandedCoverage.opaqueDetailOccupiedVoxels >
                    reference.opaqueDetailOccupiedVoxels &&
                expandedCoverage.foliageOccupiedVoxels >
                    reference.foliageOccupiedVoxels,
            "Expanded coverage should add terrain, details, and foliage");
    require(expandedDense.terrainOccupiedVoxels ==
                expandedCoverage.terrainOccupiedVoxels &&
                expandedDense.opaqueDetailOccupiedVoxels >
                    expandedCoverage.opaqueDetailOccupiedVoxels &&
                expandedDense.foliageOccupiedVoxels >
                    expandedCoverage.foliageOccupiedVoxels &&
                expandedDense.volumes[3].voxels == expandedCoverage.volumes[3].voxels &&
                expandedDense.volumes[4].voxels == expandedCoverage.volumes[4].voxels &&
                expandedDense.volumes[5].voxels == expandedCoverage.volumes[5].voxels,
            "Dense scatter should change only packed detail occupancy");

    const NaturePondVolumeData& denseFoliage = expandedDense.volumes[2];
    glm::ivec3 foliageOccupiedMin = denseFoliage.dims;
    glm::ivec3 foliageOccupiedMaxExclusive(0);
    for (int z = 0; z < denseFoliage.dims.z; ++z)
    {
        for (int y = 0; y < denseFoliage.dims.y; ++y)
        {
            for (int x = 0; x < denseFoliage.dims.x; ++x)
            {
                const size_t index = static_cast<size_t>(x) +
                    static_cast<size_t>(denseFoliage.dims.x) *
                        (static_cast<size_t>(y) +
                         static_cast<size_t>(denseFoliage.dims.y) *
                             static_cast<size_t>(z));
                if (denseFoliage.voxels[index] == 0u)
                {
                    continue;
                }
                foliageOccupiedMin = glm::min(foliageOccupiedMin,
                                              glm::ivec3(x, y, z));
                foliageOccupiedMaxExclusive = glm::max(
                    foliageOccupiedMaxExclusive, glm::ivec3(x + 1, y + 1, z + 1));
            }
        }
    }
    const glm::mat4 foliageWorldFromLocal =
        glm::translate(glm::mat4(1.0f), denseFoliage.position) *
        glm::scale(glm::mat4(1.0f), denseFoliage.scale);
    const glm::mat4 foliageLocalFromWorld = glm::inverse(foliageWorldFromLocal);
    constexpr float kNearClipGuardRadius = 0.1284f;
    const auto denseOverviewFaces = engine::render::classifyVoxelRasterFaces(
        foliageLocalFromWorld, foliageWorldFromLocal,
        glm::vec3(foliageOccupiedMin), glm::vec3(foliageOccupiedMaxExclusive),
        cameraAnchors[0].position, kNearClipGuardRadius);
    const auto denseBankFaces = engine::render::classifyVoxelRasterFaces(
        foliageLocalFromWorld, foliageWorldFromLocal,
        glm::vec3(foliageOccupiedMin), glm::vec3(foliageOccupiedMaxExclusive),
        cameraAnchors[1].position, kNearClipGuardRadius);
    require(!denseOverviewFaces.fullScreenCoverageRequired && denseOverviewFaces.safe &&
                denseBankFaces.fullScreenCoverageRequired &&
                denseBankFaces.cameraInside,
            "Fixed density cameras should isolate normal overview coverage from the bank fallback");

    const NaturePondBuild parity = buildNaturePond(
        kSeed, NaturePondDetailTier::Reference10Cm,
        NaturePondBuildOptions{true});
    const NaturePondBuild parityRepeat = buildNaturePond(
        kSeed, NaturePondDetailTier::Reference10Cm,
        NaturePondBuildOptions{true});
    require(reference.lightingParityEmissiveOccupiedVoxels == 0 &&
                countBand(reference.volumes[1].voxels,
                          NaturePondMaterial::Ember::Base,
                          NaturePondMaterial::Ember::Count) == 0,
            "Accepted nature-pond scenes must not acquire parity-only emissive content");
    require(parity.lightingParityEmissiveOccupiedVoxels > 20 &&
                parity.lightingParityEmissiveOccupiedVoxels ==
                    countBand(parity.volumes[1].voxels,
                              NaturePondMaterial::Ember::Base,
                              NaturePondMaterial::Ember::Count),
            "Lighting parity generation should report every emissive alcove cell");
    require(parity.volumes[0].voxels == reference.volumes[0].voxels &&
                parity.volumes[3].voxels == reference.volumes[3].voxels &&
                parity.volumes[4].voxels == reference.volumes[4].voxels &&
                parity.volumes[5].voxels == reference.volumes[5].voxels,
            "Parity content should leave terrain and hero-tree ownership unchanged");
    require(parity.volumes[1].voxels == parityRepeat.volumes[1].voxels &&
                parity.volumes[2].voxels == parityRepeat.volumes[2].voxels,
            "Lighting parity alcove and its foliage clearing should be deterministic");
    require(sameFoliageSequence(parity, parityRepeat) &&
                parity.foliageInstances.size() < reference.foliageInstances.size(),
            "Lighting parity foliage clearing should remove semantic instances deterministically");

    const glm::vec3 referencePoint(0.0f, reference.layout.pondSurfaceHeight, 0.0f);
    const float overview10 = naturePondProjectedPixelsPerVoxel(
        0.10f, cameraAnchors[0], referencePoint, 1080.0f, glm::radians(60.0f));
    const float overview20 = naturePondProjectedPixelsPerVoxel(
        0.20f, cameraAnchors[0], referencePoint, 1080.0f, glm::radians(60.0f));
    const float overview25 = naturePondProjectedPixelsPerVoxel(
        0.25f, cameraAnchors[0], referencePoint, 1080.0f, glm::radians(60.0f));
    require(overview10 > 4.5f && overview10 < 6.0f &&
                nearlyEqual(overview20, overview10 * 2.0f) &&
                nearlyEqual(overview25, overview10 * 2.5f),
            "Fixed-camera projected pixels should scale linearly with detail pitch");
}

void testNaturePondSunroofProbeContract()
{
    constexpr uint32_t kSeed = 4319u;
    require(isNaturePondSunroofProbeName(NaturePondSunroof::SceneName) &&
                naturePondDetailTierForSceneName(
                    NaturePondSunroof::SceneName) ==
                    NaturePondDetailTier::Reference10Cm &&
                !isNaturePondSunroofProbeName("nature_pond_probe"),
            "The pond sunroof probe should have explicit, isolated reference-tier routing");

    const NaturePondBuild baseline = buildNaturePond(kSeed);
    NaturePondBuild decorated = baseline;
    NaturePondBuild repeat = buildNaturePond(kSeed);
    require(decorateNaturePondSunroofProbe(decorated) &&
                decorateNaturePondSunroofProbe(repeat),
            "The sunroof decorator should accept a complete nature-pond build");
    require(!decorateNaturePondSunroofProbe(decorated),
            "The sunroof decorator should reject a duplicate application");
    require(baseline.volumes.size() == 6u && decorated.volumes.size() == 7u &&
                repeat.volumes.size() == decorated.volumes.size(),
            "The probe should append exactly one scene-owned island/pavilion volume");

    for (size_t index = 0; index < decorated.volumes.size(); ++index)
    {
        const NaturePondVolumeData& first = decorated.volumes[index];
        const NaturePondVolumeData& second = repeat.volumes[index];
        require(first.name == second.name && first.dims == second.dims &&
                    first.position == second.position &&
                    first.scale == second.scale && first.role == second.role &&
                    first.voxels == second.voxels,
                "The complete pond sunroof composition should be byte-deterministic");
    }

    const NaturePondSunroofProbeLayout& layout =
        naturePondSunroofProbeLayout();
    require(layout.centerXZ == glm::vec2(0.0f) &&
                layout.islandSurfaceHeight >
                    baseline.layout.pondSurfaceHeight + 0.45f &&
                layout.pavilionInnerRadius > 3.7f &&
                layout.pavilionOuterRadius > 5.6f &&
                layout.pavilionOuterRadius > layout.pavilionInnerRadius &&
                nearlyEqual(layout.pavilionColumnRadius, 5.05f) &&
                nearlyEqual(layout.pavilionColumnShaftRadius, 0.24f) &&
                nearlyEqual(layout.pavilionColumnBottom, 0.68f) &&
                nearlyEqual(layout.pavilionColumnTop, 8.05f) &&
                layout.pavilionRoofBottom > 7.8f &&
                layout.pavilionRoofTop > layout.pavilionRoofBottom &&
                layout.columnCount == 12u &&
                decorated.layout.heroTreeCenterXZ == layout.centerXZ,
            "The probe layout should expose a raised island and canopy-clearing circular oculus");

    const NaturePondVolumeData& trunk = decorated.volumes[3];
    const NaturePondVolumeData& canopy = decorated.volumes[4];
    const NaturePondVolumeData& proxy = decorated.volumes[5];
    const glm::vec3 trunkCenter =
        trunk.position + glm::vec3(trunk.dims) * trunk.scale * 0.5f;
    const glm::vec3 canopyCenter =
        canopy.position + glm::vec3(canopy.dims) * canopy.scale * 0.5f;
    const glm::vec3 proxyCenter =
        proxy.position + glm::vec3(proxy.dims) * proxy.scale * 0.5f;
    require(nearlyEqual(trunkCenter.x, layout.centerXZ.x) &&
                nearlyEqual(trunkCenter.z, layout.centerXZ.y) &&
                nearlyEqual(trunk.position.y, layout.islandSurfaceHeight) &&
                nearlyEqual(canopyCenter.x, layout.centerXZ.x) &&
                nearlyEqual(canopyCenter.z, layout.centerXZ.y) &&
                nearlyEqual(proxyCenter.x, layout.centerXZ.x) &&
                nearlyEqual(proxyCenter.z, layout.centerXZ.y),
            "The trunk, canopy, and lighting proxy should move together onto the island");
    require(trunk.dims == baseline.volumes[3].dims &&
                trunk.scale == baseline.volumes[3].scale &&
                canopy.dims == baseline.volumes[4].dims &&
                canopy.scale == baseline.volumes[4].scale &&
                proxy.dims == baseline.volumes[5].dims &&
                proxy.scale == baseline.volumes[5].scale,
            "Tree Fidelity must sculpt the existing hero volumes without changing their allocation bounds");

    uint64_t addedBoughVoxels = 0u;
    for (size_t index = 0; index < trunk.voxels.size(); ++index)
    {
        const uint8_t authored = baseline.volumes[3].voxels[index];
        const uint8_t sculpted = trunk.voxels[index];
        require(authored == 0u || authored == sculpted,
                "The sunroof bough scaffold must preserve every base trunk voxel");
        if (authored == 0u && sculpted != 0u)
        {
            require(sculpted >= NaturePondMaterial::Wood::Base &&
                        sculpted < NaturePondMaterial::Wood::Base +
                                       NaturePondMaterial::Wood::Count,
                    "New radial bough cells must remain in the wood material band");
            ++addedBoughVoxels;
        }
    }

    uint64_t carvedCanopyVoxels = 0u;
    uint64_t lavenderCanopyVoxels = 0u;
    uint64_t retainedLeafVoxels = 0u;
    uint64_t upperBlossomVoxels = 0u;
    uint64_t undersideBlossomVoxels = 0u;
    uint64_t undersideLeafVoxels = 0u;
    std::array<uint64_t, 2> lavenderShadeCounts{};
    constexpr std::array<glm::ivec3, 6> kCanopyNeighbors{
        glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0),
        glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0),
        glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1),
    };
    uint64_t exposedBlossomVoxels = 0u;
    uint64_t clusteredBlossomVoxels = 0u;
    const auto canopyMaterialAt = [&canopy](int x, int y, int z) {
        if (x < 0 || y < 0 || z < 0 || x >= canopy.dims.x ||
            y >= canopy.dims.y || z >= canopy.dims.z)
        {
            return uint8_t{0};
        }
        return canopy.voxels[static_cast<size_t>(x) +
                             static_cast<size_t>(canopy.dims.x) *
                                 (static_cast<size_t>(y) +
                                  static_cast<size_t>(canopy.dims.y) *
                                      static_cast<size_t>(z))];
    };
    for (int z = 0; z < canopy.dims.z; ++z)
    {
        for (int y = 0; y < canopy.dims.y; ++y)
        {
            for (int x = 0; x < canopy.dims.x; ++x)
            {
                const size_t index =
                    static_cast<size_t>(x) +
                    static_cast<size_t>(canopy.dims.x) *
                        (static_cast<size_t>(y) +
                         static_cast<size_t>(canopy.dims.y) *
                             static_cast<size_t>(z));
                const uint8_t authored = baseline.volumes[4].voxels[index];
                const uint8_t sculpted = canopy.voxels[index];
                require(sculpted == 0u || authored != 0u,
                        "Tree Fidelity may carve or recolor the base canopy but must not inflate its voxel envelope");
                carvedCanopyVoxels += authored != 0u && sculpted == 0u ? 1u : 0u;
                const bool blossom =
                    sculpted >= NaturePondMaterial::Flower::Base + 4u &&
                    sculpted <= NaturePondMaterial::Flower::Base + 5u;
                const bool leaf =
                    sculpted >= NaturePondMaterial::Foliage::Base &&
                    sculpted < NaturePondMaterial::Foliage::Base +
                                   NaturePondMaterial::Foliage::Count;
                require(sculpted == 0u || blossom || leaf,
                        "The sculpted crown must contain only retained leaves and clustered lavender blossoms");
                lavenderCanopyVoxels += blossom ? 1u : 0u;
                retainedLeafVoxels += leaf ? 1u : 0u;
                if (blossom)
                {
                    ++lavenderShadeCounts[sculpted -
                                          (NaturePondMaterial::Flower::Base + 4u)];
                }
                const float localY =
                    (static_cast<float>(y) + 0.5f) * canopy.scale.y -
                    glm::vec3(canopy.dims).y * canopy.scale.y * 0.5f;
                upperBlossomVoxels += blossom && localY >= -0.28f ? 1u : 0u;
                undersideBlossomVoxels += blossom && localY < -0.28f ? 1u : 0u;
                undersideLeafVoxels += leaf && localY < -0.28f ? 1u : 0u;
                if (!blossom)
                {
                    continue;
                }
                bool exposed = false;
                bool clustered = false;
                for (const glm::ivec3& neighbor : kCanopyNeighbors)
                {
                    const uint8_t adjacent = canopyMaterialAt(
                        x + neighbor.x, y + neighbor.y, z + neighbor.z);
                    exposed = exposed || adjacent == 0u;
                    clustered = clustered ||
                        (adjacent >= NaturePondMaterial::Flower::Base + 4u &&
                         adjacent <= NaturePondMaterial::Flower::Base + 5u);
                }
                exposedBlossomVoxels += exposed ? 1u : 0u;
                clusteredBlossomVoxels += clustered ? 1u : 0u;
            }
        }
    }

    uint64_t carvedProxyVoxels = 0u;
    for (size_t index = 0; index < proxy.voxels.size(); ++index)
    {
        const uint8_t authored = baseline.volumes[5].voxels[index];
        const uint8_t sculpted = proxy.voxels[index];
        require(sculpted == 0u || sculpted == authored,
                "The lobe-aware AO proxy may only remove stale occlusion cells");
        carvedProxyVoxels += authored != 0u && sculpted == 0u ? 1u : 0u;
    }
    require(addedBoughVoxels == 440u &&
                carvedCanopyVoxels == 6411u &&
                carvedProxyVoxels == 14u &&
                decorated.heroTrunkOccupiedVoxels ==
                    baseline.heroTrunkOccupiedVoxels + addedBoughVoxels &&
                decorated.heroCanopyOccupiedVoxels ==
                    retainedLeafVoxels + lavenderCanopyVoxels &&
                decorated.heroCanopyOccupiedVoxels + carvedCanopyVoxels ==
                    baseline.heroCanopyOccupiedVoxels &&
                decorated.heroCanopyProxyOccupiedVoxels + carvedProxyVoxels ==
                    baseline.heroCanopyProxyOccupiedVoxels,
            "The sunroof tree must expose bounded branch windows while keeping all occupancy counters authoritative (added/carved/proxy=" +
                std::to_string(addedBoughVoxels) + "/" +
                std::to_string(carvedCanopyVoxels) + "/" +
                std::to_string(carvedProxyVoxels) + ")");
    require(lavenderCanopyVoxels >
                    decorated.heroCanopyOccupiedVoxels / 12u &&
                retainedLeafVoxels > lavenderCanopyVoxels &&
                lavenderShadeCounts[0] > 0u && lavenderShadeCounts[1] > 0u &&
                exposedBlossomVoxels == lavenderCanopyVoxels &&
                clusteredBlossomVoxels * 5u > lavenderCanopyVoxels * 4u &&
                upperBlossomVoxels > undersideBlossomVoxels &&
                undersideLeafVoxels > undersideBlossomVoxels &&
                decorated.heroCanopyFoliageInstances.size() == 63u,
            "The centered crown should retain a dark leafy underside and coherent exposed lavender clusters (leaf/blossom/exposed/clustered=" +
                std::to_string(retainedLeafVoxels) + "/" +
                std::to_string(lavenderCanopyVoxels) + "/" +
                std::to_string(exposedBlossomVoxels) + "/" +
                std::to_string(clusteredBlossomVoxels) + ")");

    const glm::vec3 treeTranslation =
        trunk.position - baseline.volumes[3].position;
    std::array<uint32_t, 2> floweringPatchShades{};
    for (size_t sprigIndex = 0;
         sprigIndex < decorated.heroCanopyFoliageInstances.size(); ++sprigIndex)
    {
        const engine::game::FoliageBladeInstance& sprig =
            decorated.heroCanopyFoliageInstances[sprigIndex];
        const engine::game::FoliageBladeInstance& authored =
            baseline.heroCanopyFoliageInstances[sprigIndex];
        const bool purpleTip =
            sprig.tipMaterialId >= NaturePondMaterial::Flower::Base + 4u &&
            sprig.tipMaterialId <= NaturePondMaterial::Flower::Base + 5u;
        bool attachedToCanopy = false;
        const glm::ivec3 rootCell = glm::ivec3(glm::floor(
            (sprig.rootWorld - canopy.position) / canopy.scale));
        for (int z = rootCell.z - 2; z <= rootCell.z + 2; ++z)
        {
            for (int y = rootCell.y - 2; y <= rootCell.y + 2; ++y)
            {
                for (int x = rootCell.x - 2; x <= rootCell.x + 2; ++x)
                {
                    if (canopyMaterialAt(x, y, z) == 0u)
                    {
                        continue;
                    }
                    const glm::vec3 occupiedCenter =
                        canopy.position +
                        (glm::vec3(x, y, z) + glm::vec3(0.5f)) *
                            canopy.scale;
                    attachedToCanopy = attachedToCanopy ||
                        glm::distance(sprig.rootWorld, occupiedCenter) <=
                            glm::length(canopy.scale) * 1.05f;
                }
            }
        }
        require(sprig.stableId == authored.stableId &&
                    sprig.stablePatchId == authored.stablePatchId &&
                    glm::length(sprig.patchRootWorld -
                                (authored.patchRootWorld +
                                 treeTranslation)) < 1e-5f &&
                    sprig.heightMeters == authored.heightMeters &&
                    sprig.cellSizeMeters == authored.cellSizeMeters &&
                    sprig.patchRadiusMeters == authored.patchRadiusMeters &&
                    sprig.yawRadians == authored.yawRadians &&
                    sprig.phaseRadians == authored.phaseRadians &&
                    sprig.randomSeed == authored.randomSeed &&
                    sprig.patchSeed == authored.patchSeed &&
                    sprig.stemMaterialId == authored.stemMaterialId &&
                    sprig.secondaryMaterialId ==
                        authored.secondaryMaterialId &&
                    sprig.flags == authored.flags &&
                    sprig.patchKind == authored.patchKind &&
                    sprig.morphology == authored.morphology && purpleTip,
                "Tree Fidelity should preserve green patch identity and change only root placement plus the flowering tip (sprig=" +
                    std::to_string(sprigIndex) + ")");
        require(attachedToCanopy,
                "Every re-snapped flowering sprig must remain attached to an occupied crown cell (sprig=" +
                    std::to_string(sprigIndex) + ")");
        floweringPatchShades[sprig.tipMaterialId -
                             (NaturePondMaterial::Flower::Base + 4u)] = 1u;
    }
    require(floweringPatchShades[0] == 1u &&
                floweringPatchShades[1] == 1u,
            "The seven flowering crown patches should retain both violet shades");

    std::vector<engine::game::FoliageBladeInstance> renderFoliage =
        decorated.foliageInstances;
    renderFoliage.insert(renderFoliage.end(),
                         decorated.heroCanopyFoliageInstances.begin(),
                         decorated.heroCanopyFoliageInstances.end());
    const engine::render::FoliageVoxelGeometry meadowGeometry =
        engine::render::buildFoliageVoxelGeometry(
            decorated.foliageInstances);
    const engine::render::FoliageVoxelGeometry canopyGeometry =
        engine::render::buildFoliageVoxelGeometry(
            decorated.heroCanopyFoliageInstances);
    const engine::render::FoliageVoxelGeometry renderGeometry =
        engine::render::buildFoliageVoxelGeometry(renderFoliage);
    uint64_t canopyLeafPrimitives = 0u;
    uint64_t canopyFlowerPrimitives = 0u;
    uint64_t anchoredCanopyLeaves = 0u;
    uint64_t movingCanopyLeaves = 0u;
    uint64_t movingCanopyFlowers = 0u;
    for (const engine::render::FoliageGpuPrimitive& primitive :
         canopyGeometry.primitives)
    {
        const auto role = static_cast<
            engine::render::FoliageVoxelPrimitiveRole>(
                primitive.materialSeedFlags.w);
        const bool leaf =
            role == engine::render::FoliageVoxelPrimitiveRole::Leaf;
        const bool flower =
            role == engine::render::FoliageVoxelPrimitiveRole::Flower;
        canopyLeafPrimitives += leaf ? 1u : 0u;
        canopyFlowerPrimitives += flower ? 1u : 0u;
        anchoredCanopyLeaves +=
            leaf && primitive.centerMotionT.w == 0.0f ? 1u : 0u;
        movingCanopyLeaves +=
            leaf && primitive.centerMotionT.w > 0.0f ? 1u : 0u;
        movingCanopyFlowers +=
            flower && primitive.centerMotionT.w > 0.0f ? 1u : 0u;
        require((leaf &&
                 primitive.materialSeedFlags.x >=
                     NaturePondMaterial::Foliage::Base &&
                 primitive.materialSeedFlags.x <
                     NaturePondMaterial::Foliage::Base +
                         NaturePondMaterial::Foliage::Count) ||
                    (flower &&
                     primitive.materialSeedFlags.x >=
                         NaturePondMaterial::Flower::Base + 4u &&
                     primitive.materialSeedFlags.x <=
                         NaturePondMaterial::Flower::Base + 5u),
                "Flowering crown primitives must keep green leaves and purple Flower-role tips distinct");
    }
    require(!meadowGeometry.primitives.empty() &&
                canopyGeometry.semanticInstanceCount == 63u &&
                canopyGeometry.patchCount == 7u &&
                canopyGeometry.primitives.size() == 315u &&
                canopyLeafPrimitives == 189u &&
                canopyFlowerPrimitives == 126u &&
                anchoredCanopyLeaves == 63u && movingCanopyLeaves == 126u &&
                movingCanopyFlowers == 126u &&
                renderGeometry.primitives.size() ==
                    meadowGeometry.primitives.size() + 315u &&
                renderGeometry.semanticInstanceCount == renderFoliage.size(),
            "The decorated meadow and flowering canopy should remain one valid instanced-foliage upload payload (meadow/canopy/combined primitives=" +
                std::to_string(meadowGeometry.primitives.size()) + "/" +
                std::to_string(canopyGeometry.primitives.size()) + "/" +
                std::to_string(renderGeometry.primitives.size()) + ")");

    const NaturePondVolumeData& pavilion = decorated.volumes.back();
    require(pavilion.name == "NaturePondSunroofIslandPavilion" &&
                pavilion.role ==
                    NaturePondVolumeRole::SunroofArchitectureOpaque &&
                pavilion.dims == glm::ivec3(60, 43, 60) &&
                pavilion.scale == glm::vec3(0.20f) &&
                decorated.sunroofArchitectureOccupiedVoxels > 4000u &&
                decorated.sunroofArchitectureOccupiedVoxels < 18000u,
            "The island/pavilion should remain a bounded coarse performance-probe volume");

    uint64_t architectureVoxels = 0u;
    uint64_t islandVoxels = 0u;
    for (uint8_t material : pavilion.voxels)
    {
        const bool architecture =
            material >= NaturePondMaterial::Architecture::Base &&
            material < NaturePondMaterial::Architecture::Base +
                           NaturePondMaterial::Architecture::Count;
        const bool island =
            (material >= NaturePondMaterial::Soil::Base &&
             material < NaturePondMaterial::Soil::Base +
                            NaturePondMaterial::Soil::Count) ||
            (material >= NaturePondMaterial::Meadow::Base &&
             material < NaturePondMaterial::Meadow::Base +
                            NaturePondMaterial::Meadow::Count) ||
            (material >= NaturePondMaterial::Gravel::Base &&
             material < NaturePondMaterial::Gravel::Base +
                            NaturePondMaterial::Gravel::Count);
        require(material == 0u || architecture || island,
                "The probe volume should use only island substrate and pavilion material bands");
        architectureVoxels += architecture ? 1u : 0u;
        islandVoxels += island ? 1u : 0u;
    }
    require(architectureVoxels > 2500u && islandVoxels > 500u,
            "The probe volume should contain both a readable pavilion and raised land mass");

    const auto materialAt = [&pavilion](const glm::vec3& world) {
        const glm::ivec3 cell = glm::ivec3(glm::floor(
            (world - pavilion.position) / pavilion.scale));
        if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
            cell.x >= pavilion.dims.x || cell.y >= pavilion.dims.y ||
            cell.z >= pavilion.dims.z)
        {
            return uint8_t{0};
        }
        const size_t index =
            static_cast<size_t>(cell.x) +
            static_cast<size_t>(pavilion.dims.x) *
                (static_cast<size_t>(cell.y) +
                 static_cast<size_t>(pavilion.dims.y) *
                     static_cast<size_t>(cell.z));
        return pavilion.voxels[index];
    };
    const float roofY =
        (layout.pavilionRoofBottom + layout.pavilionRoofTop) * 0.5f;
    const uint8_t islandTop = materialAt(glm::vec3(
        layout.centerXZ.x, layout.islandSurfaceHeight - 0.05f,
        layout.centerXZ.y));
    const uint8_t apertureCenter = materialAt(
        glm::vec3(layout.centerXZ.x, roofY, layout.centerXZ.y));
    const uint8_t roofRing = materialAt(glm::vec3(
        layout.centerXZ.x +
            (layout.pavilionInnerRadius + layout.pavilionOuterRadius) * 0.5f,
        roofY, layout.centerXZ.y));
    const uint8_t column = materialAt(glm::vec3(
        layout.centerXZ.x + 5.05f, 4.0f, layout.centerXZ.y));
    require(islandTop >= NaturePondMaterial::Meadow::Base &&
                islandTop < NaturePondMaterial::Meadow::Base +
                                NaturePondMaterial::Meadow::Count &&
                apertureCenter == 0u &&
                roofRing >= NaturePondMaterial::Architecture::Base &&
                column >= NaturePondMaterial::Architecture::Base,
            "The composition should expose grassy land, a genuinely open oculus, roof annulus, and columns");

    for (const engine::game::FoliageBladeInstance& instance :
         decorated.foliageInstances)
    {
        const glm::vec2 normalized(
            (instance.rootWorld.x - layout.centerXZ.x) /
                layout.islandRadii.x,
            (instance.rootWorld.z - layout.centerXZ.y) /
                layout.islandRadii.y);
        require(glm::length(normalized) >= 1.18f,
                "Legacy pond ecology should leave the island and tree roots clear");
    }

    SceneConfig glassScene{};
    glassScene.name = std::string(NaturePondSunroof::SceneName);
    glassScene.enableGlass = true;
    const NaturePondSunroofGlassLayout& glassLayout =
        naturePondSunroofGlassLayout();
    const std::vector<glm::mat4> glassPanes =
        naturePondSunroofGlassPaneModels(glassScene);
    const std::vector<glm::mat4> repeatedGlassPanes =
        naturePondSunroofGlassPaneModels(glassScene);
    require(glassLayout.entranceBayIndex == 1u &&
                glassPanes.size() == layout.columnCount - 1u &&
                repeatedGlassPanes.size() == glassPanes.size() &&
                nearlyEqual(glassLayout.paneWidth, 1.90f) &&
                nearlyEqual(glassLayout.paneBottom, 3.42f) &&
                nearlyEqual(glassLayout.paneTop, 7.34f) &&
                nearlyEqual(glassLayout.paneThickness, 0.10f) &&
                glassLayout.paneBottom > baseline.layout.pondSurfaceHeight + 0.20f &&
                glassLayout.paneTop < layout.pavilionColumnTop - 0.60f,
            "The pavilion glazing should connect eleven dry, capital-safe bays while leaving one entrance open");

    size_t paneIndex = 0u;
    constexpr float kTau = 6.28318530717958647692f;
    const float halfBayAngle =
        kTau / (2.0f * static_cast<float>(layout.columnCount));
    const float expectedPaneRadius =
        layout.pavilionColumnRadius * std::cos(halfBayAngle);
    const float adjacentColumnChord =
        2.0f * layout.pavilionColumnRadius * std::sin(halfBayAngle);
    for (uint32_t bay = 0u; bay < layout.columnCount; ++bay)
    {
        if (bay == glassLayout.entranceBayIndex)
        {
            continue;
        }
        const glm::mat4& model = glassPanes[paneIndex];
        const glm::mat4& repeatedModel = repeatedGlassPanes[paneIndex];
        for (glm::mat4::length_type columnIndex = 0; columnIndex < 4;
             ++columnIndex)
        {
            require(glm::length(model[columnIndex] -
                                repeatedModel[columnIndex]) < 1e-6f,
                    "Pavilion glass transforms should be deterministic");
        }

        const float midAngle =
            kTau * (static_cast<float>(bay) + 0.5f) /
            static_cast<float>(layout.columnCount);
        const glm::vec3 expectedOutward(std::cos(midAngle), 0.0f,
                                        std::sin(midAngle));
        const glm::vec3 expectedTangent(-std::sin(midAngle), 0.0f,
                                        std::cos(midAngle));
        const glm::vec3 center(model[3]);
        const glm::vec3 paneWidthAxis(model[0]);
        const glm::vec3 paneHeightAxis(model[1]);
        const glm::vec3 paneThicknessAxis(model[2]);
        require(nearlyEqual(center.x,
                            layout.centerXZ.x +
                                expectedOutward.x * expectedPaneRadius,
                            1e-5f) &&
                    nearlyEqual(center.y,
                                (glassLayout.paneBottom +
                                 glassLayout.paneTop) * 0.5f) &&
                    nearlyEqual(center.z,
                                layout.centerXZ.y +
                                    expectedOutward.z * expectedPaneRadius,
                                1e-5f) &&
                    nearlyEqual(glm::length(paneWidthAxis),
                                glassLayout.paneWidth) &&
                    nearlyEqual(glm::length(paneHeightAxis),
                                glassLayout.paneTop - glassLayout.paneBottom) &&
                    nearlyEqual(glm::length(paneThicknessAxis),
                                glassLayout.paneThickness) &&
                    glm::dot(glm::normalize(paneWidthAxis), -expectedTangent) >
                        0.9999f &&
                    glm::dot(glm::normalize(paneThicknessAxis), expectedOutward) >
                        0.9999f &&
                    glm::dot(glm::cross(glm::normalize(paneWidthAxis),
                                        glm::normalize(paneHeightAxis)),
                             glm::normalize(paneThicknessAxis)) > 0.9999f,
                "Every glass pane should be centered, tangent between its adjacent circular columns, and right-handed for GlassPass culling");
        ++paneIndex;
    }
    require((adjacentColumnChord - glassLayout.paneWidth) * 0.5f >
                    layout.pavilionColumnShaftRadius + 0.09f &&
                paneIndex == glassPanes.size(),
            "Glass panes should retain a visible column reveal without leaving unintended glazed bays");

    glassScene.enableGlass = false;
    require(naturePondSunroofGlassPaneModels(glassScene).empty(),
            "The content glass toggle should disable all pavilion panes");
    glassScene.enableGlass = true;
    glassScene.name = "nature_pond_probe";
    require(naturePondSunroofGlassPaneModels(glassScene).empty(),
            "The original nature pond dome must not acquire pavilion panes");

    const engine::game::FishHabitat& fishHabitat =
        naturePondSunroofProbeFishHabitat();
    require(fishHabitat.isValid() && fishHabitat.boundsMax.x < -2.0f &&
                fishHabitat.schoolCenter.x < fishHabitat.boundsMax.x,
            "The probe fish habitat should stay in a valid side-water lane away from the island");
}

void testNaturePondGlassDomeContract()
{
    SceneConfig config{};
    config.name = "nature_pond_probe";
    config.enableGlass = true;
    const auto placement = naturePondGlassDomePlacement(config);
    require(placement.has_value(),
            "The authored nature pond should opt into its inverted fishbowl dome");
    require(placement->shellSpec.volumeDims == glm::ivec3(21, 13, 21) &&
                placement->shellSpec.radialSegments == 96 &&
                placement->shellSpec.verticalBands == 24 &&
                placement->shellSpec.includeBase &&
                placement->shellSpec.includeRim &&
                placement->shellSpec.includeBaseFoot &&
                placement->shellSpec.profileShape ==
                    TankGlassBuilder::FishbowlProfileShape::RoundedDome,
            "The dome should retain its stable high-resolution fishbowl shell contract");

    const NaturePondLayout& pond = naturePondLayout();
    require(placement->center.x - placement->openingRadii.x <= pond.pondBoundsMin.x &&
                placement->center.x + placement->openingRadii.x >= pond.pondBoundsMax.x &&
                placement->center.z - placement->openingRadii.y <= pond.pondBoundsMin.z &&
                placement->center.z + placement->openingRadii.y >= pond.pondBoundsMax.z &&
                placement->rimHeight > pond.pondSurfaceHeight &&
                placement->apexHeight - placement->rimHeight < 5.25f &&
                placement->openingRadii.y / placement->openingRadii.x > 0.85f,
            "The compact, gently oval dome should closely cover the pond and sit above water");
    require(std::abs(pond.heroTreeCenterXZ.x - placement->center.x) -
                    placement->openingRadii.x > 2.0f,
            "The hero tree center should retain a deliberate gap beyond the pond rim");

    const std::vector<Vertex> vertices =
        TankGlassBuilder::buildFishbowlGlassShellVertices(placement->shellSpec);
    require(vertices.size() == 29952,
            "The dome shell should emit deterministic outer, inner, rim, and foot triangles");
    glm::vec3 rawProfileRadii(0.0f);
    for (const Vertex& vertex : vertices)
    {
        const float radius = glm::length(
            glm::vec2(vertex.pos.x - placement->center.x,
                      vertex.pos.z - placement->center.z));
        if (nearlyEqual(vertex.pos.y, 3.0f, 1e-4f))
            rawProfileRadii.x = std::max(rawProfileRadii.x, radius);
        if (nearlyEqual(vertex.pos.y, 6.0f, 1e-4f))
            rawProfileRadii.y = std::max(rawProfileRadii.y, radius);
        if (nearlyEqual(vertex.pos.y, 9.0f, 1e-4f))
            rawProfileRadii.z = std::max(rawProfileRadii.z, radius);
    }
    require(rawProfileRadii.x < 1.5f &&
                rawProfileRadii.y >
                    (rawProfileRadii.x + rawProfileRadii.z) * 0.5f + 1.0f,
            "The upper bowl must bow outward like a rounded dome rather than a cone");
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    glm::vec2 rimExtents(0.0f);
    for (const Vertex& vertex : vertices)
    {
        const glm::vec3 world =
            glm::vec3(placement->model * glm::vec4(vertex.pos, 1.0f));
        minY = std::min(minY, world.y);
        maxY = std::max(maxY, world.y);
        if (nearlyEqual(world.y, placement->rimHeight, 1e-4f))
        {
            rimExtents.x =
                std::max(rimExtents.x, std::abs(world.x - placement->center.x));
            rimExtents.y =
                std::max(rimExtents.y, std::abs(world.z - placement->center.z));
        }
    }
    require(nearlyEqual(minY, placement->rimHeight, 1e-4f) &&
                nearlyEqual(maxY, placement->apexHeight, 1e-4f) &&
                nearlyEqual(rimExtents.x, placement->openingRadii.x, 1e-4f) &&
                nearlyEqual(rimExtents.y, placement->openingRadii.y, 1e-4f),
            "The transform should invert and compress the fishbowl into its oval footprint");

    config.enableGlass = false;
    require(!naturePondGlassDomePlacement(config).has_value(),
            "The scene content glass toggle should disable the dome");
    config.enableGlass = true;
    config.name = "nature_pond_20cm_probe";
    require(!naturePondGlassDomePlacement(config).has_value(),
            "Detail-tier comparison probes should remain dome-free controls");
}

void testLightingParityProbeContract()
{
    SceneConfig lit{};
    lit.name = std::string(NaturePondLightingParity::LitSceneName);
    lit.enablePointLights = true;
    engine::render::LightingSettings lighting{};
    lighting.areaLights_.resize(3);

    require(engine::scene::applyLightingParityProbeAreaLights(lit, lighting),
            "The lit parity scene should resolve its authored local-light preset");
    const auto& authored = engine::scene::lightingParityProbeAreaLights();
    require(lighting.areaLights_.size() == 1 && lighting.lightCount_ == 1 &&
                lighting.areaLights_[0].shape == LightShape::Sphere &&
                lighting.areaLights_[0].castsShadows &&
                lighting.areaLights_[0].position ==
                    NaturePondLightingParity::FireLightPosition &&
                lighting.areaLights_[0].color.r >
                    lighting.areaLights_[0].color.g * 3.0f &&
                lighting.areaLights_[0].intensity >= 8.0f &&
                authored.size() == 1 &&
                authored[0].position == lighting.areaLights_[0].position,
            "The parity preset should expose one stable, warm, shadowed fire light");

    SceneConfig unlit = lit;
    unlit.name = std::string(NaturePondLightingParity::UnlitSceneName);
    unlit.enablePointLights = false;
    require(engine::scene::applyLightingParityProbeAreaLights(unlit, lighting) &&
                lighting.areaLights_.size() == 1,
            "The unlit control should retain identical authored light data while its scene toggle disables evaluation");

    SceneConfig unrelated = lit;
    unrelated.name = "nature_pond_probe";
    const engine::render::LightingSettings before = lighting;
    require(!engine::scene::applyLightingParityProbeAreaLights(unrelated, lighting) &&
                lighting.areaLights_.size() == before.areaLights_.size() &&
                lighting.lightCount_ == before.lightCount_,
            "Unrelated scenes should remain untouched by the parity light resolver");
}

void testVoxelLightingOcclusionPolicyContract()
{
    using Mode = engine::VoxelVolume::LightingOcclusionMode;
    using engine::render::voxelLightingOcclusionParticipation;
    using engine::render::voxelVolumeQualifiesForOpaqueOnlyDda;
    using engine::render::voxelVolumeQualifiesForUnwrappedOpaqueDda;

    const auto staticOpaque =
        voxelLightingOcclusionParticipation(Mode::BinaryOpaque, true);
    require(staticOpaque.mainDda && staticOpaque.planarReflection &&
                staticOpaque.sunShadow && staticOpaque.localLighting,
            "Static opaque voxels should participate in every established path");

    const auto staticNone = voxelLightingOcclusionParticipation(Mode::None, true);
    require(staticNone.mainDda && staticNone.planarReflection &&
                !staticNone.sunShadow && !staticNone.localLighting,
            "Non-occluding visible voxels should still render and reflect");

    const auto dynamicCanopy =
        voxelLightingOcclusionParticipation(Mode::TranslucentFoliage, false);
    require(dynamicCanopy.mainDda && !dynamicCanopy.planarReflection &&
                dynamicCanopy.sunShadow && !dynamicCanopy.localLighting,
            "Dynamic canopy voxels should retain plant-only sun transmittance");

    const auto staticCanopy =
        voxelLightingOcclusionParticipation(Mode::TranslucentFoliage, true);
    require(staticCanopy.mainDda && staticCanopy.planarReflection &&
                staticCanopy.sunShadow && !staticCanopy.localLighting,
            "Real canopy voxels should not double-count the coarse AO/local proxy");

    const auto staticMeadow =
        voxelLightingOcclusionParticipation(Mode::TranslucentMeadow, true);
    require(staticMeadow.mainDda && staticMeadow.planarReflection &&
                staticMeadow.sunShadow && !staticMeadow.localLighting &&
                nearlyEqual(engine::render::voxelSunShadowFoliageOpacityScale(
                                Mode::TranslucentMeadow),
                            0.45f) &&
                nearlyEqual(engine::render::voxelSunShadowFoliageOpacityScale(
                                Mode::TranslucentFoliage),
                            1.0f),
            "Meadow fallback voxels should retain their visible fallback contract while casting lighter translucent sun shadows");

    const auto staticProxy = voxelLightingOcclusionParticipation(Mode::Proxy, true);
    require(!staticProxy.mainDda && !staticProxy.planarReflection &&
                !staticProxy.sunShadow && staticProxy.localLighting,
            "Static proxies should be invisible AO/local-only occluders");

    const auto dynamicProxy = voxelLightingOcclusionParticipation(Mode::Proxy, false);
    require(!dynamicProxy.mainDda && !dynamicProxy.planarReflection &&
                !dynamicProxy.sunShadow && !dynamicProxy.localLighting,
            "Untracked dynamic proxies should fail closed instead of leaving stale occlusion");

    require(voxelVolumeQualifiesForOpaqueOnlyDda(0u) &&
                voxelVolumeQualifiesForOpaqueOnlyDda(engine::VoxelVolume::FLAG_STATIC),
            "Volumes without transmissive declarations should use the opaque-only DDA");
    require(!voxelVolumeQualifiesForOpaqueOnlyDda(engine::VoxelVolume::FLAG_GLASS) &&
                !voxelVolumeQualifiesForOpaqueOnlyDda(engine::VoxelVolume::FLAG_WATER) &&
                !voxelVolumeQualifiesForOpaqueOnlyDda(
                    engine::VoxelVolume::FLAG_GLASS |
                    engine::VoxelVolume::FLAG_WATER),
            "Every declared glass or water volume must retain transmissive DDA traversal");
    require(voxelVolumeQualifiesForUnwrappedOpaqueDda(0u) &&
                voxelVolumeQualifiesForUnwrappedOpaqueDda(
                    engine::VoxelVolume::FLAG_STATIC |
                    engine::VoxelVolume::FLAG_ALLOW_DENSE_SKIP),
            "Ordinary opaque volumes should use the unwrapped opaque DDA");
    require(!voxelVolumeQualifiesForUnwrappedOpaqueDda(
                engine::VoxelVolume::FLAG_GLASS) &&
                !voxelVolumeQualifiesForUnwrappedOpaqueDda(
                    engine::VoxelVolume::FLAG_WATER) &&
                !voxelVolumeQualifiesForUnwrappedOpaqueDda(
                    engine::VoxelVolume::FLAG_WRAP_XZ) &&
                !voxelVolumeQualifiesForUnwrappedOpaqueDda(
                    engine::VoxelVolume::FLAG_CLOUD),
            "Transmissive, wrapped, and cloud declarations must fail closed");
}

void testVoxelRasterFaceSelectionContract()
{
    using engine::render::classifyVoxelRasterFaces;

    const glm::mat4 identity(1.0f);
    const glm::vec3 boundsMin(0.0f);
    const glm::vec3 boundsMax(10.0f);
    constexpr float nearClipGuardRadius = 0.2f;

    const auto outside = classifyVoxelRasterFaces(
        identity, identity, boundsMin, boundsMax, glm::vec3(15.0f, 5.0f, 5.0f),
        nearClipGuardRadius);
    require(!outside.cameraInside && outside.safe &&
                !outside.fullScreenCoverageRequired &&
                !outside.windingReversed,
            "An ordinary outside camera should retain front faces");

    const auto inside = classifyVoxelRasterFaces(
        identity, identity, boundsMin, boundsMax, glm::vec3(5.0f),
        nearClipGuardRadius);
    require(inside.cameraInside && !inside.safe &&
                inside.fullScreenCoverageRequired && !inside.windingReversed,
            "A camera inside sparse raster bounds should require full-screen coverage");

    const auto boundary = classifyVoxelRasterFaces(
        identity, identity, boundsMin, boundsMax, glm::vec3(0.0f, 5.0f, 5.0f),
        nearClipGuardRadius);
    require(!boundary.safe && boundary.fullScreenCoverageRequired,
            "A camera on a raster boundary should require conservative coverage");

    const glm::mat4 translatedWorld =
        glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, -2.0f, 4.0f));
    const glm::mat4 translatedLocal = glm::inverse(translatedWorld);
    const auto translatedInside = classifyVoxelRasterFaces(
        translatedLocal, translatedWorld, boundsMin, boundsMax,
        glm::vec3(15.0f, 3.0f, 9.0f), nearClipGuardRadius);
    require(translatedInside.cameraInside && !translatedInside.safe &&
                translatedInside.fullScreenCoverageRequired,
            "Local-space inside classification should require full-screen coverage");

    const glm::mat4 mirroredWorld =
        glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f));
    const auto mirroredInside = classifyVoxelRasterFaces(
        glm::inverse(mirroredWorld), mirroredWorld, boundsMin, boundsMax,
        glm::vec3(-5.0f, 5.0f, 5.0f), nearClipGuardRadius);
    require(mirroredInside.cameraInside && !mirroredInside.safe &&
                mirroredInside.fullScreenCoverageRequired &&
                mirroredInside.windingReversed,
            "Mirrored inside transforms should preserve handedness and full coverage");

    const auto invalidBounds = classifyVoxelRasterFaces(
        identity, identity, boundsMax, boundsMin, glm::vec3(5.0f),
        nearClipGuardRadius);
    require(!invalidBounds.safe && invalidBounds.fullScreenCoverageRequired,
            "Invalid raster bounds should conservatively require full coverage");

    const glm::vec3 nonFiniteBounds(
        0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN());
    const auto nonFinite = classifyVoxelRasterFaces(
        identity, identity, nonFiniteBounds, boundsMax, glm::vec3(5.0f),
        nearClipGuardRadius);
    require(!nonFinite.safe && nonFinite.fullScreenCoverageRequired,
            "Non-finite raster inputs should conservatively require full coverage");

    const auto nearClipIntersection = classifyVoxelRasterFaces(
        identity, identity, boundsMin, boundsMax,
        glm::vec3(10.05f, 5.0f, 5.0f), nearClipGuardRadius);
    require(!nearClipIntersection.cameraInside && !nearClipIntersection.safe &&
                nearClipIntersection.fullScreenCoverageRequired,
            "A near clipping rectangle intersecting the proxy should require full-screen coverage");

    const auto clearOfNearClip = classifyVoxelRasterFaces(
        identity, identity, boundsMin, boundsMax,
        glm::vec3(10.25f, 5.0f, 5.0f), nearClipGuardRadius);
    require(!clearOfNearClip.cameraInside && clearOfNearClip.safe &&
                !clearOfNearClip.fullScreenCoverageRequired,
            "A camera clear of the near clipping guard should retain face culling");

    const auto invalidNearClipGuard = classifyVoxelRasterFaces(
        identity, identity, boundsMin, boundsMax,
        glm::vec3(15.0f, 5.0f, 5.0f), -1.0f);
    require(!invalidNearClipGuard.safe &&
                invalidNearClipGuard.fullScreenCoverageRequired,
            "Invalid near clipping data should fail closed to conservative coverage");
}

void testVoxelAlignedLayerTraversalContract()
{
    using engine::render::VoxelAlignedLayerInfo;
    using engine::render::classifyVoxelAlignedLayerTraversal;

    VoxelAlignedLayerInfo first{};
    first.dimensions = glm::ivec3(32, 16, 24);
    first.worldFromLocal =
        glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 1.0f, 4.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));
    first.localFromWorld = glm::inverse(first.worldFromLocal);
    first.occupiedMin = glm::ivec3(2, 3, 4);
    first.occupiedMaxExclusive = glm::ivec3(20, 12, 18);
    first.flags = engine::VoxelVolume::FLAG_STATIC |
                  engine::VoxelVolume::FLAG_ALLOW_DENSE_SKIP;
    first.opaqueOnlyDda = true;
    first.unwrappedOpaqueDda = true;

    VoxelAlignedLayerInfo second = first;
    second.occupiedMin = glm::ivec3(1, 5, 6);
    second.occupiedMaxExclusive = glm::ivec3(28, 15, 21);
    const auto compatible =
        classifyVoxelAlignedLayerTraversal(first, second);
    require(compatible.compatible &&
                compatible.occupiedMin == glm::ivec3(1, 3, 4) &&
                compatible.occupiedMaxExclusive == glm::ivec3(28, 15, 21),
            "Aligned opaque layers should expose exact union traversal bounds");

    VoxelAlignedLayerInfo translated = second;
    translated.worldFromLocal[3].x += 0.01f;
    require(!classifyVoxelAlignedLayerTraversal(first, translated).compatible,
            "Mismatched transforms must fail closed to independent traversal");

    VoxelAlignedLayerInfo wrapped = second;
    wrapped.flags |= engine::VoxelVolume::FLAG_WRAP_XZ;
    require(!classifyVoxelAlignedLayerTraversal(first, wrapped).compatible,
            "Wrapped layers must not enter the aligned opaque traversal path");

    const NaturePondBuild pond = buildNaturePond(
        17u, NaturePondDetailTier::Reference10Cm);
    const NaturePondVolumeData& details = pond.volumes[1];
    const NaturePondVolumeData& foliage = pond.volumes[2];
    VoxelAlignedLayerInfo pondDetails = first;
    pondDetails.dimensions = details.dims;
    pondDetails.worldFromLocal =
        glm::translate(glm::mat4(1.0f), details.position) *
        glm::scale(glm::mat4(1.0f), details.scale);
    pondDetails.localFromWorld = glm::inverse(pondDetails.worldFromLocal);
    pondDetails.occupiedMin = glm::ivec3(0);
    pondDetails.occupiedMaxExclusive = details.dims;
    VoxelAlignedLayerInfo pondFoliage = pondDetails;
    pondFoliage.worldFromLocal =
        glm::translate(glm::mat4(1.0f), foliage.position) *
        glm::scale(glm::mat4(1.0f), foliage.scale);
    pondFoliage.localFromWorld = glm::inverse(pondFoliage.worldFromLocal);
    size_t overlappingOccupiedCells = 0;
    for (size_t voxelIndex = 0; voxelIndex < details.voxels.size(); ++voxelIndex)
    {
        overlappingOccupiedCells +=
            details.voxels[voxelIndex] != 0u && foliage.voxels[voxelIndex] != 0u
                ? 1u
                : 0u;
    }
    require(details.dims == foliage.dims && details.position == foliage.position &&
                details.scale == foliage.scale &&
                classifyVoxelAlignedLayerTraversal(pondDetails, pondFoliage)
                    .compatible && overlappingOccupiedCells > 0,
            "Nature pond detail and foliage layers should retain the authored shared lattice");
}

void testTerrainShadowColumnEligibilityContract()
{
    using engine::render::TerrainShadowColumnInput;
    using engine::render::TerrainShadowColumnRejection;
    using engine::render::classifyTerrainShadowColumns;
    using Mode = engine::VoxelVolume::LightingOcclusionMode;

    const glm::ivec3 dimensions(3, 4, 2);
    const std::array<uint32_t, 6> expectedTops{1u, 4u, 2u, 3u, 1u, 4u};
    std::vector<uint8_t> voxels(
        static_cast<size_t>(dimensions.x * dimensions.y * dimensions.z), 0u);
    auto index = [&](int x, int y, int z) {
        return static_cast<size_t>(x) +
               static_cast<size_t>(dimensions.x) *
                   (static_cast<size_t>(y) +
                    static_cast<size_t>(dimensions.y) * static_cast<size_t>(z));
    };
    for (int z = 0; z < dimensions.z; ++z)
    {
        for (int x = 0; x < dimensions.x; ++x)
        {
            const uint32_t top =
                expectedTops[static_cast<size_t>(x + dimensions.x * z)];
            for (uint32_t y = 0; y < top; ++y)
            {
                voxels[index(x, static_cast<int>(y), z)] =
                    static_cast<uint8_t>(1u + ((x + z) % 3));
            }
        }
    }

    TerrainShadowColumnInput input{};
    input.dimensions = dimensions;
    input.volumeFlags = engine::VoxelVolume::FLAG_STATIC;
    input.lightingOcclusionMode = Mode::BinaryOpaque;
    input.voxels = voxels;

    const auto eligible = classifyTerrainShadowColumns(input);
    require(eligible.eligible() &&
                eligible.rejection == TerrainShadowColumnRejection::None &&
                eligible.topYExclusive.size() == expectedTops.size() &&
                std::equal(eligible.topYExclusive.begin(),
                           eligible.topYExclusive.end(), expectedTops.begin()) &&
                std::string(engine::render::terrainShadowColumnRejectionId(
                    eligible.rejection)) == "eligible",
            "Static non-wrapped binary terrain with solid base columns should expose exact X-major top heights");

    TerrainShadowColumnInput rejected = input;
    rejected.volumeFlags = engine::VoxelVolume::FLAG_STATIC |
                           engine::VoxelVolume::FLAG_DYNAMIC;
    require(classifyTerrainShadowColumns(rejected).rejection ==
                TerrainShadowColumnRejection::DynamicVolume,
            "Dynamic terrain must retain the existing 3D shadow DDA");

    rejected = input;
    rejected.volumeFlags = 0u;
    require(classifyTerrainShadowColumns(rejected).rejection ==
                TerrainShadowColumnRejection::MissingStaticFlag,
            "Unclassified terrain must fail closed without the static flag");

    rejected = input;
    rejected.volumeFlags |= engine::VoxelVolume::FLAG_WRAP_XZ;
    require(classifyTerrainShadowColumns(rejected).rejection ==
                TerrainShadowColumnRejection::WrappedVolume,
            "Wrapped terrain must retain the existing 3D shadow DDA");

    rejected = input;
    rejected.lightingOcclusionMode = Mode::TranslucentFoliage;
    require(classifyTerrainShadowColumns(rejected).rejection ==
                TerrainShadowColumnRejection::NonBinaryOpaque,
            "Translucent volumes must not enter a binary terrain-column path");

    rejected = input;
    rejected.volumeFlags |= engine::VoxelVolume::FLAG_GLASS;
    require(classifyTerrainShadowColumns(rejected).rejection ==
                TerrainShadowColumnRejection::UnsupportedMaterialFlags,
            "Transmissive material declarations must fail closed even if the occlusion mode is inconsistent");

    rejected = input;
    rejected.dimensions = glm::ivec3(0, 4, 2);
    require(classifyTerrainShadowColumns(rejected).rejection ==
                TerrainShadowColumnRejection::InvalidDimensions,
            "Invalid terrain dimensions must fail before indexing voxel data");

    std::vector<uint8_t> shortVoxels = voxels;
    shortVoxels.pop_back();
    rejected = input;
    rejected.voxels = shortVoxels;
    require(classifyTerrainShadowColumns(rejected).rejection ==
                TerrainShadowColumnRejection::VoxelCountMismatch,
            "Mismatched CPU voxel data must fail before column classification");

    std::vector<uint8_t> emptyColumnVoxels = voxels;
    for (int y = 0; y < dimensions.y; ++y)
    {
        emptyColumnVoxels[index(1, y, 0)] = 0u;
    }
    rejected = input;
    rejected.voxels = emptyColumnVoxels;
    const auto emptyColumn = classifyTerrainShadowColumns(rejected);
    require(emptyColumn.rejection == TerrainShadowColumnRejection::EmptyColumn &&
                !emptyColumn.eligible() && emptyColumn.topYExclusive.empty(),
            "An empty X/Z column must reject the complete result without leaking partial heights");

    std::vector<uint8_t> gappedColumnVoxels = voxels;
    gappedColumnVoxels[index(1, 1, 0)] = 0u;
    rejected = input;
    rejected.voxels = gappedColumnVoxels;
    const auto gappedColumn = classifyTerrainShadowColumns(rejected);
    require(gappedColumn.rejection ==
                    TerrainShadowColumnRejection::NonContiguousColumn &&
                !gappedColumn.eligible() &&
                gappedColumn.topYExclusive.empty(),
            "A hole below an occupied voxel must reject the terrain-column path");

    for (const NaturePondDetailTierInfo& tier : naturePondDetailTiers())
    {
        for (const NaturePondDensityStepInfo& density : naturePondDensitySteps())
        {
            const NaturePondBuild pond = buildNaturePond(
                17u, tier.tier, NaturePondBuildOptions{false, density.step});
            const auto terrain = std::find_if(
                pond.volumes.begin(), pond.volumes.end(),
                [](const NaturePondVolumeData& volume) {
                    return volume.role == NaturePondVolumeRole::TerrainOpaque;
                });
            require(terrain != pond.volumes.end(),
                    "Every Nature Pond variant must expose its opaque terrain volume");

            TerrainShadowColumnInput pondInput{};
            pondInput.dimensions = terrain->dims;
            pondInput.volumeFlags = engine::VoxelVolume::FLAG_STATIC;
            pondInput.lightingOcclusionMode = Mode::BinaryOpaque;
            pondInput.voxels = terrain->voxels;
            const auto pondTerrain = classifyTerrainShadowColumns(pondInput);
            require(pondTerrain.eligible() &&
                        pondTerrain.topYExclusive.size() ==
                            static_cast<size_t>(pondInput.dimensions.x) *
                                static_cast<size_t>(pondInput.dimensions.z),
                    "Every authored Nature Pond terrain tier and density step must satisfy the strict solid-column contract");
        }
    }
}

void testLocalShadowVolumeInfluenceCullingContract()
{
    using engine::render::EditableAreaLight;
    using engine::render::LocalShadowLightInfluence;
    using engine::render::localShadowLightInfluence;
    using engine::render::worldBoundsMayOccludeLocalShadows;

    EditableAreaLight point{};
    point.shape = LightShape::Point;
    point.position = glm::vec3(2.0f, 3.0f, 4.0f);
    point.influenceRadius = 5.0f;
    const LocalShadowLightInfluence pointInfluence =
        localShadowLightInfluence(point, 0.25f);
    require(pointInfluence.center == point.position &&
                pointInfluence.conservativeRadius >= 5.25f &&
                pointInfluence.conservativeRadius < 5.251f,
            "Point-light shadow influence should include the biased ray origin");

    const std::array<LocalShadowLightInfluence, 1> pointLights{pointInfluence};
    require(worldBoundsMayOccludeLocalShadows(
                glm::vec3(6.5f, 2.5f, 3.5f), glm::vec3(7.0f, 3.5f, 4.5f),
                pointLights),
            "Bounds intersecting a selected light influence must remain");
    require(!worldBoundsMayOccludeLocalShadows(
                glm::vec3(8.0f, 2.5f, 3.5f), glm::vec3(9.0f, 3.5f, 4.5f),
                pointLights),
            "Bounds disjoint from every selected light influence should be culled");

    EditableAreaLight rectangle{};
    rectangle.shape = LightShape::Rectangle;
    rectangle.position = glm::vec3(0.0f);
    rectangle.influenceRadius = 1.0f;
    rectangle.edge1 = glm::vec3(4.0f, 0.0f, 0.0f);
    rectangle.edge2 = glm::vec3(0.0f, 0.0f, 3.0f);
    const LocalShadowLightInfluence rectangleInfluence =
        localShadowLightInfluence(rectangle, 0.0f);
    require(rectangleInfluence.conservativeRadius >= 5.0f &&
                rectangleInfluence.conservativeRadius < 5.001f,
            "Rectangle shadow influence should contain every sampled corner");

    EditableAreaLight capsule{};
    capsule.shape = LightShape::Capsule;
    capsule.position = glm::vec3(99.0f);
    capsule.influenceRadius = 2.0f;
    capsule.capsuleEndA = glm::vec3(-3.0f, 1.0f, 0.0f);
    capsule.capsuleEndB = glm::vec3(5.0f, 1.0f, 0.0f);
    capsule.sourceRadius = 0.5f;
    const LocalShadowLightInfluence capsuleInfluence =
        localShadowLightInfluence(capsule, 0.0f);
    require(capsuleInfluence.center == glm::vec3(1.0f, 1.0f, 0.0f) &&
                capsuleInfluence.conservativeRadius >= 4.5f &&
                capsuleInfluence.conservativeRadius < 4.501f,
            "Capsule shadow influence should use its GPU midpoint and radial extent");

    const std::array<LocalShadowLightInfluence, 2> separatedLights{
        pointInfluence, capsuleInfluence};
    require(worldBoundsMayOccludeLocalShadows(
                glm::vec3(-3.6f, 0.8f, -0.2f),
                glm::vec3(-3.4f, 1.2f, 0.2f), separatedLights),
            "A volume intersecting any selected light region must remain");
    require(!worldBoundsMayOccludeLocalShadows(
                glm::vec3(30.0f), glm::vec3(31.0f), separatedLights),
            "A volume outside all selected light regions should be rejected");
    require(!worldBoundsMayOccludeLocalShadows(
                glm::vec3(-1.0f), glm::vec3(1.0f),
                std::span<const LocalShadowLightInfluence>{}),
            "No selected shadow-casting lights should produce no local occluders");

    rectangle.edge1.x = std::numeric_limits<float>::quiet_NaN();
    const LocalShadowLightInfluence malformedInfluence =
        localShadowLightInfluence(rectangle, 0.0f);
    require(std::isinf(malformedInfluence.conservativeRadius),
            "Malformed light geometry should fail open instead of culling volumes");
}

void testProceduralWorldMaterialCategoryOwnership()
{
    using engine::VoxelMaterialCategory;
    using engine::scene::ProceduralPaletteKind;
    using namespace engine::scene::ProceduralWorldMaterial;

    auto requireCategory = [](ProceduralPaletteKind palette, uint8_t voxelId,
                              VoxelMaterialCategory expected, const char* message) {
        require(engine::scene::proceduralWorldMaterialCategory(palette, voxelId) == expected,
                message);
    };

    for (ProceduralPaletteKind palette :
         {ProceduralPaletteKind::Default, ProceduralPaletteKind::Beach})
    {
        requireCategory(palette, Grass, VoxelMaterialCategory::Plant,
                        "Procedural grass should own the Plant category");
        requireCategory(palette, Dirt, VoxelMaterialCategory::Gravel,
                        "Procedural dirt should own the granular Gravel category");
        requireCategory(palette, Stone, VoxelMaterialCategory::Stone,
                        "Procedural stone should own the Stone category");
        requireCategory(palette, Wood, VoxelMaterialCategory::Wood,
                        "Procedural wood should own the Wood category");
        requireCategory(palette, Leaves, VoxelMaterialCategory::Plant,
                        "Procedural leaves should own the Plant category");
    }

    auto requireBand = [&](uint8_t base, uint8_t count, VoxelMaterialCategory expected,
                           const char* message) {
        for (uint16_t id = base; id < static_cast<uint16_t>(base) + count; ++id)
        {
            requireCategory(ProceduralPaletteKind::Beach, static_cast<uint8_t>(id), expected,
                            message);
        }
    };
    requireBand(BeachDrySandBase, BeachDrySandCount, VoxelMaterialCategory::Gravel,
                "Every dry-sand shade should own the Gravel category");
    requireBand(BeachWetSandBase, BeachWetSandCount, VoxelMaterialCategory::Gravel,
                "Every wet-sand shade should own the Gravel category");
    requireBand(BeachShellBase, BeachShellCount, VoxelMaterialCategory::Stone,
                "Every shell shade should own the Stone category");
    requireBand(BeachDuneGrassBase, BeachDuneGrassCount, VoxelMaterialCategory::Plant,
                "Every dune-grass shade should own the Plant category");
    requireBand(BeachRockBase, BeachRockCount, VoxelMaterialCategory::Stone,
                "Every beach-rock shade should own the Stone category");
    requireBand(BeachDriftwoodBase, BeachDriftwoodCount, VoxelMaterialCategory::Wood,
                "Every driftwood shade should own the Wood category");

    requireCategory(ProceduralPaletteKind::Default, BeachDrySandBase,
                    VoxelMaterialCategory::Generic,
                    "Beach-only IDs should remain Generic in the default palette");
    requireCategory(ProceduralPaletteKind::Beach,
                    static_cast<uint8_t>(BeachDriftwoodBase + BeachDriftwoodCount),
                    VoxelMaterialCategory::Generic,
                    "IDs beyond the authored beach bands should remain Generic");
}

void testSceneCatalogRevisionTracksRefresh()
{
    ReservedTemporaryDirectory temp("scene-catalog-revision");
    engine::scene::SceneManager manager;
    require(manager.sceneCatalogRevision() == 0, "Catalog revision must start at zero");
    manager.setScenesRoot(temp.path());
    manager.refreshSceneCatalog();
    require(manager.sceneCatalogRevision() == 1 && manager.availableScenes().empty(),
            "An empty refresh must advance the catalog revision");
    const auto path = temp.path() / "broken.json";
    { std::ofstream file(path); file << "{broken"; }
    manager.refreshSceneCatalog();
    require(manager.sceneCatalogRevision() == 2 && manager.availableScenes().size() == 1 &&
                !manager.availableScenes()[0].valid,
            "A refresh must publish invalid entries and advance the revision");
    manager.refreshSceneCatalog();
    require(manager.sceneCatalogRevision() == 3,
            "Even an unchanged refresh must invalidate derived catalog metadata");
    std::filesystem::remove(path);
    manager.refreshSceneCatalog();
    require(manager.sceneCatalogRevision() == 4 && manager.availableScenes().empty(),
            "Removing a scene must invalidate cached indices");
}

void testSceneManagerReloadDecisionEffects()
{
    SceneConfig current{};
    current.name = "aquarium_test";
    current.loadVoxelWorld = false;
    current.loadOBBVolumes = false;
    current.loadGlassPanel = false;
    current.loadAquariumTest = true;
    current.useMeshTankGlass = true;
    current.useWaterV2 = true;
    current.loadCloudScene = true;
    current.cloudSeed = 11u;
    current.cloudCoverage = 0.35f;
    current.worldDimsX = 1;
    current.worldDimsY = 1;
    current.worldDimsZ = 1;

    const engine::scene::SceneReloadDecision unchanged =
        engine::scene::SceneManager::evaluateReloadDecision(current, current);
    require(!unchanged.scenePresetChanged && !unchanged.sceneCameraChanged &&
                !unchanged.volumeSceneChanged && !unchanged.voxelWorldChanged &&
                !unchanged.cloudGenerationChanged && !unchanged.skyColorChanged,
            "SceneManager should report no reload effects for identical configs");

    SceneConfig cameraOnly = current;
    cameraOnly.cameraPosition.x += 1.0f;
    const engine::scene::SceneReloadDecision cameraDecision =
        engine::scene::SceneManager::evaluateReloadDecision(cameraOnly, current);
    require(cameraDecision.sceneCameraChanged && cameraDecision.shouldUpdateCamera(),
            "SceneManager should surface camera-only reload effects");
    require(!cameraDecision.volumeSceneChanged && !cameraDecision.voxelWorldChanged,
            "SceneManager should not escalate camera-only edits to scene rebuilds");

    SceneConfig skyOnly = current;
    skyOnly.skyColor += glm::vec3(0.1f, 0.0f, 0.0f);
    const engine::scene::SceneReloadDecision skyDecision =
        engine::scene::SceneManager::evaluateReloadDecision(skyOnly, current);
    require(skyDecision.skyColorChanged && !skyDecision.volumeSceneChanged &&
                !skyDecision.cloudGenerationChanged,
            "SceneManager should keep sky-only edits live-update scoped");

    SceneConfig voxelWorldOnly = current;
    voxelWorldOnly.loadVoxelWorld = !current.loadVoxelWorld;
    const engine::scene::SceneReloadDecision voxelDecision =
        engine::scene::SceneManager::evaluateReloadDecision(voxelWorldOnly, current);
    require(voxelDecision.voxelWorldChanged && !voxelDecision.volumeSceneChanged,
            "SceneManager should separate legacy voxel-world changes from volume scene rebuilds");

    SceneConfig waterV2Only = current;
    waterV2Only.useWaterV2 = !current.useWaterV2;
    const engine::scene::SceneReloadDecision waterDecision =
        engine::scene::SceneManager::evaluateReloadDecision(waterV2Only, current);
    require(waterDecision.waterRuntimeChanged &&
                waterDecision.aquariumWaterV2ModeChanged &&
                waterDecision.volumeSceneChanged,
            "SceneManager should rebuild aquarium volumes when the water V2 mode changes");

    SceneConfig placeableEdit = current;
    PlaceableInstance placeable{};
    placeable.uuid = "scene-manager-test-placeable";
    placeable.prototypeSlug = "eelgrass";
    placeable.prototypeVersion = 1u;
    placeable.position = glm::vec3(1.0f, 0.0f, 2.0f);
    placeable.seed = 77u;
    placeableEdit.placeables.push_back(placeable);
    const engine::scene::SceneReloadDecision placeableDecision =
        engine::scene::SceneManager::evaluateReloadDecision(placeableEdit, current);
    require(placeableDecision.aquariumPlaceablesChanged &&
                placeableDecision.volumeSceneChanged,
            "SceneManager should rebuild aquarium volumes when placeables change");

    SceneConfig equivalentRotation = placeableEdit;
    equivalentRotation.placeables[0].rotation =
        -equivalentRotation.placeables[0].rotation;
    const engine::scene::SceneReloadDecision equivalentRotationDecision =
        engine::scene::SceneManager::evaluateReloadDecision(equivalentRotation,
                                                             placeableEdit);
    require(!equivalentRotationDecision.aquariumPlaceablesChanged &&
                !equivalentRotationDecision.volumeSceneChanged,
            "Equivalent quaternion signs should not trigger a placeable rebuild");

    SceneConfig cloudOnly = current;
    cloudOnly.cloudCoverage += 0.1f;
    const engine::scene::SceneReloadDecision cloudDecision =
        engine::scene::SceneManager::evaluateReloadDecision(cloudOnly, current);
    require(cloudDecision.cloudGenerationChanged &&
                cloudDecision.shouldHandleCloudLifecycle() &&
                !cloudDecision.volumeSceneChanged,
            "SceneManager should keep cloud-generation edits out of volume scene rebuilds");

    SceneConfig procedural = current;
    procedural.name = "procedural_world";
    procedural.loadAquariumTest = false;
    procedural.loadProceduralWorld = true;
    procedural.useMeshTankGlass = false;
    procedural.enableWater = false;
    procedural.useWaterV2 = false;
    const engine::scene::SceneReloadDecision proceduralDecision =
        engine::scene::SceneManager::evaluateReloadDecision(procedural, current);
    require(proceduralDecision.scenePresetChanged &&
                proceduralDecision.enteringProceduralScene &&
                proceduralDecision.volumeScenePresetChanged &&
                proceduralDecision.volumeSceneChanged &&
                proceduralDecision.shouldApplyProceduralPresetDefaults(),
            "SceneManager should classify procedural scene entry as a preset and volume reload");

    SceneConfig copied = current;
    SceneConfig source = current;
    source.loadCloudScene = false;
    source.cloudSeed = 99u;
    source.cloudAltitude = 140.0f;
    source.cloudCoverage = 0.8f;
    source.cloudScale = 64.0f;
    source.name = "unrelated-name";
    engine::scene::SceneManager::copyCloudGenerationSettings(copied, source);
    require(!copied.loadCloudScene && copied.cloudSeed == 99u &&
                nearlyEqual(copied.cloudAltitude, 140.0f) &&
                nearlyEqual(copied.cloudCoverage, 0.8f) &&
                nearlyEqual(copied.cloudScale, 64.0f),
            "SceneManager should copy cloud generation settings");
    require(copied.name == current.name,
            "SceneManager cloud setting copy should leave unrelated scene fields alone");
}

void testEnvironmentWindContract()
{
    using engine::scene::EnvironmentWindSettings;

    const EnvironmentWindSettings defaults =
        engine::scene::sanitizeEnvironmentWindSettings(
            engine::scene::defaultEnvironmentWindSettings());
    require(nearlyEqual(defaults.direction.x, 0.8f) &&
                nearlyEqual(defaults.direction.y, 0.6f) &&
                nearlyEqual(defaults.speed, 1.0f) &&
                nearlyEqual(defaults.strength, 1.0f) &&
                nearlyEqual(defaults.gustStrength, 0.0f) &&
                nearlyEqual(defaults.turbulenceStrength, 0.0f) &&
                nearlyEqual(defaults.verticalLift, 0.0f),
            "Environment wind defaults should preserve the accepted foliage motion");

    EnvironmentWindSettings invalid{};
    invalid.direction = glm::vec2(std::numeric_limits<float>::max(),
                                  std::numeric_limits<float>::max());
    invalid.speed = -2.0f;
    invalid.strength = 9.0f;
    invalid.gustStrength = 4.0f;
    invalid.gustFrequencyHz = 0.0f;
    invalid.turbulenceStrength = -1.0f;
    invalid.verticalLift = std::numeric_limits<float>::infinity();
    const EnvironmentWindSettings sanitized =
        engine::scene::sanitizeEnvironmentWindSettings(invalid);
    require(std::isfinite(sanitized.direction.x) &&
                std::isfinite(sanitized.direction.y) &&
                nearlyEqual(glm::length(sanitized.direction), 1.0f) &&
                nearlyEqual(sanitized.speed, 0.0f) &&
                nearlyEqual(sanitized.strength, 2.0f) &&
                nearlyEqual(sanitized.gustStrength, 1.0f) &&
                nearlyEqual(sanitized.gustFrequencyHz, 0.01f) &&
                nearlyEqual(sanitized.turbulenceStrength, 0.0f) &&
                nearlyEqual(sanitized.verticalLift, 0.0f),
            "Environment wind sanitization should normalize safely and clamp every control");

    EnvironmentWindSettings animated = defaults;
    animated.gustStrength = 0.55f;
    animated.gustFrequencyHz = 0.3f;
    animated.turbulenceStrength = 0.4f;
    animated.verticalLift = 0.2f;
    const glm::vec3 position(12.5f, 3.0f, -7.25f);
    const engine::scene::EnvironmentWindSample first =
        engine::scene::sampleEnvironmentWind(animated, position, 4.25f);
    const engine::scene::EnvironmentWindSample repeat =
        engine::scene::sampleEnvironmentWind(animated, position, 4.25f);
    const engine::scene::EnvironmentWindSample later =
        engine::scene::sampleEnvironmentWind(animated, position, 5.25f);
    require(first.velocity == repeat.velocity &&
                first.intensity == repeat.intensity &&
                first.gustMultiplier == repeat.gustMultiplier &&
                first.turbulenceSignal == repeat.turbulenceSignal,
            "Environment wind sampling must be deterministic for equal inputs");
    require(first.gustMultiplier != later.gustMultiplier ||
                first.turbulenceSignal != later.turbulenceSignal,
            "Enabled gusts and turbulence should evolve over time");

    const engine::scene::EnvironmentWindSample defaultSample =
        engine::scene::sampleEnvironmentWind(defaults, glm::vec3(0.0f), 0.0f);
    require(nearlyEqual(defaultSample.velocity.x, 0.8f) &&
                nearlyEqual(defaultSample.velocity.y, 0.0f) &&
                nearlyEqual(defaultSample.velocity.z, 0.6f) &&
                nearlyEqual(defaultSample.intensity, 1.0f),
            "Default environment wind should produce the established unit horizontal field");

    const float phaseGust =
        engine::scene::environmentWindGustMultiplierFromPhase(
            animated, 1.75f, 4.25f);
    require(nearlyEqual(
                phaseGust,
                engine::scene::environmentWindGustMultiplierFromPhase(
                    animated, 1.75f, 4.25f)),
            "Patch-coherent foliage gust sampling must be deterministic");
}

void testEnvironmentSettingsRoundTripAndSkyPreset()
{
    SceneConfig source{};
    source.name = "environment-source";
    source.loadCloudScene = false;
    source.cloudCoverage = 0.77f;
    source.worldSeed = 314u;
    source.cloudWindSpeed = 2.25f;
    source.cloudWindDirection = glm::vec2(-0.6f, 0.8f);
    source.cloudShadowStrength = 0.62f;
    source.skyPreset = 4;
    source.skyColor = glm::vec3(0.2f, 0.3f, 0.45f);
    source.environmentWind.direction = glm::vec2(-0.6f, 0.8f);
    source.environmentWind.speed = 1.7f;
    source.environmentWind.strength = 0.85f;
    source.environmentWind.gustStrength = 0.35f;
    source.environmentWind.gustFrequencyHz = 0.42f;
    source.environmentWind.turbulenceStrength = 0.25f;
    source.environmentWind.verticalLift = 0.15f;
    source.environmentTime.enabled = true;
    source.environmentTime.cycleEnabled = true;
    source.environmentTime.timeOfDayHours = 6.5f;
    source.environmentTime.dayLengthMinutes = 20.0f;
    source.windborneParticles.enabled = true;
    source.windborneParticles.amount = 0.48f;
    source.windborneParticles.leafFraction = 0.62f;
    source.windborneParticles.scale = 1.2f;
    source.windborneParticles.visibilityDistance = 44.0f;

    const engine::scene::EnvironmentSettings settings =
        engine::scene::environmentSettingsFromSceneConfig(source);
    require(engine::scene::environmentWindSettingsEquivalent(
                settings.wind, source.environmentWind),
            "EnvironmentSettings should copy production environment wind");
    require(engine::scene::environmentTimeSettingsEquivalent(
                settings.time, source.environmentTime),
            "EnvironmentSettings should copy time-of-day authoring");
    require(engine::scene::windborneParticleSettingsEquivalent(
                settings.windborneParticles, source.windborneParticles),
            "EnvironmentSettings should copy windborne particle authoring");
    require(nearlyEqual(settings.cloudWindSpeed, source.cloudWindSpeed),
            "EnvironmentSettings should copy cloud wind speed");
    require(nearlyEqual(settings.cloudWindDirection.x,
                        source.cloudWindDirection.x) &&
                nearlyEqual(settings.cloudWindDirection.y,
                            source.cloudWindDirection.y),
            "EnvironmentSettings should copy cloud wind direction");
    require(nearlyEqual(settings.cloudShadowStrength,
                        source.cloudShadowStrength),
            "EnvironmentSettings should copy cloud shadow strength");
    require(settings.skyPreset == source.skyPreset,
            "EnvironmentSettings should copy sky preset");
    require(nearlyEqual(settings.skyColor.x, source.skyColor.x) &&
                nearlyEqual(settings.skyColor.y, source.skyColor.y) &&
                nearlyEqual(settings.skyColor.z, source.skyColor.z),
            "EnvironmentSettings should copy sky color");

    SceneConfig target{};
    target.name = "unchanged-target";
    target.loadCloudScene = true;
    target.cloudCoverage = 0.25f;
    target.worldSeed = 77u;
    engine::scene::applyEnvironmentSettingsToSceneConfig(target, settings);
    require(engine::scene::environmentWindSettingsEquivalent(
                target.environmentWind, source.environmentWind) &&
                engine::scene::environmentTimeSettingsEquivalent(
                    target.environmentTime, source.environmentTime) &&
                engine::scene::windborneParticleSettingsEquivalent(
                    target.windborneParticles, source.windborneParticles) &&
                nearlyEqual(target.cloudWindSpeed, source.cloudWindSpeed) &&
                nearlyEqual(target.cloudWindDirection.x,
                            source.cloudWindDirection.x) &&
                nearlyEqual(target.cloudWindDirection.y,
                            source.cloudWindDirection.y) &&
                nearlyEqual(target.cloudShadowStrength,
                            source.cloudShadowStrength) &&
                target.skyPreset == source.skyPreset &&
                nearlyEqual(target.skyColor.z, source.skyColor.z),
            "EnvironmentSettings should apply only environment fields");
    require(target.name == "unchanged-target" && target.loadCloudScene &&
                nearlyEqual(target.cloudCoverage, 0.25f) &&
                target.worldSeed == 77u,
            "EnvironmentSettings apply should leave unrelated fields alone");

    const engine::scene::EnvironmentSettings sunset =
        engine::scene::withSkyPreset(settings, 1);
    require(sunset.skyPreset == 1 &&
                nearlyEqual(sunset.skyColor.x, 0.95f) &&
                nearlyEqual(sunset.skyColor.y, 0.45f) &&
                nearlyEqual(sunset.skyColor.z, 0.25f),
            "EnvironmentSettings should derive sunset preset color");

    const engine::scene::EnvironmentSettings custom =
        engine::scene::withSkyPreset(settings, 4);
    require(engine::scene::isCustomSkyPreset(custom.skyPreset) &&
                nearlyEqual(custom.skyColor.x, source.skyColor.x) &&
                nearlyEqual(custom.skyColor.y, source.skyColor.y) &&
                nearlyEqual(custom.skyColor.z, source.skyColor.z),
            "EnvironmentSettings custom preset should preserve the current color");

    const glm::vec3 manualColor(0.8f, 0.7f, 0.6f);
    const engine::scene::EnvironmentSettings colorOnly =
        engine::scene::withSkyColor(sunset, manualColor);
    require(colorOnly.skyPreset == sunset.skyPreset &&
                nearlyEqual(colorOnly.skyColor.x, manualColor.x) &&
                nearlyEqual(colorOnly.skyColor.y, manualColor.y) &&
                nearlyEqual(colorOnly.skyColor.z, manualColor.z),
            "EnvironmentSettings manual color should not force the custom preset");
}

void testEnvironmentTimeContract()
{
    using engine::scene::EnvironmentTimePhase;
    using engine::scene::EnvironmentTimePresentation;
    using engine::scene::EnvironmentTimeSettings;

    EnvironmentTimePresentation authored{};
    authored.sunElevation = 55.0f;
    authored.sunAzimuth = 135.0f;
    authored.sunColor = glm::vec3(1.0f, 0.95f, 0.85f);
    authored.sunIntensity = 2.5f;
    authored.skyColor = glm::vec3(0.42f, 0.67f, 0.92f);
    authored.paintedSky = engine::render::makePaintedSkySettings(
        engine::render::PaintedSkyPreset::SoftDayV0);
    authored.paintedClouds = engine::render::makePaintedCloudSettings(
        engine::render::PaintedCloudPreset::SoftDayV0);
    authored.hemisphereAmbient =
        engine::render::makeHemisphereAmbientSettings(
            engine::render::HemisphereAmbientPreset::WarmCoolOutdoorV0);
    authored.sceneAtmosphere =
        engine::render::makeSceneAtmosphereSettings(
            engine::render::SceneAtmospherePreset::OutdoorHazeV0);

    EnvironmentTimeSettings disabled{};
    const auto disabledSample =
        engine::scene::sampleEnvironmentTime(disabled, 999.0, authored);
    require(!disabledSample.active &&
                disabledSample.presentation == authored &&
                nearlyEqual(disabledSample.hour, 12.0f),
            "Disabled TIME-001 must return the authored presentation exactly");

    EnvironmentTimeSettings fixed = disabled;
    fixed.enabled = true;
    const auto noon = engine::scene::sampleEnvironmentTime(fixed, 999.0, authored);
    require(noon.active && noon.phase == EnvironmentTimePhase::Day &&
                noon.presentation == authored,
            "Fixed noon must preserve the accepted authored presentation exactly");

    fixed.timeOfDayHours = 0.0f;
    const auto night = engine::scene::sampleEnvironmentTime(fixed, 0.0, authored);
    require(night.phase == EnvironmentTimePhase::Night &&
                nearlyEqual(night.presentation.sunElevation, -20.0f) &&
                night.presentation.sunIntensity < authored.sunIntensity &&
                night.presentation.skyColor.z < authored.skyColor.z &&
                nearlyEqual(night.presentation.paintedClouds.coverage,
                            authored.paintedClouds.coverage) &&
                nearlyEqual(night.presentation.paintedClouds.worldScale,
                            authored.paintedClouds.worldScale),
            "Night should alter transient color/lighting while preserving cloud structure and budget");

    EnvironmentTimePresentation compatibilityOff = authored;
    compatibilityOff.paintedSky = {};
    compatibilityOff.paintedClouds = {};
    const auto offNight =
        engine::scene::sampleEnvironmentTime(fixed, 0.0, compatibilityOff);
    require(!offNight.presentation.paintedSky.enabled() &&
                !offNight.presentation.paintedClouds.enabled(),
            "Time-of-day sampling must not opt disabled sky or cloud effects in");

    EnvironmentTimeSettings cycling{};
    cycling.enabled = true;
    cycling.cycleEnabled = true;
    cycling.timeOfDayHours = 12.0f;
    cycling.dayLengthMinutes = 12.0f;
    require(nearlyEqual(engine::scene::resolveEnvironmentTimeHour(
                            cycling, 180.0),
                        18.0f) &&
                nearlyEqual(engine::scene::resolveEnvironmentTimeHour(
                                cycling, 720.0),
                            12.0f) &&
                nearlyEqual(engine::scene::resolveEnvironmentTimeHour(
                                cycling, 180.0),
                            engine::scene::resolveEnvironmentTimeHour(
                                cycling, 180.0)),
            "The dedicated clock must advance deterministically and wrap at one authored day");

    EnvironmentTimeSettings invalid{};
    invalid.enabled = true;
    invalid.timeOfDayHours = -49.0f;
    invalid.dayLengthMinutes = std::numeric_limits<float>::infinity();
    const EnvironmentTimeSettings sanitized =
        engine::scene::sanitizeEnvironmentTimeSettings(invalid);
    require(sanitized.enabled && nearlyEqual(sanitized.timeOfDayHours, 23.0f) &&
                nearlyEqual(sanitized.dayLengthMinutes, 12.0f),
            "Time-of-day sanitization should wrap hours and repair non-finite day length");
}

void testWindborneParticleSettingsContract()
{
    using engine::scene::WindborneParticleSettings;

    require(engine::scene::kMaxWindborneParticleCount == 64u,
            "Windborne particles must keep the fixed 64-particle ceiling");
    const WindborneParticleSettings defaults =
        engine::scene::sanitizeWindborneParticleSettings(
            engine::scene::defaultWindborneParticleSettings());
    require(!defaults.enabled && nearlyEqual(defaults.amount, 0.65f) &&
                nearlyEqual(defaults.leafFraction, 0.70f) &&
                nearlyEqual(defaults.scale, 1.0f) &&
                nearlyEqual(defaults.visibilityDistance, 36.0f),
            "Windborne particle defaults should be bounded and compatibility-off");

    WindborneParticleSettings invalid{};
    invalid.enabled = true;
    invalid.amount = -1.0f;
    invalid.leafFraction = 3.0f;
    invalid.scale = std::numeric_limits<float>::infinity();
    invalid.visibilityDistance = 2.0f;
    const WindborneParticleSettings sanitized =
        engine::scene::sanitizeWindborneParticleSettings(invalid);
    require(sanitized.enabled && nearlyEqual(sanitized.amount, 0.0f) &&
                nearlyEqual(sanitized.leafFraction, 1.0f) &&
                nearlyEqual(sanitized.scale, 1.0f) &&
                nearlyEqual(sanitized.visibilityDistance, 8.0f),
            "Windborne particle sanitization should clamp values and repair non-finite input");
    require(engine::scene::windborneParticleSettingsEquivalent(
                sanitized,
                engine::scene::sanitizeWindborneParticleSettings(sanitized)),
            "Sanitized windborne particle settings should be stable");
}

void testWindborneParticleFieldContract()
{
    using engine::scene::EnvironmentWindSettings;
    using engine::scene::WindborneParticleCandidate;
    using engine::scene::WindborneParticleDomain;
    using engine::scene::WindborneParticleSettings;

    constexpr uint32_t kSeed = 0x1234abcdu;
    const WindborneParticleDomain domain{
        .minimum = glm::vec3(-12.0f, 4.0f, -10.0f),
        .maximum = glm::vec3(12.0f, 8.0f, 10.0f),
        .leafMaterialBase = 222u,
        .leafMaterialCount = 8u,
        .moteMaterialBase = 230u,
        .moteMaterialCount = 8u,
    };
    require(engine::scene::validWindborneParticleDomain(domain),
            "A finite bounded WIND-002 domain with byte-packable palettes should be valid");

    const std::vector<WindborneParticleCandidate> field =
        engine::scene::buildWindborneParticleField(domain, kSeed);
    const std::vector<WindborneParticleCandidate> repeat =
        engine::scene::buildWindborneParticleField(domain, kSeed);
    const std::vector<WindborneParticleCandidate> differentSeed =
        engine::scene::buildWindborneParticleField(domain, kSeed + 1u);
    require(field.size() == engine::scene::kMaxWindborneParticleCount &&
                field.size() == 64u,
            "A valid WIND-002 domain must always produce the exact fixed 64-candidate budget");
    require(engine::scene::windborneParticleFieldEquivalent(field, repeat),
            "WIND-002 candidates must be deterministic for equal domain and seed inputs");
    require(!engine::scene::windborneParticleFieldEquivalent(field,
                                                               differentSeed),
            "WIND-002 candidates should change when the world seed changes");

    std::array<bool, engine::scene::kMaxWindborneParticleCount> stableIds{};
    for (size_t index = 0; index < field.size(); ++index)
    {
        const WindborneParticleCandidate& candidate = field[index];
        const float expectedRank =
            (static_cast<float>(index) + 0.5f) /
            static_cast<float>(engine::scene::kMaxWindborneParticleCount);
        require(nearlyEqual(candidate.selectionRank, expectedRank) &&
                    (index == 0u ||
                     field[index - 1u].selectionRank < candidate.selectionRank),
                "WIND-002 candidates must retain strict normalized prefix-selection order");
        require(candidate.stableId < stableIds.size() &&
                    !stableIds[candidate.stableId],
                "Every WIND-002 candidate should retain one unique stable ID");
        stableIds[candidate.stableId] = true;
        require(candidate.anchorWorld.x >= domain.minimum.x &&
                    candidate.anchorWorld.x <= domain.maximum.x &&
                    candidate.anchorWorld.y >= domain.minimum.y &&
                    candidate.anchorWorld.y <= domain.maximum.y &&
                    candidate.anchorWorld.z >= domain.minimum.z &&
                    candidate.anchorWorld.z <= domain.maximum.z,
                "WIND-002 candidate anchors must stay inside their authored domain");
        require(candidate.leafMaterialId >= domain.leafMaterialBase &&
                    candidate.leafMaterialId <
                        domain.leafMaterialBase + domain.leafMaterialCount &&
                    candidate.moteMaterialId >= domain.moteMaterialBase &&
                    candidate.moteMaterialId <
                        domain.moteMaterialBase + domain.moteMaterialCount,
                "WIND-002 candidates must select only their authored leaf and mote palettes");
        require(std::isfinite(candidate.phaseRadians) &&
                    std::isfinite(candidate.kindRank) &&
                    std::isfinite(candidate.travelAmplitude) &&
                    std::isfinite(candidate.motionSpeed) &&
                    std::isfinite(candidate.flutter),
                "WIND-002 candidate authoring values must remain finite");
    }
    require(std::all_of(stableIds.begin(), stableIds.end(),
                        [](bool present) { return present; }),
            "The fixed WIND-002 field should preserve all stable IDs exactly once");

    WindborneParticleDomain invalid = domain;
    invalid.maximum.x = invalid.minimum.x;
    require(!engine::scene::validWindborneParticleDomain(invalid) &&
                engine::scene::buildWindborneParticleField(invalid, kSeed).empty(),
            "Degenerate WIND-002 domains must fail closed");
    invalid = domain;
    invalid.minimum.y = std::numeric_limits<float>::quiet_NaN();
    require(!engine::scene::validWindborneParticleDomain(invalid) &&
                engine::scene::buildWindborneParticleField(invalid, kSeed).empty(),
            "Non-finite WIND-002 domains must fail closed");
    invalid = domain;
    invalid.maximum.z = std::numeric_limits<float>::infinity();
    require(!engine::scene::validWindborneParticleDomain(invalid) &&
                engine::scene::buildWindborneParticleField(invalid, kSeed).empty(),
            "Infinite WIND-002 bounds must fail closed");
    invalid = domain;
    invalid.leafMaterialBase = 250u;
    invalid.leafMaterialCount = std::numeric_limits<uint32_t>::max();
    require(!engine::scene::validWindborneParticleDomain(invalid) &&
                engine::scene::buildWindborneParticleField(invalid, kSeed).empty(),
            "Overflowing WIND-002 material ranges must fail closed without unsigned wraparound");
    invalid = domain;
    invalid.moteMaterialBase = std::numeric_limits<uint32_t>::max();
    invalid.moteMaterialCount = std::numeric_limits<uint32_t>::max();
    require(!engine::scene::validWindborneParticleDomain(invalid) &&
                engine::scene::buildWindborneParticleField(invalid, kSeed).empty(),
            "Out-of-byte-range WIND-002 material bases must fail closed");

    WindborneParticleSettings particles =
        engine::scene::defaultWindborneParticleSettings();
    particles.amount = 1.0f;
    require(engine::scene::activeWindborneParticleCount(particles) == 0u,
            "Compatibility-disabled WIND-002 settings should activate no candidates");
    particles.enabled = true;
    particles.amount = 0.0f;
    require(engine::scene::activeWindborneParticleCount(particles) == 0u,
            "Zero WIND-002 amount should activate no candidates");
    particles.amount = 1.0f;
    require(engine::scene::activeWindborneParticleCount(particles) == 64u &&
                engine::scene::activeWindborneParticleCount(
                    particles, std::numeric_limits<uint32_t>::max()) == 64u,
            "Full WIND-002 amount must activate exactly the fixed ceiling even for oversized residency input");

    uint32_t previousActive = 0u;
    for (uint32_t prefix = 0u;
         prefix <= engine::scene::kMaxWindborneParticleCount; ++prefix)
    {
        particles.amount =
            static_cast<float>(prefix) /
            static_cast<float>(engine::scene::kMaxWindborneParticleCount);
        const uint32_t active =
            engine::scene::activeWindborneParticleCount(particles,
                                                        field.size());
        require(active == prefix && active >= previousActive,
                "WIND-002 amount changes should select one exact monotonic candidate prefix");
        for (size_t index = 0; index < field.size(); ++index)
        {
            const engine::scene::WindborneParticleSample sample =
                engine::scene::sampleWindborneParticle(
                    field[index], particles,
                    engine::scene::defaultEnvironmentWindSettings(), 0.0f);
            require(sample.active == (index < prefix),
                    "WIND-002 CPU sampling should activate precisely the rank-selected prefix");
        }
        previousActive = active;
    }

    particles.amount = 10.5f /
        static_cast<float>(engine::scene::kMaxWindborneParticleCount);
    require(engine::scene::activeWindborneParticleCount(particles) == 10u &&
                engine::scene::sampleWindborneParticle(
                    field[9u], particles,
                    engine::scene::defaultEnvironmentWindSettings(), 0.0f)
                    .active &&
                !engine::scene::sampleWindborneParticle(
                     field[10u], particles,
                     engine::scene::defaultEnvironmentWindSettings(), 0.0f)
                     .active,
            "An exact WIND-002 midpoint must use the same strict prefix comparison on CPU and GPU");

    const std::vector<engine::render::FoliageGpuPrimitive> geometry =
        engine::render::buildWindborneParticleVoxelGeometry(field);
    require(sizeof(engine::render::FoliageGpuPrimitive) == 64u &&
                alignof(engine::render::FoliageGpuPrimitive) == 16u &&
                engine::render::kWindborneParticlePrimitiveFlag == 0x80000000u &&
                geometry.size() == field.size(),
            "WIND-002 geometry must preserve the fixed foliage SSBO ABI, tag, and candidate count");
    for (size_t index = 0; index < geometry.size(); ++index)
    {
        const WindborneParticleCandidate& candidate = field[index];
        const engine::render::FoliageGpuPrimitive& primitive = geometry[index];
        const uint32_t packedMaterials = primitive.materialSeedFlags.x;
        const uint32_t expectedSeed =
            (candidate.randomSeed &
             engine::render::kFoliageVoxelRandomSeedMask) |
            engine::render::kFoliageVoxelOrientedSlabFlag |
            (((candidate.randomSeed >> 8u) & 7u)
             << engine::render::kFoliageVoxelYawOctantShift);
        require((packedMaterials & 0xffu) == candidate.leafMaterialId &&
                    ((packedMaterials >> 8u) & 0xffu) ==
                        candidate.moteMaterialId &&
                    (packedMaterials & 0xffff0000u) == 0u,
                "WIND-002 geometry must byte-pack its leaf and mote material IDs without aliasing");
        require(primitive.materialSeedFlags.y ==
                        engine::render::kWindborneParticlePrimitiveFlag &&
                    primitive.materialSeedFlags.z == expectedSeed &&
                    primitive.materialSeedFlags.w == static_cast<uint32_t>(
                        engine::render::FoliageVoxelPrimitiveRole::Leaf),
                "WIND-002 geometry must retain its ambient tag, deterministic orientation, and foliage role");
        require(nearlyEqual(primitive.centerMotionT.x,
                            candidate.anchorWorld.x) &&
                    nearlyEqual(primitive.centerMotionT.y,
                                candidate.anchorWorld.y) &&
                    nearlyEqual(primitive.centerMotionT.z,
                                candidate.anchorWorld.z) &&
                    nearlyEqual(primitive.centerMotionT.w,
                                candidate.selectionRank) &&
                    nearlyEqual(primitive.halfExtentPhase.w,
                                candidate.phaseRadians) &&
                    nearlyEqual(primitive.swayProfile.x,
                                candidate.travelAmplitude) &&
                    nearlyEqual(primitive.swayProfile.y,
                                candidate.motionSpeed) &&
                    nearlyEqual(primitive.swayProfile.z,
                                candidate.flutter) &&
                    nearlyEqual(primitive.swayProfile.w,
                                candidate.kindRank),
                "WIND-002 geometry must transport the stable candidate state through the foliage ABI");
    }

    std::vector<WindborneParticleCandidate> tooMany = field;
    tooMany.push_back(field.front());
    require(engine::render::buildWindborneParticleVoxelGeometry(tooMany).empty(),
            "WIND-002 geometry must reject inputs above its fixed ceiling");
    std::vector<WindborneParticleCandidate> unsorted = field;
    std::swap(unsorted.front(), unsorted.back());
    require(engine::render::buildWindborneParticleVoxelGeometry(unsorted).empty(),
            "WIND-002 geometry must fail closed when prefix rank order is lost");
    std::vector<WindborneParticleCandidate> malformedRank = field;
    malformedRank[10u].selectionRank += 0.001f;
    require(
        engine::render::buildWindborneParticleVoxelGeometry(malformedRank).empty(),
        "WIND-002 geometry must reject a sorted field whose ranks no longer match the fixed prefix contract");
    std::vector<WindborneParticleCandidate> nonFinite = field;
    nonFinite.front().anchorWorld.x =
        std::numeric_limits<float>::quiet_NaN();
    require(engine::render::buildWindborneParticleVoxelGeometry(nonFinite).empty(),
            "WIND-002 geometry must fail closed for non-finite candidates");

    particles.enabled = true;
    particles.amount = 1.0f;
    EnvironmentWindSettings wind =
        engine::scene::defaultEnvironmentWindSettings();
    wind.strength = 0.0f;
    wind.speed = 8.0f;
    wind.gustStrength = 1.0f;
    wind.turbulenceStrength = 1.0f;
    wind.verticalLift = 2.0f;
    const engine::scene::WindborneParticleSample zeroStrength =
        engine::scene::sampleWindborneParticle(field.front(), particles, wind,
                                               173.0f);
    require(zeroStrength.active &&
                glm::distance(zeroStrength.worldPosition,
                              field.front().anchorWorld) <= 1e-6f,
            "Zero wind strength must keep active WIND-002 particles at their static anchors");

    wind = engine::scene::defaultEnvironmentWindSettings();
    wind.speed = 0.0f;
    wind.gustStrength = 0.0f;
    wind.turbulenceStrength = 0.0f;
    const engine::scene::WindborneParticleSample frozenFirst =
        engine::scene::sampleWindborneParticle(field.front(), particles, wind,
                                               -100.0f);
    const engine::scene::WindborneParticleSample frozenLater =
        engine::scene::sampleWindborneParticle(field.front(), particles, wind,
                                               100.0f);
    require(glm::distance(frozenFirst.worldPosition,
                          frozenLater.worldPosition) <= 1e-6f,
            "Zero wind speed with gusts disabled must freeze WIND-002 motion over time");

    const auto responseExists =
        [&](const EnvironmentWindSettings& baseline,
            const EnvironmentWindSettings& changed, bool verticalOnly) {
            for (uint32_t step = 0u; step < 24u; ++step)
            {
                const float time = static_cast<float>(step) * 0.37f + 0.13f;
                const glm::vec3 baselinePosition =
                    engine::scene::sampleWindborneParticle(
                        field[7u], particles, baseline, time)
                        .worldPosition;
                const glm::vec3 changedPosition =
                    engine::scene::sampleWindborneParticle(
                        field[7u], particles, changed, time)
                        .worldPosition;
                if (verticalOnly
                        ? std::abs(changedPosition.y - baselinePosition.y) >
                              1e-4f
                        : glm::distance(changedPosition, baselinePosition) >
                              1e-4f)
                {
                    return true;
                }
            }
            return false;
        };

    EnvironmentWindSettings responseBaseline =
        engine::scene::defaultEnvironmentWindSettings();
    EnvironmentWindSettings directionChanged = responseBaseline;
    directionChanged.direction = glm::vec2(0.0f, 1.0f);
    require(responseExists(responseBaseline, directionChanged, false),
            "WIND-002 motion must respond to the shared wind direction");
    EnvironmentWindSettings gustChanged = responseBaseline;
    gustChanged.gustStrength = 0.8f;
    gustChanged.gustFrequencyHz = 0.43f;
    require(responseExists(responseBaseline, gustChanged, false),
            "WIND-002 motion must respond to shared gust strength and frequency");
    EnvironmentWindSettings turbulenceChanged = responseBaseline;
    turbulenceChanged.turbulenceStrength = 1.0f;
    require(responseExists(responseBaseline, turbulenceChanged, false),
            "WIND-002 motion must respond to shared turbulence");
    EnvironmentWindSettings liftChanged = responseBaseline;
    liftChanged.verticalLift = 2.0f;
    require(responseExists(responseBaseline, liftChanged, true),
            "WIND-002 motion must respond to shared vertical lift");

    EnvironmentWindSettings extreme = responseBaseline;
    extreme.direction = glm::vec2(-0.37f, 0.91f);
    extreme.speed = 8.0f;
    extreme.strength = 2.0f;
    extreme.gustStrength = 1.0f;
    extreme.gustFrequencyHz = 2.0f;
    extreme.turbulenceStrength = 1.0f;
    extreme.verticalLift = 2.0f;
    constexpr std::array<float, 8> kSampleTimes{
        -1000.0f,
        -3.5f,
        0.0f,
        0.37f,
        4.2f,
        1000.0f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
    };
    for (const WindborneParticleCandidate& candidate : field)
    {
        for (float time : kSampleTimes)
        {
            const engine::scene::WindborneParticleSample sample =
                engine::scene::sampleWindborneParticle(candidate, particles,
                                                        extreme, time);
            const glm::vec3 offset = sample.worldPosition - candidate.anchorWorld;
            require(sample.active && std::isfinite(sample.worldPosition.x) &&
                        std::isfinite(sample.worldPosition.y) &&
                        std::isfinite(sample.worldPosition.z) &&
                        glm::length(offset) <= 5.0f,
                    "WIND-002 closed-loop samples must remain finite and bounded at the authored anchor");
        }
    }
}

void testWorldStateViewAliasesLiveState()
{
    engine::scene::SceneManager manager{};
    engine::render::LightingSettings lighting{};
    engine::render::ShadowSettings shadows{};
    engine::render::AmbientOcclusionSettings ao{};
    engine::render::PostFxSettings postFx{};
    engine::render::WaterSettings water{};
    engine::render::GlassSettings glass{};
    engine::render::VoxelDebugSettings voxelDebug{};
    engine::render::DiagnosticsSettings diagnostics{};
    engine::render::FramePacingSettings framePacing{};

    const engine::scene::WorldStateView view{
        manager.sceneConfig(), lighting, shadows,      ao,          postFx,
        water,                 glass,    voxelDebug,   diagnostics, framePacing};

    static_assert(
        std::is_const_v<std::remove_reference_t<decltype(view.sceneConfig)>> &&
            std::is_const_v<std::remove_reference_t<decltype(view.lighting)>> &&
            std::is_const_v<std::remove_reference_t<decltype(view.framePacing)>>,
        "WorldStateView members must be read-only references");

    const bool initialWater = view.sceneConfig.enableWater;
    manager.sceneConfigMutable().enableWater = !initialWater;
    require(view.sceneConfig.enableWater == !initialWater,
            "WorldStateView should observe SceneConfig writes made through the "
            "sceneConfigMutable door without rebinding");

    lighting.lightCount_ = 3;
    postFx.taaEnabled_ = true;
    diagnostics.viewMode_ = 4;
    framePacing.maxFpsLimit_ = 144.0f;
    require(view.lighting.lightCount_ == 3 && view.postFx.taaEnabled_ &&
                view.diagnostics.viewMode_ == 4 &&
                nearlyEqual(view.framePacing.maxFpsLimit_, 144.0f),
            "WorldStateView should observe settings-bucket writes without rebinding");

    FrameContext frame{};
    require(frame.worldState == nullptr, "FrameContext world state view should default to null");
    frame.worldState = &view;
    require(frame.worldState->lighting.lightCount_ == 3 &&
                frame.worldState->diagnostics.viewMode_ == 4,
            "FrameContext should carry the read-only WorldStateView for pass adoption");

    bool registrySawWorldState = false;
    engine::render::PassRegistry registry{};
    registry.add("world-state-probe", [&](const engine::render::RenderPassContext& context) {
        require(context.frame.worldState == &view,
                "RenderPassContext should expose FrameContext WorldStateView");
        registrySawWorldState = true;
    });
    VulkanContext ctx{};
    engine::render::FrameInputs inputs{};
    registry.recordAll(engine::render::RenderPassContext{ctx, frame, inputs});
    require(registrySawWorldState, "PassRegistry should invoke world-state probe");
}

void testSceneManagerCatalogLoadAndFallback()
{
    namespace fs = std::filesystem;

    const ReservedTemporaryDirectory temporaryDirectory(
        "dda-voxel-scene-manager-core-tests");
    const fs::path& root = temporaryDirectory.path();
    std::error_code ec;

    SceneConfig authored{};
    authored.name = "scene_manager_authored";
    authored.description = "SceneManager catalog test scene";
    authored.loadVoxelWorld = false;
    authored.loadOBBVolumes = false;
    authored.loadCloudScene = false;
    const fs::path authoredPath = root / "authored.json";
    require(authored.saveToFile(authoredPath),
            "SceneManager test should write an authored scene fixture");

    engine::scene::SceneManager manager;
    manager.setScenesRoot(root);
    manager.refreshSceneCatalog(false);
    require(manager.availableScenes().size() == 1,
            "SceneManager should own authored scene catalog scanning");
    require(manager.availableScenes()[0].valid &&
                manager.availableScenes()[0].displayName == authored.name,
            "SceneManager catalog entries should include loaded scene metadata");

    const engine::scene::SceneLoadResult directLoad =
        manager.loadSceneFromFile(authoredPath);
    require(directLoad.loaded && directLoad.config.name == authored.name,
            "SceneManager should load an authored scene file");
    require(manager.currentScenePath() == authoredPath.lexically_normal(),
            "SceneManager should own the current authored scene path");

    const engine::scene::SceneLoadResult initialLoad =
        manager.loadInitialScene("authored");
    require(initialLoad.loaded && initialLoad.config.name == authored.name &&
                !initialLoad.usedBuiltInFallback,
            "SceneManager initial load should resolve named authored scenes");

    fs::remove_all(root, ec);

    engine::scene::SceneManager fallbackManager;
    fallbackManager.setScenesRoot(root);
    const engine::scene::SceneLoadResult fallback =
        fallbackManager.loadInitialScene("missing_scene");
    require(fallback.loaded && fallback.usedBuiltInFallback &&
                fallback.config.name == "obb_test",
            "SceneManager should fall back to the built-in OBB scene");
    require(fallbackManager.currentScenePath().empty(),
            "SceneManager built-in fallback should clear the authored path");
}

void testSceneManagerCloudBuildLifecycle()
{
    struct ScopedJobSystem
    {
        JobSystem jobs;

        ScopedJobSystem() { jobs.start(1); }
        ~ScopedJobSystem() { jobs.stop(); }
    } scopedJobs;

    SceneConfig config{};
    config.cloudSeed = 77u;
    config.cloudAltitude = 88.0f;
    config.cloudCoverage = 0.5f;
    config.cloudScale = 24.0f;
    const glm::vec3 cameraPosition(10.0f, 4.0f, 30.0f);

    CloudSettings settings =
        engine::scene::SceneManager::buildCloudSettingsFromSceneConfig(
            config, cameraPosition);
    require(settings.seed == config.cloudSeed &&
                nearlyEqual(settings.position.y, config.cloudAltitude) &&
                nearlyEqual(settings.baseNoiseScale, config.cloudScale),
            "SceneManager should derive cloud build settings from scene config");
    require(nearlyEqual(settings.position.x,
                        cameraPosition.x -
                            static_cast<float>(settings.dims.x) * 0.5f) &&
                nearlyEqual(settings.position.z,
                            cameraPosition.z -
                                static_cast<float>(settings.dims.z) * 0.5f),
            "SceneManager cloud build settings should be camera-relative");

    settings.dims = glm::ivec3(8, 4, 8);
    engine::scene::SceneManager manager;
    const engine::scene::CloudBuildQueueResult queueResult =
        manager.requestCloudBuild(settings, "unit test", scopedJobs.jobs, 4u,
                                  12.0);
    require(queueResult.queued && queueResult.clearAutomationCommitObserved,
            "SceneManager should queue cloud builds and request automation reset");

    const engine::scene::CloudBuildPollResult beforeFrame =
        manager.pollPendingCloudBuild(4u, true);
    require(beforeFrame.status ==
                engine::scene::CloudBuildPollStatus::WaitingForFrame,
            "SceneManager should defer cloud commit until a later rendered frame");

    scopedJobs.jobs.waitIdle();
    const engine::scene::CloudBuildPollResult ready =
        manager.pollPendingCloudBuild(5u, true);
    require(ready.status == engine::scene::CloudBuildPollStatus::Ready &&
                ready.pending &&
                ready.pending->result.spec.dims == settings.dims,
            "SceneManager should expose ready CPU cloud builds for GPU commit");
    manager.clearPendingCloudBuild();
    require(manager.pollPendingCloudBuild(5u, true).status ==
                engine::scene::CloudBuildPollStatus::None,
            "SceneManager should clear committed cloud builds");

    manager.requestCloudBuild(settings, "unit test invalidate", scopedJobs.jobs,
                              5u, 13.0);
    manager.invalidatePendingCloudBuild("unit test invalidate");
    require(manager.pollPendingCloudBuild(6u, true).status ==
                engine::scene::CloudBuildPollStatus::None,
            "SceneManager should own cloud build invalidation");
    scopedJobs.jobs.waitIdle();
}

void testSceneManagerVolumeSceneEffectPlans()
{
    const engine::scene::VolumeSceneEffectPlan reload =
        engine::scene::SceneManager::planSceneReloadVolumeChange();
    require(reload.kind == engine::scene::VolumeSceneOperationKind::SceneReload &&
                std::string_view(reload.reason) == "scene reload",
            "SceneManager should name scene-reload volume plans");
    require(reload.invalidateCloudBuild && reload.shutdownVoxelWorld &&
                reload.resetCloudRuntimeState && reload.runInitVolumeScene,
            "SceneManager scene-reload plans should cover volume reinitialization");
    require(!reload.waitForInFlightFrame && !reload.requestCloudBuildOnSuccess &&
                !reload.markSceneObjectsDirty && !reload.markVoxelObjectsDirty,
            "SceneManager scene-reload plans should leave outer reload effects in App");
    require(reload.clearWaterVolumesOnFailure &&
                reload.updateWaterVolumesOnSuccess && reload.logFailure,
            "SceneManager scene-reload plans should cover water cleanup and failure logging");

    const engine::scene::VolumeSceneEffectPlan volume =
        engine::scene::SceneManager::planVolumeSceneRebuild();
    require(volume.kind == engine::scene::VolumeSceneOperationKind::VolumeRebuild &&
                std::string_view(volume.reason) == "volume scene rebuild",
            "SceneManager should name volume rebuild plans");
    require(volume.invalidateCloudBuild && volume.waitForInFlightFrame &&
                volume.shutdownVoxelWorld && volume.resetCloudRuntimeState &&
                volume.runInitVolumeScene,
            "SceneManager volume rebuild plans should cover full volume reinitialization");
    require(volume.clearAnimatedStateOnFailure &&
                volume.destroyFishbowlGlassOnFailure &&
                volume.clearModularGlassOnFailure,
            "SceneManager volume rebuild plans should preserve failure cleanup");
    require(volume.requestCloudBuildOnSuccess &&
                volume.rebuildAnimatedObjectsOnSuccess &&
                volume.rebuildFishbowlGlassOnSuccess &&
                volume.rebuildModularGlassOnSuccess &&
                volume.rebuildGlassObjectsAfter,
            "SceneManager volume rebuild plans should preserve post-rebuild effects");
    require(volume.markSceneObjectsDirty && volume.markVoxelObjectsDirty &&
                volume.resetRenderHistory && volume.logSuccess &&
                volume.logFailure,
            "SceneManager volume rebuild plans should preserve dirty flags and logging");

    const engine::scene::VolumeSceneEffectPlan procedural =
        engine::scene::SceneManager::planProceduralWorldRebuild(true);
    require(procedural.kind ==
                    engine::scene::VolumeSceneOperationKind::ProceduralWorldRebuild &&
                std::string_view(procedural.reason) ==
                    "procedural world rebuild",
            "SceneManager should name procedural world rebuild plans");
    require(procedural.invalidateCloudBuild && procedural.waitForInFlightFrame &&
                procedural.resetCloudRuntimeState &&
                procedural.runProceduralRegenerate,
            "SceneManager procedural rebuild plans should cover procedural regeneration");
    require(!procedural.shutdownVoxelWorld && !procedural.runInitVolumeScene,
            "SceneManager procedural rebuild plans should not use the volume init path");
    require(procedural.requestCloudBuildOnSuccess &&
                procedural.markProceduralDirtyFromResult &&
                procedural.markSceneObjectsDirty &&
                procedural.markVoxelObjectsDirty &&
                procedural.resetRenderHistory,
            "SceneManager procedural rebuild plans should preserve App result effects");

    const engine::scene::VolumeSceneEffectPlan fallback =
        engine::scene::SceneManager::planProceduralWorldRebuild(false);
    require(fallback.kind ==
                    engine::scene::VolumeSceneOperationKind::FallbackToVolumeRebuild &&
                fallback.fallbackToVolumeRebuild &&
                std::string_view(fallback.reason) == "volume scene rebuild",
            "SceneManager should fall back procedural rebuilds to volume rebuilds when disabled");
    require(!fallback.runProceduralRegenerate && !fallback.requestCloudBuildOnSuccess,
            "SceneManager procedural fallback plans should not duplicate volume effects");
}

void testSceneObjectBuilderBaseDrawLists()
{
    MeshGpu meshes[8]{};
    Material materials[8]{};
    auto makeObject = [&](int index) {
        RenderObject object{};
        object.mesh = &meshes[index];
        object.material = &materials[index];
        object.model = glm::mat4(static_cast<float>(index + 1));
        return object;
    };

    const std::vector<RenderObject> staticObjects{makeObject(0), makeObject(1)};
    const std::vector<RenderObject> animatedObjects{makeObject(2)};
    const std::vector<RenderObject> waterFoamObjects{makeObject(3)};
    const std::vector<RenderObject> voxelObjects{makeObject(4)};
    const std::vector<RenderObject> voxelShadowObjects{makeObject(5)};
    const std::vector<RenderObject> chunkBoundsObjects{makeObject(6)};
    const std::vector<RenderObject> volumeBoundsObjects{makeObject(7)};

    engine::scene::SceneObjectBuilderInputs inputs{};
    inputs.staticObjects = std::span<const RenderObject>(staticObjects);
    inputs.animatedObjects = std::span<const RenderObject>(animatedObjects);
    inputs.waterFoamObjects = std::span<const RenderObject>(waterFoamObjects);
    inputs.voxelObjects = std::span<const RenderObject>(voxelObjects);
    inputs.voxelShadowObjects = std::span<const RenderObject>(voxelShadowObjects);
    inputs.chunkBoundsVisibleObjects = std::span<const RenderObject>(chunkBoundsObjects);
    inputs.volumeBoundsVisibleObjects = std::span<const RenderObject>(volumeBoundsObjects);
    inputs.voxelVisible = true;
    inputs.chunkBoundsVisible = true;
    inputs.volumeBoundsVisible = true;
    inputs.overlayReserveHint = 4;

    std::vector<RenderObject> sceneObjects;
    std::vector<RenderObject> shadowObjects;
    engine::scene::SceneObjectBuilder::buildBaseDrawLists(inputs, sceneObjects,
                                                          shadowObjects);

    const std::vector<Material*> expectedSceneMaterials{
        &materials[0], &materials[1], &materials[2], &materials[3],
        &materials[4], &materials[6], &materials[7]};
    require(sceneObjects.size() == expectedSceneMaterials.size(),
            "SceneObjectBuilder should merge visible base scene objects");
    for (size_t i = 0; i < expectedSceneMaterials.size(); ++i)
    {
        require(sceneObjects[i].material == expectedSceneMaterials[i],
                "SceneObjectBuilder should preserve base scene object order");
    }

    const std::vector<Material*> expectedShadowMaterials{
        &materials[0], &materials[1], &materials[2], &materials[5]};
    require(shadowObjects.size() == expectedShadowMaterials.size(),
            "SceneObjectBuilder should merge shadow-casting base objects");
    for (size_t i = 0; i < expectedShadowMaterials.size(); ++i)
    {
        require(shadowObjects[i].material == expectedShadowMaterials[i],
                "SceneObjectBuilder should preserve base shadow object order");
    }

    inputs.voxelVisible = false;
    inputs.chunkBoundsVisible = false;
    inputs.volumeBoundsVisible = false;
    engine::scene::SceneObjectBuilder::buildBaseDrawLists(inputs, sceneObjects,
                                                          shadowObjects);

    require(sceneObjects.size() == 4,
            "SceneObjectBuilder should omit hidden voxel and bounds objects from scene");
    require(sceneObjects[0].material == &materials[0] &&
                sceneObjects[1].material == &materials[1] &&
                sceneObjects[2].material == &materials[2] &&
                sceneObjects[3].material == &materials[3],
            "SceneObjectBuilder should keep static, animated, and foam objects when extras are hidden");
    require(shadowObjects.size() == 3,
            "SceneObjectBuilder should omit voxel shadow objects when voxels are hidden");
    require(shadowObjects[0].material == &materials[0] &&
                shadowObjects[1].material == &materials[1] &&
                shadowObjects[2].material == &materials[2],
            "SceneObjectBuilder should keep static and animated shadow objects when voxels are hidden");
}

void testSceneObjectBuilderChunkBoundsObjects()
{
    MeshGpu mesh{};
    Material material{};

    const std::vector<glm::ivec3> coords{
        glm::ivec3(0, 0, 0), glm::ivec3(1, 0, -2), glm::ivec3(-1, 2, 3)};
    const glm::ivec3 dims(32, 16, 8);

    engine::scene::ChunkBoundsInputs inputs{};
    inputs.chunkCoords = std::span<const glm::ivec3>(coords);
    inputs.chunkDimensions = dims;
    inputs.mesh = &mesh;
    inputs.material = &material;

    std::vector<RenderObject> objects;
    engine::scene::SceneObjectBuilder::buildChunkBoundsObjects(inputs, objects);

    require(objects.size() == coords.size(),
            "buildChunkBoundsObjects should emit one bounds object per chunk");
    for (size_t i = 0; i < coords.size(); ++i)
    {
        require(objects[i].mesh == &mesh && objects[i].material == &material,
                "buildChunkBoundsObjects should assign the shared bounds mesh/material");
        const glm::vec3 expectedOrigin(coords[i] * dims);
        require(nearlyEqual(objects[i].model[3].x, expectedOrigin.x) &&
                    nearlyEqual(objects[i].model[3].y, expectedOrigin.y) &&
                    nearlyEqual(objects[i].model[3].z, expectedOrigin.z),
                "buildChunkBoundsObjects should translate each chunk to its world origin");
        require(nearlyEqual(objects[i].model[0][0], static_cast<float>(dims.x)) &&
                    nearlyEqual(objects[i].model[1][1], static_cast<float>(dims.y)) &&
                    nearlyEqual(objects[i].model[2][2], static_cast<float>(dims.z)),
                "buildChunkBoundsObjects should scale each chunk by its dimensions");
    }

    inputs.mesh = nullptr;
    engine::scene::SceneObjectBuilder::buildChunkBoundsObjects(inputs, objects);
    require(objects.empty(),
            "buildChunkBoundsObjects should emit nothing without a bounds mesh");
}

void testSceneObjectBuilderVolumeBoundsObjects()
{
    MeshGpu mesh{};
    Material material{};

    // Frustum that accepts any aabb overlapping the box [0,8]^3.
    Frustum frustum{};
    frustum.planes[0] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);   // x >= 0
    frustum.planes[1] = glm::vec4(-1.0f, 0.0f, 0.0f, 8.0f);  // x <= 8
    frustum.planes[2] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);   // y >= 0
    frustum.planes[3] = glm::vec4(0.0f, -1.0f, 0.0f, 8.0f);  // y <= 8
    frustum.planes[4] = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);   // z >= 0
    frustum.planes[5] = glm::vec4(0.0f, 0.0f, -1.0f, 8.0f);  // z <= 8

    engine::scene::VolumeBoundsInstance inside{};
    inside.visible = true;
    inside.aabbMin = glm::vec3(1.0f, 1.0f, 1.0f);
    inside.aabbMax = glm::vec3(2.0f, 2.0f, 2.0f);
    inside.dimensions = glm::ivec3(3, 4, 5);
    inside.worldFromLocal = glm::mat4(1.0f);
    inside.worldFromLocal[3] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    engine::scene::VolumeBoundsInstance outside{};
    outside.visible = true;
    outside.aabbMin = glm::vec3(100.0f, 100.0f, 100.0f);
    outside.aabbMax = glm::vec3(101.0f, 101.0f, 101.0f);
    outside.dimensions = glm::ivec3(1, 1, 1);
    outside.worldFromLocal = glm::mat4(1.0f);

    engine::scene::VolumeBoundsInstance hidden = inside;
    hidden.visible = false;

    const std::vector<engine::scene::VolumeBoundsInstance> instances{inside, outside,
                                                                     hidden};

    engine::scene::VolumeBoundsInputs inputs{};
    inputs.instances = std::span<const engine::scene::VolumeBoundsInstance>(instances);
    inputs.frustum = frustum;
    inputs.mesh = &mesh;
    inputs.material = &material;

    std::vector<RenderObject> objects;
    engine::scene::SceneObjectBuilder::buildVolumeBoundsObjects(inputs, objects);

    require(objects.size() == 1,
            "buildVolumeBoundsObjects should keep only visible, in-frustum volumes");
    require(objects[0].mesh == &mesh && objects[0].material == &material,
            "buildVolumeBoundsObjects should assign the shared bounds mesh/material");
    require(nearlyEqual(objects[0].model[3].x, 1.0f) &&
                nearlyEqual(objects[0].model[3].y, 1.0f) &&
                nearlyEqual(objects[0].model[3].z, 1.0f),
            "buildVolumeBoundsObjects should preserve the volume world transform");
    require(nearlyEqual(objects[0].model[0][0], 3.0f) &&
                nearlyEqual(objects[0].model[1][1], 4.0f) &&
                nearlyEqual(objects[0].model[2][2], 5.0f),
            "buildVolumeBoundsObjects should scale the bounds by volume dimensions");

    inputs.mesh = nullptr;
    engine::scene::SceneObjectBuilder::buildVolumeBoundsObjects(inputs, objects);
    require(objects.empty(),
            "buildVolumeBoundsObjects should emit nothing without a bounds mesh");
}

void testChunkGridWorldAccessAcrossNegativeCoordinates()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(1, 1, 1), glm::ivec3(-1, 0, -1));

    require(grid.setVoxelWorld(-1, 0, -1, BLOCK_STONE),
            "ChunkGrid should allow writes in negative world coordinates");
    require(grid.getVoxelWorld(-1, 0, -1) == BLOCK_STONE,
            "ChunkGrid should read back negative world coordinates correctly");
    require(grid.setVoxelWorld(-32, 31, -32, BLOCK_LOG),
            "ChunkGrid should allow writes at the negative chunk boundary");
    require(grid.getVoxelWorld(-32, 31, -32) == BLOCK_LOG,
            "ChunkGrid should read back the negative chunk boundary correctly");
    require(!grid.setVoxelWorld(-1, 0, -1, BLOCK_STONE),
            "ChunkGrid should report no change when the voxel value is unchanged");
    require(!grid.setVoxelWorld(0, 0, 0, BLOCK_STONE),
            "ChunkGrid should reject writes outside the owned chunk bounds");
    require(grid.getVoxelWorld(0, 0, 0) == BLOCK_AIR,
            "ChunkGrid should return air outside the owned chunk bounds");
}

void testChunkGridCreatePreservingBlocksKeepsExistingCells()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(1, 1, 1), glm::ivec3(0, 0, 0));

    require(grid.setVoxelWorld(1, 2, 3, BLOCK_STONE),
            "Preserve test should seed a first block");
    require(grid.setVoxelWorld(31, 31, 31, BLOCK_LOG),
            "Preserve test should seed a chunk-edge block");

    grid.createPreservingBlocks(glm::ivec3(3, 1, 3), glm::ivec3(-1, 0, -1));

    require(grid.origin() == glm::ivec3(-1, 0, -1) &&
                grid.dims() == glm::ivec3(3, 1, 3),
            "Preserving recreate should apply the requested chunk bounds");
    require(grid.getVoxelWorld(1, 2, 3) == BLOCK_STONE,
            "Preserving recreate should keep existing interior blocks");
    require(grid.getVoxelWorld(31, 31, 31) == BLOCK_LOG,
            "Preserving recreate should keep existing chunk-edge blocks");
    require(grid.setVoxelWorld(-1, 0, -1, BLOCK_LEAF),
            "Preserving recreate should allow writes in newly added chunks");
}

void testVoxelRaycastHitAndMissBehavior()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(1, 1, 1), glm::ivec3(0, 0, 0));
    require(grid.setVoxelWorld(3, 4, 5, BLOCK_STONE),
            "Raycast test setup should place a solid voxel");

    const RayHit hit =
        voxelRaycast(grid, glm::vec3(0.5f, 4.5f, 5.5f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f);
    require(hit.hit, "Voxel raycast should report a hit for a directly visible solid voxel");
    require(hit.voxel.x == 3 && hit.voxel.y == 4 && hit.voxel.z == 5,
            "Voxel raycast should report the correct voxel coordinates");
    require(hit.prevVoxel.x == 2 && hit.prevVoxel.y == 4 && hit.prevVoxel.z == 5,
            "Voxel raycast should report the previous empty voxel before the hit");
    require(nearlyEqual(hit.t, 2.5f), "Voxel raycast hit distance should match grid traversal");

    const RayHit miss =
        voxelRaycast(grid, glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(-1.0f, 0.0f, 0.0f), 4.0f);
    require(!miss.hit, "Voxel raycast should miss when no solid voxel is along the ray");

    const RayHit zeroDir =
        voxelRaycast(grid, glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.0f), 4.0f);
    require(!zeroDir.hit, "Voxel raycast should ignore zero-length direction vectors");
}

void testCloudGenerationIsDeterministic()
{
    CloudSettings settings{};
    settings.seed = 42u;
    settings.dims = glm::ivec3(48, 16, 48);
    settings.position = glm::vec3(0.0f, 100.0f, 0.0f);
    settings.baseNoiseScale = 24.0f;
    settings.detailNoiseScale = 6.0f;
    settings.densityThreshold = 0.42f;

    const CloudBuildResult first = CloudScene::buildVolumeData(settings);
    const CloudBuildResult second = CloudScene::buildVolumeData(settings);

    require(first.spec.dims == second.spec.dims,
            "Cloud generation should preserve dimensions for identical settings");
    require(first.spec.position == second.spec.position,
            "Cloud generation should preserve position for identical settings");
    require(first.filledVoxelCount == second.filledVoxelCount,
            "Cloud generation filled voxel count should be deterministic");
    require(first.voxels == second.voxels,
            "Cloud generation voxel payload should be deterministic for identical settings");

    settings.seed = 1337u;
    const CloudBuildResult differentSeed = CloudScene::buildVolumeData(settings);
    require(first.voxels != differentSeed.voxels ||
                first.filledVoxelCount != differentSeed.filledVoxelCount,
            "Changing the cloud seed should change the generated cloud payload");
}

void testModularGlassMesherBuildsSingleVoxelShell()
{
    constexpr uint8_t kGlass = 3;
    const glm::ivec3 dims(1, 1, 1);
    std::vector<uint8_t> voxels(1, kGlass);

    const engine::ModularGlassMesh mesh = engine::buildModularGlassMesh(
        engine::ModularGlassMeshInput{dims, &voxels, {kGlass}});

    require(mesh.occupiedVoxelCount == 1,
            "Modular glass mesher should count one glass voxel");
    require(mesh.quadCount == 6,
            "Single modular glass voxel should produce six exterior quads");
    require(mesh.vertices.size() == 24,
            "Single modular glass voxel should produce four vertices per quad");
    require(mesh.indices.size() == 36,
            "Single modular glass voxel should produce two triangles per quad");
}

void testModularGlassMesherMergesAdjacentVoxels()
{
    constexpr uint8_t kGlass = 3;
    const glm::ivec3 dims(2, 1, 1);
    std::vector<uint8_t> voxels(2, kGlass);

    const engine::ModularGlassMesh mesh = engine::buildModularGlassMesh(
        engine::ModularGlassMeshInput{dims, &voxels, {kGlass}});

    require(mesh.occupiedVoxelCount == 2,
            "Modular glass mesher should count both adjacent glass voxels");
    require(mesh.quadCount == 6,
            "Two adjacent modular glass voxels should merge into one rectangular prism");
    require(mesh.indices.size() == 36,
            "Merged adjacent modular glass voxels should not emit the shared internal face");
}

void testModularGlassMesherDoesNotMergeAcrossDifferentGlassMaterials()
{
    constexpr uint8_t kGlassA = 3;
    constexpr uint8_t kGlassB = 12;
    const glm::ivec3 dims(2, 1, 1);
    std::vector<uint8_t> voxels = {kGlassA, kGlassB};

    const engine::ModularGlassMesh mesh = engine::buildModularGlassMesh(
        engine::ModularGlassMeshInput{dims, &voxels, {kGlassA, kGlassB}});

    require(mesh.occupiedVoxelCount == 2,
            "Modular glass mesher should count all configured glass materials");
    require(mesh.quadCount == 10,
            "Different adjacent glass materials should hide the shared face but keep separate exterior quads");
}

bool rectNear(const ui::UiRect& rect, float x, float y, float w, float h)
{
    return nearlyEqual(rect.x, x) && nearlyEqual(rect.y, y) &&
           nearlyEqual(rect.width, w) && nearlyEqual(rect.height, h);
}

bool rectInside(const ui::UiRect& inner, const ui::UiRect& outer,
                float epsilon = 0.5f)
{
    return inner.width >= 0.0f && inner.height >= 0.0f &&
           inner.x + epsilon >= outer.x && inner.y + epsilon >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width + epsilon &&
           inner.y + inner.height <= outer.y + outer.height + epsilon;
}

bool rectsOverlap(const ui::UiRect& lhs, const ui::UiRect& rhs)
{
    return lhs.x < rhs.x + rhs.width && lhs.x + lhs.width > rhs.x &&
           lhs.y < rhs.y + rhs.height && lhs.y + lhs.height > rhs.y;
}

void testRuntimeUiPackColorPremultipliesAndClamps()
{
    require(ui::packColor(1.0f, 0.0f, 0.0f, 1.0f) == 0xff0000ffu,
            "packColor should pack opaque red as little-endian RGBA");
    require(ui::packColor(1.0f, 1.0f, 1.0f, 0.5f) == 0x80808080u,
            "packColor should premultiply RGB channels by alpha");
    require(ui::packColor(2.0f, -1.0f, 0.0f, 2.0f) == 0xff0000ffu,
            "packColor should clamp colour and alpha into the [0,1] range");
    require((ui::packColor(0.5f, 0.5f, 0.5f, 0.0f) >> 24u) == 0u,
            "packColor should encode zero alpha in the high byte");
}

void testRuntimeUiThemePreservesLegacyPalette()
{
    const ui::UiTheme& theme = ui::defaultUiTheme();

    // themeColor must route through packColor so themed output stays
    // byte-identical with the legacy literals it replaced.
    require(ui::themeColor(theme.accent, 0.95f) ==
                ui::packColor(0.04f, 0.78f, 0.88f, 0.95f),
            "themeColor(accent) should match the legacy cyan accent literal");
    require(ui::themeColor(theme.panelInk, 0.96f) ==
                ui::packColor(0.015f, 0.020f, 0.026f, 0.96f),
            "themeColor(panelInk) should match the legacy panel background literal");
    require(ui::themeColor(theme.foreground, 0.92f) ==
                ui::packColor(0.92f, 0.96f, 0.88f, 0.92f),
            "themeColor(foreground) should match the legacy text literal");
    require(ui::themeColor(theme.accentAlt, 1.0f) ==
                ui::packColor(0.96f, 0.12f, 0.42f, 1.0f),
            "themeColor(accentAlt) should match the legacy magenta accent literal");

    // Default button style must keep producing the legacy packed values.
    const ui::UiButtonStyle buttonStyle{};
    require(buttonStyle.backgroundColor == ui::packColor(0.08f, 0.11f, 0.15f, 1.0f),
            "default button background should match the legacy literal");
    require(buttonStyle.pressedColor == ui::packColor(0.04f, 0.78f, 0.88f, 0.95f),
            "default button pressed color should match the legacy literal");
    require(buttonStyle.disabledTextColor == ui::packColor(0.48f, 0.52f, 0.50f, 0.76f),
            "default button disabled text should match the legacy literal");

    // typography tokens lock the text sizes the overlay-smoke needles depend on.
    require(nearlyEqual(theme.titleTextHeight, 28.0f), "title text height token");
    require(nearlyEqual(theme.headingTextHeight, 20.0f), "heading text height token");
    require(nearlyEqual(theme.bodyTextHeight, 16.0f), "body text height token");
    require(nearlyEqual(theme.captionTextHeight, 14.0f), "caption text height token");

    // animation timing tokens exist for the tween slice and must be positive.
    require(theme.fadeInSeconds > 0.0f && theme.fadeOutSeconds > 0.0f &&
                theme.slideSeconds > 0.0f,
            "animation timing tokens should be positive");
}

void testRuntimeUiFishListBindingBuildsRows()
{
    ui::UiTree tree = ui::makeOverlaySmokeTree();
    std::vector<ui::UiFishListEntry> entries{{1, "FISH 1", true}, {2, "FISH 2"}};
    ui::setOverlaySmokeFishList(tree, entries);

    const ui::UiElement* row = ui::findElementById(tree.root, "fish_row_1");
    require(row != nullptr && row->action == "focus_on_fish" &&
                row->actionPayload == "1" && row->text.value == "FISH 1",
            "fish row should carry the focus action with the fish id payload");
    require(row->style.color == ui::themeColor(ui::defaultUiTheme().accent, 0.78f),
            "focused fish row should use the accent highlight color");
    require(ui::findElementById(tree.root, "fish_row_2") != nullptr,
            "second fish row should exist");
    const ui::UiElement* releaseButton =
        ui::findElementById(tree.root, ui::kFishFocusReleaseButtonId);
    require(releaseButton != nullptr &&
                releaseButton->action == std::string(ui::kReleaseFishFocusAction) &&
                releaseButton->enabled,
            "release button should enable while a fish is focused");
    require(releaseButton->style.color ==
                ui::themeColor(ui::defaultUiTheme().accentAlt, 0.88f),
            "enabled release button should use an active accent color");

    entries.pop_back();
    entries.front().focused = false;
    ui::setOverlaySmokeFishList(tree, entries);
    require(ui::findElementById(tree.root, "fish_row_2") == nullptr,
            "removed fish should drop its row on rebuild");
    releaseButton = ui::findElementById(tree.root, ui::kFishFocusReleaseButtonId);
    require(releaseButton != nullptr && !releaseButton->enabled,
            "release button should disable when no fish is focused");
    require(releaseButton->style.disabledColor ==
                ui::themeColor(ui::defaultUiTheme().buttonDisabledInk, 0.52f),
            "disabled release button should stay visually subdued");

    // regression: the default roster is now seven entries (six fish + the
    // axolotl); the panel must grow so the last row is not clipped away.
    std::vector<ui::UiFishListEntry> roster;
    for (uint64_t id = 1; id <= 7; ++id)
    {
        roster.push_back({id, id == 7 ? std::string("AXOLOTL 7")
                                      : "FISH " + std::to_string(id)});
    }
    ui::setOverlaySmokeFishList(tree, roster);
    const ui::UiElement* panel = ui::findElementById(tree.root, ui::kFishListPanelId);
    const ui::UiElement* content =
        ui::findElementById(tree.root, ui::kFishListContentId);
    const ui::UiElement* axolotlRow = ui::findElementById(tree.root, "fish_row_7");
    require(panel != nullptr && content != nullptr && axolotlRow != nullptr,
            "seven-entry roster should build a row for the axolotl");
    require(axolotlRow->rect.y + axolotlRow->rect.height <= content->rect.height + 0.5f,
            "the panel should grow so the axolotl row is not clipped");
    require(panel->rect.height >
                content->rect.y + axolotlRow->rect.y + axolotlRow->rect.height,
            "the panel encloses the full roster");

    ui::OverlaySmokeScreenController controller;
    require(controller.setFishList({{1, "FISH 1"}}),
            "first fish list should report a change");
    require(!controller.setFishList({{1, "FISH 1"}}),
            "identical fish list should not report a change");
    require(controller.setFishList({{1, "FISH 1", true}}),
            "focused fish state should report a change");
    require(controller.setFishList({{1, "FISH 1"}, {2, "FISH 2"}}),
            "grown fish list should report a change");
    require(controller.fishListCount() == 2, "fish list count should track entries");
}

void testRuntimeUiAxolotlVersusLabelBuildsFlagsAndTypedText()
{
    ui::UiTree tree = ui::makeOverlaySmokeTree();

    ui::UiFishWorldLabelEntry versus{};
    versus.id = 7;
    versus.label = "MEXICO VS|"; // mid-typing substring + cursor supplied by the caller
    versus.screenX = 640.0f;
    versus.screenY = 360.0f;
    versus.focused = true;
    versus.axolotlVersus = true;
    std::vector<ui::UiFishWorldLabelEntry> entries{versus};
    require(ui::setOverlaySmokeFishWorldLabels(tree, entries),
            "versus label entry should build a world label");

    const ui::UiElement* label = ui::findElementById(tree.root, "fish_world_label_7");
    require(label != nullptr, "focused axolotl should get a world label bubble");

    const ui::UiElement* mxGreen =
        ui::findElementById(tree.root, "fish_world_label_7_mx_green");
    const ui::UiElement* enField =
        ui::findElementById(tree.root, "fish_world_label_7_en_field");
    const ui::UiElement* crossH =
        ui::findElementById(tree.root, "fish_world_label_7_en_cross_h");
    const ui::UiElement* crossV =
        ui::findElementById(tree.root, "fish_world_label_7_en_cross_v");
    require(mxGreen != nullptr && enField != nullptr,
            "versus bubble should carry both the Mexico and England flags");
    require(crossH != nullptr && crossV != nullptr,
            "England flag should carry the St George's cross bars");
    require(mxGreen->rect.x < enField->rect.x,
            "Mexico flag sits left of the England flag");

    const ui::UiElement* vsText = ui::findElementById(tree.root, "fish_world_label_7_vs");
    require(vsText != nullptr && vsText->text.value == "VS",
            "the bubble shows VS between the flags");
    const ui::UiElement* banner =
        ui::findElementById(tree.root, "fish_world_label_7_text");
    require(banner != nullptr && banner->text.value == "MEXICO VS|",
            "the typing banner renders the caller-supplied substring");

    // a focused non-versus fish keeps the viva mexico celebration bubble.
    ui::UiFishWorldLabelEntry viva{};
    viva.id = 3;
    viva.label = "FISH 3";
    viva.screenX = 400.0f;
    viva.screenY = 300.0f;
    viva.focused = true;
    entries.assign({viva});
    require(ui::setOverlaySmokeFishWorldLabels(tree, entries),
            "swapping entries should rebuild the label layer");
    require(ui::findElementById(tree.root, "fish_world_label_3_flag_green") != nullptr,
            "focused fish without versus keeps the Mexico celebration flag");
    require(ui::findElementById(tree.root, "fish_world_label_3_en_field") == nullptr,
            "focused fish without versus gets no England flag");
}

void testRuntimeUiFishWorldLabelsBuildScreenLayer()
{
    ui::UiTree tree = ui::makeOverlaySmokeTree();
    require(ui::setOverlaySmokeViewport(tree, 800.0f, 600.0f),
            "viewport setup should resize the fish label layer");

    std::vector<ui::UiFishWorldLabelEntry> labels{
        {1, "FISH 1", 700.0f, 500.0f, false},
        {2, "FISH 2", 10.0f, 10.0f, true},
    };
    require(ui::setOverlaySmokeFishWorldLabels(tree, labels),
            "first fish world label sync should report changed retained state");
    ui::computeLayout(tree.root);

    const ui::UiElement* layer =
        ui::findElementById(tree.root, ui::kFishWorldLabelLayerId);
    const ui::UiElement* labelA =
        ui::findElementById(tree.root, "fish_world_label_1");
    const ui::UiElement* labelAText =
        ui::findElementById(tree.root, "fish_world_label_1_text");
    const ui::UiElement* labelB =
        ui::findElementById(tree.root, "fish_world_label_2");
    const ui::UiElement* labelBFlagGreen =
        ui::findElementById(tree.root, "fish_world_label_2_flag_green");
    const ui::UiElement* labelBText =
        ui::findElementById(tree.root, "fish_world_label_2_text");

    require(layer != nullptr && layer->children.size() == 2 &&
                rectNear(layer->computedRect, 0.0f, 0.0f, 800.0f, 600.0f),
            "fish world label layer should match the viewport");
    require(labelA != nullptr && !labelA->enabled &&
                rectNear(labelA->computedRect, 654.0f, 468.0f, 92.0f, 24.0f),
            "centered fish label should anchor above its projected screen point");
    require(labelAText != nullptr && labelAText->text.value == "FISH 1",
            "fish world label should expose readable text");
    // the focused fish gets the larger celebration bubble (8-bit mexico flag +
    // "viva mexico!" banner), so it clamps using the focused box dimensions.
    require(labelB != nullptr &&
                rectNear(labelB->computedRect, 8.0f, 8.0f, 132.0f, 50.0f),
            "edge fish labels should clamp inside the viewport");
    require(labelB->style.color == ui::themeColor(ui::defaultUiTheme().panelInk, 0.86f),
            "focused fish label should use the celebration background color");
    require(labelBFlagGreen != nullptr,
            "focused fish label should include the 8-bit Mexico flag");
    require(labelBText != nullptr &&
                labelBText->text.value == "\xC2\xA1VIVA M\xC3\xA9XICO!",
            "focused fish label should show the Viva Mexico banner");

    const ui::UiHitResult labelHit = ui::hitTest(*layer, 700.0f, 480.0f);
    require(!labelHit.hit,
            "world-space fish labels should not capture runtime UI input");

    require(!ui::setOverlaySmokeFishWorldLabels(tree, labels),
            "identical fish world labels should not report changed retained state");
    labels.clear();
    require(ui::setOverlaySmokeFishWorldLabels(tree, labels),
            "clearing fish world labels should report changed retained state");
    layer = ui::findElementById(tree.root, ui::kFishWorldLabelLayerId);
    require(layer != nullptr && layer->children.empty(),
            "clearing fish world labels should remove retained label children");

    labels = {
        {1, "FISH 1", 400.0f, 300.0f, false},
        {2, "FISH 2", 410.0f, 306.0f, false},
        {3, "FISH 3", 404.0f, 304.0f, true},
    };
    require(ui::setOverlaySmokeFishWorldLabels(tree, labels),
            "crowded fish world labels should still report changed retained state");
    ui::computeLayout(tree.root);
    layer = ui::findElementById(tree.root, ui::kFishWorldLabelLayerId);
    const ui::UiElement* focusedLabel =
        ui::findElementById(tree.root, "fish_world_label_3");
    require(layer != nullptr && layer->children.size() == 1 && focusedLabel != nullptr,
            "crowded fish world labels should keep the focused label and suppress overlaps");
}

void testRuntimeUiMainMenuScreenFactoryBuildsExpectedLayout()
{
    require(ui::isRuntimeUiScreenRegistered(ui::kMainMenuScreenId),
            "main menu screen should be registered");
    require(ui::isRuntimeUiScreenRegistered(ui::kMainMenuOptionsScreenId),
            "main menu options screen should be registered");

    ui::UiTree menu = ui::makeMainMenuScreenTree();
    ui::computeLayout(menu.root);
    const ui::UiElement* backdrop =
        ui::findElementById(menu.root, "main_menu_backdrop");
    const ui::UiElement* panel = ui::findElementById(menu.root, "main_menu_panel");
    const ui::UiElement* start =
        ui::findElementById(menu.root, ui::kMainMenuStartButtonId);
    const ui::UiElement* options =
        ui::findElementById(menu.root, ui::kMainMenuOptionsButtonId);
    const ui::UiElement* title = ui::findElementById(menu.root, "main_menu_title");
    const ui::UiElement* schoolStatus =
        ui::findElementById(menu.root, "main_menu_status_school");
    const ui::UiElement* waterStatus =
        ui::findElementById(menu.root, "main_menu_status_water");
    const ui::UiElement* buildStatus =
        ui::findElementById(menu.root, "main_menu_status_build");
    require(menu.root.id == "ui_main_menu_root" && panel != nullptr &&
                start != nullptr && options != nullptr && title != nullptr &&
                backdrop != nullptr,
            "main menu should build stable retained ids");
    require(schoolStatus == nullptr && waterStatus == nullptr &&
                buildStatus == nullptr,
            "main menu should not expose HUD status rows");
    require(rectNear(backdrop->computedRect, 0.0f, 0.0f, 1280.0f, 720.0f),
            "main menu backdrop should start at the design viewport size");
    require(rectNear(panel->computedRect, 72.0f, 84.0f, 456.0f, 320.0f),
            "main menu panel rect should remain stable");
    require(rectNear(start->computedRect, 116.0f, 234.0f, 332.0f, 40.0f),
            "main menu start button rect should remain stable");
    require(start->action == ui::kStartAquariumAction &&
                start->text.value == "START AQUARIUM",
            "main menu start should expose the start aquarium action");
    require(rectNear(options->computedRect, 116.0f, 290.0f, 332.0f, 32.0f),
            "main menu options button rect should remain stable");
    require(options->action == ui::kOpenMainMenuOptionsAction,
            "main menu options should expose the options action");

    require(ui::setMainMenuViewport(menu, 1920.0f, 1080.0f),
            "main menu viewport application should report layout-affecting changes");
    ui::computeLayout(menu.root);
    backdrop = ui::findElementById(menu.root, "main_menu_backdrop");
    panel = ui::findElementById(menu.root, "main_menu_panel");
    require(rectNear(menu.root.computedRect, 0.0f, 0.0f, 1920.0f, 1080.0f) &&
                backdrop != nullptr &&
                rectNear(backdrop->computedRect, 0.0f, 0.0f, 1920.0f, 1080.0f),
            "main menu root and backdrop should expand to the runtime viewport");
    require(panel != nullptr &&
                rectNear(panel->computedRect, 72.0f, 84.0f, 456.0f, 320.0f),
            "main menu panel should keep its designed placement after viewport resize");

    ui::UiTree optionsTree = ui::makeMainMenuOptionsScreenTree();
    ui::computeLayout(optionsTree.root);
    const ui::UiElement* optionsBackdrop =
        ui::findElementById(optionsTree.root, "main_menu_options_backdrop");
    const ui::UiElement* optionsPanel =
        ui::findElementById(optionsTree.root, "main_menu_options_panel");
    const ui::UiElement* labelsToggle =
        ui::findElementById(optionsTree.root, ui::kMainMenuOptionLabelsButtonId);
    const ui::UiElement* pixelStatus =
        ui::findElementById(optionsTree.root, "main_menu_option_pixel");
    const ui::UiElement* back =
        ui::findElementById(optionsTree.root, ui::kMainMenuOptionsBackButtonId);
    require(optionsTree.root.id == "ui_main_menu_options_root" &&
                optionsPanel != nullptr && back != nullptr && optionsBackdrop != nullptr &&
                labelsToggle != nullptr && pixelStatus != nullptr,
            "main menu options should build stable retained ids");
    require(rectNear(optionsPanel->computedRect, 430.0f, 170.0f, 420.0f, 300.0f),
            "main menu options panel rect should remain stable");
    require(rectNear(labelsToggle->computedRect, 468.0f, 262.0f, 330.0f, 28.0f) &&
                labelsToggle->action == ui::kToggleFishWorldLabelsAction &&
                labelsToggle->text.value == "LABELS: VISIBLE",
            "main menu options labels toggle should expose its state and action");
    require(rectNear(back->computedRect, 570.0f, 408.0f, 140.0f, 32.0f),
            "main menu options back button rect should remain stable");
    require(back->action == ui::kCloseMainMenuOptionsAction,
            "main menu options back should expose the close action");
    ui::UiMainMenuOptionsState hiddenLabelState{};
    hiddenLabelState.fishWorldLabelsVisible = false;
    require(ui::setMainMenuOptionsState(optionsTree, hiddenLabelState),
            "main menu options state sync should report label toggle changes");
    labelsToggle =
        ui::findElementById(optionsTree.root, ui::kMainMenuOptionLabelsButtonId);
    require(labelsToggle != nullptr && labelsToggle->text.value == "LABELS: HIDDEN",
            "main menu options labels toggle should reflect hidden state");

    require(ui::setMainMenuViewport(optionsTree, 1920.0f, 1080.0f),
            "main menu options viewport application should report layout-affecting changes");
    ui::computeLayout(optionsTree.root);
    optionsBackdrop = ui::findElementById(optionsTree.root, "main_menu_options_backdrop");
    optionsPanel = ui::findElementById(optionsTree.root, "main_menu_options_panel");
    require(rectNear(optionsTree.root.computedRect, 0.0f, 0.0f, 1920.0f, 1080.0f) &&
                optionsBackdrop != nullptr &&
                rectNear(optionsBackdrop->computedRect, 0.0f, 0.0f, 1920.0f,
                         1080.0f),
            "main menu options root and backdrop should expand to the runtime viewport");
    require(optionsPanel != nullptr &&
                rectNear(optionsPanel->computedRect, 430.0f, 170.0f, 420.0f, 300.0f),
            "main menu options panel should keep its designed placement after viewport resize");
}

void testRuntimeUiScreenStackPushPopAndModality()
{
    ui::UiContext context;
    require(context.setActiveScreen(ui::kOverlaySmokeScreenId),
            "overlay smoke screen should register as the base screen");
    require(context.ensureActiveTree() != nullptr, "base tree should build");
    require(context.overlayCount() == 0 &&
                context.topScreenId() == ui::kOverlaySmokeScreenId,
            "with no overlays the base screen is on top");

    require(!context.pushScreen("unknown_screen"),
            "unregistered screens should not push");
    require(context.pushScreen(ui::kPauseScreenId), "pause screen should push");
    require(context.overlayCount() == 1 &&
                context.topScreenId() == ui::kPauseScreenId,
            "pushed pause screen should be on top");

    ui::UiTree* top = context.topTree();
    require(top != nullptr && top->root.id == "ui_pause_smoke_root",
            "topTree should return the pause overlay tree");
    const ui::UiElement* resume =
        ui::findElementById(top->root, ui::kPauseResumeButtonId);
    const ui::UiElement* pauseOptions =
        ui::findElementById(top->root, ui::kPauseOptionsButtonId);
    const ui::UiElement* pauseMainMenu =
        ui::findElementById(top->root, ui::kPauseMainMenuButtonId);
    const ui::UiElement* pauseSubtitle =
        ui::findElementById(top->root, "pause_subtitle");
    const ui::UiElement* pauseStatus =
        ui::findElementById(top->root, "pause_status_school");
    require(resume != nullptr && pauseOptions != nullptr && pauseMainMenu != nullptr,
            "pause overlay should expose resume, options, and main menu actions");
    require(pauseSubtitle == nullptr && pauseStatus == nullptr,
            "pause overlay should avoid decorative status text");
    require(rectNear(resume->computedRect, 544.0f, 337.0f, 192.0f, 30.0f) &&
                resume->action == ui::kResumeGameAction,
            "pause resume button should be laid out and dispatch resume");
    require(rectNear(pauseOptions->computedRect, 544.0f, 379.0f, 192.0f, 30.0f) &&
                pauseOptions->action == ui::kOpenMainMenuOptionsAction,
            "pause options button should be laid out and dispatch options");
    require(rectNear(pauseMainMenu->computedRect, 544.0f, 421.0f, 192.0f, 30.0f) &&
                pauseMainMenu->action == ui::kReturnMainMenuAction,
            "pause main menu button should be laid out and dispatch return");
    require(ui::setPauseScreenViewport(*top, 1920.0f, 1080.0f),
            "pause viewport application should report layout-affecting changes");
    ui::computeLayout(top->root);
    const ui::UiElement* pausePanel =
        ui::findElementById(top->root, "pause_panel");
    require(pausePanel != nullptr &&
                rectNear(pausePanel->computedRect, 800.0f, 435.0f, 320.0f, 210.0f),
            "pause panel should center in the runtime viewport");

    require(context.overlayTrees().size() == 1,
            "overlayTrees should expose the pushed overlay for painting");
    require(context.popScreen(), "pop should succeed with an overlay");
    require(!context.popScreen(), "pop should fail with no overlays");
    require(context.topScreenId() == ui::kOverlaySmokeScreenId,
            "after pop the base screen is on top again");
}

void testRuntimeUiCollectionCodexBuildsOwnedAdvisoryView()
{
    require(ui::isRuntimeUiScreenRegistered(ui::kCollectionCodexScreenId),
            "The UI should register the collection Codex as a runtime screen");

    GameState state{};
    state.water = WaterState{24.0f, 0.82f, 0.45f, 1.0f};
    CreatureInstance starter{};
    starter.uuid = "codex-starter";
    starter.speciesId = engine::game::kStarterFishSpeciesId;
    CreatureInstance snail{};
    snail.uuid = "codex-snail";
    snail.speciesId = engine::game::kRamshornSnailSpeciesId;
    state.creatures = {starter, snail};
    const engine::game::HabitatDecorContribution shelter{
        PlaceableMaterialCategory::Rock, 1};
    const engine::game::CollectionCodexView view =
        engine::game::buildCollectionCodexView(state, {&shelter, 1});

    ui::UiTree tree = ui::makeCollectionCodexScreenTree();
    require(ui::setCollectionCodexView(
                tree, view, engine::game::kRamshornSnailSpeciesId),
            "Codex binding should accept the owned collection view");
    ui::computeLayout(tree.root);

    const ui::UiElement* panel =
        ui::findElementById(tree.root, "collection_codex_panel");
    const ui::UiElement* close =
        ui::findElementById(tree.root, ui::kCollectionCodexCloseButtonId);
    const ui::UiElement* summary =
        ui::findElementById(tree.root, "collection_codex_summary");
    const ui::UiElement* cleanup =
        ui::findElementById(tree.root, "collection_codex_cleanup");
    const ui::UiElement* list =
        ui::findElementById(tree.root, ui::kCollectionCodexListContentId);
    const ui::UiElement* firstSpecies =
        ui::findElementById(tree.root, "collection_codex_species_0");
    const ui::UiElement* secondSpecies =
        ui::findElementById(tree.root, "collection_codex_species_1");
    const ui::UiElement* roles =
        ui::findElementById(tree.root, "collection_codex_detail_roles");
    const ui::UiElement* status =
        ui::findElementById(tree.root, "collection_codex_detail_status");
    const ui::UiElement* advisory =
        ui::findElementById(tree.root, "collection_codex_advisory");
    require(tree.root.id == "ui_collection_codex_root" && panel != nullptr &&
                close != nullptr && list != nullptr && firstSpecies != nullptr &&
                secondSpecies != nullptr && roles != nullptr && status != nullptr &&
                advisory != nullptr,
            "Codex should expose stable retained ids for navigation and details");
    require(close->action == ui::kCloseCollectionCodexAction &&
                firstSpecies->action == ui::kSelectCollectionCodexSpeciesAction &&
                firstSpecies->actionPayload == engine::game::kStarterFishSpeciesId &&
                secondSpecies->actionPayload ==
                    engine::game::kRamshornSnailSpeciesId,
            "Codex controls should expose semantic close and species-selection actions");
    require(list->children.size() == 2 && summary != nullptr &&
                summary->text.value == "OWNED 2 CREATURES / 2 SPECIES" &&
                cleanup != nullptr &&
                cleanup->text.value == "CLEANUP CREW 1 / CLEANING LOAD -20%",
            "Codex summary should report owned species and their cleanup benefit");
    require(roles->text.value.find("Cleanup crew") != std::string::npos &&
                roles->text.value.find("Grazer") != std::string::npos &&
                status->text.value == "HABITAT  READY",
            "Selected species should display functional roles and current habitat advice");
    require(advisory->text.value.find("DOES NOT CONTROL") != std::string::npos,
            "Codex should state explicitly that habitat results are non-blocking");

    require(ui::setRuntimeUiOverlayViewport(tree, 1920.0f, 1080.0f),
            "Generic overlay viewport binding should include the Codex");
    ui::computeLayout(tree.root);
    panel = ui::findElementById(tree.root, "collection_codex_panel");
    require(panel != nullptr &&
                rectNear(panel->computedRect, 490.0f, 245.0f, 940.0f, 590.0f),
            "Codex panel should center at the runtime viewport");

    ui::UiContext context;
    require(context.setActiveScreen(ui::kOverlaySmokeScreenId) &&
                context.ensureActiveTree() != nullptr &&
                context.pushScreen(ui::kCollectionCodexScreenId) &&
                context.topScreenId() == ui::kCollectionCodexScreenId,
            "Codex should participate in the existing modal screen stack");
}

void testRuntimeUiCompactOverlayPanelsAndCatalogStayReachable()
{
    constexpr ui::UiRect viewport{0.0f, 0.0f, 640.0f, 360.0f};
    ui::UiTree tree = ui::makeOverlaySmokeTree();
    std::vector<ui::UiFishListEntry> roster;
    for (uint64_t id = 1; id <= 12; ++id)
    {
        roster.push_back({id, "FISH " + std::to_string(id), id == 1});
    }
    ui::setOverlaySmokeFishList(tree, roster);
    require(ui::setOverlaySmokeViewport(tree, viewport.width, viewport.height),
            "Compact overlay viewport should report retained geometry changes");
    ui::computeLayout(tree.root);

    const ui::UiElement* primary = ui::findElementById(tree.root, "test_panel");
    const ui::UiElement* telemetry =
        ui::findElementById(tree.root, "secondary_panel");
    const ui::UiElement* fish =
        ui::findElementById(tree.root, ui::kFishListPanelId);
    const ui::UiElement* care =
        ui::findElementById(tree.root, "primary_creature_care_panel");
    const std::array<const ui::UiElement*, 4> panels{primary, telemetry, fish, care};
    for (const ui::UiElement* panel : panels)
    {
        require(panel != nullptr && panel->visible && panel->enabled &&
                    rectInside(panel->computedRect, viewport),
                "Every compact HUD panel should remain visible and inside 640x360");
    }
    for (size_t lhs = 0; lhs < panels.size(); ++lhs)
    {
        for (size_t rhs = lhs + 1; rhs < panels.size(); ++rhs)
        {
            require(!rectsOverlap(panels[lhs]->computedRect,
                                  panels[rhs]->computedRect),
                    "Compact HUD panels should not overlap each other");
        }
    }

    ui::UiOverlaySmokeHudStatus worstCaseTelemetry{};
    worstCaseTelemetry.waterOxygen = 1.0f;
    worstCaseTelemetry.waterTemperatureC = 40.0f;
    worstCaseTelemetry.waterFlow = 1.0f;
    ui::setOverlaySmokeHudStatus(tree, worstCaseTelemetry);
    const ui::FontAsset layoutFont = ui::makeDefaultDebugFontAsset();
    const std::array<std::string_view, 3> telemetryLabelIds{
        "secondary_label_o2", "secondary_label_temp", "secondary_label_flow"};
    for (std::string_view id : telemetryLabelIds)
    {
        const ui::UiElement* label = ui::findElementById(tree.root, id);
        require(label != nullptr &&
                    ui::measureText(
                        layoutFont, label->text.value, label->text.pixelHeight,
                        ui::TextLayoutOptions{label->computedRect.width, 0.0f,
                                              ui::TextAlign::Left})
                            .lineCount == 1,
                "Compact telemetry values should fit without clipped wrapping");
    }

    const std::array<std::string_view, 5> compactControlIds{
        ui::kMaintainWaterButtonId, "catalog_bar",
        ui::kCollectionCodexButtonId, ui::kFishFocusReleaseButtonId,
        ui::kFeedPrimaryCreatureButtonId};
    for (std::string_view id : compactControlIds)
    {
        const ui::UiElement* control = ui::findElementById(tree.root, id);
        require(control != nullptr && rectInside(control->computedRect, viewport),
                "Compact HUD actions should remain inside the viewport");
    }
    const ui::UiElement* fishTitle =
        ui::findElementById(tree.root, "fish_list_title");
    const ui::UiElement* codexButton =
        ui::findElementById(tree.root, ui::kCollectionCodexButtonId);
    require(fishTitle != nullptr && codexButton != nullptr &&
                ui::measureText(
                    layoutFont, fishTitle->text.value,
                    fishTitle->text.pixelHeight,
                    ui::TextLayoutOptions{fishTitle->computedRect.width, 0.0f,
                                          ui::TextAlign::Left})
                        .lineCount == 1 &&
                !rectsOverlap(fishTitle->computedRect,
                              codexButton->computedRect),
            "Fish sidebar title should fit on one line beside the Codex action");

    ui::setOverlaySmokeBuildCatalogOpen(tree, true);
    ui::computeLayout(tree.root);
    const ui::UiElement* catalog =
        ui::findElementById(tree.root, "build_catalog_panel");
    const ui::UiElement* close =
        ui::findElementById(tree.root, "build_catalog_close");
    const ui::UiElement* cancel =
        ui::findElementById(tree.root, ui::kBuildCatalogCancelId);
    const ui::UiElement* remove =
        ui::findElementById(tree.root, ui::kBuildCatalogRemoveId);
    const ui::UiElement* list =
        ui::findElementById(tree.root, ui::kBuildCatalogListViewportId);
    primary = ui::findElementById(tree.root, "test_panel");
    telemetry = ui::findElementById(tree.root, "secondary_panel");
    require(catalog != nullptr && catalog->visible && catalog->enabled &&
                rectInside(catalog->computedRect, viewport) &&
                primary != nullptr && !primary->visible &&
                telemetry != nullptr && !telemetry->visible,
            "Compact catalog should replace the left HUD column without overflow");
    require(close != nullptr && cancel != nullptr && remove != nullptr &&
                list != nullptr && rectInside(close->computedRect, catalog->computedRect) &&
                rectInside(cancel->computedRect, catalog->computedRect) &&
                rectInside(remove->computedRect, catalog->computedRect) &&
                rectInside(list->computedRect, catalog->computedRect),
            "Compact catalog controls and list viewport should fit inside the panel");
    const std::array<const ui::UiElement*, 3> catalogActions{close, cancel, remove};
    for (const ui::UiElement* action : catalogActions)
    {
        const ui::UiHitResult hit = ui::hitTest(
            tree.root, action->computedRect.x + action->computedRect.width * 0.5f,
            action->computedRect.y + action->computedRect.height * 0.5f);
        require(hit.hit && hit.element != nullptr && hit.element->id == action->id,
                "Every compact catalog action should own its visible pointer area");
    }

    const std::span<const ui::BuildCatalogItemDefinition> items =
        ui::overlaySmokeBuildCatalogItems();
    require(!items.empty(), "Compact catalog reachability needs generated rows");
    const ui::UiElement* first = ui::findElementById(tree.root, items.front().id);
    require(first != nullptr && rectInside(first->computedRect, list->computedRect),
            "The first catalog row should be reachable before scrolling");
    const ui::UiHitResult firstHit = ui::hitTest(
        tree.root, first->computedRect.x + 4.0f, first->computedRect.y + 4.0f);
    require(firstHit.hit && firstHit.element != nullptr &&
                firstHit.element->id == items.front().id,
            "The first compact catalog row should own pointer hits");

    ui::setOverlaySmokeBuildCatalogScrollOffset(
        tree, ui::overlaySmokeBuildCatalogMaxScrollOffset());
    ui::computeLayout(tree.root);
    list = ui::findElementById(tree.root, ui::kBuildCatalogListViewportId);
    const ui::UiElement* last = ui::findElementById(tree.root, items.back().id);
    require(list != nullptr && last != nullptr &&
                rectInside(last->computedRect, list->computedRect),
            "The last catalog row should become reachable at maximum scroll");
    const ui::UiHitResult lastHit = ui::hitTest(
        tree.root, last->computedRect.x + 4.0f, last->computedRect.y + 4.0f);
    require(lastHit.hit && lastHit.element != nullptr &&
                lastHit.element->id == items.back().id,
            "The last compact catalog row should own pointer hits after scrolling");

    constexpr ui::UiRect midHeightViewport{0.0f, 0.0f, 1280.0f, 600.0f};
    ui::UiTree midHeightTree = ui::makeOverlaySmokeTree();
    require(ui::setOverlaySmokeViewport(midHeightTree, midHeightViewport.width,
                                        midHeightViewport.height),
            "Mid-height HUD should select its collision-free retained layout");
    ui::setOverlaySmokeBuildCatalogOpen(midHeightTree, true);
    ui::computeLayout(midHeightTree.root);
    const ui::UiElement* midCatalog =
        ui::findElementById(midHeightTree.root, "build_catalog_panel");
    const ui::UiElement* midPrimary =
        ui::findElementById(midHeightTree.root, "test_panel");
    const ui::UiElement* midTelemetry =
        ui::findElementById(midHeightTree.root, "secondary_panel");
    const ui::UiElement* midFish =
        ui::findElementById(midHeightTree.root, ui::kFishListPanelId);
    const ui::UiElement* midCare =
        ui::findElementById(midHeightTree.root, "primary_creature_care_panel");
    require(midCatalog != nullptr && midCatalog->visible &&
                rectInside(midCatalog->computedRect, midHeightViewport) &&
                midPrimary != nullptr && !midPrimary->visible &&
                midTelemetry != nullptr && !midTelemetry->visible &&
                midFish != nullptr &&
                rectInside(midFish->computedRect, midHeightViewport) &&
                midCare != nullptr &&
                rectInside(midCare->computedRect, midHeightViewport) &&
                !rectsOverlap(midCatalog->computedRect,
                              midFish->computedRect) &&
                !rectsOverlap(midCatalog->computedRect,
                              midCare->computedRect),
            "Open catalog should replace colliding HUD cards at mid-height sizes");
}

void testRuntimeUiCompactMenusPauseAndFishScrolling()
{
    constexpr ui::UiRect viewport{0.0f, 0.0f, 640.0f, 360.0f};
    const auto requireScreenControlsInside =
        [&](ui::UiTree& tree, const ui::UiRect& bounds,
            std::string_view panelId,
            std::span<const std::string_view> controlIds) {
            ui::computeLayout(tree.root);
            const ui::UiElement* panel = ui::findElementById(tree.root, panelId);
            require(panel != nullptr && rectInside(panel->computedRect, bounds),
                    "Responsive menu panel should remain inside its viewport");
            for (std::string_view id : controlIds)
            {
                const ui::UiElement* control = ui::findElementById(tree.root, id);
                require(control != nullptr &&
                            rectInside(control->computedRect, panel->computedRect),
                        "Responsive menu actions should remain inside their panel");
            }
        };

    ui::UiTree menu = ui::makeMainMenuScreenTree();
    require(ui::setMainMenuViewport(menu, viewport.width, viewport.height),
            "Main menu should respond to a compact viewport");
    const std::array<std::string_view, 2> menuControls{
        ui::kMainMenuStartButtonId, ui::kMainMenuOptionsButtonId};
    requireScreenControlsInside(menu, viewport, "main_menu_panel", menuControls);

    ui::UiTree options = ui::makeMainMenuOptionsScreenTree();
    require(ui::setMainMenuViewport(options, viewport.width, viewport.height),
            "Options menu should respond to a compact viewport");
    const std::array<std::string_view, 2> optionControls{
        ui::kMainMenuOptionLabelsButtonId, ui::kMainMenuOptionsBackButtonId};
    requireScreenControlsInside(options, viewport, "main_menu_options_panel",
                                optionControls);

    constexpr ui::UiRect midWidthViewport{0.0f, 0.0f, 800.0f, 600.0f};
    require(ui::setMainMenuViewport(options, midWidthViewport.width,
                                    midWidthViewport.height),
            "Options menu should respond before its fixed origin overflows");
    requireScreenControlsInside(options, midWidthViewport,
                                "main_menu_options_panel", optionControls);
    constexpr ui::UiRect shortViewport{0.0f, 0.0f, 1280.0f, 450.0f};
    require(ui::setMainMenuViewport(options, shortViewport.width,
                                    shortViewport.height),
            "Options menu should respond to a short wide viewport");
    requireScreenControlsInside(options, shortViewport,
                                "main_menu_options_panel", optionControls);

    ui::UiTree pause = ui::makePauseScreenTree();
    require(ui::setPauseScreenViewport(pause, viewport.width, viewport.height),
            "Pause overlay should respond to a compact viewport");
    const std::array<std::string_view, 3> pauseControls{
        ui::kPauseResumeButtonId, ui::kPauseOptionsButtonId,
        ui::kPauseMainMenuButtonId};
    requireScreenControlsInside(pause, viewport, "pause_panel", pauseControls);
    const ui::UiElement* backdrop =
        ui::findElementById(pause.root, ui::kPauseBackdropId);
    const ui::UiHitResult backdropHit = ui::hitTest(pause.root, 8.0f, 8.0f);
    require(backdrop != nullptr && rectNear(backdrop->computedRect, 0.0f, 0.0f,
                                            viewport.width, viewport.height) &&
                backdropHit.hit && backdropHit.element != nullptr &&
                backdropHit.element->id == ui::kPauseBackdropId,
            "Pause blank space should be owned by the full-screen modal backdrop");

    ui::UiTree fishTree = ui::makeOverlaySmokeTree();
    require(ui::setOverlaySmokeViewport(fishTree, viewport.width, viewport.height),
            "Fish scrolling test should use the compact retained layout");
    std::vector<ui::UiFishListEntry> roster;
    for (uint64_t id = 1; id <= 16; ++id)
    {
        roster.push_back({id, "FISH " + std::to_string(id)});
    }
    ui::setOverlaySmokeFishList(fishTree, roster);
    ui::computeLayout(fishTree.root);
    const ui::UiElement* fishViewport =
        ui::findElementById(fishTree.root, ui::kFishListContentId);
    const ui::UiElement* fishPanel =
        ui::findElementById(fishTree.root, ui::kFishListPanelId);
    require(fishViewport != nullptr && fishPanel != nullptr &&
                ui::overlaySmokeFishListMaxScrollOffset(fishTree) > 0.0f,
            "An oversized compact roster should expose a scroll range");
    require(!ui::handleOverlaySmokeFishListWheel(
                fishTree, fishViewport->computedRect.x - 1.0f,
                fishViewport->computedRect.y + 4.0f, -1.0),
            "Fish wheel input outside the clipped list should not be consumed");
    require(ui::handleOverlaySmokeFishListWheel(
                fishTree, fishViewport->computedRect.x + 4.0f,
                fishViewport->computedRect.y + 4.0f, -100.0),
            "Fish wheel input inside the clipped list should be consumed");
    const float maxFishOffset =
        ui::overlaySmokeFishListMaxScrollOffset(fishTree);
    require(nearlyEqual(ui::overlaySmokeFishListScrollOffset(fishTree),
                        maxFishOffset) &&
                nearlyEqual(fishPanel->scrollOffsetY, 0.0f),
            "Fish scrolling should clamp at the roster tail and move only list content");
    ui::computeLayout(fishTree.root);
    fishViewport = ui::findElementById(fishTree.root, ui::kFishListContentId);
    const ui::UiElement* lastFish =
        ui::findElementById(fishTree.root, "fish_row_16");
    require(fishViewport != nullptr && lastFish != nullptr &&
                rectInside(lastFish->computedRect, fishViewport->computedRect),
            "Maximum fish scroll should make the final roster row reachable");
    require(ui::handleOverlaySmokeFishListWheel(
                fishTree, fishViewport->computedRect.x + 4.0f,
                fishViewport->computedRect.y + 4.0f, -1.0) &&
                nearlyEqual(ui::overlaySmokeFishListScrollOffset(fishTree),
                            maxFishOffset),
            "Fish viewport should consume boundary wheel input without overscrolling");
    const std::array<ui::UiFishListEntry, 1> reducedRoster{
        ui::UiFishListEntry{1, "FISH 1"}};
    ui::setOverlaySmokeFishList(fishTree, reducedRoster);
    require(nearlyEqual(ui::overlaySmokeFishListMaxScrollOffset(fishTree), 0.0f) &&
                nearlyEqual(ui::overlaySmokeFishListScrollOffset(fishTree), 0.0f),
            "Shrinking a fish roster should clamp a stale scroll offset to zero");
}

void testRuntimeUiCompactCollectionCodexScrollsRegionsIndependently()
{
    constexpr ui::UiRect viewport{0.0f, 0.0f, 640.0f, 360.0f};
    engine::game::CollectionCodexView view{};
    view.totalOwnedCreatures = 14;
    view.registeredSpeciesCount = 14;
    view.knownOwnedSpeciesCount = 14;
    for (size_t speciesIndex = 0; speciesIndex < 14; ++speciesIndex)
    {
        engine::game::CollectionCodexEntry entry{};
        entry.speciesId = "compact-species-" + std::to_string(speciesIndex);
        entry.displayName = "SPECIES " + std::to_string(speciesIndex);
        entry.description = "COMPACT CODEX SCROLL TEST";
        entry.ownedCount = 1;
        entry.roles = engine::game::speciesRoleMask(
            engine::game::SpeciesRole::Showpiece);
        entry.habitat.status =
            engine::game::HabitatCompatibilityStatus::NeedsAttention;
        entry.habitat.speciesId = entry.speciesId;
        entry.habitat.unsatisfiedRequirementCount = 12;
        for (size_t requirementIndex = 0; requirementIndex < 12;
             ++requirementIndex)
        {
            engine::game::HabitatDecorEvaluation evaluation{};
            evaluation.requirementId =
                "decor-" + std::to_string(requirementIndex);
            evaluation.displayName =
                "GRAZING SURFACE WITH LONG DETAIL " +
                std::to_string(requirementIndex);
            evaluation.requiredCount = 1;
            evaluation.status =
                engine::game::HabitatRequirementStatus::Unsatisfied;
            entry.habitat.decor.push_back(std::move(evaluation));
        }
        view.entries.push_back(std::move(entry));
    }

    ui::UiTree tree = ui::makeCollectionCodexScreenTree();
    require(ui::setCollectionCodexViewport(tree, viewport.width, viewport.height),
            "Codex should apply compact 640x360 geometry");
    require(ui::setCollectionCodexView(tree, view, view.entries.front().speciesId),
            "Compact Codex should accept an overflowing retained view");
    ui::computeLayout(tree.root);

    const ui::UiElement* panel =
        ui::findElementById(tree.root, "collection_codex_panel");
    const ui::UiElement* close =
        ui::findElementById(tree.root, ui::kCollectionCodexCloseButtonId);
    const ui::UiElement* list =
        ui::findElementById(tree.root, ui::kCollectionCodexListContentId);
    const ui::UiElement* detail =
        ui::findElementById(tree.root, ui::kCollectionCodexDetailContentId);
    const ui::UiElement* advisory =
        ui::findElementById(tree.root, "collection_codex_advisory");
    require(panel != nullptr && close != nullptr && list != nullptr &&
                detail != nullptr && advisory != nullptr &&
                rectInside(panel->computedRect, viewport) &&
                rectInside(close->computedRect, panel->computedRect) &&
                rectInside(list->computedRect, panel->computedRect) &&
                rectInside(detail->computedRect, panel->computedRect) &&
                rectInside(advisory->computedRect, panel->computedRect) &&
                !rectsOverlap(list->computedRect, detail->computedRect),
            "Compact Codex panel, close action, columns, and advisory should stay in bounds");

    bool foundWrappedRequirement = false;
    const ui::UiElement* previousRequirement = nullptr;
    for (size_t index = 0; index < 12; ++index)
    {
        const ui::UiElement* requirement = ui::findElementById(
            tree.root, "collection_codex_requirement_" + std::to_string(index));
        require(requirement != nullptr,
                "Compact Codex should retain every generated requirement row");
        foundWrappedRequirement =
            foundWrappedRequirement || requirement->computedRect.height > 18.0f;
        if (previousRequirement != nullptr)
        {
            require(requirement->computedRect.y >=
                        previousRequirement->computedRect.y +
                            previousRequirement->computedRect.height + 7.5f,
                    "Wrapped Codex requirements should not overlap following rows");
        }
        previousRequirement = requirement;
    }
    require(foundWrappedRequirement,
            "Compact Codex coverage should exercise a wrapped requirement row");

    const float listX = list->computedRect.x + 4.0f;
    const float listY = list->computedRect.y + 4.0f;
    const float detailX = detail->computedRect.x + 4.0f;
    const float detailY = detail->computedRect.y + 4.0f;
    require(ui::collectionCodexScrollRegionAt(tree, listX, listY) ==
                ui::CollectionCodexScrollRegion::SpeciesList &&
                ui::collectionCodexScrollRegionAt(tree, detailX, detailY) ==
                    ui::CollectionCodexScrollRegion::Detail &&
                ui::collectionCodexScrollRegionAt(tree, 4.0f, 4.0f) ==
                    ui::CollectionCodexScrollRegion::None,
            "Codex pointer routing should distinguish list, detail, and backdrop");

    require(ui::scrollCollectionCodexAt(tree, listX, listY, -100.0),
            "Species-list wheel input should be consumed");
    list = ui::findElementById(tree.root, ui::kCollectionCodexListContentId);
    detail = ui::findElementById(tree.root, ui::kCollectionCodexDetailContentId);
    require(list != nullptr && detail != nullptr && list->scrollOffsetY > 0.0f &&
                nearlyEqual(detail->scrollOffsetY, 0.0f),
            "Species-list scrolling should not move Codex details");
    const float listOffset = list->scrollOffsetY;
    require(ui::scrollCollectionCodexAt(tree, detailX, detailY, -100.0),
            "Detail wheel input should be consumed");
    list = ui::findElementById(tree.root, ui::kCollectionCodexListContentId);
    detail = ui::findElementById(tree.root, ui::kCollectionCodexDetailContentId);
    require(list != nullptr && detail != nullptr &&
                nearlyEqual(list->scrollOffsetY, listOffset) &&
                detail->scrollOffsetY > 0.0f,
            "Detail scrolling should not move the Codex species list");
    const ui::UiElement* finalRequirement =
        ui::findElementById(tree.root, "collection_codex_requirement_11");
    require(finalRequirement != nullptr &&
                rectInside(finalRequirement->computedRect, detail->computedRect),
            "Maximum detail scroll should reveal the full final wrapped requirement");
    const float detailOffset = detail->scrollOffsetY;

    require(ui::setCollectionCodexView(tree, view, view.entries.front().speciesId),
            "Refreshing the same Codex selection should rebuild retained content");
    list = ui::findElementById(tree.root, ui::kCollectionCodexListContentId);
    detail = ui::findElementById(tree.root, ui::kCollectionCodexDetailContentId);
    require(list != nullptr && detail != nullptr &&
                nearlyEqual(list->scrollOffsetY, listOffset) &&
                nearlyEqual(detail->scrollOffsetY, detailOffset),
            "Same-selection refresh should preserve independent Codex offsets");
    require(ui::setCollectionCodexView(tree, view, view.entries[1].speciesId),
            "Changing Codex selection should rebuild detail content");
    list = ui::findElementById(tree.root, ui::kCollectionCodexListContentId);
    detail = ui::findElementById(tree.root, ui::kCollectionCodexDetailContentId);
    require(list != nullptr && detail != nullptr &&
                nearlyEqual(list->scrollOffsetY, listOffset) &&
                nearlyEqual(detail->scrollOffsetY, 0.0f),
            "Changing species should preserve list position and reset detail scroll");
}

void testRuntimeUiBindingSetAppliesOnlyDirtyBindings()
{
    ui::UiTree tree{};
    tree.root.id = "root";

    ui::UiDataVersion versionA;
    ui::UiDataVersion versionB;
    int appliedA = 0;
    int appliedB = 0;

    ui::UiBindingSet bindings;
    bindings.bind("a", &versionA, [&](ui::UiTree&) { ++appliedA; });
    bindings.bind("b", &versionB, [&](ui::UiTree&) { ++appliedB; });

    ui::UiBindingStats stats = bindings.sync(tree, 1);
    require(stats.total == 2 && stats.applied == 2 && appliedA == 1 && appliedB == 1,
            "first sync should apply all bindings");

    stats = bindings.sync(tree, 1);
    require(stats.applied == 0 && appliedA == 1 && appliedB == 1,
            "sync without version changes should apply nothing");

    versionA.bump();
    stats = bindings.sync(tree, 1);
    require(stats.applied == 1 && appliedA == 2 && appliedB == 1,
            "only the binding with a bumped version should re-apply");

    // a rebuilt tree (new revision) starts from authored defaults, so every
    // binding must re-apply even though versions did not change.
    stats = bindings.sync(tree, 2);
    require(stats.applied == 2 && appliedA == 3 && appliedB == 2,
            "a tree revision change should re-apply all bindings");
}

void testRuntimeUiTweenSetAnimatesAndCompletes()
{
    ui::UiTree tree{};
    tree.root.id = "root";
    ui::UiElement panel{};
    panel.id = "panel";
    tree.root.children.push_back(panel);

    ui::UiTweenSet tweens;
    tweens.start(ui::UiTweenSpec{"panel", ui::UiTweenProperty::Opacity, 0.0f, 1.0f,
                                 0.2f, ui::UiEase::Linear},
                 tree);
    ui::UiElement* element = ui::findElementById(tree.root, "panel");
    require(element != nullptr, "tween test element should exist");
    require(nearlyEqual(element->opacity, 0.0f),
            "starting a tween should apply its from-value immediately");

    std::vector<ui::UiTweenCompletion> completions = tweens.update(0.1f, tree);
    require(completions.empty(), "half-way tween should not complete");
    require(nearlyEqual(element->opacity, 0.5f),
            "linear tween should reach its midpoint value at half duration");

    completions = tweens.update(0.1f, tree);
    require(completions.size() == 1 && completions[0].elementId == "panel" &&
                completions[0].property == ui::UiTweenProperty::Opacity,
            "tween should report completion at full duration");
    require(nearlyEqual(element->opacity, 1.0f), "completed tween should land on to-value");
    require(!tweens.anyActive(), "completed tween should leave the active set");

    // restarting the same element+property replaces the previous tween.
    tweens.start(ui::UiTweenSpec{"panel", ui::UiTweenProperty::OffsetY, -12.0f, 0.0f,
                                 0.2f, ui::UiEase::Linear},
                 tree);
    tweens.start(ui::UiTweenSpec{"panel", ui::UiTweenProperty::OffsetY, -6.0f, 0.0f,
                                 0.2f, ui::UiEase::Linear},
                 tree);
    require(tweens.activeCount() == 1,
            "starting a tween on the same element+property should replace it");
    require(nearlyEqual(element->animOffsetY, -6.0f),
            "replacement tween should apply its own from-value");

    // offset tweens affect layout; opacity-only tweens are paint-time.
    require(tweens.hasLayoutAffectingTweens(),
            "active offset tween should report as layout-affecting");

    // instant mode completes in a single update regardless of dt.
    tweens.setInstant(true);
    completions = tweens.update(0.0f, tree);
    require(completions.size() == 1 && nearlyEqual(element->animOffsetY, 0.0f),
            "instant mode should complete the tween in one update");

    tweens.setInstant(false);
    tweens.start(ui::UiTweenSpec{"panel", ui::UiTweenProperty::Opacity, 0.0f, 1.0f,
                                 0.2f, ui::UiEase::Linear},
                 tree);
    require(!tweens.hasLayoutAffectingTweens(),
            "opacity-only tween set should not report as layout-affecting");

    require(ui::uiEaseValue(ui::UiEase::EaseOutCubic, 1.0f) == 1.0f &&
                ui::uiEaseValue(ui::UiEase::EaseInCubic, 0.0f) == 0.0f,
            "easing endpoints should be exact");
}

void testRuntimeUiOpacityScalesPaintedColors()
{
    ui::UiTree tree{};
    tree.root.id = "root";
    tree.root.rect = ui::UiRect{0.0f, 0.0f, 100.0f, 100.0f};
    ui::UiElement rect{};
    rect.id = "fade_rect";
    rect.kind = ui::UiElementKind::Rect;
    rect.rect = ui::UiRect{10.0f, 10.0f, 20.0f, 20.0f};
    rect.style.color = ui::packColor(1.0f, 1.0f, 1.0f, 1.0f);
    rect.opacity = 0.5f;
    rect.animOffsetY = 5.0f;
    tree.root.children.push_back(rect);

    ui::computeLayout(tree.root);
    const ui::UiElement* laidOut = ui::findElementById(tree.root, "fade_rect");
    require(laidOut != nullptr && nearlyEqual(laidOut->computedRect.y, 15.0f),
            "animOffsetY should shift the computed rect");

    const ui::FontAsset font{};
    ui::UiDrawList drawList;
    ui::paintTree(tree.root, font, drawList);
    require(!drawList.commands.empty(), "faded rect should still emit draw commands");
    bool sawScaledColor = false;
    for (const ui::UiVertex& vertex : drawList.vertices)
    {
        if (vertex.color == 0x80808080u)
        {
            sawScaledColor = true;
        }
    }
    require(sawScaledColor,
            "0.5 opacity should scale opaque white vertices to 0x80808080");
}

void testRuntimeUiDrawListEmitsQuadGeometry()
{
    ui::UiDrawList list;
    const ui::UiScissor full{0, 0, 256, 256};
    list.addSolidRect(ui::UiRect{10.0f, 20.0f, 30.0f, 40.0f}, 0xffffffffu, full);

    require(list.vertices.size() == 4, "A rect should emit four vertices");
    require(list.indices.size() == 6, "A rect should emit six indices");
    require(list.commands.size() == 1, "A rect should emit one draw command");
    require(list.commands[0].indexCount == 6, "Quad command should cover six indices");
    require(list.commands[0].indexOffset == 0, "First quad command should start at index 0");
    require(list.commands[0].atlasTextureId == ui::kWhiteAtlasTextureId,
            "addSolidRect should target the white atlas");
    require(nearlyEqual(list.vertices[0].pos.x, 10.0f) &&
                nearlyEqual(list.vertices[0].pos.y, 20.0f),
            "Top-left vertex should match the rect origin");
    require(nearlyEqual(list.vertices[2].pos.x, 40.0f) &&
                nearlyEqual(list.vertices[2].pos.y, 60.0f),
            "Bottom-right vertex should match the rect extent");
    require(list.indices[0] == 0 && list.indices[1] == 1 && list.indices[2] == 2 &&
                list.indices[3] == 0 && list.indices[4] == 2 && list.indices[5] == 3,
            "Quad indices should use the expected two-triangle winding");

    list.addSolidRect(ui::UiRect{0.0f, 0.0f, 5.0f, 5.0f}, 0xffffffffu, full);
    require(list.commands.size() == 2, "A second rect should append a command");
    require(list.commands[1].indexOffset == 6,
            "The second command should start after the first quad");
    require(list.indices[6] == 4, "The second quad should reference the appended vertices");
}

void testRuntimeUiDrawListRejectsDegenerateRects()
{
    ui::UiDrawList list;
    const ui::UiScissor full{0, 0, 256, 256};
    list.addSolidRect(ui::UiRect{0.0f, 0.0f, 0.0f, 10.0f}, 0xffffffffu, full);
    list.addSolidRect(ui::UiRect{0.0f, 0.0f, 10.0f, -1.0f}, 0xffffffffu, full);
    list.addSolidRect(ui::UiRect{0.0f, 0.0f, 10.0f, 10.0f}, 0xffffffffu,
                      ui::UiScissor{0, 0, 0, 10});
    require(list.empty(), "Degenerate rects and empty scissors should emit no geometry");
}

void testRuntimeUiNoneLayoutOffsetsChildrenByParentOrigin()
{
    ui::UiElement root;
    root.id = "root";
    root.kind = ui::UiElementKind::Container;
    root.rect = ui::UiRect{10.0f, 20.0f, 100.0f, 100.0f};

    ui::UiElement child;
    child.id = "child";
    child.rect = ui::UiRect{5.0f, 5.0f, 30.0f, 40.0f};

    ui::UiElement grandchild;
    grandchild.id = "grandchild";
    grandchild.rect = ui::UiRect{1.0f, 2.0f, 3.0f, 4.0f};
    child.children.push_back(grandchild);
    root.children.push_back(child);

    ui::computeLayout(root);

    const ui::UiElement* c = ui::findElementById(root, "child");
    const ui::UiElement* g = ui::findElementById(root, "grandchild");
    require(c != nullptr && g != nullptr, "findElementById should locate nested elements");
    require(rectNear(c->computedRect, 15.0f, 25.0f, 30.0f, 40.0f),
            "None layout should offset a child by the parent origin");
    require(rectNear(g->computedRect, 16.0f, 27.0f, 3.0f, 4.0f),
            "None layout should offset nested children recursively");
    require(ui::findElementById(root, "missing") == nullptr,
            "findElementById should return null for unknown ids");
}

void testRuntimeUiVerticalStackAppliesPaddingSpacingAndFill()
{
    ui::UiElement parent;
    parent.kind = ui::UiElementKind::Container;
    parent.rect = ui::UiRect{0.0f, 0.0f, 200.0f, 300.0f};
    parent.layoutMode = ui::UiLayoutMode::VerticalStack;
    parent.padding = ui::UiPadding{10.0f, 8.0f, 4.0f, 6.0f};
    parent.spacing = 5.0f;
    parent.crossAxisAlign = ui::UiCrossAxisAlign::Start;

    ui::UiElement a;
    a.id = "a";
    a.rect = ui::UiRect{0.0f, 0.0f, 0.0f, 20.0f}; // zero width -> fill cross axis
    ui::UiElement b;
    b.id = "b";
    b.rect = ui::UiRect{0.0f, 0.0f, 50.0f, 30.0f};
    parent.children.push_back(a);
    parent.children.push_back(b);

    ui::computeLayout(parent);

    const ui::UiElement* ra = ui::findElementById(parent, "a");
    const ui::UiElement* rb = ui::findElementById(parent, "b");
    require(ra != nullptr && rb != nullptr, "Vertical stack children should exist");
    require(rectNear(ra->computedRect, 10.0f, 8.0f, 186.0f, 20.0f),
            "A zero-width stack child should fill the padded content width");
    require(rectNear(rb->computedRect, 10.0f, 33.0f, 50.0f, 30.0f),
            "A vertical stack should advance by child height plus spacing");
}

void testRuntimeUiVerticalStackDistributesGrowSpace()
{
    ui::UiElement parent;
    parent.kind = ui::UiElementKind::Container;
    parent.rect = ui::UiRect{0.0f, 0.0f, 100.0f, 100.0f};
    parent.layoutMode = ui::UiLayoutMode::VerticalStack;
    parent.padding = ui::UiPadding{0.0f, 10.0f, 0.0f, 10.0f};
    parent.spacing = 5.0f;

    ui::UiElement fixedA;
    fixedA.id = "fixed_a";
    fixedA.rect = ui::UiRect{0.0f, 0.0f, 0.0f, 20.0f};

    ui::UiElement grow;
    grow.id = "grow";
    grow.rect = ui::UiRect{0.0f, 0.0f, 0.0f, 0.0f};
    grow.grow = 1.0f;
    grow.minMainSize = 10.0f;

    ui::UiElement fixedB;
    fixedB.id = "fixed_b";
    fixedB.rect = ui::UiRect{0.0f, 0.0f, 0.0f, 10.0f};

    parent.children.push_back(fixedA);
    parent.children.push_back(grow);
    parent.children.push_back(fixedB);

    ui::computeLayout(parent);

    const ui::UiElement* resolvedGrow = ui::findElementById(parent, "grow");
    const ui::UiElement* resolvedFixedB = ui::findElementById(parent, "fixed_b");
    require(resolvedGrow != nullptr && resolvedFixedB != nullptr,
            "Grow stack children should exist");
    require(rectNear(resolvedGrow->computedRect, 0.0f, 35.0f, 100.0f, 40.0f),
            "A vertical grow child should receive remaining content height");
    require(rectNear(resolvedFixedB->computedRect, 0.0f, 80.0f, 100.0f, 10.0f),
            "Children after a grow child should advance by the resolved grow height");
}

void testRuntimeUiHorizontalStackDistributesGrowSpace()
{
    ui::UiElement parent;
    parent.kind = ui::UiElementKind::Container;
    parent.rect = ui::UiRect{0.0f, 0.0f, 200.0f, 100.0f};
    parent.layoutMode = ui::UiLayoutMode::HorizontalStack;
    parent.padding = ui::UiPadding{10.0f, 0.0f, 10.0f, 0.0f};
    parent.spacing = 5.0f;

    ui::UiElement fixedA;
    fixedA.id = "fixed_a";
    fixedA.rect = ui::UiRect{0.0f, 0.0f, 40.0f, 10.0f};

    ui::UiElement grow;
    grow.id = "grow";
    grow.rect = ui::UiRect{0.0f, 0.0f, 0.0f, 20.0f};
    grow.grow = 1.0f;
    grow.minMainSize = 20.0f;

    ui::UiElement fixedB;
    fixedB.id = "fixed_b";
    fixedB.rect = ui::UiRect{0.0f, 0.0f, 20.0f, 10.0f};

    parent.children.push_back(fixedA);
    parent.children.push_back(grow);
    parent.children.push_back(fixedB);

    ui::computeLayout(parent);

    const ui::UiElement* resolvedGrow = ui::findElementById(parent, "grow");
    const ui::UiElement* resolvedFixedB = ui::findElementById(parent, "fixed_b");
    require(resolvedGrow != nullptr && resolvedFixedB != nullptr,
            "Horizontal grow stack children should exist");
    require(rectNear(resolvedGrow->computedRect, 55.0f, 0.0f, 110.0f, 20.0f),
            "A horizontal grow child should receive remaining content width");
    require(rectNear(resolvedFixedB->computedRect, 170.0f, 0.0f, 20.0f, 10.0f),
            "Horizontal children after grow should advance by the resolved grow width");
}

void testRuntimeUiCrossAxisAlignmentPlacesChildren()
{
    const auto childRect = [](ui::UiLayoutMode mode, ui::UiCrossAxisAlign align,
                              const ui::UiRect& parentRect,
                              const ui::UiRect& child) -> ui::UiRect {
        ui::UiElement parent;
        parent.rect = parentRect;
        parent.layoutMode = mode;
        parent.crossAxisAlign = align;
        ui::UiElement c;
        c.id = "c";
        c.rect = child;
        parent.children.push_back(c);
        ui::computeLayout(parent);
        return ui::findElementById(parent, "c")->computedRect;
    };

    const ui::UiRect parent200x300{0.0f, 0.0f, 200.0f, 300.0f};
    const ui::UiRect fixed50x30{0.0f, 0.0f, 50.0f, 30.0f};

    require(rectNear(childRect(ui::UiLayoutMode::VerticalStack, ui::UiCrossAxisAlign::Center,
                               parent200x300, fixed50x30),
                     75.0f, 0.0f, 50.0f, 30.0f),
            "Centre alignment should centre a fixed-width vertical-stack child");
    require(rectNear(childRect(ui::UiLayoutMode::VerticalStack, ui::UiCrossAxisAlign::End,
                               parent200x300, fixed50x30),
                     150.0f, 0.0f, 50.0f, 30.0f),
            "End alignment should right-align a fixed-width vertical-stack child");
    require(nearlyEqual(childRect(ui::UiLayoutMode::VerticalStack,
                                  ui::UiCrossAxisAlign::Stretch, parent200x300, fixed50x30)
                            .width,
                        200.0f),
            "Stretch alignment should expand a vertical-stack child to the content width");
    require(rectNear(childRect(ui::UiLayoutMode::HorizontalStack, ui::UiCrossAxisAlign::Center,
                               ui::UiRect{0.0f, 0.0f, 300.0f, 100.0f},
                               ui::UiRect{0.0f, 0.0f, 40.0f, 20.0f}),
                     0.0f, 40.0f, 40.0f, 20.0f),
            "Centre alignment should centre a horizontal-stack child vertically");
}

void testRuntimeUiHitTestUsesZOrderAndState()
{
    ui::UiElement root;
    root.id = "root";
    root.kind = ui::UiElementKind::Container;
    root.rect = ui::UiRect{0.0f, 0.0f, 200.0f, 200.0f};

    ui::UiElement back;
    back.id = "back";
    back.kind = ui::UiElementKind::Rect;
    back.rect = ui::UiRect{0.0f, 0.0f, 100.0f, 100.0f};

    ui::UiElement top;
    top.id = "top";
    top.kind = ui::UiElementKind::Rect;
    top.rect = ui::UiRect{10.0f, 10.0f, 50.0f, 50.0f};

    ui::UiElement disabled;
    disabled.id = "disabled";
    disabled.kind = ui::UiElementKind::Rect;
    disabled.rect = ui::UiRect{20.0f, 20.0f, 60.0f, 60.0f};
    disabled.enabled = false;

    root.children.push_back(back);
    root.children.push_back(top);
    root.children.push_back(disabled);

    ui::computeLayout(root);

    ui::UiHitResult hit = ui::hitTest(root, 25.0f, 25.0f);
    require(hit.hit && hit.element != nullptr && hit.element->id == "top",
            "Hit testing should return the topmost enabled child");

    ui::UiElement* topElement = ui::findElementById(root, "top");
    require(topElement != nullptr, "Test setup should find the top element");
    topElement->visible = false;
    hit = ui::hitTest(root, 25.0f, 25.0f);
    require(hit.hit && hit.element != nullptr && hit.element->id == "back",
            "Hit testing should ignore invisible and disabled elements");

    require(!ui::hitTest(root, 150.0f, 150.0f).hit,
            "A container-only gap should not claim pointer ownership");
}

void testRuntimeUiHitTestRespectsClipping()
{
    ui::UiElement root;
    root.id = "root";
    root.kind = ui::UiElementKind::Container;
    root.rect = ui::UiRect{0.0f, 0.0f, 100.0f, 100.0f};

    ui::UiElement panel;
    panel.id = "panel";
    panel.kind = ui::UiElementKind::Rect;
    panel.rect = ui::UiRect{10.0f, 10.0f, 30.0f, 30.0f};

    ui::UiElement child;
    child.id = "outside_child";
    child.kind = ui::UiElementKind::Rect;
    child.rect = ui::UiRect{35.0f, 0.0f, 20.0f, 20.0f};
    panel.children.push_back(child);
    root.children.push_back(panel);

    ui::computeLayout(root);

    require(!ui::hitTest(root, 50.0f, 15.0f).hit,
            "A clipped child outside its parent rect should not be hit");

    ui::UiElement* panelElement = ui::findElementById(root, "panel");
    require(panelElement != nullptr, "Test setup should find the panel element");
    panelElement->clipChildren = false;
    ui::computeLayout(root);

    ui::UiHitResult hit = ui::hitTest(root, 50.0f, 15.0f);
    require(hit.hit && hit.element != nullptr && hit.element->id == "outside_child",
            "Disabling parent clipping should allow out-of-bounds child hits");
}

void testRuntimeUiPaintTreeCountsVisibleElements()
{
    const ui::FontAsset font = ui::makeDefaultDebugFontAsset();

    ui::UiElement root;
    root.id = "root";
    root.kind = ui::UiElementKind::Container;
    root.rect = ui::UiRect{0.0f, 0.0f, 200.0f, 200.0f};

    ui::UiElement visibleRect;
    visibleRect.id = "rect";
    visibleRect.kind = ui::UiElementKind::Rect;
    visibleRect.rect = ui::UiRect{0.0f, 0.0f, 50.0f, 50.0f};
    visibleRect.style.color = ui::packColor(1.0f, 1.0f, 1.0f, 1.0f);

    ui::UiElement hiddenRect;
    hiddenRect.id = "hidden";
    hiddenRect.kind = ui::UiElementKind::Rect;
    hiddenRect.rect = ui::UiRect{0.0f, 0.0f, 10.0f, 10.0f};
    hiddenRect.visible = false;

    ui::UiElement label;
    label.id = "label";
    label.kind = ui::UiElementKind::Text;
    label.rect = ui::UiRect{0.0f, 60.0f, 180.0f, 24.0f};
    label.text.value = "AB";
    label.text.pixelHeight = 16.0f;
    label.style.color = ui::packColor(1.0f, 1.0f, 1.0f, 1.0f);

    root.children.push_back(visibleRect);
    root.children.push_back(hiddenRect);
    root.children.push_back(label);

    ui::computeLayout(root);
    ui::UiDrawList list;
    const ui::UiPaintStats stats = ui::paintTree(root, font, list);

    require(stats.elements == 3, "paintTree should count the root and its two visible children");
    require(stats.rectElements == 1, "paintTree should count one painted rect");
    require(stats.textElements == 1, "paintTree should count one painted text element");
    require(stats.glyphCount > 0, "paintTree should emit glyph quads for visible text");
    require(stats.commandCountAfter > stats.commandCountBefore,
            "paintTree should append draw commands");
    require(!list.empty(), "paintTree should populate the draw list");
}

void testRuntimeUiButtonPaintUsesPointerStateColors()
{
    const ui::FontAsset font = ui::makeDefaultDebugFontAsset();

    ui::UiElement root;
    root.id = "root";
    root.kind = ui::UiElementKind::Container;
    root.rect = ui::UiRect{0.0f, 0.0f, 200.0f, 100.0f};

    ui::UiElement button;
    button.id = "button";
    button.kind = ui::UiElementKind::Button;
    button.rect = ui::UiRect{10.0f, 10.0f, 120.0f, 28.0f};
    button.style.color = ui::packColor(0.10f, 0.10f, 0.10f, 1.0f);
    button.style.hoverColor = ui::packColor(0.20f, 0.30f, 0.40f, 1.0f);
    button.style.pressedColor = ui::packColor(0.80f, 0.20f, 0.10f, 1.0f);
    button.style.hasHoverColor = true;
    button.style.hasPressedColor = true;
    button.style.textColor = ui::packColor(1.0f, 1.0f, 1.0f, 1.0f);
    button.text.value = "GO";
    button.text.pixelHeight = 16.0f;
    button.text.layout = ui::TextLayoutOptions{0.0f, 0.0f, ui::TextAlign::Center};

    root.children.push_back(button);
    ui::computeLayout(root);

    ui::UiDrawList list;
    const ui::UiPaintStats hoverStats = ui::paintTree(
        root, font, list, ui::UiPaintOptions{"button", ""});
    require(hoverStats.rectElements == 1 && hoverStats.textElements == 1,
            "Button painting should emit one rect and one text element");
    require(hoverStats.glyphCount > 0, "Button painting should emit label glyphs");
    require(!list.vertices.empty() &&
                list.vertices[0].color == ui::packColor(0.20f, 0.30f, 0.40f, 1.0f),
            "Hovered button paint should use the hover color");

    list.clear();
    ui::paintTree(root, font, list, ui::UiPaintOptions{"button", "button"});
    require(!list.vertices.empty() &&
                list.vertices[0].color == ui::packColor(0.80f, 0.20f, 0.10f, 1.0f),
            "Pressed button paint should override hover color");
}

void testRuntimeUiButtonHelperBuildsSemanticButton()
{
    ui::UiButtonOptions options{};
    options.id = "button";
    options.rect = ui::UiRect{4.0f, 6.0f, 100.0f, 24.0f};
    options.label = "OPEN";
    options.action = "open";
    options.actionPayload = "payload";

    const ui::UiElement button = ui::makeButton(std::move(options));
    require(button.kind == ui::UiElementKind::Button,
            "makeButton should build a retained button element");
    require(button.id == "button" && button.action == "open" &&
                button.actionPayload == "payload",
            "makeButton should preserve id, semantic action, and payload");
    require(button.text.value == "OPEN" && button.text.layout.align == ui::TextAlign::Center,
            "makeButton should configure centered label text");
    require(button.enabled && !button.clipChildren,
            "makeButton should default to enabled and avoid clipping label glyphs");
    require(button.style.hasHoverColor && button.style.hasPressedColor &&
                button.style.hasDisabledColor && button.style.hasDisabledTextColor,
            "makeButton should install complete button state colors");
}

void testRuntimeUiDisabledButtonPaintsDisabledStyle()
{
    const ui::FontAsset font = ui::makeDefaultDebugFontAsset();

    ui::UiElement root;
    root.id = "root";
    root.kind = ui::UiElementKind::Container;
    root.rect = ui::UiRect{0.0f, 0.0f, 200.0f, 100.0f};

    ui::UiButtonOptions options{};
    options.id = "disabled_button";
    options.rect = ui::UiRect{10.0f, 10.0f, 140.0f, 28.0f};
    options.label = "LOCKED";
    options.action = "locked";
    options.enabled = false;
    options.style.backgroundColor = ui::packColor(0.10f, 0.10f, 0.10f, 1.0f);
    options.style.hoverColor = ui::packColor(0.20f, 0.30f, 0.40f, 1.0f);
    options.style.pressedColor = ui::packColor(0.80f, 0.20f, 0.10f, 1.0f);
    options.style.disabledColor = ui::packColor(0.05f, 0.06f, 0.07f, 0.80f);
    options.style.textColor = ui::packColor(1.0f, 1.0f, 1.0f, 1.0f);
    options.style.disabledTextColor = ui::packColor(0.35f, 0.36f, 0.37f, 0.80f);

    root.children.push_back(ui::makeButton(std::move(options)));
    ui::computeLayout(root);

    require(!ui::hitTest(root, 20.0f, 20.0f).hit,
            "Disabled buttons should not claim pointer hits");

    ui::UiDrawList list;
    const ui::UiPaintStats stats = ui::paintTree(
        root, font, list, ui::UiPaintOptions{"disabled_button", "disabled_button"});
    require(stats.rectElements == 1 && stats.textElements == 1,
            "Disabled button painting should still emit visible chrome and text");
    require(list.vertices.size() > 4,
            "Disabled button painting should emit rect vertices followed by glyph vertices");
    require(list.vertices[0].color == ui::packColor(0.05f, 0.06f, 0.07f, 0.80f),
            "Disabled button background should override hover and pressed colors");
    require(list.vertices[4].color == ui::packColor(0.35f, 0.36f, 0.37f, 0.80f),
            "Disabled button text should use the disabled text color");
}

void testRuntimeUiTextMeasurementWrapsAndCountsLines()
{
    const ui::FontAsset font = ui::makeDefaultDebugFontAsset();

    require(ui::measureText(font, "", 16.0f).glyphCount == 0,
            "Empty text should measure zero glyphs");

    const ui::TextLayoutResult one = ui::measureText(font, "A", 16.0f);
    const ui::TextLayoutResult two = ui::measureText(font, "AA", 16.0f);
    require(one.glyphCount == 1 && two.glyphCount == 2,
            "Glyph count should track the number of visible characters");
    require(two.width > one.width, "Wider text should measure a larger advance");

    const ui::TextLayoutOptions newlineOpts;
    require(ui::measureText(font, "A\nB", 16.0f, newlineOpts).lineCount == 2,
            "Explicit newlines should produce multiple measured lines");

    ui::TextLayoutOptions wrapOpts;
    wrapOpts.maxWidth = ui::measureText(font, "AAA", 16.0f).width;
    require(ui::measureText(font, "AAAAAAAAAA", 16.0f, wrapOpts).lineCount > 1,
            "Text exceeding the max width should wrap to multiple lines");

    ui::TextLayoutOptions leftOpts;
    leftOpts.align = ui::TextAlign::Left;
    ui::TextLayoutOptions centerOpts;
    centerOpts.align = ui::TextAlign::Center;
    require(ui::measureText(font, "AAA", 16.0f, leftOpts).glyphCount ==
                ui::measureText(font, "AAA", 16.0f, centerOpts).glyphCount,
            "Alignment should not change the measured glyph count");
}

void testRuntimeUiBuildPlacementDefinitionsExposePlacementRules()
{
    const std::span<const ui::BuildCatalogItemDefinition> catalogItems =
        ui::runtimeBuildCatalogItems();
    require(catalogItems.size() == 8,
            "Runtime build catalog should expose the current smoke items and scroll rows");

    const ui::BuildCatalogItemDefinition* filter =
        ui::findRuntimeBuildCatalogItem(ui::kBuildCatalogKeyFilter);
    require(filter != nullptr, "Filter build item should be registered");
    require(filter->id == ui::kBuildCatalogItemAId &&
                filter->label == "FILTER" &&
                filter->category == "EQUIPMENT" &&
                filter->block == BLOCK_STONE &&
                filter->footprintVoxels == glm::ivec3(1) &&
                filter->requiresSurface &&
                filter->price == 80 &&
                filter->unlocked &&
                filter->iconId == "filter",
            "Filter build item should expose stable production catalog metadata");

    const std::vector<glm::ivec3> footprint =
        ui::buildPlacementFootprintCells(*filter, glm::ivec3(4, 5, 6));
    require(footprint.size() == 1 && footprint[0] == glm::ivec3(4, 5, 6),
            "A 1x1x1 footprint should occupy only the anchor cell");

    const ui::BuildCatalogItemDefinition* eelgrass =
        ui::findRuntimeBuildCatalogItem(ui::kBuildCatalogKeyEelgrass);
    require(eelgrass != nullptr && eelgrass->id == ui::kBuildCatalogItemBId &&
                eelgrass->label == "EELGRASS" &&
                eelgrass->category == "PLANTS" &&
                eelgrass->hasPlaceablePrototype() &&
                eelgrass->placeablePrototypeSlug == "eelgrass" &&
                eelgrass->placeablePrototypeVersion == 1,
            "Eelgrass should expose the foliage prototype used by runtime previews");

    const ui::BuildCatalogItemDefinition* ribbonKelp =
        ui::findRuntimeBuildCatalogItem(ui::kBuildCatalogKeyRibbonKelp);
    require(ribbonKelp != nullptr && ribbonKelp->id == ui::kBuildCatalogItemCId &&
                ribbonKelp->label == "RIBBON KELP" &&
                ribbonKelp->hasPlaceablePrototype() &&
                ribbonKelp->placeablePrototypeSlug == "ribbon_kelp" &&
                ribbonKelp->placeablePrototypeVersion == 1,
            "Ribbon kelp should be selectable as a concrete runtime plant");

    const ui::BuildCatalogItemDefinition* budCluster =
        ui::findRuntimeBuildCatalogItem(ui::kBuildCatalogKeyBudCluster);
    require(budCluster != nullptr && budCluster->id == ui::kBuildCatalogItemDId &&
                budCluster->label == "BUD CLUSTER" &&
                budCluster->hasPlaceablePrototype() &&
                budCluster->placeablePrototypeSlug == "bud_cluster" &&
                budCluster->placeablePrototypeVersion == 1,
            "Bud cluster should be selectable as a concrete runtime plant");

    const ui::BuildCatalogItemDefinition* decor =
        ui::findRuntimeBuildCatalogItem(ui::kBuildCatalogKeyDecor);
    require(decor != nullptr && decor->id == ui::kBuildCatalogItemFId &&
                decor->label == "DECOR" &&
                decor->footprintVoxels == glm::ivec3(2, 1, 1),
            "Decor build item should expose the first multi-cell footprint");
    const std::vector<glm::ivec3> decorFootprint =
        ui::buildPlacementFootprintCells(*decor, glm::ivec3(4, 5, 6));
    require(decorFootprint.size() == 2 &&
                decorFootprint[0] == glm::ivec3(4, 5, 6) &&
                decorFootprint[1] == glm::ivec3(5, 5, 6),
            "A 2x1x1 footprint should occupy two x-axis cells from the anchor");
    const std::vector<glm::ivec3> rotatedDecorFootprint =
        ui::buildPlacementFootprintCells(*decor, glm::ivec3(4, 5, 6), 1);
    require(rotatedDecorFootprint.size() == 2 &&
                rotatedDecorFootprint[0] == glm::ivec3(4, 5, 6) &&
                rotatedDecorFootprint[1] == glm::ivec3(4, 5, 7),
            "A rotated 2x1x1 footprint should occupy two z-axis cells from the anchor");
    require(ui::buildPlacementRotatedFootprintVoxels(*decor, 1) ==
                    glm::ivec3(1, 1, 2) &&
                ui::buildPlacementRotatedFootprintVoxels(*decor, 2) ==
                    glm::ivec3(2, 1, 1),
            "Rotated footprint dimensions should swap horizontal axes by quarter-turn");
    require(ui::normalizeBuildPlacementRotationSteps(-1) == 3 &&
                ui::normalizeBuildPlacementRotationSteps(5) == 1,
            "Placement rotation should normalize to a 0..3 quarter-turn range");

    const ui::BuildCatalogItemDefinition* forkedSprig =
        ui::findRuntimeBuildCatalogItem(ui::kBuildCatalogKeyForkedSprig);
    require(forkedSprig != nullptr && forkedSprig->id == ui::kBuildCatalogItemEId &&
                forkedSprig->label == "FORKED SPRIG" &&
                forkedSprig->hasPlaceablePrototype() &&
                forkedSprig->placeablePrototypeSlug == "forked_sprig" &&
                forkedSprig->placeablePrototypeVersion == 1,
            "Forked sprig should expose the foliage prototype used by runtime previews");

    const ui::BuildCatalogItemDefinition* heater =
        ui::findRuntimeBuildCatalogItem(ui::kBuildCatalogKeyHeater);
    require(heater != nullptr && heater->id == ui::kBuildCatalogItemHId &&
                heater->label == "HEATER" &&
                heater->category == "EQUIPMENT" &&
                heater->price == 150 &&
                heater->unlocked &&
                heater->iconId == "heater",
            "Heater build item should expose scroll-row production metadata");

    require(ui::findRuntimeBuildCatalogItem("missing") == nullptr,
            "Unknown build item keys should not resolve to a default item");
}

void testRuntimeUiBuildPlacementEvaluationHandlesCoreReasons()
{
    ChunkGrid grid;
    grid.create(glm::ivec3(1), glm::ivec3(0));

    const glm::ivec3 anchor(16, 16, 17);
    const ui::BuildPlacementEvaluation valid =
        ui::evaluateBuildPlacementCandidate(
            grid, ui::kBuildCatalogKeyFilter, anchor, true);
    require(valid.valid &&
                valid.invalidReason == ui::BuildPlacementInvalidReason::None &&
                valid.item != nullptr &&
                valid.block == BLOCK_STONE &&
                valid.anchorVoxel == anchor &&
                valid.footprintVoxels == glm::ivec3(1),
            "Empty in-bounds target with a surface hit should be valid");
    require(std::string(ui::buildPlacementInvalidReasonLabel(valid.invalidReason)) ==
                "none",
            "Valid placement should label its reason as none");
    require(ui::commitBuildPlacement(grid, valid),
            "Valid placement evaluation should commit to the grid");
    require(grid.getVoxelWorld(anchor.x, anchor.y, anchor.z) == BLOCK_STONE,
            "Committed placement should write the configured block");

    const ui::BuildPlacementEvaluation occupied =
        ui::evaluateBuildPlacementCandidate(
            grid, ui::kBuildCatalogKeyFilter, anchor, true);
    require(!occupied.valid &&
                occupied.invalidReason == ui::BuildPlacementInvalidReason::Occupied,
            "Occupied targets should be rejected");
    require(std::string(ui::buildPlacementInvalidReasonLabel(
                occupied.invalidReason)) == "occupied",
            "Occupied placement should expose a deterministic reason label");

    ChunkGrid multiCellGrid;
    multiCellGrid.create(glm::ivec3(1), glm::ivec3(0));
    const glm::ivec3 decorAnchor(8, 8, 8);
    const ui::BuildPlacementEvaluation decorValid =
        ui::evaluateBuildPlacementCandidate(
            multiCellGrid, ui::kBuildCatalogKeyDecor, decorAnchor, true);
    require(decorValid.valid && decorValid.footprintVoxels == glm::ivec3(2, 1, 1),
            "Empty in-bounds multi-cell decor target should be valid");
    require(ui::commitBuildPlacement(multiCellGrid, decorValid),
            "Valid multi-cell decor placement should commit");
    require(multiCellGrid.getVoxelWorld(8, 8, 8) == BLOCK_SAND &&
                multiCellGrid.getVoxelWorld(9, 8, 8) == BLOCK_SAND,
            "Multi-cell placement should write every footprint cell");

    ChunkGrid blockedMultiCellGrid;
    blockedMultiCellGrid.create(glm::ivec3(1), glm::ivec3(0));
    require(blockedMultiCellGrid.setVoxelWorld(9, 8, 8, BLOCK_STONE),
            "Blocked multi-cell test should seed the second footprint cell");
    const ui::BuildPlacementEvaluation decorBlocked =
        ui::evaluateBuildPlacementCandidate(
            blockedMultiCellGrid, ui::kBuildCatalogKeyDecor, decorAnchor, true);
    require(!decorBlocked.valid &&
                decorBlocked.invalidReason ==
                    ui::BuildPlacementInvalidReason::Occupied,
            "Multi-cell placement should reject occupancy in any footprint cell");

    ChunkGrid rotatedMultiCellGrid;
    rotatedMultiCellGrid.create(glm::ivec3(1), glm::ivec3(0));
    const ui::BuildPlacementEvaluation decorRotatedValid =
        ui::evaluateBuildPlacementCandidate(
            rotatedMultiCellGrid, ui::kBuildCatalogKeyDecor, decorAnchor, true, 1);
    require(decorRotatedValid.valid &&
                decorRotatedValid.footprintVoxels == glm::ivec3(1, 1, 2) &&
                decorRotatedValid.rotationSteps == 1,
            "Rotated multi-cell decor target should use z-axis footprint cells");
    require(ui::commitBuildPlacement(rotatedMultiCellGrid, decorRotatedValid),
            "Valid rotated multi-cell decor placement should commit");
    require(rotatedMultiCellGrid.getVoxelWorld(8, 8, 8) == BLOCK_SAND &&
                rotatedMultiCellGrid.getVoxelWorld(8, 8, 9) == BLOCK_SAND,
            "Rotated multi-cell placement should write every rotated footprint cell");

    ChunkGrid blockedRotatedMultiCellGrid;
    blockedRotatedMultiCellGrid.create(glm::ivec3(1), glm::ivec3(0));
    require(blockedRotatedMultiCellGrid.setVoxelWorld(8, 8, 9, BLOCK_STONE),
            "Blocked rotated multi-cell test should seed the second rotated cell");
    const ui::BuildPlacementEvaluation decorRotatedBlocked =
        ui::evaluateBuildPlacementCandidate(
            blockedRotatedMultiCellGrid, ui::kBuildCatalogKeyDecor,
            decorAnchor, true, 1);
    require(!decorRotatedBlocked.valid &&
                decorRotatedBlocked.footprintVoxels == glm::ivec3(1, 1, 2) &&
                decorRotatedBlocked.invalidReason ==
                    ui::BuildPlacementInvalidReason::Occupied,
            "Rotated multi-cell placement should reject occupancy in any rotated cell");

    ChunkGrid outOfBoundsGrid;
    outOfBoundsGrid.create(glm::ivec3(1), glm::ivec3(0));
    const ui::BuildPlacementEvaluation outOfBounds =
        ui::evaluateBuildPlacementCandidate(
            outOfBoundsGrid, ui::kBuildCatalogKeyFilter,
            glm::ivec3(32, 16, 17), true);
    require(!outOfBounds.valid &&
                outOfBounds.invalidReason ==
                    ui::BuildPlacementInvalidReason::OutOfBounds,
            "Targets outside the owned chunk grid should be rejected");

    const ui::BuildPlacementEvaluation noSurface =
        ui::evaluateBuildPlacementCandidate(
            outOfBoundsGrid, ui::kBuildCatalogKeyFilter, anchor, false);
    require(!noSurface.valid &&
                noSurface.invalidReason == ui::BuildPlacementInvalidReason::NoSurface,
            "Surface-required items should reject non-raycast fallback candidates");

    const ui::BuildPlacementEvaluation unknown =
        ui::evaluateBuildPlacementCandidate(
            outOfBoundsGrid, "missing", anchor, true);
    require(!unknown.valid &&
                unknown.invalidReason ==
                    ui::BuildPlacementInvalidReason::UnknownItem &&
                unknown.item == nullptr &&
                unknown.block == BLOCK_AIR,
            "Unknown item keys should fail before grid validation");
    require(!ui::commitBuildPlacement(outOfBoundsGrid, unknown),
            "Invalid placement evaluations should not commit");

    ChunkGrid removalGrid;
    removalGrid.create(glm::ivec3(1), glm::ivec3(0));
    const glm::ivec3 removalTarget(4, 4, 4);
    require(removalGrid.setVoxelWorld(removalTarget.x, removalTarget.y,
                                      removalTarget.z, BLOCK_SAND),
            "Removal evaluation should seed an occupied target");
    const ui::BuildRemovalEvaluation removalValid =
        ui::evaluateBuildRemovalCandidate(removalGrid, removalTarget, true);
    require(removalValid.valid &&
                removalValid.invalidReason == ui::BuildRemovalInvalidReason::None &&
                removalValid.block == BLOCK_SAND &&
                removalValid.targetVoxel == removalTarget,
            "Removal should accept an occupied in-bounds raycast target");
    require(std::string(ui::buildRemovalInvalidReasonLabel(
                removalValid.invalidReason)) == "none",
            "Valid removal should label its reason as none");
    require(removalGrid.getVoxelWorld(removalTarget.x, removalTarget.y,
                                      removalTarget.z) == BLOCK_SAND,
            "Evaluating removal should not mutate the grid");
    require(ui::commitBuildRemoval(removalGrid, removalValid),
            "Valid removal evaluation should commit to the grid");
    require(removalGrid.getVoxelWorld(removalTarget.x, removalTarget.y,
                                      removalTarget.z) == BLOCK_AIR,
            "Committed removal should clear the target voxel");

    const ui::BuildRemovalEvaluation removalEmpty =
        ui::evaluateBuildRemovalCandidate(removalGrid, removalTarget, true);
    require(!removalEmpty.valid &&
                removalEmpty.invalidReason == ui::BuildRemovalInvalidReason::Empty,
            "Removal should reject empty targets");
    require(std::string(ui::buildRemovalInvalidReasonLabel(
                removalEmpty.invalidReason)) == "empty",
            "Empty removal should expose a deterministic reason label");
    const ui::BuildRemovalEvaluation removalNoTarget =
        ui::evaluateBuildRemovalCandidate(removalGrid, removalTarget, false);
    require(!removalNoTarget.valid &&
                removalNoTarget.invalidReason ==
                    ui::BuildRemovalInvalidReason::NoTarget,
            "Removal should reject missing raycast targets");
    const ui::BuildRemovalEvaluation removalOutOfBounds =
        ui::evaluateBuildRemovalCandidate(removalGrid, glm::ivec3(64, 4, 4), true);
    require(!removalOutOfBounds.valid &&
                removalOutOfBounds.invalidReason ==
                    ui::BuildRemovalInvalidReason::OutOfBounds,
            "Removal should reject targets outside the owned chunk grid");
    require(!ui::commitBuildRemoval(removalGrid, removalEmpty),
            "Invalid removal evaluations should not commit");
}

void testRuntimeUiOverlaySmokeScreenFactoryBuildsExpectedLayout()
{
    const std::span<const ui::BuildCatalogItemDefinition> catalogItems =
        ui::overlaySmokeBuildCatalogItems();
    require(catalogItems.size() == 8,
            "Overlay smoke build catalog should expose generated scroll items");
    require(catalogItems[0].id == ui::kBuildCatalogItemAId &&
                catalogItems[0].key == ui::kBuildCatalogKeyFilter &&
                catalogItems[0].label == "FILTER" &&
                nearlyEqual(catalogItems[0].rowWidth, 332.0f),
            "Overlay smoke build catalog item A data should remain stable");
    require(catalogItems[1].id == ui::kBuildCatalogItemBId &&
                catalogItems[1].key == ui::kBuildCatalogKeyEelgrass &&
                catalogItems[1].label == "EELGRASS" &&
                nearlyEqual(catalogItems[1].rowWidth, 332.0f),
            "Overlay smoke build catalog item B data should remain stable");
    require(catalogItems[2].id == ui::kBuildCatalogItemCId &&
                catalogItems[2].key == ui::kBuildCatalogKeyRibbonKelp &&
                catalogItems[2].label == "RIBBON KELP" &&
                nearlyEqual(catalogItems[2].rowWidth, 352.0f),
            "Overlay smoke build catalog item C data should remain stable");
    require(catalogItems[3].id == ui::kBuildCatalogItemDId &&
                catalogItems[3].key == ui::kBuildCatalogKeyBudCluster &&
                catalogItems[3].label == "BUD CLUSTER" &&
                nearlyEqual(catalogItems[3].rowWidth, 344.0f),
            "Overlay smoke build catalog item D data should remain stable");

    ui::UiTree tree = ui::makeOverlaySmokeTree();
    require(tree.root.id == "ui_overlay_smoke_root",
            "Overlay smoke screen should build the expected root id");

    ui::computeLayout(tree.root);

    const ui::UiElement* panel = ui::findElementById(tree.root, "test_panel");
    const ui::UiElement* contentStack = ui::findElementById(tree.root, "content_stack");
    const ui::UiElement* title = ui::findElementById(tree.root, "title");
    const ui::UiElement* statusRows = ui::findElementById(tree.root, "status_rows");
    const ui::UiElement* statusFishLabel =
        ui::findElementById(tree.root, "status_row_fish_label");
    const ui::UiElement* statusFishValue =
        ui::findElementById(tree.root, "status_row_fish_value");
    const ui::UiElement* statusTimeLabel =
        ui::findElementById(tree.root, "status_row_time_label");
    const ui::UiElement* statusTimeValue =
        ui::findElementById(tree.root, "status_row_time_value");
    const ui::UiElement* statusCleanLabel =
        ui::findElementById(tree.root, "status_row_clean_label");
    const ui::UiElement* statusCleanValue =
        ui::findElementById(tree.root, "status_row_clean_value");
    const ui::UiElement* statusFishMeter =
        ui::findElementById(tree.root, "status_row_fish_meter");
    const ui::UiElement* statusCleanMeter =
        ui::findElementById(tree.root, "status_row_clean_meter");
    const ui::UiElement* growSpacer =
        ui::findElementById(tree.root, "content_grow_spacer");
    const ui::UiElement* maintainWater =
        ui::findElementById(tree.root, ui::kMaintainWaterButtonId);
    const ui::UiElement* carePanel =
        ui::findElementById(tree.root, "primary_creature_care_panel");
    const ui::UiElement* careName =
        ui::findElementById(tree.root, "care_name");
    const ui::UiElement* careHungerValue =
        ui::findElementById(tree.root, "care_hunger_value");
    const ui::UiElement* careHungerMeter =
        ui::findElementById(tree.root, "care_hunger_meter");
    const ui::UiElement* careHungerMeterTrack =
        ui::findElementById(tree.root, "care_hunger_meter_track");
    const ui::UiElement* feedPrimary =
        ui::findElementById(tree.root, ui::kFeedPrimaryCreatureButtonId);
    const ui::UiElement* catalogBar = ui::findElementById(tree.root, "catalog_bar");
    const ui::UiElement* collectionCodex =
        ui::findElementById(tree.root, ui::kCollectionCodexButtonId);
    const ui::UiElement* secondaryPanel = ui::findElementById(tree.root, "secondary_panel");
    const ui::UiElement* secondaryStack = ui::findElementById(tree.root, "secondary_stack");
    const ui::UiElement* secondaryMetricA =
        ui::findElementById(tree.root, "secondary_metric_a");
    const ui::UiElement* secondaryMetricB =
        ui::findElementById(tree.root, "secondary_metric_b");
    const ui::UiElement* secondaryMetricC =
        ui::findElementById(tree.root, "secondary_metric_c");
    const ui::UiElement* secondaryLabelO2 =
        ui::findElementById(tree.root, "secondary_label_o2");
    const ui::UiElement* secondaryLabelTemp =
        ui::findElementById(tree.root, "secondary_label_temp");
    const ui::UiElement* secondaryLabelFlow =
        ui::findElementById(tree.root, "secondary_label_flow");
    const ui::UiElement* buildCatalogPanel =
        ui::findElementById(tree.root, "build_catalog_panel");
    const ui::UiElement* buildCatalogClose =
        ui::findElementById(tree.root, "build_catalog_close");
    const ui::UiElement* buildCatalogCancel =
        ui::findElementById(tree.root, ui::kBuildCatalogCancelId);
    const ui::UiElement* buildCatalogRemove =
        ui::findElementById(tree.root, ui::kBuildCatalogRemoveId);
    const ui::UiElement* buildCatalogListViewport =
        ui::findElementById(tree.root, ui::kBuildCatalogListViewportId);
    const ui::UiElement* buildCatalogListContent =
        ui::findElementById(tree.root, ui::kBuildCatalogListContentId);
    const ui::UiElement* buildGhostPreview =
        ui::findElementById(tree.root, ui::kBuildGhostPreviewId);
    const ui::UiElement* buildGhostPreviewLabel =
        ui::findElementById(tree.root, ui::kBuildGhostPreviewLabelId);
    const ui::UiElement* buildGhostPreviewHint =
        ui::findElementById(tree.root, ui::kBuildGhostPreviewHintId);
    const ui::UiElement* buildCatalogItemA =
        ui::findElementById(tree.root, "build_catalog_item_a");
    const ui::UiElement* buildCatalogItemB =
        ui::findElementById(tree.root, "build_catalog_item_b");
    const ui::UiElement* buildCatalogItemC =
        ui::findElementById(tree.root, "build_catalog_item_c");
    const ui::UiElement* buildCatalogItemD =
        ui::findElementById(tree.root, ui::kBuildCatalogItemDId);

    require(panel != nullptr && contentStack != nullptr && title != nullptr &&
                statusRows != nullptr && statusFishLabel != nullptr &&
                statusFishValue != nullptr && statusTimeLabel != nullptr &&
                statusTimeValue != nullptr && statusCleanLabel != nullptr &&
                statusCleanValue != nullptr && statusFishMeter != nullptr &&
                statusCleanMeter != nullptr &&
                growSpacer != nullptr &&
                maintainWater != nullptr && catalogBar != nullptr &&
                collectionCodex != nullptr &&
                collectionCodex->action == ui::kOpenCollectionCodexAction,
            "Overlay smoke screen should include the primary retained panel");
    require(carePanel != nullptr && careName != nullptr &&
                careHungerValue != nullptr && careHungerMeter != nullptr &&
                careHungerMeterTrack != nullptr && feedPrimary != nullptr,
            "Overlay smoke screen should include the focused hunger/feed panel");
    require(ui::findElementById(tree.root, "care_clean_value") == nullptr &&
                ui::findElementById(tree.root, "care_happy_value") == nullptr &&
                ui::findElementById(tree.root, "care_health_value") == nullptr,
            "Compact feed HUD should leave secondary needs to a future details view");
    require(secondaryPanel != nullptr && secondaryStack != nullptr &&
                secondaryLabelO2 != nullptr && secondaryLabelTemp != nullptr &&
                secondaryLabelFlow != nullptr && secondaryMetricA != nullptr &&
                secondaryMetricB != nullptr && secondaryMetricC != nullptr,
            "Overlay smoke screen should include the secondary retained panel");
    require(buildCatalogPanel != nullptr && buildCatalogClose != nullptr &&
                buildCatalogCancel != nullptr &&
                buildCatalogRemove != nullptr &&
                buildCatalogListViewport != nullptr &&
                buildCatalogListContent != nullptr &&
                buildGhostPreview != nullptr && buildGhostPreviewLabel != nullptr &&
                buildGhostPreviewHint != nullptr &&
                buildCatalogItemA != nullptr && buildCatalogItemB != nullptr &&
                buildCatalogItemC != nullptr && buildCatalogItemD != nullptr,
            "Overlay smoke screen should include generated build catalog rows and ghost preview");

    require(rectNear(panel->computedRect, 32.0f, 32.0f, 460.0f, 240.0f),
            "Overlay smoke panel rect should remain stable for automation");
    require(rectNear(contentStack->computedRect, 56.0f, 50.0f, 408.0f, 222.0f),
            "Overlay smoke content stack rect should remain stable for automation");
    require(contentStack->layoutMode == ui::UiLayoutMode::VerticalStack,
            "Overlay smoke content should use vertical stack layout");
    require(contentStack->crossAxisAlign == ui::UiCrossAxisAlign::Start,
            "Overlay smoke content should use start cross-axis alignment");
    require(nearlyEqual(contentStack->spacing, 6.0f),
            "Overlay smoke content spacing should leave room for grow layout");
    require(rectNear(title->computedRect, 64.0f, 54.0f, 388.0f, 18.0f),
            "Overlay smoke title rect should come from the padded content stack");
    require(rectNear(statusRows->computedRect, 64.0f, 102.0f, 388.0f, 62.0f),
            "Overlay smoke status rows should occupy an intentional HUD block");
    require(statusFishLabel->text.value == "FISH" &&
                statusFishValue->text.value == "0" &&
                statusTimeLabel->text.value == "TIME" &&
                statusTimeValue->text.value == "DAY" &&
                statusCleanLabel->text.value == "CLEAN" &&
                statusCleanValue->text.value == "100%",
            "Overlay smoke status rows should expose aquarium-facing labels");
    require(rectNear(statusFishLabel->computedRect, 64.0f, 102.0f, 82.0f,
                     16.0f),
            "Overlay smoke status row labels should use a stable first column");
    require(rectNear(statusFishMeter->computedRect, 296.0f, 106.0f, 156.0f,
                     8.0f),
            "Overlay smoke status row meters should align to the right edge");
    require(rectNear(statusCleanMeter->computedRect, 324.0f, 148.0f, 128.0f,
                     8.0f),
            "Overlay smoke clean meter should align to the right edge");
    require(rectNear(carePanel->computedRect, 988.0f, 534.0f, 260.0f, 154.0f),
            "Primary care panel should retain its compact bottom-right footprint");
    require(rectNear(careHungerMeterTrack->computedRect, 1004.0f, 602.0f,
                     228.0f, 12.0f),
            "Primary care hunger track should span the compact card");
    require(rectNear(feedPrimary->computedRect, 1004.0f, 642.0f, 228.0f, 34.0f),
            "Primary care feed action should remain large and visually separated");
    ui::UiOverlaySmokeHudStatus hudStatus{};
    hudStatus.fishCount = 6;
    hudStatus.timeOfDayLabel = "SUNSET";
    hudStatus.waterOxygen = 0.5f;
    hudStatus.waterTemperatureC = 20.0f;
    hudStatus.waterFlow = 0.25f;
    hudStatus.waterCleanliness = 0.75f;
    hudStatus.primaryCreatureName = "BUBBLES";
    hudStatus.primaryCreatureHunger = 0.65f;
    hudStatus.primaryCreatureCleanliness = 0.75f;
    hudStatus.primaryCreatureHappiness = 0.85f;
    hudStatus.primaryCreatureHealth = 0.95f;
    hudStatus.creatureFeedState = ui::UiCreatureFeedState::Ready;
    require(ui::setOverlaySmokeHudStatus(tree, hudStatus),
            "Overlay smoke HUD status sync should report text changes");
    statusFishValue = ui::findElementById(tree.root, "status_row_fish_value");
    statusTimeValue = ui::findElementById(tree.root, "status_row_time_value");
    statusCleanValue = ui::findElementById(tree.root, "status_row_clean_value");
    statusCleanMeter = ui::findElementById(tree.root, "status_row_clean_meter");
    maintainWater = ui::findElementById(tree.root, ui::kMaintainWaterButtonId);
    secondaryLabelO2 = ui::findElementById(tree.root, "secondary_label_o2");
    secondaryLabelTemp = ui::findElementById(tree.root, "secondary_label_temp");
    secondaryLabelFlow = ui::findElementById(tree.root, "secondary_label_flow");
    secondaryMetricA = ui::findElementById(tree.root, "secondary_metric_a");
    secondaryMetricB = ui::findElementById(tree.root, "secondary_metric_b");
    secondaryMetricC = ui::findElementById(tree.root, "secondary_metric_c");
    careName = ui::findElementById(tree.root, "care_name");
    careHungerValue = ui::findElementById(tree.root, "care_hunger_value");
    careHungerMeter = ui::findElementById(tree.root, "care_hunger_meter");
    careHungerMeterTrack =
        ui::findElementById(tree.root, "care_hunger_meter_track");
    feedPrimary =
        ui::findElementById(tree.root, ui::kFeedPrimaryCreatureButtonId);
    require(statusFishValue != nullptr && statusFishValue->text.value == "6" &&
                statusTimeValue != nullptr && statusTimeValue->text.value == "SUNSET" &&
                statusCleanValue != nullptr && statusCleanValue->text.value == "75%" &&
                statusCleanMeter != nullptr &&
                nearlyEqual(statusCleanMeter->rect.width, 96.0f) &&
                maintainWater != nullptr && maintainWater->enabled &&
                maintainWater->text.value == "MAINTAIN WATER",
            "Overlay smoke HUD status sync should apply live fish, time, and clean values");
    require(secondaryLabelO2 != nullptr && secondaryLabelO2->text.value == "O2 50%" &&
                secondaryLabelTemp != nullptr &&
                secondaryLabelTemp->text.value == "TEMP 20C" &&
                secondaryLabelFlow != nullptr &&
                secondaryLabelFlow->text.value == "FLOW 25%" &&
                secondaryMetricA != nullptr && nearlyEqual(secondaryMetricA->rect.width, 28.0f) &&
                secondaryMetricB != nullptr &&
                nearlyEqual(secondaryMetricB->rect.width, 35.0f) &&
                secondaryMetricC != nullptr &&
                nearlyEqual(secondaryMetricC->rect.width, 10.0f),
            "Overlay smoke HUD status sync should apply live water telemetry values");
    require(careName != nullptr && careName->text.value == "BUBBLES" &&
                careHungerValue != nullptr &&
                careHungerValue->text.value == "65%" &&
                careHungerMeter != nullptr &&
                nearlyEqual(careHungerMeter->rect.width, 148.2f) &&
                careHungerMeterTrack != nullptr &&
                nearlyEqual(careHungerMeterTrack->rect.width, 228.0f) &&
                feedPrimary != nullptr && feedPrimary->enabled &&
                feedPrimary->text.value == "FEED" &&
                feedPrimary->action == ui::kFeedPrimaryCreatureAction,
            "Overlay smoke care binding should emphasize hunger and an enabled feed command");

    hudStatus.creatureFeedState = ui::UiCreatureFeedState::FedFeedback;
    require(ui::setOverlaySmokeHudStatus(tree, hudStatus),
            "Overlay smoke care binding should report feed success feedback");
    feedPrimary =
        ui::findElementById(tree.root, ui::kFeedPrimaryCreatureButtonId);
    require(feedPrimary != nullptr && !feedPrimary->enabled &&
                feedPrimary->text.value == "FED!",
            "Successful feeding should provide clear transient HUD feedback");
    hudStatus.creatureFeedState = ui::UiCreatureFeedState::Cooldown;
    hudStatus.creatureFeedCooldownRemaining = 0.64f;
    require(ui::setOverlaySmokeHudStatus(tree, hudStatus),
            "Overlay smoke care binding should report cooldown feedback");
    feedPrimary =
        ui::findElementById(tree.root, ui::kFeedPrimaryCreatureButtonId);
    require(feedPrimary != nullptr && !feedPrimary->enabled &&
                feedPrimary->text.value == "FEED 0.7S",
            "Feed cooldown should remain visible and non-interactive until ready");
    hudStatus.waterOxygen = 2.0f;
    hudStatus.waterTemperatureC = -4.0f;
    hudStatus.waterFlow = -1.0f;
    hudStatus.waterCleanliness = -1.0f;
    require(ui::setOverlaySmokeHudStatus(tree, hudStatus),
            "Overlay smoke HUD status sync should report clamped telemetry changes");
    statusCleanValue = ui::findElementById(tree.root, "status_row_clean_value");
    statusCleanMeter = ui::findElementById(tree.root, "status_row_clean_meter");
    secondaryLabelO2 = ui::findElementById(tree.root, "secondary_label_o2");
    secondaryLabelTemp = ui::findElementById(tree.root, "secondary_label_temp");
    secondaryLabelFlow = ui::findElementById(tree.root, "secondary_label_flow");
    secondaryMetricA = ui::findElementById(tree.root, "secondary_metric_a");
    secondaryMetricB = ui::findElementById(tree.root, "secondary_metric_b");
    secondaryMetricC = ui::findElementById(tree.root, "secondary_metric_c");
    require(statusCleanValue != nullptr && statusCleanValue->text.value == "0%" &&
                statusCleanMeter != nullptr &&
                nearlyEqual(statusCleanMeter->rect.width, 0.0f) &&
                maintainWater != nullptr && maintainWater->enabled &&
                secondaryLabelO2 != nullptr && secondaryLabelO2->text.value == "O2 100%" &&
                secondaryLabelTemp != nullptr &&
                secondaryLabelTemp->text.value == "TEMP 0C" &&
                secondaryLabelFlow != nullptr &&
                secondaryLabelFlow->text.value == "FLOW 0%" &&
                secondaryMetricA != nullptr && nearlyEqual(secondaryMetricA->rect.width, 56.0f) &&
                secondaryMetricB != nullptr &&
                nearlyEqual(secondaryMetricB->rect.width, 0.0f) &&
                secondaryMetricC != nullptr &&
                nearlyEqual(secondaryMetricC->rect.width, 0.0f),
            "Overlay smoke HUD status sync should clamp water telemetry bar widths");
    hudStatus.waterOxygen = 0.90f;
    hudStatus.waterCleanliness = 1.0f;
    hudStatus.waterMaintenanceFeedbackActive = false;
    require(ui::setOverlaySmokeHudStatus(tree, hudStatus),
            "Overlay smoke HUD status sync should report maintenance disabled state changes");
    maintainWater = ui::findElementById(tree.root, ui::kMaintainWaterButtonId);
    require(maintainWater != nullptr && !maintainWater->enabled &&
                maintainWater->text.value == "MAINTAIN WATER",
            "Overlay smoke water maintenance should dim while water is already stable");
    const ui::UiHitResult stableMaintainHit = ui::hitTest(tree.root, 96.0f, 210.0f);
    require(stableMaintainHit.hit && stableMaintainHit.element != nullptr &&
                stableMaintainHit.element->id == "test_panel",
            "Disabled water maintenance should not own pointer hits");
    hudStatus.waterMaintenanceFeedbackActive = true;
    require(ui::setOverlaySmokeHudStatus(tree, hudStatus),
            "Overlay smoke HUD status sync should report maintenance feedback changes");
    statusCleanValue = ui::findElementById(tree.root, "status_row_clean_value");
    statusCleanMeter = ui::findElementById(tree.root, "status_row_clean_meter");
    maintainWater = ui::findElementById(tree.root, ui::kMaintainWaterButtonId);
    require(statusCleanValue != nullptr && statusCleanMeter != nullptr &&
                maintainWater != nullptr && !maintainWater->enabled &&
                maintainWater->text.value == "WATER STABLE" &&
                statusCleanValue->style.color == statusCleanMeter->style.color,
            "Overlay smoke water maintenance feedback should pulse clean status and button copy");
    require(nearlyEqual(growSpacer->grow, 1.0f),
            "Overlay smoke content should include a grow spacer");
    require(rectNear(growSpacer->computedRect, 64.0f, 170.0f, 388.0f, 20.0f),
            "Overlay smoke grow spacer should absorb the remaining content height");
    require(rectNear(maintainWater->computedRect, 64.0f, 196.0f, 388.0f, 26.0f),
            "Overlay smoke water maintenance button should sit above the catalog bar");
    require(maintainWater->kind == ui::UiElementKind::Button &&
                maintainWater->text.value == "WATER STABLE" &&
                maintainWater->action == ui::kMaintainWaterAction,
            "Overlay smoke water maintenance button should expose its semantic action");
    require(rectNear(catalogBar->computedRect, 64.0f, 228.0f, 388.0f, 28.0f),
            "Overlay smoke catalog bar should stay inside the padded content stack");
    require(catalogBar->kind == ui::UiElementKind::Button,
            "Overlay smoke catalog bar should be the first retained button");
    require(catalogBar->text.value == "BUILD CATALOG",
            "Overlay smoke catalog button should expose a visible label");

    require(rectNear(secondaryPanel->computedRect, 32.0f, 300.0f, 460.0f, 48.0f),
            "Overlay smoke secondary panel rect should remain stable");
    require(secondaryStack->layoutMode == ui::UiLayoutMode::HorizontalStack,
            "Overlay smoke secondary stack should use horizontal layout");
    require(secondaryStack->crossAxisAlign == ui::UiCrossAxisAlign::Center,
            "Overlay smoke secondary stack should vertically center children");
    require(rectNear(secondaryMetricA->computedRect, 118.0f, 320.0f, 56.0f, 8.0f),
            "Overlay smoke secondary metric should sit beside its telemetry label");
    require(rectNear(secondaryLabelO2->computedRect, 48.0f, 318.0f, 64.0f, 12.0f),
            "Overlay smoke secondary telemetry labels should identify the strip metrics");

    require(ui::setOverlaySmokeViewport(tree, 1920.0f, 1080.0f),
            "Overlay smoke viewport application should report layout-affecting changes");
    ui::computeLayout(tree.root);
    const ui::UiElement* fishPanel =
        ui::findElementById(tree.root, ui::kFishListPanelId);
    secondaryPanel = ui::findElementById(tree.root, "secondary_panel");
    secondaryStack = ui::findElementById(tree.root, "secondary_stack");
    secondaryMetricA = ui::findElementById(tree.root, "secondary_metric_a");
    require(rectNear(tree.root.computedRect, 0.0f, 0.0f, 1920.0f, 1080.0f),
            "Overlay smoke root should expand to the runtime viewport");
    require(fishPanel != nullptr &&
                rectNear(fishPanel->computedRect, 1668.0f, 32.0f, 220.0f, 260.0f),
            "Fish sidebar should anchor to the right edge of a wide viewport");
    require(secondaryPanel != nullptr &&
                rectNear(secondaryPanel->computedRect, 32.0f, 960.0f, 460.0f, 48.0f),
            "Secondary HUD strip should anchor near the lower-left with a safe bottom gap");
    require(secondaryStack != nullptr && secondaryMetricA != nullptr &&
                rectNear(secondaryMetricA->computedRect, 118.0f, 980.0f, 50.4f, 8.0f),
            "Secondary HUD children should follow the viewport-anchored panel");

    require(!buildCatalogPanel->visible && !buildCatalogPanel->enabled,
            "Overlay smoke build catalog shell should start closed");
    require(!buildGhostPreview->visible && !buildGhostPreview->enabled,
            "Overlay smoke build ghost preview should start hidden and non-interactive");
    require(buildGhostPreviewLabel->text.value == "PLACING ITEM" &&
                buildGhostPreviewHint != nullptr &&
                buildGhostPreviewHint->text.value == "ROT 0 READY",
            "Overlay smoke build ghost preview should have a default label");
    require(buildCatalogClose->action == ui::kCloseBuildCatalogAction,
            "Overlay smoke build catalog shell should expose a close action");
    require(buildCatalogCancel->action == ui::kCancelBuildPlacementAction,
            "Overlay smoke build catalog shell should expose a cancel placement action");
    require(buildCatalogRemove->action == ui::kSelectRemovePlacementAction &&
                buildCatalogRemove->actionPayload == ui::kBuildRemoveKey,
            "Overlay smoke build catalog shell should expose deliberate remove mode");
    require(buildCatalogItemA->kind == ui::UiElementKind::Button,
            "Overlay smoke build catalog item A should be a retained button");
    require(buildCatalogItemA->action == ui::kSelectBuildCatalogItemAction,
            "Overlay smoke build catalog item A should expose a selection action");
    require(buildCatalogItemA->actionPayload == ui::kBuildCatalogKeyFilter,
            "Overlay smoke build catalog item A should expose the filter item key");
    require(buildCatalogItemB->kind == ui::UiElementKind::Button &&
                buildCatalogItemB->action == ui::kSelectBuildCatalogItemAction &&
                buildCatalogItemB->actionPayload == ui::kBuildCatalogKeyEelgrass &&
                buildCatalogItemB->text.value == "EELGRASS",
            "Overlay smoke build catalog item B should be generated as a selectable button");
    require(buildCatalogItemC->kind == ui::UiElementKind::Button &&
                buildCatalogItemC->action == ui::kSelectBuildCatalogItemAction &&
                buildCatalogItemC->actionPayload == ui::kBuildCatalogKeyRibbonKelp &&
                buildCatalogItemC->text.value == "RIBBON KELP",
            "Overlay smoke build catalog item C should be generated as a selectable button");
    require(!ui::hitTest(tree.root, 96.0f, 112.0f).hit ||
                ui::hitTest(tree.root, 96.0f, 112.0f).element->id !=
                    "build_catalog_panel",
            "Closed build catalog shell should not claim hits");

    ui::setOverlaySmokeBuildCatalogOpen(tree, true);
    ui::computeLayout(tree.root);
    buildCatalogPanel = ui::findElementById(tree.root, "build_catalog_panel");
    buildCatalogClose = ui::findElementById(tree.root, "build_catalog_close");
    buildCatalogCancel = ui::findElementById(tree.root, ui::kBuildCatalogCancelId);
    buildCatalogRemove = ui::findElementById(tree.root, ui::kBuildCatalogRemoveId);
    buildCatalogListViewport =
        ui::findElementById(tree.root, ui::kBuildCatalogListViewportId);
    buildCatalogListContent =
        ui::findElementById(tree.root, ui::kBuildCatalogListContentId);
    buildCatalogItemA = ui::findElementById(tree.root, "build_catalog_item_a");
    buildCatalogItemB = ui::findElementById(tree.root, "build_catalog_item_b");
    buildCatalogItemC = ui::findElementById(tree.root, "build_catalog_item_c");
    buildCatalogItemD = ui::findElementById(tree.root, ui::kBuildCatalogItemDId);
    require(buildCatalogPanel != nullptr && buildCatalogPanel->visible &&
                buildCatalogPanel->enabled,
            "Opening the build catalog shell should make it visible and enabled");
    require(rectNear(buildCatalogPanel->computedRect, 32.0f, 288.0f, 460.0f, 260.0f),
            "Opened build catalog shell should keep a stable rect");
    require(buildCatalogClose != nullptr &&
                rectNear(buildCatalogClose->computedRect, 396.0f, 310.0f, 72.0f,
                         26.0f),
            "Opened build catalog close button should keep a stable clickable rect");
    require(buildCatalogCancel != nullptr &&
                rectNear(buildCatalogCancel->computedRect, 56.0f, 512.0f, 176.0f,
                         26.0f),
            "Opened build catalog cancel button should keep a stable clickable rect");
    require(buildCatalogRemove != nullptr &&
                rectNear(buildCatalogRemove->computedRect, 276.0f, 512.0f, 176.0f,
                         26.0f),
            "Opened build catalog remove button should keep a stable clickable rect");
    require(buildCatalogListViewport != nullptr &&
                rectNear(buildCatalogListViewport->computedRect, 56.0f, 380.0f,
                         412.0f, 124.0f) &&
                nearlyEqual(buildCatalogListViewport->scrollOffsetY, 0.0f),
            "Opened build catalog list viewport should keep a stable clipped rect");
    require(buildCatalogListContent != nullptr &&
                rectNear(buildCatalogListContent->computedRect, 56.0f, 380.0f,
                         412.0f, 308.0f),
            "Opened build catalog list content should expose the full scroll height");
    require(buildCatalogItemA != nullptr &&
                rectNear(buildCatalogItemA->computedRect, 56.0f, 384.0f, 332.0f,
                         28.0f),
            "Opened build catalog item A should keep a stable clickable rect");
    require(buildCatalogItemB != nullptr &&
                rectNear(buildCatalogItemB->computedRect, 56.0f, 422.0f, 332.0f,
                         28.0f),
            "Opened build catalog item B should keep a stable generated rect");
    require(buildCatalogItemC != nullptr &&
                rectNear(buildCatalogItemC->computedRect, 56.0f, 460.0f, 352.0f,
                         28.0f),
            "Opened build catalog item C should keep a stable generated rect");
    require(buildCatalogItemD != nullptr &&
                rectNear(buildCatalogItemD->computedRect, 56.0f, 498.0f, 344.0f,
                         28.0f),
            "Opened build catalog item D should start below the clipped viewport");
    const ui::UiHitResult catalogHit = ui::hitTest(tree.root, 44.0f, 300.0f);
    require(catalogHit.hit && catalogHit.element != nullptr &&
                catalogHit.element->id == "build_catalog_panel",
            "Opened build catalog shell should claim panel hits");
    const ui::UiHitResult closeHit = ui::hitTest(tree.root, 420.0f, 322.0f);
    require(closeHit.hit && closeHit.element != nullptr &&
                closeHit.element->id == "build_catalog_close",
            "Opened build catalog close button should claim close hits");
    const ui::UiHitResult itemHit = ui::hitTest(tree.root, 72.0f, 398.0f);
    require(itemHit.hit && itemHit.element != nullptr &&
                itemHit.element->id == "build_catalog_item_a",
            "Opened build catalog item A should claim row hits");
    const ui::UiHitResult hiddenItemHit = ui::hitTest(tree.root, 72.0f, 524.0f);
    require(hiddenItemHit.hit && hiddenItemHit.element != nullptr &&
                hiddenItemHit.element->id == ui::kBuildCatalogCancelId,
            "Unscrolled off-screen catalog rows should not claim hits past the viewport");
    const ui::UiHitResult cancelHit = ui::hitTest(tree.root, 72.0f, 524.0f);
    require(cancelHit.hit && cancelHit.element != nullptr &&
                cancelHit.element->id == ui::kBuildCatalogCancelId,
            "Opened build catalog cancel button should claim cancel hits");
    const ui::UiHitResult removeHit = ui::hitTest(tree.root, 316.0f, 524.0f);
    require(removeHit.hit && removeHit.element != nullptr &&
                removeHit.element->id == ui::kBuildCatalogRemoveId,
            "Opened build catalog remove button should claim remove hits");

    ui::setOverlaySmokeBuildCatalogScrollOffset(tree, 114.0f);
    ui::computeLayout(tree.root);
    buildCatalogListViewport =
        ui::findElementById(tree.root, ui::kBuildCatalogListViewportId);
    buildCatalogItemA = ui::findElementById(tree.root, "build_catalog_item_a");
    buildCatalogItemD = ui::findElementById(tree.root, ui::kBuildCatalogItemDId);
    require(buildCatalogListViewport != nullptr &&
                nearlyEqual(buildCatalogListViewport->scrollOffsetY, 114.0f),
            "Build catalog viewport should store the clamped scroll offset");
    require(buildCatalogItemA != nullptr &&
                rectNear(buildCatalogItemA->computedRect, 56.0f, 270.0f, 332.0f,
                         28.0f),
            "Scrolled catalog item A should move above the clipped viewport");
    require(buildCatalogItemD != nullptr &&
                rectNear(buildCatalogItemD->computedRect, 56.0f, 384.0f, 344.0f,
                         28.0f),
            "Scrolled catalog item D should move into the first visible row slot");
    const ui::UiHitResult scrolledItemHit = ui::hitTest(tree.root, 72.0f, 398.0f);
    require(scrolledItemHit.hit && scrolledItemHit.element != nullptr &&
                scrolledItemHit.element->id == ui::kBuildCatalogItemDId,
            "Scrolled build catalog item D should claim the row hit");

    ui::setOverlaySmokeBuildGhostPreview(tree, true, ui::kBuildCatalogKeyFilter);
    ui::computeLayout(tree.root);
    buildGhostPreview = ui::findElementById(tree.root, ui::kBuildGhostPreviewId);
    buildGhostPreviewLabel = ui::findElementById(tree.root, ui::kBuildGhostPreviewLabelId);
    buildGhostPreviewHint = ui::findElementById(tree.root, ui::kBuildGhostPreviewHintId);
    require(buildGhostPreview != nullptr && buildGhostPreview->visible &&
                !buildGhostPreview->enabled,
            "Build ghost preview should become visible without claiming input");
    require(rectNear(buildGhostPreview->computedRect, 600.0f, 332.0f, 96.0f, 96.0f),
            "Build ghost preview should keep a stable placeholder rect");
    require(buildGhostPreviewLabel != nullptr &&
                buildGhostPreviewLabel->text.value == "PLACING FILTER" &&
                buildGhostPreviewHint != nullptr &&
                buildGhostPreviewHint->text.value == "ROT 0 READY",
            "Build ghost preview should expose the selected item key in its label");
    ui::setOverlaySmokeBuildGhostPreview(tree, true, ui::kBuildCatalogKeyFilter, 1);
    buildGhostPreviewHint = ui::findElementById(tree.root, ui::kBuildGhostPreviewHintId);
    require(buildGhostPreviewHint != nullptr &&
                buildGhostPreviewHint->text.value == "ROT 90 READY",
            "Build ghost preview should expose the current rotation state");
    ui::setOverlaySmokeBuildGhostPreview(tree, true, ui::kBuildRemoveKey);
    ui::computeLayout(tree.root);
    buildGhostPreviewLabel = ui::findElementById(tree.root, ui::kBuildGhostPreviewLabelId);
    buildGhostPreviewHint = ui::findElementById(tree.root, ui::kBuildGhostPreviewHintId);
    require(buildGhostPreviewLabel != nullptr &&
                buildGhostPreviewLabel->text.value == "REMOVING TARGET" &&
                buildGhostPreviewHint != nullptr &&
                buildGhostPreviewHint->text.value == "TARGET READY",
            "Build ghost preview should expose remove mode in its label");
    require(!ui::hitTest(tree.root, 616.0f, 348.0f).hit,
            "Disabled build ghost preview should not claim pointer ownership");

    ui::setOverlaySmokeBuildGhostPreview(tree, false, {});
    ui::computeLayout(tree.root);
    buildGhostPreview = ui::findElementById(tree.root, ui::kBuildGhostPreviewId);
    require(buildGhostPreview != nullptr && !buildGhostPreview->visible &&
                !buildGhostPreview->enabled,
            "Build ghost preview should hide cleanly after reset");
}

void testRuntimeUiContextBuildsRegisteredScreen()
{
    ui::UiContext context;
    require(!context.hasActiveScreen(), "A new UI context should start without a screen");
    require(context.activeTree() == nullptr,
            "A new UI context should not expose an active tree");

    require(context.setActiveScreen(ui::kOverlaySmokeScreenId),
            "The overlay smoke screen should be registered");
    require(context.hasActiveScreen(), "Setting a screen should mark the context active");
    require(context.activeScreenRegistered(),
            "The active overlay smoke screen should report as registered");
    require(context.activeTree() == nullptr,
            "Selecting a screen should not build the tree until requested");

    ui::UiTree* tree = context.ensureActiveTree();
    require(tree != nullptr, "A registered active screen should build a retained tree");
    require(context.activeTreeBuilt(), "ensureActiveTree should mark the tree as built");
    require(tree->root.id == "ui_overlay_smoke_root",
            "The context should build the registered smoke screen tree");
    require(context.ensureActiveTree() == tree,
            "ensureActiveTree should reuse the cached retained tree");

    ui::computeLayout(tree->root);
    const ui::UiElement* panel = ui::findElementById(tree->root, "test_panel");
    require(panel != nullptr && rectNear(panel->computedRect, 32.0f, 32.0f, 460.0f,
                                         240.0f),
            "The context-built smoke screen should keep the automation panel rect");
}

void testRuntimeUiContextHandlesUnknownScreensAndInvalidation()
{
    ui::UiContext context;

    require(!context.setActiveScreen("missing_screen"),
            "Unknown screens should be stored but report as unregistered");
    require(context.hasActiveScreen(), "Unknown screen requests should preserve the id");
    require(context.activeScreenId() == "missing_screen",
            "The context should keep the requested unknown screen id");
    require(!context.activeScreenRegistered(),
            "Unknown screens should not report as registered");
    require(context.ensureActiveTree() == nullptr,
            "Unknown screens should not build retained trees");

    require(context.setActiveScreen(ui::kOverlaySmokeScreenId),
            "Switching back to a registered screen should succeed");
    ui::UiTree* builtTree = context.ensureActiveTree();
    require(builtTree != nullptr, "Registered screen should build after an unknown screen");
    context.tweens().start(
        ui::UiTweenSpec{"test_panel", ui::UiTweenProperty::Opacity, 0.0f, 1.0f,
                        1.0f, ui::UiEase::Linear},
        *builtTree);
    require(context.tweens().anyActive(),
            "The transition test should begin with active presentation work");
    context.invalidateActiveTree();
    require(!context.activeTreeBuilt(), "invalidateActiveTree should drop the cached tree");
    require(context.activeTree() == nullptr,
            "invalidateActiveTree should hide the stale cached tree");
    require(!context.tweens().anyActive() && !context.tweens().instant(),
            "Tree invalidation should discard tweens that target the replaced tree");
    require(context.ensureActiveTree() != nullptr,
            "A registered active screen should rebuild after invalidation");

    context.clearActiveScreen();
    require(!context.hasActiveScreen(), "clearActiveScreen should remove the active id");
    require(context.ensureActiveTree() == nullptr,
            "A cleared context should not build a tree");
}

void testRuntimeUiContextTracksPointerClickState()
{
    ui::UiContext context;
    require(context.setActiveScreen(ui::kOverlaySmokeScreenId),
            "Pointer-state test should select a registered screen");
    ui::UiTree* tree = context.ensureActiveTree();
    require(tree != nullptr, "Pointer-state test should build a retained tree");
    ui::computeLayout(tree->root);

    const ui::UiHitResult buttonHit = ui::hitTest(tree->root, 96.0f, 240.0f);
    require(buttonHit.hit && buttonHit.element != nullptr &&
                buttonHit.element->id == "catalog_bar",
            "Pointer-state test should hit the smoke catalog bar");

    context.updatePointerHover(buttonHit);
    require(context.pointerState().hoveredElementId == "catalog_bar",
            "Hover state should track the current hit element");
    require(context.pointerState().pressedElementId.empty(),
            "Hovering should not press an element");

    context.handlePointerDown(buttonHit);
    require(context.pointerState().pressedElementId == "catalog_bar",
            "Pointer down should store the pressed element id");
    require(!context.pointerState().clickPending,
            "Pointer down should clear pending clicks");

    require(context.handlePointerUp(buttonHit),
            "Pointer up over the pressed element should produce a click");
    require(context.pointerState().pressedElementId.empty(),
            "Pointer up should clear the pressed element id");
    require(context.pointerState().clickPending,
            "A matching pointer up should leave a pending click");
    require(context.pointerState().clickedElementId == "catalog_bar",
            "A matching pointer up should store the clicked element id");
    require(context.consumeClickedElementId() == "catalog_bar",
            "consumeClickedElementId should return the pending clicked id");
    require(context.consumeClickedElementId().empty(),
            "consumeClickedElementId should clear the pending clicked id");
}

void testRuntimeUiContextConsumesSemanticActionEvents()
{
    ui::UiContext context;
    require(context.setActiveScreen(ui::kOverlaySmokeScreenId),
            "Action-event test should select a registered screen");
    ui::UiTree* tree = context.ensureActiveTree();
    require(tree != nullptr, "Action-event test should build a retained tree");
    ui::computeLayout(tree->root);

    ui::UiOverlaySmokeHudStatus hudStatus{};
    hudStatus.waterCleanliness = 0.50f;
    hudStatus.waterOxygen = 0.70f;
    hudStatus.primaryCreatureName = "BUBBLES";
    hudStatus.primaryCreatureHunger = 0.65f;
    hudStatus.creatureFeedState = ui::UiCreatureFeedState::Ready;
    require(ui::setOverlaySmokeHudStatus(*tree, hudStatus),
            "Action-event test should enable water maintenance for dirty water");

    const ui::UiElement* catalogBar = ui::findElementById(tree->root, "catalog_bar");
    require(catalogBar != nullptr && catalogBar->action == ui::kOpenBuildCatalogAction,
            "The smoke catalog button should expose a semantic action");
    const ui::UiElement* maintainWater =
        ui::findElementById(tree->root, ui::kMaintainWaterButtonId);
    require(maintainWater != nullptr &&
                maintainWater->action == ui::kMaintainWaterAction,
            "The smoke water maintenance button should expose a semantic action");

    const ui::UiHitResult maintainHit = ui::hitTest(tree->root, 96.0f, 210.0f);
    require(maintainHit.hit && maintainHit.element != nullptr &&
                maintainHit.element->id == ui::kMaintainWaterButtonId,
            "Action-event test should hit the water maintenance button");
    context.handlePointerDown(maintainHit);
    require(context.handlePointerUp(maintainHit),
            "Action-event test should produce a water maintenance click");

    const ui::UiActionEvent maintainEvent = context.consumeActionEvent();
    require(maintainEvent.elementId == ui::kMaintainWaterButtonId,
            "consumeActionEvent should report the water maintenance button id");
    require(maintainEvent.action == ui::kMaintainWaterAction,
            "consumeActionEvent should report the water maintenance action");
    require(maintainEvent.payload.empty(),
            "Water maintenance should not carry an action payload");

    const ui::UiElement* feedPrimary =
        ui::findElementById(tree->root, ui::kFeedPrimaryCreatureButtonId);
    require(feedPrimary != nullptr && feedPrimary->enabled &&
                feedPrimary->action == ui::kFeedPrimaryCreatureAction,
            "The primary-care feed button should expose a semantic action");
    ui::computeLayout(tree->root);
    feedPrimary =
        ui::findElementById(tree->root, ui::kFeedPrimaryCreatureButtonId);
    require(feedPrimary != nullptr,
            "The feed button should remain retained after layout");
    const float feedX = feedPrimary->computedRect.x + 4.0f;
    const float feedY = feedPrimary->computedRect.y + 4.0f;
    const ui::UiHitResult feedHit = ui::hitTest(tree->root, feedX, feedY);
    require(feedHit.hit && feedHit.element != nullptr &&
                feedHit.element->id == ui::kFeedPrimaryCreatureButtonId,
            "Action-event test should hit the feed button");
    context.handlePointerDown(feedHit);
    require(context.handlePointerUp(feedHit),
            "Action-event test should produce a feed click");
    const ui::UiActionEvent feedEvent = context.consumeActionEvent();
    require(feedEvent.elementId == ui::kFeedPrimaryCreatureButtonId &&
                feedEvent.action == ui::kFeedPrimaryCreatureAction &&
                feedEvent.payload.empty(),
            "The feed click should report its semantic command without UI-owned data");

    const ui::UiHitResult buttonHit = ui::hitTest(tree->root, 96.0f, 240.0f);
    context.handlePointerDown(buttonHit);
    require(context.handlePointerUp(buttonHit),
            "Action-event test should produce a button click");

    const ui::UiActionEvent event = context.consumeActionEvent();
    require(event.elementId == "catalog_bar",
            "consumeActionEvent should report the clicked element id");
    require(event.action == ui::kOpenBuildCatalogAction,
            "consumeActionEvent should report the clicked element action");
    require(context.consumeActionEvent().elementId.empty(),
            "consumeActionEvent should clear the pending event");

    ui::setOverlaySmokeBuildCatalogOpen(*tree, true);
    ui::computeLayout(tree->root);
    const ui::UiHitResult catalogItemHit = ui::hitTest(tree->root, 72.0f, 398.0f);
    require(catalogItemHit.hit && catalogItemHit.element != nullptr &&
                catalogItemHit.element->id == ui::kBuildCatalogItemAId,
            "Action-event test should hit generated catalog item A");

    context.handlePointerDown(catalogItemHit);
    require(context.handlePointerUp(catalogItemHit),
            "Action-event test should produce a catalog item click");

    const ui::UiActionEvent catalogEvent = context.consumeActionEvent();
    require(catalogEvent.elementId == ui::kBuildCatalogItemAId,
            "consumeActionEvent should report the generated catalog item id");
    require(catalogEvent.action == ui::kSelectBuildCatalogItemAction,
            "consumeActionEvent should report the generated catalog item action");
    require(catalogEvent.payload == ui::kBuildCatalogKeyFilter,
            "consumeActionEvent should report the generated catalog item key payload");

    const ui::UiHitResult cancelHit = ui::hitTest(tree->root, 72.0f, 524.0f);
    require(cancelHit.hit && cancelHit.element != nullptr &&
                cancelHit.element->id == ui::kBuildCatalogCancelId,
            "Action-event test should hit the build placement cancel button");

    context.handlePointerDown(cancelHit);
    require(context.handlePointerUp(cancelHit),
            "Action-event test should produce a cancel placement click");

    const ui::UiActionEvent cancelEvent = context.consumeActionEvent();
    require(cancelEvent.elementId == ui::kBuildCatalogCancelId,
            "consumeActionEvent should report the cancel button id");
    require(cancelEvent.action == ui::kCancelBuildPlacementAction,
            "consumeActionEvent should report the cancel placement action");
    require(cancelEvent.payload.empty(),
            "Cancel placement should not carry a catalog item payload");

    const ui::UiHitResult removeHit = ui::hitTest(tree->root, 316.0f, 524.0f);
    require(removeHit.hit && removeHit.element != nullptr &&
                removeHit.element->id == ui::kBuildCatalogRemoveId,
            "Action-event test should hit the build removal button");

    context.handlePointerDown(removeHit);
    require(context.handlePointerUp(removeHit),
            "Action-event test should produce a remove-mode click");

    const ui::UiActionEvent removeEvent = context.consumeActionEvent();
    require(removeEvent.elementId == ui::kBuildCatalogRemoveId,
            "consumeActionEvent should report the remove button id");
    require(removeEvent.action == ui::kSelectRemovePlacementAction,
            "consumeActionEvent should report the remove-mode action");
    require(removeEvent.payload == ui::kBuildRemoveKey,
            "Remove mode should carry the stable remove payload");
}

void testRuntimeUiActionDispatcherInvokesRegisteredHandlers()
{
    ui::UiActionDispatcher dispatcher;
    int handledCount = 0;
    std::string handledElement;
    std::string handledAction;
    std::string handledPayload;

    dispatcher.registerHandler("open", [&](const ui::UiActionEvent& event) {
        ++handledCount;
        handledElement = event.elementId;
        handledAction = event.action;
        handledPayload = event.payload;
    });

    require(dispatcher.hasHandler("open"),
            "Action dispatcher should report registered handlers");
    require(!dispatcher.hasHandler("missing"),
            "Action dispatcher should report missing handlers");

    const ui::UiActionDispatchResult handled =
        dispatcher.dispatch(ui::UiActionEvent{"button", "open", "payload"});
    require(handled.handled && handled.elementId == "button" &&
                handled.action == "open" && handled.payload == "payload",
            "Action dispatcher should report handled action results");
    require(handledCount == 1 && handledElement == "button" &&
                handledAction == "open" && handledPayload == "payload",
            "Action dispatcher should invoke the registered handler");

    const ui::UiActionDispatchResult unhandled =
        dispatcher.dispatch(ui::UiActionEvent{"button", "missing", "payload"});
    require(!unhandled.handled && unhandled.elementId == "button" &&
                unhandled.action == "missing" && unhandled.payload == "payload",
            "Action dispatcher should preserve unhandled action metadata");
    require(handledCount == 1, "Unhandled actions should not call prior handlers");

    dispatcher.clear();
    require(!dispatcher.hasHandler("open"),
            "Action dispatcher clear should remove registered handlers");
}

void testRuntimeUiOverlaySmokeScreenControllerOwnsDynamicState()
{
    ui::OverlaySmokeScreenController controller;
    ui::UiActionDispatcher dispatcher;
    controller.registerActions(dispatcher);

    require(dispatcher.hasHandler(ui::kOpenBuildCatalogAction),
            "Overlay smoke controller should register catalog open handling");
    require(dispatcher.hasHandler(ui::kCloseBuildCatalogAction),
            "Overlay smoke controller should register catalog close handling");
    require(dispatcher.hasHandler(ui::kSelectBuildCatalogItemAction),
            "Overlay smoke controller should register catalog selection handling");
    require(dispatcher.hasHandler(ui::kCancelBuildPlacementAction),
            "Overlay smoke controller should register pending-placement cancel handling");
    require(dispatcher.hasHandler(ui::kCommitBuildPlacementAction),
            "Overlay smoke controller should register pending-placement commit handling");
    require(dispatcher.hasHandler(ui::kSelectRemovePlacementAction),
            "Overlay smoke controller should register removal-mode selection handling");
    require(dispatcher.hasHandler(ui::kRemoveBuildPlacementAction),
            "Overlay smoke controller should register removal commit handling");
    require(std::string(controller.buildModeStateLogValue()) == "inactive",
            "Overlay smoke controller should start with inactive build mode");
    require(controller.selectedCatalogItemLogValue() == "<none>" &&
                controller.pendingBuildItemKeyLogValue() == "<none>",
            "Overlay smoke controller should start with no selection or pending key");

    ui::UiTree tree = ui::makeOverlaySmokeTree();
    controller.syncTree(tree);
    ui::computeLayout(tree.root);
    const ui::UiElement* panel =
        ui::findElementById(tree.root, "build_catalog_panel");
    const ui::UiElement* preview =
        ui::findElementById(tree.root, ui::kBuildGhostPreviewId);
    require(panel != nullptr && !panel->visible,
            "Controller sync should keep the catalog panel hidden initially");
    require(preview != nullptr && !preview->visible,
            "Controller sync should keep the ghost preview hidden initially");

    const ui::UiActionDispatchResult opened =
        dispatcher.dispatch(ui::UiActionEvent{"catalog_bar",
                                              std::string(ui::kOpenBuildCatalogAction), {}});
    require(opened.handled && controller.state().buildCatalogOpen,
            "Controller should open the catalog through the action dispatcher");
    require(controller.shouldLogBuildCatalogShell(),
            "Controller should request a catalog-shell log after opening");
    controller.markBuildCatalogShellLogged();
    require(!controller.shouldLogBuildCatalogShell(),
            "Controller should clear catalog-shell log requests after marking logged");

    controller.syncTree(tree);
    ui::computeLayout(tree.root);
    panel = ui::findElementById(tree.root, "build_catalog_panel");
    require(panel != nullptr && panel->visible && panel->enabled,
            "Controller sync should expose the opened catalog panel");
    require(controller.scrollBuildCatalog(-3.0) &&
                nearlyEqual(controller.buildCatalogScrollOffsetY(), 114.0f) &&
                controller.shouldLogBuildCatalogScroll(),
            "Controller should scroll the opened build catalog by row-sized wheel steps");
    controller.syncTree(tree);
    ui::computeLayout(tree.root);
    const ui::UiHitResult scrolledCatalogHit =
        ui::hitTest(tree.root, 72.0f, 398.0f);
    require(scrolledCatalogHit.hit && scrolledCatalogHit.element != nullptr &&
                scrolledCatalogHit.element->id == ui::kBuildCatalogItemDId,
            "Controller sync should expose scrolled catalog rows to hit testing");
    controller.markBuildCatalogScrollLogged();
    require(!controller.shouldLogBuildCatalogScroll(),
            "Controller should clear catalog-scroll log requests after marking logged");

    const ui::UiActionDispatchResult selected = dispatcher.dispatch(
        ui::UiActionEvent{std::string(ui::kBuildCatalogItemAId),
                          std::string(ui::kSelectBuildCatalogItemAction),
                          std::string(ui::kBuildCatalogKeyFilter)});
    require(selected.handled &&
                controller.state().buildModeState == ui::BuildModeState::PendingPlacement,
            "Controller should enter pending placement after catalog selection");
    require(controller.pendingPlacementActive(),
            "Controller should report active pending placement after catalog selection");
    require(controller.selectedCatalogItemLogValue() ==
                    std::string(ui::kBuildCatalogItemAId) &&
                controller.selectedCatalogKeyLogValue() ==
                    std::string(ui::kBuildCatalogKeyFilter) &&
                controller.pendingBuildItemKeyLogValue() ==
                    std::string(ui::kBuildCatalogKeyFilter) &&
                std::string(controller.buildModeStateLogValue()) == "pending-placement",
            "Controller should expose deterministic state values for logging");
    require(controller.shouldLogBuildGhostPreview(),
            "Controller should request a ghost-preview log after selection");
    require(controller.placementRotationSteps() == 0,
            "New placement selections should start without rotation");
    require(controller.rotatePendingPlacement() &&
                controller.placementRotationSteps() == 1 &&
                controller.pendingPlacementActive() &&
                !controller.state().placementProbe.active,
            "Rotating pending placement should advance rotation and invalidate the probe");

    controller.syncTree(tree);
    ui::computeLayout(tree.root);
    preview = ui::findElementById(tree.root, ui::kBuildGhostPreviewId);
    const ui::UiElement* previewLabel =
        ui::findElementById(tree.root, ui::kBuildGhostPreviewLabelId);
    const ui::UiElement* previewHint =
        ui::findElementById(tree.root, ui::kBuildGhostPreviewHintId);
    const ui::UiElement* selectedCatalogItem =
        ui::findElementById(tree.root, ui::kBuildCatalogItemAId);
    require(preview != nullptr && preview->visible && !preview->enabled,
            "Controller sync should expose a disabled ghost preview");
    require(previewLabel != nullptr && previewLabel->text.value == "PLACING FILTER" &&
                previewHint != nullptr && previewHint->text.value == "ROT 90 READY",
            "Controller sync should label the ghost preview from the selected key");
    require(selectedCatalogItem != nullptr &&
                selectedCatalogItem->style.color ==
                    ui::themeColor(ui::defaultUiTheme().accent, 0.92f),
            "Controller sync should highlight the selected build catalog row");
    controller.markBuildGhostPreviewLogged();
    require(!controller.shouldLogBuildGhostPreview(),
            "Controller should clear ghost-preview log requests after marking logged");

    ui::BuildPlacementProbe probe{};
    probe.active = true;
    probe.rayHit = true;
    probe.hasPlacementCandidate = true;
    probe.snappedToGrid = true;
    probe.placementValid = true;
    probe.itemKey = std::string(ui::kBuildCatalogKeyFilter);
    probe.invalidReason = "none";
    probe.hitVoxel = glm::ivec3(3, 4, 5);
    probe.hitNormal = glm::ivec3(0, 1, 0);
    probe.placementVoxel = glm::ivec3(3, 5, 5);
    probe.footprintVoxels = glm::ivec3(1, 1, 1);
    probe.rotationSteps = controller.placementRotationSteps();
    probe.ghostWorldPosition = glm::vec3(3.5f, 5.5f, 5.5f);
    probe.distance = 2.5f;
    controller.updatePlacementProbe(probe);
    require(controller.state().placementProbe.active &&
                controller.state().placementProbe.rayHit &&
                controller.state().placementProbe.hasPlacementCandidate &&
                controller.state().placementProbe.snappedToGrid &&
                controller.state().placementProbe.placementValid &&
                controller.state().placementProbe.itemKey ==
                    std::string(ui::kBuildCatalogKeyFilter) &&
                controller.state().placementProbe.invalidReason == "none" &&
                controller.state().placementProbe.hitVoxel == glm::ivec3(3, 4, 5) &&
                controller.state().placementProbe.hitNormal == glm::ivec3(0, 1, 0) &&
                controller.state().placementProbe.placementVoxel ==
                    glm::ivec3(3, 5, 5) &&
                controller.state().placementProbe.footprintVoxels ==
                    glm::ivec3(1, 1, 1) &&
                controller.state().placementProbe.rotationSteps == 1 &&
                controller.state().placementProbe.ghostWorldPosition ==
                    glm::vec3(3.5f, 5.5f, 5.5f) &&
                nearlyEqual(controller.state().placementProbe.distance, 2.5f),
            "Controller should store the latest read-only placement probe");
    require(controller.shouldLogBuildPlacementProbe(),
            "Controller should request a placement-probe log after receiving a probe");
    controller.markBuildPlacementProbeLogged();
    require(!controller.shouldLogBuildPlacementProbe(),
            "Controller should clear placement-probe log requests after marking logged");
    require(controller.shouldLogBuildPlacementGhost(),
            "Controller should request a placement-ghost log after receiving a probe");
    controller.markBuildPlacementGhostLogged();
    require(!controller.shouldLogBuildPlacementGhost(),
            "Controller should clear placement-ghost log requests after marking logged");

    const ui::UiActionDispatchResult canceled =
        dispatcher.dispatch(ui::UiActionEvent{std::string(ui::kBuildCatalogCancelId),
                                              std::string(ui::kCancelBuildPlacementAction), {}});
    require(canceled.handled && controller.state().buildCatalogOpen,
            "Cancel should clear pending placement while leaving the catalog open");
    require(controller.state().buildModeState == ui::BuildModeState::Inactive &&
                controller.selectedCatalogItemLogValue() == "<none>" &&
                controller.selectedCatalogKeyLogValue() == "<none>" &&
                controller.pendingBuildItemKeyLogValue() == "<none>" &&
                controller.placementRotationSteps() == 0 &&
                !controller.pendingPlacementActive() &&
                !controller.state().placementProbe.active &&
                controller.state().placementProbe.itemKey.empty(),
            "Cancel should reset controller selection and pending state");

    controller.syncTree(tree);
    ui::computeLayout(tree.root);
    preview = ui::findElementById(tree.root, ui::kBuildGhostPreviewId);
    require(preview != nullptr && !preview->visible && !preview->enabled,
            "Controller sync should hide the ghost preview after cancel");

    const ui::UiActionDispatchResult removeSelected =
        dispatcher.dispatch(ui::UiActionEvent{std::string(ui::kBuildCatalogRemoveId),
                                              std::string(ui::kSelectRemovePlacementAction),
                                              std::string(ui::kBuildRemoveKey)});
    require(removeSelected.handled &&
                controller.state().buildModeState ==
                    ui::BuildModeState::PendingRemoval &&
                controller.pendingPlacementActive() &&
                controller.selectedCatalogItemLogValue() ==
                    std::string(ui::kBuildCatalogRemoveId) &&
                controller.selectedCatalogKeyLogValue() ==
                    std::string(ui::kBuildRemoveKey) &&
                controller.pendingBuildItemKeyLogValue() ==
                    std::string(ui::kBuildRemoveKey) &&
                std::string(controller.buildModeStateLogValue()) ==
                    "pending-removal",
            "Controller should enter explicit pending removal mode");
    require(controller.shouldLogBuildGhostPreview(),
            "Controller should request a ghost-preview log after removal selection");
    require(!controller.rotatePendingPlacement() &&
                controller.placementRotationSteps() == 0,
            "Pending removal should not consume placement rotation");
    controller.syncTree(tree);
    ui::computeLayout(tree.root);
    preview = ui::findElementById(tree.root, ui::kBuildGhostPreviewId);
    previewLabel = ui::findElementById(tree.root, ui::kBuildGhostPreviewLabelId);
    previewHint = ui::findElementById(tree.root, ui::kBuildGhostPreviewHintId);
    require(preview != nullptr && preview->visible && !preview->enabled &&
                previewLabel != nullptr &&
                previewLabel->text.value == "REMOVING TARGET" &&
                previewHint != nullptr && previewHint->text.value == "TARGET READY",
            "Controller sync should label pending removal separately");
    const ui::UiActionDispatchResult removalCanceled =
        dispatcher.dispatch(ui::UiActionEvent{std::string(ui::kBuildCatalogCancelId),
                                              std::string(ui::kCancelBuildPlacementAction), {}});
    require(removalCanceled.handled &&
                controller.state().buildModeState == ui::BuildModeState::Inactive &&
                controller.pendingBuildItemKeyLogValue() == "<none>",
            "Cancel should reset pending removal mode");

    dispatcher.dispatch(ui::UiActionEvent{std::string(ui::kBuildCatalogItemAId),
                                          std::string(ui::kSelectBuildCatalogItemAction),
                                          std::string(ui::kBuildCatalogKeyFilter)});
    probe.rotationSteps = controller.placementRotationSteps();
    controller.updatePlacementProbe(probe);
    require(controller.pendingPlacementActive() &&
                controller.state().placementProbe.placementValid,
            "Controller should allow pending placement to be re-entered before commit");
    const ui::UiActionDispatchResult committed =
        dispatcher.dispatch(ui::UiActionEvent{std::string(ui::kPendingPlacementWorldId),
                                              std::string(ui::kCommitBuildPlacementAction),
                                              std::string(ui::kBuildCatalogKeyFilter)});
    require(committed.handled && controller.state().buildCatalogOpen,
            "Commit should clear pending placement while leaving the catalog open");
    require(controller.state().buildModeState == ui::BuildModeState::Inactive &&
                controller.selectedCatalogItemLogValue() == "<none>" &&
                controller.selectedCatalogKeyLogValue() == "<none>" &&
                controller.pendingBuildItemKeyLogValue() == "<none>" &&
                controller.placementRotationSteps() == 0 &&
                !controller.pendingPlacementActive() &&
                !controller.state().placementProbe.active,
            "Commit should reset controller selection and pending state");

    dispatcher.dispatch(ui::UiActionEvent{"catalog_bar",
                                          std::string(ui::kOpenBuildCatalogAction), {}});
    dispatcher.dispatch(ui::UiActionEvent{std::string(ui::kBuildCatalogItemAId),
                                          std::string(ui::kSelectBuildCatalogItemAction),
                                          std::string(ui::kBuildCatalogKeyFilter)});
    probe.rotationSteps = controller.placementRotationSteps();
    controller.updatePlacementProbe(probe);
    require(controller.state().buildCatalogOpen && controller.pendingPlacementActive() &&
                controller.state().placementProbe.active,
            "Controller should allow pending placement before close");
    const ui::UiActionDispatchResult closed =
        dispatcher.dispatch(ui::UiActionEvent{"build_catalog_close",
                                              std::string(ui::kCloseBuildCatalogAction), {}});
    require(closed.handled && !controller.state().buildCatalogOpen,
            "Close should hide the catalog");
    require(controller.state().buildModeState == ui::BuildModeState::Inactive &&
                controller.selectedCatalogItemLogValue() == "<none>" &&
                controller.selectedCatalogKeyLogValue() == "<none>" &&
                controller.pendingBuildItemKeyLogValue() == "<none>" &&
                controller.placementRotationSteps() == 0 &&
                !controller.pendingPlacementActive() &&
                !controller.state().placementProbe.active,
            "Close should clear pending placement and selection state");

    controller.syncTree(tree);
    ui::computeLayout(tree.root);
    panel = ui::findElementById(tree.root, "build_catalog_panel");
    preview = ui::findElementById(tree.root, ui::kBuildGhostPreviewId);
    require(panel != nullptr && !panel->visible && !panel->enabled &&
                preview != nullptr && !preview->visible && !preview->enabled,
            "Controller sync should hide catalog and ghost preview after close");
}

void testRuntimeUiControllerResetsTransientSessionState()
{
    ui::OverlaySmokeScreenController controller;
    ui::UiActionDispatcher dispatcher;
    controller.registerActions(dispatcher);
    controller.setFishList({ui::UiFishListEntry{42, "KEEPER", true}});

    dispatcher.dispatch(ui::UiActionEvent{
        "catalog_bar", std::string(ui::kOpenBuildCatalogAction), {}});
    require(controller.scrollBuildCatalog(-3.0),
            "Session-reset coverage should begin with non-zero catalog scroll");
    dispatcher.dispatch(ui::UiActionEvent{
        std::string(ui::kBuildCatalogItemAId),
        std::string(ui::kSelectBuildCatalogItemAction),
        std::string(ui::kBuildCatalogKeyFilter)});
    require(controller.rotatePendingPlacement(),
            "Session-reset coverage should begin with rotated pending placement");
    ui::BuildPlacementProbe probe{};
    probe.active = true;
    probe.placementValid = true;
    controller.updatePlacementProbe(probe);

    require(controller.resetTransientSessionState(),
            "Reset should report that transient session state changed");
    require(!controller.state().buildCatalogOpen &&
                nearlyEqual(controller.state().buildCatalogScrollOffsetY, 0.0f) &&
                controller.state().selectedCatalogItem.empty() &&
                controller.state().selectedCatalogKey.empty() &&
                controller.state().pendingBuildItemKey.empty() &&
                controller.state().placementRotationSteps == 0 &&
                controller.state().buildModeState == ui::BuildModeState::Inactive &&
                !controller.state().placementProbe.active,
            "Session reset should clear catalog, selection, placement, rotation, and probe state");
    require(controller.fishListCount() == 1,
            "Session reset should preserve independently managed fish-list data");
    require(!controller.resetTransientSessionState(),
            "Resetting an already clean session should be idempotent");

    ui::UiTree tree = ui::makeOverlaySmokeTree();
    controller.syncTree(tree, 1);
    ui::computeLayout(tree.root);
    const ui::UiElement* catalog =
        ui::findElementById(tree.root, "build_catalog_panel");
    const ui::UiElement* fish = ui::findElementById(tree.root, "fish_row_42");
    require(catalog != nullptr && !catalog->visible && fish != nullptr,
            "A fresh tree should apply clean transient state while retaining the fish roster");
}

void testRuntimeUiContextRequiresMatchingPointerReleaseForClick()
{
    ui::UiContext context;
    require(context.setActiveScreen(ui::kOverlaySmokeScreenId),
            "Pointer mismatch test should select a registered screen");
    ui::UiTree* tree = context.ensureActiveTree();
    require(tree != nullptr, "Pointer mismatch test should build a retained tree");
    ui::computeLayout(tree->root);

    const ui::UiHitResult buttonHit = ui::hitTest(tree->root, 96.0f, 240.0f);
    const ui::UiHitResult panelHit = ui::hitTest(tree->root, 48.0f, 48.0f);
    require(buttonHit.hit && panelHit.hit, "Pointer mismatch test needs two hits");
    require(buttonHit.element->id == "catalog_bar" && panelHit.element->id == "test_panel",
            "Pointer mismatch test should hit different elements");

    context.handlePointerDown(buttonHit);
    require(!context.handlePointerUp(panelHit),
            "Releasing over a different element should not click");
    require(!context.pointerState().clickPending,
            "A mismatched release should not leave a pending click");
    require(context.consumeClickedElementId().empty(),
            "A mismatched release should not produce a clicked id");

    context.handlePointerDown(buttonHit);
    context.clearPointerState();
    require(context.pointerState().hoveredElementId.empty() &&
                context.pointerState().pressedElementId.empty(),
            "clearPointerState should reset hover and press state");
}

} // namespace

int main()
{
    try
    {
        testWindowCoordinatesMapToFramebufferPixels();
        testInputRouterHonorsRuntimeUiModalCapture();
        testAutomationProfileSessionUsesSteadyStateWindow();
        testFloorDivisionHelpers();
        testWorldGenerationIsDeterministic();
        testWorldGenerationVariesWithSeed();
        testSamplerCachePresetCreateInfos();
        testDescriptorLayoutCacheKeys();
        testDescriptorAllocatorKeys();
        testUniqueAllocationOwnershipContract();
        testRenderSettingsDefaultsAndIndependence();
        testRenderResolutionContract();
        testAuxiliaryRayResolutionContract();
        testRenderQualityPresetContract();
        testSunShadowSamplingPolicyContract();
        testAmbientOcclusionPushConstantsPreserveLiveControls();
        testProjectedAoDistanceTierContract();
        testVoxelCellVariationContract();
        testHemisphereAmbientContract();
        testGBufferFrameUboBuilder();
        testLightingFrameUboBuilder();
        testLightingProjectionJitterNormalization();
        testCompositeAtmosphereUboBuilder();
        testGlassFrameUboBuilder();
        testVoxelGlassFrameUboBuilder();
        testPassRegistryOrderAndGating();
        testRenderPipelineShowcaseContract();
        testFramePassRecordOrderCharacterization();
        testFrameHandshakeOrderCharacterization();
        testRendererOwnedFrameLoopTraceCharacterization();
        testFramePassLifecycleOrderCharacterization();
        testRendererCapabilityPredicates();
        testRendererFrameLifecycleBindingContract();
        testRendererPassLifecycleBindingContract();
        testRendererOwnsPassResourceBundle();
        testWorldGenerationPopulatesTerrainAndResetsChunkFlags();
        testVoxelSystemCreatesGridAndGeneratesWorldState();
        testVoxelSystemDirtyNeighborsMarksChunkBoundaries();
        testVoxelSystemApplyVoxelEditPlacesAndRemoves();
        testVoxelSystemSchedulesAndConsumesChunkMeshes();
        testVoxelSystemChunkGridReadQueries();
        testVoxelSystemRaycastPlacementProbeModes();
        testVoxelSystemFallbackPlacementProbeModes();
        testVoxelSystemCommitBlockPlacementAndRemoval();
        testVoxelSystemExpandGridToIncludeChunkPreservesBlocks();
        testWaterHeartbeatDriftsCareStateAndClampsInput();
        testGameRuntimeOfflineCareUsesInjectedClockWithoutLiveBinding();
        testGameRuntimeHeartbeatAndWaterMaintenance();
        testGameRuntimePlaceableEditRoundTrip();
        testGameRuntimePlaceableEditSessionIsolation();
        testFishCelebrationLifecycle();
        testAxolotlBellyFloatLifecycle();
        testAxolotlCreaturePolishFocusDetails();
        testFishTypesDefaults();
        testGameRuntimeCelebrationExclusivity();
        testAxolotlBodySculptGeometry();
        testAxolotlTailSculptGeometry();
        testAxolotlLimbSculptGeometry();
        testFoliageCatalogDefaultsAndColorVariants();
        testFoliageMotionHistoryContract();
        testFoliageVoxelGeometryContract();
        testFoliageMorphologyContract();
        testNaturePondGenerationContract();
        testNaturePondSunroofProbeContract();
        testNaturePondGlassDomeContract();
        testLightingParityProbeContract();
        testVoxelLightingOcclusionPolicyContract();
        testVoxelRasterFaceSelectionContract();
        testVoxelAlignedLayerTraversalContract();
        testTerrainShadowColumnEligibilityContract();
        testLocalShadowVolumeInfluenceCullingContract();
        testProceduralWorldMaterialCategoryOwnership();
        testSceneCatalogRevisionTracksRefresh();
        testSceneManagerReloadDecisionEffects();
        testEnvironmentWindContract();
        testEnvironmentTimeContract();
        testWindborneParticleSettingsContract();
        testWindborneParticleFieldContract();
        testEnvironmentSettingsRoundTripAndSkyPreset();
        testWorldStateViewAliasesLiveState();
        testSceneManagerCatalogLoadAndFallback();
        testSceneManagerCloudBuildLifecycle();
        testSceneManagerVolumeSceneEffectPlans();
        testSceneObjectBuilderBaseDrawLists();
        testSceneObjectBuilderChunkBoundsObjects();
        testSceneObjectBuilderVolumeBoundsObjects();
        testChunkGridWorldAccessAcrossNegativeCoordinates();
        testChunkGridCreatePreservingBlocksKeepsExistingCells();
        testVoxelRaycastHitAndMissBehavior();
        testCloudGenerationIsDeterministic();
        testModularGlassMesherBuildsSingleVoxelShell();
        testModularGlassMesherMergesAdjacentVoxels();
        testModularGlassMesherDoesNotMergeAcrossDifferentGlassMaterials();
        testRuntimeUiPackColorPremultipliesAndClamps();
        testRuntimeUiThemePreservesLegacyPalette();
        testRuntimeUiFishListBindingBuildsRows();
        testRuntimeUiFishWorldLabelsBuildScreenLayer();
        testRuntimeUiAxolotlVersusLabelBuildsFlagsAndTypedText();
        testRuntimeUiMainMenuScreenFactoryBuildsExpectedLayout();
        testRuntimeUiScreenStackPushPopAndModality();
        testRuntimeUiCollectionCodexBuildsOwnedAdvisoryView();
        testRuntimeUiCompactOverlayPanelsAndCatalogStayReachable();
        testRuntimeUiCompactMenusPauseAndFishScrolling();
        testRuntimeUiCompactCollectionCodexScrollsRegionsIndependently();
        testRuntimeUiBindingSetAppliesOnlyDirtyBindings();
        testRuntimeUiTweenSetAnimatesAndCompletes();
        testRuntimeUiOpacityScalesPaintedColors();
        testRuntimeUiDrawListEmitsQuadGeometry();
        testRuntimeUiDrawListRejectsDegenerateRects();
        testRuntimeUiNoneLayoutOffsetsChildrenByParentOrigin();
        testRuntimeUiVerticalStackAppliesPaddingSpacingAndFill();
        testRuntimeUiVerticalStackDistributesGrowSpace();
        testRuntimeUiHorizontalStackDistributesGrowSpace();
        testRuntimeUiCrossAxisAlignmentPlacesChildren();
        testRuntimeUiHitTestUsesZOrderAndState();
        testRuntimeUiHitTestRespectsClipping();
        testRuntimeUiPaintTreeCountsVisibleElements();
        testRuntimeUiButtonPaintUsesPointerStateColors();
        testRuntimeUiButtonHelperBuildsSemanticButton();
        testRuntimeUiDisabledButtonPaintsDisabledStyle();
        testRuntimeUiTextMeasurementWrapsAndCountsLines();
        testRuntimeUiBuildPlacementDefinitionsExposePlacementRules();
        testRuntimeUiBuildPlacementEvaluationHandlesCoreReasons();
        testRuntimeUiOverlaySmokeScreenFactoryBuildsExpectedLayout();
        testRuntimeUiContextBuildsRegisteredScreen();
        testRuntimeUiContextHandlesUnknownScreensAndInvalidation();
        testRuntimeUiContextTracksPointerClickState();
        testRuntimeUiContextConsumesSemanticActionEvents();
        testRuntimeUiActionDispatcherInvokesRegisteredHandlers();
        testRuntimeUiOverlaySmokeScreenControllerOwnsDynamicState();
        testRuntimeUiControllerResetsTransientSessionState();
        testRuntimeUiContextRequiresMatchingPointerReleaseForClick();
        std::cout << "Engine core tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Engine core test failure: " << e.what() << "\n";
        return 1;
    }
}
