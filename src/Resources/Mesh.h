#pragma once

#include <vector>

#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

struct VulkanContext;
struct Commands;

struct Vertex
{
    glm::vec3 pos{};
    glm::vec3 normal{};
    glm::vec2 uv{};
};

struct MeshGpu
{
    VkBuffer vbo = VK_NULL_HANDLE;
    VkDeviceMemory vboMem = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;
    VkDeviceSize vboCapacityBytes = 0;

    VkBuffer ibo = VK_NULL_HANDLE;
    VkDeviceMemory iboMem = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
    bool useIndex = false;
    VkDeviceSize iboCapacityBytes = 0;
    uint32_t lastUsedFrame = 0;
};

std::vector<Vertex> makeCubeVertices();
std::vector<Vertex> makePlaneVertices(float halfExtent);
std::vector<Vertex> makeGridVertices(float halfExtent, int gridSize);
std::vector<Vertex> makeFishHeadVertices();
std::vector<Vertex> makeFishMidVertices();
std::vector<Vertex> makeFishTailPeduncleVertices();
std::vector<Vertex> makeFishHeadStripeVertices();
std::vector<Vertex> makeFishMidStripeVertices();
std::vector<Vertex> makeFishTailFinVertices();
std::vector<Vertex> makeFishDetailFinVertices();
std::vector<Vertex> makeFishEyeVertices();
void createMeshBuffer(VulkanContext& ctx, Commands& commands, const std::vector<Vertex>& vertices,
                      const std::vector<uint32_t>& indices, MeshGpu& mesh);
void createMeshBuffer(VulkanContext& ctx, Commands& commands, const std::vector<Vertex>& vertices,
                      MeshGpu& mesh);
void destroyMeshBuffer(VkDevice device, MeshGpu& mesh);
