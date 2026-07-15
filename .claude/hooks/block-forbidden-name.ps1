# PreToolUse hook for the Write|Edit matcher: denies any tool call whose new file content or
# target path contains the name of the commercial real-guitar game this project must never
# reference by name (the NAMING FIREWALL constraint restated in every docs/plans/roadmap/*.md
# Constraints section; use "RS" or neutral phrasing instead).
#
# The forbidden pattern is assembled by string concatenation so this script itself never
# contains the contiguous word. Windows PowerShell 5.1 compatible.
#
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

# Built by concatenation on purpose -- never spell the contiguous word, even in this hook.
$forbidden = "rock" + "smith"

$texts = @()
$tool_input = $payload.tool_input
if ($null -ne $tool_input)
{
    # Write tool payload.
    $texts += [string]$tool_input.content
    # Edit tool payload. old_string is deliberately NOT scanned: edits that remove an existing
    # occurrence must stay allowed.
    $texts += [string]$tool_input.new_string
    # Both tools: the target path itself must not carry the name either.
    $texts += [string]$tool_input.file_path
    # Defensive: batched-edit payload shapes carry an edits array of {old_string,new_string}.
    if ($null -ne $tool_input.edits)
    {
        foreach ($edit in $tool_input.edits)
        {
            $texts += [string]$edit.new_string
        }
    }
}

foreach ($text in $texts)
{
    if (-not [string]::IsNullOrEmpty($text) -and
        $text.IndexOf($forbidden, [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
    {
        [Console]::Error.WriteLine(
            "BLOCKED by .claude/hooks/block-forbidden-name.ps1: the content or file path " +
            "contains the name of the commercial real-guitar game that inspired this project. " +
            "Repo naming rule (see the NAMING FIREWALL constraint restated in the Constraints " +
            "section of every docs/plans/roadmap/*.md plan): that name must never appear in any file, " +
            "commit message, or generated text -- use 'RS'/'RS2014' or neutral phrasing such " +
            "as 'the reference real-guitar game' instead, including inside quotes, comments, " +
            "and test data. Rewrite the content without the name and retry.")
        exit 2
    }
}

exit 0
