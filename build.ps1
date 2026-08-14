# Builds a standalone CVBuilder.exe into the dist folder (Windows 11).
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptPath

if (-not (Test-Path ".venv")) {
    try {
        py -3 -m venv .venv
    } catch {
        python -m venv .venv
    }
}

& ".venv\Scripts\Activate.ps1"
try {
    python -m pip install -r requirements.txt
    python -m pip install pyinstaller
    pyinstaller --noconfirm --clean --onefile --windowed --name CVBuilder main.py

    Write-Host ""
    Write-Host "Done. The app is at dist\CVBuilder.exe"
    Read-Host "Press Enter to exit"
} catch {
    Write-Host "An error occurred while building the application."
}
