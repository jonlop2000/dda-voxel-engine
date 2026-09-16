# dda voxel engine

C++ voxel engine using vulkan and dda ray traversal. includes water, lighting,
an editor, and an aquarium demo. build presets are included for
windows and apple silicon macs.

## windows

Requires visual studio 2022 with the c++ workload, the vulkan sdk, cmake, ninja,
and vcpkg. set `VCPKG_ROOT` to your vcpkg installation and use the x64 native
tools command prompt for visual studio.

```powershell
cmake --preset win-msvc-ninja-debug
cmake --build --preset build-win-debug
.\build\win-msvc-ninja-debug\voxel_aquarium.exe
```

## macos

Requires an apple silicon mac, xcode command line tools, the vulkan sdk with
moltenvk, cmake, ninja, and vcpkg. set `VCPKG_ROOT` and load the vulkan sdk's
`setup-env.sh` in your shell. make sure cmake and ninja are on `PATH`.

```bash
cmake --preset mac-ninja-debug-arm64
cmake --build --preset build-mac-debug-arm64
./build/mac-ninja-debug-arm64/voxel_aquarium
```

run the executable from the repository root so it can find assets and scenes.

## checks

run the tests after building:

```powershell
ctest --preset test-win-debug --output-on-failure
```

```bash
ctest --preset test-mac-debug-arm64 --output-on-failure
```

the source-size and resource-ownership checks can run without a build:

```bash
cmake -DSOURCE_DIR=. -P tests/StructuralRatchet.cmake
cmake -DSOURCE_DIR=. -P tests/VmaOwnershipRatchet.cmake
```

source-size limits live in `tests/app-size-ceiling.txt`. the scripts in `scripts/`
include validation and performance tools for both platforms.

for the optional macos setup helper, set `DDA_VOXEL_MACOS_TOOLCHAIN_ROOT`,
`DDA_VOXEL_VULKAN_SDK_ROOT`, and `DDA_VOXEL_VCPKG_ROOT` to your installed tools.
`VCPKG_ROOT` is also accepted when the vcpkg override is unset. validation scripts
accept `DDA_VOXEL_MACOS_AUTO_SETUP=0` to use an environment you already configured.
