#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/.." && pwd)

runs=3
warmup_frames=120
sample_frames=240
window_size="1280x720"
frame_budget_ms=33.3
variance_threshold=5
jobs=8
skip_build=0
no_editor=0
ao_projected_radius_px=""
ao_projected_min_distance_m="2.0"
output_root=""
scene_csv="nature_pond_density_base_probe,nature_pond_density_coverage_probe,nature_pond_density_dense_probe,nature_pond_density_base_bank_probe,nature_pond_density_coverage_bank_probe,nature_pond_density_dense_bank_probe"

usage() {
    echo "Usage: $0 [options]"
    echo "  --runs N                  Repetitions per scene (default: 3)"
    echo "  --warmup-frames N         Resolved warm-up frames (default: 120)"
    echo "  --sample-frames N         Resolved measured frames (default: 240)"
    echo "  --window-size WIDTHxHEIGHT  Logical window size (default: 1280x720)"
    echo "  --frame-budget-ms MS      GPU frame budget (default: 33.3)"
    echo "  --variance-threshold PCT  Maximum run-median deviation (default: 5)"
    echo "  --scenes NAME[,NAME...]   Density scene subset for a focused rerun"
    echo "  --no-editor               Use the no-editor RelWithDebInfo preset"
    echo "  --ao-projected-radius-px PX  Enable the automation-only projected AO distance tier"
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
    --frame-budget-ms)
        frame_budget_ms=${2:-}
        shift 2
        ;;
    --variance-threshold)
        variance_threshold=${2:-}
        shift 2
        ;;
    --scenes)
        scene_csv=${2:-}
        shift 2
        ;;
    --no-editor)
        no_editor=1
        shift
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

if [[ -z $output_root ]]; then
    timestamp=$(date -u +"%Y%m%dT%H%M%SZ")
    output_root="build/performance/nature-pond-density-${timestamp}"
fi
if [[ $output_root != /* ]]; then
    output_root="${repo_root}/${output_root}"
fi

runner_args=(
    --runs "$runs"
    --warmup-frames "$warmup_frames"
    --sample-frames "$sample_frames"
    --window-size "$window_size"
    --framebuffer-scale native
    --scenes "$scene_csv"
    --variance-threshold "$variance_threshold"
    --jobs "$jobs"
    --output "$output_root"
)
if [[ $skip_build -eq 1 ]]; then
    runner_args+=(--skip-build)
fi
if [[ $no_editor -eq 1 ]]; then
    runner_args+=(--no-editor)
fi
if [[ -n $ao_projected_radius_px ]]; then
    runner_args+=(--ao-projected-radius-px "$ao_projected_radius_px")
    runner_args+=(--ao-projected-min-distance-m "$ao_projected_min_distance_m")
fi

set +e
"${script_dir}/Run-MacOSPerformanceBenchmark.sh" "${runner_args[@]}"
runner_status=$?
set -e
if [[ $runner_status -ne 0 && $runner_status -ne 3 ]]; then
    exit "$runner_status"
fi

{
    echo "milestone=7-progressive-density-measurement"
    echo "frame_budget_ms=${frame_budget_ms}"
    echo "density_axis=24x20m@1.0x,30x25m@1.0x,30x25m@1.5x"
    echo "camera_axis=overview,bank"
    echo "measurement_profile=painted-outdoor-clouds-v1"
    echo "measurement_glass_enabled=0"
    echo "ao_projected_radius_px=${ao_projected_radius_px:-disabled}"
    echo "ao_projected_min_distance_m=${ao_projected_min_distance_m}"
} >> "${output_root}/metadata.txt"

extract_field() {
    local line=$1
    local key=$2
    local marker="${key}="
    if [[ $line != *"$marker"* ]]; then
        echo "missing"
        return
    fi
    local remainder=${line#*${marker}}
    local value=${remainder%% *}
    printf '%s' "${value%.}"
}

pass_value() {
    local line=$1
    local label=$2
    local marker="${label}="
    if [[ $line != *"$marker"* ]]; then
        echo "n/a"
        return
    fi
    local remainder=${line#*${marker}}
    local value=${remainder%%;*}
    printf '%s' "${value% ms}"
}

median_values() {
    sort -n | awk '
        { values[NR] = $1 }
        END {
            if (NR == 0) {
                print "n/a"
            } else if (NR % 2 == 1) {
                printf "%.6f", values[(NR + 1) / 2]
            } else {
                printf "%.6f", (values[NR / 2] + values[NR / 2 + 1]) / 2.0
            }
        }'
}

median_csv_column() {
    local csv=$1
    local scene=$2
    local column=$3
    awk -F, -v scene="$scene" -v column="$column" \
        '$1 == scene && $column != "n/a" { print $column }' "$csv" |
        median_values
}

content_runs_csv="${output_root}/content-runs.csv"
pass_runs_csv="${output_root}/pass-runs.csv"
curve_summary_csv="${output_root}/density-curve-summary.csv"

echo "scene,run,camera,density_step,patch_width_m,patch_depth_m,scatter_density_multiplier,volume_count,terrain_dims,detail_dims,packed_voxel_capacity,nominal_packed_r8_bytes,nominal_voxel_r8_bytes,nominal_occupancy_r8_bytes,nominal_total_r8_bytes,terrain_occupied,opaque_detail_occupied,foliage_occupied,hero_trunk_occupied,hero_canopy_occupied,hero_canopy_proxy_occupied,total_occupied" > "$content_runs_csv"
echo "scene,run,voxel_dda_ms,shadow_rays_ms,shadow_resolve_ms,ao_rays_ms,ao_resolve_ms,local_shadows_ms,local_shadow_resolve_ms,water_volume_prepass_ms,water_ms,lighting_ms,taa_ms,composite_ui_ms" > "$pass_runs_csv"

IFS=',' read -r -a scenes <<< "$scene_csv"
for scene in "${scenes[@]}"; do
    camera="overview"
    if [[ $scene == *_bank_probe ]]; then
        camera="bank"
    fi
    for ((run_index = 1; run_index <= runs; ++run_index)); do
        stdout_path="${output_root}/${scene}/run-${run_index}/stdout.txt"
        detail_line=$(rg -m1 "Detail tier:" "$stdout_path")
        decision_line=$(rg -m1 "Decision GPU passes:" "$stdout_path")

        density_step=$(extract_field "$detail_line" "densityStep")
        patch_extent=$(extract_field "$detail_line" "patchExtent")
        patch_extent=${patch_extent%m}
        patch_width=${patch_extent%x*}
        patch_depth=${patch_extent#*x}
        scatter=$(extract_field "$detail_line" "scatterDensityMultiplier")
        volume_count=$(extract_field "$detail_line" "volumeCount")
        terrain_dims=$(extract_field "$detail_line" "terrainDims")
        detail_dims=$(extract_field "$detail_line" "detailDims")
        packed_capacity=$(extract_field "$detail_line" "packedVoxelCapacity")
        nominal_packed=$(extract_field "$detail_line" "nominalPackedR8Bytes")
        nominal_voxel=$(extract_field "$detail_line" "nominalVoxelR8Bytes")
        nominal_occupancy=$(extract_field "$detail_line" "nominalOccupancyR8Bytes")
        nominal_total=$(extract_field "$detail_line" "nominalTotalR8Bytes")
        terrain_occupied=$(extract_field "$detail_line" "terrainOccupied")
        opaque_occupied=$(extract_field "$detail_line" "opaqueDetailOccupied")
        foliage_occupied=$(extract_field "$detail_line" "foliageOccupied")
        trunk_occupied=$(extract_field "$detail_line" "heroTrunkOccupied")
        canopy_occupied=$(extract_field "$detail_line" "heroCanopyOccupied")
        proxy_occupied=$(extract_field "$detail_line" "heroCanopyProxyOccupied")
        total_occupied=$((terrain_occupied + opaque_occupied + foliage_occupied + trunk_occupied + canopy_occupied + proxy_occupied))

        echo "${scene},${run_index},${camera},${density_step},${patch_width},${patch_depth},${scatter},${volume_count},${terrain_dims},${detail_dims},${packed_capacity},${nominal_packed},${nominal_voxel},${nominal_occupancy},${nominal_total},${terrain_occupied},${opaque_occupied},${foliage_occupied},${trunk_occupied},${canopy_occupied},${proxy_occupied},${total_occupied}" >> "$content_runs_csv"

        voxel_dda=$(pass_value "$decision_line" "Voxel DDA")
        shadow_rays=$(pass_value "$decision_line" "Shadow Rays")
        shadow_resolve=$(pass_value "$decision_line" "Shadow Resolve")
        ao_rays=$(pass_value "$decision_line" "AO Rays")
        ao_resolve=$(pass_value "$decision_line" "AO Resolve")
        local_shadows=$(pass_value "$decision_line" "Local Shadows")
        local_shadow_resolve=$(pass_value "$decision_line" "Local Shadow Resolve")
        water_prepass=$(pass_value "$decision_line" "Water Volume Prepass")
        water=$(pass_value "$decision_line" "Water")
        lighting=$(pass_value "$decision_line" "Lighting")
        taa=$(pass_value "$decision_line" "TAA")
        composite=$(pass_value "$decision_line" "Composite+UI")
        echo "${scene},${run_index},${voxel_dda},${shadow_rays},${shadow_resolve},${ao_rays},${ao_resolve},${local_shadows},${local_shadow_resolve},${water_prepass},${water},${lighting},${taa},${composite}" >> "$pass_runs_csv"
    done
done

echo "scene,camera,density_step,patch_width_m,patch_depth_m,scatter_density_multiplier,volume_count,total_occupied,nominal_total_r8_bytes,cpu_median_ms,cpu_deviation_percent,gpu_median_ms,gpu_deviation_percent,variance_status,voxel_dda_ms,shadow_rays_ms,shadow_resolve_ms,ao_rays_ms,ao_resolve_ms,local_shadows_ms,local_shadow_resolve_ms,water_volume_prepass_ms,water_ms,lighting_ms,taa_ms,composite_ui_ms,budget_status" > "$curve_summary_csv"

for scene in "${scenes[@]}"; do
    content_row=$(awk -F, -v scene="$scene" '$1 == scene { print; exit }' "$content_runs_csv")
    IFS=',' read -r _ _ camera density_step patch_width patch_depth scatter volume_count _ _ _ _ _ _ nominal_total _ _ _ _ _ _ total_occupied <<< "$content_row"
    benchmark_row=$(awk -F, -v scene="$scene" '$1 == scene { print; exit }' "${output_root}/summary.csv")
    IFS=',' read -r _ _ cpu_median cpu_deviation gpu_median gpu_deviation variance_status <<< "$benchmark_row"

    voxel_dda=$(median_csv_column "$pass_runs_csv" "$scene" 3)
    shadow_rays=$(median_csv_column "$pass_runs_csv" "$scene" 4)
    shadow_resolve=$(median_csv_column "$pass_runs_csv" "$scene" 5)
    ao_rays=$(median_csv_column "$pass_runs_csv" "$scene" 6)
    ao_resolve=$(median_csv_column "$pass_runs_csv" "$scene" 7)
    local_shadows=$(median_csv_column "$pass_runs_csv" "$scene" 8)
    local_shadow_resolve=$(median_csv_column "$pass_runs_csv" "$scene" 9)
    water_prepass=$(median_csv_column "$pass_runs_csv" "$scene" 10)
    water=$(median_csv_column "$pass_runs_csv" "$scene" 11)
    lighting=$(median_csv_column "$pass_runs_csv" "$scene" 12)
    taa=$(median_csv_column "$pass_runs_csv" "$scene" 13)
    composite=$(median_csv_column "$pass_runs_csv" "$scene" 14)
    budget_status="PASS"
    if awk -v gpu="$gpu_median" -v budget="$frame_budget_ms" \
        'BEGIN { exit !(gpu > budget) }'; then
        budget_status="OVER"
    fi

    echo "${scene},${camera},${density_step},${patch_width},${patch_depth},${scatter},${volume_count},${total_occupied},${nominal_total},${cpu_median},${cpu_deviation},${gpu_median},${gpu_deviation},${variance_status},${voxel_dda},${shadow_rays},${shadow_resolve},${ao_rays},${ao_resolve},${local_shadows},${local_shadow_resolve},${water_prepass},${water},${lighting},${taa},${composite},${budget_status}" >> "$curve_summary_csv"
done

echo
echo "Density benchmark results: ${output_root}"
echo "Content records: ${content_runs_csv}"
echo "Pass records: ${pass_runs_csv}"
echo "Density curve: ${curve_summary_csv}"

exit "$runner_status"
