# prevent completed VMA ownership slices from regressing to raw image/buffer
# allocation. this is a source-level guard; runtime tests cover lifecycle behavior.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required (pass -DSOURCE_DIR=<repo-root>)")
endif()

set(VMA_OWNED_SOURCES
    src/engine/render/passes/AOPass.cpp
    src/engine/render/passes/BlueNoiseTexture.cpp
    src/engine/render/passes/LocalLightShadowBuffer.cpp
    src/engine/render/passes/LocalLightTemporalResolve.cpp
    src/engine/render/passes/OBBPass.cpp
    src/engine/render/passes/ShadowBuffer.cpp
    src/engine/render/passes/ShadowDenoisePass.cpp
    src/engine/render/passes/TAAPass.cpp
    src/engine/render/passes/TemporalResolve.cpp
    src/engine/render/voxel/TerrainShadowColumnGpuBuffer.cpp
    src/engine/voxel/VoxelMaterialAtlas.cpp
    src/engine/voxel/VoxelPalette.cpp
    src/engine/voxel/VoxelVolume.cpp)

set(FORBIDDEN_RAW_OWNERSHIP_CALLS
    "vkAllocateMemory"
    "vkFreeMemory"
    "vkBindImageMemory"
    "vkBindBufferMemory"
    "vkCreateImage("
    "vkCreateBuffer("
    "vkDestroyImage("
    "vkDestroyBuffer(")

foreach(relative_path IN LISTS VMA_OWNED_SOURCES)
    set(source_path "${SOURCE_DIR}/${relative_path}")
    if(NOT EXISTS "${source_path}")
        message(FATAL_ERROR "VMA ownership ratchet source is missing: ${relative_path}")
    endif()

    file(READ "${source_path}" source_contents)
    foreach(forbidden_call IN LISTS FORBIDDEN_RAW_OWNERSHIP_CALLS)
        string(FIND "${source_contents}" "${forbidden_call}" call_index)
        if(NOT call_index EQUAL -1)
            message(FATAL_ERROR
                "${relative_path} reintroduced ${forbidden_call}; use the shared VMA/RAII owners")
        endif()
    endforeach()
endforeach()

message(STATUS "VMA ownership ratchet passed for completed migration slices")
