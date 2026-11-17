[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$Test,
    [switch]$Run,
    [string]$Scene,
    [ValidateRange(1, 64)]
    [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'Install Visual Studio 2022 with the Desktop development with C++ workload.'
}
$vsRoot = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) {
    throw 'Visual Studio 2022 C++ tools were not found. Install the Desktop development with C++ workload.'
}

# Prefer the stable CMake bundled with Visual Studio, including its matching CTest.
$cmakeBin = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$cmake = Join-Path $cmakeBin 'cmake.exe'
$ctest = Join-Path $cmakeBin 'ctest.exe'
if (-not (Test-Path $cmake)) {
    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
}

if (-not $env:VULKAN_SDK) {
    $sdkCandidates = @(Join-Path $projectRoot 'build/_tools/VulkanSDK')
    foreach ($sdkParent in @('C:/VulkanSDK', 'D:/VulkanSDK')) {
        if (Test-Path $sdkParent) {
            $sdkCandidates += Get-ChildItem $sdkParent -Directory | Sort-Object Name -Descending | Select-Object -ExpandProperty FullName
        }
    }
    foreach ($sdk in $sdkCandidates) {
        if ((Test-Path "$sdk/Include/vulkan/vulkan.h") -and
            (Test-Path "$sdk/Lib/vulkan-1.lib") -and
            (Test-Path "$sdk/Bin/glslc.exe")) {
            $env:VULKAN_SDK = $sdk
            break
        }
    }
}
if (-not $env:VULKAN_SDK -or -not (Test-Path "$env:VULKAN_SDK/Bin/glslc.exe")) {
    throw 'Install the Windows Vulkan SDK from https://vulkan.lunarg.com/sdk/home and reopen PowerShell, or set VULKAN_SDK to its directory.'
}

Push-Location $projectRoot
try {
    & $cmake --preset windows
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    $preset = "windows-$($Configuration.ToLowerInvariant())"
    & $cmake --build --preset $preset --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    if ($Test) {
        & $ctest --preset $preset
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    }
    if ($Run) {
        $executable = Join-Path $projectRoot "build/windows/$Configuration/vulkcore.exe"
        $appArguments = @()
        if ($Scene) { $appArguments += @('--scene', $Scene) }
        & $executable @appArguments
        if ($LASTEXITCODE -ne 0) { throw "VulkCore exited with code $LASTEXITCODE." }
    }
} finally {
    Pop-Location
}
