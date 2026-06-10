$ErrorActionPreference = "Stop"

$root = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($root)) {
    $root = (Get-Location).Path
}

$headerPath = Join-Path $root "version_auto.h"
$pattern = '^(?:v)?(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?$'
$version = "0.0.0.0"
$parsed = @()

$allTags = @(git -C $root tag)
foreach ($tag in $allTags) {
    if ($tag -match $pattern) {
        $parsed += [PSCustomObject]@{
            Tag = $tag
            Major = [int]$Matches[1]
            Minor = [int]$Matches[2]
            Patch = [int]$Matches[3]
            Build = if ($Matches[4]) { [int]$Matches[4] } else { 0 }
        }
    }
}

$exactTag = cmd /c "git -C `"$root`" describe --tags --exact-match 2>nul"
if ($LASTEXITCODE -eq 0 -and $exactTag -match $pattern) {
    $version = "{0}.{1}.{2}.{3}" -f [int]$Matches[1], [int]$Matches[2], [int]$Matches[3], $(if ($Matches[4]) { [int]$Matches[4] } else { 0 })
}
elseif ($parsed.Count -gt 0) {
    $latest = $parsed | Sort-Object Major, Minor, Patch, Build | Select-Object -Last 1
    $version = "{0}.{1}.{2}.{3}" -f $latest.Major, $latest.Minor, $latest.Patch, $latest.Build
}

$quad = $version -replace '\.', ','
$content = @(
    '#pragma once',
    '',
    "#define APP_VERSION_QUAD $quad",
    "#define APP_VERSION_STR ""$version""",
    "#define APP_VERSION_LABEL_EN ""Version $version""",
    "#define APP_VERSION_LABEL_ZH ""版本 $version"""
) -join [Environment]::NewLine

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($headerPath, $content + [Environment]::NewLine, $utf8NoBom)

Write-Host "Resolved version: $version"
