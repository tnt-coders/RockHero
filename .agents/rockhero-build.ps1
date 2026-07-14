[CmdletBinding()]
param(
    # Preset names are case-sensitive and lowercase in CMakePresets.json.
    [string]$Preset = "debug",
    # Defaults to build/<preset lowercased> so alternate presets do not need a second flag.
    [string]$BuildDir = "",
    [string[]]$Targets = @(),
    [switch]$Configure,
    [string[]]$Tests = @(),
    [switch]$RunTouchedTests,
    [switch]$FullOutput
)

$ErrorActionPreference = "Stop"

if ($BuildDir -eq "")
{
    $BuildDir = "build/" + $Preset.ToLower()
}

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
        [string]$FailureMessage,
        [string]$SuccessMessage
    )

    # Windows PowerShell 5.1 turns native stderr lines into ErrorRecords under 2>&1; with the
    # script's ErrorActionPreference=Stop, harmless stderr chatter (e.g. VsDevCmd debug lines)
    # would become a terminating error even when the command exits 0. Judge by exit code only.
    $previous_preference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $output = & $Command 2>&1
    $exit_code = $LASTEXITCODE
    $ErrorActionPreference = $previous_preference
    if ($FullOutput -or $exit_code -ne 0)
    {
        $output | ForEach-Object { Write-Host $_ }
    }

    if ($exit_code -ne 0)
    {
        throw $FailureMessage
    }

    if ($SuccessMessage)
    {
        Write-Host $SuccessMessage
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

if ($FullOutput)
{
    Write-Host "CMake: $cmake"
    Write-Host "VsDevCmd: $vs_dev_cmd"
}

if ($Configure)
{
    Invoke-Checked -Command { & $cmake --preset $Preset } `
        -FailureMessage "CMake configure failed for preset '$Preset'." `
        -SuccessMessage "Configured preset '$Preset'."
}

if ($Targets.Count -gt 0)
{
    $target_text = ($Targets | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join " "
    # VsDevCmd.bat shells out to vswhere.exe from the VS Installer directory, which minimal
    # agent-shell PATHs do not include; prepend it so the developer environment can bootstrap.
    $installer_dir = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
    $build_command = 'set "PATH=' + $installer_dir + ';%PATH%" && call "' + $vs_dev_cmd +
        '" -arch=x64 -host_arch=x64 && ninja -C "' + $BuildDir + '" ' + $target_text

    Invoke-Checked -Command { & cmd.exe /d /c $build_command } `
        -FailureMessage "Ninja build failed for targets: $($Targets -join ', ')." `
        -SuccessMessage "Built targets: $($Targets -join ', ')."
}

# Each passing run records the executable's timestamp here, so -RunTouchedTests can skip suites
# whose binaries did not relink since they last passed. Ticks are stored as strings: they exceed
# the exact-integer range of JSON's double representation.
$stamp_path = Join-Path $BuildDir ".agents-test-stamps.json"
$stamps = @{}
if (Test-Path -LiteralPath $stamp_path)
{
    $stored = Get-Content -LiteralPath $stamp_path -Raw | ConvertFrom-Json
    foreach ($property in $stored.PSObject.Properties)
    {
        $stamps[$property.Name] = $property.Value
    }
}

function Get-TestStampValue
{
    param([string]$TestPath)

    return (Get-Item -LiteralPath $TestPath).LastWriteTimeUtc.Ticks.ToString()
}

if ($RunTouchedTests)
{
    # Discover every built per-library test executable instead of maintaining a hardcoded list;
    # a hardcoded list once silently omitted rock_hero_common_core_tests. "Touched" means the
    # executable changed since it last passed under this helper: an unchanged binary means none
    # of the code it tests relinked, so rerunning it cannot change the outcome.
    $discovered = Get-ChildItem -Path $BuildDir -Recurse -Filter "*_tests.exe" `
        -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch 'CMakeFiles' } |
        ForEach-Object { $_.FullName }

    if (-not $discovered)
    {
        throw "No *_tests.exe found under '$BuildDir'. Build the test targets first."
    }

    $skipped = 0
    foreach ($exe in $discovered)
    {
        if ($stamps[$exe] -eq (Get-TestStampValue -TestPath $exe))
        {
            $skipped += 1
            continue
        }

        $Tests += $exe
    }

    if ($skipped -gt 0)
    {
        Write-Host "Skipped $skipped test suite(s) unchanged since their last passing run."
    }

    if ($Tests.Count -eq 0)
    {
        Write-Host "All test suites are unchanged since their last passing run."
    }
}

foreach ($test in $Tests)
{
    if (-not (Test-Path -LiteralPath $test))
    {
        throw "Test executable not found: $test"
    }

    Invoke-Checked -Command { & $test } -FailureMessage "Test failed: $test" `
        -SuccessMessage "Passed test: $test"

    # Record the pass immediately so suites already validated stay skippable even when a later
    # suite in this run fails.
    $stamps[$test] = Get-TestStampValue -TestPath $test
    $stamps | ConvertTo-Json | Set-Content -LiteralPath $stamp_path -Encoding utf8
}
