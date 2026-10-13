@echo off
rem Builds and runs the unit tests (host-native, with the CRT; not part of the shipped binaries).
rem Exit code 0 = all passed.
setlocal
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (echo vswhere.exe not found - install Visual Studio & exit /b 1)
rem a pre-release Visual Studio is used only when asked for (local use); the CI image has a release one
set VSPRE=
if defined DRF_ALLOW_PRERELEASE set VSPRE=-prerelease
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest %VSPRE% -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo No Visual Studio with C++ tools found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0.."
if not exist build mkdir build
cl /nologo /W3 /utf-8 /Fobuild\ /Fe:build\test_shexec_match.exe tests\test_shexec_match.c kernel32.lib || exit /b 1
build\test_shexec_match.exe
exit /b %errorlevel%
