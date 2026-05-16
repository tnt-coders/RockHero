[CmdletBinding()]
param(
    [string]$Preset = "debug",
    [string]$BuildDir = "build/debug",
    [string[]]$Targets = @(),
    [switch]$Configure,
    [string[]]$Tests = @(),
    [switch]$RunTouchedTests
)

$ErrorActionPreference = "Stop"

function Find-FirstExistingPath
{
    param([string[]]$Paths)

    foreach ($path in $Paths)
    {
        if (Test-Path -LiteralPath $path)
        {
            return $path
        }
    }

    return $null
}

function Find-ClionCMake
{
    $known_paths = @(
        "C:\Program Files\JetBrains\CLion 2025.3.2\bin\cmake\win\x64\bin\cmake.exe",
        "C:\Program Files\JetBrains\CLion 2025.3.1\bin\cmake\win\x64\bin\cmake.exe",
        "C:\Program Files\JetBrains\CLion 2025.3\bin\cmake\win\x64\bin\cmake.exe"
    )

    $cmake = Find-FirstExistingPath -Paths $known_paths
    if ($cmake)
    {
        return $cmake
    }

    $jetbrains_root = "C:\Program Files\JetBrains"
    if (Test-Path -LiteralPath $jetbrains_root)
    {
        $discovered = Get-ChildItem -LiteralPath $jetbrains_root -Recurse -Filter cmake.exe `
            -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like "*\CLion *\bin\cmake\win\x64\bin\cmake.exe" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1

        if ($discovered)
        {
            return $discovered.FullName
        }
    }

    $path_cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($path_cmake)
    {
        return $path_cmake.Source
    }

    throw "Could not find CMake. Prefer CLion's bundled cmake.exe for this repository."
}

function Find-VsDevCmd
{
    $known_paths = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    )

    $vs_dev_cmd = Find-FirstExistingPath -Paths $known_paths
    if ($vs_dev_cmd)
    {
        return $vs_dev_cmd
    }

    $visual_studio_root = "C:\Program Files\Microsoft Visual Studio"
    if (Test-Path -LiteralPath $visual_studio_root)
    {
        $discovered = Get-ChildItem -LiteralPath $visual_studio_root -Recurse `
            -Filter VsDevCmd.bat -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1

        if ($discovered)
        {
            return $discovered.FullName
        }
    }

    throw "Could not find VsDevCmd.bat. Ninja builds need the MSVC developer environment."
}

function Invoke-Checked
{
    param(
        [scriptblock]$Command,
        [string]$FailureMessage
    )

    & $Command
    if ($LASTEXITCODE -ne 0)
    {
        throw $FailureMessage
    }
}

function Split-CommaSeparatedValues
{
    param([string[]]$Values)

    $split_values = @()
    foreach ($value in $Values)
    {
        $split_values += $value -split "," | ForEach-Object { $_.Trim() } |
            Where-Object { $_ -ne "" }
    }

    return $split_values
}

$Targets = Split-CommaSeparatedValues -Values $Targets
$Tests = Split-CommaSeparatedValues -Values $Tests

if (-not $Configure -and $Targets.Count -eq 0 -and $Tests.Count -eq 0 -and -not $RunTouchedTests)
{
    Write-Host "No work requested. Use -Configure, -Targets, -Tests, or -RunTouchedTests."
    exit 0
}

$cmake = Find-ClionCMake
$vs_dev_cmd = Find-VsDevCmd

Write-Host "CMake: $cmake"
Write-Host "VsDevCmd: $vs_dev_cmd"

if ($Configure)
{
    Invoke-Checked -Command { & $cmake --preset $Preset } `
        -FailureMessage "CMake configure failed for preset '$Preset'."
}

if ($Targets.Count -gt 0)
{
    $target_text = ($Targets | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join " "
    $build_command = 'call "' + $vs_dev_cmd + '" -arch=x64 -host_arch=x64 && ninja -C "' +
        $BuildDir + '" ' + $target_text

    Invoke-Checked -Command { & cmd.exe /d /c $build_command } `
        -FailureMessage "Ninja build failed for targets: $($Targets -join ', ')."
}

if ($RunTouchedTests)
{
    $Tests += @(
        "build/debug/rock-hero-common/audio/tests/rock_hero_common_audio_tests.exe",
        "build/debug/rock-hero-editor/core/tests/rock_hero_editor_core_tests.exe",
        "build/debug/rock-hero-editor/ui/tests/rock_hero_editor_ui_tests.exe"
    )
}

foreach ($test in $Tests)
{
    if (-not (Test-Path -LiteralPath $test))
    {
        throw "Test executable not found: $test"
    }

    Invoke-Checked -Command { & $test } -FailureMessage "Test failed: $test"
}
