param(
    [string] $Executable = "$PSScriptRoot\..\latest\bin\64bit\obs64.exe"
)

$executablePath = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
$programs = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$shortcutPath = Join-Path $programs 'Polden OBS.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$workingDirectory = Split-Path -Parent $executablePath
$iconLocation = "$executablePath,0"
if ($shortcut.TargetPath -eq $executablePath -and
    $shortcut.WorkingDirectory -eq $workingDirectory -and
    $shortcut.IconLocation -eq $iconLocation -and
    $shortcut.Description -eq 'Polden OBS') {
    Write-Output $shortcutPath
    return
}
$shortcut.TargetPath = $executablePath
$shortcut.WorkingDirectory = $workingDirectory
$shortcut.IconLocation = $iconLocation
$shortcut.Description = 'Polden OBS'
$shortcut.Save()
Write-Output $shortcutPath
