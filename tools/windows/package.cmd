@echo off
rem Builds the package for other players (docs/windows.md, Packaging):
rem build-win\dist\mw3-<git describe>-win64\ and its .zip, with mw3.exe, the
rem 32-bit Indeo helper mw3indeo.exe (the movies through the install's own
rem codec), mw3import.exe (the movies converted when the helper cannot play
rem them, and the disc image's music), every DLL they load (SDL3, SDL3_ttf,
rem SDL3_sound, FFmpeg's, the Visual C++ runtime), README.txt,
rem THIRD-PARTY.txt and the libraries' licence files, and the installer
rem MW3-Rebuild-Setup-<git describe>.exe (Inno Setup, tools\windows\installer.iss).
rem Nothing from the game. Its own build tree, build-win-pkg,
rem configured with MW3_PACKAGE (no development copy of the game in the exes).
rem Usage: tools\windows\package.cmd
setlocal
cd /d "%~dp0\..\.."
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS=%%i
if not defined VS (echo No Visual Studio with the C++ tools found. & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
for /d %%d in (deps\SDL3-3*) do set SDL3_DIR=%CD%\%%d\cmake
for /d %%d in (deps\SDL3_ttf-*) do set SDL3_ttf_DIR=%CD%\%%d\cmake
for /d %%d in (deps\SDL3_sound-*) do set SDL3_sound_ROOT=%CD%\%%d
if not defined SDL3_sound_ROOT echo Warning: no deps\SDL3_sound-*: the package's music plays .wav only.
rem SETUP's archives (docs/setup-sources.md): a package always reads them.
if not exist deps\archive\lib\archive.lib (echo No deps\archive: run tools\windows\build_deps_archive.cmd first. & exit /b 1)
if not exist deps\archive\share\licenses\libarchive-3.7.9\COPYING (echo deps\archive has no licences: run tools\windows\build_deps_archive.cmd again. & exit /b 1)

rem The build, from scratch each time so the version and the switches are this checkout's.
if exist build-win-pkg rmdir /s /q build-win-pkg
cmake -S . -B build-win-pkg -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
  -DMW3_PACKAGE=ON "-DSDL3_DIR=%SDL3_DIR%" "-DSDL3_ttf_DIR=%SDL3_ttf_DIR%" || exit /b 1
cmake --build build-win-pkg --target mw3 mw3launcher mw3import || exit /b 1
if not exist build-win-pkg\mw3import.exe (echo mw3import.exe was not built: FFmpeg's shared build is missing from deps\ ^(docs/windows.md^). & exit /b 1)
rem The helper, 32-bit (its own vcvarsall x86, in a child shell).
cmd /c tools\windows\build_indeo.cmd build-win-pkg || exit /b 1

for /f "usebackq delims=" %%v in (`git describe --tags --match "v[0-9]*" --always --dirty 2^>nul`) do set VER=%%v
if not defined VER set VER=dev
set NAME=mw3-%VER%-win64
set DIST=build-win\dist\%NAME%
if exist "%DIST%" rmdir /s /q "%DIST%"
if exist "build-win\dist\%NAME%.zip" del "build-win\dist\%NAME%.zip"
mkdir "%DIST%\licenses" || exit /b 1

rem The three programs and the DLLs their builds copied beside them.
copy /y build-win-pkg\mw3.exe "%DIST%\" >nul || exit /b 1
copy /y build-win-pkg\mw3launcher.exe "%DIST%\" >nul || exit /b 1
copy /y build-win-pkg\mw3indeo.exe "%DIST%\" >nul || exit /b 1
copy /y build-win-pkg\mw3import.exe "%DIST%\" >nul || exit /b 1
copy /y build-win-pkg\*.dll "%DIST%\" >nul || exit /b 1
rem The Visual C++ runtime, app-local (a player may not have the redistributable).
for /d %%d in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do (
  for %%f in (msvcp140.dll vcruntime140.dll vcruntime140_1.dll) do if exist "%%d\%%f" copy /y "%%d\%%f" "%DIST%\" >nul
)

rem The read-me, the notices, and each library's own licence files.
copy /y tools\windows\dist\README.txt "%DIST%\" >nul || exit /b 1
rem The launcher's art (ours, assets/launcher: generated, with its prompts).
mkdir "%DIST%\assets\launcher"
copy /y assets\launcher\*.png "%DIST%\assets\launcher\" >nul || exit /b 1
copy /y tools\windows\dist\THIRD-PARTY.txt "%DIST%\" >nul || exit /b 1
if exist LICENSE copy /y LICENSE "%DIST%\LICENSE.txt" >nul
rem libarchive, zlib and liblzma: linked into mw3.exe, their licences from
rem tools\windows\build_deps_archive.cmd's prefix.
for /d %%d in (deps\archive\share\licenses\*) do (
  mkdir "%DIST%\licenses\%%~nxd" 2>nul
  copy /y "%%d\*" "%DIST%\licenses\%%~nxd\" >nul || exit /b 1
)
for /d %%d in (deps\*) do if /i not "%%~nxd"=="archive" (
  mkdir "%DIST%\licenses\%%~nxd" 2>nul
  for %%f in ("%%d\LICENSE*" "%%d\COPYING*" "%%d\README-SDL.txt") do copy /y "%%f" "%DIST%\licenses\%%~nxd\" >nul
)
rem FFmpeg's version and configure line (THIRD-PARTY.txt).
build-win-pkg\mw3import.exe --version > "%DIST%\licenses\ffmpeg-configuration.txt" || exit /b 1
for /d %%d in ("%DIST%\licenses\*") do dir /b /a-d "%%d" >nul 2>nul || echo Warning: no licence file found for %%~nxd: copy it into licenses\%%~nxd by hand.
mkdir "%DIST%\licenses\pl_mpeg"
copy /y third_party\pl_mpeg\README.md "%DIST%\licenses\pl_mpeg\" >nul || exit /b 1
mkdir "%DIST%\licenses\enet"
copy /y third_party\enet\LICENSE "%DIST%\licenses\enet\" >nul || exit /b 1
mkdir "%DIST%\licenses\miniupnpc"
copy /y third_party\miniupnpc\LICENSE "%DIST%\licenses\miniupnpc\" >nul || exit /b 1
mkdir "%DIST%\licenses\miniz"
copy /y third_party\miniz\LICENSE "%DIST%\licenses\miniz\" >nul || exit /b 1

rem Every DLL the programs load is in the folder or part of Windows; no game file is.
powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\check_dist.ps1 "%DIST%" || exit /b 1

tar -a -c -f "build-win\dist\%NAME%.zip" -C build-win\dist "%NAME%" || exit /b 1

rem The installer, MW3-Rebuild-Setup-<version>.exe (tools\windows\installer.iss),
rem with Inno Setup's compiler: iscc on the PATH (scoop's shim), else its usual folders.
set ISCC=
for %%i in (iscc.exe) do if not "%%~$PATH:i"=="" set "ISCC=%%~$PATH:i"
for %%d in ("%LOCALAPPDATA%\Programs\Inno Setup 7" "%LOCALAPPDATA%\Programs\Inno Setup 6" "%ProgramFiles(x86)%\Inno Setup 7" "%ProgramFiles(x86)%\Inno Setup 6" "%ProgramFiles%\Inno Setup 7") do (
  if not defined ISCC if exist "%%~d\ISCC.exe" set "ISCC=%%~d\ISCC.exe"
)
if not defined ISCC (
  echo Warning: Inno Setup's ISCC.exe not found, so no installer was made ^(docs/windows.md, Packaging^).
  goto :done
)
"%ISCC%" /Qp "/DAppVersion=%VER%" "/DDist=%CD%\%DIST%" "/O%CD%\build-win\dist" tools\windows\installer.iss || exit /b 1
echo Setup:   build-win\dist\MW3-Rebuild-Setup-%VER%.exe

:done
echo Package: %DIST%
echo Zip:     build-win\dist\%NAME%.zip
