@echo off
REM Runs CV Builder from source (needs Python 3.10+ installed).
cd /d "%~dp0"
if not exist ".venv" (
    py -3 -m venv .venv || python -m venv .venv
    call .venv\Scripts\activate.bat
    python -m pip install --upgrade pip
    pip install -r requirements.txt
) else (
    call .venv\Scripts\activate.bat
)
python main.py
