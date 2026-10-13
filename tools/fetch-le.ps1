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

# SHA-256 of the four binaries as extracted from the verified release above. When all are present in -OutDir
# with these hashes (build.cmd runs this script on every build) nothing is downloaded; only THIRD-PARTY.txt
# and LEConfig.xml, which are generated/copied here, are brought up to date.
$bins = [ordered]@{
    'LEProc.exe'          = '2A81CB20B5B705D84416FD8B1CD9A819B1CAFAECCC0C1E23C289884310EC0461'
    'LoaderDll.dll'       = '82FAE0F44F4CA0C9C37907DF74CEF2415EEB5FAE1CF8D4F36F34FFCAF7E3CC0C'
    'LocaleEmulator.dll'  = 'C79C175FDAD174AA46A72197D148316299A56F950AABB1B84930D09EE1084A88'
    'LECommonLibrary.dll' = '6EDBB48B891E3ED830E7B487F352D90044474A757905FF49D3B2ABFDB980C211'
}
# SHA-256 of the licence texts at tag v$ver (the raw files are fetched over HTTPS but checked all the same).
$docs = [ordered]@{
    'COPYING'         = '8CEB4B9EE5ADEDDE47B31E975C1D90C73AD27B6B165A1DCD80C7C545EB65B903'
    'COPYING.LESSER'  = 'DA7EABB7BAFDF7D3AE5E9F223AA5BDC1EECE45AC569DC21B3B037520B4464768'
}
$third = @"
Locale Emulator $ver - https://github.com/xupefei/Locale-Emulator
Copyright (C) 2014 Paddy Xu (xupefei) and contributors (as stated in the AssemblyInfo of LEProc and LECommonLibrary: "Copyright (C) Paddy Xu 2014";
the source files of LoaderDll and LocaleEmulator carry no header, their README names Paddy Xu as the author; further contributors are listed in the
repositories' history). Licensed under the GNU Lesser General Public License v3.0 (COPYING.LESSER, COPYING).

The files in this folder are unmodified binaries from the official release
  $zip
  (SHA-256 $hash)
LECommonLibrary.dll is the copy embedded in that release's LEInstaller.exe.

Corresponding source code:
  LEProc, LECommonLibrary: https://github.com/xupefei/Locale-Emulator/tree/v$ver
  LoaderDll, LocaleEmulator: https://github.com/xupefei/Locale-Emulator-Core/tree/ae7160dc5deb97947396abcd784f9b98b6ee38b3
Forks of the same sources kept by the DynaRunFix maintainer:
  Locale Emulator (tag v2.5.0.1): https://github.com/timliudev/Locale-Emulator/tree/v2.5.0.1
  Locale Emulator Core (commit ae7160dc5deb97947396abcd784f9b98b6ee38b3): https://github.com/timliudev/Locale-Emulator-Core/tree/ae7160dc5deb97947396abcd784f9b98b6ee38b3

LEConfig.xml is DynaRunFix's own profile (zh-TW) and is part of DynaRunFix (MIT).
DynaRunFix only starts LEProc.exe as a separate program; you may replace these files with any other
build of Locale Emulator.
"@

# THIRD-PARTY.txt is written whenever it is missing or differs from the text above (an older build\le may hold older text).
function Set-ThirdParty {
    $f = Join-Path $OutDir 'THIRD-PARTY.txt'
    $want = $third -replace "`r?`n", "`r`n"
    if (-not (Test-Path $f) -or ([IO.File]::ReadAllText($f).Trim([char]0xFEFF, "`r", "`n") -replace "`r?`n", "`r`n") -ne $want.Trim()) {
        Set-Content $f $third -Encoding utf8
        Write-Host 'THIRD-PARTY.txt written'
    }
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
$have = $true
foreach ($n in $docs.Keys) {
    $f = Join-Path $OutDir $n
    if (-not (Test-Path $f) -or (Get-FileHash $f -Algorithm SHA256).Hash -ne $docs[$n]) { $have = $false }
}
foreach ($n in $bins.Keys) {
    $f = Join-Path $OutDir $n
    if (-not (Test-Path $f) -or (Get-FileHash $f -Algorithm SHA256).Hash -ne $bins[$n]) { $have = $false }
}
if ($have) {
    Copy-Item (Join-Path $PSScriptRoot '..\le\LEConfig.xml') $OutDir -Force
    Set-ThirdParty
    Write-Host 'Locale Emulator files present and verified, not downloading'
    Get-ChildItem $OutDir | Select-Object Name, Length
    return
}

$tmp = Join-Path ([IO.Path]::GetTempPath()) "le-$([guid]::NewGuid())"
New-Item -ItemType Directory -Force $tmp | Out-Null
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
    foreach ($n in $docs.Keys) {
        Invoke-WebRequest "$raw/$n" -OutFile "$tmp\$n" -UseBasicParsing
        $got = (Get-FileHash "$tmp\$n" -Algorithm SHA256).Hash
        if ($got -ne $docs[$n]) { throw "$n hash mismatch: $got" }
        Copy-Item "$tmp\$n" "$OutDir\$n" -Force
    }
    Set-ThirdParty
}
finally { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
Get-ChildItem $OutDir | Select-Object Name, Length
