@echo off
rem Builds DynaRunFix (x86, no CRT dependency, runs on Windows XP .. Windows 11).
rem Requires Visual Studio (any edition with the "Desktop development with C++" workload).
rem DRF_VERSION (e.g. 1.1.0) sets the version shown in "Programs and Features"; default "dev" (shown as the commit hash).
setlocal
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (echo vswhere.exe not found - install Visual Studio & exit /b 1)
rem a pre-release Visual Studio is used only when asked for (local use, DRF_ALLOW_PRERELEASE=1); the CI image has a release one
set VSPRE=
if defined DRF_ALLOW_PRERELEASE set VSPRE=-prerelease
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest %VSPRE% -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo No Visual Studio with C++ tools found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1

cd /d "%~dp0"
if not exist build mkdir build
rem DRF_COMMIT = short git hash, "-dirty" if the work tree has changes, "unknown" without git.
rem DRF_DISPLAY = what is shown (wizard title, Programs and Features, dynafix.log): DRF_VERSION, or DRF_COMMIT for a dev build.
rem DRF_VERSION stays "dev" in a dev build: the update check treats that as "not a release" and never updates.
set DRF_COMMIT=unknown
set DRF_DIRTY=
for /f "delims=" %%i in ('git rev-parse --short HEAD 2^>nul') do set DRF_COMMIT=%%i
if not "%DRF_COMMIT%"=="unknown" for /f "delims=" %%i in ('git status --porcelain 2^>nul') do set DRF_DIRTY=1
if defined DRF_DIRTY set DRF_COMMIT=%DRF_COMMIT%-dirty
set DRF_DISPLAY=%DRF_VERSION%
if not defined DRF_VERSION set DRF_VERSION=dev
if not defined DRF_DISPLAY set DRF_DISPLAY=%DRF_COMMIT%
> build\version.h echo #define DRF_VERSION "%DRF_VERSION%"
>> build\version.h echo #define DRF_COMMIT "%DRF_COMMIT%"
>> build\version.h echo #define DRF_DISPLAY "%DRF_DISPLAY%"
set CFLAGS=/nologo /O1 /GS- /W3 /utf-8 /Ibuild /Fobuild\
rem src\loadcfg.c provides the load configuration the linker needs for /DEPENDENTLOADFLAG without the C runtime (else LNK4266, flag not applied).
rem /DEPENDENTLOADFLAG:0x800 (LOAD_LIBRARY_SEARCH_SYSTEM32): the two exes load their DLLs only from the system folder (the setup is often started
rem from Downloads). Ignored by systems that do not know it; the setup also calls SetDefaultDllDirectories and loads the delay-loaded
rem msi.dll / wininet.dll by full system path (delay-load hook in setup.c).
set LFLAGS=/nologo /NODEFAULTLIB
set DLFLAGS=/DEPENDENTLOADFLAG:0x800

cl %CFLAGS% /c src\dynafix.c src\launcher.c src\loadcfg.c || exit /b 1
rem installer: miniz (third_party\miniz, inflate only) and the parts that use it share these options
set MZFLAGS=/DNDEBUG /DMINIZ_NO_STDIO /DMINIZ_NO_TIME /DMINIZ_NO_DEFLATE_APIS /DMINIZ_NO_ARCHIVE_APIS /DMINIZ_NO_ZLIB_APIS /DMINIZ_NO_MALLOC /Ithird_party\miniz
cl %CFLAGS% %MZFLAGS% /W0 /Gy /Gs1000000 /c third_party\miniz\miniz.c || exit /b 1
cl %CFLAGS% %MZFLAGS% /c src\setup.c src\wizard.c src\package.c src\update.c src\crt.c || exit /b 1
link %LFLAGS% /SAFESEH:NO /DLL /ENTRY:DllMain /SUBSYSTEM:WINDOWS,5.01 /OUT:build\dynafix.dll /IMPLIB:build\dynafix.lib build\dynafix.obj kernel32.lib user32.lib gdi32.lib advapi32.lib || exit /b 1
link %LFLAGS% %DLFLAGS% /ENTRY:WinMainCRTStartup /SUBSYSTEM:WINDOWS,5.01 /OUT:build\DynaRunFix.exe build\launcher.obj build\loadcfg.obj kernel32.lib user32.lib advapi32.lib shell32.lib || exit /b 1

rem The installer embeds the launcher, the dll and the code-page manifest. Its own manifest
rem (src\setup.manifest) says asInvoker: Windows' installer detection would elevate it otherwise.
rem Locale Emulator for the installer payload (downloaded once into build\le, SHA-256 checked;
rem later builds only refresh THIRD-PARTY.txt there when it differs from the committed copy).
rem A PSModulePath inherited from PowerShell 7 (or a terminal started by it) hides Windows PowerShell 5.1's own modules
rem (Get-FileHash is then "not recognized"), so the child gets the default one.
set PSModulePath=
powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch-le.ps1 -OutDir build\le >nul || exit /b 1
rc /nologo /Ibuild /fo build\setup.res src\setup.rc || exit /b 1
link %LFLAGS% %DLFLAGS% /DELAYLOAD:msi.dll /DELAYLOAD:wininet.dll /ENTRY:WinMainCRTStartup /SUBSYSTEM:WINDOWS,5.01 /MANIFEST:EMBED /MANIFESTINPUT:src\setup.manifest /MANIFESTUAC:NO /OPT:REF /OUT:build\DynaRunFix-Setup.exe build\setup.obj build\wizard.obj build\package.obj build\update.obj build\crt.obj build\loadcfg.obj build\miniz.obj build\setup.res kernel32.lib user32.lib gdi32.lib advapi32.lib shell32.lib ole32.lib comdlg32.lib comctl32.lib wininet.lib msi.lib uuid.lib delayimp.lib || exit /b 1
echo.
echo Build OK: build\DynaRunFix-Setup.exe (installer), build\DynaRunFix.exe, build\dynafix.dll
