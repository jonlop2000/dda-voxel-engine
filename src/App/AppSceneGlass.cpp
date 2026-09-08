#include "App/AppSceneGlass.h"

#include "App/AppSceneVolume.h"
#include "App/Transform.h"
#include "Core/Logger.h"
#include "engine/scene/AquariumScene.h"
#include "engine/scene/NaturePondGlassDome.h"
#include "engine/scene/NaturePondSunroofProbe.h"
#include "engine/scene/SceneManager.h"
#include "engine/scene/TankGlassBuilder.h"

namespace
{
void appendDebugGlassPanelObjects(std::vector<RenderObject>& out,
                                  MeshGpu* cubeMesh)
{
    if (cubeMesh == nullptr)
    {
        return;
    }

    Transform frontPanel{};
    frontPanel.position = glm::vec3(0.0f, 2.0f, 2.0f);
    frontPanel.scale = glm::vec3(8.0f, 4.5f, 0.3f);
    out.push_back(RenderObject{frontPanel.modelMatrix(), cubeMesh, nullptr});

    Transform angledPanel{};
    angledPanel.position = glm::vec3(-6.0f, 2.0f, -2.0f);
    angledPanel.rotationEuler = glm::vec3(0.0f, glm::radians(45.0f), 0.0f);
    angledPanel.scale = glm::vec3(4.0f, 4.0f, 0.2f);
    out.push_back(RenderObject{angledPanel.modelMatrix(), cubeMesh, nullptr});

    Transform glassCube{};
    glassCube.position = glm::vec3(6.0f, 1.5f, 0.0f);
    glassCube.scale = glm::vec3(2.0f);
    out.push_back(RenderObject{glassCube.modelMatrix(), cubeMesh, nullptr});

    Transform tiltedPanel{};
    tiltedPanel.position = glm::vec3(0.0f, 4.0f, -5.0f);
    tiltedPanel.rotationEuler = glm::vec3(glm::radians(-30.0f), 0.0f, 0.0f);
    tiltedPanel.scale = glm::vec3(5.0f, 3.0f, 0.15f);
    out.push_back(RenderObject{tiltedPanel.modelMatrix(), cubeMesh, nullptr});
}

void appendAquariumMeshGlassObjects(std::vector<RenderObject>& out,
                                    MeshGpu* cubeMesh,
                                    MeshGpu* fishbowlMesh,
                                    const SceneConfig& sceneConfig)
{
    if (cubeMesh == nullptr || !sceneConfig.loadAquariumTest ||
        !sceneConfig.useMeshTankGlass)
    {
        return;
    }

    const auto& volumeInfos = AquariumScene::getVolumeInfos();
    if (volumeInfos.size() <= AquariumScene::VOLUME_TANK_COMPOSITE)
    {
        return;
    }

    const AquariumVolumeInfo& tankInfo =
        volumeInfos[AquariumScene::VOLUME_TANK_COMPOSITE];
    if (isFishbowlAquariumScene(sceneConfig))
    {
        if (fishbowlMesh != nullptr && fishbowlMesh->vbo != VK_NULL_HANDLE)
        {
            out.push_back(
                RenderObject{glm::mat4(1.0f), fishbowlMesh, nullptr});
            return;
        }

        TankGlassBuilder::FishbowlGlassSpec fishbowlSpec{};
        fishbowlSpec.volumeDims = tankInfo.dimensions;
        fishbowlSpec.volumeWorldPos = tankInfo.worldPosition;
        fishbowlSpec.paneThickness = 0.075f;
        fishbowlSpec.radialSegments = 48;
        fishbowlSpec.verticalBands = 15;
        fishbowlSpec.includeBase = true;
        fishbowlSpec.includeRim = true;

        const std::vector<glm::mat4> paneModels =
            TankGlassBuilder::buildFishbowlGlassPaneModels(fishbowlSpec);
        for (const glm::mat4& model : paneModels)
        {
            out.push_back(RenderObject{model, cubeMesh, nullptr});
        }
        return;
    }

    TankGlassBuilder::RectangularTankSpec tankSpec{};
    tankSpec.volumeDims = tankInfo.dimensions;
    tankSpec.volumeWorldPos = tankInfo.worldPosition;
    tankSpec.wallThickness = 2;
    tankSpec.paneThickness = 0.15f;
    tankSpec.includeBottom = false;

    const std::vector<glm::mat4> paneModels =
        TankGlassBuilder::buildRectangularTankGlassPaneModels(tankSpec);
    for (const glm::mat4& model : paneModels)
    {
        out.push_back(RenderObject{model, cubeMesh, nullptr});
    }
}

void appendNaturePondSunroofGlassObjects(std::vector<RenderObject>& out,
                                         MeshGpu* cubeMesh,
                                         const SceneConfig& sceneConfig)
{
    if (cubeMesh == nullptr)
    {
        return;
    }

    const std::vector<glm::mat4> paneModels =
        naturePondSunroofGlassPaneModels(sceneConfig);
    for (const glm::mat4& model : paneModels)
    {
        out.push_back(RenderObject{model, cubeMesh, nullptr});
    }
    if (!paneModels.empty())
    {
        logInfo("NaturePondSunroofGlass",
                makeLogMessage("Built pavilion glazing: panes=",
                               paneModels.size(), " openEntranceBay=",
                               naturePondSunroofGlassLayout().entranceBayIndex,
                               "."));
    }
}
} // namespace

void rebuildGlassObjectsForScene(std::vector<RenderObject>& out,
                                 MeshGpu* cubeMesh,
                                 MeshGpu* fishbowlMesh,
                                 const SceneConfig& sceneConfig,
                                 const glm::vec3& sunDirection)
{
    (void)sunDirection;
    out.clear();
    if (sceneConfig.loadGlassPanel)
    {
        appendDebugGlassPanelObjects(out, cubeMesh);
    }
    appendAquariumMeshGlassObjects(out, cubeMesh, fishbowlMesh, sceneConfig);
    appendNaturePondSunroofGlassObjects(out, cubeMesh, sceneConfig);
    const auto dome = naturePondGlassDomePlacement(sceneConfig);
    if (dome && fishbowlMesh != nullptr &&
        fishbowlMesh->vbo != VK_NULL_HANDLE)
    {
        out.push_back(RenderObject{dome->model, fishbowlMesh, nullptr});
    }
}
