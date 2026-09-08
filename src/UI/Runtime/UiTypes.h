#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace ui
{

inline constexpr uint32_t kWhiteAtlasTextureId = 0u;
inline constexpr uint32_t kFontAtlasTextureId = 1u;
inline constexpr uint32_t kDebugFontAtlasTextureId = kFontAtlasTextureId;
inline constexpr uint32_t kUiTextureFlagMsdf = 1u << 0u;

struct UiRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct UiUvRect
{
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

struct UiScissor
{
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct UiVertex
{
    glm::vec2 pos{0.0f};
    glm::vec2 uv{0.0f};
    uint32_t color = 0xffffffffu;
    uint32_t flags = 0u;
};

struct UiDrawCommand
{
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t atlasTextureId = 0;
    UiScissor scissor{};
};

inline uint32_t packColor(float r, float g, float b, float a)
{
    const float alpha = std::clamp(a, 0.0f, 1.0f);
    const auto toByte = [](float value) {
        return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };

    const uint32_t rr = toByte(r * alpha);
    const uint32_t gg = toByte(g * alpha);
    const uint32_t bb = toByte(b * alpha);
    const uint32_t aa = toByte(alpha);
    return rr | (gg << 8u) | (bb << 16u) | (aa << 24u);
}

struct UiDrawList
{
    std::vector<UiVertex> vertices{};
    std::vector<uint32_t> indices{};
    std::vector<UiDrawCommand> commands{};

    void clear()
    {
        vertices.clear();
        indices.clear();
        commands.clear();
    }

    bool empty() const { return vertices.empty() || indices.empty() || commands.empty(); }

    void addTexturedRect(const UiRect& rect, const UiUvRect& uv, uint32_t color,
                         UiScissor scissor, uint32_t atlasTextureId, uint32_t flags = 0u)
    {
        if (rect.width <= 0.0f || rect.height <= 0.0f || scissor.width == 0 ||
            scissor.height == 0)
        {
            return;
        }

        const uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
        const uint32_t indexOffset = static_cast<uint32_t>(indices.size());

        const float x0 = rect.x;
        const float y0 = rect.y;
        const float x1 = rect.x + rect.width;
        const float y1 = rect.y + rect.height;

        vertices.push_back({glm::vec2(x0, y0), glm::vec2(uv.u0, uv.v0), color, flags});
        vertices.push_back({glm::vec2(x1, y0), glm::vec2(uv.u1, uv.v0), color, flags});
        vertices.push_back({glm::vec2(x1, y1), glm::vec2(uv.u1, uv.v1), color, flags});
        vertices.push_back({glm::vec2(x0, y1), glm::vec2(uv.u0, uv.v1), color, flags});

        indices.push_back(vertexOffset + 0u);
        indices.push_back(vertexOffset + 1u);
        indices.push_back(vertexOffset + 2u);
        indices.push_back(vertexOffset + 0u);
        indices.push_back(vertexOffset + 2u);
        indices.push_back(vertexOffset + 3u);

        UiDrawCommand command{};
        command.indexOffset = indexOffset;
        command.indexCount = 6;
        command.atlasTextureId = atlasTextureId;
        command.scissor = scissor;
        commands.push_back(command);
    }

    void addTexturedRect(const UiRect& rect, uint32_t color, UiScissor scissor,
                         uint32_t atlasTextureId)
    {
        addTexturedRect(rect, UiUvRect{}, color, scissor, atlasTextureId);
    }

    void addSolidRect(const UiRect& rect, uint32_t color, UiScissor scissor)
    {
        addTexturedRect(rect, color, scissor, kWhiteAtlasTextureId);
    }
};

} // namespace ui
