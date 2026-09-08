#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace engine
{

// cpu-side voxel data builder with shape primitives.
// use this to construct voxel volumes procedurally before uploading to gpu.
class VoxelBuilder
{
public:
    explicit VoxelBuilder(const glm::ivec3& dimensions);

    // reset all voxels to empty (id 0)
    void clear();

    // single voxel operations
    void setVoxel(int x, int y, int z, uint8_t id);
    void setVoxel(const glm::ivec3& pos, uint8_t id);
    uint8_t getVoxel(int x, int y, int z) const;
    uint8_t getVoxel(const glm::ivec3& pos) const;

    // shape primitives - all coordinates are inclusive [min, max]
    void fillBox(const glm::ivec3& min, const glm::ivec3& max, uint8_t id);
    void fillShellBox(const glm::ivec3& min, const glm::ivec3& max, int thickness, uint8_t id);
    void fillSphere(const glm::vec3& center, float radius, uint8_t id);
    void fillEllipsoid(const glm::vec3& center, const glm::vec3& radii, uint8_t id);
    void fillCylinder(const glm::vec3& baseCenter, float radius, float height, int axis, uint8_t id);

    // convenience operations
    void fillFloor(int y, uint8_t id);  // fill entire xz plane at given y

    // access the built data
    const std::vector<uint8_t>& data() const { return m_data; }
    std::vector<uint8_t>& data() { return m_data; }
    const glm::ivec3& dimensions() const { return m_dimensions; }
    size_t byteCount() const { return m_data.size(); }

private:
    bool inBounds(int x, int y, int z) const;
    size_t index(int x, int y, int z) const;

    glm::ivec3 m_dimensions;
    std::vector<uint8_t> m_data;
};

} // namespace engine
