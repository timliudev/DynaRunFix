# Root cause analysis: DynaRun V3 flicker on Windows 10/11

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

## 2. Who hides the form

A debugger breakpoint on `win32u!NtUserShowWindow` (on current Windows `user32!ShowWindow` forwards to it)
showed one repeating cycle:

```
ShowWindow(main form, SW_MAXIMIZE)
ShowWindow(blank 699x483 form, SW_HIDE)
ShowWindow(main form, SW_MAXIMIZE)
ShowWindow(main form, SW_HIDE)
...
```

Every call stack goes through `USER32!DispatchMessage → THBRes25.dll → MSVBVM60 (VB event) → ...`, and the
event handler also calls `DoEvents`, so the cycle re-enters itself.
`THBRes25.dll` is *THBResize 2.5* (THB Componentware, 2000), an auto-resize control for VB6 forms. It
subclasses the form and reports resizes to the program through a posted private message `0x591`.

## 3. Windows 11 vs Windows 7, message by message

`tools/msgspy` hooks the GUI thread (WH_CALLWNDPROC, WH_CALLWNDPROCRET, WH_GETMESSAGE) and logs messages.
On Windows 7 the program sits idle (only timers and text updates). To compare the same code path, one
`0x591` was posted to the Windows 7 form manually (`msgspy 4 591`).

Windows 7, after the manual `0x591`:

```
P 0591                                    <- THBResize event
S 0018 WM_SHOWWINDOW(FALSE)               <- main form hidden
S 0081/0001 create blank form 699x483     <- Load blank form
S 0024 WM_GETMINMAXINFO maxsz=2562,1445
S 0046 WM_WINDOWPOSCHANGING sz=2562,1445  <- maximize; form keeps 4:3 -> 1924x1443
S 0047 WM_WINDOWPOSCHANGED  sz=1924,1443 fl=10001867   (no SWP_STATECHANGED)
S 0018 WM_SHOWWINDOW(TRUE)
S 0002 destroy blank form                 <- Unload
(no WM_SIZE, no further 0x591)            <- loop ends after one round
```

Windows 11, steady state:

```
S 0047 WM_WINDOWPOSCHANGED  pos=-1,-1 sz=1706,1280 fl=10009863   (0x8000 = SWP_STATECHANGED)
S 0005 WM_SIZE SIZE_MAXIMIZED 1704x1258   <- identical to the previous WM_SIZE
S 0018 WM_SHOWWINDOW(TRUE)
S 0002 destroy blank form
P 0591                                    <- THBResize reacts to WM_SIZE
S 0018 WM_SHOWWINDOW(FALSE)               <- main form hidden again
S 0081 create blank form 699x483
... ~450 ms later the same sequence repeats, forever
```

The only difference: when a hidden window that is already maximized is shown again, **Windows 10/11 mark
the position change as a state change and send `WM_SIZE (SIZE_MAXIMIZED)` with the unchanged size;
Windows 7 does not.** That extra `WM_SIZE` makes THBResize post `0x591`, and DynaRun's handler for it
hides and re-shows the form, which produces the next `WM_SIZE`.

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

A first attempt that subclassed the form and dropped the duplicate `WM_SIZE` itself did not work, because
THBResize subclasses the form after it is created and therefore sits outside any earlier subclass.
Patching THBRes25's own `PostMessageA` import does not depend on subclass order.

Result on Windows 11: the main form stays visible (130/130 samples over 4 s), and the log shows that only
the redundant `0x591` messages are suppressed.

## 5. Notes for running elevated

A hook can only be installed into a process of the same or lower integrity level. If DynaRun has to run
as administrator (for example to reach its setup/initialisation screens), run `DynaRunFix.exe` as
administrator as well.
