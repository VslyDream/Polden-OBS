param(
    [Parameter(Mandatory = $true)][string] $RequestPath,
    [switch] $NoRestart,
    [switch] $Quiet
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$changes = [System.Collections.Generic.List[object]]::new()
$backup = $null
$root = $null
$failed = $false
$transactionStarted = $false
$requestDirectory = [System.IO.Path]::GetFullPath((Split-Path -Parent $RequestPath))
$logPath = Join-Path $requestDirectory 'update.log'

function Write-UpdateLog([string] $Message) {
    Add-Content -LiteralPath $logPath -Value "$(Get-Date -Format o) $Message" -Encoding UTF8
}

function Get-UpdateHash([string] $Path) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        return [System.BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $algorithm.Dispose()
    }
}

function Get-SafeRelativePath([string] $Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or $Relative.Contains('\') -or
        $Relative -match '[\x00-\x1f<>:"|?*]' -or $Relative.StartsWith('/')) {
        throw "Unsafe package path: $Relative"
    }
    foreach ($part in $Relative.Split('/')) {
        if ($part -in @('', '.', '..') -or $part.EndsWith('.') -or $part.EndsWith(' ') -or
            $part -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)') {
            throw "Unsafe package path: $Relative"
        }
    }
    if ($Relative -notmatch '^(bin/|data/|obs-plugins/)' -and
        $Relative -notin @('portable_mode.txt', 'COPYING', 'polden-install.json')) {
        throw "Package may not modify this path: $Relative"
    }
    return $Relative
}

function Get-ChildPath([string] $Directory, [string] $Relative) {
    $full = [System.IO.Path]::GetFullPath((Join-Path $Directory $Relative))
    $prefix = [System.IO.Path]::GetFullPath($Directory).TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escapes its directory: $Relative"
    }
    return $full
}

function Assert-NoLinks([string] $Path, [string] $Boundary) {
    $current = $Path
    while ($current.Length -ge $Boundary.Length) {
        $item = Get-Item -LiteralPath $current -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
            throw "Update path contains a link: $current"
        }
        if ($current.Equals($Boundary, [System.StringComparison]::OrdinalIgnoreCase)) { break }
        $current = Split-Path -Parent $current
    }
}

function Read-Manifest([string] $Path) {
    $manifest = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or $manifest.version -notmatch '^\d+\.\d+\.\d+$') {
        throw 'Unsupported installation manifest'
    }
    $files = @{}
    foreach ($property in $manifest.files.PSObject.Properties) {
        $relative = Get-SafeRelativePath $property.Name
        if ($relative -eq 'polden-install.json' -or $files.ContainsKey($relative) -or
            [string] $property.Value -notmatch '^[0-9a-f]{64}$') {
            throw "Invalid file entry in installation manifest: $relative"
        }
        $files[$relative] = [string] $property.Value
    }
    if (-not $files.ContainsKey('bin/64bit/obs64.exe') -or $files.Count -gt 20000) {
        throw 'Installation manifest is missing the application'
    }
    return @{ Version = [string] $manifest.version; Files = $files }
}

try {
    Write-UpdateLog 'Preparing update'
    $request = Get-Content -LiteralPath $RequestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $root = [System.IO.Path]::GetFullPath([string] $request.root).TrimEnd('\')
    if ($root.Length -le [System.IO.Path]::GetPathRoot($root).Length -or
        -not (Test-Path -LiteralPath (Join-Path $root 'polden-install.json') -PathType Leaf)) {
        throw 'Target is not a packaged Polden installation'
    }
    Assert-NoLinks $root $root
    $old = Read-Manifest (Join-Path $root 'polden-install.json')
    if ($request.version -notmatch '^\d+\.\d+\.\d+$' -or
        [version] $request.version -le [version] $old.Version -or
        $request.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'Update version or checksum is invalid'
    }
    $archivePath = Join-Path $requestDirectory 'update.zip'
    if ((Get-UpdateHash $archivePath) -ne $request.sha256) {
        throw 'Update archive checksum does not match'
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stage = Get-ChildPath $requestDirectory 'stage'
    New-Item -ItemType Directory -Path $stage | Out-Null
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $seen = @{}
        [long] $expandedSize = 0
        foreach ($entry in $zip.Entries) {
            if (-not $entry.FullName.StartsWith('Polden OBS/', [System.StringComparison]::Ordinal)) {
                throw 'Unexpected archive layout'
            }
            $relative = Get-SafeRelativePath $entry.FullName.Substring(11)
            if ($seen.ContainsKey($relative)) { throw "Duplicate archive entry: $relative" }
            $seen[$relative] = $true
            $expandedSize += $entry.Length
            if ($expandedSize -gt 4GB -or $seen.Count -gt 20001) { throw 'Update archive is too large' }
            $destination = Get-ChildPath $stage $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination, $false)
        }
    } finally { $zip.Dispose() }
    $new = Read-Manifest (Join-Path $stage 'polden-install.json')
    if ($new.Version -ne $request.version -or $seen.Count -ne ($new.Files.Count + 1)) {
        throw 'Update manifest does not match the release'
    }
    foreach ($relative in $new.Files.Keys) {
        $stagedFile = Get-ChildPath $stage $relative
        if ((Get-UpdateHash $stagedFile) -ne $new.Files[$relative]) {
            throw "Update file checksum does not match: $relative"
        }
    }
    # Prepare everything while OBS is still alive, then wait for a normal exit.
    $parent = Get-Process -Id ([int] $request.pid) -ErrorAction SilentlyContinue
    if ($null -ne $parent -and -not $parent.WaitForExit(180000)) {
        throw 'Polden is still running. Close it before retrying the update.'
    }
    $binary = Join-Path $root 'bin\64bit\obs64.exe'
    $otherInstances = Get-Process -Name obs64 -ErrorAction SilentlyContinue | Where-Object {
        $_.Path -and [System.IO.Path]::GetFullPath($_.Path).Equals($binary, [System.StringComparison]::OrdinalIgnoreCase)
    }
    if ($otherInstances) { throw 'Another instance of this Polden installation is running' }
    $backupRelative = '.polden-backups/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N')
    $backup = Get-ChildPath $root $backupRelative
    Assert-NoLinks $backup $root
    New-Item -ItemType Directory -Path $backup | Out-Null
    $settings = [System.IO.Path]::GetFullPath([string] $request.settings).TrimEnd('\')
    $portableSettings = Join-Path $root 'config\obs-studio'
    $regularSettings = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'obs-studio'
    if (-not $settings.Equals($portableSettings, [System.StringComparison]::OrdinalIgnoreCase) -and
        -not $settings.Equals($regularSettings, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Unexpected settings directory'
    }
    if (Test-Path -LiteralPath $settings) {
        # Read/copy settings only; installation never writes into them.
        Copy-Item -LiteralPath $settings -Destination (Join-Path $backup 'settings') -Recurse
    }
    $affected = @($old.Files.Keys) + @($new.Files.Keys) + @('polden-install.json') | Sort-Object -Unique
    foreach ($relative in $affected) {
        $target = Get-ChildPath $root $relative
        Assert-NoLinks $target $root
        $exists = Test-Path -LiteralPath $target
        if ($exists -and -not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "Target is not a file: $relative" }
        if ($exists -and $relative -ne 'polden-install.json' -and -not $old.Files.ContainsKey($relative)) {
            throw "Update would replace a custom file: $relative"
        }
        $saved = Get-ChildPath $backup ('files/' + $relative)
        if ($exists) {
            New-Item -ItemType Directory -Path (Split-Path -Parent $saved) -Force | Out-Null
            Copy-Item -LiteralPath $target -Destination $saved
        }
        $changes.Add(@{ Target = $target; Saved = $saved; Existed = $exists; Relative = $relative })
    }
    Write-UpdateLog "Backup: $backup"
    $transactionStarted = $true
    foreach ($change in $changes) {
        $relative = $change.Relative
        if ($new.Files.ContainsKey($relative) -or $relative -eq 'polden-install.json') {
            New-Item -ItemType Directory -Path (Split-Path -Parent $change.Target) -Force | Out-Null
            Copy-Item -LiteralPath (Get-ChildPath $stage $relative) -Destination $change.Target -Force
        } elseif ($change.Existed) {
            Remove-Item -LiteralPath $change.Target -Force
        }
    }
    Write-UpdateLog "Installed Polden $($new.Version)"
    Copy-Item -LiteralPath $logPath -Destination (Join-Path $backup 'update.log')
    if (-not $NoRestart) {
        $restartOptions = @{ FilePath = $binary; WorkingDirectory = (Split-Path -Parent $binary) }
        if ($request.portable) { $restartOptions.ArgumentList = @('--portable') }
        Start-Process @restartOptions | Out-Null
    }
    # Both paths were resolved and checked inside the request folder above.
    if ($stage.StartsWith($requestDirectory.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
    }
} catch {
    $failed = $true
    $message = $_.Exception.Message
    Write-UpdateLog "Failed: $message"
    if ($transactionStarted) {
        $rollbackErrors = @()
        foreach ($change in $changes) {
            try {
                if ($change.Existed) {
                    Copy-Item -LiteralPath $change.Saved -Destination $change.Target -Force
                } elseif (Test-Path -LiteralPath $change.Target -PathType Leaf) {
                    Remove-Item -LiteralPath $change.Target -Force
                }
            } catch { $rollbackErrors += $_.Exception.Message }
        }
        if ($rollbackErrors.Count -eq 0) {
            Write-UpdateLog 'Previous version restored'
            $message += "`nPrevious version restored."
        } else {
            Write-UpdateLog ('Rollback errors: ' + ($rollbackErrors -join '; '))
            $message += "`nRestore the previous version from the backup: $backup"
        }
    }
    if ($null -ne $backup) {
        Copy-Item -LiteralPath $logPath -Destination (Join-Path $backup 'update.log') -ErrorAction SilentlyContinue
    }
    if (-not $Quiet) {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show("$message`n`nLog: $logPath", 'Polden update failed') | Out-Null
        if ($transactionStarted -and $rollbackErrors.Count -eq 0 -and -not $NoRestart) {
            $restartOptions = @{ FilePath = (Join-Path $root 'bin\64bit\obs64.exe'); WorkingDirectory = (Join-Path $root 'bin\64bit') }
            if ($request.portable) { $restartOptions.ArgumentList = @('--portable') }
            Start-Process @restartOptions | Out-Null
        }
    }
}
if ($failed) { exit 1 }
exit 0
