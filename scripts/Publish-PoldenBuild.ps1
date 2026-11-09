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
$existingTarget = $null
if ($null -ne $existing) {
    if ($existing.LinkType -ne 'Junction') {
        throw "$latest exists and is not a junction; refusing to replace it"
    }
    $existingTarget = [System.IO.Path]::GetFullPath([string] $existing.Target)
}

# A successful build is installed immediately, but a running OBS is never stopped for it.
$protectedRuntimes = @($latest, $release, $existingTarget) | Where-Object { $_ }
$running = @(Get-CimInstance Win32_Process -Filter "Name='obs64.exe'" -ErrorAction Stop | Where-Object {
    $processPath = $_.ExecutablePath
    if (-not $processPath) {
        throw 'Cannot determine the path of a running OBS. Close OBS before publishing.'
    }
    $matchesRuntime = $false
    foreach ($runtime in $protectedRuntimes) {
        if ($processPath.StartsWith($runtime.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            $matchesRuntime = $true
        }
    }
    $matchesRuntime
})
if ($running.Count -gt 0) {
    throw 'Polden OBS is running. Ask the user to close it, then publish this prepared build again. No files were changed.'
}

if ($existingTarget -and -not $existingTarget.Equals($release, [System.StringComparison]::OrdinalIgnoreCase)) {
    $portableMarker = Join-Path $existingTarget 'portable_mode.txt'
    if (Test-Path -LiteralPath $portableMarker -PathType Leaf) {
        Copy-Item -LiteralPath $portableMarker -Destination (Join-Path $release 'portable_mode.txt')
    }
    $sourceConfig = Join-Path $existingTarget 'config'
    if (Test-Path -LiteralPath $sourceConfig -PathType Container) {
        # Preserve the user's currently selected runtime, replacing stale settings in the new build.
        $backup = Join-Path $repository ('local-build-backups\publish-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $backup | Out-Null
        $stagedConfig = Join-Path $backup 'current-config'
        Copy-Item -LiteralPath $sourceConfig -Destination $stagedConfig -Recurse -Force
        $targetConfig = Join-Path $release 'config'
        $previousConfig = Join-Path $backup 'previous-build-config'
        # Every move target is an explicit child of the repository; no recursive deletion is used.
        foreach ($path in @($targetConfig, $previousConfig, $stagedConfig)) {
            if (-not [System.IO.Path]::GetFullPath($path).StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Config path must be inside $repository : $path"
            }
        }
        if (Test-Path -LiteralPath $targetConfig) {
            Move-Item -LiteralPath $targetConfig -Destination $previousConfig
        }
        try {
            Move-Item -LiteralPath $stagedConfig -Destination $targetConfig
        } catch {
            if ((Test-Path -LiteralPath $previousConfig) -and -not (Test-Path -LiteralPath $targetConfig)) {
                Move-Item -LiteralPath $previousConfig -Destination $targetConfig
            }
            throw
        }
        Write-Output "Preserved current OBS settings. Previous build settings backup: $backup"
    }
    # New bundled plugin files win; preserve additional locally installed plugin files.
    foreach ($relative in @('obs-plugins', 'data\obs-plugins')) {
        $sourceDirectory = Join-Path $existingTarget $relative
        if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) { continue }
        Get-ChildItem -LiteralPath $sourceDirectory -Recurse -File | ForEach-Object {
            $relativeFile = $_.FullName.Substring($sourceDirectory.Length).TrimStart('\')
            $targetFile = Join-Path (Join-Path $release $relative) $relativeFile
            if (-not (Test-Path -LiteralPath $targetFile)) {
                New-Item -ItemType Directory -Path (Split-Path -Parent $targetFile) -Force | Out-Null
                Copy-Item -LiteralPath $_.FullName -Destination $targetFile
            }
        }
    }
}

if ($null -ne $existing) {
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
