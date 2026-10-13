# Runs the log tests. Usage: tests\run-tests.ps1 [-Bin build\tests] [-Work <scratch dir>]
# Writes only under -Work (default: a temp folder); nothing is launched except the test exes.
param([string]$Bin = "$PSScriptRoot\..\build\tests", [string]$Work = (Join-Path $env:TEMP 'dynafix-logtest'), [string]$Mode = '', [int]$Lines = 30000)   # -Mode old: the dynafix.c under test has no w2u
$ErrorActionPreference = 'Stop'
$Bin = (Resolve-Path $Bin).Path
$py = if (Get-Command python -ErrorAction SilentlyContinue) { 'python' } else { 'py' }
$chk = Join-Path $PSScriptRoot 'check_log.py'
New-Item -ItemType Directory -Force $Work | Out-Null
$env:TEMP = $Work
$log = Join-Path $Work 'dynafix.log'
$fail = 0
function Result($name, $ok) { if ($ok) { "PASS  $name" } else { "FAIL  $name"; $script:fail++ } }

# 1. long strings (2000-5000 chars): cut, CRLF-terminated, the next line intact, no stack corruption
foreach ($t in @(@('logtest.exe', $Mode), @('logtest_l.exe', 'launcher'))) {
    Remove-Item $log -ErrorAction SilentlyContinue
    $p = Start-Process "$Bin\$($t[0])" -ArgumentList 'long' -Wait -PassThru -NoNewWindow
    "$($t[0]) long: exit $($p.ExitCode)"
    & $py $chk long $log $t[1]
    Result "long strings, $($t[0])" ($p.ExitCode -eq 0 -and $LASTEXITCODE -eq 0)
}

# 2. 4 processes x 2 threads x $Lines lines, trim running in each, file pre-filled to just over 10 MB
$n = $Lines
Remove-Item $log -ErrorAction SilentlyContinue
$sb = New-Object System.Text.StringBuilder
$line = ('2026-01-01 00:00:00.000 PRE L{0:D8} ' + ('x' * 50) + " END`r`n")
$i = 0
$fs = [IO.File]::Create($log)
while ($fs.Length -lt 10500000) { $b = [Text.Encoding]::ASCII.GetBytes(($line -f $i)); $fs.Write($b, 0, $b.Length); $i++ }
$fs.Close()
$procs = foreach ($k in 1..4) {
    $exe = if ($k % 2) { 'logtest.exe' } else { 'logtest_l.exe' }
    Start-Process "$Bin\$exe" -ArgumentList "mp $k $n 2" -PassThru -NoNewWindow
}
$procs | Wait-Process
$writers = foreach ($k in 1..4) { foreach ($t in 0..1) { "$($k * 10 + $t):$n" } }
& $py $chk mp $log @writers
Result "4 processes, trim under load" (($procs | ? { $_.ExitCode -ne 0 }).Count -eq 0 -and $LASTEXITCODE -eq 0)
"failures: $fail"
exit $fail
