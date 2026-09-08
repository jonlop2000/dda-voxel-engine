#include "UI/Runtime/UiDebugFont.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace ui
{
namespace
{

using GlyphPattern = std::array<std::string_view, 7>;
using json = nlohmann::json;

constexpr int kFirstGlyph = 32;
constexpr int kLastGlyph = 126;
constexpr int kGlyphCount = kLastGlyph - kFirstGlyph + 1;
constexpr int kAtlasColumns = 16;
constexpr int kAtlasRows = 6;
constexpr int kCellWidth = 12;
constexpr int kCellHeight = 16;
constexpr int kBitScale = 2;
constexpr int kGlyphPixelWidth = 5;
constexpr int kGlyphPixelHeight = 7;
constexpr int kGlyphOffsetX = 1;
constexpr int kGlyphOffsetY = 1;
constexpr int kAdvancePixels = 11;
constexpr int kAtlasWidth = kAtlasColumns * kCellWidth;
constexpr int kAtlasHeight = kAtlasRows * kCellHeight;
constexpr const char* kFontSchema = "voxel.runtime_ui.font.v1";
constexpr const char* kLegacyDebugFontSchema = "voxel.runtime_ui.debug_font.v1";
constexpr const char* kDebugFontSource = "generated_debug_ascii_grid";

uint64_t kerningKey(int leftCodepoint, int rightCodepoint)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(leftCodepoint)) << 32u) |
           static_cast<uint32_t>(rightCodepoint);
}

const GlyphPattern& glyphPattern(char ch)
{
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    switch (upper)
    {
    case 'A':
    {
        static constexpr GlyphPattern p = {".###.", "#...#", "#...#", "#####", "#...#",
                                           "#...#", "#...#"};
        return p;
    }
    case 'B':
    {
        static constexpr GlyphPattern p = {"####.", "#...#", "#...#", "####.", "#...#",
                                           "#...#", "####."};
        return p;
    }
    case 'C':
    {
        static constexpr GlyphPattern p = {".####", "#....", "#....", "#....", "#....",
                                           "#....", ".####"};
        return p;
    }
    case 'D':
    {
        static constexpr GlyphPattern p = {"####.", "#...#", "#...#", "#...#", "#...#",
                                           "#...#", "####."};
        return p;
    }
    case 'E':
    {
        static constexpr GlyphPattern p = {"#####", "#....", "#....", "####.", "#....",
                                           "#....", "#####"};
        return p;
    }
    case 'F':
    {
        static constexpr GlyphPattern p = {"#####", "#....", "#....", "####.", "#....",
                                           "#....", "#...."};
        return p;
    }
    case 'G':
    {
        static constexpr GlyphPattern p = {".####", "#....", "#....", "#.###", "#...#",
                                           "#...#", ".####"};
        return p;
    }
    case 'H':
    {
        static constexpr GlyphPattern p = {"#...#", "#...#", "#...#", "#####", "#...#",
                                           "#...#", "#...#"};
        return p;
    }
    case 'I':
    {
        static constexpr GlyphPattern p = {"#####", "..#..", "..#..", "..#..", "..#..",
                                           "..#..", "#####"};
        return p;
    }
    case 'J':
    {
        static constexpr GlyphPattern p = {"..###", "...#.", "...#.", "...#.", "...#.",
                                           "#..#.", ".##.."};
        return p;
    }
    case 'K':
    {
        static constexpr GlyphPattern p = {"#...#", "#..#.", "#.#..", "##...", "#.#..",
                                           "#..#.", "#...#"};
        return p;
    }
    case 'L':
    {
        static constexpr GlyphPattern p = {"#....", "#....", "#....", "#....", "#....",
                                           "#....", "#####"};
        return p;
    }
    case 'M':
    {
        static constexpr GlyphPattern p = {"#...#", "##.##", "#.#.#", "#.#.#", "#...#",
                                           "#...#", "#...#"};
        return p;
    }
    case 'N':
    {
        static constexpr GlyphPattern p = {"#...#", "##..#", "#.#.#", "#..##", "#...#",
                                           "#...#", "#...#"};
        return p;
    }
    case 'O':
    {
        static constexpr GlyphPattern p = {".###.", "#...#", "#...#", "#...#", "#...#",
                                           "#...#", ".###."};
        return p;
    }
    case 'P':
    {
        static constexpr GlyphPattern p = {"####.", "#...#", "#...#", "####.", "#....",
                                           "#....", "#...."};
        return p;
    }
    case 'Q':
    {
        static constexpr GlyphPattern p = {".###.", "#...#", "#...#", "#...#", "#.#.#",
                                           "#..#.", ".##.#"};
        return p;
    }
    case 'R':
    {
        static constexpr GlyphPattern p = {"####.", "#...#", "#...#", "####.", "#.#..",
                                           "#..#.", "#...#"};
        return p;
    }
    case 'S':
    {
        static constexpr GlyphPattern p = {".####", "#....", "#....", ".###.", "....#",
                                           "....#", "####."};
        return p;
    }
    case 'T':
    {
        static constexpr GlyphPattern p = {"#####", "..#..", "..#..", "..#..", "..#..",
                                           "..#..", "..#.."};
        return p;
    }
    case 'U':
    {
        static constexpr GlyphPattern p = {"#...#", "#...#", "#...#", "#...#", "#...#",
                                           "#...#", ".###."};
        return p;
    }
    case 'V':
    {
        static constexpr GlyphPattern p = {"#...#", "#...#", "#...#", "#...#", "#...#",
                                           ".#.#.", "..#.."};
        return p;
    }
    case 'W':
    {
        static constexpr GlyphPattern p = {"#...#", "#...#", "#...#", "#.#.#", "#.#.#",
                                           "##.##", "#...#"};
        return p;
    }
    case 'X':
    {
        static constexpr GlyphPattern p = {"#...#", "#...#", ".#.#.", "..#..", ".#.#.",
                                           "#...#", "#...#"};
        return p;
    }
    case 'Y':
    {
        static constexpr GlyphPattern p = {"#...#", "#...#", ".#.#.", "..#..", "..#..",
                                           "..#..", "..#.."};
        return p;
    }
    case 'Z':
    {
        static constexpr GlyphPattern p = {"#####", "....#", "...#.", "..#..", ".#...",
                                           "#....", "#####"};
        return p;
    }
    case '0':
    {
        static constexpr GlyphPattern p = {".###.", "#...#", "#..##", "#.#.#", "##..#",
                                           "#...#", ".###."};
        return p;
    }
    case '1':
    {
        static constexpr GlyphPattern p = {"..#..", ".##..", "..#..", "..#..", "..#..",
                                           "..#..", ".###."};
        return p;
    }
    case '2':
    {
        static constexpr GlyphPattern p = {".###.", "#...#", "....#", "...#.", "..#..",
                                           ".#...", "#####"};
        return p;
    }
    case '3':
    {
        static constexpr GlyphPattern p = {"####.", "....#", "....#", ".###.", "....#",
                                           "....#", "####."};
        return p;
    }
    case '4':
    {
        static constexpr GlyphPattern p = {"#...#", "#...#", "#...#", "#####", "....#",
                                           "....#", "....#"};
        return p;
    }
    case '5':
    {
        static constexpr GlyphPattern p = {"#####", "#....", "#....", "####.", "....#",
                                           "....#", "####."};
        return p;
    }
    case '6':
    {
        static constexpr GlyphPattern p = {".###.", "#....", "#....", "####.", "#...#",
                                           "#...#", ".###."};
        return p;
    }
    case '7':
    {
        static constexpr GlyphPattern p = {"#####", "....#", "...#.", "..#..", ".#...",
                                           ".#...", ".#..."};
        return p;
    }
    case '8':
    {
        static constexpr GlyphPattern p = {".###.", "#...#", "#...#", ".###.", "#...#",
                                           "#...#", ".###."};
        return p;
    }
    case '9':
    {
        static constexpr GlyphPattern p = {".###.", "#...#", "#...#", ".####", "....#",
                                           "....#", ".###."};
        return p;
    }
    case '-':
    {
        static constexpr GlyphPattern p = {".....", ".....", ".....", ".###.", ".....",
                                           ".....", "....."};
        return p;
    }
    case '.':
    {
        static constexpr GlyphPattern p = {".....", ".....", ".....", ".....", ".....",
                                           ".##..", ".##.."};
        return p;
    }
    case ':':
    {
        static constexpr GlyphPattern p = {".....", ".##..", ".##..", ".....", ".##..",
                                           ".##..", "....."};
        return p;
    }
    case '/':
    {
        static constexpr GlyphPattern p = {"....#", "...#.", "...#.", "..#..", ".#...",
                                           ".#...", "#...."};
        return p;
    }
    case ' ':
    {
        static constexpr GlyphPattern p = {".....", ".....", ".....", ".....", ".....",
                                           ".....", "....."};
        return p;
    }
    case '!':
    {
        static constexpr GlyphPattern p = {"..#..", "..#..", "..#..", "..#..", "..#..",
                                           ".....", "..#.."};
        return p;
    }
    default:
    {
        static constexpr GlyphPattern p = {".###.", "#...#", "...#.", "..#..", "..#..",
                                           ".....", "..#.."};
        return p;
    }
    }
}

bool readInt(const json& j, const char* key, int& outValue, std::string& outError)
{
    if (!j.contains(key) || !j[key].is_number_integer())
    {
        outError = std::string("Font field '") + key + "' must be an integer";
        return false;
    }
    outValue = j[key].get<int>();
    return true;
}

bool readFloat(const json& j, const char* key, float& outValue, std::string& outError)
{
    if (!j.contains(key) || !j[key].is_number())
    {
        outError = std::string("Font field '") + key + "' must be a number";
        return false;
    }
    outValue = j[key].get<float>();
    return true;
}

bool readOptionalFloat(const json& j, const char* key, float& outValue, float defaultValue,
                       std::string& outError)
{
    if (!j.contains(key))
    {
        outValue = defaultValue;
        return true;
    }
    return readFloat(j, key, outValue, outError);
}

bool readBounds(const json& j, const char* key, float& left, float& bottom, float& right,
                float& top, std::string& outError)
{
    if (!j.contains(key) || !j[key].is_object())
    {
        outError = std::string("Font field '") + key + "' must be an object";
        return false;
    }
    const json& bounds = j[key];
    return readFloat(bounds, "left", left, outError) &&
           readFloat(bounds, "bottom", bottom, outError) &&
           readFloat(bounds, "right", right, outError) &&
           readFloat(bounds, "top", top, outError);
}

bool readOptionalYOriginTop(const json& atlas)
{
    const auto readString = [&atlas](const char* key, std::string& outValue) {
        if (!atlas.contains(key) || !atlas[key].is_string())
        {
            return false;
        }
        outValue = atlas[key].get<std::string>();
        return true;
    };

    std::string yOrigin;
    if (!readString("y_origin", yOrigin) && !readString("yOrigin", yOrigin))
    {
        return false;
    }
    return yOrigin == "top";
}

bool parseRenderMode(const json& root, FontRenderMode& outMode, std::string& outError)
{
    if (!root.contains("render_mode"))
    {
        outMode = FontRenderMode::BitmapAlpha;
        return true;
    }
    if (!root["render_mode"].is_string())
    {
        outError = "Font field 'render_mode' must be a string";
        return false;
    }
    const std::string value = root["render_mode"].get<std::string>();
    if (value == "bitmap_alpha")
    {
        outMode = FontRenderMode::BitmapAlpha;
        return true;
    }
    if (value == "msdf")
    {
        outMode = FontRenderMode::Msdf;
        return true;
    }
    outError = "Font field 'render_mode' must be bitmap_alpha or msdf";
    return false;
}

bool requireExactInt(const char* key, int value, int expected, std::string& outError)
{
    if (value != expected)
    {
        outError = std::string("Font field '") + key + "' must be " +
                   std::to_string(expected) + " for the generated debug atlas";
        return false;
    }
    return true;
}

bool validateGeneratedDebugAsset(const FontAsset& asset, std::string& outError)
{
    if (asset.atlasSource != kDebugFontSource)
    {
        outError = "Debug font atlas source must be generated_debug_ascii_grid";
        return false;
    }
    if (asset.renderMode != FontRenderMode::BitmapAlpha)
    {
        outError = "The generated debug atlas must use render_mode bitmap_alpha";
        return false;
    }
    return requireExactInt("atlas.width", asset.atlasWidth, kAtlasWidth, outError) &&
           requireExactInt("atlas.height", asset.atlasHeight, kAtlasHeight, outError) &&
           requireExactInt("atlas.columns", asset.columns, kAtlasColumns, outError) &&
           requireExactInt("atlas.rows", asset.rows, kAtlasRows, outError) &&
           requireExactInt("atlas.cell_width", asset.cellWidth, kCellWidth, outError) &&
           requireExactInt("atlas.cell_height", asset.cellHeight, kCellHeight, outError) &&
           requireExactInt("atlas.first_codepoint", asset.firstCodepoint, kFirstGlyph,
                           outError) &&
           requireExactInt("atlas.last_codepoint", asset.lastCodepoint, kLastGlyph,
                           outError) &&
           requireExactInt("metrics.fallback_codepoint", asset.fallbackCodepoint, '?',
                           outError);
}

bool parseGlyphs(const json& root, const json& atlas, FontAsset& asset, std::string& outError)
{
    if (!root.contains("glyphs"))
    {
        return true;
    }
    if (!root["glyphs"].is_array())
    {
        outError = "Font field 'glyphs' must be an array";
        return false;
    }

    const bool atlasOriginTop = readOptionalYOriginTop(atlas);
    for (const json& glyphJson : root["glyphs"])
    {
        if (!glyphJson.is_object())
        {
            outError = "Font glyph entries must be objects";
            return false;
        }

        int codepoint = 0;
        if (glyphJson.contains("unicode"))
        {
            if (!readInt(glyphJson, "unicode", codepoint, outError))
            {
                return false;
            }
        }
        else if (glyphJson.contains("codepoint"))
        {
            if (!readInt(glyphJson, "codepoint", codepoint, outError))
            {
                return false;
            }
        }
        else
        {
            outError = "Font glyph entry requires 'unicode' or 'codepoint'";
            return false;
        }

        FontGlyph glyph{};
        glyph.codepoint = codepoint;
        if (!readFloat(glyphJson, "advance", glyph.advance, outError))
        {
            return false;
        }

        if (glyphJson.contains("planeBounds"))
        {
            if (!readBounds(glyphJson, "planeBounds", glyph.planeLeft, glyph.planeBottom,
                            glyph.planeRight, glyph.planeTop, outError))
            {
                return false;
            }
            glyph.hasPlaneBounds = true;
        }

        if (glyphJson.contains("atlasBounds"))
        {
            float atlasLeft = 0.0f;
            float atlasBottom = 0.0f;
            float atlasRight = 0.0f;
            float atlasTop = 0.0f;
            if (!readBounds(glyphJson, "atlasBounds", atlasLeft, atlasBottom, atlasRight,
                            atlasTop, outError))
            {
                return false;
            }
            if (atlasRight < atlasLeft || atlasTop < atlasBottom)
            {
                outError = "Font glyph atlasBounds must have right >= left and top >= bottom";
                return false;
            }

            const float atlasWidth = static_cast<float>(asset.atlasWidth);
            const float atlasHeight = static_cast<float>(asset.atlasHeight);
            if (atlasOriginTop)
            {
                glyph.uv = UiUvRect{atlasLeft / atlasWidth, atlasBottom / atlasHeight,
                                    atlasRight / atlasWidth, atlasTop / atlasHeight};
            }
            else
            {
                glyph.uv = UiUvRect{atlasLeft / atlasWidth, 1.0f - atlasTop / atlasHeight,
                                    atlasRight / atlasWidth,
                                    1.0f - atlasBottom / atlasHeight};
            }
            glyph.atlasWidth = atlasRight - atlasLeft;
            glyph.atlasHeight = atlasTop - atlasBottom;
            glyph.visible = glyph.atlasWidth > 0.0f && glyph.atlasHeight > 0.0f &&
                            glyph.hasPlaneBounds;
        }

        const size_t glyphIndex = asset.glyphs.size();
        asset.glyphByCodepoint[codepoint] = glyphIndex;
        asset.glyphs.push_back(glyph);
    }

    return true;
}

bool parseKerning(const json& root, FontAsset& asset, std::string& outError)
{
    if (!root.contains("kerning"))
    {
        return true;
    }
    if (!root["kerning"].is_array())
    {
        outError = "Font field 'kerning' must be an array";
        return false;
    }

    for (const json& pairJson : root["kerning"])
    {
        if (!pairJson.is_object())
        {
            outError = "Font kerning entries must be objects";
            return false;
        }

        int left = 0;
        int right = 0;
        float advance = 0.0f;
        if (pairJson.contains("unicode1"))
        {
            if (!readInt(pairJson, "unicode1", left, outError))
            {
                return false;
            }
        }
        else if (!readInt(pairJson, "left", left, outError))
        {
            return false;
        }

        if (pairJson.contains("unicode2"))
        {
            if (!readInt(pairJson, "unicode2", right, outError))
            {
                return false;
            }
        }
        else if (!readInt(pairJson, "right", right, outError))
        {
            return false;
        }

        if (!readFloat(pairJson, "advance", advance, outError))
        {
            return false;
        }
        asset.kerningByPair[kerningKey(left, right)] = advance;
    }

    return true;
}

bool validateFontAsset(const FontAsset& asset, std::string& outError)
{
    if (asset.id.empty())
    {
        outError = "Font field 'id' must not be empty";
        return false;
    }
    if (asset.atlasSource.empty())
    {
        outError = "Font field 'atlas.source' must not be empty";
        return false;
    }
    if (asset.atlasWidth <= 0 || asset.atlasHeight <= 0 || asset.columns <= 0 ||
        asset.rows <= 0 || asset.cellWidth <= 0 || asset.cellHeight <= 0)
    {
        outError = "Font atlas dimensions, grid, and cell sizes must be positive";
        return false;
    }
    if (asset.firstCodepoint > asset.lastCodepoint)
    {
        outError = "Font atlas first_codepoint must be <= last_codepoint";
        return false;
    }
    const int glyphCount = asset.lastCodepoint - asset.firstCodepoint + 1;
    if (glyphCount > asset.columns * asset.rows)
    {
        outError = "Font atlas grid does not contain enough cells for the codepoint range";
        return false;
    }
    if (asset.lineHeight <= 0.0f || asset.spaceAdvance < 0.0f ||
        asset.defaultAdvance <= 0.0f)
    {
        outError = "Font metrics line_height/default_advance must be positive";
        return false;
    }
    if (asset.fallbackCodepoint < asset.firstCodepoint ||
        asset.fallbackCodepoint > asset.lastCodepoint)
    {
        outError = "Font metrics fallback_codepoint must be inside the codepoint range";
        return false;
    }
    if (asset.atlasImage.empty() && asset.atlasSource != kDebugFontSource)
    {
        outError = "Font field 'atlas.image' is required unless atlas.source is "
                   "generated_debug_ascii_grid";
        return false;
    }
    if (asset.atlasSource == kDebugFontSource)
    {
        return validateGeneratedDebugAsset(asset, outError);
    }
    if (!asset.glyphs.empty() &&
        asset.glyphByCodepoint.find(asset.fallbackCodepoint) == asset.glyphByCodepoint.end())
    {
        outError = "Font glyphs must include the fallback_codepoint";
        return false;
    }
    return true;
}

} // namespace

const char* fontRenderModeLabel(FontRenderMode mode)
{
    switch (mode)
    {
    case FontRenderMode::BitmapAlpha:
        return "bitmap_alpha";
    case FontRenderMode::Msdf:
        return "msdf";
    }
    return "unknown";
}

uint32_t fontRenderFlags(FontRenderMode mode)
{
    return mode == FontRenderMode::Msdf ? kUiTextureFlagMsdf : 0u;
}

FontAsset makeDefaultDebugFontAsset()
{
    FontAsset asset{};
    asset.id = "runtime_debug_font";
    asset.renderMode = FontRenderMode::BitmapAlpha;
    asset.atlasSource = kDebugFontSource;
    asset.atlasWidth = kAtlasWidth;
    asset.atlasHeight = kAtlasHeight;
    asset.columns = kAtlasColumns;
    asset.rows = kAtlasRows;
    asset.cellWidth = kCellWidth;
    asset.cellHeight = kCellHeight;
    asset.firstCodepoint = kFirstGlyph;
    asset.lastCodepoint = kLastGlyph;
    asset.lineHeight = static_cast<float>(kCellHeight);
    asset.ascender = static_cast<float>(kCellHeight);
    asset.descender = 0.0f;
    asset.spaceAdvance = 7.0f;
    asset.defaultAdvance = static_cast<float>(kAdvancePixels);
    asset.distanceRange = 0.0f;
    asset.fallbackCodepoint = '?';
    return asset;
}

bool loadFontAsset(const std::filesystem::path& path, FontAsset& outAsset,
                   std::string* outError)
{
    outAsset = FontAsset{};
    if (outError != nullptr)
    {
        outError->clear();
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        if (outError != nullptr)
        {
            *outError = "Failed to open runtime UI font metadata: " + path.string();
        }
        return false;
    }

    try
    {
        const json root = json::parse(file);
        if (!root.contains("schema") || !root["schema"].is_string())
        {
            if (outError != nullptr)
            {
                *outError = std::string("Font schema must be ") + kFontSchema;
            }
            return false;
        }
        const std::string schema = root["schema"].get<std::string>();
        if (schema != kFontSchema && schema != kLegacyDebugFontSchema)
        {
            if (outError != nullptr)
            {
                *outError = std::string("Font schema must be ") + kFontSchema;
            }
            return false;
        }
        if (!root.contains("id") || !root["id"].is_string())
        {
            if (outError != nullptr)
            {
                *outError = "Font field 'id' must be a string";
            }
            return false;
        }
        if (!root.contains("atlas") || !root["atlas"].is_object() ||
            !root.contains("metrics") || !root["metrics"].is_object())
        {
            if (outError != nullptr)
            {
                *outError = "Font metadata requires object fields 'atlas' and 'metrics'";
            }
            return false;
        }

        const json& atlas = root["atlas"];
        const json& metrics = root["metrics"];
        FontAsset asset{};
        asset.id = root["id"].get<std::string>();
        std::string error;
        if (!parseRenderMode(root, asset.renderMode, error))
        {
            if (outError != nullptr)
            {
                *outError = error;
            }
            return false;
        }
        if (!atlas.contains("source") || !atlas["source"].is_string())
        {
            if (outError != nullptr)
            {
                *outError = "Font field 'atlas.source' must be a string";
            }
            return false;
        }
        asset.atlasSource = atlas["source"].get<std::string>();
        if (atlas.contains("image"))
        {
            if (!atlas["image"].is_string())
            {
                if (outError != nullptr)
                {
                    *outError = "Font field 'atlas.image' must be a string";
                }
                return false;
            }
            asset.atlasImage = atlas["image"].get<std::string>();
        }

        if (!readInt(atlas, "width", asset.atlasWidth, error) ||
            !readInt(atlas, "height", asset.atlasHeight, error) ||
            !readInt(atlas, "columns", asset.columns, error) ||
            !readInt(atlas, "rows", asset.rows, error) ||
            !readInt(atlas, "cell_width", asset.cellWidth, error) ||
            !readInt(atlas, "cell_height", asset.cellHeight, error) ||
            !readInt(atlas, "first_codepoint", asset.firstCodepoint, error) ||
            !readInt(atlas, "last_codepoint", asset.lastCodepoint, error) ||
            !readFloat(metrics, "line_height", asset.lineHeight, error) ||
            !readOptionalFloat(metrics, "ascender", asset.ascender, asset.lineHeight, error) ||
            !readOptionalFloat(metrics, "descender", asset.descender, 0.0f, error) ||
            !readFloat(metrics, "space_advance", asset.spaceAdvance, error) ||
            !readFloat(metrics, "default_advance", asset.defaultAdvance, error) ||
            !readOptionalFloat(metrics, "distance_range", asset.distanceRange, 0.0f, error) ||
            !readInt(metrics, "fallback_codepoint", asset.fallbackCodepoint, error) ||
            !parseGlyphs(root, atlas, asset, error) || !parseKerning(root, asset, error) ||
            !validateFontAsset(asset, error))
        {
            if (outError != nullptr)
            {
                *outError = error;
            }
            return false;
        }

        asset.loadedFromMetadata = true;
        outAsset = asset;
        return true;
    }
    catch (const json::exception& e)
    {
        if (outError != nullptr)
        {
            *outError = std::string("JSON parse error in runtime UI font metadata: ") + e.what();
        }
        return false;
    }
}

bool loadDebugFontAsset(const std::filesystem::path& path, DebugFontAsset& outAsset,
                        std::string* outError)
{
    return loadFontAsset(path, outAsset, outError);
}

bool loadFontAtlasImage(const std::filesystem::path& assetRoot, const FontAsset& asset,
                        CpuImage& outImage, std::string* outError)
{
    outImage = CpuImage{};
    if (outError != nullptr)
    {
        outError->clear();
    }

    if (asset.atlasImage.empty())
    {
        if (asset.atlasSource == kDebugFontSource)
        {
            outImage = makeDebugFontAtlasImage();
            return true;
        }
        if (outError != nullptr)
        {
            *outError = "Font atlas image path is empty";
        }
        return false;
    }

    std::filesystem::path atlasPath(asset.atlasImage);
    if (atlasPath.is_relative())
    {
        atlasPath = assetRoot / atlasPath;
    }

    std::string imageError;
    if (!loadImageRGBA8(atlasPath, false, outImage, &imageError, false))
    {
        if (outError != nullptr)
        {
            *outError = "Failed to load font atlas image: " + atlasPath.string();
            if (!imageError.empty())
            {
                *outError += " (" + imageError + ")";
            }
        }
        return false;
    }

    if (outImage.w != asset.atlasWidth || outImage.h != asset.atlasHeight)
    {
        if (outError != nullptr)
        {
            *outError = "Font atlas image dimensions do not match metadata: " +
                        std::to_string(outImage.w) + "x" + std::to_string(outImage.h) +
                        " loaded, expected " + std::to_string(asset.atlasWidth) + "x" +
                        std::to_string(asset.atlasHeight);
        }
        outImage = CpuImage{};
        return false;
    }

    return true;
}

CpuImage makeDebugFontAtlasImage()
{
    CpuImage image{};
    image.w = kAtlasWidth;
    image.h = kAtlasHeight;
    image.comp = 4;
    image.srgb = false;
    image.rgba.resize(static_cast<size_t>(kAtlasWidth * kAtlasHeight * 4), 0u);

    auto writePixel = [&image](int x, int y) {
        if (x < 0 || y < 0 || x >= image.w || y >= image.h)
        {
            return;
        }
        const size_t offset = static_cast<size_t>((y * image.w + x) * 4);
        image.rgba[offset + 0] = 255;
        image.rgba[offset + 1] = 255;
        image.rgba[offset + 2] = 255;
        image.rgba[offset + 3] = 255;
    };

    for (int code = kFirstGlyph; code <= kLastGlyph; ++code)
    {
        const int index = code - kFirstGlyph;
        const int col = index % kAtlasColumns;
        const int row = index / kAtlasColumns;
        const int baseX = col * kCellWidth;
        const int baseY = row * kCellHeight;
        const GlyphPattern& pattern = glyphPattern(static_cast<char>(code));
        for (int gy = 0; gy < kGlyphPixelHeight; ++gy)
        {
            for (int gx = 0; gx < kGlyphPixelWidth; ++gx)
            {
                if (pattern[static_cast<size_t>(gy)][static_cast<size_t>(gx)] != '#')
                {
                    continue;
                }
                for (int sy = 0; sy < kBitScale; ++sy)
                {
                    for (int sx = 0; sx < kBitScale; ++sx)
                    {
                        writePixel(baseX + kGlyphOffsetX + gx * kBitScale + sx,
                                   baseY + kGlyphOffsetY + gy * kBitScale + sy);
                    }
                }
            }
        }
    }

    return image;
}

FontGlyph fontGlyphForCodepoint(const FontAsset& asset, int codepoint)
{
    // loaded fonts can carry glyphs outside the contiguous grid range (e.g.
    // latin-1 accents like u+00A1 / u+00E9), so consult the codepoint map for the
    // real codepoint *before* clamping to the grid fallback range.
    if (!asset.glyphs.empty())
    {
        auto it = asset.glyphByCodepoint.find(codepoint);
        if (it == asset.glyphByCodepoint.end())
        {
            it = asset.glyphByCodepoint.find(asset.fallbackCodepoint);
        }
        if (it != asset.glyphByCodepoint.end() && it->second < asset.glyphs.size())
        {
            return asset.glyphs[it->second];
        }
    }

    int code = codepoint;
    if (code < asset.firstCodepoint || code > asset.lastCodepoint)
    {
        code = asset.fallbackCodepoint;
    }
    if (code < asset.firstCodepoint || code > asset.lastCodepoint)
    {
        code = '?';
    }

    const int index = code - asset.firstCodepoint;
    const int col = index % asset.columns;
    const int row = index / asset.columns;
    const float x0 = static_cast<float>(col * asset.cellWidth);
    const float y0 = static_cast<float>(row * asset.cellHeight);

    FontGlyph glyph{};
    glyph.codepoint = code;
    glyph.uv = UiUvRect{
        x0 / static_cast<float>(asset.atlasWidth),
        y0 / static_cast<float>(asset.atlasHeight),
        (x0 + static_cast<float>(asset.cellWidth)) / static_cast<float>(asset.atlasWidth),
        (y0 + static_cast<float>(asset.cellHeight)) / static_cast<float>(asset.atlasHeight),
    };
    glyph.atlasWidth = static_cast<float>(asset.cellWidth);
    glyph.atlasHeight = static_cast<float>(asset.cellHeight);
    glyph.advance = code == ' ' ? asset.spaceAdvance : asset.defaultAdvance;
    glyph.visible = code != ' ';
    return glyph;
}

FontGlyph fontGlyph(const FontAsset& asset, char c)
{
    return fontGlyphForCodepoint(asset, static_cast<unsigned char>(c));
}

float fontKerning(const FontAsset& asset, int leftCodepoint, int rightCodepoint)
{
    const auto it = asset.kerningByPair.find(kerningKey(leftCodepoint, rightCodepoint));
    if (it == asset.kerningByPair.end())
    {
        return 0.0f;
    }
    return it->second;
}

namespace
{

struct LaidOutGlyph
{
    FontGlyph glyph{};
    float cursorX = 0.0f;
};

struct LaidOutLine
{
    std::vector<LaidOutGlyph> glyphs{};
    float width = 0.0f;
    uint32_t visibleGlyphCount = 0;
    int previousCodepoint = -1;
};

struct TextLayout
{
    std::vector<LaidOutLine> lines{};
    TextLayoutResult result{};
};

bool isTextSeparator(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

bool hasVisibleGlyphs(const LaidOutLine& line)
{
    return line.visibleGlyphCount > 0 || line.width > 0.0f;
}

// decode one utf-8 codepoint starting at byte `pos`. ascii bytes (< 0x80) decode
// to themselves, so existing ascii text is byte-for-byte unaffected. malformed or
// truncated sequences fall back to the single leading byte (latin-1 style).
struct Utf8Decoded
{
    int codepoint = 0;
    size_t length = 1;
};

Utf8Decoded decodeUtf8(std::string_view text, size_t pos)
{
    const unsigned char b0 = static_cast<unsigned char>(text[pos]);
    if (b0 < 0x80u)
    {
        return {b0, 1};
    }
    const size_t remaining = text.size() - pos;
    const auto cont = [&](size_t i) {
        return (static_cast<unsigned char>(text[pos + i]) & 0xC0u) == 0x80u;
    };
    if ((b0 & 0xE0u) == 0xC0u && remaining >= 2 && cont(1))
    {
        const int cp = ((b0 & 0x1Fu) << 6) |
                       (static_cast<unsigned char>(text[pos + 1]) & 0x3Fu);
        return {cp, 2};
    }
    if ((b0 & 0xF0u) == 0xE0u && remaining >= 3 && cont(1) && cont(2))
    {
        const int cp = ((b0 & 0x0Fu) << 12) |
                       ((static_cast<unsigned char>(text[pos + 1]) & 0x3Fu) << 6) |
                       (static_cast<unsigned char>(text[pos + 2]) & 0x3Fu);
        return {cp, 3};
    }
    if ((b0 & 0xF8u) == 0xF0u && remaining >= 4 && cont(1) && cont(2) && cont(3))
    {
        const int cp = ((b0 & 0x07u) << 18) |
                       ((static_cast<unsigned char>(text[pos + 1]) & 0x3Fu) << 12) |
                       ((static_cast<unsigned char>(text[pos + 2]) & 0x3Fu) << 6) |
                       (static_cast<unsigned char>(text[pos + 3]) & 0x3Fu);
        return {cp, 4};
    }
    return {b0, 1};
}

void appendCodepointToLine(const FontAsset& asset, LaidOutLine& line, int codepoint,
                           float scale)
{
    const FontGlyph glyph = fontGlyphForCodepoint(asset, codepoint);
    if (line.previousCodepoint >= 0)
    {
        line.width += fontKerning(asset, line.previousCodepoint, glyph.codepoint) * scale;
    }
    if (glyph.visible)
    {
        line.glyphs.push_back(LaidOutGlyph{glyph, line.width});
        ++line.visibleGlyphCount;
    }
    line.width += glyph.advance * scale;
    line.previousCodepoint = glyph.codepoint;
}

void appendCharToLine(const FontAsset& asset, LaidOutLine& line, char c, float scale)
{
    appendCodepointToLine(asset, line, static_cast<unsigned char>(c), scale);
}

void appendRunToLine(const FontAsset& asset, LaidOutLine& line, std::string_view run,
                     float scale)
{
    size_t pos = 0;
    while (pos < run.size())
    {
        const Utf8Decoded decoded = decodeUtf8(run, pos);
        appendCodepointToLine(asset, line, decoded.codepoint, scale);
        pos += decoded.length;
    }
}

void appendLine(TextLayout& layout, LaidOutLine& line, bool force)
{
    if (!force && !hasVisibleGlyphs(line))
    {
        return;
    }

    layout.result.width = std::max(layout.result.width, line.width);
    layout.result.glyphCount += line.visibleGlyphCount;
    layout.lines.push_back(std::move(line));
    line = LaidOutLine{};
}

void appendWrappedRun(const FontAsset& asset, TextLayout& layout, LaidOutLine& line,
                      std::string_view run, float scale, float maxWidth)
{
    size_t pos = 0;
    while (pos < run.size())
    {
        const Utf8Decoded decoded = decodeUtf8(run, pos);
        pos += decoded.length;
        LaidOutLine candidate = line;
        appendCodepointToLine(asset, candidate, decoded.codepoint, scale);
        if (maxWidth > 0.0f && candidate.width > maxWidth && hasVisibleGlyphs(line))
        {
            appendLine(layout, line, true);
            appendCodepointToLine(asset, line, decoded.codepoint, scale);
        }
        else
        {
            line = std::move(candidate);
        }
    }
}

TextLayout layoutText(const FontAsset& asset, std::string_view text, float pixelHeight,
                      const TextLayoutOptions& options)
{
    const float scale = pixelHeight / std::max(asset.lineHeight, 1.0f);
    const float maxWidth = std::max(0.0f, options.maxWidth);
    TextLayout layout{};
    LaidOutLine line{};
    bool pendingSeparator = false;

    size_t pos = 0;
    while (pos < text.size())
    {
        const char c = text[pos];
        if (c == '\n')
        {
            appendLine(layout, line, true);
            pendingSeparator = false;
            ++pos;
            continue;
        }
        if (isTextSeparator(c))
        {
            pendingSeparator = hasVisibleGlyphs(line);
            ++pos;
            continue;
        }

        const size_t runStart = pos;
        while (pos < text.size() && text[pos] != '\n' && !isTextSeparator(text[pos]))
        {
            ++pos;
        }
        const std::string_view run = text.substr(runStart, pos - runStart);

        LaidOutLine candidate = line;
        if (pendingSeparator && hasVisibleGlyphs(candidate))
        {
            appendCharToLine(asset, candidate, ' ', scale);
        }
        appendRunToLine(asset, candidate, run, scale);

        if (maxWidth > 0.0f && candidate.width > maxWidth && hasVisibleGlyphs(line))
        {
            appendLine(layout, line, true);
            pendingSeparator = false;
            appendWrappedRun(asset, layout, line, run, scale, maxWidth);
        }
        else if (maxWidth > 0.0f && candidate.width > maxWidth)
        {
            appendWrappedRun(asset, layout, line, run, scale, maxWidth);
        }
        else
        {
            line = std::move(candidate);
        }
        pendingSeparator = true;
    }

    appendLine(layout, line, false);
    layout.result.lineCount = static_cast<uint32_t>(layout.lines.size());
    if (layout.result.lineCount > 0)
    {
        layout.result.height =
            static_cast<float>(layout.result.lineCount) * pixelHeight +
            static_cast<float>(layout.result.lineCount - 1u) * std::max(0.0f, options.lineGap);
    }
    return layout;
}

float alignedLineOffset(float lineWidth, const TextLayoutOptions& options)
{
    const float containerWidth = options.maxWidth > 0.0f ? options.maxWidth : lineWidth;
    const float remaining = containerWidth - lineWidth;
    switch (options.align)
    {
    case TextAlign::Left:
        return 0.0f;
    case TextAlign::Center:
        return remaining * 0.5f;
    case TextAlign::Right:
        return remaining;
    }
    return 0.0f;
}

} // namespace

TextLayoutResult measureText(const FontAsset& asset, std::string_view text, float pixelHeight)
{
    return measureText(asset, text, pixelHeight, TextLayoutOptions{});
}

TextLayoutResult measureText(const FontAsset& asset, std::string_view text, float pixelHeight,
                             const TextLayoutOptions& options)
{
    return layoutText(asset, text, pixelHeight, options).result;
}

TextLayoutResult addText(const FontAsset& asset, UiDrawList& drawList, std::string_view text,
                         float x, float y, float pixelHeight, uint32_t color,
                         UiScissor scissor)
{
    return addText(asset, drawList, text, x, y, pixelHeight, color, scissor,
                   TextLayoutOptions{});
}

TextLayoutResult addText(const FontAsset& asset, UiDrawList& drawList, std::string_view text,
                         float x, float y, float pixelHeight, uint32_t color,
                         UiScissor scissor, const TextLayoutOptions& options)
{
    const float scale = pixelHeight / std::max(asset.lineHeight, 1.0f);
    const TextLayout layout = layoutText(asset, text, pixelHeight, options);
    float cursorY = y;
    for (const LaidOutLine& line : layout.lines)
    {
        const float lineOffset = alignedLineOffset(line.width, options);
        for (const LaidOutGlyph& laidOutGlyph : line.glyphs)
        {
            const FontGlyph& glyph = laidOutGlyph.glyph;
            UiRect glyphRect{x + lineOffset + laidOutGlyph.cursorX, cursorY,
                             glyph.atlasWidth * scale, glyph.atlasHeight * scale};
            if (glyph.hasPlaneBounds)
            {
                const float baselineY = cursorY + asset.ascender * scale;
                glyphRect.x = x + lineOffset + laidOutGlyph.cursorX + glyph.planeLeft * scale;
                glyphRect.y = baselineY - glyph.planeTop * scale;
                glyphRect.width = (glyph.planeRight - glyph.planeLeft) * scale;
                glyphRect.height = (glyph.planeTop - glyph.planeBottom) * scale;
            }
            drawList.addTexturedRect(
                glyphRect, glyph.uv, color, scissor, kFontAtlasTextureId,
                fontRenderFlags(asset.renderMode));
        }
        cursorY += pixelHeight + std::max(0.0f, options.lineGap);
    }
    return layout.result;
}

DebugFontGlyph debugFontGlyph(const DebugFontAsset& asset, char c)
{
    return fontGlyph(asset, c);
}

DebugTextResult measureDebugText(const DebugFontAsset& asset, std::string_view text,
                                 float pixelHeight)
{
    return measureText(asset, text, pixelHeight);
}

DebugTextResult measureDebugText(const DebugFontAsset& asset, std::string_view text,
                                 float pixelHeight, const TextLayoutOptions& options)
{
    return measureText(asset, text, pixelHeight, options);
}

DebugTextResult addDebugText(const DebugFontAsset& asset, UiDrawList& drawList,
                             std::string_view text, float x, float y, float pixelHeight,
                             uint32_t color, UiScissor scissor)
{
    return addText(asset, drawList, text, x, y, pixelHeight, color, scissor);
}

DebugTextResult addDebugText(const DebugFontAsset& asset, UiDrawList& drawList,
                             std::string_view text, float x, float y, float pixelHeight,
                             uint32_t color, UiScissor scissor,
                             const TextLayoutOptions& options)
{
    return addText(asset, drawList, text, x, y, pixelHeight, color, scissor, options);
}

} // namespace ui
