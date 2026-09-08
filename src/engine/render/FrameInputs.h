#pragma once

namespace engine::render
{

struct FrameInputs
{
    bool renderCascadedShadows = false;
    bool renderWaterVolumePrepass = false;
    bool useDdaShadows = false;
    bool useLocalLightShadows = false;
    bool useAmbientOcclusion = false;
    bool renderVoxelGlass = false;
    bool renderWaterBody = false;
    bool renderPlanarReflection = false;
    bool renderWater = false;
    bool renderGlass = false;
    bool runBloom = false;
    bool runTaa = false;
    bool renderRuntimeUi = false;
};

}  // namespace engine::render
