param(
    [int]$Jobs = 8
)

$ErrorActionPreference = "Stop"

function Resolve-VsDevCmdPath {
    if ($env:VSDEVCMD_BAT -and (Test-Path -LiteralPath $env:VSDEVCMD_BAT)) {
        return $env:VSDEVCMD_BAT
    }

    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "Unable to locate VsDevCmd.bat. Set VSDEVCMD_BAT or install Visual Studio Build Tools."
}

function Import-VsDevEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$VsDevCmdPath
    )

    $setOutput = & cmd.exe /c "`"$VsDevCmdPath`" -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd.bat failed with exit code $LASTEXITCODE"
    }

    foreach ($line in $setOutput) {
        if ($line -match "^(.*?)=(.*)$") {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
        }
    }
}

function Invoke-Tool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    Write-Host ">> $FilePath $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

function Add-ToPathIfExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PathEntry
    )

    if (-not (Test-Path -LiteralPath $PathEntry)) {
        return
    }

    $currentPath = [System.Environment]::GetEnvironmentVariable("PATH")
    $pathParts = $currentPath -split ';' | Where-Object { $_ -ne "" }
    if ($pathParts -notcontains $PathEntry) {
        [System.Environment]::SetEnvironmentVariable("PATH", "$PathEntry;$currentPath")
    }
}

function Ensure-GlslangValidator {
    if (Get-Command glslangValidator -ErrorAction SilentlyContinue) {
        return
    }

    if ($env:VULKAN_SDK) {
        Add-ToPathIfExists -PathEntry (Join-Path $env:VULKAN_SDK "Bin")
        if (Get-Command glslangValidator -ErrorAction SilentlyContinue) {
            return
        }
    }

    if (-not $env:VCPKG_ROOT) {
        throw "glslangValidator was not found, and VCPKG_ROOT is not set for vcpkg fallback."
    }

    $vcpkgExe = Join-Path $env:VCPKG_ROOT "vcpkg.exe"
    if (-not (Test-Path -LiteralPath $vcpkgExe)) {
        throw "glslangValidator was not found, and vcpkg.exe is missing at $vcpkgExe"
    }

    Write-Host ">> $vcpkgExe install glslang:x64-windows --disable-metrics"
    & $vcpkgExe install glslang:x64-windows --disable-metrics
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg failed to install glslang:x64-windows with exit code $LASTEXITCODE"
    }

    Add-ToPathIfExists -PathEntry (Join-Path $env:VCPKG_ROOT "installed\x64-windows\tools\glslang")
    if (-not (Get-Command glslangValidator -ErrorAction SilentlyContinue)) {
        throw "glslangValidator is still unavailable after vcpkg installation."
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    $vsDevCmd = Resolve-VsDevCmdPath
    Import-VsDevEnvironment -VsDevCmdPath $vsDevCmd
    Ensure-GlslangValidator

    Invoke-Tool -FilePath "cmake" -Arguments @("--preset", "win-msvc-ninja-debug")
    Invoke-Tool -FilePath "cmake" -Arguments @("--build", "--preset", "build-win-debug", "-j", "$Jobs")
    Invoke-Tool -FilePath "ctest" -Arguments @("--preset", "test-win-debug")

    Invoke-Tool -FilePath "cmake" -Arguments @("--preset", "win-msvc-ninja-debug-noeditor")
    Invoke-Tool -FilePath "cmake" -Arguments @("--build", "--preset", "build-win-debug-noeditor", "-j", "$Jobs")
    Invoke-Tool -FilePath "ctest" -Arguments @("--preset", "test-win-debug-noeditor")
}
finally {
    Pop-Location
}
