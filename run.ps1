# Builds if needed, then starts the app.
param(
    [string]$Distro = "Ubuntu"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$exe = "build\CVBuilder.exe"
$sources = Get-ChildItem src, res -File -ErrorAction SilentlyContinue
$newest = ($sources | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
if (-not (Test-Path $exe) -or (Get-Item $exe).LastWriteTime -lt $newest) {
    & "$PSScriptRoot\build.ps1" -Distro $Distro
}

Start-Process $exe
