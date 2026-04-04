@echo off
REM Run the Audio Data Augmentation Pipeline

cd /d "%~dp0\.."

echo Activating Virtual Environment...
call .venv\Scripts\activate.bat

echo.
echo =========================================
echo Running Audio Data Augmentation Pipeline
echo =========================================
echo.

python src\augment_audio.py --input sample\org --output-dir sample\augmented --num-aug 10 --dataset-mode

echo.
echo =========================================
echo Inspecting Augmented Audio Statistics
echo =========================================
echo.

python src\inspect_audio.py --input sample\augmented

echo.
echo Dataset Generation and Augmentation Complete!
pause
