# Builds CVBuilder.exe by cross-compiling with mingw-w64 inside WSL.
#
# One-off setup in the WSL distro:
#   sudo apt install -y g++-mingw-w64-x86-64 make
#
# Pass -Distro to build in a distro other than Ubuntu, -Clean to rebuild from
# scratch.
param(
    [string]$Distro = "Ubuntu",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# WSL sees the project through /mnt/<drive>, so translate the Windows path.
$drive = $PSScriptRoot.Substring(0, 1).ToLower()
$rest = $PSScriptRoot.Substring(2).Replace("\", "/")
$wslPath = "/mnt/$drive$rest"

$target = if ($Clean) { "clean all" } else { "all" }
Write-Host "Сборка в WSL ($Distro): $wslPath"
wsl -d $Distro -- bash -lc "cd '$wslPath' && make -j`$(nproc) $target"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Сборка не удалась." -ForegroundColor Red
    exit $LASTEXITCODE
}

Copy-Item sample_cv.json build\ -Force
Write-Host ""
Write-Host "Готово: build\CVBuilder.exe" -ForegroundColor Green
