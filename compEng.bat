@echo off
setlocal
cd /d "%~dp0"

echo [1/4] Actualizando main...
git pull --ff-only || exit /b 1

echo [2/4] Eliminando build anterior...
if exist build rmdir /s /q build

echo [3/4] Configurando CnCGZH Studio...
cmake -S . -B build -A x64 || exit /b 1

echo [4/4] Compilando CnCGZH Studio...
cmake --build build --config Release --target CnCGZHStudio || exit /b 1

if not exist "build\Release\CnCGZHStudio.exe" (
  echo ERROR: No se genero build\Release\CnCGZHStudio.exe
  exit /b 1
)

start "" "build\Release\CnCGZHStudio.exe"
endlocal
