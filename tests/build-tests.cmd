@echo off
rem Builds the log test harness (tests\logtest.c). Not part of build.cmd / the release.
rem Usage: tests\build-tests.cmd [outdir] [path\to\dynafix.c] [path\to\launcher.c] [extra cl options, e.g. /DOLD_SRC for a revision without w2u]
rem   outdir defaults to build\tests; the two sources default to ..\src (give another copy to test an older revision).
rem Needs build\version.h, i.e. run build.cmd once first. The harness uses the normal CRT with /GS and /RTC1
rem (stack cookies and run-time checks), unlike the release build, so an overflow in logf/llog is caught.
setlocal
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
rem a pre-release Visual Studio is used only when asked for (local use); the CI image has a release one
set VSPRE=
if defined DRF_ALLOW_PRERELEASE set VSPRE=-prerelease
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest %VSPRE% -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo No Visual Studio with C++ tools found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0.."
if not exist build\version.h (echo build\version.h missing - run build.cmd first & exit /b 1)
set OUT=%~1
if not defined OUT set OUT=build\tests
if not exist "%OUT%" mkdir "%OUT%"
set DSRC=%~2
if not defined DSRC set DSRC=%CD%\src\dynafix.c
set LSRC=%~3
if not defined LSRC set LSRC=%CD%\src\launcher.c
set CF=/nologo /Od /GS /RTC1 /W3 /utf-8 /Ibuild /D_CRT_SECURE_NO_WARNINGS %~4
cl %CF% /DSRC=\"%DSRC:\=/%\" /Fo"%OUT%\\" /Fe"%OUT%\logtest.exe" tests\logtest.c /link user32.lib advapi32.lib shell32.lib gdi32.lib || exit /b 1
cl %CF% /DTEST_LAUNCHER /DSRC=\"%LSRC:\=/%\" /Fo"%OUT%\\" /Fe"%OUT%\logtest_l.exe" tests\logtest.c /link user32.lib advapi32.lib shell32.lib gdi32.lib || exit /b 1
echo Tests built in %OUT%
