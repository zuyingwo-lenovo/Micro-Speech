# setup_windows.ps1
# Sets up the Python environment for the "Hey ThinkPad" Wake Word app

Write-Host "Setting up Python virtual environment..."
python -m venv .venv

Write-Host "Activating venv and installing packages..."
# Activate script must be called in the current scope using dot-sourcing
. \.venv\Scripts\Activate.ps1

python -m pip install --upgrade pip
pip install -r requirements.txt

Write-Host "Installation Complete! To run the app:"
Write-Host "1. .\.venv\Scripts\Activate.ps1"
Write-Host "2. python app/main.py"
