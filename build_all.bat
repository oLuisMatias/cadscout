@echo off
rem Builds both apps and stages the packages in build\package\Lite and build\package\Full.
rem Run from anywhere: build_all.bat
cd /d "%~dp0"

echo === 0/2 Configuring (reads the version from vcpkg.json) ===
cmake -S . -B build >nul
if errorlevel 1 (
    echo CONFIGURE FAILED. Run "cmake -S . -B build" to see the reason.
    exit /b 1
)
set /p VERSION=<build\version.txt
echo Version %VERSION%

echo === 1/2 Building CADscout (Full) and CADscoutLite ===
cmake --build build --config Release --target CADscout CADscoutLite CADscoutLiteMenu
if errorlevel 1 (
    echo BUILD FAILED. If CADscoutLite.exe is open, close it and run this again.
    exit /b 1
)

echo === 2/2 Staging the packages ===
cmake --install build --config Release --prefix build\package\Lite --component Lite >nul || exit /b 1
cmake --install build --config Release --prefix build\package\Full --component Lite >nul || exit /b 1
cmake --install build --config Release --prefix build\package\Full --component Full >nul || exit /b 1
echo Done: Full and Lite are built and staged in build\package (version %VERSION%).
