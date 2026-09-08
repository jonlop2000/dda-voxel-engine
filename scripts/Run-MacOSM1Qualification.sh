#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/.." && pwd)

jobs=8
soak_seconds=300
preflight=0
skip_build=0
skip_validation=0
allow_dirty=0
output_root=""

usage() {
    echo "Usage: $0 [options]"
    echo "  --preflight           Validate orchestration on any Apple Silicon Mac"
    echo "  --soak-seconds N      Continuous native bank soak (default: 300)"
    echo "  --jobs N              Parallel build jobs (default: 8)"
    echo "  --skip-build          Reuse existing RelWithDebInfo executables"
    echo "  --skip-validation     Skip full Debug validation (formal run becomes incomplete)"
    echo "  --allow-dirty         Permit a dirty worktree (formal evidence records it)"
    echo "  --output DIR          Evidence directory override"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --preflight)
        preflight=1
        shift
        ;;
    --soak-seconds)
        soak_seconds=${2:-}
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
    --skip-validation)
        skip_validation=1
        shift
        ;;
    --allow-dirty)
        allow_dirty=1
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

if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
    echo "--jobs must be a positive integer." >&2
    exit 2
fi
if [[ ! $soak_seconds =~ ^[1-9][0-9]*$ ]]; then
    echo "--soak-seconds must be a positive integer." >&2
    exit 2
fi

if [[ ${DDA_VOXEL_MACOS_AUTO_SETUP:-1} != "0" &&
      ( -z ${VCPKG_ROOT:-} || -z ${VULKAN_SDK:-} ) ]]; then
    # shellcheck source=Setup-MacOSEnvironment.sh
    source "${script_dir}/Setup-MacOSEnvironment.sh"
fi

if [[ $(uname -s) != "Darwin" || $(uname -m) != "arm64" ]]; then
    echo "M1 qualification requires native Apple Silicon macOS." >&2
    exit 1
fi

for required_command in cmake plutil rg system_profiler; do
    if ! command -v "$required_command" >/dev/null 2>&1; then
        echo "Required command not found: $required_command" >&2
        exit 1
    fi
done

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

chip_type=$(profile_value "SPHardwareDataType.0.chip_type")
machine_name=$(profile_value "SPHardwareDataType.0.machine_name")
machine_model=$(profile_value "SPHardwareDataType.0.machine_model")
physical_memory=$(profile_value "SPHardwareDataType.0.physical_memory")
gpu_model=$(profile_value "SPDisplaysDataType.0.sppci_model")
gpu_cores=$(profile_value "SPDisplaysDataType.0.sppci_cores")
hardware_model=$(sysctl -n hw.model)
os_version=$(sw_vers -productVersion)

if [[ $preflight -eq 0 && $chip_type != "Apple M1"* ]]; then
    echo "M1 qualification requires real M1 hardware; found '${chip_type}'." >&2
    echo "Use --preflight to validate the workflow without producing an M1 result." >&2
    exit 1
fi

git_commit=$(git -C "$repo_root" rev-parse HEAD)
working_tree_dirty=0
if [[ -n $(git -C "$repo_root" status --porcelain) ]]; then
    working_tree_dirty=1
fi
if [[ $preflight -eq 0 && $working_tree_dirty -eq 1 && $allow_dirty -eq 0 ]]; then
    echo "Formal M1 qualification requires a clean worktree." >&2
    echo "Commit/stash changes or use --allow-dirty to record an explicitly dirty run." >&2
    exit 1
fi

if [[ -z $output_root ]]; then
    timestamp=$(date -u +"%Y%m%dT%H%M%SZ")
    if [[ $preflight -eq 1 ]]; then
        output_root="${repo_root}/build/performance/m1-qualification-preflight-${timestamp}"
    else
        output_root="${repo_root}/build/performance/m1-qualification-${timestamp}"
    fi
elif [[ $output_root != /* ]]; then
    output_root="${repo_root}/${output_root}"
fi
mkdir -p "$output_root"

qualification_mode="formal-m1"
if [[ $preflight -eq 1 ]]; then
    qualification_mode="preflight-only"
fi
validation_requested=$((1 - skip_validation))
if [[ $preflight -eq 1 ]]; then
    validation_requested=0
fi
executed_soak_seconds=$soak_seconds
if [[ $preflight -eq 1 ]]; then
    executed_soak_seconds=2
fi

{
    echo "generated_at=$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
    echo "qualification_mode=${qualification_mode}"
    echo "git_commit=${git_commit}"
    echo "working_tree_dirty=${working_tree_dirty}"
    echo "os=macOS ${os_version}"
    echo "architecture=$(uname -m)"
    echo "hardware_model=${hardware_model}"
    echo "machine_name=${machine_name}"
    echo "machine_model=${machine_model}"
    echo "chip=${chip_type}"
    echo "physical_memory=${physical_memory}"
    echo "gpu=${gpu_model}"
    echo "gpu_cores=${gpu_cores}"
    echo "soak_seconds=${executed_soak_seconds}"
    echo "formal_default_soak_seconds=${soak_seconds}"
    echo "validation_requested=${validation_requested}"
} > "${output_root}/qualification-metadata.txt"

benchmark_common=("--runs" "3")
if [[ $skip_build -eq 1 ]]; then
    benchmark_common+=("--skip-build")
fi

soak_elapsed_seconds=0
run_thermal_soak() {
    local build_dir=$1
    local executable=$2
    local duration_seconds=$3
    local soak_dir=$4

    if [[ ! -x $executable ]]; then
        echo "Thermal-soak executable not found: ${executable}" >&2
        exit 1
    fi

    mkdir -p "$soak_dir"
    pmset -g therm > "${soak_dir}/thermal-before.txt" 2>&1 || true
    local soak_start_epoch
    local soak_end_epoch
    soak_start_epoch=$(date +%s)
    echo "Running continuous native bank soak for ${duration_seconds} seconds..."
    (
        cd "$build_dir"
        "$executable" \
            --no-persist-last-used \
            --assets "${repo_root}/assets" \
            --scenes "${repo_root}/scenes" \
            --scene nature_pond_bank_probe \
            --automation-window-size 1280x720 \
            --automation-hide-ui \
            --automation-freeze-scene \
            --automation-disable-procedural-fish \
            --automation-wait-for-cloud-commit \
            --auto-exit-ms "$((duration_seconds * 1000))" \
            > "${soak_dir}/stdout.txt" 2> "${soak_dir}/stderr.txt"
    )
    soak_end_epoch=$(date +%s)
    soak_elapsed_seconds=$((soak_end_epoch - soak_start_epoch))
    pmset -g therm > "${soak_dir}/thermal-after.txt" 2>&1 || true

    if [[ $soak_elapsed_seconds -lt $duration_seconds ]]; then
        echo "Thermal soak ended early after ${soak_elapsed_seconds} seconds." >&2
        exit 1
    fi
    if ! rg -q "First frame rendered" "${soak_dir}/stdout.txt" ||
       ! rg -q "Clean shutdown" "${soak_dir}/stdout.txt"; then
        echo "Thermal soak did not render and shut down cleanly." >&2
        exit 1
    fi
    if rg -n "Validation Error:|VUID-|VMA ASSERT|FATAL|Runtime content validation failed" \
        "${soak_dir}/stdout.txt" "${soak_dir}/stderr.txt" >/dev/null; then
        echo "Validation or fatal output detected during the thermal soak." >&2
        exit 1
    fi
    echo "elapsed_seconds=${soak_elapsed_seconds}" > "${soak_dir}/elapsed.txt"
}

if [[ $preflight -eq 1 ]]; then
    preflight_output="${output_root}/preflight-native"
    "${script_dir}/Run-MacOSPerformanceBenchmark.sh" \
        "${benchmark_common[@]}" \
        --warmup-frames 2 \
        --sample-frames 4 \
        --variance-threshold 100 \
        --scenes nature_pond_probe \
        --output "$preflight_output"

    if [[ ! -s ${preflight_output}/metadata.txt ||
          ! -s ${preflight_output}/runs.csv ||
          ! -s ${preflight_output}/summary.csv ]]; then
        echo "Preflight benchmark did not produce the required evidence." >&2
        exit 1
    fi
    if ! rg -q "^chip=${chip_type}$" "${preflight_output}/metadata.txt"; then
        echo "Preflight benchmark hardware metadata does not match the qualification wrapper." >&2
        exit 1
    fi

    preflight_build_dir="${repo_root}/build/mac-ninja-relwithdebinfo-arm64"
    run_thermal_soak \
        "$preflight_build_dir" \
        "${preflight_build_dir}/voxel_aquarium" \
        "$executed_soak_seconds" \
        "${output_root}/preflight-soak"

    {
        echo "status=PREFLIGHT_ONLY"
        echo "m1_qualified=0"
        echo "reason=Workflow validated on ${chip_type}; formal support requires real M1 hardware."
        echo "benchmark=${preflight_output}"
        echo "soak_elapsed_seconds=${soak_elapsed_seconds}"
    } > "${output_root}/qualification-status.txt"

    echo
    echo "M1 preflight complete: ${output_root}"
    echo "This is not an M1 qualification result."
    exit 0
fi

if [[ $skip_validation -eq 0 ]]; then
    echo "Running complete Debug editor/no-editor validation..."
    "${script_dir}/Run-MacOSValidation.sh" --jobs "$jobs" \
        > "${output_root}/validation.log" 2>&1
fi

run_matrix() {
    local label=$1
    shift
    local matrix_output="${output_root}/${label}"
    echo "Running ${label}..."
    "${script_dir}/Run-MacOSPerformanceBenchmark.sh" \
        "${benchmark_common[@]}" \
        "$@" \
        --output "$matrix_output"
}

run_matrix initial-native-editor --framebuffer-scale native
run_matrix initial-native-noeditor --framebuffer-scale native --no-editor
run_matrix initial-1x-editor --framebuffer-scale 1x --skip-build
run_matrix initial-1x-noeditor --framebuffer-scale 1x --no-editor --skip-build

noeditor_build_dir="${repo_root}/build/mac-ninja-relwithdebinfo-arm64-noeditor"
noeditor_executable="${noeditor_build_dir}/voxel_aquarium"
if [[ ! -x $noeditor_executable ]]; then
    echo "No-editor RelWithDebInfo executable not found: ${noeditor_executable}" >&2
    exit 1
fi

soak_dir="${output_root}/thermal-soak"
run_thermal_soak \
    "$noeditor_build_dir" \
    "$noeditor_executable" \
    "$executed_soak_seconds" \
    "$soak_dir"

post_soak_output="${output_root}/post-soak-native-noeditor"
echo "Running immediate post-soak overview/bank bracket..."
"${script_dir}/Run-MacOSPerformanceBenchmark.sh" \
    --runs 3 \
    --warmup-frames 120 \
    --sample-frames 240 \
    --framebuffer-scale native \
    --no-editor \
    --skip-build \
    --scenes nature_pond_probe,nature_pond_bank_probe \
    --output "$post_soak_output"

initial_summary="${output_root}/initial-native-noeditor/summary.csv"
post_summary="${post_soak_output}/summary.csv"
comparison_csv="${output_root}/initial-vs-sustained.csv"
echo "scene,initial_gpu_median_ms,post_soak_gpu_median_ms,drift_percent,status" > "$comparison_csv"

for scene in nature_pond_probe nature_pond_bank_probe; do
    initial_gpu=$(awk -F, -v scene="$scene" 'NR > 1 && $1 == scene { print $5 }' "$initial_summary")
    post_gpu=$(awk -F, -v scene="$scene" 'NR > 1 && $1 == scene { print $5 }' "$post_summary")
    if [[ -z $initial_gpu || -z $post_gpu ]]; then
        echo "Missing initial/post-soak row for ${scene}." >&2
        exit 1
    fi
    drift=$(awk -v initial="$initial_gpu" -v post="$post_gpu" \
        'BEGIN { printf "%.3f", (post - initial) * 100.0 / initial }')
    drift_status=$(awk -v drift="$drift" \
        'BEGIN { print drift <= 5.0 ? "PASS" : "FLAG" }')
    echo "${scene},${initial_gpu},${post_gpu},${drift},${drift_status}" >> "$comparison_csv"
done

thermal_warning_status="REVIEW"
if rg -q "No thermal warning level has been recorded" \
       "${soak_dir}/thermal-after.txt" &&
   rg -q "No performance warning level has been recorded" \
       "${soak_dir}/thermal-after.txt"; then
    thermal_warning_status="PASS"
elif rg -i "warning level.*[1-9]|performance warning.*[1-9]" \
     "${soak_dir}/thermal-after.txt" >/dev/null; then
    thermal_warning_status="FLAG"
fi
sustained_status="PASS"
if rg -q ",FLAG$" "$comparison_csv"; then
    sustained_status="FLAG"
fi

qualification_status="MEASURED_M1_REVIEW_REQUIRED"
if [[ $skip_validation -eq 1 ]]; then
    qualification_status="INCOMPLETE_VALIDATION_SKIPPED"
fi

{
    echo "status=${qualification_status}"
    echo "m1_qualified=0"
    echo "reason=Evidence complete; product support requires review of native product-scene budgets and both M1 Air/Pro captures."
    echo "thermal_warning_status=${thermal_warning_status}"
    echo "sustained_drift_status=${sustained_status}"
    echo "soak_elapsed_seconds=${soak_elapsed_seconds}"
    echo "initial_vs_sustained=${comparison_csv}"
} > "${output_root}/qualification-status.txt"

echo
echo "M1 evidence capture complete: ${output_root}"
echo "Status: ${qualification_status}"
echo "No support tier is assigned automatically; review both M1 Air and Pro evidence."
