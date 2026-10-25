@echo off
rem Builds both apps, stages the installer packages and (if Inno Setup is installed) builds build\StepViewerSetup.exe.
rem Run from anywhere: build_all.bat
cd /d "%~dp0"

echo === 0/3 Configuring (reads the version from vcpkg.json) ===
cmake -S . -B build >nul
if errorlevel 1 (
    echo CONFIGURE FAILED. Run "cmake -S . -B build" to see the reason.
    exit /b 1
)
set /p VERSION=<build\version.txt
echo Version %VERSION%

echo === 1/3 Building StepViewer (Full) and StepViewerLight ===
cmake --build build --config Release --target StepViewer StepViewerLight
if errorlevel 1 (
    echo BUILD FAILED. If StepViewerLight.exe is open, close it and run this again.
    exit /b 1
)

echo === 2/3 Staging installer packages ===
cmake --install build --config Release --prefix build\package\Light --component Light >nul || exit /b 1
cmake --install build --config Release --prefix build\package\Full --component Light >nul || exit /b 1
cmake --install build --config Release --prefix build\package\Full --component Full >nul || exit /b 1

echo === 3/3 Building the installer ===
set "ISCC="
where iscc >nul 2>nul && set "ISCC=iscc"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC (
    echo Inno Setup is not installed, so StepViewerSetup.exe was NOT built. Apps and packages are up to date.
    exit /b 0
)
"%ISCC%" /DAppVersion=%VERSION% installer\StepViewer.iss
if errorlevel 1 exit /b 1
echo Done: build\StepViewerSetup.exe (version %VERSION%)
