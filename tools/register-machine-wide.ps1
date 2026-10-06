<#
.SYNOPSIS
  Makes per-user (HKCU) COM/ActiveX registrations visible to elevated processes by copying
  them to the machine-wide hive (HKLM).

.DESCRIPTION
  Legacy installers that run without elevation often register ActiveX controls only under
  HKCU\Software\Classes. Windows ignores per-user COM registrations in elevated processes, so the
  same program fails when started with "Run as administrator" (DynaRun V3 stops at
  "System Initializing. Please Wait. 115" because MSComm/MSCOMCTL cannot be created).

  Two traps this script handles:
   * regsvr32 run elevated does NOT fix it: while a class key exists under HKCU, writes through
     HKCR land in HKCU again.
   * A class key may already exist in HKLM but be incomplete (no InprocServer32 path). Elevated
     processes then still fail, so completeness is checked, not just existence.

  For every 32-bit (WOW6432Node) or 64-bit CLSID under HKCU whose InprocServer32 points to one of
  the selected files, the CLSID key, its ProgIDs and its TypeLib are copied to HKLM when HKLM lacks
  a complete registration. Existing HKLM keys that are touched are exported to a .reg backup first.

  See docs/ELEVATED_COM.md for the background and how to diagnose other programs.

.PARAMETER Files
  Server file names (e.g. mscomm32.ocx). Default: the controls used by DynaRun V3.

.PARAMETER FromExe
  Derive the file list from the OCX/DLL names referenced inside an executable (works for VB5/VB6
  programs, whose form data names every control's .ocx).

.PARAMETER BackupDir
  Where the .reg backup and the log of copied keys are written. Default: current directory.

.EXAMPLE
  .\register-machine-wide.ps1 -WhatIf
.EXAMPLE
  .\register-machine-wide.ps1 -FromExe 'C:\Program Files (x86)\Vendor\App.exe'
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string[]]$Files = @(
        'comctl32.ocx', 'comdlg32.ocx', 'csimctl5.ocx', 'cwpid.ocx', 'cwui.ocx', 'dguard2.ocx',
        'filev090.ocx', 'mscomctl.ocx', 'mscomm32.ocx', 'msdatgrd.ocx', 'msflxgrd.ocx', 'mshflxgd.ocx',
        'numled.ocx', 'pesgo32e.ocx', 'richtx32.ocx', 'shcmb090.ocx', 'tabctl32.ocx', 'thbres25.dll'),
    [string]$FromExe,
    [string]$BackupDir = (Get-Location).Path
)

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin -and -not $WhatIfPreference) { throw 'Run this script from an elevated PowerShell (or use -WhatIf).' }

if ($FromExe) {
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($FromExe))
    $Files = [regex]::Matches($text, '[A-Za-z0-9_\-]+\.(ocx|oca|dll)', 'IgnoreCase') |
        ForEach-Object { $_.Value.ToLower() -replace '\.oca$', '.ocx' } | Sort-Object -Unique |
        Where-Object {
            # skip Windows' own DLLs (shell32, user32, ...): their per-user entries are shell
            # extensions of other software and must not be promoted to machine-wide
            if ($_ -notlike '*.dll') { return $true }
            $f = @("$env:windir\SysWOW64\$_", "$env:windir\System32\$_") | Where-Object { Test-Path $_ } | Select-Object -First 1
            -not $f -or (Get-Item $f).VersionInfo.ProductName -notmatch 'Windows.*Operating System'
        }
    Write-Host "Files referenced by $([IO.Path]::GetFileName($FromExe)): $($Files -join ', ')"
}
$Files = $Files | ForEach-Object { $_.ToLower() }

function Get-Default([string]$path) { (Get-ItemProperty $path -ErrorAction SilentlyContinue).'(default)' }

# keys (relative to Software\Classes) that need copying
$keys = [System.Collections.Generic.List[string]]::new()
foreach ($view in 'WOW6432Node\CLSID', 'CLSID') {
    foreach ($clsid in Get-ChildItem "HKCU:\Software\Classes\$view" -ErrorAction SilentlyContinue) {
        $server = Get-Default "$($clsid.PSPath)\InprocServer32"
        if (-not $server -or $Files -notcontains [IO.Path]::GetFileName($server).ToLower()) { continue }
        $rel = "$view\$($clsid.PSChildName)"
        if (-not (Get-Default "HKLM:\SOFTWARE\Classes\$rel\InprocServer32")) { $keys.Add($rel) }

        $progId = Get-Default "$($clsid.PSPath)\ProgID"
        foreach ($p in @($progId, ($progId -replace '\.\d+$', '')) | Select-Object -Unique) {
            if ($p -and (Test-Path "HKCU:\Software\Classes\$p") -and -not (Get-Default "HKLM:\SOFTWARE\Classes\$p\CLSID")) {
                $keys.Add($p)
            }
        }

        $tlb = Get-Default "$($clsid.PSPath)\TypeLib"
        if ($tlb -and (Test-Path "HKCU:\Software\Classes\TypeLib\$tlb")) {
            $complete = Get-ChildItem "HKLM:\SOFTWARE\Classes\TypeLib\$tlb" -ErrorAction SilentlyContinue |
                Get-ChildItem -ErrorAction SilentlyContinue | Where-Object { Get-Default "$($_.PSPath)\win32" }
            if (-not $complete) { $keys.Add("TypeLib\$tlb") }
        }
    }
}
$keys = $keys | Select-Object -Unique
if (-not $keys) { Write-Host 'Nothing to do: all selected classes are registered machine-wide.'; return }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $BackupDir "hklm-classes-backup-$stamp.reg"
$log = Join-Path $BackupDir "hklm-classes-copied-$stamp.txt"
foreach ($k in $keys) {
    if (-not $PSCmdlet.ShouldProcess("HKLM\SOFTWARE\Classes\$k", 'copy from HKCU')) { continue }
    if (Test-Path "HKLM:\SOFTWARE\Classes\$k") {          # keep what was there before
        $tmp = [IO.Path]::GetTempFileName()
        & reg.exe export "HKLM\SOFTWARE\Classes\$k" $tmp /y | Out-Null
        Get-Content $tmp | Add-Content $backup
        Remove-Item $tmp
    }
    & reg.exe copy "HKCU\Software\Classes\$k" "HKLM\SOFTWARE\Classes\$k" /s /f | Out-Null
    "HKLM\SOFTWARE\Classes\$k" | Add-Content $log
    Write-Host "copied $k"
}
if (Test-Path $log) { Write-Host "Log of copied keys: $log" }
if (Test-Path $backup) { Write-Host "Backup of pre-existing HKLM keys: $backup" }
