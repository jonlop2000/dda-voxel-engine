param(
    [string]$BuildDir = "build/win-msvc-ninja-debug",
    [string]$Scene = "nature_pond_probe",
    [string]$WindowSize = "1600x900",
    [ValidateSet("", "disabled", "outdoor-haze-v0")]
    [string]$SceneAtmospherePreset = "",
    [int]$Frames = 360,
    [double]$CaptureDelaySeconds = 2.5,
    [int]$Jobs = 8,
    [switch]$SkipBuild,
    [switch]$NoScreenshots,
    [switch]$AllowValidationErrors
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuildDir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$executable = Join-Path $resolvedBuildDir "voxel_aquarium.exe"
$assetRoot = Join-Path $repoRoot "assets"
$scenesRoot = Join-Path $repoRoot "scenes"
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputRoot = Join-Path $repoRoot "out\soft-voxel-reconstruction\$timestamp"
$runtimeLog = Join-Path $resolvedBuildDir "logs\voxel_aquarium.log"

if ($WindowSize -notmatch "^([0-9]+)x([0-9]+)$") {
    throw "WindowSize must use WIDTHxHEIGHT syntax, for example 1600x900"
}
$captureClientWidth = [int]$matches[1]
$captureClientHeight = [int]$matches[2]

if (-not $SkipBuild) {
    & cmake --build $resolvedBuildDir --target voxel_aquarium -j $Jobs
    if ($LASTEXITCODE -ne 0) {
        throw "voxel_aquarium build failed with exit code $LASTEXITCODE"
    }
}
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Executable not found: $executable"
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

if (-not $NoScreenshots) {
    if (-not ("SoftVoxelCapture.DpiBootstrap" -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

namespace SoftVoxelCapture
{
    public static class DpiBootstrap
    {
        [DllImport("user32.dll")]
        public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    }
}
"@
    }
    # this must happen before System.Drawing or Windows Forms initializes; otherwise
    # windows can lock the PowerShell host into virtualized desktop coordinates.
    [SoftVoxelCapture.DpiBootstrap]::SetProcessDpiAwarenessContext(
        [IntPtr](-4)) | Out-Null
    Add-Type -AssemblyName System.Drawing
    if (-not ("SoftVoxelCapture.NativeMethods" -as [type])) {
        Add-Type -ReferencedAssemblies "System.Drawing" -TypeDefinition @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;

namespace SoftVoxelCapture
{
    public static class NativeMethods
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct RECT { public int Left, Top, Right, Bottom; }

        [StructLayout(LayoutKind.Sequential)]
        public struct POINT { public int X, Y; }

        [DllImport("user32.dll")]
        public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);

        [DllImport("user32.dll")]
        public static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);

        [DllImport("user32.dll")]
        public static extern bool SetForegroundWindow(IntPtr hwnd);

    }

    public static class ImageMetrics
    {
        private static double Luma(Color color)
        {
            return 0.2126 * color.R + 0.7152 * color.G + 0.0722 * color.B;
        }

        public static double RmsDifference(string firstPath, string secondPath, int stride)
        {
            using (var first = new Bitmap(firstPath))
            using (var second = new Bitmap(secondPath))
            {
                int width = Math.Min(first.Width, second.Width);
                int height = Math.Min(first.Height, second.Height);
                int marginX = Math.Max(stride, width / 12);
                int marginY = Math.Max(stride, height / 12);
                double sum = 0.0;
                long count = 0;
                for (int y = marginY; y < height - marginY; y += stride)
                {
                    for (int x = marginX; x < width - marginX; x += stride)
                    {
                        Color a = first.GetPixel(x, y);
                        Color b = second.GetPixel(x, y);
                        double dr = a.R - b.R;
                        double dg = a.G - b.G;
                        double db = a.B - b.B;
                        sum += dr * dr + dg * dg + db * db;
                        count += 3;
                    }
                }
                return count > 0 ? Math.Sqrt(sum / count) : 0.0;
            }
        }

        public static double EdgeEnergy(string path, int stride)
        {
            using (var image = new Bitmap(path))
            {
                int marginX = Math.Max(stride, image.Width / 12);
                int marginY = Math.Max(stride, image.Height / 12);
                double sum = 0.0;
                long count = 0;
                for (int y = marginY; y + 1 < image.Height - marginY; y += stride)
                {
                    for (int x = marginX; x + 1 < image.Width - marginX; x += stride)
                    {
                        double center = Luma(image.GetPixel(x, y));
                        sum += Math.Abs(center - Luma(image.GetPixel(x + 1, y)));
                        sum += Math.Abs(center - Luma(image.GetPixel(x, y + 1)));
                        count += 2;
                    }
                }
                return count > 0 ? sum / count : 0.0;
            }
        }
    }
}
"@
    }
    Add-Type -AssemblyName System.Windows.Forms
}

function Wait-ForMainWindow {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutMs = 12000
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) {
            return [IntPtr]::Zero
        }
        $Process.Refresh()
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            return $Process.MainWindowHandle
        }
        Start-Sleep -Milliseconds 100
    }
    return [IntPtr]::Zero
}

function Save-ClientScreenshot {
    param(
        [Parameter(Mandatory = $true)]
        [IntPtr]$WindowHandle,
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [int]$ExpectedWidth,
        [Parameter(Mandatory = $true)]
        [int]$ExpectedHeight
    )

    [SoftVoxelCapture.NativeMethods+RECT]$client = New-Object SoftVoxelCapture.NativeMethods+RECT
    [SoftVoxelCapture.NativeMethods+POINT]$origin = New-Object SoftVoxelCapture.NativeMethods+POINT
    if (-not [SoftVoxelCapture.NativeMethods]::GetClientRect($WindowHandle, [ref]$client) -or
        -not [SoftVoxelCapture.NativeMethods]::ClientToScreen($WindowHandle, [ref]$origin)) {
        throw "Unable to query the runtime window client rectangle"
    }

    $clientWidth = $client.Right - $client.Left
    $clientHeight = $client.Bottom - $client.Top
    if ($clientWidth -ne $ExpectedWidth -or $clientHeight -ne $ExpectedHeight) {
        throw (
            "Runtime client is ${clientWidth}x${clientHeight}; " +
            "expected ${ExpectedWidth}x${ExpectedHeight}")
    }

    $screen = [System.Windows.Forms.Screen]::FromHandle($WindowHandle)
    $left = [Math]::Max($origin.X, $screen.Bounds.Left)
    $top = [Math]::Max($origin.Y, $screen.Bounds.Top)
    $right = [Math]::Min($origin.X + $clientWidth, $screen.Bounds.Right)
    $bottom = [Math]::Min($origin.Y + $clientHeight, $screen.Bounds.Bottom)
    $width = $right - $left
    $height = $bottom - $top
    if ($width -le 0 -or $height -le 0) {
        throw "Runtime window is not visible on the selected screen"
    }

    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($left, $top, 0, 0, $bitmap.Size)
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Get-PassMilliseconds {
    param(
        [string]$DecisionLine,
        [string]$Label
    )

    $match = [regex]::Match(
        $DecisionLine,
        [regex]::Escape($Label) + "=([0-9.]+) ms")
    if ($match.Success) {
        return [double]$match.Groups[1].Value
    }
    return $null
}

$modes = @(
    [pscustomobject]@{ Name = "no-aa"; Taa = 0; Jitter = 0; Fxaa = 0 },
    [pscustomobject]@{ Name = "fxaa"; Taa = 0; Jitter = 0; Fxaa = 1 },
    [pscustomobject]@{ Name = "taa"; Taa = 1; Jitter = 1; Fxaa = 0 },
    [pscustomobject]@{ Name = "taa-fxaa"; Taa = 1; Jitter = 1; Fxaa = 1 }
)
$results = @()

foreach ($mode in $modes) {
    Write-Host ">> Soft Profile v0 reconstruction mode: $($mode.Name)"
    $modeDir = Join-Path $outputRoot $mode.Name
    New-Item -ItemType Directory -Force -Path $modeDir | Out-Null
    $runtimeStdout = Join-Path $modeDir "runtime-stdout.txt"
    $runtimeStderr = Join-Path $modeDir "runtime-stderr.txt"
    if (Test-Path -LiteralPath $runtimeLog) {
        Remove-Item -LiteralPath $runtimeLog -Force
    }

    $arguments = @(
        "--no-persist-last-used",
        "--scene", $Scene,
        "--assets", $assetRoot,
        "--scenes", $scenesRoot,
        "--automation-window-size", $WindowSize,
        "--automation-log-gpu-profile",
        "--automation-hide-ui",
        "--automation-freeze-scene",
        "--automation-reconstruction-mode", $mode.Name,
        "--auto-exit-frames", "$Frames"
    )
    if ($SceneAtmospherePreset -ne "") {
        $arguments += @(
            "--automation-scene-atmosphere", $SceneAtmospherePreset
        )
    }
    $process = Start-Process -FilePath $executable -ArgumentList $arguments `
        -WorkingDirectory $resolvedBuildDir -RedirectStandardOutput $runtimeStdout `
        -RedirectStandardError $runtimeStderr -PassThru

    $captureA = $null
    $captureB = $null
    if (-not $NoScreenshots) {
        $startupHandle = Wait-ForMainWindow -Process $process
        if ($startupHandle -eq [IntPtr]::Zero) {
            throw "Runtime window did not become available for mode '$($mode.Name)'"
        }
        # the engine replaces its temporary startup window with the final glfw window.
        # reacquire the handle after initialization instead of capturing the stale one.
        Start-Sleep -Milliseconds ([int]($CaptureDelaySeconds * 1000.0))
        $process.Refresh()
        $handle = $process.MainWindowHandle
        if ($handle -eq [IntPtr]::Zero) {
            throw "Final runtime window did not become available for mode '$($mode.Name)'"
        }
        [SoftVoxelCapture.NativeMethods]::SetForegroundWindow($handle) | Out-Null
        Start-Sleep -Milliseconds 150
        $captureA = Join-Path $modeDir "$($mode.Name)-a.png"
        Save-ClientScreenshot -WindowHandle $handle -Path $captureA `
            -ExpectedWidth $captureClientWidth -ExpectedHeight $captureClientHeight
        Start-Sleep -Milliseconds 750
        $captureB = Join-Path $modeDir "$($mode.Name)-b.png"
        Save-ClientScreenshot -WindowHandle $handle -Path $captureB `
            -ExpectedWidth $captureClientWidth -ExpectedHeight $captureClientHeight
    }

    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        throw "Runtime timed out for mode '$($mode.Name)'"
    }
    # complete asynchronous stdout/stderr draining before PowerShell reads ExitCode.
    $process.WaitForExit()
    $process.Refresh()
    $runtimeExitCode = $process.ExitCode
    if ($null -ne $runtimeExitCode -and $runtimeExitCode -ne 0) {
        throw "Runtime failed for mode '$($mode.Name)' with exit code $runtimeExitCode"
    }
    if (-not (Test-Path -LiteralPath $runtimeLog)) {
        throw "Runtime log was not written for mode '$($mode.Name)'"
    }
    $validationMatches = @(
        Select-String -LiteralPath $runtimeStdout -Pattern "Validation Error: \[ ([^ ]+)" `
            -AllMatches | ForEach-Object {
                $_.Matches | ForEach-Object { $_.Groups[1].Value }
            }
    )
    $validationVuids = @($validationMatches | Sort-Object -Unique)
    if ($validationMatches.Count -gt 0) {
        Write-Warning (
            "Mode '$($mode.Name)' emitted $($validationMatches.Count) validation-layer " +
            "message(s): $($validationVuids -join ', ')")
        if (-not $AllowValidationErrors) {
            throw (
                "Mode '$($mode.Name)' failed the Vulkan validation gate. " +
                "Use -AllowValidationErrors only for diagnostic evidence collection.")
        }
    }

    $modeLog = Join-Path $modeDir "voxel_aquarium.log"
    Copy-Item -LiteralPath $runtimeLog -Destination $modeLog -Force
    $logText = Get-Content -LiteralPath $modeLog -Raw
    $profilePattern =
        "Soft Profile v0 reconstruction mode '$([regex]::Escape($mode.Name))' applied: " +
        "profile='([^']+)', taa=([01]), jitter=([01]), fxaa=([01]), " +
        "requestedSunRadius=([0-9]+(?:\.[0-9]+)?), " +
        "effectiveSunRadius=([0-9]+(?:\.[0-9]+)?)"
    $profileMatch = [regex]::Match($logText, $profilePattern)
    $gpuMatch = [regex]::Match(
        $logText,
        "GPU profile summary for scene '[^']+': avg CPU ([0-9.]+) ms, avg GPU ([0-9.]+) ms, profiledFrames=([0-9]+)")
    $decisionMatch = [regex]::Match($logText, "Decision GPU passes:([^\r\n]+)")
    if (-not $profileMatch.Success -or -not $gpuMatch.Success -or
        -not $decisionMatch.Success -or
        -not $logText.Contains("[INFO][App] Clean shutdown.")) {
        throw "Required comparison evidence is missing from $modeLog"
    }
    if ($SceneAtmospherePreset -ne "" -and
        -not $logText.Contains(
            "[INFO][Automation] Scene atmosphere preset '$SceneAtmospherePreset' applied:")) {
        throw "Scene-atmosphere automation evidence is missing from $modeLog"
    }

    $temporalRms = $null
    $edgeEnergy = $null
    if ($captureA -and $captureB) {
        $temporalRms = [SoftVoxelCapture.ImageMetrics]::RmsDifference(
            $captureA, $captureB, 4)
        $edgeEnergy = [SoftVoxelCapture.ImageMetrics]::EdgeEnergy($captureB, 4)
    }

    $results += [pscustomobject]@{
        Mode = $mode.Name
        Profile = $profileMatch.Groups[1].Value
        Taa = [int]$profileMatch.Groups[2].Value
        Jitter = [int]$profileMatch.Groups[3].Value
        Fxaa = [int]$profileMatch.Groups[4].Value
        RequestedSunRadius = [double]$profileMatch.Groups[5].Value
        EffectiveSunRadius = [double]$profileMatch.Groups[6].Value
        AverageCpuMs = [double]$gpuMatch.Groups[1].Value
        AverageGpuMs = [double]$gpuMatch.Groups[2].Value
        ProfiledFrames = [int]$gpuMatch.Groups[3].Value
        TaaPassMs = Get-PassMilliseconds $decisionMatch.Value "TAA"
        CompositePassMs = Get-PassMilliseconds $decisionMatch.Value "Composite+UI"
        TemporalRms = $temporalRms
        EdgeEnergy = $edgeEnergy
        ValidationErrors = $validationMatches.Count
        ValidationVuids = $validationVuids -join ";"
        ScreenshotA = if ($captureA) { "$($mode.Name)/$($mode.Name)-a.png" } else { "" }
        ScreenshotB = if ($captureB) { "$($mode.Name)/$($mode.Name)-b.png" } else { "" }
        Log = "$($mode.Name)/voxel_aquarium.log"
    }
}

$csvPath = Join-Path $outputRoot "summary.csv"
$jsonPath = Join-Path $outputRoot "summary.json"
$markdownPath = Join-Path $outputRoot "README.md"
$results | Export-Csv -LiteralPath $csvPath -NoTypeInformation
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $jsonPath

$markdown = [System.Text.StringBuilder]::new()
[void]$markdown.AppendLine("# Soft Profile v0 Reconstruction Comparison")
[void]$markdown.AppendLine()
[void]$markdown.AppendLine("- Scene: ``$Scene``")
[void]$markdown.AppendLine("- Window: ``$WindowSize``")
[void]$markdown.AppendLine("- Frames: ``$Frames``")
[void]$markdown.AppendLine(
    "- Scene atmosphere: ``$(if ($SceneAtmospherePreset -eq '') { 'scene/profile default' } else { $SceneAtmospherePreset })``")
[void]$markdown.AppendLine("- Camera/time frozen; TAA jitter preserved")
[void]$markdown.AppendLine("- Pixel presentation disabled in every mode")
[void]$markdown.AppendLine()
[void]$markdown.AppendLine("| Mode | TAA | Jitter | FXAA | Avg GPU ms | TAA ms | Composite ms | Frame RMS | Edge energy | Validation messages |")
[void]$markdown.AppendLine("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
foreach ($result in $results) {
    $taaMs = if ($null -eq $result.TaaPassMs) { "n/a" } else { "{0:F2}" -f $result.TaaPassMs }
    $compositeMs = if ($null -eq $result.CompositePassMs) { "n/a" } else { "{0:F2}" -f $result.CompositePassMs }
    $rms = if ($null -eq $result.TemporalRms) { "n/a" } else { "{0:F3}" -f $result.TemporalRms }
    $edge = if ($null -eq $result.EdgeEnergy) { "n/a" } else { "{0:F3}" -f $result.EdgeEnergy }
    [void]$markdown.AppendLine(
        "| $($result.Mode) | $($result.Taa) | $($result.Jitter) | $($result.Fxaa) | " +
        ("{0:F2}" -f $result.AverageGpuMs) +
        " | $taaMs | $compositeMs | $rms | $edge | $($result.ValidationErrors) |")
}
[void]$markdown.AppendLine()
[void]$markdown.AppendLine(
    "Frame RMS is a sampled RGB difference between two captures 750 ms apart; lower is more stable.")
[void]$markdown.AppendLine(
    "Both image metrics exclude an 8.3% border to avoid desktop/GPU-overlay contamination.")
[void]$markdown.AppendLine(
    "Edge energy is a sampled local luminance gradient; it helps detect blur but is not a standalone quality score.")
[void]$markdown.AppendLine(
    "Automated metrics do not replace visual acceptance of shimmer, silhouette quality, or trails.")
$markdown.ToString() | Set-Content -LiteralPath $markdownPath

Write-Host ""
Write-Host "Comparison complete: $outputRoot"
$results | Format-Table Mode, Taa, Jitter, Fxaa, AverageGpuMs, TaaPassMs, CompositePassMs, TemporalRms, EdgeEnergy, ValidationErrors
