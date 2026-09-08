#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/.." && pwd)

runs=3
allow_single_run=0
warmup_frames=120
sample_frames=240
window_size="1280x720"
framebuffer_scale="native"
render_quality_preset=""
render_scale="1.0"
shadow_ray_scale="1.0"
ao_ray_scale="1.0"
render_scale_explicit=0
shadow_ray_scale_explicit=0
ao_ray_scale_explicit=0
adaptive_auxiliary_rays=0
scene_csv="fishbowl_perf_probe,aquarium_test_perf_probe,nature_pond_probe,nature_pond_bank_probe,beach_sand_perf_probe"
variance_threshold=5
jobs=8
no_editor=0
skip_build=0
disable_empty_skip=0
disable_aligned_primary_traversal=0
foliage_renderer="voxel"
foliage_palette="pastel-light-v4"
disable_foliage_sway=0
live_animation=0
shadow_ray_audit=0
terrain_shadow_columns=0
terrain_shadow_column_parity=0
sun_shadow_sampling=""
fixed_scene_time_seconds=""
ao_projected_radius_px=""
ao_projected_min_distance_m="2.0"
output_root=""

usage() {
    echo "Usage: $0 [options]"
    echo "  --runs N                  Repetitions per scene (default: 3)"
    echo "  --allow-single-run         Permit one run for an outer interleaved bracket"
    echo "  --warmup-frames N         Resolved frames discarded after readiness (default: 120)"
    echo "  --sample-frames N         Resolved frames measured per run (default: 240)"
    echo "  --window-size WIDTHxHEIGHT  Logical window size (default: 1280x720)"
    echo "  --framebuffer-scale native|1x  Retina or diagnostic 1x output"
    echo "  --render-quality-preset native|quality|performance"
    echo "                                Coordinated scene and auxiliary-ray scales"
    echo "                                Explicit scale options override preset values"
    echo "  --render-scale SCALE       Internal scene scale from 0.50 to 1.00 (default: 1.0)"
    echo "  --shadow-ray-scale SCALE   DDA sun-shadow ray scale from 0.50 to 1.00 (default: 1.0)"
    echo "  --ao-ray-scale SCALE       AO ray scale from 0.50 to 1.00 (default: 1.0)"
    echo "  --adaptive-auxiliary-rays  Leave shadow/AO scales unforced so coverage adaptation remains enabled"
    echo "  --scenes NAME[,NAME...]   Scene matrix override"
    echo "  --variance-threshold PCT  Maximum run-median deviation (default: 5)"
    echo "  --no-editor               Use the no-editor RelWithDebInfo preset"
    echo "  --disable-empty-skip      Run the legacy primary-DDA reference path"
    echo "  --disable-aligned-primary-traversal  Disable shared DDA traversal for control measurements"
    echo "  --foliage-renderer voxel|instanced  Select the nature-pond foliage path"
    echo "  --foliage-palette raw|soft-value-v1|botanical-depth-v2|meadow-volume-v3|pastel-light-v4"
    echo "                                Select the instanced foliage color response"
    echo "  --disable-foliage-sway  Measure static instanced foliage"
    echo "  --live-animation        Freeze only the camera; keep animation and TAA jitter live"
    echo "  --shadow-ray-audit        Record profiler-only per-volume Shadow Rays scopes"
    echo "  --terrain-shadow-columns  Enable classifier-approved terrain height traversal"
    echo "  --terrain-shadow-column-parity  Compare raw terrain results against 3D DDA"
    echo "  --sun-shadow-sampling full|temporal-2|temporal-2-uncompensated"
    echo "                                Automation-only sun-cone sampling route"
    echo "  --fixed-scene-time-seconds S  Freeze animation at an exact non-negative time"
    echo "  --ao-projected-radius-px PX  Override the projected AO radius for an isolated benchmark"
    echo "  --ao-projected-min-distance-m M  Minimum AO trace reach for that tier (default: 2.0)"
    echo "  --jobs N                  Parallel build jobs (default: 8)"
    echo "  --skip-build              Reuse the existing executable"
    echo "  --output DIR              Evidence directory override"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --runs)
        runs=${2:-}
        shift 2
        ;;
    --allow-single-run)
        allow_single_run=1
        shift
        ;;
    --warmup-frames)
        warmup_frames=${2:-}
        shift 2
        ;;
    --sample-frames)
        sample_frames=${2:-}
        shift 2
        ;;
    --window-size)
        window_size=${2:-}
        shift 2
        ;;
    --framebuffer-scale)
        framebuffer_scale=${2:-}
        shift 2
        ;;
    --render-quality-preset)
        render_quality_preset=${2:-}
        shift 2
        ;;
    --render-scale)
        render_scale=${2:-}
        render_scale_explicit=1
        shift 2
        ;;
    --shadow-ray-scale)
        shadow_ray_scale=${2:-}
        shadow_ray_scale_explicit=1
        shift 2
        ;;
    --ao-ray-scale)
        ao_ray_scale=${2:-}
        ao_ray_scale_explicit=1
        shift 2
        ;;
    --adaptive-auxiliary-rays)
        adaptive_auxiliary_rays=1
        shift
        ;;
    --scenes)
        scene_csv=${2:-}
        shift 2
        ;;
    --variance-threshold)
        variance_threshold=${2:-}
        shift 2
        ;;
    --no-editor)
        no_editor=1
        shift
        ;;
    --disable-empty-skip)
        disable_empty_skip=1
        shift
        ;;
    --disable-aligned-primary-traversal)
        disable_aligned_primary_traversal=1
        shift
        ;;
    --foliage-renderer)
        foliage_renderer=${2:-}
        shift 2
        ;;
    --foliage-palette)
        foliage_palette=${2:-}
        shift 2
        ;;
    --disable-foliage-sway)
        disable_foliage_sway=1
        shift
        ;;
    --live-animation)
        live_animation=1
        shift
        ;;
    --shadow-ray-audit)
        shadow_ray_audit=1
        shift
        ;;
    --terrain-shadow-columns)
        terrain_shadow_columns=1
        shift
        ;;
    --terrain-shadow-column-parity)
        terrain_shadow_column_parity=1
        shift
        ;;
    --sun-shadow-sampling)
        sun_shadow_sampling=${2:-}
        shift 2
        ;;
    --fixed-scene-time-seconds)
        fixed_scene_time_seconds=${2:-}
        shift 2
        ;;
    --ao-projected-radius-px)
        ao_projected_radius_px=${2:-}
        shift 2
        ;;
    --ao-projected-min-distance-m)
        ao_projected_min_distance_m=${2:-}
        shift 2
        ;;
    --jobs)
        jobs=${2:-}
        shift 2
        ;;
    --skip-build)
        skip_build=1
        shift
        ;;
    --output)
        output_root=${2:-}
        shift 2
        ;;
    --help|-h)
        usage
        exit 0
        ;;
    *)
        echo "Unknown option: $1" >&2
        usage >&2
        exit 2
        ;;
    esac
done

for value in "$runs" "$warmup_frames" "$sample_frames" "$jobs"; do
    if [[ ! $value =~ ^[0-9]+$ ]]; then
        echo "Run, frame, and job counts must be non-negative integers." >&2
        exit 2
    fi
done
if [[ $runs -lt 3 && ! ( $allow_single_run -eq 1 && $runs -eq 1 ) ]]; then
    echo "--runs must be at least 3, or exactly 1 with --allow-single-run." >&2
    exit 2
fi
if [[ $sample_frames -lt 1 || $jobs -lt 1 ]]; then
    echo "--sample-frames and --jobs must be positive." >&2
    exit 2
fi
if [[ $terrain_shadow_columns -eq 1 &&
      $terrain_shadow_column_parity -eq 1 ]]; then
    echo "Terrain shadow-column execution and parity modes are mutually exclusive." >&2
    exit 2
fi
if [[ ! $window_size =~ ^[0-9]+x[0-9]+$ ]]; then
    echo "--window-size must use WIDTHxHEIGHT syntax." >&2
    exit 2
fi
if [[ $framebuffer_scale != "native" && $framebuffer_scale != "1x" ]]; then
    echo "--framebuffer-scale must be native or 1x." >&2
    exit 2
fi
if [[ -n $render_quality_preset &&
      $render_quality_preset != "native" &&
      $render_quality_preset != "quality" &&
      $render_quality_preset != "performance" ]]; then
    echo "--render-quality-preset must be native, quality, or performance." >&2
    exit 2
fi
if [[ -n $render_quality_preset ]]; then
    preset_render_scale="1.0"
    preset_shadow_ray_scale="1.0"
    preset_ao_ray_scale="1.0"
    case "$render_quality_preset" in
    quality)
        preset_render_scale="0.75"
        preset_shadow_ray_scale="0.5"
        preset_ao_ray_scale="0.5"
        ;;
    performance)
        preset_render_scale="0.6"
        preset_shadow_ray_scale="0.5"
        preset_ao_ray_scale="0.5"
        ;;
    esac
    if [[ $render_scale_explicit -eq 0 ]]; then
        render_scale=$preset_render_scale
    fi
    if [[ $shadow_ray_scale_explicit -eq 0 ]]; then
        shadow_ray_scale=$preset_shadow_ray_scale
    fi
    if [[ $ao_ray_scale_explicit -eq 0 ]]; then
        ao_ray_scale=$preset_ao_ray_scale
    fi
fi
if [[ $adaptive_auxiliary_rays -eq 1 && -n $render_quality_preset ]]; then
    echo "--adaptive-auxiliary-rays cannot be combined with --render-quality-preset." >&2
    exit 2
fi
if [[ $adaptive_auxiliary_rays -eq 1 &&
      ( $shadow_ray_scale_explicit -eq 1 || $ao_ray_scale_explicit -eq 1 ) ]]; then
    echo "--adaptive-auxiliary-rays cannot be combined with explicit auxiliary ray scales." >&2
    exit 2
fi
if [[ -n $sun_shadow_sampling &&
      $sun_shadow_sampling != "full" &&
      $sun_shadow_sampling != "temporal-2" &&
      $sun_shadow_sampling != "temporal-2-uncompensated" ]]; then
    echo "--sun-shadow-sampling must be full, temporal-2, or temporal-2-uncompensated." >&2
    exit 2
fi
if [[ -n $fixed_scene_time_seconds &&
      ! $fixed_scene_time_seconds =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "--fixed-scene-time-seconds must be a non-negative number." >&2
    exit 2
fi
if [[ -n $fixed_scene_time_seconds && $live_animation -eq 1 ]]; then
    echo "--fixed-scene-time-seconds cannot be combined with --live-animation." >&2
    exit 2
fi
if [[ $foliage_renderer != "voxel" && $foliage_renderer != "instanced" ]]; then
    echo "--foliage-renderer must be voxel or instanced." >&2
    exit 2
fi
if [[ $foliage_palette != "raw" && $foliage_palette != "soft-value-v1" &&
      $foliage_palette != "botanical-depth-v2" &&
      $foliage_palette != "meadow-volume-v3" &&
      $foliage_palette != "pastel-light-v4" ]]; then
    echo "--foliage-palette must be raw, soft-value-v1, botanical-depth-v2, meadow-volume-v3, or pastel-light-v4." >&2
    exit 2
fi
if [[ ! $render_scale =~ ^[0-9]+([.][0-9]+)?$ ]] ||
   awk -v scale="$render_scale" 'BEGIN { exit !((scale < 0.5) || (scale > 1.0)) }'; then
    echo "--render-scale must be between 0.50 and 1.00." >&2
    exit 2
fi
for auxiliary_scale in "$shadow_ray_scale" "$ao_ray_scale"; do
    if [[ ! $auxiliary_scale =~ ^[0-9]+([.][0-9]+)?$ ]] ||
       awk -v scale="$auxiliary_scale" 'BEGIN { exit !((scale < 0.5) || (scale > 1.0)) }'; then
        echo "Auxiliary ray scales must be between 0.50 and 1.00." >&2
        exit 2
    fi
done
if [[ ! $variance_threshold =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "--variance-threshold must be a non-negative number." >&2
    exit 2
fi
if [[ -n $ao_projected_radius_px &&
      ! $ao_projected_radius_px =~ ^[0-9]+([.][0-9]+)?$ ]] ||
   [[ ! $ao_projected_min_distance_m =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "Projected AO radius and minimum distance must be non-negative numbers." >&2
    exit 2
fi
if [[ -z $ao_projected_radius_px && $ao_projected_min_distance_m != "2.0" ]]; then
    echo "--ao-projected-min-distance-m requires --ao-projected-radius-px." >&2
    exit 2
fi

if [[ ${DDA_VOXEL_MACOS_AUTO_SETUP:-1} != "0" &&
      ( -z ${VCPKG_ROOT:-} || -z ${VULKAN_SDK:-} ) ]]; then
    # shellcheck source=Setup-MacOSEnvironment.sh
    source "${script_dir}/Setup-MacOSEnvironment.sh"
fi

if [[ $(uname -s) != "Darwin" || $(uname -m) != "arm64" ]]; then
    echo "This runner targets native Apple Silicon macOS." >&2
    exit 1
fi

configure_preset="mac-ninja-relwithdebinfo-arm64"
build_preset="build-mac-relwithdebinfo-arm64"
build_dir="${repo_root}/build/mac-ninja-relwithdebinfo-arm64"
editor_label="editor"
if [[ $no_editor -eq 1 ]]; then
    configure_preset="mac-ninja-relwithdebinfo-arm64-noeditor"
    build_preset="build-mac-relwithdebinfo-arm64-noeditor"
    build_dir="${repo_root}/build/mac-ninja-relwithdebinfo-arm64-noeditor"
    editor_label="no-editor"
fi

if [[ $skip_build -eq 0 ]]; then
    cmake --preset "$configure_preset"
    cmake --build --preset "$build_preset" --target voxel_aquarium -j "$jobs"
fi

executable="${build_dir}/voxel_aquarium"
if [[ ! -x $executable ]]; then
    echo "Executable not found: $executable" >&2
    exit 1
fi
executable_sha256=$(shasum -a 256 "$executable" | awk '{ print $1 }')
source_state_sha256=$(
    {
        git -C "$repo_root" rev-parse HEAD
        git -C "$repo_root" diff HEAD --binary --no-ext-diff --
        git -C "$repo_root" ls-files --others --exclude-standard |
            LC_ALL=C sort |
            while IFS= read -r untracked_file; do
                printf '%s\n' "$untracked_file"
                shasum -a 256 "${repo_root}/${untracked_file}"
            done
    } | shasum -a 256 | awk '{ print $1 }'
)
primary_dda_shader="${build_dir}/shaders/obb_dda_hot.frag.spv"
if [[ ! -f $primary_dda_shader ]]; then
    echo "Primary DDA shader not found: $primary_dda_shader" >&2
    exit 1
fi
primary_dda_shader_sha256=$(shasum -a 256 "$primary_dda_shader" | awk '{ print $1 }')
opaque_dda_shader="${build_dir}/shaders/obb_dda_opaque.frag.spv"
if [[ ! -f $opaque_dda_shader ]]; then
    echo "Opaque-only DDA shader not found: $opaque_dda_shader" >&2
    exit 1
fi
opaque_dda_shader_sha256=$(shasum -a 256 "$opaque_dda_shader" | awk '{ print $1 }')
unwrapped_opaque_dda_shader="${build_dir}/shaders/obb_dda_unwrapped_opaque.frag.spv"
if [[ ! -f $unwrapped_opaque_dda_shader ]]; then
    echo "Unwrapped opaque DDA shader not found: $unwrapped_opaque_dda_shader" >&2
    exit 1
fi
unwrapped_opaque_dda_shader_sha256=$(
    shasum -a 256 "$unwrapped_opaque_dda_shader" | awk '{ print $1 }'
)
shared_aligned_opaque_dda_shader="${build_dir}/shaders/obb_dda_shared_aligned_opaque.frag.spv"
if [[ ! -f $shared_aligned_opaque_dda_shader ]]; then
    echo "Shared aligned opaque DDA shader not found: $shared_aligned_opaque_dda_shader" >&2
    exit 1
fi
shared_aligned_opaque_dda_shader_sha256=$(
    shasum -a 256 "$shared_aligned_opaque_dda_shader" | awk '{ print $1 }'
)
shared_aligned_near_clip_dda_shader="${build_dir}/shaders/obb_dda_shared_aligned_near_clip.frag.spv"
if [[ ! -f $shared_aligned_near_clip_dda_shader ]]; then
    echo "Shared aligned near-clip DDA shader not found: $shared_aligned_near_clip_dda_shader" >&2
    exit 1
fi
shared_aligned_near_clip_dda_shader_sha256=$(
    shasum -a 256 "$shared_aligned_near_clip_dda_shader" | awk '{ print $1 }'
)
diagnostic_dda_shader="${build_dir}/shaders/obb_dda.frag.spv"
if [[ ! -f $diagnostic_dda_shader ]]; then
    echo "Diagnostic DDA shader not found: $diagnostic_dda_shader" >&2
    exit 1
fi
diagnostic_dda_shader_sha256=$(shasum -a 256 "$diagnostic_dda_shader" | awk '{ print $1 }')
shadow_ray_shader="${build_dir}/shaders/shadow_ray.comp.spv"
temporal_resolve_shader="${build_dir}/shaders/temporal_resolve.comp.spv"
if [[ ! -f $shadow_ray_shader || ! -f $temporal_resolve_shader ]]; then
    echo "Sun-shadow and production temporal-resolve shaders are required." >&2
    exit 1
fi
shadow_ray_shader_sha256=$(shasum -a 256 "$shadow_ray_shader" | awk '{ print $1 }')
temporal_resolve_shader_sha256=$(shasum -a 256 "$temporal_resolve_shader" | awk '{ print $1 }')
foliage_vertex_shader="${build_dir}/shaders/foliage.vert.spv"
foliage_fragment_shader="${build_dir}/shaders/foliage.frag.spv"
foliage_vertex_shader_sha256="unavailable"
foliage_fragment_shader_sha256="unavailable"
if [[ -f $foliage_vertex_shader ]]; then
    foliage_vertex_shader_sha256=$(shasum -a 256 "$foliage_vertex_shader" | awk '{ print $1 }')
fi
if [[ -f $foliage_fragment_shader ]]; then
    foliage_fragment_shader_sha256=$(shasum -a 256 "$foliage_fragment_shader" | awk '{ print $1 }')
fi
if [[ $foliage_renderer == "instanced" &&
      ( $foliage_vertex_shader_sha256 == "unavailable" ||
        $foliage_fragment_shader_sha256 == "unavailable" ) ]]; then
    echo "Instanced foliage shaders are required for this benchmark." >&2
    exit 1
fi

if [[ -z $output_root ]]; then
    timestamp=$(date -u +"%Y%m%dT%H%M%SZ")
    output_root="${repo_root}/build/performance/macos-${timestamp}-${editor_label}-${framebuffer_scale}"
elif [[ $output_root != /* ]]; then
    output_root="${repo_root}/${output_root}"
fi
mkdir -p "$output_root"

IFS=',' read -r -a scenes <<< "$scene_csv"
if [[ ${#scenes[@]} -eq 0 ]]; then
    echo "At least one scene is required." >&2
    exit 2
fi

git_commit=$(git -C "$repo_root" rev-parse HEAD)
working_tree_dirty=0
if [[ -n $(git -C "$repo_root" status --porcelain) ]]; then
    working_tree_dirty=1
fi
os_version=$(sw_vers -productVersion)
hardware_model=$(sysctl -n hw.model)
cpu_brand=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo "Apple Silicon")
power_source=$(pmset -g batt | sed -n "1s/Now drawing from '\(.*\)'/\1/p")
low_power_mode=$(pmset -g | awk '$1 == "lowpowermode" { print $2; exit }')
power_source=${power_source:-unavailable}
low_power_mode=${low_power_mode:-unavailable}
system_profile_json=$(system_profiler SPHardwareDataType SPDisplaysDataType -json)
profile_value() {
    local key=$1
    local value
    value=$(plutil -extract "$key" raw -o - - <<< "$system_profile_json" 2>/dev/null || true)
    if [[ -z $value ]]; then
        value="unavailable"
    fi
    printf '%s' "$value"
}
machine_name=$(profile_value "SPHardwareDataType.0.machine_name")
chip_type=$(profile_value "SPHardwareDataType.0.chip_type")
physical_memory=$(profile_value "SPHardwareDataType.0.physical_memory")
gpu_model=$(profile_value "SPDisplaysDataType.0.sppci_model")
gpu_cores=$(profile_value "SPDisplaysDataType.0.sppci_cores")
vulkan_sdk_version="unavailable"
if [[ -n ${VULKAN_SDK:-} ]]; then
    vulkan_sdk_version=$(basename "$VULKAN_SDK")
    if [[ $vulkan_sdk_version == "macOS" ]]; then
        vulkan_sdk_version=$(basename "$(dirname "$VULKAN_SDK")")
    fi
fi

{
    echo "generated_at=$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
    echo "git_commit=${git_commit}"
    echo "working_tree_dirty=${working_tree_dirty}"
    echo "source_state_sha256=${source_state_sha256}"
    echo "executable_sha256=${executable_sha256}"
    echo "primary_dda_shader_sha256=${primary_dda_shader_sha256}"
    echo "opaque_dda_shader_sha256=${opaque_dda_shader_sha256}"
    echo "unwrapped_opaque_dda_shader_sha256=${unwrapped_opaque_dda_shader_sha256}"
    echo "shared_aligned_opaque_dda_shader_sha256=${shared_aligned_opaque_dda_shader_sha256}"
    echo "shared_aligned_near_clip_dda_shader_sha256=${shared_aligned_near_clip_dda_shader_sha256}"
    echo "diagnostic_dda_shader_sha256=${diagnostic_dda_shader_sha256}"
    echo "shadow_ray_shader_sha256=${shadow_ray_shader_sha256}"
    echo "temporal_resolve_shader_sha256=${temporal_resolve_shader_sha256}"
    echo "foliage_vertex_shader_sha256=${foliage_vertex_shader_sha256}"
    echo "foliage_fragment_shader_sha256=${foliage_fragment_shader_sha256}"
    echo "os=macOS ${os_version}"
    echo "architecture=$(uname -m)"
    echo "hardware_model=${hardware_model}"
    echo "cpu=${cpu_brand}"
    echo "machine_name=${machine_name}"
    echo "chip=${chip_type}"
    echo "physical_memory=${physical_memory}"
    echo "gpu=${gpu_model}"
    echo "gpu_cores=${gpu_cores}"
    echo "power_source=${power_source}"
    echo "low_power_mode=${low_power_mode}"
    echo "vulkan_sdk=${vulkan_sdk_version}"
    echo "build=RelWithDebInfo ${editor_label}"
    echo "window_size=${window_size}"
    echo "framebuffer_scale=${framebuffer_scale}"
    echo "render_quality_preset=${render_quality_preset:-none}"
    echo "render_scale=${render_scale}"
    echo "shadow_ray_scale=${shadow_ray_scale}"
    echo "ao_ray_scale=${ao_ray_scale}"
    echo "adaptive_auxiliary_rays=${adaptive_auxiliary_rays}"
    echo "runs=${runs}"
    echo "warmup_frames=${warmup_frames}"
    echo "sample_frames=${sample_frames}"
    echo "variance_threshold_percent=${variance_threshold}"
    echo "empty_skip_enabled=$((1 - disable_empty_skip))"
    echo "aligned_primary_traversal=$((1 - disable_aligned_primary_traversal))"
    echo "foliage_renderer=${foliage_renderer}"
    echo "instanced_foliage_topology_contract=foliage-meadow-volume-v10"
    echo "instanced_foliage_vertices_per_primitive=36"
    echo "foliage_palette=${foliage_palette}"
    echo "foliage_sway_enabled=$((1 - disable_foliage_sway))"
    echo "live_animation=${live_animation}"
    echo "shadow_ray_audit=${shadow_ray_audit}"
    echo "sun_shadow_sampling=${sun_shadow_sampling:-full}"
    echo "fixed_scene_time_seconds=${fixed_scene_time_seconds:-startup-snapshot}"
    echo "ao_projected_radius_px=${ao_projected_radius_px:-disabled}"
    echo "ao_projected_min_distance_m=${ao_projected_min_distance_m}"
    echo "scenes=${scene_csv}"
} > "${output_root}/metadata.txt"

run_csv="${output_root}/runs.csv"
summary_csv="${output_root}/summary.csv"
echo "scene,run,avg_cpu_ms,median_cpu_ms,p95_cpu_ms,p99_cpu_ms,avg_gpu_ms,median_gpu_ms,p95_gpu_ms,p99_gpu_ms,sampled_frames,swapchain,render_target,foliage_renderer,foliage_topology,foliage_palette,foliage_instances,foliage_patches,foliage_primitives,foliage_vertices_per_primitive,foliage_submitted_vertices,foliage_active_gpu_bytes,foliage_resident_gpu_bytes,windborne_particles,windborne_resident_particles,windborne_submitted_vertices,windborne_resident_gpu_bytes,total_foliage_submitted_vertices,total_foliage_resident_gpu_bytes" > "$run_csv"
echo "scene,runs,cpu_median_of_medians_ms,cpu_max_deviation_percent,gpu_median_of_medians_ms,gpu_max_deviation_percent,variance_status" > "$summary_csv"

extract_number() {
    local line=$1
    local expression=$2
    printf '%s\n' "$line" | sed -E "$expression"
}

median_values() {
    sort -n | awk '
        { values[NR] = $1 }
        END {
            if (NR % 2 == 1) {
                printf "%.6f", values[(NR + 1) / 2]
            } else {
                printf "%.6f", (values[NR / 2] + values[NR / 2 + 1]) / 2.0
            }
        }'
}

max_deviation_percent() {
    local median=$1
    awk -v median="$median" '
        BEGIN { maxDeviation = 0.0 }
        {
            deviation = $1 - median
            if (deviation < 0.0) {
                deviation = -deviation
            }
            if (deviation > maxDeviation) {
                maxDeviation = deviation
            }
        }
        END {
            if (median > 0.0) {
                printf "%.3f", maxDeviation * 100.0 / median
            } else {
                printf "0.000"
            }
        }'
}

for scene in "${scenes[@]}"; do
    if [[ -z $scene ]]; then
        echo "Scene names must not be empty." >&2
        exit 2
    fi

    cpu_medians=()
    gpu_medians=()
    for ((run_index = 1; run_index <= runs; ++run_index)); do
        run_dir="${output_root}/${scene}/run-${run_index}"
        mkdir -p "$run_dir"
        stdout_path="${run_dir}/stdout.txt"
        stderr_path="${run_dir}/stderr.txt"
        runtime_log="${build_dir}/logs/voxel_aquarium.log"
        runtime_log_start_lines=0
        if [[ -f $runtime_log ]]; then
            runtime_log_start_lines=$(wc -l < "$runtime_log")
        fi

        command=(
            "$executable"
            "--no-persist-last-used"
            "--assets" "${repo_root}/assets"
            "--scenes" "${repo_root}/scenes"
            "--scene" "$scene"
            "--automation-window-size" "$window_size"
            "--automation-log-gpu-profile"
            "--automation-hide-ui"
            "--automation-foliage-renderer" "$foliage_renderer"
            "--automation-foliage-palette" "$foliage_palette"
            "--automation-profile-wait-for-scene-ready"
            "--automation-profile-warmup-frames" "$warmup_frames"
            "--automation-profile-sample-frames" "$sample_frames"
            "--auto-exit-frames" "$sample_frames"
        )
        if [[ -n $render_quality_preset ]]; then
            command+=("--automation-render-quality-preset" "$render_quality_preset")
        fi
        if [[ -z $render_quality_preset || $render_scale_explicit -eq 1 ]]; then
            command+=("--automation-render-scale" "$render_scale")
        fi
        if [[ $adaptive_auxiliary_rays -eq 0 &&
              ( -z $render_quality_preset || $shadow_ray_scale_explicit -eq 1 ) ]]; then
            command+=("--automation-shadow-ray-scale" "$shadow_ray_scale")
        fi
        if [[ $adaptive_auxiliary_rays -eq 0 &&
              ( -z $render_quality_preset || $ao_ray_scale_explicit -eq 1 ) ]]; then
            command+=("--automation-ao-ray-scale" "$ao_ray_scale")
        fi
        if [[ $live_animation -eq 1 ]]; then
            command+=("--automation-freeze-camera")
        else
            command+=("--automation-freeze-scene")
        fi
        if [[ $disable_foliage_sway -eq 1 ]]; then
            command+=("--automation-disable-foliage-sway")
        fi
        if [[ $framebuffer_scale == "1x" ]]; then
            command+=("--automation-framebuffer-scale-1x")
        fi
        if [[ $disable_empty_skip -eq 1 ]]; then
            command+=("--automation-disable-empty-skip")
        fi
        if [[ $disable_aligned_primary_traversal -eq 1 ]]; then
            command+=("--automation-disable-aligned-primary-traversal")
        fi
        if [[ $shadow_ray_audit -eq 1 ]]; then
            command+=("--automation-shadow-ray-audit")
        fi
        if [[ $terrain_shadow_columns -eq 1 ]]; then
            command+=("--automation-terrain-shadow-columns")
        fi
        if [[ $terrain_shadow_column_parity -eq 1 ]]; then
            command+=("--automation-terrain-shadow-column-parity")
        fi
        if [[ -n $sun_shadow_sampling ]]; then
            command+=("--automation-sun-shadow-sampling" "$sun_shadow_sampling")
        fi
        if [[ -n $fixed_scene_time_seconds ]]; then
            command+=("--automation-fixed-scene-time-seconds" "$fixed_scene_time_seconds")
        fi
        if [[ -n $ao_projected_radius_px ]]; then
            command+=("--automation-ao-projected-radius-px" "$ao_projected_radius_px")
            command+=("--automation-ao-projected-min-distance-m" "$ao_projected_min_distance_m")
        fi

        echo "Profiling ${scene} run ${run_index}/${runs}..."
        (
            cd "$build_dir"
            "${command[@]}" > "$stdout_path" 2> "$stderr_path"
        )

        if [[ ! -f $runtime_log ]]; then
            echo "Runtime log not found after ${scene} run ${run_index}." >&2
            exit 1
        fi
        runtime_log_first_new_line=$((runtime_log_start_lines + 1))
        tail -n +"$runtime_log_first_new_line" "$runtime_log" \
            > "${run_dir}/voxel_aquarium.log"

        if rg -n "Validation Error:|VUID-|VMA ASSERT" \
            "$stdout_path" "$stderr_path" "${run_dir}/voxel_aquarium.log" >/dev/null; then
            echo "Validation output detected in ${scene} run ${run_index}." >&2
            exit 1
        fi

        # stdout is process-local; voxel_aquarium.log is cumulative across launches.
        # parse the isolated stream so repeated runs cannot reuse an earlier result.
        log_path="$stdout_path"
        summary_line=$(rg -m1 "GPU profile summary for scene" "$log_path")
        statistics_line=$(rg -m1 "GPU profile statistics:" "$log_path")
        metadata_line=$(rg -m1 "GPU profile metadata:" "$log_path")
        printf '%s\n' "$metadata_line" > "${run_dir}/profile-metadata.txt"
        effects_line=$(rg -m1 "GPU profile effects:" "$log_path")
        printf '%s\n' "$effects_line" > "${run_dir}/effects.txt"
        if [[ $terrain_shadow_column_parity -eq 1 ]]; then
            parity_line=$(rg -m1 "Terrain shadow-column raw parity:" "$log_path")
            printf '%s\n' "$parity_line" > "${run_dir}/terrain-shadow-column-parity.txt"
            parity_compared=$(extract_number \
                "$parity_line" 's/.*comparedRays=([0-9]+).*/\1/')
            parity_mismatched=$(extract_number \
                "$parity_line" 's/.*mismatchedRays=([0-9]+).*/\1/')
            if [[ ! $parity_compared =~ ^[0-9]+$ ||
                  ! $parity_mismatched =~ ^[0-9]+$ ||
                  $parity_compared -eq 0 || $parity_mismatched -ne 0 ]]; then
                echo "Terrain shadow-column parity failed for ${scene} run ${run_index}: ${parity_line}" >&2
                exit 1
            fi
        fi
        decision_line=$(rg -m1 "Decision GPU passes:" "$log_path")
        printf '%s\n' "$decision_line" > "${run_dir}/decision-passes.txt"

        foliage_run_renderer=$(extract_number \
            "$metadata_line" 's/.*foliageRenderer=([^, ]+).*/\1/')
        foliage_topology=$(extract_number \
            "$metadata_line" 's/.*foliageTopology=([^, ]+).*/\1/')
        foliage_run_palette=$(extract_number \
            "$metadata_line" 's/.*foliagePalette=([^, ]+).*/\1/')
        foliage_instances=$(extract_number \
            "$metadata_line" 's/.*foliageInstances=([0-9]+).*/\1/')
        foliage_patches=$(extract_number \
            "$metadata_line" 's/.*foliagePatches=([0-9]+).*/\1/')
        foliage_primitives=$(extract_number \
            "$metadata_line" 's/.*foliagePrimitives=([0-9]+).*/\1/')
        foliage_vertices_per_primitive=$(extract_number \
            "$metadata_line" 's/.*foliageVerticesPerPrimitive=([0-9]+).*/\1/')
        foliage_submitted_vertices=$(extract_number \
            "$metadata_line" 's/.*foliageSubmittedVertices=([0-9]+).*/\1/')
        foliage_active_gpu_bytes=$(extract_number \
            "$metadata_line" 's/.*foliageGpuBytes=([0-9]+).*/\1/')
        foliage_resident_gpu_bytes=$(extract_number \
            "$metadata_line" 's/.*foliageResidentGpuBytes=([0-9]+).*/\1/')
        windborne_particles=$(extract_number \
            "$metadata_line" 's/.*windborneParticles=([0-9]+).*/\1/')
        windborne_resident_particles=$(extract_number \
            "$metadata_line" 's/.*windborneResidentParticles=([0-9]+).*/\1/')
        windborne_submitted_vertices=$(extract_number \
            "$metadata_line" 's/.*windborneSubmittedVertices=([0-9]+).*/\1/')
        windborne_resident_gpu_bytes=$(extract_number \
            "$metadata_line" 's/.*windborneResidentGpuBytes=([0-9]+).*/\1/')

        if [[ ! $foliage_instances =~ ^[0-9]+$ ||
              ! $foliage_patches =~ ^[0-9]+$ ||
              ! $foliage_primitives =~ ^[0-9]+$ ||
              ! $foliage_vertices_per_primitive =~ ^[0-9]+$ ||
              ! $foliage_submitted_vertices =~ ^[0-9]+$ ||
              ! $foliage_active_gpu_bytes =~ ^[0-9]+$ ||
              ! $foliage_resident_gpu_bytes =~ ^[0-9]+$ ||
              ! $windborne_particles =~ ^[0-9]+$ ||
              ! $windborne_resident_particles =~ ^[0-9]+$ ||
              ! $windborne_submitted_vertices =~ ^[0-9]+$ ||
              ! $windborne_resident_gpu_bytes =~ ^[0-9]+$ ]]; then
            echo "Foliage topology telemetry was missing for ${scene} run ${run_index}." >&2
            exit 1
        fi

        expected_windborne_submitted_vertices=$((
            windborne_particles * foliage_vertices_per_primitive))
        expected_windborne_resident_gpu_bytes=$((
            windborne_resident_particles * 64))
        if [[ $windborne_particles -gt $windborne_resident_particles ||
              $windborne_submitted_vertices -ne $expected_windborne_submitted_vertices ||
              $windborne_resident_gpu_bytes -ne $expected_windborne_resident_gpu_bytes ]]; then
            echo "Windborne-particle telemetry was inconsistent for ${scene} run ${run_index}." >&2
            exit 1
        fi
        total_foliage_submitted_vertices=$((
            foliage_submitted_vertices + windborne_submitted_vertices))
        total_foliage_resident_gpu_bytes=$((
            foliage_resident_gpu_bytes + windborne_resident_gpu_bytes))

        if [[ $scene == nature_pond* &&
              $effects_line != *"paintedClouds=1"* ]]; then
            echo "CLOUD-001 was not active for ${scene} run ${run_index}." >&2
            exit 1
        fi
        if [[ $scene == "nature_pond_probe" &&
              $effects_line != *"environmentTime=1"* ]]; then
            echo "TIME-001 was not active for ${scene} run ${run_index}." >&2
            exit 1
        fi

        if [[ $foliage_renderer == "instanced" && $scene == nature_pond* ]]; then
            expected_foliage_submitted_vertices=$((
                foliage_primitives * foliage_vertices_per_primitive))
            expected_windborne_particles=0
            if [[ $scene == "nature_pond_probe" ]]; then
                expected_windborne_particles=42
            fi
            if [[ $foliage_run_renderer != "instanced" ||
                  $foliage_topology != "foliage-meadow-volume-v10" ||
                  $foliage_run_palette != "$foliage_palette" ||
                  $foliage_vertices_per_primitive -ne 36 ||
                  $foliage_instances -le 0 ||
                  $foliage_patches -le 0 ||
                  $foliage_patches -ge $foliage_instances ||
                  $foliage_primitives -le $foliage_instances ||
                  $foliage_submitted_vertices -le 0 ||
                  $foliage_submitted_vertices -ne $expected_foliage_submitted_vertices ||
                  $foliage_active_gpu_bytes -le 0 ||
                  $foliage_active_gpu_bytes -ne $foliage_resident_gpu_bytes ||
                  $windborne_particles -ne $expected_windborne_particles ||
                  $windborne_resident_particles -ne 64 ||
                  $metadata_line != *"foliageBatches=1"* ||
                  $decision_line == *"Foliage=n/a;"* ]]; then
                echo "Instanced patch foliage did not satisfy its topology/palette contract for ${scene} run ${run_index}." >&2
                exit 1
            fi
        fi

        avg_cpu=$(extract_number "$summary_line" 's/.*avg CPU ([0-9.]+) ms.*/\1/')
        avg_gpu=$(extract_number "$summary_line" 's/.*avg GPU ([0-9.]+) ms.*/\1/')
        sampled=$(extract_number "$summary_line" 's/.*profiledFrames=([0-9]+).*/\1/')
        median_cpu=$(extract_number "$statistics_line" 's/.*CPU median ([0-9.]+) ms.*/\1/')
        p95_cpu=$(extract_number "$statistics_line" 's/.*CPU median [0-9.]+ ms, p95 ([0-9.]+) ms.*/\1/')
        p99_cpu=$(extract_number "$statistics_line" 's/.*CPU median [0-9.]+ ms, p95 [0-9.]+ ms, p99 ([0-9.]+) ms.*/\1/')
        median_gpu=$(extract_number "$statistics_line" 's/.*GPU median ([0-9.]+) ms.*/\1/')
        p95_gpu=$(extract_number "$statistics_line" 's/.*GPU median [0-9.]+ ms, p95 ([0-9.]+) ms.*/\1/')
        p99_gpu=$(extract_number "$statistics_line" 's/.*GPU median [0-9.]+ ms, p95 [0-9.]+ ms, p99 ([0-9.]+) ms.*/\1/')
        swapchain=$(extract_number "$metadata_line" 's/.*swapchain=([0-9]+x[0-9]+).*/\1/')
        render_target=$(extract_number "$metadata_line" 's/.*renderTarget=([0-9]+x[0-9]+).*/\1/')

        if [[ $sampled -ne $sample_frames ]]; then
            echo "Expected ${sample_frames} samples, found ${sampled} for ${scene} run ${run_index}." >&2
            exit 1
        fi

        echo "${scene},${run_index},${avg_cpu},${median_cpu},${p95_cpu},${p99_cpu},${avg_gpu},${median_gpu},${p95_gpu},${p99_gpu},${sampled},${swapchain},${render_target},${foliage_run_renderer},${foliage_topology},${foliage_run_palette},${foliage_instances},${foliage_patches},${foliage_primitives},${foliage_vertices_per_primitive},${foliage_submitted_vertices},${foliage_active_gpu_bytes},${foliage_resident_gpu_bytes},${windborne_particles},${windborne_resident_particles},${windborne_submitted_vertices},${windborne_resident_gpu_bytes},${total_foliage_submitted_vertices},${total_foliage_resident_gpu_bytes}" >> "$run_csv"
        cpu_medians+=("$median_cpu")
        gpu_medians+=("$median_gpu")
    done

    cpu_median_of_medians=$(printf '%s\n' "${cpu_medians[@]}" | median_values)
    gpu_median_of_medians=$(printf '%s\n' "${gpu_medians[@]}" | median_values)
    cpu_deviation=$(printf '%s\n' "${cpu_medians[@]}" |
        max_deviation_percent "$cpu_median_of_medians")
    gpu_deviation=$(printf '%s\n' "${gpu_medians[@]}" |
        max_deviation_percent "$gpu_median_of_medians")
    variance_status="PASS"
    if awk -v cpu="$cpu_deviation" -v gpu="$gpu_deviation" \
        -v threshold="$variance_threshold" \
        'BEGIN { exit !((cpu > threshold) || (gpu > threshold)) }'; then
        variance_status="FLAG"
    fi

    echo "${scene},${runs},${cpu_median_of_medians},${cpu_deviation},${gpu_median_of_medians},${gpu_deviation},${variance_status}" >> "$summary_csv"
done

echo
echo "Benchmark complete: ${output_root}"
echo "Per-run results: ${run_csv}"
echo "Variance summary: ${summary_csv}"
if rg -q ",FLAG$" "$summary_csv"; then
    echo "One or more scenes exceeded the ${variance_threshold}% variance threshold." >&2
    exit 3
fi
