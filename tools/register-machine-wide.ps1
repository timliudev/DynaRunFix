<#
.SYNOPSIS
  Copies DynaRun V3's per-user ActiveX registrations to the machine-wide (HKLM) hive.

.DESCRIPTION
  When the DynaRun V3 installer runs without elevation, some of its ActiveX controls
  (MSComm, CommonDialog, MSFlexGrid, ...) are registered only under HKCU\Software\Classes.
  Elevated processes ignore per-user COM registrations, so "Run as administrator" fails to
  create MSComm and initialisation stops at "System Initializing. Please Wait. 115".

  regsvr32 does not help here: when a class key already exists under HKCU, writes through
  HKCR land in HKCU again. This script copies the CLSID and ProgID keys explicitly.

  Run from an elevated PowerShell. Use -WhatIf to only list what would be copied.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string[]]$Files = @(
        'comctl32.ocx', 'comdlg32.ocx', 'csimctl5.ocx', 'cwpid.ocx', 'cwui.ocx', 'dguard2.ocx',
        'filev090.ocx', 'mscomctl.ocx', 'mscomm32.ocx', 'msdatgrd.ocx', 'msflxgrd.ocx', 'numled.ocx',
        'pesgo32e.ocx', 'richtx32.ocx', 'shcmb090.ocx', 'tabctl32.ocx', 'thbres25.dll')
)

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin -and -not $WhatIfPreference) { throw 'Run this script from an elevated PowerShell.' }

$keys = [System.Collections.Generic.List[string]]::new()
foreach ($clsid in Get-ChildItem 'HKCU:\Software\Classes\WOW6432Node\CLSID' -ErrorAction SilentlyContinue) {
    $server = (Get-ItemProperty "$($clsid.PSPath)\InprocServer32" -ErrorAction SilentlyContinue).'(default)'
    if (-not $server -or $Files -notcontains [IO.Path]::GetFileName($server).ToLower()) { continue }
    if (-not (Test-Path "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$($clsid.PSChildName)")) {
        $keys.Add("WOW6432Node\CLSID\$($clsid.PSChildName)")
    }
    $progId = (Get-ItemProperty "$($clsid.PSPath)\ProgID" -ErrorAction SilentlyContinue).'(default)'
    foreach ($p in @($progId, ($progId -replace '\.\d+$', '')) | Select-Object -Unique) {
        if ($p -and (Test-Path "HKCU:\Software\Classes\$p") -and -not (Test-Path "HKLM:\SOFTWARE\Classes\$p")) { $keys.Add($p) }
    }
}

if ($keys.Count -eq 0) { Write-Host 'Nothing to do: all classes are already registered machine-wide.'; return }
foreach ($k in $keys | Select-Object -Unique) {
    if ($PSCmdlet.ShouldProcess("HKLM\SOFTWARE\Classes\$k", 'copy from HKCU')) {
        & reg.exe copy "HKCU\Software\Classes\$k" "HKLM\SOFTWARE\Classes\$k" /s /f | Out-Null
        Write-Host "copied $k"
    }
}
