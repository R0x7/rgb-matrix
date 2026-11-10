# Makes cube-sender.exe start (hidden) every time you log in to Windows, and
# starts it now. Undo with:  .\install-startup.ps1 -Remove
param([switch]$Remove, [string]$Address = "192.168.50.66")

$link = Join-Path ([Environment]::GetFolderPath("Startup")) "LED cube sender.lnk"
$exe = Join-Path $PSScriptRoot "cube-sender.exe"

Get-Process cube-sender -ErrorAction SilentlyContinue | Stop-Process -Force
if ($Remove) {
    Remove-Item $link -ErrorAction SilentlyContinue
    "removed"
    return
}
if (-not (Test-Path $exe)) { & "$PSScriptRoot\build.ps1" }
$s = (New-Object -ComObject WScript.Shell).CreateShortcut($link)
$s.TargetPath = $exe
$s.Arguments = $Address
$s.WorkingDirectory = $PSScriptRoot
$s.Save()
Start-Process $exe -ArgumentList $Address
"installed: $link"
