# Puts an "LED cube on-off" shortcut on the desktop. Double-click it when you
# leave to blank the cube, and again when you're back to turn it on.
# Undo with:  .\install-toggle.ps1 -Remove
param([switch]$Remove, [string]$Address = "192.168.50.66")

$link = Join-Path ([Environment]::GetFolderPath("Desktop")) "LED cube on-off.lnk"
if ($Remove) {
    Remove-Item $link -ErrorAction SilentlyContinue
    "removed"
    return
}
$s = (New-Object -ComObject WScript.Shell).CreateShortcut($link)
$s.TargetPath = "powershell.exe"
$s.Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$PSScriptRoot\cubectl.ps1`" toggle -Address $Address"
$s.WorkingDirectory = $PSScriptRoot
$s.IconLocation = "$env:SystemRoot\System32\shell32.dll,27"   # power icon
$s.WindowStyle = 7   # minimized, so no console flashes up
$s.Save()
"installed: $link"
