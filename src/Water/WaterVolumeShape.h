#pragma once

#include <cstdint>

namespace engine
{

enum class WaterVolumeShape : uint32_t
{
    Box = 0,
    Fishbowl = 1,
};

constexpr uint32_t waterVolumeShapeValue(WaterVolumeShape shape)
{
    return static_cast<uint32_t>(shape);
}

} // namespace engine
