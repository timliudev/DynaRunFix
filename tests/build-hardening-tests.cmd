@echo off
rem Builds and runs the installer hardening tests (host-native, with the CRT; not part of the shipped binaries).
rem Needs build.cmd to have run (build\version.h, build\package.obj, build\miniz.obj, build\setup.res: the embedded manifest text is tested; the asInvoker manifest keeps Windows from asking for elevation for the setup.res strings). Exit code 0 = all passed.
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
if not exist build\tests mkdir build\tests
cl /nologo /W3 /utf-8 /Ibuild /Isrc /Fobuild\tests\ /Fe:build\tests\test_hardening.exe tests\test_hardening.c build\package.obj build\miniz.obj build\setup.res kernel32.lib user32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib comdlg32.lib wininet.lib msi.lib uuid.lib /link /MANIFEST:EMBED /MANIFESTINPUT:src\setup.manifest /MANIFESTUAC:NO || exit /b 1
build\tests\test_hardening.exe || exit /b 1
rem (the exe is not called *update*: Windows would ask for elevation) test_update.c uses its own version.h (this build counts as release 1.0.0)
cl /nologo /W3 /utf-8 /Itests\fakever /Isrc /Fobuild\tests\u_ /Fe:build\tests\test_relcheck.exe tests\test_update.c kernel32.lib user32.lib gdi32.lib advapi32.lib wininet.lib || exit /b 1
build\tests\test_relcheck.exe
exit /b %errorlevel%