@echo off
REM Builds a standalone CVBuilder.exe into the dist folder (Windows 11).
cd /d "%~dp0"
if not exist ".venv" (
    py -3 -m venv .venv || python -m venv .venv
)
call .venv\Scripts\activate.bat
python -m pip install --upgrade pip
pip install -r requirements.txt pyinstaller
pyinstaller --noconfirm --clean --onefile --windowed --name CVBuilder ^
    main.py
echo.
echo Done. The app is at dist\CVBuilder.exe
pause
