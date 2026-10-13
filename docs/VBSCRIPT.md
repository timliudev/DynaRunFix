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

Windows 11 25H2 shows the warning above when Dyna Pro's `Setup.msi` is started with its wizard (double-click).
What will happen once VBScript is disabled has not been tested; DynaRunFix only relies on what it does itself:

| Situation | What DynaRunFix knows |
|---|---|
| Interactive install (double-click `Setup.msi`) | Shows the 25H2 warning; may stop working once VBScript is disabled |
| Install with basic UI `msiexec /i Setup.msi /qb!` (what `DynaRunFix-Setup.exe` does) | The wizard pages are not shown |
| Installed program `DynaRun V3.exe`, uninstall | Not tested without VBScript |

Until then, or on a PC where it has been switched off, VBScript can be turned back on (elevated):

```
DISM /Online /Add-Capability /CapabilityName:VBSCRIPT~~~~
```

DynaRunFix does not change Dyna Pro's setup. If VBScript has been switched off, turn it back on as above
before running `Setup.msi`, use `DynaRunFix-Setup.exe` (it runs the setup without the wizard pages), or ask
Dyna Pro for a setup that does not need it.
