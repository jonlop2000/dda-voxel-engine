#include "engine/voxel/VoxLoader.h"

#include <cstring>
#include <fstream>
#include <limits>

namespace engine
{

namespace
{

struct ChunkHeader
{
    char id[4]{};
    uint32_t contentSize = 0;
    uint32_t childrenSize = 0;
};

bool setError(std::string* outError, const std::string& msg)
{
    if (outError != nullptr)
    {
        *outError = msg;
    }
    return false;
}

bool readU32(const uint8_t*& ptr, const uint8_t* end, uint32_t& out)
{
    if (static_cast<size_t>(end - ptr) < 4)
    {
        return false;
    }
    out = static_cast<uint32_t>(ptr[0]) |
          (static_cast<uint32_t>(ptr[1]) << 8) |
          (static_cast<uint32_t>(ptr[2]) << 16) |
          (static_cast<uint32_t>(ptr[3]) << 24);
    ptr += 4;
    return true;
}

bool readChunkHeader(const uint8_t*& ptr, const uint8_t* end, ChunkHeader& out)
{
    if (static_cast<size_t>(end - ptr) < 12)
    {
        return false;
    }
    std::memcpy(out.id, ptr, 4);
    ptr += 4;
    if (!readU32(ptr, end, out.contentSize))
    {
        return false;
    }
    if (!readU32(ptr, end, out.childrenSize))
    {
        return false;
    }
    return true;
}

bool idEquals(const char* id, const char* lit)
{
    return std::memcmp(id, lit, 4) == 0;
}

void fillFallbackPalette(VoxPalette& palette)
{
    palette.colors[0] = VoxColor{0, 0, 0, 0};
    for (int i = 1; i < 256; ++i)
    {
        const uint8_t v = static_cast<uint8_t>(i);
        palette.colors[static_cast<size_t>(i)] = VoxColor{v, v, v, 255};
    }
}

} // namespace

bool VoxLoader::load(const std::filesystem::path& path, VoxFile& outFile,
                     std::string* outError)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return setError(outError, "VoxLoader: failed to open file");
    }

    const std::streamsize size = file.tellg();
    if (size <= 0)
    {
        return setError(outError, "VoxLoader: file is empty");
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        return setError(outError, "VoxLoader: failed to read file");
    }

    const uint8_t* ptr = buffer.data();
    const uint8_t* end = buffer.data() + buffer.size();

    if (static_cast<size_t>(end - ptr) < 8)
    {
        return setError(outError, "VoxLoader: file too small");
    }

    if (std::memcmp(ptr, "VOX ", 4) != 0)
    {
        return setError(outError, "VoxLoader: invalid magic number");
    }
    ptr += 4;

    uint32_t version = 0;
    if (!readU32(ptr, end, version))
    {
        return setError(outError, "VoxLoader: missing version");
    }
    (void)version;

    ChunkHeader mainHeader{};
    if (!readChunkHeader(ptr, end, mainHeader))
    {
        return setError(outError, "VoxLoader: missing MAIN chunk");
    }
    if (!idEquals(mainHeader.id, "MAIN"))
    {
        return setError(outError, "VoxLoader: expected MAIN chunk");
    }

    if (static_cast<size_t>(end - ptr) < mainHeader.contentSize)
    {
        return setError(outError, "VoxLoader: MAIN content truncated");
    }
    ptr += mainHeader.contentSize;

    const uint8_t* mainEnd = ptr + mainHeader.childrenSize;
    if (mainEnd > end)
    {
        return setError(outError, "VoxLoader: MAIN children truncated");
    }

    outFile.models.clear();
    outFile.hasPalette = false;
    fillFallbackPalette(outFile.palette);

    glm::ivec3 pendingSize{0, 0, 0};

    while (ptr + 12 <= mainEnd)
    {
        ChunkHeader chunk{};
        if (!readChunkHeader(ptr, mainEnd, chunk))
        {
            return setError(outError, "VoxLoader: failed reading chunk header");
        }

        const uint8_t* chunkPtr = ptr;
        const uint8_t* chunkEnd = ptr + chunk.contentSize;
        if (chunkEnd > mainEnd)
        {
            return setError(outError, "VoxLoader: chunk extends past MAIN");
        }

        if (idEquals(chunk.id, "SIZE"))
        {
            uint32_t sx = 0;
            uint32_t sy = 0;
            uint32_t sz = 0;
            if (!readU32(chunkPtr, chunkEnd, sx) ||
                !readU32(chunkPtr, chunkEnd, sy) ||
                !readU32(chunkPtr, chunkEnd, sz))
            {
                return setError(outError, "VoxLoader: invalid SIZE chunk");
            }
            if (sx == 0 || sy == 0 || sz == 0)
            {
                return setError(outError, "VoxLoader: SIZE chunk has zero dimension");
            }
            pendingSize = glm::ivec3(static_cast<int>(sx),
                                     static_cast<int>(sy),
                                     static_cast<int>(sz));
        }
        else if (idEquals(chunk.id, "XYZI"))
        {
            if (pendingSize.x <= 0 || pendingSize.y <= 0 || pendingSize.z <= 0)
            {
                return setError(outError, "VoxLoader: XYZI without SIZE");
            }

            uint32_t numVoxels = 0;
            if (!readU32(chunkPtr, chunkEnd, numVoxels))
            {
                return setError(outError, "VoxLoader: invalid XYZI chunk");
            }

            const size_t expectedBytes =
                4ull + static_cast<size_t>(numVoxels) * 4ull;
            if (chunk.contentSize < expectedBytes)
            {
                return setError(outError, "VoxLoader: XYZI chunk truncated");
            }

            // swizzle from MagicaVoxel (x, y, z-up) to engine (x, y-up, z).
            const glm::ivec3 size(pendingSize.x, pendingSize.z, pendingSize.y);

            const uint64_t voxelCount64 =
                static_cast<uint64_t>(size.x) * static_cast<uint64_t>(size.y) *
                static_cast<uint64_t>(size.z);
            if (voxelCount64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
            {
                return setError(outError, "VoxLoader: model size too large");
            }

            VoxModel model{};
            model.size = size;
            model.voxels.resize(static_cast<size_t>(voxelCount64), 0);

            for (uint32_t i = 0; i < numVoxels; ++i)
            {
                if (chunkPtr + 4 > chunkEnd)
                {
                    return setError(outError, "VoxLoader: XYZI data truncated");
                }
                const uint8_t vx = *chunkPtr++;
                const uint8_t vy = *chunkPtr++;
                const uint8_t vz = *chunkPtr++;
                const uint8_t color = *chunkPtr++;

                const int x = static_cast<int>(vx);
                const int y = static_cast<int>(vz);
                const int z = static_cast<int>(vy);

                if (x < 0 || y < 0 || z < 0 ||
                    x >= size.x || y >= size.y || z >= size.z)
                {
                    continue;
                }

                const size_t idx = static_cast<size_t>(x) +
                                   static_cast<size_t>(y) * size.x +
                                   static_cast<size_t>(z) * size.x * size.y;
                model.voxels[idx] = color;
            }

            outFile.models.push_back(std::move(model));
            pendingSize = glm::ivec3(0, 0, 0);
        }
        else if (idEquals(chunk.id, "RGBA"))
        {
            if (chunk.contentSize < 256u * 4u)
            {
                return setError(outError, "VoxLoader: RGBA chunk too small");
            }

            for (int i = 0; i < 256; ++i)
            {
                if (chunkPtr + 4 > chunkEnd)
                {
                    return setError(outError, "VoxLoader: RGBA data truncated");
                }
                const uint8_t r = *chunkPtr++;
                const uint8_t g = *chunkPtr++;
                const uint8_t b = *chunkPtr++;
                const uint8_t a = *chunkPtr++;

                if (i < 255)
                {
                    outFile.palette.colors[static_cast<size_t>(i + 1)] =
                        VoxColor{r, g, b, a};
                }
            }
            outFile.palette.colors[0] = VoxColor{0, 0, 0, 0};
            outFile.hasPalette = true;
        }

        ptr = chunkEnd + chunk.childrenSize;
    }

    if (outFile.models.empty())
    {
        return setError(outError, "VoxLoader: no models found");
    }

    return true;
}

} // namespace engine
