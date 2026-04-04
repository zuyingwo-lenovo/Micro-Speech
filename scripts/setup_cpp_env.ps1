# setup_cpp_env.ps1 - Automated Final Robust TFLite v2.18.0 Setup (Final Naming Fix)

$TFLITE_VERSION = "2.18.0"
$DIST_URL = "https://github.com/ValYouW/tflite-dist/releases/download/v2.18.0/tflite-dist-2.18.0.zip"

$LIB_DIR = "lib"
$TFLITE_DEST = "$LIB_DIR\tflite"
$DIST_ZIP = "$LIB_DIR\tflite_dist_v$TFLITE_VERSION.zip"

if (!(Test-Path $LIB_DIR)) { New-Item -ItemType Directory -Path $LIB_DIR -Force | Out-Null }

Write-Host "--- [1/2] Setting up Final TFLite v$TFLITE_VERSION ---" -ForegroundColor Cyan

# 0. Defensive Cleanup
taskkill /F /IM HeyThinkPad.exe /T 2>$null | Out-Null
if (Test-Path $TFLITE_DEST) { Remove-Item -Recurse -Force $TFLITE_DEST -ErrorAction SilentlyContinue }

# 1. Download if missing
if (!(Test-Path $DIST_ZIP)) {
    Write-Host "Downloading TFLite v$TFLITE_VERSION full distribution..."
    Invoke-WebRequest -Uri $DIST_URL -OutFile $DIST_ZIP
}

# 2. Extract
New-Item -ItemType Directory -Path $TFLITE_DEST -Force | Out-Null
Write-Host "Extracting distribution..."
# --strip-components=1 removes the 'tflite-dist' root folder from the zip
tar -xf "$DIST_ZIP" --strip-components=1 -C "$TFLITE_DEST"

# 3. Finalize Binary Layout for CMake
Write-Host "Organizing binaries..."
New-Item -ItemType Directory -Path "$TFLITE_DEST\bin" -Force | Out-Null
New-Item -ItemType Directory -Path "$TFLITE_DEST\lib" -Force | Out-Null

# Smart Search for Binaries (AMD64 / x86_64)
$DLL = Get-ChildItem -Path $TFLITE_DEST -Filter "tensorflowlite_c.dll" -Recurse | Where-Object { $_.FullName -match "windows" } | Select-Object -First 1
# Note: Import library may be named 'tensorflowlite_c.dll.if.lib' or 'tensorflowlite_c.lib'
$LIB = Get-ChildItem -Path $TFLITE_DEST -Filter "*.lib" -Recurse | Where-Object { $_.FullName -match "windows" } | Select-Object -First 1

if ($DLL) {
    Write-Host "Installing DLL: $($DLL.FullName)"
    Copy-Item -Path $DLL.FullName -Destination "$TFLITE_DEST\bin\tensorflowlite_c.dll" -Force
}
if ($LIB) {
    Write-Host "Installing LIB: $($LIB.FullName) (as tensorflowlite_c.lib)"
    Copy-Item -Path $LIB.FullName -Destination "$TFLITE_DEST\lib\tensorflowlite_c.lib" -Force
}

# 4. Miniaudio
Write-Host "`n--- [2/2] Checking Miniaudio ---" -ForegroundColor Cyan
$MINIAUDIO_PATH = "lib/miniaudio.h"
if (!(Test-Path $MINIAUDIO_PATH)) {
    Write-Host "Downloading miniaudio.h..."
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h" -OutFile $MINIAUDIO_PATH
}

Write-Host "`n=== Environment Setup Complete (Robust Final v$TFLITE_VERSION) ===" -ForegroundColor Green
