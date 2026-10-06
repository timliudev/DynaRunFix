@echo off
rem Builds DynaRunFix (x86, no CRT dependency, runs on Windows XP .. Windows 11).
rem Requires Visual Studio (any edition with the "Desktop development with C++" workload).
rem DRF_VERSION (e.g. 1.1.0) sets the version shown in "Programs and Features"; default "dev".
setlocal
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (echo vswhere.exe not found - install Visual Studio & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo No Visual Studio with C++ tools found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1

cd /d "%~dp0"
if not exist build mkdir build
if not defined DRF_VERSION set DRF_VERSION=dev
> build\version.h echo #define DRF_VERSION "%DRF_VERSION%"
set CFLAGS=/nologo /O1 /GS- /W3 /utf-8 /Ibuild /Fobuild\
set LFLAGS=/nologo /NODEFAULTLIB

cl %CFLAGS% /c src\dynafix.c src\launcher.c src\setup.c tools\msgspy\msgspy.c tools\msgspy\msgspy_dll.c || exit /b 1
link %LFLAGS% /DLL /ENTRY:DllMain /SUBSYSTEM:WINDOWS,5.01 /OUT:build\dynafix.dll /IMPLIB:build\dynafix.lib build\dynafix.obj kernel32.lib user32.lib || exit /b 1
link %LFLAGS% /ENTRY:WinMainCRTStartup /SUBSYSTEM:WINDOWS,5.01 /OUT:build\DynaRunFix.exe build\launcher.obj kernel32.lib user32.lib advapi32.lib shell32.lib || exit /b 1
link %LFLAGS% /DLL /ENTRY:DllMain /SUBSYSTEM:WINDOWS,5.01 /OUT:build\msgspy.dll /IMPLIB:build\msgspy.lib build\msgspy_dll.obj kernel32.lib user32.lib || exit /b 1
link %LFLAGS% /ENTRY:mainCRTStartup /SUBSYSTEM:CONSOLE,5.01 /OUT:build\msgspy.exe build\msgspy.obj kernel32.lib user32.lib gdi32.lib || exit /b 1

rem The installer embeds the launcher, the dll and the code-page manifest. Its own manifest says asInvoker,
rem otherwise Windows' installer detection ("Setup" in the name) would elevate the per-user stage too.
rem Locale Emulator for the installer payload (downloaded once into build\le, SHA-256 checked).
if not exist build\le\LEProc.exe powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch-le.ps1 -OutDir build\le >nul || exit /b 1
rc /nologo /Ibuild /fo build\setup.res src\setup.rc || exit /b 1
link %LFLAGS% /ENTRY:WinMainCRTStartup /SUBSYSTEM:WINDOWS,5.01 /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'" /OUT:build\DynaRunFix-Setup.exe build\setup.obj build\setup.res kernel32.lib user32.lib advapi32.lib shell32.lib ole32.lib comdlg32.lib uuid.lib || exit /b 1
echo.
echo Build OK: build\DynaRunFix-Setup.exe (installer), build\DynaRunFix.exe, build\dynafix.dll
echo           (and diagnostic tool build\msgspy.exe / msgspy.dll)
