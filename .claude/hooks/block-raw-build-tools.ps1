# PreToolUse hook for the Bash|PowerShell matcher: denies commands that invoke cmake, ninja,
# or ctest directly as an executable. All build/test/lint work must route through
# .agents/rockhero-build.ps1 (policy: CLAUDE.md, "Agent-Run Builds"; usage: .agents/README.md).
#
# Precision rules:
# - Anything containing "rockhero-build.ps1" is allowed outright (the sanctioned helper).
# - Only the executable position of each pipeline/statement segment is inspected, and the
#   splitter is quote-aware, so prose mentions ("echo cmake failed", commit messages),
#   cmake-format, cmake-conan paths, pre-commit invocations, and grep/rg patterns never match.
#
# Windows PowerShell 5.1 compatible.
# Exit codes: 0 = allow, 2 = block (stderr is fed back to the model).
# Malformed or empty hook input fails OPEN (exit 0) so a hook bug can never wedge the session.

$ErrorActionPreference = "Stop"

try
{
    # PS 5.1 feeds piped stdin line-by-line; Out-String rejoins it so multi-line JSON parses.
    $raw = [string]($input | Out-String)
    if ([string]::IsNullOrWhiteSpace($raw))
    {
        exit 0
    }

    $payload = $raw | ConvertFrom-Json
}
catch
{
    exit 0
}

$command = ""
if ($null -ne $payload.tool_input)
{
    $command = [string]$payload.tool_input.command
}

if ([string]::IsNullOrWhiteSpace($command))
{
    exit 0
}

# The sanctioned helper is itself launched through powershell.exe; any command that routes
# through it is allowed without further inspection.
if ($command.IndexOf("rockhero-build.ps1", [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
{
    exit 0
}

# Quote-aware split into pipeline/statement segments on | || & && ; and newlines. Separators
# inside single or double quotes do not split, so quoted prose (e.g. a commit message
# containing "; ctest") can never put a word at command position.
function Split-CommandSegments
{
    param([string]$Text)

    $segments = New-Object System.Collections.Generic.List[string]
    $current = New-Object System.Text.StringBuilder
    $quote = [char]0

    for ($i = 0; $i -lt $Text.Length; $i++)
    {
        $c = $Text[$i]

        if ($quote -ne [char]0)
        {
            [void]$current.Append($c)
            if ($c -eq $quote)
            {
                $quote = [char]0
            }
            continue
        }

        if ($c -eq '"' -or $c -eq "'")
        {
            $quote = $c
            [void]$current.Append($c)
            continue
        }

        if ($c -eq '|' -or $c -eq '&' -or $c -eq ';' -or $c -eq "`n")
        {
            $segments.Add($current.ToString())
            [void]$current.Clear()
            # Consume the second character of a doubled operator (|| or &&).
            if (($c -eq '|' -or $c -eq '&') -and ($i + 1) -lt $Text.Length -and $Text[$i + 1] -eq $c)
            {
                $i++
            }
            continue
        }

        [void]$current.Append($c)
    }

    $segments.Add($current.ToString())
    return $segments
}

$blocked_names = @("cmake", "ninja", "ctest")

foreach ($segment in (Split-CommandSegments -Text $command))
{
    $text = $segment.Trim()

    # Strip grouping parens so "( cmake ..." is still seen at command position. A PowerShell
    # call operator "&" is already consumed as a separator by the splitter.
    $text = $text.TrimStart('(', ' ', "`t")

    # Skip leading POSIX-style environment assignments: VAR=value cmd ...
    while ($text -match '^[A-Za-z_][A-Za-z0-9_]*=\S*\s+')
    {
        $text = $text.Substring($Matches[0].Length)
    }

    $first_token = $null
    if ($text -match '^"([^"]+)"' -or $text -match "^'([^']+)'")
    {
        $first_token = $Matches[1]
    }
    elseif ($text -match '^(\S+)')
    {
        $first_token = $Matches[1]
    }

    if ([string]::IsNullOrEmpty($first_token))
    {
        continue
    }

    # Word boundary at command position: the whole basename must be the tool name (optionally
    # with a .exe suffix). "cmake-format" and "cmake-conan" therefore never match.
    $basename = ($first_token -split '[\\/]')[-1].Trim('"', "'").ToLowerInvariant()
    if ($basename.EndsWith(".exe"))
    {
        $basename = $basename.Substring(0, $basename.Length - 4)
    }

    if ($blocked_names -contains $basename)
    {
        [Console]::Error.WriteLine(
            "BLOCKED by .claude/hooks/block-raw-build-tools.ps1: direct '" + $basename + "' " +
            "invocations are not allowed in this repository. Route all build, test, and lint " +
            "work through the sanctioned helper instead (CLAUDE.md, 'Agent-Run Builds'; usage " +
            "in .agents/README.md): powershell -NoProfile -ExecutionPolicy Bypass -File " +
            ".\.agents\rockhero-build.ps1 with -Targets all, -RunTouchedTests, or " +
            "-Targets clang-tidy as separate invocations. Pass -Configure only after CMake " +
            "graph changes or stale-Ninja errors.")
        exit 2
    }
}

exit 0
