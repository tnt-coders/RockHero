# Swaps a note-atlas variant into the DEPLOYED resource trees so a look can be sighted in the app
# without a rebuild. Both products load `resources/textures/notes.png` from beside their executable
# at renderer-create time, so deploying there and re-opening the editor's 3D preview (F3 off, F3 on)
# picks the new art up immediately.
#
# Why this exists rather than "copy the file yourself": Copy-Item PRESERVES the source timestamp, so
# a variant copied over a newer deployed file can be silently ignored by the build's staging step and
# by anything that compares mtimes -- the swap appears to work and nothing changes on screen. This
# stamps the write time and then verifies both deployed copies by SHA256, so a swap either provably
# happened or fails loudly.
#
# The SOURCE tree is deliberately left alone: a rebuild therefore restores the committed atlas, and
# no sighting can quietly become the shipped art. -List reports all three hashes so that drift is
# always visible.
#
#   .agents\atlas-variant.ps1 -List
#   .agents\atlas-variant.ps1 -Name diamond-a1
#   .agents\atlas-variant.ps1 -Path C:\somewhere\notes-experiment.png
#   .agents\atlas-variant.ps1 -Restore
#   .agents\atlas-variant.ps1 -Stage marks-pitch-tangent,marks-pitch-107,marks-pitch-110
#   .agents\atlas-variant.ps1 -Unstage
#
# -Stage copies the named variants BESIDE notes.png as notes-variant-<name>.png in both deployed
# trees; the editor's F6 sampler (Cycle Note Atlas Variant) then swaps between the shipped atlas
# and the staged files on a key, with no script round trip per look. -Unstage removes them all.
#
# Windows PowerShell 5.1 compatible.

[CmdletBinding(DefaultParameterSetName = "List")]
param(
    [Parameter(ParameterSetName = "Name", Mandatory = $true)]
    [string]$Name,

    [Parameter(ParameterSetName = "Path", Mandatory = $true)]
    [string]$Path,

    [Parameter(ParameterSetName = "Restore", Mandatory = $true)]
    [switch]$Restore,

    [Parameter(ParameterSetName = "Stage", Mandatory = $true)]
    [string[]]$Stage,

    [Parameter(ParameterSetName = "Unstage", Mandatory = $true)]
    [switch]$Unstage,

    [Parameter(ParameterSetName = "List")]
    [switch]$List,

    # Build preset whose deployed trees are targeted.
    [string]$Preset = "debug"
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$source_atlas = Join-Path $repo "rock-hero-common\ui\resources\textures\notes.png"
$variants_dir = "C:\__MAIN__\Coding\__scratch__\rockhero-atlas-variants"

# Every deployed copy the two products read at startup. Both must move together or the editor's
# preview and the game would show different art from the same working tree.
#
# The editor's path is DISCOVERED rather than spelled out: JUCE names its artefacts subfolder
# after the build CONFIG (Debug, Release, RelWithDebInfo), which is not the preset name, so a
# hardcoded folder silently targeted the wrong tree for every non-debug preset -- the swap
# reported success against a debug build while the running RelWithDebInfo editor never changed.
# Globbing asks the filesystem instead of maintaining a preset-to-config table that must agree
# with CMake by hand.
$editor_artefacts = Join-Path $repo "build\$Preset\rock-hero-editor\app\rock_hero_editor_exe_artefacts"
$editor_target = $null
if (Test-Path $editor_artefacts)
{
    $editor_target = Get-ChildItem -Path $editor_artefacts -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName "resources\textures\notes.png" } |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1
}
if ($null -eq $editor_target)
{
    # Keep a concrete path for the error messages below rather than a null.
    $editor_target = Join-Path $editor_artefacts "<config>\resources\textures\notes.png"
}

$targets = @(
    $editor_target,
    (Join-Path $repo "build\$Preset\rock-hero-game\app\resources\textures\notes.png")
)

function Get-Sha256([string]$file)
{
    if (-not (Test-Path $file)) { return $null }
    return (Get-FileHash -Path $file -Algorithm SHA256).Hash.Substring(0, 12).ToLowerInvariant()
}

function Show-State
{
    Write-Host ""
    Write-Host "preset: $Preset   (pass -Preset to target another build tree)"
    Write-Host "deployed atlases (sha256 prefix):"
    Write-Host ("  {0,-10} {1}" -f "source", (Get-Sha256 $source_atlas))
    $i = 0
    foreach ($t in $targets)
    {
        $label = if ($i -eq 0) { "editor" } else { "game" }
        $hash = Get-Sha256 $t
        $mark = if ($null -eq $hash) { "  (MISSING - build once to create it)" } else { "" }
        Write-Host ("  {0,-10} {1}{2}" -f $label, $hash, $mark)
        $i++
    }

    $staged_dir = Split-Path -Parent $targets[0]
    if (Test-Path $staged_dir)
    {
        $staged = Get-ChildItem -Path $staged_dir -Filter "notes-variant-*.png" -ErrorAction SilentlyContinue
        Write-Host ""
        if ($null -eq $staged -or $staged.Count -eq 0)
        {
            Write-Host "staged for the editor's F6 sampler: (none - use -Stage <names>)"
        }
        else
        {
            Write-Host "staged for the editor's F6 sampler (cycled in name order):"
            foreach ($s in $staged)
            {
                Write-Host ("  {0,-40} {1}" -f $s.Name, (Get-Sha256 $s.FullName))
            }
        }
    }

    if (Test-Path $variants_dir)
    {
        $deployed = Get-Sha256 $targets[0]
        Write-Host ""
        Write-Host "variants in $variants_dir :"
        $found = Get-ChildItem -Path $variants_dir -Filter "*.png" -ErrorAction SilentlyContinue
        if ($null -eq $found -or $found.Count -eq 0)
        {
            Write-Host "  (none)"
        }
        foreach ($v in $found)
        {
            $vh = Get-Sha256 $v.FullName
            $active = if ($vh -eq $deployed) { "  <== DEPLOYED" } else { "" }
            Write-Host ("  {0,-28} {1}{2}" -f $v.BaseName, $vh, $active)
        }
    }
    else
    {
        Write-Host ""
        Write-Host "variants dir does not exist yet: $variants_dir"
    }
    Write-Host ""
}

function Deploy([string]$file, [string]$label)
{
    if (-not (Test-Path $file))
    {
        throw "no such atlas: $file"
    }
    $expected = Get-Sha256 $file

    foreach ($t in $targets)
    {
        $dir = Split-Path -Parent $t
        if (-not (Test-Path $dir))
        {
            throw "deployed resource tree missing: $dir (build the $Preset preset once first)"
        }
        Copy-Item -Path $file -Destination $t -Force
        # Copy-Item carries the source's timestamp across; stamp it so staging and mtime checks see
        # a genuinely newer file. This is the whole reason the script exists.
        (Get-Item $t).LastWriteTime = Get-Date

        $actual = Get-Sha256 $t
        if ($actual -ne $expected)
        {
            throw "verification FAILED for $t (expected $expected, got $actual)"
        }
    }

    Write-Host ""
    Write-Host "deployed '$label' ($expected) to both products - verified by hash."
    Write-Host "toggle the editor's 3D preview off and on (F3, F3) to reload it. No rebuild needed."
    Write-Host ""
}

switch ($PSCmdlet.ParameterSetName)
{
    "Name"
    {
        $candidate = Join-Path $variants_dir "$Name.png"
        Deploy $candidate $Name
    }
    "Path"
    {
        Deploy $Path (Split-Path -Leaf $Path)
    }
    "Restore"
    {
        # The committed atlas, taken from the source tree rather than from git, so a working-tree
        # rebake in progress is what gets restored rather than being silently reverted.
        Deploy $source_atlas "source tree"
    }
    "Stage"
    {
        foreach ($t in $targets)
        {
            $dir = Split-Path -Parent $t
            if (-not (Test-Path $dir))
            {
                throw "deployed resource tree missing: $dir (build the $Preset preset once first)"
            }
            foreach ($n in $Stage)
            {
                $candidate = Join-Path $variants_dir "$n.png"
                if (-not (Test-Path $candidate))
                {
                    throw "no such variant: $candidate"
                }
                $dest = Join-Path $dir "notes-variant-$n.png"
                Copy-Item -Path $candidate -Destination $dest -Force
                (Get-Item $dest).LastWriteTime = Get-Date
            }
        }
        Write-Host ""
        Write-Host ("staged {0} variant(s) beside both products' notes.png." -f $Stage.Count)
        Write-Host "cycle them in the editor with F6 while the 3D preview is open; row 0 is the shipped atlas."
        Write-Host ""
    }
    "Unstage"
    {
        $removed = 0
        foreach ($t in $targets)
        {
            $dir = Split-Path -Parent $t
            if (-not (Test-Path $dir)) { continue }
            $staged = Get-ChildItem -Path $dir -Filter "notes-variant-*.png" -ErrorAction SilentlyContinue
            foreach ($s in $staged)
            {
                Remove-Item -Force $s.FullName
                $removed++
            }
        }
        Write-Host ""
        Write-Host "removed $removed staged variant file(s) from the deployed trees."
        Write-Host ""
    }
    default
    {
        Show-State
    }
}
