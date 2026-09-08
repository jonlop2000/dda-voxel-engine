#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/.." && pwd)

# use the repository's user-local toolchain when the caller has not supplied a
# different environment. Set DDA_VOXEL_MACOS_AUTO_SETUP=0 to disable this behavior.
if [[ ${DDA_VOXEL_MACOS_AUTO_SETUP:-1} != "0" &&
      ( -z ${VCPKG_ROOT:-} || -z ${VULKAN_SDK:-} ) ]]; then
    # shellcheck source=Setup-MacOSEnvironment.sh
    source "${script_dir}/Setup-MacOSEnvironment.sh"
fi

jobs=8
if [[ ${1:-} == "--jobs" ]]; then
    jobs=${2:-}
elif [[ $# -ne 0 ]]; then
    echo "Usage: $0 [--jobs <count>]" >&2
    exit 2
fi

if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
    echo "--jobs must be a positive integer." >&2
    exit 2
fi

if [[ $(uname -s) != "Darwin" ]]; then
    echo "This validation wrapper must run on macOS." >&2
    exit 1
fi

if [[ $(uname -m) != "arm64" ]]; then
    echo "The current presets target Apple Silicon (arm64); found $(uname -m)." >&2
    exit 1
fi

if [[ -z ${VCPKG_ROOT:-} || ! -f ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake ]]; then
    echo "Set VCPKG_ROOT to a vcpkg checkout before running validation." >&2
    exit 1
fi

if [[ -z ${VULKAN_SDK:-} || ! -d ${VULKAN_SDK} ]]; then
    echo "Source the LunarG Vulkan SDK setup-env.sh so VULKAN_SDK and MoltenVK are available." >&2
    exit 1
fi

if [[ -d ${VULKAN_SDK}/bin ]]; then
    export PATH="${VULKAN_SDK}/bin:${PATH}"
fi

for required_command in cmake ninja glslangValidator; do
    if ! command -v "$required_command" >/dev/null 2>&1; then
        echo "Required command not found: $required_command" >&2
        exit 1
    fi
done

cd "$repo_root"

run() {
    echo ">> $*"
    "$@"
}

run cmake --preset mac-ninja-debug-arm64
run cmake --build --preset build-mac-debug-arm64 -j "$jobs"
run ctest --preset test-mac-debug-arm64

run cmake --preset mac-ninja-debug-arm64-noeditor
run cmake --build --preset build-mac-debug-arm64-noeditor -j "$jobs"
run ctest --preset test-mac-debug-arm64-noeditor
