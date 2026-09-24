param(
    [string] $Executable = "$PSScriptRoot\..\build_x64\rundir\Release\bin\64bit\obs64.exe"
)

$executablePath = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
$programs = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$shortcutPath = Join-Path $programs 'Polden OBS.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $executablePath
$shortcut.WorkingDirectory = Split-Path -Parent $executablePath
$shortcut.IconLocation = "$executablePath,0"
$shortcut.Description = 'Polden OBS'
$shortcut.Save()
Write-Output $shortcutPath
