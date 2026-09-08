#!/usr/bin/env bash
# regenerates the engine-refactor structural metrics and (optionally) ratchets the
# anti-growth ceilings downward. macOS/Linux twin of scripts/refactor-metrics.ps1.
#
#   scripts/refactor-metrics.sh                  # print metrics
#   scripts/refactor-metrics.sh --update-ceiling # lower app-size-ceiling.txt to current
#   scripts/refactor-metrics.sh --update-ceiling --force  # allow raising
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src_root="$repo_root/src"
ceiling_file="$repo_root/tests/app-size-ceiling.txt"
app_cpp="$src_root/App/App.cpp"
app_h="$src_root/App/App.h"

update_ceiling=0
force=0
for arg in "$@"; do
    case "$arg" in
        --update-ceiling) update_ceiling=1 ;;
        --force) force=1 ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done

# canonical line count = number of '\n' (matches wc -l and StructuralRatchet.cmake).
count_nl() { tr -cd '\n' < "$1" | wc -c | tr -d ' '; }

# matching-line count across non-ThirdParty sources (mirrors grep -rE | wc -l).
src_files() { find "$src_root" \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) ! -path '*ThirdParty*'; }
match_count() { src_files | tr '\n' '\0' | xargs -0 grep -hE "$1" 2>/dev/null | wc -l | tr -d ' '; }
ref_count() { grep -oE "\\b$1\\b" "$app_cpp" | wc -l | tr -d ' '; }

app_cpp_lines=$(count_nl "$app_cpp")
app_h_lines=$(count_nl "$app_h")
app_methods=$(grep -cE '^[A-Za-z_].*\bApp::[A-Za-z_]+\(' "$app_cpp" || true)

printf '\nEngine Refactor — structural metrics (%s)\n' "$(date +%Y-%m-%d)"
printf -- '----------------------------------------------------------------\n'
printf '  %-30s %s\n' "App.cpp lines" "$app_cpp_lines"
printf '  %-30s %s\n' "App.h lines" "$app_h_lines"
printf '  %-30s %s\n' "App:: method definitions" "$app_methods"
printf '  %-30s %s\n' "vkCreateDescriptorSetLayout" "$(match_count 'vkCreateDescriptorSetLayout')"
printf '  %-30s %s\n' "vkCreateSampler" "$(match_count 'vkCreateSampler')"
printf '  %-30s %s\n' "vkCreateDescriptorPool" "$(match_count 'vkCreateDescriptorPool')"
printf '  %-30s %s\n' "vkCreatePipelineLayout" "$(match_count 'vkCreatePipelineLayout')"
printf '  %-30s %s\n' "vkCreateRenderPass" "$(match_count 'vkCreateRenderPass')"
printf '  %-30s %s\n' "vkDestroy* call sites" "$(match_count 'vkDestroy[A-Za-z]+')"
printf '  %-30s %s\n' "sceneConfig_ refs (App.cpp)" "$(ref_count 'sceneConfig_')"
printf '  %-30s %s\n' "voxelWorld_ refs (App.cpp)" "$(ref_count 'voxelWorld_')"
printf '  %-30s %s\n' "voxelGrid_ refs (App.cpp)" "$(ref_count 'voxelGrid_')"
printf '\nLargest hand-written translation units:\n'
find "$src_root" -name '*.cpp' ! -path '*ThirdParty*' -print0 |
    while IFS= read -r -d '' f; do printf '%6s %s\n' "$(count_nl "$f")" "${f#"$repo_root"/}"; done |
    sort -rn | head -12 | sed 's/^/  /'
printf '\n'

if [ "$update_ceiling" -eq 0 ]; then
    echo "Ceiling: re-run with --update-ceiling to ratchet app-size-ceiling.txt down to current counts."
    exit 0
fi

tmp="$(mktemp)"
changed=0
while IFS= read -r line || [ -n "$line" ]; do
    trimmed="$(printf '%s' "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
    if [ -z "$trimmed" ] || [ "${trimmed#\#}" != "$trimmed" ] || ! printf '%s' "$trimmed" | grep -q '='; then
        printf '%s\n' "$line" >> "$tmp"; continue
    fi
    key="$(printf '%s' "$trimmed" | sed 's/=.*//;s/[[:space:]]*$//')"
    old="$(printf '%s' "$trimmed" | sed 's/.*=//;s/^[[:space:]]*//')"
    case "$key" in
        src/App/App.cpp) new="$app_cpp_lines" ;;
        src/App/App.h)   new="$app_h_lines" ;;
        *) printf '%s\n' "$line" >> "$tmp"; continue ;;
    esac
    if [ "$new" -gt "$old" ] && [ "$force" -eq 0 ]; then
        echo "REFUSING to raise ceiling for $key: $old -> $new. Use --force if intended." >&2
        printf '%s\n' "$line" >> "$tmp"; continue
    fi
    if [ "$new" -ne "$old" ]; then
        echo "  $key: $old -> $new"
        printf '%s = %s\n' "$key" "$new" >> "$tmp"; changed=1
    else
        printf '%s\n' "$line" >> "$tmp"
    fi
done < "$ceiling_file"

if [ "$changed" -eq 1 ]; then
    mv "$tmp" "$ceiling_file"; echo "Updated $ceiling_file"
else
    rm -f "$tmp"; echo "No ceiling change needed."
fi
