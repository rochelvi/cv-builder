# Builds CVBuilder.exe by cross-compiling with mingw-w64 inside WSL.
#
# One-off setup in the WSL distro:
#   sudo apt install -y g++-mingw-w64-x86-64 make
#
# Pass -Distro to build in a distro other than Ubuntu, -Clean to rebuild from
# scratch, -Installer to also compile installer\setup.iss into a setup .exe
# (needs Inno Setup 6: winget install JRSoftware.InnoSetup).
param(
    [string]$Distro = "Ubuntu",
    [switch]$Clean,
    [switch]$Installer
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# WSL sees the project through /mnt/<drive>, so translate the Windows path.
$drive = $PSScriptRoot.Substring(0, 1).ToLower()
$rest = $PSScriptRoot.Substring(2).Replace("\", "/")
$wslPath = "/mnt/$drive$rest"

# `make -j clean all` is a race: the two goals are independent, so a parallel
# make happily wipes the object files while it is still linking them. Clean
# first, on its own, then build.
$recipe = if ($Clean) { "make clean && make -j`$(nproc) all" } else { "make -j`$(nproc) all" }
Write-Host "Сборка в WSL ($Distro): $wslPath"
wsl -d $Distro -- bash -lc "cd '$wslPath' && $recipe"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Сборка не удалась." -ForegroundColor Red
    exit $LASTEXITCODE
}                                v\\\

\\




iiii                                                                                                                        ,   nn ,nmn,m n0-9poi;kl,./67yutjgmhv bn456yerthdfgcxbn v

Copy-Item sample_cv.json build\ -Force
Write-Host ""
Write-Host "Готово: build\CVBuilder.exe" -ForegroundColor Green

if (-not $Installer) { return }

# Inno Setup rarely lands on PATH, and winget installs it per-user under
# %LOCALAPPDATA% rather than into Program Files, so ask the registry where it
# actually went before falling back to the usual places.
$iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $uninstall = @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*"
    )
    $iscc = Get-ItemProperty $uninstall -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -like "*Inno Setup*" -and $_.InstallLocation } |
        ForEach-Object { Join-Path $_.InstallLocation "ISCC.exe" } |
        Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) {
    $iscc = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) {
    Write-Host "Inno Setup не найден. Поставить: winget install JRSoftware.InnoSetup" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Сборка установщика: $iscc"
& $iscc /Q installer\setup.iss
if ($LASTEXITCODE -ne 0) {
    Write-Host "Установщик собрать не удалось." -ForegroundColor Red
    exit $LASTEXITCODE
}

$setup = Get-ChildItem build\CVBuilder-*-setup.exe | Sort-Object LastWriteTime | Select-Object -Last 1
$size = [math]::Round($setup.Length / 1MB, 2)
Write-Host "Готово: build\$($setup.Name) ($size МБ)" -ForegroundColor Green
