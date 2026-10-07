# DynaRun V3 setup and the VBScript deprecation

## What Microsoft is doing

VBScript is being removed from Windows in three phases:

1. **Now (Windows 11 24H2 / 25H2):** VBScript is a Feature on Demand, installed and enabled by default.
   25H2 shows a warning in setups that use VBScript custom actions:
   *"This setup uses VBScript custom actions. VBScript will not be supported in the future..."*
   (Windows 7, XP and older Windows 11 releases show no such warning.)
2. **About 2027:** disabled by default. Users can turn it back on under *Optional features*.
3. **Later (no date announced):** removed from Windows.

The VB6 runtime (`msvbvm60.dll`) is a different component and is not part of this plan.

## What it means for DynaRun V3 3.26.0

`Setup.msi` (built with Wise for Windows Installer) has exactly two VBScript custom actions:
`WiseNextDlg` and `WisePrevDlg`. They run on every **Next** / **Back** button of the setup wizard and
pick the next page from the `WiseDlgSequence` table. All real installation work (files, registry, DCOM and
MDAC checks, upgrade check) is done by DLL custom actions or standard actions and does not use VBScript.

| Situation after VBScript is disabled | Result |
|---|---|
| Interactive install (double-click `Setup.msi`) | Expected to stop at the first **Next** with a Windows Installer error |
| Silent install `msiexec /i Setup.msi /qb` | Works: the wizard pages, and the script, are skipped |
| Installed program `DynaRun V3.exe` | Not affected: it uses `WScript.Shell` (`wshom.ocx`) only for `RegRead`/`RegWrite`, not the VBScript engine (`vbscript.dll`) |
| Uninstall from *Programs and Features* (`MsiExec.exe /X{C487CE7C-...}`) | Expected to work: `/X` uses reduced UI without the wizard pages, so the script is not called (not tested) |

Until then, or on a PC where it has been switched off, VBScript can be turned back on (elevated):

```
DISM /Online /Add-Capability /CapabilityName:VBSCRIPT~~~~
```

DynaRunFix does not change Dyna Pro's setup. If VBScript has been switched off, turn it back on as above
before running `Setup.msi`, use `DynaRunFix-Setup.exe` (it runs the setup without the wizard pages), or ask
Dyna Pro for a setup that does not need it.
