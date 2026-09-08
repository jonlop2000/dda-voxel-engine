// link seam for the cpu-only engine_core_tests target.
//
// the runtime ui font loader (src/UI/Runtime/UiDebugFont.cpp) references
// loadImageRGBA8 from src/Assets/Texture.cpp inside loadFontAtlasImage(). Texture.cpp
// pulls in the vulkan renderer (GpuBuffer/GpuImage/Commands/VulkanContext), which we do
// not want to drag into the fast unit-test executable. the runtime ui unit tests only
// exercise pure text/layout/colour helpers and never decode an atlas image, so we
// resolve that single symbol with a stub instead of linking the renderer.
//
// if a future unit test needs real image decoding, link src/Assets/Texture.cpp (and its
// dependencies) into engine_core_tests and delete this stub.

#include "Assets/Texture.h"

bool loadImageRGBA8(const std::filesystem::path& path, bool srgb, CpuImage& out,
                    std::string* error, bool flipVertically)
{
    (void)path;
    (void)srgb;
    (void)flipVertically;
    out = CpuImage{};
    if (error != nullptr)
    {
        *error = "loadImageRGBA8 is stubbed in engine_core_tests; image decode is not linked.";
    }
    return false;
}
