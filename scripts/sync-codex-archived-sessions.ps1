$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $HOME ".codex\archived_sessions"
$targetDir = Join-Path $repoRoot "codex-archive\archived_sessions"
$manifestPath = Join-Path $repoRoot "codex-archive\README.md"

if (-not (Test-Path $sourceDir)) {
    throw "Source directory not found: $sourceDir"
}

New-Item -ItemType Directory -Force -Path $targetDir | Out-Null

$copied = @()
Get-ChildItem -Path $sourceDir -Filter *.jsonl -File | Sort-Object Name | ForEach-Object {
    $destination = Join-Path $targetDir $_.Name
    Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
    $copied += $_
}

$generatedAt = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss zzz")
$lines = @(
    "# Codex Archived Sessions",
    "",
    "This directory tracks archived Codex Desktop threads copied from:",
    "",
    "- ``$sourceDir``",
    "",
    "Sync command:",
    "",
    "- ``powershell -ExecutionPolicy Bypass -File .\scripts\sync-codex-archived-sessions.ps1``",
    "",
    "Last synced: $generatedAt",
    "",
    "Files:"
)

if ($copied.Count -eq 0) {
    $lines += ""
    $lines += "- (none)"
} else {
    foreach ($file in $copied) {
        $lines += ""
        $lines += "- $($file.Name) ($([math]::Round($file.Length / 1KB, 1)) KiB, modified $($file.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")))"
    }
}

Set-Content -LiteralPath $manifestPath -Value $lines -Encoding utf8

Write-Output "Synced $($copied.Count) archived session file(s) to $targetDir"
