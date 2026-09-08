<#
.SYNOPSIS
    prints source metrics and optionally lowers the line limits.

.DESCRIPTION
    use -UpdateCeiling to update tests/app-size-ceiling.txt.
    increasing a limit also requires -Force.

.EXAMPLE
    pwsh scripts/refactor-metrics.ps1
    pwsh scripts/refactor-metrics.ps1 -UpdateCeiling
#>
param(
    [switch]$UpdateCeiling,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$srcRoot = Join-Path $repoRoot "src"
$ceilingFile = Join-Path $repoRoot "tests/app-size-ceiling.txt"

# canonical line count = number of '\n' (identical to `wc -l` and to StructuralRatchet.cmake).
function Get-LineCount([string]$path) {
    $text = [System.IO.File]::ReadAllText($path)
    return $text.Length - $text.Replace("`n", "").Length
}

# count matching lines across all source files (mirrors `grep -rE pattern src/ | wc -l`).
function Get-MatchCount([string]$pattern, [string[]]$files) {
    $total = 0
    foreach ($f in $files) {
        $m = Select-String -LiteralPath $f -Pattern $pattern -ErrorAction SilentlyContinue
        if ($m) { $total += @($m).Count }
    }
    return $total
}

$cppFiles = Get-ChildItem -Path $srcRoot -Recurse -Include *.cpp, *.h, *.hpp |
    Where-Object { $_.FullName -notmatch "ThirdParty" }
$cppPaths = $cppFiles | ForEach-Object { $_.FullName }

$appCpp = Join-Path $srcRoot "App/App.cpp"
$appH = Join-Path $srcRoot "App/App.h"

# --- tracked-file line counts (these feed the ratchet) ---
$appCppLines = Get-LineCount $appCpp
$appHLines = Get-LineCount $appH

# --- structural metrics ---
$appMethods = @(Select-String -LiteralPath $appCpp -Pattern '^[A-Za-z_].*\bApp::[A-Za-z_]+\(').Count
$vkCreateDSL = Get-MatchCount 'vkCreateDescriptorSetLayout' $cppPaths
$vkCreateSampler = Get-MatchCount 'vkCreateSampler' $cppPaths
$vkCreateDP = Get-MatchCount 'vkCreateDescriptorPool' $cppPaths
$vkCreatePL = Get-MatchCount 'vkCreatePipelineLayout' $cppPaths
$vkCreateRP = Get-MatchCount 'vkCreateRenderPass' $cppPaths
$vkDestroy = Get-MatchCount 'vkDestroy[A-Za-z]+' $cppPaths

# --- coupling reference counts in App.cpp ---
function Get-RefCount([string]$symbol) {
    $m = Select-String -LiteralPath $appCpp -Pattern ("\b" + [regex]::Escape($symbol) + "\b") -AllMatches
    $c = 0
    foreach ($line in $m) { $c += $line.Matches.Count }
    return $c
}
$sceneConfigRefs = Get-RefCount 'sceneConfig_'
$voxelWorldRefs = Get-RefCount 'voxelWorld_'
$voxelGridRefs = Get-RefCount 'voxelGrid_'

# --- Largest translation units ---
$largest = $cppFiles | Where-Object { $_.Extension -eq ".cpp" } |
    ForEach-Object { [PSCustomObject]@{ Path = $_.FullName.Substring($repoRoot.Length + 1); Lines = (Get-LineCount $_.FullName) } } |
    Sort-Object Lines -Descending | Select-Object -First 12

Write-Host ""
Write-Host "Engine Refactor - structural metrics ($(Get-Date -Format yyyy-MM-dd))"
Write-Host "----------------------------------------------------------------"
Write-Host ("  App.cpp lines                  {0}" -f $appCppLines)
Write-Host ("  App.h lines                    {0}" -f $appHLines)
Write-Host ("  App:: method definitions       {0}" -f $appMethods)
Write-Host ("  vkCreateDescriptorSetLayout    {0}" -f $vkCreateDSL)
Write-Host ("  vkCreateSampler                {0}" -f $vkCreateSampler)
Write-Host ("  vkCreateDescriptorPool         {0}" -f $vkCreateDP)
Write-Host ("  vkCreatePipelineLayout         {0}" -f $vkCreatePL)
Write-Host ("  vkCreateRenderPass             {0}" -f $vkCreateRP)
Write-Host ("  vkDestroy* call sites          {0}" -f $vkDestroy)
Write-Host ("  sceneConfig_ refs (App.cpp)    {0}" -f $sceneConfigRefs)
Write-Host ("  voxelWorld_ refs (App.cpp)     {0}" -f $voxelWorldRefs)
Write-Host ("  voxelGrid_ refs (App.cpp)      {0}" -f $voxelGridRefs)
Write-Host ""
Write-Host "Largest hand-written translation units:"
foreach ($tu in $largest) {
    Write-Host ("  {0,6}  {1}" -f $tu.Lines, ($tu.Path -replace '\\', '/'))
}
Write-Host ""

if (-not $UpdateCeiling) {
    Write-Host "Ceiling: re-run with -UpdateCeiling to ratchet tests/app-size-ceiling.txt down to current counts."
    return
}

# --- ratchet the ceiling down ---
$tracked = @{ "src/App/App.cpp" = $appCppLines; "src/App/App.h" = $appHLines }
$lines = [System.IO.File]::ReadAllLines($ceilingFile)
$out = New-Object System.Collections.Generic.List[string]
$changed = $false

foreach ($line in $lines) {
    $trimmed = $line.Trim()
    if ($trimmed -eq "" -or $trimmed.StartsWith("#") -or ($trimmed -notmatch '^(.+?)=(.+)$')) {
        $out.Add($line); continue
    }
    $key = $matches[1].Trim()
    $oldVal = [int]($matches[2].Trim())
    if (-not $tracked.ContainsKey($key)) { $out.Add($line); continue }
    $newVal = $tracked[$key]
    if ($newVal -gt $oldVal -and -not $Force) {
        Write-Host ("REFUSING to raise ceiling for {0}: {1} -> {2}. Growing App must be deliberate; re-run with -Force if intended." -f $key, $oldVal, $newVal) -ForegroundColor Yellow
        $out.Add($line); continue
    }
    if ($newVal -ne $oldVal) {
        Write-Host ("  {0}: {1} -> {2}" -f $key, $oldVal, $newVal)
        $out.Add("$key = $newVal"); $changed = $true
    } else {
        $out.Add($line)
    }
}

if ($changed) {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($ceilingFile, $out, $utf8NoBom)
    Write-Host "Updated $ceilingFile"
} else {
    Write-Host "No ceiling change needed."
}
