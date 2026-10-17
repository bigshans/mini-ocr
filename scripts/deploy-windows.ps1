# Build and deploy Mini OCR on Windows.
# Run from the repository root in a "x64 Native Tools Command Prompt for VS 2022".
#
# Required environment:
#   VCPKG_ROOT  - vcpkg checkout, with Qt6/OpenCV/Tesseract installed for x64-windows
# Optional:
#   QT_DIR      - Qt MSVC kit, e.g. C:\Qt\6.8.3\msvc2022_64
#
# Example:
#   $env:VCPKG_ROOT = "C:\vcpkg"
#   $env:QT_DIR = "C:\Qt\6.8.3\msvc2022_64"
#   .\scripts\deploy-windows.ps1

param(
    [string]$BuildDir = "build-windows",
    [string]$DistDir = "dist"
)

$ErrorActionPreference = "Stop"

if (-not $env:VCPKG_ROOT) {
    throw "VCPKG_ROOT is not set"
}
if (-not $env:QT_DIR) {
    throw "QT_DIR is not set (e.g. C:\Qt\6.8.3\msvc2022_64)"
}

cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_PREFIX_PATH="$env:QT_DIR" `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build $BuildDir --config Release --parallel

if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
New-Item -ItemType Directory -Path $DistDir | Out-Null

Copy-Item "$BuildDir\Release\mini-ocr.exe" $DistDir
Copy-Item -Recurse models "$DistDir\models"

& "$env:QT_DIR\bin\windeployqt.exe" --release --no-translations --dir $DistDir `
    "$DistDir\mini-ocr.exe"

# OpenCV/Tesseract and their dependencies come from vcpkg. Copy the runtime
# DLLs into the portable dist directory.
$vcpkgBin = Join-Path $env:VCPKG_ROOT "installed\x64-windows\bin"
if (Test-Path $vcpkgBin) {
    Copy-Item "$vcpkgBin\*.dll" $DistDir -Force
} else {
    Write-Warning "vcpkg bin directory not found: $vcpkgBin"
}

Write-Host "Portable build ready: $DistDir"
Write-Host "Run: $DistDir\mini-ocr.exe"
