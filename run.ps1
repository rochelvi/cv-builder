# Runs CV Builder from source (needs Python 3.10+ installed).
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptPath

if (-not (Test-Path ".venv")) {
    try {
        py -3 -m venv .venv
    } catch {
        python -m venv .venv
    }
    & ".venv\Scripts\Activate.ps1"
    python -m pip install --upgrade pip
    pip install -r requirements.txt
} else {
    & ".venv\Scripts\Activate.ps1"
}

python main.py
