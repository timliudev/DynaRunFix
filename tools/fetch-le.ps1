# Downloads Locale Emulator 2.5.0.1 (LGPL-3.0, https://github.com/xupefei/Locale-Emulator), verifies it
# and puts the files DynaRunFix needs into -OutDir (normally "le" next to DynaRunFix.exe):
#   LEProc.exe, LoaderDll.dll, LocaleEmulator.dll  - unmodified from the release zip
#   LECommonLibrary.dll                            - unmodified, taken out of LEInstaller.exe's resources
#                                                    (the installer would put it in the GAC; we don't install)
#   LEConfig.xml                                   - DynaRunFix's zh-TW profile (le\LEConfig.xml in this repo)
#   COPYING, COPYING.LESSER, THIRD-PARTY.txt       - licence texts and where the sources are
# Nothing is installed or registered; LE's shell extension and installer are not used.
param([Parameter(Mandatory)][string]$OutDir)
$ErrorActionPreference = 'Stop'

$ver  = '2.5.0.1'
$zip  = "https://github.com/xupefei/Locale-Emulator/releases/download/v$ver/Locale.Emulator.$ver.zip"
$hash = '808FF584426D52CC775AD6406DA00622F454BE95BD4C8FBCA42EEF4B7235AD5C'
$raw  = "https://raw.githubusercontent.com/xupefei/Locale-Emulator/v$ver"

$tmp = Join-Path ([IO.Path]::GetTempPath()) "le-$([guid]::NewGuid())"
New-Item -ItemType Directory -Force $tmp, $OutDir | Out-Null
try {
    Invoke-WebRequest $zip -OutFile "$tmp\le.zip" -UseBasicParsing
    $got = (Get-FileHash "$tmp\le.zip" -Algorithm SHA256).Hash
    if ($got -ne $hash) { throw "Locale Emulator zip hash mismatch: $got" }
    Expand-Archive "$tmp\le.zip" "$tmp\x"
    Copy-Item "$tmp\x\LEProc.exe", "$tmp\x\LoaderDll.dll", "$tmp\x\LocaleEmulator.dll" $OutDir

    # LEInstaller is a 32-bit .NET Framework program; read its resource with 32-bit Windows PowerShell.
    $ps32 = "$env:SystemRoot\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
    $cmd = @"
`$a = [Reflection.Assembly]::LoadFrom('$tmp\x\LEInstaller.exe')
`$r = New-Object System.Resources.ResourceReader(`$a.GetManifestResourceStream('LEInstaller.Properties.Resources.resources'))
`$e = `$r.GetEnumerator(); while (`$e.MoveNext()) { if (`$e.Key -eq 'LECommonLibrary') { [IO.File]::WriteAllBytes('$OutDir\LECommonLibrary.dll', `$e.Value) } }
"@
    & $ps32 -NoProfile -NonInteractive -Command $cmd
    if (-not (Test-Path "$OutDir\LECommonLibrary.dll")) { throw 'LECommonLibrary.dll not found in LEInstaller.exe' }

    Copy-Item (Join-Path $PSScriptRoot '..\le\LEConfig.xml') $OutDir
    Invoke-WebRequest "$raw/COPYING" -OutFile "$OutDir\COPYING" -UseBasicParsing
    Invoke-WebRequest "$raw/COPYING.LESSER" -OutFile "$OutDir\COPYING.LESSER" -UseBasicParsing
    @"
Locale Emulator $ver - https://github.com/xupefei/Locale-Emulator
Copyright (C) the Locale Emulator authors. Licensed under the GNU Lesser General Public License v3.0 (COPYING.LESSER, COPYING).

The files in this folder are unmodified binaries from the official release
  $zip
  (SHA-256 $hash)
LECommonLibrary.dll is the copy embedded in that release's LEInstaller.exe.

Corresponding source code:
  LEProc, LECommonLibrary: https://github.com/xupefei/Locale-Emulator/tree/v$ver
  LoaderDll, LocaleEmulator: https://github.com/xupefei/Locale-Emulator-Core/tree/ae7160dc5deb97947396abcd784f9b98b6ee38b3

LEConfig.xml is DynaRunFix's own profile (zh-TW) and is part of DynaRunFix (MIT).
DynaRunFix only starts LEProc.exe as a separate program; you may replace these files with any other
build of Locale Emulator.
"@ | Set-Content "$OutDir\THIRD-PARTY.txt" -Encoding utf8
}
finally { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
Get-ChildItem $OutDir | Select-Object Name, Length
