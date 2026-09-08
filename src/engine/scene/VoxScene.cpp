#include "engine/scene/VoxScene.h"

#include "Core/Logger.h"
#include "engine/voxel/VoxLoader.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelWorld.h"

namespace
{

engine::PaletteEntryCPU makePaletteEntry(const engine::VoxColor& color)
{
    engine::PaletteEntryCPU entry{};
    entry.baseColor_alpha = glm::vec4(color.r / 255.0f, color.g / 255.0f,
                                      color.b / 255.0f, color.a / 255.0f);
    entry.pbr0 = glm::vec4(0.0f, 0.85f, 0.0f, 0.0f);
    entry.extra = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    return entry;
}

} // namespace

bool VoxScene::init(VulkanContext& ctx, engine::VoxelWorld& world,
                    engine::VoxelPalette& palette,
                    const std::filesystem::path& voxPath)
{
    engine::VoxFile voxFile{};
    std::string error;
    if (!engine::VoxLoader::load(voxPath, voxFile, &error))
    {
        logError("VoxScene", makeLogMessage(error, " (", voxPath.string(), ")"));
        return false;
    }

    if (!voxFile.hasPalette)
    {
        logWarning("VoxScene", "RGBA palette missing, using fallback colors.");
    }

    for (size_t i = 0; i < voxFile.palette.colors.size(); ++i)
    {
        palette.setEntry(0, static_cast<uint32_t>(i),
                         makePaletteEntry(voxFile.palette.colors[i]));
    }
    if (!palette.upload(ctx))
    {
        logError("VoxScene", "Failed to upload palette.");
        return false;
    }

    std::vector<engine::VolumeSpec> specs;
    std::vector<std::vector<uint8_t>> volumeData;
    specs.reserve(voxFile.models.size());
    volumeData.reserve(voxFile.models.size());

    float cursorX = 0.0f;
    const float spacing = 2.0f;

    for (size_t i = 0; i < voxFile.models.size(); ++i)
    {
        const engine::VoxModel& model = voxFile.models[i];
        if (model.size.x <= 0 || model.size.y <= 0 || model.size.z <= 0)
        {
            continue;
        }

        engine::VolumeSpec spec{};
        spec.name = "VoxModel_" + std::to_string(i);
        spec.dims = model.size;
        spec.flags = engine::VoxelVolume::FLAG_STATIC;
        spec.position = glm::vec3(cursorX - 0.5f * static_cast<float>(model.size.x),
                                  0.0f,
                                  -0.5f * static_cast<float>(model.size.z));

        specs.push_back(spec);
        volumeData.push_back(model.voxels);

        cursorX += static_cast<float>(model.size.x) + spacing;
    }

    if (specs.empty())
    {
        logError("VoxScene", "No valid models to load.");
        return false;
    }

    const bool ok = world.initAquariumScene(
        ctx, palette, specs.size(),
        [&](size_t index) -> const engine::VolumeSpec& { return specs[index]; },
        [&](size_t index) -> const std::vector<uint8_t>& { return volumeData[index]; });

    if (!ok)
    {
        logError("VoxScene", "Failed to initialize voxel world.");
        return false;
    }

    logInfo("VoxScene",
            makeLogMessage("Loaded ", specs.size(), " model(s) from ", voxPath.string()));
    return true;
}
