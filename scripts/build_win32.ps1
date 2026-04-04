# build_win32.ps1
# Automates the setup and build of the HeyThinkPad native C++ application.

$ErrorActionPreference = "Stop"

Write-Host "--- [1/3] Setting up C++ Environment (Dependencies) ---" -ForegroundColor Cyan
if (!(Test-Path "lib/tflite/lib/tensorflowlite_c.lib")) {
    & "powershell.exe" -File "./scripts/setup_cpp_env.ps1"
} else {
    Write-Host "Dependencies already present in lib/tflite." -ForegroundColor Green
}

Write-Host "`n--- [2/3] Configuring CMake ---" -ForegroundColor Cyan
if (!(Test-Path "cmake-build")) {
    New-Item -ItemType Directory -Path "cmake-build" | Out-Null
}

cmake -B cmake-build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

Write-Host "`n--- [3/3] Building Release Configuration ---" -ForegroundColor Cyan
cmake --build cmake-build --config Release
if ($LASTEXITCODE -ne 0) { 
    Write-Host "`n--- ERROR: Build Failed! ---" -ForegroundColor Red
    exit 1
}

# 4. Deploy Binaries (DLLs)
Write-Host "`n--- [4/4] Deploying TFLite Binaries ---" -ForegroundColor Cyan
$DEST_BIN = "cmake-build/Release"
if (!(Test-Path $DEST_BIN)) { New-Item -ItemType Directory -Path $DEST_BIN | Out-Null }
Copy-Item -Path "lib/tflite/bin/*.dll" -Destination $DEST_BIN -Force

Write-Host "`n--- Success! ---" -ForegroundColor Green
Write-Host "Executable: d:\dev\Metrics-Insights\llm-rag-exec\tensorflow\Micro-Speech\cmake-build\Release\HeyThinkPad.exe" -ForegroundColor Yellow
Write-Host "Model Location: d:\dev\Metrics-Insights\llm-rag-exec\tensorflow\Micro-Speech\cmake-build\Release\artifacts\model.tflite" -ForegroundColor Yellow

Write-Host "`nRun with: ./cmake-build/Release/HeyThinkPad.exe" -ForegroundColor White
