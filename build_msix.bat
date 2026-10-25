@echo off
rem Builds build\CADscoutLite.msix (the Microsoft Store package of CADscout Lite) from build\package\Lite.
rem Run build_all.bat first. Needs the Windows SDK (makeappx.exe). Run from anywhere: build_msix.bat
cd /d "%~dp0"

if not exist "build\package\Lite\CADscoutLite.exe" (
    echo build\package\Lite\CADscoutLite.exe is missing. Run build_all.bat first.
    exit /b 1
)

set "MAKEAPPX="
for /f "delims=" %%F in ('dir /b /s "%ProgramFiles(x86)%\Windows Kits\10\bin\makeappx.exe" 2^>nul ^| findstr /i "\\x64\\"') do set "MAKEAPPX=%%F"
if not defined MAKEAPPX (
    echo makeappx.exe was not found. Install the Windows 10/11 SDK.
    exit /b 1
)

set /p VERSION=<build\version.txt
set "STAGE=build\msix_stage"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" || exit /b 1
xcopy /e /i /q /y "build\package\Lite\*" "%STAGE%\" >nul || exit /b 1
xcopy /e /i /q /y "msix\Assets" "%STAGE%\Assets\" >nul || exit /b 1

rem The Store wants a 4-part version whose last part is 0.
powershell -NoProfile -Command "(Get-Content -Raw 'msix\AppxManifest.xml').Replace('@VERSION@','%VERSION%.0') | Set-Content -Encoding UTF8 '%STAGE%\AppxManifest.xml'" || exit /b 1

"%MAKEAPPX%" pack /o /d "%STAGE%" /p "build\CADscoutLite.msix"
if errorlevel 1 exit /b 1
echo Done: build\CADscoutLite.msix (version %VERSION%.0). Unsigned: the Store signs the package it publishes.
