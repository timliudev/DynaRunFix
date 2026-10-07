<#
.SYNOPSIS
  Makes a Wise for Windows Installer setup (such as the DynaRun V3 Setup.msi) work without VBScript.

.DESCRIPTION
  Wise setups move between wizard pages with two VBScript custom actions, WiseNextDlg and WisePrevDlg,
  which read the WiseDlgSequence table at run time. Windows 11 25H2 shows a VBScript deprecation warning
  for them, and once VBScript is disabled by default (planned for 2027) the wizard stops at the first
  "Next" click.

  This script computes the same page order once, from WiseDlgSequence, and writes it as plain NewDialog
  and EndDialog control events. It then removes the two custom actions and their script. Nothing else
  changes: the same files, registry values and DLL custom actions are installed.

  The original MSI is never modified. The script writes, next to the output path:
    <name>-novbs.msi   a patched copy
    <name>-novbs.mst   a transform holding only the changes: msiexec /i Setup.msi TRANSFORMS=<name>-novbs.mst

.EXAMPLE
  .\Remove-WiseVBScript.ps1 -Path C:\Downloads\Setup.msi
#>
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$OutDir
)
$ErrorActionPreference = 'Stop'

$Path = (Resolve-Path -LiteralPath $Path).Path
if (-not $OutDir) { $OutDir = Split-Path -Parent $Path }
$OutDir = (New-Item -ItemType Directory -Force -Path $OutDir).FullName
$base = [IO.Path]::GetFileNameWithoutExtension($Path)
$outMsi = Join-Path $OutDir "$base-novbs.msi"
$outMst = Join-Path $OutDir "$base-novbs.mst"

# Windows Installer automation through late binding (works in Windows PowerShell 5.1 and PowerShell 7)
$BF = [Reflection.BindingFlags]
# arguments arrive wrapped in PSObject, which IDispatch rejects with DISP_E_TYPEMISMATCH
function Get-Args($a) { if ($null -eq $a) { return $null }; , [object[]]@($a | ForEach-Object { $_.PSObject.BaseObject }) }
function Get-P($o, $name, $a = $null) { [__ComObject].InvokeMember($name, $BF::GetProperty, $null, $o, (Get-Args $a)) }
function Set-P($o, $name, $a) { [void][__ComObject].InvokeMember($name, $BF::SetProperty, $null, $o, (Get-Args $a)) }
function Call($o, $name, $a = $null) { [__ComObject].InvokeMember($name, $BF::InvokeMethod, $null, $o, (Get-Args $a)) }

$installer = New-Object -ComObject WindowsInstaller.Installer

function Invoke-Sql($db, [string]$sql, [object[]]$params) {
    $view = Call $db 'OpenView' @($sql)
    if ($params) {
        $rec = Call $installer 'CreateRecord' @($params.Count)
        for ($i = 0; $i -lt $params.Count; $i++) {
            if ($params[$i] -is [int]) { Set-P $rec 'IntegerData' @(($i + 1), $params[$i]) }
            else { Set-P $rec 'StringData' @(($i + 1), [string]$params[$i]) }
        }
        [void](Call $view 'Execute' @($rec))
    } else { [void](Call $view 'Execute') }
    $view
}

# rows as arrays of strings (integer columns come back as their decimal text)
function Get-Rows($db, [string]$sql, [object[]]$params) {
    $view = Invoke-Sql $db $sql $params
    $rows = New-Object Collections.ArrayList
    while ($rec = Call $view 'Fetch') {
        $n = Get-P $rec 'FieldCount'
        $row = @(for ($i = 1; $i -le $n; $i++) { Get-P $rec 'StringData' @($i) })
        [void]$rows.Add($row)
    }
    [void](Call $view 'Close')
    , $rows   # one ArrayList of rows (string arrays); filter with .Where(), not a pipeline
}

function Test-Table($db, [string]$name) {
    (Get-Rows $db 'SELECT `Name` FROM `_Tables` WHERE `Name` = ?' @($name)).Count -gt 0
}

function Join-And([string[]]$parts) {
    $parts = @($parts | Where-Object { $_ })
    if ($parts.Count -eq 0) { return '1' }
    $parts -join ' AND '
}

# --- open a copy ---------------------------------------------------------------------------------------
Copy-Item -LiteralPath $Path -Destination $outMsi -Force
Set-ItemProperty -LiteralPath $outMsi -Name IsReadOnly -Value $false
$db = Call $installer 'OpenDatabase' @($outMsi, 1)   # msiOpenDatabaseModeTransact

foreach ($t in 'WiseDlgSequence', 'ControlEvent', 'CustomAction') {
    if (-not (Test-Table $db $t)) { throw "Table $t not found: this does not look like a Wise setup." }
}

# WiseDlgSequence: Dialog, Wizard, Ordering, Enabled, Condition  (columns used the same way as the Wise script)
$seqRows = Get-Rows $db 'SELECT * FROM `WiseDlgSequence`'
$seq = foreach ($r in $seqRows) {
    [pscustomobject]@{ Dialog = $r[0]; Wizard = $r[1]; Order = [int]$r[2]; Enabled = $r[3] -eq '1'; Cond = $r[4].Trim() }
}
$wizardsOf = @{}
foreach ($s in $seq) { $wizardsOf[$s.Dialog] = @($wizardsOf[$s.Dialog]) + $s.Wizard | Where-Object { $_ } }

# The page that follows (or precedes) $dialog in $wizard: a list of NewDialog targets with mutually
# exclusive conditions, plus the condition under which none of them applies.
function Get-Chain([string]$dialog, [string]$wizard, [bool]$forward) {
    $me = $seq | Where-Object { $_.Wizard -eq $wizard -and $_.Dialog -eq $dialog } | Select-Object -First 1
    $cands = $seq | Where-Object { $_.Wizard -eq $wizard -and $_.Enabled -and $_.Dialog -ne $dialog -and
                                   $(if ($forward) { $_.Order -gt $me.Order } else { $_.Order -lt $me.Order }) } |
             Sort-Object Order -Descending:(-not $forward)
    $wcond = if (@($wizardsOf[$dialog]).Count -gt 1) { "WiseCurrentWizard = `"$wizard`"" }
    $notBefore = @()
    $targets = @()
    foreach ($c in $cands) {
        $always = $c.Cond -eq '' -or $c.Cond -eq '1'
        $targets += [pscustomobject]@{ Dialog = $c.Dialog; Cond = Join-And (@($wcond) + $notBefore + $(if (-not $always) { "($($c.Cond))" })) }
        if ($always) { return [pscustomobject]@{ Targets = $targets; None = $null } }
        $notBefore += "NOT ($($c.Cond))"
    }
    [pscustomobject]@{ Targets = $targets; None = (Join-And (@($wcond) + $notBefore)) }
}

# --- rewrite the Next/Back buttons -----------------------------------------------------------------------
$names = @{ WiseNextDlg = $true; WisePrevDlg = $true }
$calls = Get-Rows $db 'SELECT `Dialog_`, `Control_`, `Argument`, `Ordering` FROM `ControlEvent` WHERE `Event` = ''DoAction'''
$calls = $calls.Where({ $names[$_[2]] })
if ($calls.Count -eq 0) { throw 'No WiseNextDlg/WisePrevDlg button events found: nothing to do.' }

$added = 0
$tooLong = @()
foreach ($c in $calls) {
    $dialog, $control, $ca = $c[0], $c[1], $c[2]
    $forward = $ca -eq 'WiseNextDlg'
    $events = Get-Rows $db 'SELECT `Event`, `Argument`, `Condition`, `Ordering` FROM `ControlEvent` WHERE `Dialog_` = ? AND `Control_` = ?' @($dialog, $control)
    $newDlg = $events.Where({ $_[0] -eq 'NewDialog' -and $_[1] -eq '[WiseNextDialog]' }, 'First')[0]
    $endDlg = $events.Where({ $_[0] -eq 'EndDialog' -and $_[2] -match 'WiseNextDialog' }, 'First')[0]
    $ordNew = if ($newDlg -and $newDlg[3] -ne '') { [int]$newDlg[3] } else { [int]$c[3] + 1 }
    # "Not WiseNextDialog AND (OutOfDiskSpace <> 1)" -> keep what follows the WiseNextDialog test
    $endExtra = if ($endDlg) { ($endDlg[2] -replace '(?i)^\s*not\s+WiseNextDialog\s*(and\s+)?', '').Trim() }

    [void](Invoke-Sql $db 'DELETE FROM `ControlEvent` WHERE `Dialog_` = ? AND `Control_` = ? AND `Event` = ''DoAction'' AND `Argument` = ?' @($dialog, $control, $ca))
    if ($newDlg) { [void](Invoke-Sql $db 'DELETE FROM `ControlEvent` WHERE `Dialog_` = ? AND `Control_` = ? AND `Event` = ''NewDialog'' AND `Argument` = ''[WiseNextDialog]''' @($dialog, $control)) }
    if ($endDlg) { [void](Invoke-Sql $db 'DELETE FROM `ControlEvent` WHERE `Dialog_` = ? AND `Control_` = ? AND `Event` = ''EndDialog'' AND `Argument` = ? AND `Condition` = ?' @($dialog, $control, $endDlg[1], $endDlg[2])) }

    $disabledWhen = @()   # no page to go to: the chain's "None" condition in each wizard
    foreach ($w in @($wizardsOf[$dialog])) {
        $chain = Get-Chain $dialog $w $forward
        foreach ($t in $chain.Targets) {
            if ($t.Cond.Length -gt 255) { $tooLong += "$dialog/$control -> $($t.Dialog)" }
            [void](Invoke-Sql $db 'INSERT INTO `ControlEvent` (`Dialog_`, `Control_`, `Event`, `Argument`, `Condition`, `Ordering`) VALUES (?, ?, ?, ?, ?, ?)' @($dialog, $control, 'NewDialog', $t.Dialog, $t.Cond, $ordNew))
            $added++
        }
        if ($chain.None) { $disabledWhen += $chain.None }
        if ($chain.None -and $endDlg) {
            $cond = Join-And (@($(if ($chain.None -ne '1') { $chain.None }), $endExtra))
            if ($cond.Length -gt 255) { $tooLong += "$dialog/$control -> EndDialog" }
            [void](Invoke-Sql $db 'INSERT INTO `ControlEvent` (`Dialog_`, `Control_`, `Event`, `Argument`, `Condition`, `Ordering`) VALUES (?, ?, ?, ?, ?, ?)' @($dialog, $control, 'EndDialog', $endDlg[1], $cond, [int]$endDlg[3]))
            $added++
        }
    }

    # Wise greys out "Back" while its page stack is empty; do the same from the static order
    if (-not $forward -and (Test-Table $db 'ControlCondition')) {
        $old = Get-Rows $db 'SELECT `Condition` FROM `ControlCondition` WHERE `Dialog_` = ? AND `Control_` = ? AND `Action` = ''Disable''' @($dialog, $control)
        foreach ($o in $old.Where({ $_[0] -match 'WiseDialogStack' })) {
            [void](Invoke-Sql $db 'DELETE FROM `ControlCondition` WHERE `Dialog_` = ? AND `Control_` = ? AND `Action` = ''Disable'' AND `Condition` = ?' @($dialog, $control, $o[0]))
            if ($disabledWhen.Count) {
                $cond = if ($disabledWhen.Count -eq 1) { $disabledWhen[0] } else { '(' + ($disabledWhen -join ') OR (') + ')' }
                if ($cond.Length -gt 255) { $tooLong += "$dialog/$control (Disable)" }
                [void](Invoke-Sql $db 'INSERT INTO `ControlCondition` (`Dialog_`, `Control_`, `Action`, `Condition`) VALUES (?, ?, ''Disable'', ?)' @($dialog, $control, $cond))
            }
        }
    }
}
if ($tooLong) { throw "Generated conditions longer than 255 characters: $($tooLong -join ', ')" }

# --- remove the VBScript custom actions ------------------------------------------------------------------
foreach ($ca in $names.Keys) {
    foreach ($tbl in 'InstallUISequence', 'InstallExecuteSequence', 'AdminUISequence', 'AdminExecuteSequence', 'AdvtExecuteSequence') {
        if ((Test-Table $db $tbl) -and (Get-Rows $db "SELECT ``Action`` FROM ``$tbl`` WHERE ``Action`` = ?" @($ca)).Count) {
            throw "$ca is also scheduled in $tbl; not handled by this script."
        }
    }
    if ((Get-Rows $db 'SELECT `Dialog_` FROM `ControlEvent` WHERE `Event` = ''DoAction'' AND `Argument` = ?' @($ca)).Count) {
        throw "$ca is still referenced by a control event."
    }
    $src = Get-Rows $db 'SELECT `Source` FROM `CustomAction` WHERE `Action` = ?' @($ca)
    [void](Invoke-Sql $db 'DELETE FROM `CustomAction` WHERE `Action` = ?' @($ca))
    foreach ($s in $src) {
        if (-not (Get-Rows $db 'SELECT `Action` FROM `CustomAction` WHERE `Source` = ?' @($s[0])).Count) {
            [void](Invoke-Sql $db 'DELETE FROM `Binary` WHERE `Name` = ?' @($s[0]))
        }
    }
}

# anything VBScript left? (custom action base type 6: VBScript from Binary/File/Directory/Property)
$left = (Get-Rows $db 'SELECT `Action`, `Type` FROM `CustomAction`').Where({ ([int]$_[1] -band 7) -eq 6 })

# a changed package needs its own PackageCode (summary property 9): Windows Installer identifies packages by
# it and would otherwise run the cached original instead. ProductCode stays, so it is still the same product.
$si = Get-P $db 'SummaryInformation' @(1)
Set-P $si 'Property' @(9, ('{' + [guid]::NewGuid().ToString().ToUpper() + '}'))
[void](Call $si 'Persist')
[void][Runtime.InteropServices.Marshal]::ReleaseComObject($si)   # an open summary handle blocks the transform below
[void](Call $db 'Commit')

# --- transform with only the differences ---------------------------------------------------------------
$orig = Call $installer 'OpenDatabase' @($Path, 0)
if (Test-Path -LiteralPath $outMst) { Remove-Item -LiteralPath $outMst -Force }
[void](Call $db 'GenerateTransform' @($orig, $outMst))
# apply only to this product (ProductCode + UpgradeCode must match); report every error
[void](Call $db 'CreateTransformSummaryInfo' @($orig, $outMst, 0, 0x0802))
$db = $null; $orig = $null
[GC]::Collect(); [GC]::WaitForPendingFinalizers()

Write-Host "Rewrote $($calls.Count) button events into $added static events; removed WiseNextDlg/WisePrevDlg."
if ($left.Count) { Write-Warning ("VBScript custom actions still present: " + (($left | ForEach-Object { $_[0] }) -join ', ')) }
Write-Host "Patched copy: $outMsi"
Write-Host "Transform:    $outMst"
Write-Host "Install with: msiexec /i `"$Path`" TRANSFORMS=`"$outMst`""
