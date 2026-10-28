# Installs Notepad-- for the current user (no admin needed):
#   - copies the app to %LOCALAPPDATA%\Programs\Notepad--
#   - adds a Start menu shortcut, so it shows under Start > All apps
#   - registers it under Settings > Apps > Installed apps, with an uninstaller
param([ValidateSet('auto', 'x64', 'arm64')] [string]$Arch = 'auto')
$ErrorActionPreference = 'Stop'
if ($Arch -eq 'auto') { $Arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' } }
$src = Join-Path $PSScriptRoot "build\$Arch\Notepad--.exe"
if (-not (Test-Path $src)) { throw "$src not found - run build.bat $Arch first" }
$dir = Join-Path $env:LOCALAPPDATA 'Programs\Notepad--'
$exe = Join-Path $dir 'Notepad--.exe'
$lnk = Join-Path ([Environment]::GetFolderPath('Programs')) 'Notepad--.lnk'
$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Notepad--'

Get-Process 'Notepad--' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Stop-Process -Force
New-Item -ItemType Directory -Force $dir | Out-Null
Copy-Item $src $exe -Force
Copy-Item (Join-Path $PSScriptRoot 'ANALYSIS.md') $dir -Force

$shell = New-Object -ComObject WScript.Shell
$s = $shell.CreateShortcut($lnk)
$s.TargetPath = $exe
$s.WorkingDirectory = $dir
$s.IconLocation = "$exe,0"
$s.Description = 'Notepad-- text editor'
$s.Save()

$uninstall = Join-Path $dir 'uninstall.ps1'
@"
`$ErrorActionPreference = 'SilentlyContinue'
Get-Process 'Notepad--' | Where-Object { `$_.Path -eq '$exe' } | Stop-Process -Force
Remove-Item '$lnk' -Force
Remove-Item '$key' -Recurse -Force
Start-Process cmd -ArgumentList '/c timeout /t 2 >nul & rmdir /s /q "$dir"' -WindowStyle Hidden
"@ | Set-Content -Path $uninstall -Encoding UTF8

New-Item -Path $key -Force | Out-Null
$size = [int]((Get-Item $exe).Length / 1KB)
$values = @{
    DisplayName = 'Notepad--'; DisplayVersion = '1.2.0'; Publisher = 'Yoi'
    DisplayIcon = "$exe,0"; InstallLocation = $dir
    UninstallString = "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$uninstall`""
}
foreach ($k in $values.Keys) { Set-ItemProperty -Path $key -Name $k -Value $values[$k] }
Set-ItemProperty -Path $key -Name EstimatedSize -Value $size -Type DWord
Set-ItemProperty -Path $key -Name NoModify -Value 1 -Type DWord
Set-ItemProperty -Path $key -Name NoRepair -Value 1 -Type DWord
Write-Output "Installed to $dir"
