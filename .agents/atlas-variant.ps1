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
$targets = @(
    (Join-Path $repo "build\$Preset\rock-hero-editor\app\rock_hero_editor_exe_artefacts\Debug\resources\textures\notes.png"),
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
    default
    {
        Show-State
    }
}
