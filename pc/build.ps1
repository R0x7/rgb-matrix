# Builds cube-sender.exe with the C# compiler that ships with Windows.
#   .\build.ps1            windowless build (for running in the background)
#   .\build.ps1 -Console   console build, use with --verbose to debug
param([switch]$Console)

$csc = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) { throw "csc.exe not found at $csc" }
if ($Console) { $target = "exe"; $out = "cube-sender-console.exe" }
else { $target = "winexe"; $out = "cube-sender.exe" }
& $csc /nologo /optimize+ /target:$target "/out:$PSScriptRoot\$out" "$PSScriptRoot\CubeSender.cs"
if ($LASTEXITCODE -eq 0) { "built $out" }
