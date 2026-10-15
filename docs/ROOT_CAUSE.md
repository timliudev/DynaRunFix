# DynaRun V3 flicker on Windows 10/11: observations and fix

Environment where this was analysed:

| | Windows 11 (native) | Windows 7 SP1 (VirtualBox VM) |
|---|---|---|
| DynaRun V3 | 3.26 | 3.26 (same installer) |
| Screen | 2048x1280 logical, 2 monitors | 2560x1443, 1 monitor |
| Result | main form flickers continuously | stable |

## 1. Ruling things out

* **USB / dyno connection** – flicker happens with and without the dyno connected.
* **DPI scaling** – the executable was copied to a test path and started with no override, `DPIUNAWARE` and
  `HIGHDPIAWARE` compatibility layers. In every case the main form toggled ~35 times in 4 seconds, and
  its rectangle never changed. So it is not a DPI/GDI repaint problem.
* **Repaint vs. visibility** – sampling `IsWindowVisible` of the main `ThunderRT6FormDC` every 30 ms showed
  the form really being hidden and shown (not just redrawn).

## 2. What the windows do

Watching DynaRun's top-level windows from outside (visibility, size and the window messages they receive)
shows one repeating cycle: the main form is hidden, a blank 699x483 form appears, the main form is shown
maximized again and the blank form is closed; then the cycle starts over. Each cycle begins right after a
private message `0x591` is posted to the main form.

DynaRun's forms are scaled by `THBRes25.dll`, *THBResize 2.5*, an auto-resize control for VB6 forms made by
THB Componentware. DynaRunFix is not affiliated with THB Componentware.

## 3. Windows 11 vs Windows 7, message by message

`tools/msgspy` hooks the GUI thread (WH_CALLWNDPROC, WH_CALLWNDPROCRET, WH_GETMESSAGE) and logs messages.
On Windows 7 the program sits idle (only timers and text updates). To compare the same code path, one
`0x591` was posted to the Windows 7 form manually (`msgspy 4 591`).

Windows 7, after the manual `0x591`:

```
P 0591                                    <- posted private message
S 0018 WM_SHOWWINDOW(FALSE)               <- main form hidden
S 0081/0001 create blank form 699x483     <- blank form appears
S 0024 WM_GETMINMAXINFO maxsz=2562,1445
S 0046 WM_WINDOWPOSCHANGING sz=2562,1445  <- maximize; form keeps 4:3 -> 1924x1443
S 0047 WM_WINDOWPOSCHANGED  sz=1924,1443 fl=10001867   (no SWP_STATECHANGED)
S 0018 WM_SHOWWINDOW(TRUE)
S 0002 destroy blank form                 <- blank form closed
(no WM_SIZE, no further 0x591)            <- loop ends after one round
```

Windows 11, steady state:

```
S 0047 WM_WINDOWPOSCHANGED  pos=-1,-1 sz=1706,1280 fl=10009863   (0x8000 = SWP_STATECHANGED)
S 0005 WM_SIZE SIZE_MAXIMIZED 1704x1258   <- identical to the previous WM_SIZE
S 0018 WM_SHOWWINDOW(TRUE)
S 0002 destroy blank form
P 0591                                    <- posted again after the WM_SIZE
S 0018 WM_SHOWWINDOW(FALSE)               <- main form hidden again
S 0081 create blank form 699x483
... ~450 ms later the same sequence repeats, forever
```

The only difference: when a hidden window that is already maximized is shown again, **Windows 10/11 mark
the position change as a state change and send `WM_SIZE (SIZE_MAXIMIZED)` with the unchanged size;
Windows 7 does not.** That extra `WM_SIZE` is followed by another `0x591`, after which the form is hidden
and shown again, which produces the next `WM_SIZE`.

## 4. The fix

The goal was to restore the Windows 7 behaviour without modifying any vendor file:

* `DynaRunFix.exe` starts (or finds) `DynaRun V3.exe`, installs a `WH_CALLWNDPROC` hook on its GUI thread
  with `dynafix.dll`, and waits until the DLL signals that it is active. The DLL then pins itself and
  installs its own hook so it stays active after the launcher exits.
* The hook records every `WM_SIZE` delivered to a top-level `ThunderRT6FormDC` and marks the window when a
  `WM_SIZE` is identical (same type and same client size) to the previous one.
* `dynafix.dll` patches **THBRes25.dll's import address table entry** for `user32!PostMessageA` in memory.
  The replacement drops `0x591` for a window marked as having received a duplicate `WM_SIZE`, and forwards
  everything else.

A first attempt that subclassed the form and dropped the duplicate `WM_SIZE` itself did not stop the loop in
our tests. Filtering the posted `0x591` messages at THBRes25's `PostMessageA` import works regardless of the
order in which window procedures are installed.

Result on Windows 11: the main form stays visible (130/130 samples over 4 s), and the log shows that only
the redundant `0x591` messages are suppressed.

## 5. Notes on administrator rights

A hook can only be installed into a process of the same or lower integrity level. DynaRun does not need
administrator rights for any of its screens, so `DynaRunFix.exe` always starts it without them: the compatibility
setting *Run as administrator* does not elevate it when it is started through the launcher (observed: the launcher starts it
with `RunAsInvoker` in the compatibility layer, see the [README](../README.md#run-as-administrator-is-not-needed)), and a
launcher that is itself elevated starts DynaRun with the desktop shell's non-elevated token. An already running DynaRun that was started as administrator cannot be attached to; the
launcher asks for it to be closed and started again through DynaRunFix. Running `DynaRun V3.exe` itself elevated is
not recommended: elevated processes ignore per-user COM registrations, which is where DynaRun's setup registers its
ActiveX controls.
