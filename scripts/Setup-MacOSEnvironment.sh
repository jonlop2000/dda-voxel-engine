#!/usr/bin/env bash

# source this file from a shell. override the toolchain, sdk, and vcpkg roots
# below when your tools are installed elsewhere.
if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
    echo "Source this script instead of executing it:" >&2
    echo "  source scripts/Setup-MacOSEnvironment.sh" >&2
    exit 2
fi

dda_voxel_toolchain_root=${DDA_VOXEL_MACOS_TOOLCHAIN_ROOT:-"${HOME}/.local/share/dda-voxel-engine/toolchain"}
dda_voxel_vulkan_root=${DDA_VOXEL_VULKAN_SDK_ROOT:-"${HOME}/VulkanSDK/1.4.350.1"}
dda_voxel_vcpkg_root=${DDA_VOXEL_VCPKG_ROOT:-${VCPKG_ROOT:-"${HOME}/.local/share/dda-voxel-engine/vcpkg"}}

dda_voxel_cmake_bin="${dda_voxel_toolchain_root}/cmake-4.4.0/CMake.app/Contents/bin"
dda_voxel_ninja_bin="${dda_voxel_toolchain_root}/ninja-1.13.2"
dda_voxel_pkgconf_bin="${dda_voxel_toolchain_root}/pkgconf-3.0.4/bin"

if [[ ! -x ${dda_voxel_cmake_bin}/cmake ]]; then
    echo "dda-voxel-engine CMake installation not found: ${dda_voxel_cmake_bin}" >&2
    return 1
fi
if [[ ! -x ${dda_voxel_ninja_bin}/ninja ]]; then
    echo "dda-voxel-engine Ninja installation not found: ${dda_voxel_ninja_bin}" >&2
    return 1
fi
if [[ ! -x ${dda_voxel_pkgconf_bin}/pkg-config ]]; then
    echo "dda-voxel-engine pkg-config installation not found: ${dda_voxel_pkgconf_bin}" >&2
    return 1
fi
if [[ ! -x ${dda_voxel_vcpkg_root}/vcpkg ]]; then
    echo "dda-voxel-engine vcpkg installation not found: ${dda_voxel_vcpkg_root}" >&2
    return 1
fi
if [[ ! -f ${dda_voxel_vulkan_root}/setup-env.sh ]]; then
    echo "dda-voxel-engine Vulkan SDK installation not found: ${dda_voxel_vulkan_root}" >&2
    return 1
fi

# LunarG's setup script appends to this variable without an unset fallback;
# initialize it so this script is safe to source from `set -u` wrappers.
export PKG_CONFIG_PATH=${PKG_CONFIG_PATH:-}
source "${dda_voxel_vulkan_root}/setup-env.sh"
export VCPKG_ROOT="${dda_voxel_vcpkg_root}"
export VCPKG_DISABLE_METRICS=1
export PATH="${dda_voxel_cmake_bin}:${dda_voxel_ninja_bin}:${dda_voxel_pkgconf_bin}:${VCPKG_ROOT}:${PATH}"

# keep the bring-up deterministic on MoltenVK. the current SDK also contains a
# KosmicKrisp preview whose extension contract differs from the existing engine.
export VK_DRIVER_FILES="${VULKAN_SDK}/share/vulkan/icd.d/MoltenVK_icd.json"
export VK_ICD_FILENAMES="${VK_DRIVER_FILES}"

unset dda_voxel_cmake_bin dda_voxel_ninja_bin dda_voxel_pkgconf_bin dda_voxel_toolchain_root
unset dda_voxel_vcpkg_root dda_voxel_vulkan_root
