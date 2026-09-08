#include "engine/voxel/VoxelBuilder.h"

#include <algorithm>
#include <cmath>

namespace engine
{

VoxelBuilder::VoxelBuilder(const glm::ivec3& dimensions)
    : m_dimensions(dimensions)
    , m_data(static_cast<size_t>(dimensions.x) * dimensions.y * dimensions.z, 0)
{
}

void VoxelBuilder::clear()
{
    std::fill(m_data.begin(), m_data.end(), 0);
}

bool VoxelBuilder::inBounds(int x, int y, int z) const
{
    return x >= 0 && x < m_dimensions.x && y >= 0 && y < m_dimensions.y && z >= 0 &&
           z < m_dimensions.z;
}

size_t VoxelBuilder::index(int x, int y, int z) const
{
    return static_cast<size_t>(x) + static_cast<size_t>(y) * m_dimensions.x +
           static_cast<size_t>(z) * m_dimensions.x * m_dimensions.y;
}

void VoxelBuilder::setVoxel(int x, int y, int z, uint8_t id)
{
    if (inBounds(x, y, z))
    {
        m_data[index(x, y, z)] = id;
    }
}

void VoxelBuilder::setVoxel(const glm::ivec3& pos, uint8_t id)
{
    setVoxel(pos.x, pos.y, pos.z, id);
}

uint8_t VoxelBuilder::getVoxel(int x, int y, int z) const
{
    if (inBounds(x, y, z))
    {
        return m_data[index(x, y, z)];
    }
    return 0;
}

uint8_t VoxelBuilder::getVoxel(const glm::ivec3& pos) const
{
    return getVoxel(pos.x, pos.y, pos.z);
}

void VoxelBuilder::fillBox(const glm::ivec3& min, const glm::ivec3& max, uint8_t id)
{
    const int x0 = std::max(0, min.x);
    const int y0 = std::max(0, min.y);
    const int z0 = std::max(0, min.z);
    const int x1 = std::min(m_dimensions.x - 1, max.x);
    const int y1 = std::min(m_dimensions.y - 1, max.y);
    const int z1 = std::min(m_dimensions.z - 1, max.z);

    for (int z = z0; z <= z1; ++z)
    {
        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                m_data[index(x, y, z)] = id;
            }
        }
    }
}

void VoxelBuilder::fillShellBox(const glm::ivec3& min, const glm::ivec3& max, int thickness,
                                uint8_t id)
{
    const int x0 = std::max(0, min.x);
    const int y0 = std::max(0, min.y);
    const int z0 = std::max(0, min.z);
    const int x1 = std::min(m_dimensions.x - 1, max.x);
    const int y1 = std::min(m_dimensions.y - 1, max.y);
    const int z1 = std::min(m_dimensions.z - 1, max.z);

    for (int z = z0; z <= z1; ++z)
    {
        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                // check if on any face within thickness
                const bool onXMin = (x - x0) < thickness;
                const bool onXMax = (x1 - x) < thickness;
                const bool onYMin = (y - y0) < thickness;
                const bool onYMax = (y1 - y) < thickness;
                const bool onZMin = (z - z0) < thickness;
                const bool onZMax = (z1 - z) < thickness;

                if (onXMin || onXMax || onYMin || onYMax || onZMin || onZMax)
                {
                    m_data[index(x, y, z)] = id;
                }
            }
        }
    }
}

void VoxelBuilder::fillSphere(const glm::vec3& center, float radius, uint8_t id)
{
    const float r2 = radius * radius;

    const int x0 = std::max(0, static_cast<int>(std::floor(center.x - radius)));
    const int y0 = std::max(0, static_cast<int>(std::floor(center.y - radius)));
    const int z0 = std::max(0, static_cast<int>(std::floor(center.z - radius)));
    const int x1 = std::min(m_dimensions.x - 1, static_cast<int>(std::ceil(center.x + radius)));
    const int y1 = std::min(m_dimensions.y - 1, static_cast<int>(std::ceil(center.y + radius)));
    const int z1 = std::min(m_dimensions.z - 1, static_cast<int>(std::ceil(center.z + radius)));

    for (int z = z0; z <= z1; ++z)
    {
        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const glm::vec3 p(x + 0.5f, y + 0.5f, z + 0.5f);
                const glm::vec3 d = p - center;
                if (glm::dot(d, d) <= r2)
                {
                    m_data[index(x, y, z)] = id;
                }
            }
        }
    }
}

void VoxelBuilder::fillEllipsoid(const glm::vec3& center, const glm::vec3& radii, uint8_t id)
{
    const int x0 = std::max(0, static_cast<int>(std::floor(center.x - radii.x)));
    const int y0 = std::max(0, static_cast<int>(std::floor(center.y - radii.y)));
    const int z0 = std::max(0, static_cast<int>(std::floor(center.z - radii.z)));
    const int x1 = std::min(m_dimensions.x - 1, static_cast<int>(std::ceil(center.x + radii.x)));
    const int y1 = std::min(m_dimensions.y - 1, static_cast<int>(std::ceil(center.y + radii.y)));
    const int z1 = std::min(m_dimensions.z - 1, static_cast<int>(std::ceil(center.z + radii.z)));

    for (int z = z0; z <= z1; ++z)
    {
        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const glm::vec3 p(x + 0.5f, y + 0.5f, z + 0.5f);
                const glm::vec3 d = (p - center) / radii;
                if (glm::dot(d, d) <= 1.0f)
                {
                    m_data[index(x, y, z)] = id;
                }
            }
        }
    }
}

void VoxelBuilder::fillCylinder(const glm::vec3& baseCenter, float radius, float height, int axis,
                                uint8_t id)
{
    const float r2 = radius * radius;

    // determine bounds based on axis
    int x0, y0, z0, x1, y1, z1;

    if (axis == 1)
    {
        // y-axis cylinder (vertical)
        x0 = std::max(0, static_cast<int>(std::floor(baseCenter.x - radius)));
        x1 = std::min(m_dimensions.x - 1, static_cast<int>(std::ceil(baseCenter.x + radius)));
        y0 = std::max(0, static_cast<int>(std::floor(baseCenter.y)));
        y1 = std::min(m_dimensions.y - 1, static_cast<int>(std::ceil(baseCenter.y + height)));
        z0 = std::max(0, static_cast<int>(std::floor(baseCenter.z - radius)));
        z1 = std::min(m_dimensions.z - 1, static_cast<int>(std::ceil(baseCenter.z + radius)));

        for (int z = z0; z <= z1; ++z)
        {
            for (int y = y0; y <= y1; ++y)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    const float dx = (x + 0.5f) - baseCenter.x;
                    const float dz = (z + 0.5f) - baseCenter.z;
                    if (dx * dx + dz * dz <= r2)
                    {
                        m_data[index(x, y, z)] = id;
                    }
                }
            }
        }
    }
    else if (axis == 0)
    {
        // x-axis cylinder (horizontal)
        x0 = std::max(0, static_cast<int>(std::floor(baseCenter.x)));
        x1 = std::min(m_dimensions.x - 1, static_cast<int>(std::ceil(baseCenter.x + height)));
        y0 = std::max(0, static_cast<int>(std::floor(baseCenter.y - radius)));
        y1 = std::min(m_dimensions.y - 1, static_cast<int>(std::ceil(baseCenter.y + radius)));
        z0 = std::max(0, static_cast<int>(std::floor(baseCenter.z - radius)));
        z1 = std::min(m_dimensions.z - 1, static_cast<int>(std::ceil(baseCenter.z + radius)));

        for (int z = z0; z <= z1; ++z)
        {
            for (int y = y0; y <= y1; ++y)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    const float dy = (y + 0.5f) - baseCenter.y;
                    const float dz = (z + 0.5f) - baseCenter.z;
                    if (dy * dy + dz * dz <= r2)
                    {
                        m_data[index(x, y, z)] = id;
                    }
                }
            }
        }
    }
    else
    {
        // z-axis cylinder
        x0 = std::max(0, static_cast<int>(std::floor(baseCenter.x - radius)));
        x1 = std::min(m_dimensions.x - 1, static_cast<int>(std::ceil(baseCenter.x + radius)));
        y0 = std::max(0, static_cast<int>(std::floor(baseCenter.y - radius)));
        y1 = std::min(m_dimensions.y - 1, static_cast<int>(std::ceil(baseCenter.y + radius)));
        z0 = std::max(0, static_cast<int>(std::floor(baseCenter.z)));
        z1 = std::min(m_dimensions.z - 1, static_cast<int>(std::ceil(baseCenter.z + height)));

        for (int z = z0; z <= z1; ++z)
        {
            for (int y = y0; y <= y1; ++y)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    const float dx = (x + 0.5f) - baseCenter.x;
                    const float dy = (y + 0.5f) - baseCenter.y;
                    if (dx * dx + dy * dy <= r2)
                    {
                        m_data[index(x, y, z)] = id;
                    }
                }
            }
        }
    }
}

void VoxelBuilder::fillFloor(int y, uint8_t id)
{
    if (y < 0 || y >= m_dimensions.y)
    {
        return;
    }

    for (int z = 0; z < m_dimensions.z; ++z)
    {
        for (int x = 0; x < m_dimensions.x; ++x)
        {
            m_data[index(x, y, z)] = id;
        }
    }
}

} // namespace engine
