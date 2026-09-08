param(
    [string]$BuildDir = "build/win-msvc-ninja-debug",
    [string]$WindowSize = "1600x900",
    [int]$Frames = 420,
    [double]$CaptureDelaySeconds = 2.25,
    [int]$Jobs = 8,
    [switch]$SkipBuild,
    [switch]$AllowValidationErrors
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuildDir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$executable = Join-Path $resolvedBuildDir "voxel_aquarium.exe"
$assetRoot = Join-Path $repoRoot "assets"
$scenesRoot = Join-Path $repoRoot "scenes"
$runtimeLog = Join-Path $resolvedBuildDir "logs\voxel_aquarium.log"
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputRoot = Join-Path $repoRoot "out\soft-voxel-lighting-parity\$timestamp"

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

if (-not ("SoftVoxelLightingParity.DpiBootstrap" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

namespace SoftVoxelLightingParity
{
    public static class DpiBootstrap
    {
        [DllImport("user32.dll")]
        public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    }
}
"@
}
[SoftVoxelLightingParity.DpiBootstrap]::SetProcessDpiAwarenessContext(
    [IntPtr](-4)) | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
if (-not ("SoftVoxelLightingParity.NativeMethods" -as [type])) {
    Add-Type -ReferencedAssemblies "System.Drawing" -TypeDefinition @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;

namespace SoftVoxelLightingParity
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

        [DllImport("user32.dll")]
        public static extern void keybd_event(
            byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
    }

    public static class ImageMetrics
    {
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

        public static double[] MeanRgb(string path, int stride)
        {
            using (var image = new Bitmap(path))
            {
                int marginX = Math.Max(stride, image.Width / 12);
                int marginY = Math.Max(stride, image.Height / 12);
                double red = 0.0;
                double green = 0.0;
                double blue = 0.0;
                long count = 0;
                for (int y = marginY; y < image.Height - marginY; y += stride)
                {
                    for (int x = marginX; x < image.Width - marginX; x += stride)
                    {
                        Color color = image.GetPixel(x, y);
                        red += color.R;
                        green += color.G;
                        blue += color.B;
                        count++;
                    }
                }
                if (count == 0)
                    return new double[] { 0.0, 0.0, 0.0 };
                return new double[] { red / count, green / count, blue / count };
            }
        }
    }
}
"@
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
        [string]$Path
    )

    [SoftVoxelLightingParity.NativeMethods+RECT]$client =
        New-Object SoftVoxelLightingParity.NativeMethods+RECT
    [SoftVoxelLightingParity.NativeMethods+POINT]$origin =
        New-Object SoftVoxelLightingParity.NativeMethods+POINT
    if (-not [SoftVoxelLightingParity.NativeMethods]::GetClientRect(
            $WindowHandle, [ref]$client) -or
        -not [SoftVoxelLightingParity.NativeMethods]::ClientToScreen(
            $WindowHandle, [ref]$origin)) {
        throw "Unable to query the runtime window client rectangle"
    }

    $clientWidth = $client.Right - $client.Left
    $clientHeight = $client.Bottom - $client.Top
    if ($clientWidth -ne $captureClientWidth -or
        $clientHeight -ne $captureClientHeight) {
        throw (
            "Runtime client is ${clientWidth}x${clientHeight}; " +
            "expected ${captureClientWidth}x${captureClientHeight}")
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

function Get-ValidationMessages {
    param([string]$StdoutPath)

    return @(
        Select-String -LiteralPath $StdoutPath -Pattern "Validation Error: \[ ([^ ]+)" `
            -AllMatches | ForEach-Object {
                $_.Matches | ForEach-Object { $_.Groups[1].Value }
            }
    )
}

$cases = @(
    [pscustomobject]@{
        Name = "daylight-shadow-fill"
        Scene = "nature_pond_probe"
        DebugMode = 0
        Motion = $false
    },
    [pscustomobject]@{
        Name = "canopy-interior-ao"
        Scene = "nature_pond_probe"
        DebugMode = 4
        Motion = $false
    },
    [pscustomobject]@{
        Name = "colored-local-on"
        Scene = "nature_pond_lighting_probe"
        DebugMode = 0
        Motion = $false
    },
    [pscustomobject]@{
        Name = "colored-local-off"
        Scene = "nature_pond_lighting_unlit_probe"
        DebugMode = 0
        Motion = $false
    },
    [pscustomobject]@{
        Name = "emissive-only-on"
        Scene = "nature_pond_lighting_probe"
        DebugMode = 13
        Motion = $false
    },
    [pscustomobject]@{
        Name = "emissive-only-off"
        Scene = "nature_pond_lighting_unlit_probe"
        DebugMode = 13
        Motion = $false
    },
    [pscustomobject]@{
        Name = "camera-motion"
        Scene = "nature_pond_lighting_probe"
        DebugMode = 0
        Motion = $true
    }
)

$results = @()
foreach ($case in $cases) {
    Write-Host ">> Lighting parity capture: $($case.Name)"
    $caseDir = Join-Path $outputRoot $case.Name
    New-Item -ItemType Directory -Force -Path $caseDir | Out-Null
    $runtimeStdout = Join-Path $caseDir "runtime-stdout.txt"
    $runtimeStderr = Join-Path $caseDir "runtime-stderr.txt"
    if (Test-Path -LiteralPath $runtimeLog) {
        Remove-Item -LiteralPath $runtimeLog -Force
    }

    $arguments = @(
        "--no-persist-last-used",
        "--scene", $case.Scene,
        "--assets", $assetRoot,
        "--scenes", $scenesRoot,
        "--automation-window-size", $WindowSize,
        "--automation-log-gpu-profile",
        "--automation-hide-ui",
        "--automation-disable-procedural-fish",
        "--automation-reconstruction-mode", "taa",
        "--automation-voxel-cell-variation", "soft-v0",
        "--automation-hemisphere-ambient", "warm-cool-v0",
        "--auto-exit-frames", "$Frames"
    )
    if (-not $case.Motion) {
        $arguments += "--automation-freeze-scene"
    }
    if ($case.DebugMode -ne 0) {
        $arguments += @(
            "--automation-lighting-debug-mode", "$($case.DebugMode)")
    }

    $process = Start-Process -FilePath $executable -ArgumentList $arguments `
        -WorkingDirectory $resolvedBuildDir -RedirectStandardOutput $runtimeStdout `
        -RedirectStandardError $runtimeStderr -PassThru
    $startupHandle = Wait-ForMainWindow -Process $process
    if ($startupHandle -eq [IntPtr]::Zero) {
        throw "Runtime window did not become available for '$($case.Name)'"
    }

    Start-Sleep -Milliseconds ([int]($CaptureDelaySeconds * 1000.0))
    $process.Refresh()
    $handle = $process.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) {
        throw "Final runtime window did not become available for '$($case.Name)'"
    }
    [SoftVoxelLightingParity.NativeMethods]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Milliseconds 150

    $captureA = Join-Path $caseDir "$($case.Name)-a.png"
    Save-ClientScreenshot -WindowHandle $handle -Path $captureA
    $captureB = ""
    if ($case.Motion) {
        # VK_D = 0x44. Capture while strafing so the pair represents a real
        # camera-transform change, not only temporal jitter or scene animation.
        [SoftVoxelLightingParity.NativeMethods]::keybd_event(
            0x44, 0, 0, [UIntPtr]::Zero)
        try {
            Start-Sleep -Milliseconds 650
            $captureB = Join-Path $caseDir "$($case.Name)-b.png"
            Save-ClientScreenshot -WindowHandle $handle -Path $captureB
        }
        finally {
            [SoftVoxelLightingParity.NativeMethods]::keybd_event(
                0x44, 0, 2, [UIntPtr]::Zero)
        }
    }

    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        throw "Runtime timed out for '$($case.Name)'"
    }
    $process.WaitForExit()
    $process.Refresh()
    $runtimeExitCode = $process.ExitCode
    if ($null -ne $runtimeExitCode -and $runtimeExitCode -ne 0) {
        throw "Runtime failed for '$($case.Name)' with exit code $runtimeExitCode"
    }
    if (-not (Test-Path -LiteralPath $runtimeLog)) {
        throw "Runtime log was not written for '$($case.Name)'"
    }

    $validationMessages = Get-ValidationMessages -StdoutPath $runtimeStdout
    $validationVuids = @($validationMessages | Sort-Object -Unique)
    if ($validationMessages.Count -gt 0 -and -not $AllowValidationErrors) {
        throw (
            "Capture '$($case.Name)' emitted Vulkan validation messages: " +
            ($validationVuids -join ", "))
    }

    $caseLog = Join-Path $caseDir "voxel_aquarium.log"
    Copy-Item -LiteralPath $runtimeLog -Destination $caseLog -Force
    $logText = Get-Content -LiteralPath $caseLog -Raw
    $gpuMatch = [regex]::Match(
        $logText,
        "GPU profile summary for scene '[^']+': avg CPU ([0-9.]+) ms, avg GPU ([0-9.]+) ms, profiledFrames=([0-9]+)")
    if (-not $gpuMatch.Success -or
        -not $logText.Contains("[INFO][App] Clean shutdown.")) {
        throw "Required runtime evidence is missing from $caseLog"
    }

    $meanRgb = [SoftVoxelLightingParity.ImageMetrics]::MeanRgb($captureA, 4)
    $results += [pscustomobject]@{
        Name = $case.Name
        Scene = $case.Scene
        DebugMode = $case.DebugMode
        Motion = $case.Motion
        AverageCpuMs = [double]$gpuMatch.Groups[1].Value
        AverageGpuMs = [double]$gpuMatch.Groups[2].Value
        ProfiledFrames = [int]$gpuMatch.Groups[3].Value
        MeanRed = $meanRgb[0]
        MeanGreen = $meanRgb[1]
        MeanBlue = $meanRgb[2]
        ValidationErrors = $validationMessages.Count
        ValidationVuids = $validationVuids -join ";"
        ScreenshotA = "$($case.Name)/$($case.Name)-a.png"
        ScreenshotB = if ($captureB) {
            "$($case.Name)/$($case.Name)-b.png"
        } else {
            ""
        }
        Log = "$($case.Name)/voxel_aquarium.log"
    }
}

function Find-Capture {
    param([string]$Name)

    $result = $results | Where-Object { $_.Name -eq $Name } | Select-Object -First 1
    return Join-Path $outputRoot $result.ScreenshotA
}

$localLightRms = [SoftVoxelLightingParity.ImageMetrics]::RmsDifference(
    (Find-Capture "colored-local-on"),
    (Find-Capture "colored-local-off"), 4)
$emissiveOnlyRms = [SoftVoxelLightingParity.ImageMetrics]::RmsDifference(
    (Find-Capture "emissive-only-on"),
    (Find-Capture "emissive-only-off"), 4)
$motionResult =
    $results | Where-Object { $_.Name -eq "camera-motion" } | Select-Object -First 1
$motionRms = [SoftVoxelLightingParity.ImageMetrics]::RmsDifference(
    (Join-Path $outputRoot $motionResult.ScreenshotA),
    (Join-Path $outputRoot $motionResult.ScreenshotB), 4)

$summary = [pscustomobject]@{
    GeneratedAt = (Get-Date).ToString("o")
    WindowSize = $WindowSize
    Frames = $Frames
    LocalLightOnOffRms = $localLightRms
    EmissiveOnlyOnOffRms = $emissiveOnlyRms
    CameraMotionRms = $motionRms
    Captures = $results
}
$results | Export-Csv -LiteralPath (Join-Path $outputRoot "captures.csv") -NoTypeInformation
$summary | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $outputRoot "summary.json")

$markdown = [System.Text.StringBuilder]::new()
[void]$markdown.AppendLine("# Lighting-Parity Matrix")
[void]$markdown.AppendLine()
[void]$markdown.AppendLine("- Window: ``$WindowSize``")
[void]$markdown.AppendLine("- Reconstruction: TAA + ``soft-v0`` cell variation")
[void]$markdown.AppendLine("- Ambient: ``warm-cool-v0``")
[void]$markdown.AppendLine("- UI and procedural fish hidden for deterministic captures")
[void]$markdown.AppendLine()
[void]$markdown.AppendLine("| Matrix row | Evidence | Automated reading |")
[void]$markdown.AppendLine("| --- | --- | --- |")
[void]$markdown.AppendLine(
    "| Daylight shadow fill | ``daylight-shadow-fill`` | Final Composite, fixed camera |")
[void]$markdown.AppendLine(
    "| Canopy/interior occlusion | ``canopy-interior-ao`` | AO Raw, fixed camera |")
[void]$markdown.AppendLine(
    "| Colored local light | ``colored-local-on`` vs ``colored-local-off`` | " +
    ("RGB RMS ``{0:F3}``" -f $localLightRms) + " |")
[void]$markdown.AppendLine(
    "| Emissive cave/fire response | ``emissive-only-on`` vs ``emissive-only-off`` | " +
    ("Emissive-only RGB RMS ``{0:F3}``" -f $emissiveOnlyRms) + " |")
[void]$markdown.AppendLine(
    "| Camera motion | ``camera-motion`` A/B | " +
    ("Real strafe RGB RMS ``{0:F3}``" -f $motionRms) + " |")
[void]$markdown.AppendLine()
[void]$markdown.AppendLine(
    "The local-light A/B measures direct colored-light contribution. The emissive-only " +
    "A/B should remain effectively invariant because local-light evaluation does not " +
    "create or modify self-emission.")
[void]$markdown.AppendLine(
    "Camera-motion RMS confirms that the captures differ; it is not a quality threshold. " +
    "Inspect the A/B pair for shimmer, shadow crawl, history trails, or detached lighting.")
[void]$markdown.AppendLine(
    "These captures do not demonstrate indirect color propagation. Any illumination on " +
    "the alcove outside the emissive-only view comes from the authored local light, " +
    "hemisphere ambient, or direct sun/moon lighting.")
$markdown.ToString() |
    Set-Content -LiteralPath (Join-Path $outputRoot "README.md")

Write-Host ""
Write-Host "Lighting-parity matrix complete: $outputRoot"
Write-Host ("Local-light on/off RMS: {0:F3}" -f $localLightRms)
Write-Host ("Emissive-only on/off RMS: {0:F3}" -f $emissiveOnlyRms)
Write-Host ("Camera-motion RMS: {0:F3}" -f $motionRms)
$results | Format-Table Name, Scene, DebugMode, AverageGpuMs, ValidationErrors
