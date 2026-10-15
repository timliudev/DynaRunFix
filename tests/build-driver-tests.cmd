@echo off
rem Builds and runs the driver tests (host-native, with the CRT; not part of the shipped binaries): src\driver.c, the multi-entry zip extraction
rem of src\package.c and the INF / code integrity / task XML helpers. Needs build.cmd to have run (build\version.h, build\package.obj, build\miniz.obj).
rem Only %TEMP%\drf-drv-<pid> is touched (deleted at the end); no system tool is started. Optional: set DRF_DRIVER_ZIP to the real "Dyna Pro USB.zip"
rem to check the pinned SHA-256, the extraction and the INF matcher against it. Exit code 0 = all passed.
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
if not exist build\version.h (echo build\version.h missing - run build.cmd first & exit /b 1)
if not exist build\tests mkdir build\tests
rem (setup.c and driver.c are compiled into the test; the embedded manifest keeps Windows from asking for elevation)
cl /nologo /W3 /utf-8 /Ibuild /Isrc /Fobuild\tests\d_ /Fe:build\tests\test_driver.exe tests\test_driver.c build\package.obj build\miniz.obj kernel32.lib user32.lib gdi32.lib advapi32.lib shell32.lib ole32.lib comdlg32.lib wininet.lib msi.lib uuid.lib /link /MANIFEST:EMBED /MANIFESTINPUT:src\setup.manifest /MANIFESTUAC:NO || exit /b 1
build\tests\test_driver.exe
exit /b %errorlevel%
