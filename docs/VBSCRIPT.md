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

## Removing the dependency: `tools/msi-novbs/Remove-WiseVBScript.ps1`

The script computes the page order once from `WiseDlgSequence` and writes it into the `ControlEvent` table
as ordinary `NewDialog` / `EndDialog` events with conditions (and the matching *Back disabled* conditions).
Then it deletes the two custom actions and their script. It works on any Wise setup that uses these
two actions. It needs Windows PowerShell 5.1 or PowerShell 7.

```
powershell -ExecutionPolicy Bypass -File tools\msi-novbs\Remove-WiseVBScript.ps1 -Path D:\Setup.msi
```

It leaves `Setup.msi` untouched and writes two files next to it:

- `Setup-novbs.mst` is a transform holding only the changed table rows. Install with
  `msiexec /i Setup.msi TRANSFORMS=Setup-novbs.mst`. The transform checks ProductCode and UpgradeCode, so
  it only applies to the same DynaRun build.
- `Setup-novbs.msi` is a patched copy with a new PackageCode (the ProductCode is unchanged) that installs by
  double-clicking.

A transform is applied only when a product is first installed. A copy of DynaRun that is already installed
keeps using its cached original package for *Change/Repair* (its *Programs and Features* entry has
`NoModify=1`, so normally only *Uninstall* is offered, and that path does not run the script).

### Verification (Windows 11, DynaRun 3.26.0)

The UI was driven with UI Automation and Windows Installer verbose logs, and every run was cancelled before
installing. Test copies were given a new ProductCode and PackageCode so they did not collide with the
installed product.

- Original: warning shown on the welcome page; the log shows `Doing action: WiseNextDlg` on **Next**.
- Patched, fresh install: no warning. The page order matched the original's
  (Welcome → License, where Next stays disabled until accepted → Destination Folder → Ready to Install),
  and Back worked from every page. No script actions appeared in the log.
- Patched, maintenance wizard (forced in a test copy): Modify → Select Features → Ready, Repair → Ready,
  Remove → Uninstall confirmation; Back from each returned to *Application Maintenance*.
- Not tested: an actual install with VBScript disabled (that needs the optional feature removed system-wide).
