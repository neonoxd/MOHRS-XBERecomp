@echo off
rem MOHRS-XBERecomp.zip: both executables, the compiler folder the first start builds the game code with, an empty
rem assets folder and the player's README. No game code or game data goes in: the player's own disc image is
rem translated and built on their PC.
setlocal
cd /d "%~dp0"
set "OUT=out\build\release"
set "STAGE=%OUT%\release"
rem one name for every release, so a releases/latest/download link stays working
set "ZIP=MOHRS-XBERecomp.zip"
set "D3DCOMPILER=%ProgramFiles(x86)%\Windows Kits\10\Redist\D3D\x64\d3dcompiler_47.dll"
if not exist "%D3DCOMPILER%" (echo d3dcompiler_47.dll not found: install the Windows SDK from the Visual Studio Installer & exit /b 1)
rem the Visual C++ runtime beside the executables, so a fresh Windows install needs nothing more
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
for /d %%v in ("%VSDIR%\VC\Redist\MSVC\1*") do for /d %%c in ("%%v\x64\Microsoft.VC*.CRT") do set "VCRUNTIME=%%c"
if not exist "%VCRUNTIME%\vcruntime140.dll" (echo Visual C++ runtime not found: install Visual Studio with the C++ workload & exit /b 1)
where 7z >nul 2>nul || (echo 7z not found: install 7-Zip and put it on PATH & exit /b 1)

rem the first build goes through build.bat (symbols, toolchain checks); later ones are brought up to date here
if not exist "%OUT%\MOHRS.exe" (call build.bat || exit /b 1) else (call tools\dev.bat cmake --build %OUT% || exit /b 1)
if not exist "%OUT%\MOHRSCoop.exe" (echo %OUT%\MOHRSCoop.exe is missing & exit /b 1)
if not exist "%OUT%\sdk_dir.txt" (echo %OUT%\sdk_dir.txt was not written, update the SDK. & exit /b 1)
set /p SDK=<%OUT%\sdk_dir.txt
set /p VERSION=<%OUT%\version.txt

rd /s /q "%STAGE%" 2>nul
mkdir "%STAGE%\assets" || exit /b 1
copy /y "%OUT%\MOHRS.exe" "%STAGE%\" >nul || exit /b 1
copy /y "%OUT%\MOHRSCoop.exe" "%STAGE%\" >nul || exit /b 1
for %%f in (msvcp140 vcruntime140 vcruntime140_1) do copy /y "%VCRUNTIME%\%%f.dll" "%STAGE%\" >nul || exit /b 1
copy /y "%D3DCOMPILER%" "%STAGE%\" >nul || exit /b 1
copy /y "%SDK%\THIRD_PARTY.txt" "%STAGE%\" >nul || exit /b 1
copy /y LICENSE "%STAGE%\LICENSE.txt" >nul || exit /b 1
copy /y "%SDK%\LICENSE" "%STAGE%\LICENSE-XboxXBE-Recomp.txt" >nul || exit /b 1
rem the compiler folder without its work folder (the build's game code); xbe_recomp.exe needs its own runtime copy,
rem Windows only looks beside the exe it starts
robocopy "%OUT%\compiler" "%STAGE%\compiler" /e /xd "%CD%\%OUT%\compiler\work" /xf *.dll /njh /njs /nfl /ndl >nul
if errorlevel 8 (echo copying the compiler folder failed & exit /b 1)
for %%f in (msvcp140 vcruntime140) do copy /y "%VCRUNTIME%\%%f.dll" "%STAGE%\compiler\" >nul || exit /b 1
powershell -NoProfile -Command "(Get-Content -Raw docs\player-readme.txt).Replace('@VERSION@', '%VERSION%') | Set-Content -NoNewline -Encoding ascii '%STAGE%\README.txt'" || exit /b 1

del /q "%ZIP%" 2>nul
pushd "%STAGE%"
7z a -tzip "%~dp0%ZIP%" * >nul || (popd & exit /b 1)
popd
echo %ZIP%: MOHRS-XBERecomp %VERSION%
endlocal
