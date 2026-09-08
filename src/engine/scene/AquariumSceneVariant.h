#pragma once

#include <string_view>

namespace engine::scene
{

inline constexpr std::string_view kAquariumSunroofSceneName = "aquarium_sunroof";
inline constexpr std::string_view kAquariumSunroofProbeSceneName =
    "aquarium_sunroof_probe";

[[nodiscard]] constexpr bool isAquariumSunroofSceneName(std::string_view sceneName)
{
    return sceneName == kAquariumSunroofSceneName ||
           sceneName == kAquariumSunroofProbeSceneName;
}

} // namespace engine::scene
