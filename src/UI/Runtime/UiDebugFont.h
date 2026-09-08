#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Assets/Texture.h"
#include "UI/Runtime/UiTypes.h"

namespace ui
{

enum class FontRenderMode
{
    BitmapAlpha,
    Msdf,
};

enum class TextAlign
{
    Left,
    Center,
    Right,
};

struct TextLayoutOptions
{
    float maxWidth = 0.0f;
    float lineGap = 0.0f;
    TextAlign align = TextAlign::Left;
};

struct FontGlyph
{
    int codepoint = 0;
    UiUvRect uv{};
    float atlasWidth = 0.0f;
    float atlasHeight = 0.0f;
    float planeLeft = 0.0f;
    float planeBottom = 0.0f;
    float planeRight = 0.0f;
    float planeTop = 0.0f;
    float advance = 0.0f;
    bool hasPlaneBounds = false;
    bool visible = false;
};

struct FontAsset
{
    std::string id{};
    FontRenderMode renderMode = FontRenderMode::BitmapAlpha;
    std::string atlasSource{};
    std::string atlasImage{};
    int atlasWidth = 0;
    int atlasHeight = 0;
    int columns = 0;
    int rows = 0;
    int cellWidth = 0;
    int cellHeight = 0;
    int firstCodepoint = 0;
    int lastCodepoint = 0;
    float lineHeight = 0.0f;
    float ascender = 0.0f;
    float descender = 0.0f;
    float spaceAdvance = 0.0f;
    float defaultAdvance = 0.0f;
    float distanceRange = 0.0f;
    int fallbackCodepoint = 0;
    std::vector<FontGlyph> glyphs{};
    std::unordered_map<int, size_t> glyphByCodepoint{};
    std::unordered_map<uint64_t, float> kerningByPair{};
    bool loadedFromMetadata = false;
};

struct TextLayoutResult
{
    uint32_t glyphCount = 0;
    uint32_t lineCount = 0;
    float width = 0.0f;
    float height = 0.0f;
};

using DebugFontGlyph = FontGlyph;
using DebugFontAsset = FontAsset;
using DebugTextResult = TextLayoutResult;

const char* fontRenderModeLabel(FontRenderMode mode);
uint32_t fontRenderFlags(FontRenderMode mode);
FontAsset makeDefaultDebugFontAsset();
bool loadFontAsset(const std::filesystem::path& path, FontAsset& outAsset,
                   std::string* outError = nullptr);
bool loadDebugFontAsset(const std::filesystem::path& path, DebugFontAsset& outAsset,
                        std::string* outError = nullptr);
bool loadFontAtlasImage(const std::filesystem::path& assetRoot, const FontAsset& asset,
                        CpuImage& outImage, std::string* outError = nullptr);
CpuImage makeDebugFontAtlasImage();
FontGlyph fontGlyph(const FontAsset& asset, char c);
float fontKerning(const FontAsset& asset, int leftCodepoint, int rightCodepoint);
TextLayoutResult measureText(const FontAsset& asset, std::string_view text, float pixelHeight);
TextLayoutResult measureText(const FontAsset& asset, std::string_view text, float pixelHeight,
                             const TextLayoutOptions& options);
TextLayoutResult addText(const FontAsset& asset, UiDrawList& drawList, std::string_view text,
                         float x, float y, float pixelHeight, uint32_t color,
                         UiScissor scissor);
TextLayoutResult addText(const FontAsset& asset, UiDrawList& drawList, std::string_view text,
                         float x, float y, float pixelHeight, uint32_t color,
                         UiScissor scissor, const TextLayoutOptions& options);
DebugFontGlyph debugFontGlyph(const DebugFontAsset& asset, char c);
DebugTextResult measureDebugText(const DebugFontAsset& asset, std::string_view text,
                                 float pixelHeight);
DebugTextResult measureDebugText(const DebugFontAsset& asset, std::string_view text,
                                 float pixelHeight, const TextLayoutOptions& options);
DebugTextResult addDebugText(const DebugFontAsset& asset, UiDrawList& drawList,
                             std::string_view text, float x, float y, float pixelHeight,
                             uint32_t color, UiScissor scissor);
DebugTextResult addDebugText(const DebugFontAsset& asset, UiDrawList& drawList,
                             std::string_view text, float x, float y, float pixelHeight,
                             uint32_t color, UiScissor scissor,
                             const TextLayoutOptions& options);

} // namespace ui
