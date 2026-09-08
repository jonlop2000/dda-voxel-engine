#!/usr/bin/env bash

set -euo pipefail
export LC_ALL=C
export LANG=C

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/.." && pwd)
benchmark_runner="${script_dir}/Run-MacOSPerformanceBenchmark.sh"

jobs=8
skip_soak=0
soak_seconds=300
tail_regression_threshold=5
output_root=""

usage() {
    echo "Usage: $0 [options]"
    echo "  --output DIR       Evidence directory (must be empty)"
    echo "  --jobs N           Parallel build jobs (default: 8)"
    echo "  --skip-soak        Harness-only check; omits the required five-minute soak"
    echo "  --help             Show this help"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --output)
        if [[ $# -lt 2 || -z ${2:-} || ${2:-} == --* ]]; then
            echo "--output requires a non-empty directory." >&2
            exit 2
        fi
        output_root=$2
        shift 2
        ;;
    --jobs)
        if [[ $# -lt 2 || -z ${2:-} || ${2:-} == --* ]]; then
            echo "--jobs requires a positive integer." >&2
            exit 2
        fi
        jobs=$2
        shift 2
        ;;
    --skip-soak)
        skip_soak=1
        shift
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

if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
    echo "--jobs must be a positive integer." >&2
    exit 2
fi
if [[ $(uname -s) != "Darwin" || $(uname -m) != "arm64" ]]; then
    echo "temporal-shadow benchmark must run on the target Apple Silicon Mac." >&2
    exit 1
fi
if [[ ! -x $benchmark_runner ]]; then
    echo "Benchmark runner not found: $benchmark_runner" >&2
    exit 1
fi

system_profile_json=$(system_profiler SPHardwareDataType SPDisplaysDataType -json)
profile_value() {
    local key=$1
    local value
    value=$(plutil -extract "$key" raw -o - - <<< "$system_profile_json" 2>/dev/null || true)
    printf '%s' "${value:-unavailable}"
}

hardware_model=$(sysctl -n hw.model)
chip_type=$(profile_value "SPHardwareDataType.0.chip_type")
gpu_cores=$(profile_value "SPDisplaysDataType.0.sppci_cores")
if [[ $hardware_model != "MacBookAir10,1" || $chip_type != "Apple M1" ||
      $gpu_cores != "7" ]]; then
    echo "temporal-shadow benchmark is qualified only on the 7-core-GPU M1 MacBook Air." >&2
    echo "Detected model=${hardware_model}, chip=${chip_type}, gpu_cores=${gpu_cores}." >&2
    exit 1
fi

if pgrep -x voxel_aquarium >/dev/null 2>&1; then
    echo "Close the interactive voxel_aquarium process before benchmarking." >&2
    exit 1
fi

if [[ -z $output_root ]]; then
    timestamp=$(date -u +"%Y%m%dT%H%M%SZ")
    output_root="${repo_root}/build/performance/m1-temporal-shadow/native-bracket-${timestamp}"
elif [[ $output_root != /* ]]; then
    output_root="${repo_root}/${output_root}"
fi
case "$output_root" in
"${repo_root}/build/performance/m1-temporal-shadow/"*) ;;
*)
    echo "temporal-shadow benchmark evidence must stay under build/performance/m1-temporal-shadow/." >&2
    exit 1
    ;;
esac
if [[ -e $output_root && ! -d $output_root ]]; then
    echo "Output path exists and is not a directory: $output_root" >&2
    exit 1
fi
if [[ -d $output_root && -n $(find "$output_root" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
    echo "Output directory must be empty: $output_root" >&2
    exit 1
fi
mkdir -p "$output_root"

if [[ ${DDA_VOXEL_MACOS_AUTO_SETUP:-1} != "0" &&
      ( -z ${VCPKG_ROOT:-} || -z ${VULKAN_SDK:-} ) ]]; then
    # shellcheck source=Setup-MacOSEnvironment.sh
    source "${script_dir}/Setup-MacOSEnvironment.sh"
fi

preflight_root="${output_root}/preflight"
mkdir -p "$preflight_root"
echo "temporal-shadow benchmark static preflight: Debug editor"
{
    cmake --preset mac-ninja-debug-arm64
    cmake --build --preset build-mac-debug-arm64 \
        --target engine_core_tests scene_presentation_profile_tests voxel_aquarium \
        -j "$jobs"
    spirv-val --target-env vulkan1.2 \
        "${repo_root}/build/mac-ninja-debug-arm64/shaders/shadow_ray.comp.spv"
    spirv-val --target-env vulkan1.2 \
        "${repo_root}/build/mac-ninja-debug-arm64/shaders/temporal_resolve.comp.spv"
    ctest --preset test-mac-debug-arm64 --output-on-failure \
        -R '^(engine_core_tests|scene_presentation_profile_tests|structural_app_size_ratchet|structural_vma_ownership_ratchet)$'
} 2>&1 | tee "${preflight_root}/debug-editor-static.txt"

echo "temporal-shadow benchmark static preflight: Debug no-editor"
{
    cmake --preset mac-ninja-debug-arm64-noeditor
    cmake --build --preset build-mac-debug-arm64-noeditor \
        --target engine_core_tests scene_presentation_profile_tests voxel_aquarium \
        -j "$jobs"
    spirv-val --target-env vulkan1.2 \
        "${repo_root}/build/mac-ninja-debug-arm64-noeditor/shaders/shadow_ray.comp.spv"
    spirv-val --target-env vulkan1.2 \
        "${repo_root}/build/mac-ninja-debug-arm64-noeditor/shaders/temporal_resolve.comp.spv"
    ctest --preset test-mac-debug-arm64-noeditor --output-on-failure \
        -R '^(engine_core_tests|scene_presentation_profile_tests|structural_app_size_ratchet|structural_vma_ownership_ratchet)$'
} 2>&1 | tee "${preflight_root}/debug-noeditor-static.txt"

# engine launches are allowed only after both static gates above pass.
echo "temporal-shadow benchmark focused runtime routing probes"
{
    ctest --preset test-mac-debug-arm64 --output-on-failure \
        -R '^(runtime_fishbowl_sun_shadow_default_probe|runtime_fishbowl_sun_shadow_full_probe|runtime_fishbowl_sun_shadow_temporal_two_probe)$'
    ctest --preset test-mac-debug-arm64-noeditor --output-on-failure \
        -R '^(runtime_fishbowl_sun_shadow_default_probe|runtime_fishbowl_sun_shadow_full_probe|runtime_fishbowl_sun_shadow_temporal_two_probe)$'
} 2>&1 | tee "${preflight_root}/focused-runtime.txt"

build_dir="${repo_root}/build/mac-ninja-relwithdebinfo-arm64"
executable="${build_dir}/voxel_aquarium"
cmake --preset mac-ninja-relwithdebinfo-arm64
cmake --build --preset build-mac-relwithdebinfo-arm64 \
    --target voxel_aquarium -j "$jobs"
if [[ ! -x $executable ]]; then
    echo "RelWithDebInfo editor executable not found: $executable" >&2
    exit 1
fi
spirv-val --target-env vulkan1.2 "${build_dir}/shaders/shadow_ray.comp.spv"
spirv-val --target-env vulkan1.2 "${build_dir}/shaders/temporal_resolve.comp.spv"

read_metadata() {
    local metadata_path=$1
    local key=$2
    awk -F= -v key="$key" '$1 == key { sub(/^[^=]*=/, ""); print; exit }' \
        "$metadata_path"
}

require_literal() {
    local file_path=$1
    local expected=$2
    if ! rg -Fq -- "$expected" "$file_path"; then
        echo "Missing expected evidence in ${file_path}: ${expected}" >&2
        exit 1
    fi
}

median_values() {
    sort -n | awk '
        { values[NR] = $1 }
        END {
            if (NR == 0) {
                exit 1
            } else if (NR % 2 == 1) {
                printf "%.6f", values[(NR + 1) / 2]
            } else {
                printf "%.6f", (values[NR / 2] + values[NR / 2 + 1]) / 2.0
            }
        }'
}

route_median() {
    local route=$1
    local column=$2
    awk -F, -v route="$route" -v column="$column" \
        'NR > 1 && $2 == route { print $column }' "$bracket_csv" | median_values
}

route_max_deviation() {
    local route=$1
    local column=$2
    local median=$3
    awk -F, -v route="$route" -v column="$column" -v median="$median" '
        NR > 1 && $2 == route {
            deviation = $column - median
            if (deviation < 0) deviation = -deviation
            if (deviation > maxDeviation) maxDeviation = deviation
        }
        END {
            if (median > 0) printf "%.3f", maxDeviation * 100.0 / median
            else printf "0.000"
        }' "$bracket_csv"
}

percent_change() {
    local baseline=$1
    local candidate=$2
    awk -v baseline="$baseline" -v candidate="$candidate" \
        'BEGIN {
            if (baseline == 0) printf "0.000"
            else printf "%.3f", (candidate - baseline) * 100.0 / baseline
        }'
}

float_le() {
    awk -v lhs="$1" -v rhs="$2" 'BEGIN { exit !(lhs <= rhs) }'
}

float_ge() {
    awk -v lhs="$1" -v rhs="$2" 'BEGIN { exit !(lhs >= rhs) }'
}

float_lt() {
    awk -v lhs="$1" -v rhs="$2" 'BEGIN { exit !(lhs < rhs) }'
}

current_power_source() {
    local value
    value=$(pmset -g batt | sed -n "1s/Now drawing from '\(.*\)'/\1/p")
    printf '%s' "${value:-unavailable}"
}

current_low_power_mode() {
    local value
    value=$(pmset -g | awk '$1 == "lowpowermode" { print $2; exit }')
    printf '%s' "${value:-unavailable}"
}

capture_state() {
    local destination=$1
    local power_source
    local low_power_mode
    power_source=$(current_power_source)
    low_power_mode=$(current_low_power_mode)
    {
        echo "captured_at=$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
        echo "power_source=${power_source}"
        echo "low_power_mode=${low_power_mode}"
    } > "${destination}-power.txt"
    pmset -g therm > "${destination}-thermal.txt"
    if [[ $power_source != "AC Power" || $low_power_mode != "0" ]]; then
        echo "temporal-shadow benchmark requires stable AC power with Low Power Mode disabled." >&2
        exit 1
    fi
    if ! rg -Fq "No thermal warning level has been recorded" \
            "${destination}-thermal.txt" ||
       ! rg -Fq "No performance warning level has been recorded" \
            "${destination}-thermal.txt"; then
        echo "temporal-shadow benchmark detected a recorded thermal or performance warning." >&2
        exit 1
    fi
}

validate_run() {
    local run_root=$1
    local expected_mode=$2
    local expected_effective=$3
    local metadata_path="${run_root}/metadata.txt"
    local runs_path="${run_root}/runs.csv"
    local first_sample_root="${run_root}/fishbowl_perf_probe/run-1"

    for required_path in "$metadata_path" "$runs_path" \
        "${first_sample_root}/effects.txt" "${first_sample_root}/stdout.txt" \
        "${first_sample_root}/decision-passes.txt"; do
        if [[ ! -s $required_path ]]; then
            echo "Missing temporal-shadow benchmark evidence: $required_path" >&2
            exit 1
        fi
    done

    if [[ $(read_metadata "$metadata_path" build) != "RelWithDebInfo editor" ||
          $(read_metadata "$metadata_path" window_size) != "1280x720" ||
          $(read_metadata "$metadata_path" framebuffer_scale) != "native" ||
          $(read_metadata "$metadata_path" render_quality_preset) != "native" ||
          $(read_metadata "$metadata_path" render_scale) != "1.0" ||
          $(read_metadata "$metadata_path" shadow_ray_scale) != "1.0" ||
          $(read_metadata "$metadata_path" ao_ray_scale) != "1.0" ||
          $(read_metadata "$metadata_path" shadow_ray_audit) != "0" ||
          $(read_metadata "$metadata_path" sun_shadow_sampling) != "$expected_mode" ||
          $(read_metadata "$metadata_path" fixed_scene_time_seconds) != "1.0" ||
          $(read_metadata "$metadata_path" ao_projected_radius_px) != "disabled" ]]; then
        echo "temporal-shadow benchmark isolation metadata mismatch in $run_root" >&2
        exit 1
    fi

    if ! awk -F, 'NR > 1 {
            ++rows
            if ($11 != 240 || $12 != "2560x1440" || $13 != "2560x1440") bad = 1
        } END { exit !(rows >= 1 && !bad) }' "$runs_path"; then
        echo "temporal-shadow benchmark sample count or native target mismatch in $run_root" >&2
        exit 1
    fi

    local sample_root
    for sample_root in "${run_root}/fishbowl_perf_probe"/run-*; do
        local effects_path="${sample_root}/effects.txt"
        local stdout_path="${sample_root}/stdout.txt"
        local decision_path="${sample_root}/decision-passes.txt"
        for required_path in "$effects_path" "$stdout_path" "$decision_path"; do
            if [[ ! -s $required_path ]]; then
                echo "Missing temporal-shadow benchmark per-run evidence: $required_path" >&2
                exit 1
            fi
        done
        require_literal "$effects_path" "sunShadowSampling=${expected_mode}"
        require_literal "$effects_path" "sunShadowSamplesAuthored=4"
        require_literal "$effects_path" "sunShadowSamplesEffective=${expected_effective}"
        require_literal "$effects_path" "sceneTimeFrozen=1, sceneTimeSeconds=1"
        require_literal "$effects_path" "shadowRayScale=1"
        require_literal "$effects_path" "aoRayScale=1"
        require_literal "$effects_path" "renderScale=1"
        require_literal "$effects_path" "AOProjectedOverride=0"
        require_literal "$stdout_path" "Runtime content validation succeeded."
        require_literal "$stdout_path" "First frame rendered."
        require_literal "$stdout_path" "Clean shutdown."
        require_literal "$stdout_path" \
            "Render resolution internal=2560x1440 presentation=2560x1440 scale=1"
        require_literal "$stdout_path" \
            "Auxiliary ray targets: shadow=2560x1440 (scale 1), AO=2560x1440 (scale 1), scene=2560x1440."
    done
}

sequence_modes="full temporal-2 temporal-2 full full temporal-2"
sequence_routes="A B B A A B"
bracket_csv="${output_root}/bracket-runs.csv"
pair_csv="${output_root}/pair-comparisons.csv"
gate_csv="${output_root}/performance-gates.csv"

{
    echo "generated_at=$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
    echo "contract=temporal-shadow-benchmark"
    echo "sequence=A-B-B-A-A-B"
    echo "scene=fishbowl_perf_probe"
    echo "logical_window=1280x720"
    echo "required_native_target=2560x1440"
    echo "render_quality_preset=native"
    echo "render_scale=1.0"
    echo "shadow_ray_scale=1.0"
    echo "ao_ray_scale=1.0"
    echo "fixed_scene_time_seconds=1.0"
    echo "warmup_frames=120"
    echo "sample_frames=240"
    echo "tail_regression_threshold_percent=${tail_regression_threshold}"
    echo "required_soak_seconds=${soak_seconds}"
    echo "hardware_model=${hardware_model}"
    echo "chip=${chip_type}"
    echo "gpu_cores=${gpu_cores}"
} > "${output_root}/protocol.txt"

capture_state "${output_root}/bracket-before"
echo "position,route,mode,cpu_median_ms,cpu_p95_ms,cpu_p99_ms,gpu_median_ms,gpu_p95_ms,gpu_p99_ms,shadow_rays_ms,shadow_resolve_ms,power_source,low_power_mode,executable_sha256,source_state_sha256,shadow_shader_sha256,temporal_resolve_shader_sha256" > "$bracket_csv"

reference_executable_sha=""
reference_source_sha=""
reference_shadow_sha=""
reference_resolve_sha=""
reference_power_source=""
reference_low_power_mode=""
reference_effects_path="${output_root}/normalized-control-effects.txt"
reference_profile_path="${output_root}/normalized-control-profile-metadata.txt"

position=1
for mode in $sequence_modes; do
    route=$(printf '%s\n' "$sequence_routes" | awk -v position="$position" '{ print $position }')
    position_label=$(printf '%02d-%s-%s' "$position" "$route" "$mode")
    position_root="${output_root}/positions/${position_label}"
    mkdir -p "$position_root"
    capture_state "${position_root}/before"

    echo "temporal-shadow benchmark position ${position}/6: route ${route} (${mode})"
    "$benchmark_runner" \
        --allow-single-run \
        --runs 1 \
        --warmup-frames 120 \
        --sample-frames 240 \
        --window-size 1280x720 \
        --framebuffer-scale native \
        --render-quality-preset native \
        --render-scale 1.0 \
        --shadow-ray-scale 1.0 \
        --ao-ray-scale 1.0 \
        --scenes fishbowl_perf_probe \
        --variance-threshold 5 \
        --sun-shadow-sampling "$mode" \
        --fixed-scene-time-seconds 1.0 \
        --skip-build \
        --output "${position_root}/benchmark"

    capture_state "${position_root}/after"
    benchmark_root="${position_root}/benchmark"
    validate_run "$benchmark_root" "$mode" "$([[ $route == B ]] && echo 2 || echo 4)"

    metadata_path="${benchmark_root}/metadata.txt"
    runs_path="${benchmark_root}/runs.csv"
    decision_path="${benchmark_root}/fishbowl_perf_probe/run-1/decision-passes.txt"
    executable_sha=$(read_metadata "$metadata_path" executable_sha256)
    source_sha=$(read_metadata "$metadata_path" source_state_sha256)
    shadow_sha=$(read_metadata "$metadata_path" shadow_ray_shader_sha256)
    resolve_sha=$(read_metadata "$metadata_path" temporal_resolve_shader_sha256)
    power_source=$(read_metadata "$metadata_path" power_source)
    low_power_mode=$(read_metadata "$metadata_path" low_power_mode)

    if [[ $position -eq 1 ]]; then
        reference_executable_sha=$executable_sha
        reference_source_sha=$source_sha
        reference_shadow_sha=$shadow_sha
        reference_resolve_sha=$resolve_sha
        reference_power_source=$power_source
        reference_low_power_mode=$low_power_mode
    elif [[ $executable_sha != "$reference_executable_sha" ||
            $source_sha != "$reference_source_sha" ||
            $shadow_sha != "$reference_shadow_sha" ||
            $resolve_sha != "$reference_resolve_sha" ||
            $power_source != "$reference_power_source" ||
            $low_power_mode != "$reference_low_power_mode" ]]; then
        echo "Executable, source, shader, or power state changed at position $position." >&2
        exit 1
    fi
    if [[ $power_source != "AC Power" || $low_power_mode != "0" ]]; then
        echo "temporal-shadow benchmark requires stable AC power with Low Power Mode disabled." >&2
        exit 1
    fi

    cpu_median=$(awk -F, 'NR == 2 { print $4 }' "$runs_path")
    cpu_p95=$(awk -F, 'NR == 2 { print $5 }' "$runs_path")
    cpu_p99=$(awk -F, 'NR == 2 { print $6 }' "$runs_path")
    gpu_median=$(awk -F, 'NR == 2 { print $8 }' "$runs_path")
    gpu_p95=$(awk -F, 'NR == 2 { print $9 }' "$runs_path")
    gpu_p99=$(awk -F, 'NR == 2 { print $10 }' "$runs_path")
    shadow_rays=$(sed -E 's/.*Shadow Rays=([0-9.]+) ms.*/\1/' "$decision_path")
    shadow_resolve=$(sed -E 's/.*Shadow Resolve=([0-9.]+) ms.*/\1/' "$decision_path")
    effects_path="${benchmark_root}/fishbowl_perf_probe/run-1/effects.txt"
    profile_path="${benchmark_root}/fishbowl_perf_probe/run-1/profile-metadata.txt"
    normalized_effects_path="${position_root}/normalized-effects.txt"
    normalized_profile_path="${position_root}/normalized-profile-metadata.txt"
    sed -E \
        -e 's/^\[[^]]+\]\[[^]]+\]\[[^]]+\] //' \
        -e 's/sunShadowSampling=[^,]+/sunShadowSampling=ROUTE/' \
        -e 's/sunShadowSamplesEffective=[0-9]+/sunShadowSamplesEffective=ROUTE/' \
        "$effects_path" > "$normalized_effects_path"
    sed -E 's/^\[[^]]+\]\[[^]]+\]\[[^]]+\] //' \
        "$profile_path" > "$normalized_profile_path"
    if [[ $position -eq 1 ]]; then
        cp "$normalized_effects_path" "$reference_effects_path"
        cp "$normalized_profile_path" "$reference_profile_path"
    else
        if ! cmp -s "$normalized_effects_path" "$reference_effects_path"; then
            echo "A non-route renderer effect changed at position $position." >&2
            exit 1
        fi
        if ! cmp -s "$normalized_profile_path" "$reference_profile_path"; then
            echo "Scene/content metadata changed at position $position." >&2
            exit 1
        fi
    fi

    echo "${position},${route},${mode},${cpu_median},${cpu_p95},${cpu_p99},${gpu_median},${gpu_p95},${gpu_p99},${shadow_rays},${shadow_resolve},${power_source},${low_power_mode},${executable_sha},${source_sha},${shadow_sha},${resolve_sha}" >> "$bracket_csv"
    if [[ $position -eq 1 ]]; then
        largest_named_pass=$(
            rg -m1 "Top GPU passes:" \
                "${benchmark_root}/fishbowl_perf_probe/run-1/stdout.txt" |
                tr ';' '\n' |
                sed -nE 's/.*=([0-9.]+) ms.*/\1/p' |
                sort -nr |
                head -1
        )
        shadow_share_percent=$(awk -v shadow="$shadow_rays" -v gpu="$gpu_median" \
            'BEGIN { printf "%.3f", shadow * 100.0 / gpu }')
        attribution_pass=1
        if ! float_ge "$shadow_share_percent" 15 ||
           ! float_ge "$shadow_rays" "$largest_named_pass"; then
            attribution_pass=0
        fi
        {
            echo "metric,value,threshold,pass"
            echo "shadow_rays_share_percent,${shadow_share_percent},min_15,${attribution_pass}"
            echo "shadow_rays_ms,${shadow_rays},largest_named_${largest_named_pass},${attribution_pass}"
        } > "${output_root}/attribution-gate.csv"
        if [[ $attribution_pass -ne 1 ]]; then
            echo "PERFORMANCE_GATE=FAIL_ATTRIBUTION" > "${output_root}/status.txt"
            echo "temporal-shadow benchmark stopped: the first native control failed attribution." >&2
            exit 3
        fi
    fi
    position=$((position + 1))
done
capture_state "${output_root}/bracket-after"

control_cpu=$(route_median A 4)
candidate_cpu=$(route_median B 4)
control_gpu=$(route_median A 7)
candidate_gpu=$(route_median B 7)
control_p95=$(route_median A 8)
candidate_p95=$(route_median B 8)
control_p99=$(route_median A 9)
candidate_p99=$(route_median B 9)
control_shadow=$(route_median A 10)
candidate_shadow=$(route_median B 10)
control_resolve=$(route_median A 11)
candidate_resolve=$(route_median B 11)
control_cpu_deviation=$(route_max_deviation A 4 "$control_cpu")
candidate_cpu_deviation=$(route_max_deviation B 4 "$candidate_cpu")
control_gpu_deviation=$(route_max_deviation A 7 "$control_gpu")
candidate_gpu_deviation=$(route_max_deviation B 7 "$candidate_gpu")
gpu_improvement_ms=$(awk -v control="$control_gpu" -v candidate="$candidate_gpu" \
    'BEGIN { printf "%.3f", control - candidate }')
gpu_reduction_percent=$(awk -v control="$control_gpu" -v candidate="$candidate_gpu" \
    'BEGIN { printf "%.3f", (control - candidate) * 100.0 / control }')
shadow_reduction_percent=$(awk -v control="$control_shadow" -v candidate="$candidate_shadow" \
    'BEGIN { printf "%.3f", (control - candidate) * 100.0 / control }')
p95_change_percent=$(percent_change "$control_p95" "$candidate_p95")
p99_change_percent=$(percent_change "$control_p99" "$candidate_p99")

echo "pair,control_position,candidate_position,control_gpu_median_ms,candidate_gpu_median_ms,candidate_improvement_ms,candidate_wins" > "$pair_csv"
pair_pass=1
pair_index=1
for pair_positions in "1 2" "4 3" "5 6"; do
    control_position=$(printf '%s\n' "$pair_positions" | awk '{ print $1 }')
    candidate_position=$(printf '%s\n' "$pair_positions" | awk '{ print $2 }')
    pair_control=$(awk -F, -v position="$control_position" '$1 == position { print $7 }' "$bracket_csv")
    pair_candidate=$(awk -F, -v position="$candidate_position" '$1 == position { print $7 }' "$bracket_csv")
    pair_improvement=$(awk -v control="$pair_control" -v candidate="$pair_candidate" \
        'BEGIN { printf "%.3f", control - candidate }')
    pair_wins=0
    if float_lt "$pair_candidate" "$pair_control"; then
        pair_wins=1
    else
        pair_pass=0
    fi
    echo "${pair_index},${control_position},${candidate_position},${pair_control},${pair_candidate},${pair_improvement},${pair_wins}" >> "$pair_csv"
    pair_index=$((pair_index + 1))
done

variance_pass=1
control_cpu_variance_pass=0
candidate_cpu_variance_pass=0
control_gpu_variance_pass=0
candidate_gpu_variance_pass=0
if float_le "$control_cpu_deviation" 5; then control_cpu_variance_pass=1; fi
if float_le "$candidate_cpu_deviation" 5; then candidate_cpu_variance_pass=1; fi
if float_le "$control_gpu_deviation" 5; then control_gpu_variance_pass=1; fi
if float_le "$candidate_gpu_deviation" 5; then candidate_gpu_variance_pass=1; fi
if [[ $control_cpu_variance_pass -ne 1 ||
      $candidate_cpu_variance_pass -ne 1 ||
      $control_gpu_variance_pass -ne 1 ||
      $candidate_gpu_variance_pass -ne 1 ]]; then
    variance_pass=0
fi
shadow_pass=0
if float_ge "$shadow_reduction_percent" 25; then shadow_pass=1; fi
gpu_improvement_pass=0
if float_ge "$gpu_improvement_ms" 1; then gpu_improvement_pass=1; fi
tails_pass=1
p95_pass=0
p99_pass=0
if float_le "$p95_change_percent" "$tail_regression_threshold"; then p95_pass=1; fi
if float_le "$p99_change_percent" "$tail_regression_threshold"; then p99_pass=1; fi
if [[ $p95_pass -ne 1 || $p99_pass -ne 1 ]]; then
    tails_pass=0
fi

{
    echo "gate,value,threshold,pass"
    echo "all_adjacent_pairs_candidate_wins,${pair_pass},required_1,${pair_pass}"
    echo "control_cpu_max_deviation_percent,${control_cpu_deviation},max_5,${control_cpu_variance_pass}"
    echo "candidate_cpu_max_deviation_percent,${candidate_cpu_deviation},max_5,${candidate_cpu_variance_pass}"
    echo "control_gpu_max_deviation_percent,${control_gpu_deviation},max_5,${control_gpu_variance_pass}"
    echo "candidate_gpu_max_deviation_percent,${candidate_gpu_deviation},max_5,${candidate_gpu_variance_pass}"
    echo "shadow_rays_reduction_percent,${shadow_reduction_percent},min_25,${shadow_pass}"
    echo "whole_gpu_improvement_ms,${gpu_improvement_ms},min_1,${gpu_improvement_pass}"
    echo "gpu_p95_change_percent,${p95_change_percent},max_${tail_regression_threshold},${p95_pass}"
    echo "gpu_p99_change_percent,${p99_change_percent},max_${tail_regression_threshold},${p99_pass}"
} > "$gate_csv"

{
    echo "route,cpu_median_ms,cpu_max_deviation_percent,gpu_median_ms,gpu_max_deviation_percent,gpu_p95_ms,gpu_p99_ms,shadow_rays_ms,shadow_resolve_ms"
    echo "A,${control_cpu},${control_cpu_deviation},${control_gpu},${control_gpu_deviation},${control_p95},${control_p99},${control_shadow},${control_resolve}"
    echo "B,${candidate_cpu},${candidate_cpu_deviation},${candidate_gpu},${candidate_gpu_deviation},${candidate_p95},${candidate_p99},${candidate_shadow},${candidate_resolve}"
} > "${output_root}/route-summary.csv"

if [[ $pair_pass -ne 1 || $variance_pass -ne 1 || $shadow_pass -ne 1 ||
      $gpu_improvement_pass -ne 1 || $tails_pass -ne 1 ]]; then
    echo "PERFORMANCE_GATE=FAIL" > "${output_root}/status.txt"
    echo "temporal-shadow benchmark failed its native performance retention gate." >&2
    exit 4
fi

if [[ $skip_soak -eq 1 ]]; then
    echo "PERFORMANCE_GATE=INCOMPLETE_SOAK_SKIPPED" > "${output_root}/status.txt"
    echo "temporal-shadow benchmark bracket passed, but the required soak was skipped." >&2
    exit 6
fi

soak_root="${output_root}/soak"
mkdir -p "$soak_root"
capture_state "${soak_root}/before"
runtime_log="${build_dir}/logs/voxel_aquarium.log"
runtime_log_start_lines=0
if [[ -f $runtime_log ]]; then runtime_log_start_lines=$(wc -l < "$runtime_log"); fi
soak_ms=$((soak_seconds * 1000))
echo "temporal-shadow benchmark candidate thermal soak: ${soak_seconds} seconds"
soak_started_epoch=$(date +%s)
(
    cd "$build_dir"
    "$executable" \
        --no-persist-last-used \
        --assets "${repo_root}/assets" \
        --scenes "${repo_root}/scenes" \
        --scene fishbowl_perf_probe \
        --automation-window-size 1280x720 \
        --automation-hide-ui \
        --automation-foliage-renderer voxel \
        --automation-foliage-palette pastel-light-v4 \
        --automation-log-gpu-profile \
        --automation-profile-wait-for-scene-ready \
        --automation-profile-warmup-frames 120 \
        --automation-profile-sample-frames 240 \
        --automation-render-quality-preset native \
        --automation-render-scale 1.0 \
        --automation-shadow-ray-scale 1.0 \
        --automation-ao-ray-scale 1.0 \
        --automation-freeze-scene \
        --automation-fixed-scene-time-seconds 1.0 \
        --automation-sun-shadow-sampling temporal-2 \
        --auto-exit-ms "$soak_ms" \
        > "${soak_root}/stdout.txt" \
        2> "${soak_root}/stderr.txt"
)
soak_finished_epoch=$(date +%s)
soak_elapsed_seconds=$((soak_finished_epoch - soak_started_epoch))
echo "elapsed_seconds=${soak_elapsed_seconds}" > "${soak_root}/duration.txt"
if [[ -f $runtime_log ]]; then
    tail -n +$((runtime_log_start_lines + 1)) "$runtime_log" \
        > "${soak_root}/voxel_aquarium.log"
fi
for soak_evidence in "${soak_root}/stdout.txt" "${soak_root}/stderr.txt" \
    "${soak_root}/voxel_aquarium.log"; do
    if [[ ! -f $soak_evidence ]]; then
        echo "Missing soak evidence: $soak_evidence" >&2
        exit 1
    fi
done
if [[ $soak_elapsed_seconds -lt $soak_seconds ]]; then
    echo "temporal-shadow benchmark soak exited before ${soak_seconds} seconds." >&2
    exit 1
fi
require_literal "${soak_root}/stdout.txt" "Runtime content validation succeeded."
require_literal "${soak_root}/stdout.txt" "First frame rendered."
require_literal "${soak_root}/stdout.txt" "Clean shutdown."
require_literal "${soak_root}/stdout.txt" \
    "Render resolution internal=2560x1440 presentation=2560x1440 scale=1"
require_literal "${soak_root}/stdout.txt" \
    "Auxiliary ray targets: shadow=2560x1440 (scale 1), AO=2560x1440 (scale 1), scene=2560x1440."
require_literal "${soak_root}/stdout.txt" "sunShadowSampling=temporal-2"
require_literal "${soak_root}/stdout.txt" "sunShadowSamplesAuthored=4"
require_literal "${soak_root}/stdout.txt" "sunShadowSamplesEffective=2"
require_literal "${soak_root}/stdout.txt" \
    "sceneTimeFrozen=1, sceneTimeSeconds=1"
require_literal "${soak_root}/stdout.txt" "AOProjectedOverride=0"
soak_heartbeat_count=$(rg -c "Care heartbeat" "${soak_root}/stdout.txt" || true)
echo "care_heartbeat_count=${soak_heartbeat_count}" >> "${soak_root}/duration.txt"
if [[ ! $soak_heartbeat_count =~ ^[0-9]+$ || $soak_heartbeat_count -lt 10 ]]; then
    echo "temporal-shadow benchmark soak did not demonstrate a continuous frame-loop workload." >&2
    exit 1
fi
if rg -n "Validation Error:|VUID-|VMA ASSERT" \
    "${soak_root}/stdout.txt" "${soak_root}/stderr.txt" \
    "${soak_root}/voxel_aquarium.log" >/dev/null; then
    echo "Validation output detected during the temporal-shadow benchmark soak." >&2
    exit 1
fi
capture_state "${soak_root}/after"

post_root="${output_root}/post-soak-candidate"
"$benchmark_runner" \
    --runs 3 \
    --warmup-frames 120 \
    --sample-frames 240 \
    --window-size 1280x720 \
    --framebuffer-scale native \
    --render-quality-preset native \
    --render-scale 1.0 \
    --shadow-ray-scale 1.0 \
    --ao-ray-scale 1.0 \
    --scenes fishbowl_perf_probe \
    --variance-threshold 5 \
    --sun-shadow-sampling temporal-2 \
    --fixed-scene-time-seconds 1.0 \
    --skip-build \
    --output "$post_root"
validate_run "$post_root" temporal-2 2
post_normalized_effects="${post_root}/normalized-effects.txt"
post_normalized_profile="${post_root}/normalized-profile-metadata.txt"
sed -E \
    -e 's/^\[[^]]+\]\[[^]]+\]\[[^]]+\] //' \
    -e 's/sunShadowSampling=[^,]+/sunShadowSampling=ROUTE/' \
    -e 's/sunShadowSamplesEffective=[0-9]+/sunShadowSamplesEffective=ROUTE/' \
    "${post_root}/fishbowl_perf_probe/run-1/effects.txt" \
    > "$post_normalized_effects"
sed -E 's/^\[[^]]+\]\[[^]]+\]\[[^]]+\] //' \
    "${post_root}/fishbowl_perf_probe/run-1/profile-metadata.txt" \
    > "$post_normalized_profile"
if ! cmp -s "$post_normalized_effects" "$reference_effects_path"; then
    echo "A non-route renderer effect changed after the soak." >&2
    exit 1
fi
if ! cmp -s "$post_normalized_profile" "$reference_profile_path"; then
    echo "Scene/content metadata changed after the soak." >&2
    exit 1
fi
capture_state "${output_root}/post-soak-after"

post_executable_sha=$(read_metadata "${post_root}/metadata.txt" executable_sha256)
post_source_sha=$(read_metadata "${post_root}/metadata.txt" source_state_sha256)
post_shadow_sha=$(read_metadata "${post_root}/metadata.txt" shadow_ray_shader_sha256)
post_resolve_sha=$(read_metadata "${post_root}/metadata.txt" temporal_resolve_shader_sha256)
post_power_source=$(read_metadata "${post_root}/metadata.txt" power_source)
post_low_power_mode=$(read_metadata "${post_root}/metadata.txt" low_power_mode)
if [[ $post_executable_sha != "$reference_executable_sha" ||
      $post_source_sha != "$reference_source_sha" ||
      $post_shadow_sha != "$reference_shadow_sha" ||
      $post_resolve_sha != "$reference_resolve_sha" ||
      $post_power_source != "$reference_power_source" ||
      $post_low_power_mode != "$reference_low_power_mode" ]]; then
    echo "Executable, source, shader, or power state changed after the soak." >&2
    exit 1
fi

post_gpu=$(awk -F, 'NR == 2 { print $5 }' "${post_root}/summary.csv")
post_gpu_deviation=$(awk -F, 'NR == 2 { print $6 }' "${post_root}/summary.csv")
post_shadow=$(
    for run_index in 1 2 3; do
        sed -E 's/.*Shadow Rays=([0-9.]+) ms.*/\1/' \
            "${post_root}/fishbowl_perf_probe/run-${run_index}/decision-passes.txt"
    done | median_values
)
post_gpu_change=$(percent_change "$candidate_gpu" "$post_gpu")
post_shadow_change=$(percent_change "$candidate_shadow" "$post_shadow")
soak_pass=1
if ! float_le "$post_gpu_deviation" 5 ||
   ! float_le "$post_gpu_change" 5 ||
   ! float_le "$post_shadow_change" 5; then
    soak_pass=0
fi
{
    echo "metric,pre_soak,post_soak,change_percent,pass"
    echo "gpu_median_ms,${candidate_gpu},${post_gpu},${post_gpu_change},${soak_pass}"
    echo "shadow_rays_ms,${candidate_shadow},${post_shadow},${post_shadow_change},${soak_pass}"
    echo "post_gpu_max_deviation_percent,n/a,${post_gpu_deviation},n/a,${soak_pass}"
} > "${output_root}/soak-summary.csv"

if [[ $soak_pass -ne 1 ]]; then
    echo "PERFORMANCE_GATE=FAIL_POST_SOAK" > "${output_root}/status.txt"
    echo "temporal-shadow benchmark failed its post-soak thermal confirmation." >&2
    exit 5
fi

echo "PERFORMANCE_GATE=PASS_VISUAL_QA_PENDING" > "${output_root}/status.txt"
echo
echo "temporal-shadow benchmark performance and thermal gates passed."
echo "Evidence: ${output_root}"
echo "Creator-led motion and convergence QA remains required."
