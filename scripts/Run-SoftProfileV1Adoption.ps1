param(
    [string]$BuildDir = "build/win-msvc-ninja-debug",
    [string]$WindowSize = "1600x900",
    [int]$Frames = 720,
    [double]$CaptureDelaySeconds = 2.25,
    [int]$CameraPulseCount = 10,
    [int]$CameraPulseDownMilliseconds = 70,
    [int]$CameraPulseIntervalMilliseconds = 430,
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
$outputRoot = Join-Path $repoRoot (
    "out\soft-profile-captures\$timestamp")

if ($WindowSize -notmatch "^([0-9]+)x([0-9]+)$") {
    throw "WindowSize must use WIDTHxHEIGHT syntax, for example 1600x900"
}
$captureClientWidth = [int]$matches[1]
$captureClientHeight = [int]$matches[2]

if ($CameraPulseCount -lt 2 -or ($CameraPulseCount % 2) -ne 0) {
    throw "CameraPulseCount must be a positive even number of at least 2"
}
if ($CameraPulseDownMilliseconds -le 0 -or
    $CameraPulseIntervalMilliseconds -lt $CameraPulseDownMilliseconds) {
    throw "Camera pulse interval must be at least as long as the positive key-down time"
}

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

if (-not ("SoftProfileV1Adoption.DpiBootstrap" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

namespace SoftProfileV1Adoption
{
    public static class DpiBootstrap
    {
        [DllImport("user32.dll")]
        public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    }
}
"@
}
[SoftProfileV1Adoption.DpiBootstrap]::SetProcessDpiAwarenessContext(
    [IntPtr](-4)) | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
if (-not ("SoftProfileV1Adoption.NativeMethods" -as [type])) {
    Add-Type -ReferencedAssemblies "System.Drawing" -TypeDefinition @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;

namespace SoftProfileV1Adoption
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
        public static extern bool SetWindowPos(
            IntPtr hwnd, IntPtr hwndInsertAfter, int x, int y, int cx, int cy,
            uint flags);

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

    [SoftProfileV1Adoption.NativeMethods+RECT]$client =
        New-Object SoftProfileV1Adoption.NativeMethods+RECT
    [SoftProfileV1Adoption.NativeMethods+POINT]$origin =
        New-Object SoftProfileV1Adoption.NativeMethods+POINT
    if (-not [SoftProfileV1Adoption.NativeMethods]::GetClientRect(
            $WindowHandle, [ref]$client) -or
        -not [SoftProfileV1Adoption.NativeMethods]::ClientToScreen(
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
    $left = [Math]::Max($origin.X, $screen.WorkingArea.Left)
    $top = [Math]::Max($origin.Y, $screen.WorkingArea.Top)
    $right = [Math]::Min($origin.X + $clientWidth, $screen.WorkingArea.Right)
    $bottom = [Math]::Min($origin.Y + $clientHeight, $screen.WorkingArea.Bottom)
    $width = $right - $left
    $height = $bottom - $top
    if ($width -ne $clientWidth -or $height -ne $clientHeight) {
        throw "Runtime window must be fully visible for an exact client capture"
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

function Position-CaptureWindow {
    param(
        [Parameter(Mandatory = $true)]
        [IntPtr]$WindowHandle
    )

    $screen = [System.Windows.Forms.Screen]::FromHandle($WindowHandle)
    # keep the decorated window at the work-area origin so its client rectangle
    # cannot sit behind the taskbar. SWP_NOSIZE | SWP_NOZORDER = 0x0005.
    if (-not [SoftProfileV1Adoption.NativeMethods]::SetWindowPos(
            $WindowHandle, [IntPtr]::Zero,
            $screen.WorkingArea.Left, $screen.WorkingArea.Top,
            0, 0, 0x0005)) {
        throw "Unable to position the runtime window for an uncontaminated capture"
    }
}

function Invoke-CameraPulse {
    param([byte]$VirtualKey)

    [SoftProfileV1Adoption.NativeMethods]::keybd_event(
        $VirtualKey, 0, 0, [UIntPtr]::Zero)
    try {
        Start-Sleep -Milliseconds $CameraPulseDownMilliseconds
    }
    finally {
        [SoftProfileV1Adoption.NativeMethods]::keybd_event(
            $VirtualKey, 0, 2, [UIntPtr]::Zero)
    }
    $remainingMilliseconds =
        $CameraPulseIntervalMilliseconds - $CameraPulseDownMilliseconds
    if ($remainingMilliseconds -gt 0) {
        Start-Sleep -Milliseconds $remainingMilliseconds
    }
}

function Get-ValidationMessages {
    param(
        [string]$StdoutPath,
        [string]$StderrPath
    )

    return @(
        Select-String -LiteralPath @($StdoutPath, $StderrPath) `
            -Pattern "Validation Error: \[ ([^ ]+)" -AllMatches |
            ForEach-Object {
                $_.Matches | ForEach-Object { $_.Groups[1].Value }
            }
    )
}

function New-ContactSheet {
    param(
        [Parameter(Mandatory = $true)]
        [array]$Rows,
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $columns = @("Still", "MotionMid", "MotionEnd")
    $tileWidth = 480
    $imageHeight = 270
    $labelHeight = 26
    $sheet = New-Object System.Drawing.Bitmap(
        ($tileWidth * $columns.Count), (($imageHeight + $labelHeight) * $Rows.Count))
    $graphics = [System.Drawing.Graphics]::FromImage($sheet)
    $font = New-Object System.Drawing.Font("Segoe UI", 10)
    $brush = [System.Drawing.Brushes]::White
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(28, 31, 38))
        $graphics.InterpolationMode =
            [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        for ($rowIndex = 0; $rowIndex -lt $Rows.Count; $rowIndex++) {
            $row = $Rows[$rowIndex]
            for ($columnIndex = 0; $columnIndex -lt $columns.Count; $columnIndex++) {
                $property = $columns[$columnIndex]
                $sourcePath = Join-Path $outputRoot $row.$property
                $x = $columnIndex * $tileWidth
                $y = $rowIndex * ($imageHeight + $labelHeight)
                $image = [System.Drawing.Image]::FromFile($sourcePath)
                try {
                    $graphics.DrawImage(
                        $image, $x, $y, $tileWidth, $imageHeight)
                }
                finally {
                    $image.Dispose()
                }
                $label = "$($row.Scene) / $($property)"
                $graphics.DrawString(
                    $label, $font, $brush, $x + 5, $y + $imageHeight + 3)
            }
        }
        $sheet.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $font.Dispose()
        $graphics.Dispose()
        $sheet.Dispose()
    }
}

$cases = @(
    [pscustomobject]@{
        Scene = "nature_pond_probe"
        ExpectedProfile = "painted-outdoor-clouds-v1"
        ExpectedSource = "named-base"
        ProfileContract =
            "taa+soft-v0+warm-cool-v0+outdoor-haze-v0+painted-sky-v1+painted-clouds-v1"
    },
    [pscustomobject]@{
        Scene = "beach_sand_palettes"
        ExpectedProfile = "painted-outdoor-sky-v1"
        ExpectedSource = "named-base"
        ProfileContract =
            "taa+soft-v0+warm-cool-v0+outdoor-haze-v0+painted-sky-v1"
    },
    [pscustomobject]@{
        Scene = "procedural_world"
        ExpectedProfile = "painted-outdoor-sky-v1"
        ExpectedSource = "named-base"
        ProfileContract =
            "taa+soft-v0+warm-cool-v0+outdoor-haze-v0+painted-sky-v1"
    },
    [pscustomobject]@{
        Scene = "fishbowl_perf_probe"
        ExpectedProfile = "painted-outdoor-v3"
        ExpectedSource = "named-base"
        ProfileContract =
            "fishbowl-lighting-water-glass+painted-outdoor-v3"
    }
)

$results = @()
foreach ($case in $cases) {
    Write-Host ">> Soft Profile v1 adoption evidence: $($case.Scene)"
    $caseDir = Join-Path $outputRoot $case.Scene
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
        "--auto-exit-frames", "$Frames"
    )

    $process = Start-Process -FilePath $executable -ArgumentList $arguments `
        -WorkingDirectory $resolvedBuildDir -RedirectStandardOutput $runtimeStdout `
        -RedirectStandardError $runtimeStderr -PassThru
    try {
        $startupHandle = Wait-ForMainWindow -Process $process
        if ($startupHandle -eq [IntPtr]::Zero) {
            throw "Runtime window did not become available for '$($case.Scene)'"
        }

        Start-Sleep -Milliseconds ([int]($CaptureDelaySeconds * 1000.0))
        $process.Refresh()
        $handle = $process.MainWindowHandle
        if ($handle -eq [IntPtr]::Zero) {
            throw "Final runtime window did not become available for '$($case.Scene)'"
        }
        Position-CaptureWindow -WindowHandle $handle
        [SoftProfileV1Adoption.NativeMethods]::SetForegroundWindow($handle) | Out-Null
        Start-Sleep -Milliseconds 250

        $still = Join-Path $caseDir "$($case.Scene)-still.png"
        $motionMid = Join-Path $caseDir "$($case.Scene)-motion-mid.png"
        $motionEnd = Join-Path $caseDir "$($case.Scene)-motion-end.png"
        Save-ClientScreenshot -WindowHandle $handle -Path $still

        # VK_D = 0x44. ten brief strafe pulses spread over several seconds produce
        # slow camera motion while leaving scene animation and TAA genuinely live.
        $halfPulseCount = $CameraPulseCount / 2
        for ($pulse = 0; $pulse -lt $CameraPulseCount; $pulse++) {
            Invoke-CameraPulse -VirtualKey 0x44
            if (($pulse + 1) -eq $halfPulseCount) {
                Save-ClientScreenshot -WindowHandle $handle -Path $motionMid
            }
        }
        Save-ClientScreenshot -WindowHandle $handle -Path $motionEnd

        if (-not $process.WaitForExit(45000)) {
            throw "Runtime timed out for '$($case.Scene)'"
        }
        $process.WaitForExit()
        $process.Refresh()
        $runtimeExitCode = $process.ExitCode
        if ($null -ne $runtimeExitCode -and $runtimeExitCode -ne 0) {
            throw (
                "Runtime failed for '$($case.Scene)' with exit code " +
                $runtimeExitCode)
        }
    }
    finally {
        # release the motion key even if capture fails, then close any surviving run.
        [SoftProfileV1Adoption.NativeMethods]::keybd_event(
            0x44, 0, 2, [UIntPtr]::Zero)
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        $process.Dispose()
    }

    if (-not (Test-Path -LiteralPath $runtimeLog)) {
        throw "Runtime log was not written for '$($case.Scene)'"
    }
    $validationMessages = Get-ValidationMessages `
        -StdoutPath $runtimeStdout -StderrPath $runtimeStderr
    $validationVuids = @($validationMessages | Sort-Object -Unique)
    if ($validationMessages.Count -gt 0 -and -not $AllowValidationErrors) {
        throw (
            "Capture '$($case.Scene)' emitted Vulkan validation messages: " +
            ($validationVuids -join ", "))
    }

    $caseLog = Join-Path $caseDir "voxel_aquarium.log"
    Copy-Item -LiteralPath $runtimeLog -Destination $caseLog -Force
    $logText = Get-Content -LiteralPath $caseLog -Raw
    $profileMatch = [regex]::Match(
        $logText,
        "Applied presentation profile '([^']+)' \(source: ([^)]+)\)\.")
    $gpuMatch = [regex]::Match(
        $logText,
        "GPU profile summary for scene '[^']+': avg CPU ([0-9.]+) ms, " +
        "avg GPU ([0-9.]+) ms, profiledFrames=([0-9]+)")
    if (-not $profileMatch.Success -or -not $gpuMatch.Success -or
        -not $logText.Contains("[INFO][App] Clean shutdown.")) {
        throw "Required runtime evidence is missing from $caseLog"
    }

    $appliedProfile = $profileMatch.Groups[1].Value
    $appliedSource = $profileMatch.Groups[2].Value
    if ($appliedProfile -ne $case.ExpectedProfile -or
        $appliedSource -ne $case.ExpectedSource) {
        throw (
            "Scene '$($case.Scene)' resolved '$appliedProfile' from " +
            "'$appliedSource'; expected '$($case.ExpectedProfile)' from " +
            "'$($case.ExpectedSource)'")
    }

    $relativeRoot = $case.Scene
    $stillRelative = "$relativeRoot/$($case.Scene)-still.png"
    $midRelative = "$relativeRoot/$($case.Scene)-motion-mid.png"
    $endRelative = "$relativeRoot/$($case.Scene)-motion-end.png"
    $motionRms = [SoftProfileV1Adoption.ImageMetrics]::RmsDifference(
        (Join-Path $outputRoot $stillRelative),
        (Join-Path $outputRoot $endRelative), 4)
    if ($motionRms -le 0.1) {
        throw (
            "Scene '$($case.Scene)' did not produce measurable camera-motion " +
            "evidence (RGB RMS $motionRms)")
    }

    $results += [pscustomobject]@{
        Scene = $case.Scene
        ExpectedProfile = $case.ExpectedProfile
        AppliedProfile = $appliedProfile
        ProfileSource = $appliedSource
        ProfileContract = $case.ProfileContract
        AverageCpuMs = [double]$gpuMatch.Groups[1].Value
        AverageGpuMs = [double]$gpuMatch.Groups[2].Value
        ProfiledFrames = [int]$gpuMatch.Groups[3].Value
        CameraMotionRms = $motionRms
        ValidationErrors = $validationMessages.Count
        ValidationVuids = $validationVuids -join ";"
        Still = $stillRelative
        MotionMid = $midRelative
        MotionEnd = $endRelative
        Log = "$relativeRoot/voxel_aquarium.log"
    }
}

$gitCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
$gpuNames = @(
    Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Name -Unique
)
$summary = [pscustomobject]@{
    GeneratedAt = (Get-Date).ToString("o")
    GitCommit = $gitCommit
    BuildDirectory = $resolvedBuildDir
    WindowSize = $WindowSize
    Frames = $Frames
    CameraMotion = [pscustomobject]@{
        Key = "D"
        PulseCount = $CameraPulseCount
        KeyDownMilliseconds = $CameraPulseDownMilliseconds
        IntervalMilliseconds = $CameraPulseIntervalMilliseconds
    }
    Gpu = $gpuNames
    Captures = $results
}
$results |
    Export-Csv -LiteralPath (Join-Path $outputRoot "resolved-manifest.csv") `
        -NoTypeInformation
$summary | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $outputRoot "resolved-manifest.json")

$markdown = [System.Text.StringBuilder]::new()
[void]$markdown.AppendLine("# Soft Profile v1 Outdoor Adoption Evidence")
[void]$markdown.AppendLine()
[void]$markdown.AppendLine("- Commit: ``$gitCommit``")
[void]$markdown.AppendLine("- Window: ``$WindowSize``")
[void]$markdown.AppendLine(
    "- Camera: $CameraPulseCount short ``D`` pulses over " +
    ("{0:F2}" -f (($CameraPulseCount * $CameraPulseIntervalMilliseconds) / 1000.0)) +
    " seconds")
[void]$markdown.AppendLine("- Scene time, TAA, and authored animation remain live")
[void]$markdown.AppendLine("- No presentation-profile automation overrides are used")
[void]$markdown.AppendLine()
[void]$markdown.AppendLine(
    "| Scene | Applied profile | Source | Motion RMS | Avg GPU ms | Validation |")
[void]$markdown.AppendLine(
    "| --- | --- | --- | ---: | ---: | ---: |")
foreach ($result in $results) {
    [void]$markdown.AppendLine(
        "| ``$($result.Scene)`` | ``$($result.AppliedProfile)`` | " +
        "``$($result.ProfileSource)`` | " +
        ("{0:F3}" -f $result.CameraMotionRms) + " | " +
        ("{0:F2}" -f $result.AverageGpuMs) + " | " +
        "$($result.ValidationErrors) |")
}
[void]$markdown.AppendLine()
[void]$markdown.AppendLine(
    "Motion RMS proves that the end capture differs from the authored-camera still. " +
    "It is not a visual-quality score; inspect the still/mid/end sequence for shimmer, " +
    "trails, shadow crawl, detached lighting, water discontinuities, or state leakage.")
$markdown.ToString() | Set-Content -LiteralPath (Join-Path $outputRoot "README.md")

$contactSheetPath = Join-Path $outputRoot "contact-sheet.png"
New-ContactSheet -Rows $results -Path $contactSheetPath

$hashTargets = @(
    Get-ChildItem -LiteralPath $outputRoot -Recurse -File |
        Where-Object {
            $_.Name -ne "SHA256SUMS.txt"
        }
)
$hashLines = foreach ($file in $hashTargets) {
    $relativePath = $file.FullName.Substring($outputRoot.Length).
        TrimStart("\").Replace("\", "/")
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLower()
    "$hash  $relativePath"
}
$hashLines | Set-Content -LiteralPath (Join-Path $outputRoot "SHA256SUMS.txt")

Write-Host ""
Write-Host "Soft Profile v1 adoption evidence complete: $outputRoot"
$results |
    Format-Table Scene, AppliedProfile, ProfileSource, CameraMotionRms, `
        AverageGpuMs, ValidationErrors
