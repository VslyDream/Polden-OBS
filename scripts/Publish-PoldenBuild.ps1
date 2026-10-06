param(
    [string] $BuildDir = 'build_polden_next'
)

$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = [System.IO.Path]::GetFullPath((Join-Path $repository $BuildDir))
$repositoryPrefix = $repository.TrimEnd('\') + '\'
if (-not $build.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Build directory must be inside $repository"
}

$release = Join-Path $build 'rundir\Release'
$binary = Join-Path $release 'bin\64bit\obs64.exe'
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "Release executable is missing: $binary"
}

$latest = Join-Path $repository 'latest'
$existing = Get-Item -LiteralPath $latest -Force -ErrorAction SilentlyContinue
if ($null -ne $existing) {
    if ($existing.LinkType -ne 'Junction') {
        throw "$latest exists and is not a junction; refusing to replace it"
    }
    $existingTarget = [System.IO.Path]::GetFullPath([string] $existing.Target)
    if (-not $existingTarget.Equals($release, [System.StringComparison]::OrdinalIgnoreCase)) {
        # DirectoryInfo.Delete removes the junction itself, not its target.
        $existing.Delete()
        $existing = $null
    }
}

if ($null -eq $existing) {
    New-Item -ItemType Junction -Path $latest -Target $release | Out-Null
}

$stableExecutable = Join-Path $latest 'bin\64bit\obs64.exe'
& (Join-Path $PSScriptRoot 'Install-PoldenShortcut.ps1') -Executable $stableExecutable
Write-Output "Current Polden OBS: $stableExecutable"
