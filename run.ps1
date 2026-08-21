# Builds if needed, then starts the app.
param(
    [string]$Distro = "Ubuntu"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# A quick staleness check, only so that an unchanged tree does not pay the ~3
# seconds it takes to start WSL and let make conclude there is nothing to do.
# The Makefile belongs in this list: it decides the compiler flags and which
# resources get linked, so editing it has to force a rebuild the same way
# editing a source file does - otherwise the app silently starts as the
# previous build. When in doubt build.ps1 is authoritative; this is a shortcut,
# not a second dependency graph.
$exe = "build\CVBuilder.exe"
$sources = Get-ChildItem src, res, Makefile -File -ErrorAction SilentlyContinue
$newest = ($sources | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
if (-not (Test-Path $exe) -or (Get-Item $exe).LastWriteTime -lt $newest) {
    & "$PSScriptRoot\build.ps1" -Distro $Distro
}

Start-Process $exe
