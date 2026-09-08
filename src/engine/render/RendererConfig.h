#pragma once

#include <cstdint>

#if VOXEL_ENABLE_VALIDATION
static constexpr bool kEnableValidation = true;
#else
static constexpr bool kEnableValidation = false;
#endif

#if defined(VOXEL_PLATFORM_MACOS) && VOXEL_PLATFORM_MACOS
static constexpr bool kUseMoltenVK = true;
#else
static constexpr bool kUseMoltenVK = false;
#endif

static constexpr uint32_t kMaxFramesInFlight = 2;
static constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";
static constexpr uint32_t kShadowCascades = 3;
