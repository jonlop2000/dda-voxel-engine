#pragma once

class EngineFacade;

namespace SceneCatalogExperiment
{
// runtime A/B control: DDA_VOXEL_SCENE_CATALOG_MODE=legacy|prepared.
// optional csv timings: DDA_VOXEL_SCENE_CATALOG_PROFILE=<output path>.
// both are read once per editor session; neither changes authored settings.
void Init();
void Shutdown();
void Draw(EngineFacade& engine, const char* filter);
} // namespace SceneCatalogExperiment
