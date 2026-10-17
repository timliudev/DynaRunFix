# VM regression checklist

Manual checks for a build of `DynaRunFix-Setup.exe` that the automated tests cannot cover (UAC, the wizard, the restart for the driver, the network).
Run them in a throw-away VM (Windows 10 or 11, snapshot before each section). Do not run them on the development host.
The texts in quotes are the English ones from the source (`src\wizard.c` and `src\wizard\*.h`, `src\setup.c`, `src\update.c`); a Traditional Chinese Windows shows the Chinese pair.

Build first with `build.cmd` (the setup is `build\DynaRunFix-Setup.exe`). A dev build is version `dev` and never takes part in the update check.

## 0. Logs and state to look at after every section

| What | Where | Written by |
|---|---|---|
| Setup / wizard log | `%TEMP%\DynaRunFix-setup.log` (user, not elevated) | wizard, `user_log` |
| Elevated-stage log | `<install folder>\setup.log` (`%ProgramFiles(x86)%\DynaRunFix` on 64-bit Windows) | machine stage, `setup_log` |
| Driver log | `%ProgramData%\DynaRunFix\driver.log` (+ `bitlocker-before.txt`) | driver steps, only while the driver option is used |
| Launcher / dll / update log | `%TEMP%\dynafix.log` (shared by `DynaRunFix.exe`, `dynafix.dll` and the update check; `launcher:` and `update` lines) | launcher, dll, `update.c` |
| Exit code | `echo %errorlevel%` after `start /wait DynaRunFix-Setup.exe ...` | see the table in `docs\MANUAL.md` (0 ok, 1 error, 2 cancelled, 3 over-the-shoulder, 4 MSI failed, 5 exe not found, 6 DynaRun running) |
| Registry | `HKLM\SOFTWARE\DynaRunFix`, `HKCU\Software\DynaRunFix`, `HKLM\...\Uninstall\DynaRunFix`, `HKCU\...\Run` value `DynaRunFix` | |
| Task / boot option | `schtasks /query /tn DynaRunFix-DriverContinue`, `bcdedit /enum {current}` (`onetimeadvancedoptions`) | driver option only |

A test passes only if the expected result holds AND the logs show no unexpected error line. Note the build (`DRF_DISPLAY`) and the VM snapshot in the result.

## 1. Full install (DynaRun V3 not installed, driver ticked)

Start: snapshot "clean", administrator account, network on, no DynaRun, no files in Downloads. Have Dyna Pro's setup password ready.

1. Run `DynaRunFix-Setup.exe`. Expected: first page "Install DynaRun V3" with "This program will: 1. download ... 2. install ... 3. add the fixes ...", buttons Start / Cancel / "Installed elsewhere...", check boxes: pin to the taskbar, start DynaRun when Windows starts, "Also install the Dyna Pro USB driver (one restart needed)" (all three ticked; the last only on Windows 10/11 when the driver is missing).
2. Click Start. Expected: page "Getting the DynaRun V3 setup" with a progress bar, Pause / Cancel / "Open website"; then the page "Enter the setup password".
3. Type a wrong password, Next. Expected: "Wrong password. Please check it (upper/lower case matters)." and the page stays. Type the right one, Next. Expected: "Opening the setup file", then "Check the installer" (source, path, size, SHA-256, MSI version and manufacturer), Next.
4. License page. Expected: Dyna Pro's license in the box; the Install button is disabled until "I accept the license agreement" is ticked.
5. Click Install. Expected: the driver pages come BEFORE the install: "Install the Dyna Pro USB driver" (intro, with the line "DynaRun's fix is installed in the same step, just before the restart, so Windows asks for permission only once"), Next, then "Getting the Dyna Pro USB driver" (a copy on the computer is looked for first, else downloaded from dynapro.co.uk), then "After the restart you will see this: press 7" with the picture and the buttons "Install and restart" / Cancel.
6. Click "Install and restart". Expected: exactly ONE UAC prompt (administrator account: Yes), page "Installing" / "Please wait". No second UAC prompt for the driver preparation. `setup.log` in the install folder shows the machine stage, then the driver preparation (`driver.log` exists).
7. Expected: the page "Restarting" ("The computer restarts now. When the menu appears, press 7...") with the pin hint ("DynaRun will appear on the taskbar after you sign out and back in" on Windows 11) and, on a non-zh-TW system with a Chinese UI, the code-page warning. The computer restarts into the Windows advanced start-up menu.
8. Press 7 (Disable driver signature enforcement). Sign in. Expected: the wizard continues by itself (task `DynaRunFix-DriverContinue`, `/driver-continue`) on the page "Last step: install the driver" with the red-window picture; no UAC prompt on an administrator account. Click Install; answer Windows' red warning "Install this driver software anyway" twice.
9. Expected: "The driver is installed" with "Start DynaRun" / Close. The task and the boot option are gone (`schtasks`, `bcdedit`); `pnputil /enum-drivers` lists the Dyna Pro ftdibus package; BitLocker (if it was on) is protecting again.
10. Click "Start DynaRun". Expected: DynaRun starts through the launcher, non-elevated; the main screen does not flicker. The "start DynaRun when Windows starts" value `HKCU\...\Run\DynaRunFix` exists only now (after the driver step; `AutostartPending` is cleared).
11. `%TEMP%\dynafix.log`: lines starting `launcher: DynaRun pid=...`, `launcher: dynafix.dll loaded in N ms (error 0)`, no `did not report in`.

Full install without the driver: untick the driver box on page 1. Expected: after the license, a single UAC prompt, the page "Installing", then "Done" ("Everything is installed ...", buttons "Start DynaRun" / Close). Exit code 0. The Run value exists at once.

## 2. Fix only (DynaRun already installed)

Start: snapshot with DynaRun V3 installed and never started through the fix.

1. Run the setup. Expected: page "Fix DynaRun V3 for this Windows" ("DynaRun V3 was found: <path> ... This installs the fixes ..."), button Install, the check boxes.
2. Untick the driver box, click Install. Expected: ONE UAC prompt, "Installing", then "Done".
3. Expected state: `<install folder>` has `DynaRunFix.exe`, `dynafix.dll`, `DynaRunFix-Setup.exe`, `le\`, `setup.log`; the DynaRun desktop/Start menu shortcuts point at `DynaRunFix.exe`; the uninstall entry shows the version; a desktop shortcut exists; the pin follows the Windows version (Windows 11: after sign-out/in).
4. Run the setup a second time with the same build and click Install again. Expected: it finishes with exit code 0, the shortcut backups (`HKCU\Software\DynaRunFix\Shortcuts`) stay the original shortcut files (not our own launcher shortcuts).
5. `DynaRunFix-Setup.exe /quiet` from an administrator command prompt (DynaRun not running). Expected: no window, ONE UAC prompt (or none if already elevated), exit code 0. `/quiet` never installs the driver. With DynaRun not installed: exit code 1.
6. DynaRun running: start DynaRun, then run the wizard. Expected: page "DynaRun is running" with "Close DynaRun and install" / Cancel; the button closes DynaRun and installs. With `/quiet`: exit code 6, nothing changed; `/quiet /close`: DynaRun is closed and the install finishes with 0. `setup.log` / `DynaRunFix-setup.log` say which program blocked it.
7. `/quiet /notaskbar /noautostart` and `/quiet /keep`: no pin, no Run value / pin, Run value and desktop shortcut left as they were.

## 3. Fix + driver (DynaRun installed)

1. Run the setup, leave the driver box ticked, click Install. Expected: the driver pages first (intro, "Getting ...", "press 7"), then "Install and restart" and ONE UAC prompt that covers the fix and the driver preparation.
2. Click "Skip" on the driver intro (or on the download error page): Expected: the fix is installed without the driver, `driver: skipped, the fix is installed without it` in `DynaRunFix-setup.log`, the Run value written by the install, page "Done".
3. Cancel on the "press 7" page before "Install and restart": nothing is installed, exit code 2, no task, no boot option.
4. Decline the UAC prompt of "Install and restart" (No): Expected: back on the "press 7" page with "Nothing was changed: Windows needs your "Yes" first." / "Nothing was installed: Windows needs your "Yes" to install."; nothing in `%ProgramData%\DynaRunFix`.
5. After the restart do NOT press 7 (let Windows boot normally; the task starts the wizard): Expected: page "7 was not pressed" with Retry / Cancel. Retry restarts once more. Cancel (or closing the window, elevated) removes the task, the boot option and takes BitLocker back (`/driver-cancel` behaviour).
6. BitLocker VM (if available): protection on. Expected: BitLocker is suspended before the restart (`manage-bde -status` is not run by the tester: read `bitlocker-before.txt` / `driver.log`), resumes after the install. If the suspension fails the page "BitLocker could not be suspended" offers "Restart anyway" / Later / "Find the recovery key".
7. A driver zip that differs from the pinned files (Choose file... with a modified copy): Expected: the page "The driver differs from the known version" with Cancel as the default button, "Install anyway" second; `driver.log` records the choice.

## 4. Driver only

1. Fix current and driver missing: run the setup. Expected: page "Install the Dyna Pro USB driver" ("The fix for Windows 10/11 is already installed (version X). The Dyna Pro USB driver is not installed yet ..."), buttons "Install the driver" / Cancel / (hint) / "Reinstall the fix...", no driver check box.
2. "Install the driver": intro, download/check, "press 7" page with "Restart now" / Later (the fix is not installed in this step), ONE UAC prompt, restart, press 7, "Last step", Install, "The driver is installed".
3. `DynaRunFix-Setup.exe /driver` (also the Start menu shortcut "Install Dyna Pro USB driver"): the driver pages only, no skip button (Cancel). Driver already installed: page "The driver is already installed".
4. `DynaRunFix-Setup.exe /driver-cancel` from a NON-elevated prompt: exit code 1, nothing changes. From an administrator prompt after a prepared-but-not-restarted driver step: the task, the boot option and the BitLocker suspension are removed.
5. Windows 8 / 7 / XP VM (optional): `/driver` shows "The driver was not installed" with the manual steps for that Windows and "Open the manual steps".

## 5. Standard (non-admin) user

Start: a standard user account; an administrator account whose password is known.

1. Run the setup as the standard user, fix only. Expected: the UAC prompt asks for an administrator's user name and password. Enter the administrator's. The install completes; the standard user's own shortcuts are retargeted (not the administrator's), pin/Run value are written for the standard user (over-the-shoulder: the `/machine <SID>` argument names the starting user). Exit code 0.
2. Cancel the prompt. Expected: "Administrator permission was not granted. Nothing was changed." and exit code 2; nothing installed.
3. Full install (DynaRun missing) as a standard user, typing the administrator's credentials: Expected either the normal flow, or the over-the-shoulder path (DynaRun's MSI runs as the standard user, then the fix is installed): the install finishes and `HKCU\Software\DynaPro` is the standard user's. Exit code 0.
4. With the driver: after the restart a standard user is asked for an administrator's password again at the page "Last step" (the task cannot run elevated for a standard user). Expected: the install finishes after the credentials; "The driver is installed".
5. Uninstall as the standard user: administrator prompt first; after cancelling it nothing is changed (shortcuts still go through the fix).

## 6. Reinstall and uninstall

1. Reinstall over the same build (section 2 step 4) and over an older release if one is available (an update: `/quiet /keep` leaves the pin, Run value and desktop shortcut as they were).
2. Reinstall with the pin box cleared after a pinned install: Expected: the taskbar policy values (Windows 11) are taken back; with `/keep` they stay.
3. Uninstall from Programs and Features (or `DynaRunFix-Setup.exe /uninstall`), DynaRun V3 installed. Expected: the question "Uninstall DynaRunFix?" with "Fix only" / "Remove all" / Cancel; one UAC prompt first (cancelling it changes nothing; exit code 2). "Fix only": shortcuts go back to the original ones (DynaRun V3.exe), Run value and policy values removed, the install folder removed (or at the next restart), `setup.log` kept, "DynaRunFix has been uninstalled.", exit code 0. DynaRun V3 stays and starts normally.
4. "Remove all": Dyna Pro's own uninstaller runs afterwards; "DynaRunFix and DynaRun V3 have been uninstalled." Cancelling Dyna Pro's uninstaller: "DynaRunFix has been uninstalled. DynaRun V3 was kept (its uninstall was cancelled)." (exit code 2).
5. Uninstall with DynaRun running: "DynaRun is running and must be closed to uninstall DynaRunFix." OK closes it; Cancel exits 2. `/uninstall /quiet`: nothing asked, DynaRun closed only with `/close`, exit 0.
6. Uninstall after a driver step that was prepared but not finished: the task, the boot option and `%ProgramData%\DynaRunFix` are removed; BitLocker is resumed if the suspension flag was set. An installed driver stays.
7. Uninstall when not installed: "DynaRunFix is not installed." exit 0.

## 7. Network failure

Use the VM's virtual network adapter (disconnect / host-only) and a hosts-file entry for the unreachable case. Start each case from page 1 of a full install (DynaRun missing) and repeat the driver case from the driver intro.

1. No network (adapter disconnected): click Start. Expected: the error page at once, no waiting through retries, title "The DynaRun V3 setup could not be downloaded", text "This computer is not connected to a network (Windows reports no connection). Connect it and try again." plus "Get it yourself: 1. click "Open website" ...", buttons Retry / Cancel (Skip on the driver page in the main flow) / "Choose file..." / "Open website".
2. Reconnect, click Retry. Expected: the download starts and the flow continues. The status line shows the progress "x of y MB".
3. Unreachable host (`dynapro.co.uk` -> an address that does not answer, or the firewall drops it): Expected: the attempt gives up after about 10 seconds per try; retries after 1, 2, 4, 8, 15 s; then "No connection to the Dyna Pro website (error N). Check the internet connection." with the first real error.
4. Bad certificate / date set far off: "The security certificate of the Dyna Pro website could not be verified (error N) ... Check the date and time of this computer." Nothing downloaded.
5. Redirect off the host (hosts file pointing to a local server that redirects): "The download was refused: the address did not stay on https://dynapro.co.uk ...".
6. Slow download (throttle the adapter): click Pause. Expected: "Pausing...", then "Paused: x of y MB downloaded." with the button Resume; Resume continues from the `.part` (Range request) and the final file is accepted; Cancel while paused finishes the page at once.
7. Drop the network in the middle of a download: it retries by itself; after the last retry the error page offers "Continue download" (the `.part` is kept). Continue after reconnecting finishes without starting over.
8. "Choose file..." on the error page with a valid `Dyna Pro USB.zip` / DynaRun zip, and with a file that is not DynaRun's ("This is not the DynaRun V3 setup"): the first is accepted, the second refused with a message; neither is deleted by the wizard.
9. With the fix already installed and the network off: the first page must still appear; the "newer DynaRun" hint simply is not shown (the check says nothing on failure; a line in `DynaRunFix-setup.log`).
10. No network during the driver step after the restart: the install uses the staged copy, it needs no network.

## 8. Update check and offer

Needs a release build (a dev build never updates) and a GitHub release newer than the installed version, or a lower-version build installed on purpose. Network on. Start DynaRun only through the shortcut (the launcher).

1. Start DynaRun through the launcher with `UpdateChecked` older than a day (delete the value). Expected in `dynafix.log`: `launcher: run_setup: ... /checkupdate -- reason: the last check is more than a day old`, then `launcher: looking for an update (0 no setup, 1 started): 1`. DynaRun opens first and is not delayed. `update:` lines say `latest is vX: nothing to do` or `vX is newer: noted, asked at the next start of DynaRun`.
2. Start DynaRun again. Expected: the question beside the running DynaRun: "A new version of DynaRunFix is available: vX (this is vY). Yes: install it after you close DynaRun ... No: skip this version ... Cancel, or closing this window: ask again tomorrow." and `dynafix.log` `launcher: offering the update to vX after DynaRun was started`.
3. Answer Cancel: asked again tomorrow (`UpdateAsk` set), `update` line `asked about vX while DynaRun runs: 2`. Start again the same day: `launcher: noted update vX not offered (already asked today)`.
4. Answer No: `UpdateSkip` = vX; later starts log `... not offered (skipped by the user)`. A still newer release is offered again.
5. Answer Yes, then close DynaRun. Expected: the page "Downloading the update..." / "Installing the update...", `DynaRunFix-Setup-vX.exe /quiet /keep` ONE UAC prompt, the pin / Run value / desktop shortcut unchanged; log `the new setup ended with 0`. The next start shows the new version.
6. `DYNAFIX_NOUPDATE=1` in the environment: `launcher: update check skipped (DYNAFIX_NOUPDATE)`, no question. `/autostart`, `/restart` and `/noupdate` launches never ask.
7. Tamper cases (optional): SHA mismatch (release file replaced) -> `SHA-256 does not match: not installed`, nothing runs; no network -> the check says nothing.

## 9. Launcher log (`%TEMP%\dynafix.log`)

Every line starts with the date and time and ends with CRLF; a line has at most 1000 bytes (longer ones end in `...`). Start from a deleted log.

1. Start DynaRun via the shortcut. Expected, in order: `launcher: DynaRun pid=N gui-thread=N`, `launcher: gui-thread chosen from window class [..], rank 4 ...`, `launcher: dynafix.dll loaded in N ms (error 0)` (or the early-hook lines `launcher: DynaRun pid=N hooked from start (thread N)` and `launcher: hook set N ms after DynaRun was resumed, dll reported in after N ms`); then dll lines (`skipped THBResize 0x591 after duplicate WM_SIZE ...`, fonts, `cleared cloud attributes ...`).
2. No `launcher: dynafix.dll did not report in` and no `early hook did not report in` / `early hook failed`.
3. First start of a fresh profile (no `HKCU\Software\DynaPro`): `setup helper <file> started, waiting for it`, `setup helper finished, exit code 0`, `restarting DynaRun to load the new configuration`; DynaRun restarts by itself.
4. Start the launcher elevated (Run as administrator on `DynaRunFix.exe`): `launcher: elevated; started itself with the desktop shell's token: 1 (error 0)`, DynaRun runs non-elevated. With UAC off: `launcher: the desktop shell token is elevated ... carrying on in this process`.
5. Start the launcher while DynaRun already runs (second click): the existing instance is used, no second DynaRun; if that DynaRun is elevated: `launcher: the running DynaRun cannot be opened (it runs with more rights than this launcher)`.
6. A log larger than 10 MB at start is cut to its newest 8 MB from a full line on (copy a big file over `dynafix.log` first). Two DynaRuns / the launcher and the dll writing at once leave no torn lines.
7. `%TEMP%\DynaRunFix-setup.log` and `setup.log` lines are time-stamped; the mutex `Local\DynaRunFix.dynafix.log` is shared by the launcher, the dll and `update` (a log written by an elevated process and a normal one in turn must not fail).
