# Changelog

[繁體中文在下方](#更新紀錄)

## v1.7.1 — 2026-10-17

Fixed:

- **A full install with the driver asked for administrator permission twice, not once.** Earlier notes (v1.5.0 and the driver documentation) said that an administrator account is asked once; that was not true when the driver was ticked: Windows asked for the install (the fix, with DynaRun's setup first when it was missing) and again for the driver's preparation. Now ONE elevated step does both, so an administrator account gets a single prompt for the whole install and none after the restart (the sign-in task already runs elevated). A standard user is asked for an administrator's password before the restart and, as before, once more after it. The red *Windows Security* "can't verify the publisher" window is not a UAC prompt and still appears twice.

Changed (setup wizard):

- **The driver pages come before the install** when the driver is ticked: after the license page (or, when DynaRun V3 is already installed, right after the first page) the wizard shows what the driver step does, looks for / downloads and checks the driver, and shows the restart-menu picture with the button *Install and restart*; that button installs the fix and prepares the driver in one elevated step and restarts the computer. *Skip* on these pages installs the fix without the driver; *Cancel* (or closing the window) on the restart page changes nothing. The page "Done" is not shown when the install goes straight on to the restart; its hints (pin to the taskbar by hand, the warning about the non-Unicode language setting) are shown on the restart page instead.
- If the fix fails, the driver is not prepared and the usual error page appears. If the fix is installed but the driver preparation fails, the driver error page says that the fix itself is installed and points to the Start menu shortcut for trying the driver again. The driver alone (first page with only the driver, `/driver`, the Start menu shortcut) is unchanged: its restart page still reads *Restart now*, and it asks once.
- Internal: the elevated stage (`/machine`, `/full`) takes `/with-driver <zip> [/unverified <sha256>]` and runs the driver's preparation in the same process after the fix; `/driver-prep` alone is unchanged.

## v1.7.0 — 2026-10-17

Added (setup wizard):

- **Hint: a newer DynaRun V3.** With DynaRun installed and the computer online, the first page asks (in the background) whether Dyna Pro's software page names a newer
  version than the installed one (the Windows Installer's record; the page's download table names 3.26.0 today, and the parser is tolerant: no version found means
  no hint). If the page names none, the zip on the site is asked for its first byte and its ETag / length / date are compared with what the last completed download
  recorded. A line and *Download new version* appear (the same acquire flow, then the normal install; a copy of an older package already on the computer is not
  used for it). Nothing is automatic, nothing is blocked, each result is a log line.
- **Choose a driver by its folder:** for the driver, *Choose file...* takes a zip, or ftdibus.inf of an unpacked folder; either goes through the same two-layer check and warning page.
- **DynaRun and its fix installed, only the driver missing:** the first page offers just the driver (and *Reinstall the fix...*). "The fix is current" means the
  uninstall entry names this version and the launcher and dll are in the install folder.

Changed (setup wizard, DynaRun and driver):

- **One acquire flow for the DynaRun setup and the driver** (same pages, same code): network check (Windows' network list; clearly disconnected goes to the error
  page at once), look for a copy on the computer, download (connect timeout 10 s, receive 30 s, back-off unchanged) with progress, *Pause* / *Resume* and
  *Continue download*, check, result. One error page for both (title "... could not be downloaded", the first real error, the manual steps, *Retry* /
  *Continue download*, *Choose file...* and *Skip* only where the driver may be skipped). The duplicated error texts of the two flows are one function.
- **Buttons are the same on every page:** the primary action right-most (the default button), the way out directly left of it (*Cancel*, or *Skip* / *Later* /
  *Close* where that is what leaving means), another way on left of that (*Choose file...*, *Download new version*), and ONE link-style action at the bottom left
  (*Open website*, *Installed elsewhere...*, ...). The title-bar X and Esc do what *Cancel* does. Wording unified: *Open website*, *Choose file...*, *Retry*,
  *Next*. The hash-warning page keeps *Cancel* as its default button on purpose.
- The safe unpacking, copying and checking of files named in a pinned table moved to `package.c` and is shared (the driver's 18 files; the DynaRun `Setup.msi`
  goes through the shared size cap and free-target checks).
- Button wording: the page shown when key 7 was missed now offers *Retry* instead of *Try again* (one wording on every page).
- The driver documentation and the README now say that DynaRunFix does not check the digital signature of the driver files (it compares SHA-256 values and, for *Install anyway*, the Dyna Pro device ID in the INF).

Fixed:

- Cancelling the warning for a driver copy that was found automatically skips such copies from then on; a file or folder you chose with *Choose file...* is always checked as it is and does not affect that memory.
- *Download new version* now really downloads: before, an older DynaRun package already on the computer could be picked up instead and the old version installed again.
- A damaged DynaRun package found on the computer (not a usable zip file) is no longer offered again and again: the wizard says so and offers *Retry* (download) or *Choose file...*.
- With no network at all, a file you had chosen earlier could be deleted when the wizard was closed after a rejected local copy; the download path now always names the wizard's own file.
- The update hint and a download running at the same time could disturb each other (a wrong address or a spurious refusal); each request now has its own buffers.
- A very long driver file or folder name could overflow a buffer on the page that announces the restart-menu step (English text).
- If turning BitLocker protection back on failed, the wizard forgot that it had suspended it; it now keeps the note and tries again at the next cancel, continue or uninstall.
- Long temporary-folder or install-folder paths no longer risk running past a buffer when the setup copies itself from a network drive or backs up registry keys.
- The manual steps of the download error page come from what is being fetched (no driver wording on the DynaRun page); the outline on the restart-menu picture sits in
  the gaps between the lines instead of over the text of lines 6 and 8; the last driver step says up front that Windows shows its red security window twice.
- Logs: a folder is called a folder, the folders are walked once, a text of several lines is several UTF-8 lines (a long one used to be dropped by a fixed buffer), and
  `bcdedit /deletevalue ... -> 1` after a successful install is logged as "not present: nothing to remove" instead of looking like an error.
- Docs: the README's list of in-memory import redirections now also names `WaitForSingleObject` in `dynapro.dll` (opt-in USB switch only, for the `DPSetData` watchdog's helper thread).

## v1.6.0 — 2026-10-17

Added (setup wizard, driver step):

- **The license page is two pages.** A new *Check the installer* page comes first and shows everything the wizard knows about the setup file (where it
  came from, full path, size, SHA-256 of the zip and of the `Setup.msi` inside, MSI ProductVersion and Manufacturer) in a read-only box you can copy
  from; then the license page has only the title, one line, the license text and the accept box.
- **Downloads (DynaRun and driver).** Automatic retries with back-off (1, 2, 4, 8, 15 s, six attempts; the count starts over when data arrived), the
  error page shows the FIRST error of the failed attempts, *Pause* / *Resume*, *Continue download* (from the `.part`, by ETag and Range) with the
  amount already downloaded, *Download manually* on the driver download too, progress text for the driver, a moving bar while the
  size is unknown, and a non-pausable "Checking the driver" phase.
- **Driver: what is used.** The wizard looks for a copy first: its own cache, the setup's folder, Downloads (one folder down, two for an unpacked
  folder), Desktop, Documents; a zip of any name or packing whose driver files match, or an already unpacked folder, is used without downloading.
  The check is of the content: the 18 files Windows installs (pinned table of SHA-256 values); the zip's own SHA-256 is compared too and only noted.
- **Driver: different files.** When they differ a warning page ("The driver differs from the known version") lists the files and the zip hash
  and, if the package still looks like Dyna Pro's, offers *Install anyway* (not the default). That choice binds exactly that zip (or folder) and is
  written as `accepted.txt` (the SHA-256 of the staged files) until the driver step ends; nothing else is accepted later.
- **Starting DynaRun.** The driver's last page has *Start DynaRun*. DynaRun is started as the normal (not elevated) user through the desktop shell,
  also from an elevated wizard; if that is impossible it is not started and the desktop icon is suggested.
- Time-stamped lines of the driver search and your choices go to `%TEMP%\DynaRunFix-setup.log`.

Changed (setup wizard, driver step):

- **Layout.** Text heights are measured (a cut-off buffer made the license page overlap), the window grows when a page needs more room, children are
  clipped against each other, and a status line (also the red "nothing was installed: click Yes") always gets its room: the license text, the picture
  and the info box give way, and setting a status line lays the page out again.
- **The restart-menu picture marks the line to press**: an orange outline around "7) Disable driver signature enforcement" and a red *Press 7* label,
  drawn on the scaled bitmap at run time.
- **Driver: safe unpacking.** Only the 18 pinned files are taken out, to paths built from the program's table (never from entry names), into a new
  folder only administrators can change; links are refused; size limits; hashed after they are copied there and installed from exactly those copies.
- **Driver: BitLocker.** Also suspended when its status cannot be read, and while the drive is still being encrypted or decrypted and has a key protector; the suspension is verified;
  when it fails a warning page explains the recovery key (aka.ms/myrecoverykey) and offers *Restart anyway* / *Later*. `bitlocker-before.txt` and
  `driver.log` record what was seen; recovery passwords are masked before they are written.
- **Auto-start waits for the driver.** With the driver option the sign-in start is registered only when the driver step has ended, so DynaRun does not
  start at the sign-in that continues the installation.

Fixed:

- The driver could be installed right after the preparation step, before the restart (a test now guards the step order). The
  install step also refuses to run while this start still checks signatures and leaves the armed restart in place (the wizard offers *Try again*).
- Cancelling the warning for a mismatched copy found on this computer no longer shows the same warning again; later attempts in that run download from the Dyna Pro website.
- The warning page for a driver that differs always shows the advice to click *Cancel* if in doubt, first, and then the list of differing files (the whole list, or its start with the number of files left out) - before, a long list cut the page off, advice included.
- The progress of a driver download is shown again when the download is started a second time in the same run (after the checking phase had been shown once, the bar and the MB counter stayed still).

## v1.5.0 — 2026-10-16

Added:

- **Setup wizard installs the Dyna Pro USB driver (Windows 10/11).** The main installer has a new option, *Also install the Dyna Pro USB
  driver (one restart needed)* (on by default; hidden when the driver is already installed or Windows is older than 10; `/quiet` never
  installs the driver). It downloads Dyna Pro's official `Dyna Pro USB.zip` from dynapro.co.uk (not bundled; its SHA-256 is checked and a
  changed file is refused), asks for administrator permission (once on an administrator account; a standard user may be asked again after
  the restart), suspends BitLocker **only when protection is on** (until the driver is installed; at most 3 restarts as a safety net, then
  it resumes by itself; it is also resumed when the installation ends or is cancelled, and a retry starts the count again), requests the
  advanced start-up menu for the next start and registers a task that continues the wizard after you sign in. The first driver page
  says that BitLocker is suspended. You press 7 in the menu; if you miss it the wizard says so and offers to try again. Then Windows' red
  warning appears, **twice** (once for each of the two driver files): choose "Install this driver software anyway" each time. Driver
  signature enforcement comes back at the next normal restart. The driver can also be installed later from the Start menu ("Install Dyna
  Pro USB driver") or with `DynaRunFix-Setup.exe /driver`; `DynaRunFix-Setup.exe /driver-cancel` takes back the task, the one-time boot
  option and the BitLocker suspension. Files go to `%ProgramData%\DynaRunFix\driver`, the log is `%ProgramData%\DynaRunFix\driver.log`;
  uninstalling DynaRunFix removes the Start menu shortcut, a pending task and that folder, and resumes BitLocker if DynaRunFix suspended it (even without a pending task), but not the installed driver. See
  [docs/DRIVER.md](docs/DRIVER.md). Not yet verified on a real PC; the BitLocker path has not been tried on real hardware.
- **A DynaRun of another folder no longer blocks the install; one that cannot be closed is said so.** Only a `DynaRun V3.exe` that is the
  installed one (or whose path cannot be read, or while the installed DynaRun's path is not known yet) holds up the install. The wizard
  offers *Close DynaRun and install* only when everything in the way can be closed (same Windows session, path readable); for another
  user's DynaRun it explains that and offers no close button, and the page updates by itself once DynaRun is closed. `/quiet /close` does
  not try to close anything it cannot close and ends with exit code 6.
- **Exit code 6 is explained in a log:** one line per program in the way (name, process id, session, whether it could be closed, path), in
  `setup.log` of the install folder, or in `%TEMP%\DynaRunFix-setup.log` before that folder exists or without administrator rights. Nothing is shown with `/quiet`.

Changed and fixed (after a review of v1.4.0):

- **Driver working folder:** `%ProgramData%\DynaRunFix` is created fresh with write access for administrators only, and an old or in-use folder is never
  reused (`driver.log` is written only after the folder has been checked). If the folder cannot be prepared the wizard says so; restart Windows and try again (a retry after this error needs a restart).
- **Older Windows:** the wizard and `docs/DRIVER.md` give manual steps for Windows 8 / 8.1, Vista / 7 and XP.
- **Driver documentation:** the explanation of the INF and catalog files is reworded to what Windows reports.
- **USB watchdog log:** the `DPSetData watchdog on (usbfix)` line now reports how many hooks were set (`N hook(s) set`).
- **Installer log:** the line log (`setup.log`, `%TEMP%\DynaRunFix-setup.log`) is never written through a link: a symbolic link or a file with
  several hard links in its place is refused.
- **Fix: a DynaRun in a non-default folder.** The launcher asked the registry for the DynaRun path the setup recorded with the size of
  its own path, so a longer recorded path (a custom install folder) made the read fail and the launcher fell back to the default
  location, ending in "Cannot start DynaRun". The path is now read in full; a value that is not text, is empty or does not fit is not used.
- **Update check:** it follows redirects itself, as the downloads do: every hop (https, and one of GitHub's hosts) is checked before a
  request is sent to it. In v1.4.0 WinINet followed the redirects of the check by itself and only the address it ended up at was checked
  (no data was read from a host outside GitHub's); the duplicated host list is gone too.
- **Launcher:** only a DynaRun of the same sign-in session counts as running (with fast user switching another user's DynaRun gave a wrong
  "administrator rights" message); when the elevation state of the desktop shell's token cannot be told it no longer carries on as if it
  were a normal start but stops with the message that no usable token was found.
- **First-time setup:** the optional-feature values are not written when Dyna Pro's helper ends with a non-zero exit code (when the exit
  code cannot be read, it is as before). The README now says exactly when they are written: whenever DynaRun starts that helper
  (normally only on the first start), not "on the very first start only".
- **Opt-in `DYNAFIX_USBFIX`:** the last error code of the abandoned or finished call is handed on (abandoned: `ERROR_TIMEOUT`); a wait on a
  handle whose kind cannot be told is no longer cut short (only events are); `dynapro.dll` is recognised by its file name, not by the end
  of the path (`xdynapro.dll` is not it), and a cut-off module path is not matched.
- **`DYNAFIX_*` switches:** a value must be exactly the expected one (`1`, `0`, a number, `clip`): `10`, `1x` or a long value no longer
  switch something on, and an over-long value is no longer read from an unset buffer.
- **System libraries:** `version.dll`, `rstrtmgr.dll` and `psapi.dll` are loaded from the system folder only.
- **Installer:** the copy of the setup made in `%TEMP%` for the elevated start from a network drive gets a unique name, is created new,
  and is held open (nobody can change it) and compared with the original until the elevated process has ended; uninstall does not
  delete the manifest next to `DynaRun V3.exe`, nor schedule it for deletion at the next restart, when DynaRun's folder or exe can be
  changed by ordinary users.
- **CI:** the release build accepts only tags of the form `vN.N.N`, and the version is passed to the build quoted.
- **README / documentation:** the MSVBVM60 screen-size scan is disclosed (it finds the values by an instruction-byte pattern in
  Microsoft's code, in memory only, changes them only if they equal the real screen size, and leaves them alone when the pattern is not
  found); "modifies no Dyna Pro or THB file" now says "no file's content" and names the manifest and the briefly changed modification
  time; the log does not contain module or import addresses (it can show window and GDI handle values); the graph resource load uses
  `LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE` and a request that would load code is not redirected; the compatibility
  setting *Run as administrator* is described as observed (a test on Windows 11: through the launcher the program ran non-elevated, without a
  prompt; started directly it asked for elevation); uninstall's taskbar policy scope (the user who uninstalls, the recorded account, every
  other account whose layout file is ours) and the full list of what DynaRunFix keeps in the registry and the install folder; the
  hardening tests are run, not only built. The driver wizard, the setup switches and exit codes, and what the driver option changes on
  the PC are described in the README.
- **Tests:** new unit tests for the redirect target rules, the `DYNAFIX_*` parsing and the `dynapro.dll` name match, the launcher's
  registry path read (also a value of the wrong type or one that fills the buffer), its session rule and unknown token state, the order
  of release numbers, the system DLL loading, the BitLocker suspension decision and the installer's log writer.

## v1.4.0 — 2026-10-14

- **Uninstall:** it runs from the installed program and no longer leaves `DynaRunFix-uninstall-<n>.exe` in `%TEMP%` (also not after a
  cancelled UAC prompt; copies of earlier builds are removed). Shortcuts that no backup covers (an older version did not record
  them) are pointed back at `DynaRun V3.exe` instead of staying on the removed launcher; backups made on 64-bit Windows with the
  long or the 8.3 spelling of a folder are restored too.

- **DynaRunFix never runs DynaRun with more rights than a normal start from the desktop.** DynaRun does not need administrator
  rights. The launcher ignores the compatibility setting "Run as administrator" for `DynaRun V3.exe`; if the launcher itself is run
  as administrator, it first starts itself again with the desktop shell's token and ends, so DynaRun (and Locale Emulator) and the
  update check get what a double-click on the desktop gives: the normal, non-elevated token under UAC. If the shell's token is itself
  elevated (UAC off, built-in Administrator: the normal desktop is already full administrator), the launcher carries on with it and
  logs that; it refuses only when no shell token can be had at all, then showing a message and not starting DynaRun. A one-time
  notice (English / Traditional Chinese, can be switched off) is shown when the launcher was started as administrator and the token
  was non-elevated, or the compatibility setting was ignored. If an already running DynaRun was started with more rights than the
  launcher has, the launcher asks you to close it and start it again through DynaRunFix. Shortcuts the installer retargets no longer carry
  the "Run as administrator" flag. Do not run `DynaRun V3.exe` directly as administrator: it can stall at startup
  ("System Initializing. Please Wait. 115") and alter its per-user component registrations.
- **No more COM registrations in the machine-wide registry.** The installer no longer copies DynaRun's per-user COM
  registrations to `HKLM` (it is not needed now that DynaRun runs without elevation). Copies and records made by older
  versions are left as they are, and uninstalling keeps them (`com-copied.txt`, `com-backup`).
- **Graph popup menu in the chosen language without administrator rights:** the graph control (Gigasoft ProEssentials,
  `Pesgo32e.ocx` / `PEGRP32E.dll`) asks for its language resource `PEGRC32D.dll` by name; DynaRun's attempt to copy it into
  `SysWOW64` fails without administrator rights, so the menu was English. `dynafix.dll` now answers that one request with the
  per-user copy from `%APPDATA%\Dyna Pro Dynamometers\Dyna Run V3\Language\<language>\` (resource-only load, validated path,
  not when DynaRun is elevated); nothing is written to the system folder. `DYNAFIX_PEGRC=0` switches it off.
- **Installer, code-page manifest:** to make Windows read the manifest, the installer briefly changes the exe's modification
  time and restores it (the exe's content is not changed). The original time is recorded first and put back at the next run
  if that ever fails. The manifest is not added when DynaRun's folder or exe can be changed by ordinary users (`setup.log`
  in the install folder says so); then DynaRun's path is not recorded in the machine-wide registry either. On Windows XP links (junctions) in that path cannot be detected.
- **Security hardening:**
  - The DynaRun setup (MSI) is copied to a folder that only administrators can write to and checked again there before it
    is run elevated.
  - Uninstall asks for administrator permission first; cancelling changes nothing. It deletes the manifest only if it is identical to
    the installer's own copy, keeps the record of the exe's original modification time if restoring it fails, and the elevated
    uninstall runs from the installed program in the install folder (no copy in `%TEMP%`). If DynaRun (or the launcher / Locale
    Emulator) is running, it asks to close it first, so no `.old` files or `le` folder are left behind; anything still
    locked is deleted at the next restart.
  - Updates: the check talks HTTPS only, and only to GitHub's hosts (every redirect is checked). The release and its SHA-256 are
    verified with GitHub again just before the install, and the downloaded installer is held open so it cannot be swapped between the
    check and the start. The question: *Yes* = install after DynaRun closes, *No* = skip this version, *Cancel* / the close button = ask again tomorrow.
  - `DynaRunFix.exe /restart <pid>` ends after 15 s instead of attaching to the old process.
  - Smaller items in the installer wizard (password wiped from memory, system libraries loaded by full path) and in
    `tools/fetch-le.ps1` (SHA-256 for the licence files). Tests: `tests\build-hardening-tests.cmd`.
  - The taskbar pin technique for Windows 7 to 10 is credited to Gee Law (also used by Firefox); no Mozilla code is included.
- **`DYNAFIX_USBFIX=1` (opt-in, EXPERIMENTAL, off by default, not yet verified with real hardware):**
  tries to keep DynaRun's window from freezing for good when a `DPSetData` call into `dynapro.dll` does not return.
  - `DPSetData` (mode 3) runs on a `dynafix.dll` helper thread while the window thread waits with a watchdog; if the call is still
    blocked after about 2 s and `DPConnStatus()` reports the connection down, `DPSetData` returns 0 and DynaRun's own reconnect logic
    is expected to take over; while the connection is reported up the call is left to run. Facts about `dynapro.dll`'s calls were taken
    from API-boundary logging of DynaRun's own calls; DynaRun's reaction to a 0 result is expected, not verified.
  - No Dyna Pro file on disk is modified. `dynafix.dll` calls `dynapro.dll`'s exported `DPSetData` / `DPConnStatus` and,
    while DynaRun runs and only with the switch set, wraps `DPSetData` in a watchdog in memory: if a call does not return in time,
    DynaRun gets control back and the call is abandoned (it is left to end on a helper thread). Only mode 3 calls go through the watchdog (callers queue); other modes are passed
    straight to the original. The three values passed in `p` are copied back on normal completion. Assumptions: `dynapro.dll` tolerates the call being made from a helper thread, and `p` holds three values (what was observed in DynaRun's calls).
  - Trade-offs, stated honestly: DynaRun's window is unresponsive for about 2 s before it recovers; other calls into `dynapro.dll`
    (`DPConnStatus`, `DPConnect`) overlap with it while an abandoned `DPSetData` is stuck, and how `dynapro.dll` copes with that is
    not verified; the abandoned call may never return, in which case all further `DPSetData` calls return 0 until DynaRun is
    restarted, and a one-time notice suggests restarting it (after 300 or more dropped calls and 20 s).
  - `dynafix.log` gets a `DPSetData watchdog on (usbfix)` line (with how many hooks were set), `usbfix: ...` and `the abandoned call returned after N ms` lines.
- **Security and correctness fixes:**
  - The launcher can no longer start DynaRun with more rights than a normal start (see the first item); the update check no longer runs elevated either.
  - Uninstall removes the current user's taskbar layout policy values as well, and keeps the recorded original
    modification time of `DynaRun V3.exe` when that restore is pending; restoring shortcuts no longer trusts per-user values when run
    elevated, paths read from other users' registry values are copied with bounds checks, and the elevated uninstall no longer
    runs from a predictable file in `%TEMP%`.
  - Downloads: HTTPS and the host are checked on every redirect hop (the update download may only be redirected to GitHub's own hosts), TLS 1.2 or later (Windows 7 needs KB3140245; the daily update check asks for TLS 1.2 as well), the size is capped at 300 MB, a download that ends early is not treated as complete.
    System libraries are loaded from the system folder only (System32).
  - A decrypted Dyna Pro `Setup.msi` is deleted from `%TEMP%\DynaRunFix` when the wizard is cancelled or fails.
  - First start: only option buttons that are clearly read as green (picked) are written; when `dynafix.dll` cannot read DynaRun's
    option buttons (or is unsure what it sees) it writes **nothing** and only logs it (before, it could write feature flags the user had not chosen).
  - `dynafix.dll` clears `__COMPAT_LAYER` for DynaRun's own process so a `RunAsInvoker` set by the launcher does not suppress
    the one-time UAC prompt of Dyna Pro's configuration helper; the graph language redirect is always a resource-only load and
    is refused if the elevation state cannot be determined.
  - `dynafix.log` no longer contains absolute addresses of other modules' import slots, only module and API names, and the launcher and the dll do not follow links (symbolic links, junctions) when opening it.
  - The manifest's version is `1.0.0.0`; the installer's product version is `1.4.0`.
- **Other:** the screen-size adjustment is logged as `screen-size values adjusted (work area): AxB -> CxD`; the Locale
  Emulator notice (`THIRD-PARTY.txt`) also links to forks of its source code, and `build.cmd` regenerates it when it is
  stale; `msgspy` (the window-message logger) is no longer built or shipped (source stays in `tools/msgspy`);
  removed the manual machine-wide COM registration script and its playbook (no longer needed: DynaRun always runs non-elevated).
- **Installer, authenticity:** the Dyna Pro download must stay on HTTPS and on `dynapro.co.uk` (checked on every redirect hop), uses TLS 1.2 or
  later (Windows 7 needs KB3140245), is capped at 300 MB and aborts on any certificate error, with a clear message. The license page shows the setup's full path, size, SHA-256 and the MSI's
  version / manufacturer for you to compare (display only). Releases now include `SHA256SUMS.txt` and a build provenance
  attestation (`gh attestation verify <file> --repo timliudev/DynaRunFix`); the workflow's third-party actions are pinned to commit SHAs.
- **Documentation:** screenshots of DynaRun's own windows were removed (short text descriptions instead); the README states
  precisely what DynaRunFix changes in memory, what the installer changes, which values it writes on the first start (exactly
  the user's own choices), and that `dynafix.log` may contain file paths and window titles.

## v1.3.6 — 2026-10-13

- **Connection diagnostics:** when the Dyna Pro cannot be reached, DynaRun shows nothing for about 13 s and then its
  "USB data connection error" dialog. `dynafix.dll` now watches the connection in memory (nothing on disk, nothing
  DynaRun does is changed) and writes to `%TEMP%\dynafix.log`:
  - every change in the result of DynaRun's connection attempts, and when the connection works again;
  - a device that cannot be opened because another program holds it (`error 32`), with the program's name when
    Windows can tell;
  - a line when the connection has failed for 2 s in a row (check the USB cable and the device's power).

  On the last two a small hint window appears at the bottom right ("The Dyna Pro device is in use by another program ..."
  / "Cannot connect to the Dyna Pro. Check the USB cable and the device's power."; Traditional Chinese on a Chinese
  Windows). It does not take the focus, does not block DynaRun, closes after 10 s or on a click, and is rate-limited.
  `DYNAFIX_CONNHINT=0` turns the hints off (the log stays); `DYNAFIX_CONNHINT_MS=<ms>` changes the 2000 ms (500-60000).
- **The update question could keep DynaRun from starting:** started without `/noupdate`, the launcher ran
  `DynaRunFix-Setup.exe /update` *before* DynaRun and exited; when nobody answered (a tool or script that starts
  DynaRunFix.exe), DynaRun never opened, and with a noted update it happened at every start. Now DynaRun is
  always started (and the fix attached) first. The update check runs in the background after that, and a noted release
  is offered by `DynaRunFix-Setup.exe /offer` beside the running DynaRun, at most once a day: *Yes* installs it after
  DynaRun is closed, *No* asks again tomorrow, *Cancel* skips that version (`UpdateSkip`). Never asked for `/autostart`,
  `/restart` or `/noupdate`, when `DYNAFIX_NOUPDATE=1` is set, or when the launcher attaches to a DynaRun that is
  already running. Every start of `DynaRunFix-Setup.exe` by the launcher is logged first in `%TEMP%\dynafix.log`
  (arguments and reason).
- **Fonts of a reloaded OCX:** `dynafix.dll` remembered every module whose font imports it had redirected by base
  address and never forgot one that was unloaded, so an OCX that DynaRun unloads and loads again (at the same address)
  kept DynaRun's old fonts the second time. It now gets loader notifications from Windows and forgets unloaded
  modules, and the module list is compared as a set instead of by its length, so a module that loads while another
  unloads is not missed either. The log shows `module tracking: ...` lines (registration, forgotten modules, modules
  patched). Without the notifications (before Windows Vista) a signature of the module list is used.
- **Start-up stability (import patching):** every import slot was written with `VirtualProtect(read-write)`, store,
  `VirtualProtect(old protection)`. Other tools that patch the same imports (a logger, a compatibility shim) do the
  same on their own threads; one tool's restore could make the page read-only under the other tool's store and DynaRun
  died with an access violation. A page that is already writable is now written with an interlocked store and its
  protection left alone; a read-only page is made writable and stays writable.
- **Import hooks match by name:** every dynafix import hook (fonts, text metrics, file attributes, THBResize,
  `GetProcAddress`) now finds its slot by the import name instead of by the address the slot holds, so a slot that a
  shim or another tool had already hooked is no longer skipped silently (before, the `GetProcAddress` hook could be
  skipped this way, and with it the first-start fix). It chains onto the hook found there, never hooks a slot twice,
  falls back to an address match only for a module without an import name table, and logs every slot it could not
  patch plus a per-module summary. A slot holding another tool's hook makes the saved "real" pointer chain through it
  (whichever module is walked first); a slot holding a *different* foreign hook after that is left unpatched and
  logged, so no other tool is bypassed. Each patched slot is logged as `patched ... (by name)`.
- **First-start helper match was too loose:** any path DynaRun opened with `ShellExecuteA` that had a component
  starting with `Setup_` (e.g. `Setup_Guide.pdf`, a folder `Setup_files\`) was treated as Dyna Pro's first-start
  configuration helper: dynafix waited for it, rewrote registry values and restarted DynaRun. Now the full path must be
  `...\System Data\Setup_<digits>.exe` (case-insensitive, quotes and `/` accepted) inside
  `...\Dyna Pro Dynamometers\Dyna Run V3\System Data` under `%APPDATA%` (or the shell's AppData folder) or the
  `System Data` folder next to DynaRun's exe; anything else goes untouched to the system's `ShellExecuteA`. Only
  shell32's `ShellExecuteA` is wrapped: another module exporting the same name no longer replaces the real function.
- **Launcher, command line:** the launcher compared `/autostart` and `/restart` with fixed lengths and looked at
  fixed offsets, which could read past the end of a short argument (for example `/a`, `/` or `/autostar`). Flags are
  now recognised as whole first words, case-insensitively, with bounds-checked comparisons; unknown, short, empty
  and very long arguments no longer read outside the command line.
- **Launcher, elevated restart:** when the launcher started itself again as administrator, it passed on only the
  program path and dropped `/noupdate` and `/autostart`. The flags that were given are now passed on (before the
  rest of the command line, which is kept as it was written); the elevated start is logged in `%TEMP%\dynafix.log`.
  `/restart <pid>` is not passed on, its wait is already over.
- **Launcher, attaching to a running DynaRun:** the launcher took the thread of the first window it found as
  DynaRun's GUI thread, which could be a helper window on another thread. It now looks at all top-level windows of
  the process and takes the thread of the visible Visual Basic form (`ThunderRT6Form*`), falling back to other
  `ThunderRT6*` windows and then to any window; the chosen class is logged (`gui-thread chosen from window class`).
- **Log safety:** a very long path or window text in a log line (e.g. a long path to the first-start helper)
  could write past the end of `dynafix.dll`'s 256-byte line buffer. Lines are now cut at 1000 characters
  (ending in `...`) in a buffer large enough for the longest formatted line, always end with a line break, and the
  other places that format text for the log were bounded the same way (a window title in a non-Latin script that did
  not fit its buffer was left unterminated).
- **Log writes:** `dynafix.dll` and the launcher now append each line with one atomic write and take a named
  mutex (`Local\DynaRunFix.dynafix.log`, waits at most 2 s) around every line and around the cut to the newest 8 MB
  above 10 MB, so two processes no longer overwrite, interleave or lose each other's lines, or corrupt the file
  while it is being cut. The log is never cut without the lock.
- **Robustness (internal, no change in behaviour):** the hooks `dynafix.dll` installs in DynaRun are installed only
  once, even if the DLL is entered again, and the one-time hook used by the connection diagnostics is claimed and
  published atomically, so two threads can no longer install it twice or read a half-set pointer; the launcher closes
  its start-up event and library handles on every path; one window message is no longer processed by both the
  launcher's and the DLL's hook; the log, the stock-bitmap table and the font/line maps are guarded by a lock; and the
  walks over import/export tables first check the PE headers and are protected against bad memory, so a damaged or
  unexpected module can no longer crash DynaRun.
- **Window subclass safety:** when a form is destroyed, dynafix restores the previous window procedure only if its
  own is still on top, and reads it with the same A/W flavor it was installed with (for a Unicode window the ANSI
  read returns a conversion thunk that never equals dynafix's procedure, so the check could not match). A subclass
  installed above dynafix later is left untouched and the chain stays intact.
- **`DYNAFIX_STATS=1` (diagnostic):** counters for the THBResize guard (form `WM_SIZE`, duplicates, `0x591` passed /
  skipped, forms shown / hidden) logged every 5 s as `stats(5s)` lines, to check that the hide/show loop is really gone
  after start-up.
- **Build and tests:** `build.cmd` clears `PSModulePath` before it runs Windows PowerShell (the Locale Emulator
  download step failed with "Get-FileHash is not recognized" when the build was started from PowerShell 7).
  New tests: `tests\build-shexec-tests.cmd` builds and runs unit tests for the first-start helper match;
  `tests\build-tests.cmd` builds the log test harness (see `tests\README.md`).

## v1.3.5 — 2026-10-10

- **Run viewer after a run did not fill the screen:** the maximized "Dyna Pro測試後看圖程式" window was clipped to
  the work area *and* kept its aspect ratio (meant only for the caption-less main dashboard), so it was ~76 px too
  narrow with the desktop showing on the right, and maximizing again did not help (log:
  `clipped to the work area 1936x1216 -> 1860x1168`). A form with a caption is now only clipped to the work area.

## v1.3.4 — 2026-10-10

- **Installer, finding the setup:** the search in *Downloads*, on the desktop, in *Documents* and next to the
  installer took any zip with an `.msi` inside, e.g. a tuning tool's zip, unpacked it and
  then showed "This is not the DynaRun V3 setup". Dyna Pro's zip is password-protected, so the search now only takes
  password-protected zips (and DynaRun MSIs as before). A file the installer found by itself that turns out not to be
  DynaRun's is skipped quietly: the next one is tried, or the setup is downloaded. The error page is shown only for a
  file you chose with *Choose file...*, where any zip is still accepted.

## v1.3.3 — 2026-10-09

- **Taskbar (the actual cause):** since v1.3.0 `dynafix.dll` is loaded before DynaRun starts, and its `DllMain`
  looked up `SetCurrentProcessExplicitAppUserModelID` in a shell32 that was not loaded yet: the AppUserModelID was
  never set. The running DynaRun therefore never grouped under the pinned DynaRun icon and showed the exe's chart icon
  as a button of its own (v1.3.0 to v1.3.2, whatever the shortcuts looked like). The ID is now set when dynafix is
  first called in DynaRun, still before its first window, loading shell32 if needed; the log shows
  `AppUserModelID DynaRunFix.DynaRunV3 set: 00000000`. Verified on Windows 11: started from the pinned icon or the
  desktop shortcut, DynaRun shows as the pinned icon with DynaRun's logo, no extra button.

## v1.3.2 — 2026-10-09

- **Taskbar icon:** `DynaRun V3.exe` holds only the chart icon of its windows; DynaRun's blue logo is in the icon file
  of the shortcuts Dyna Pro's setup makes. A desktop shortcut the installer had created (when none existed) used the
  exe's icon, and as the running DynaRun takes the icon of a shortcut with its AppUserModelID, the taskbar could
  still show the chart after v1.3.1. Every shortcut of ours that would use the exe's icon now gets the logo from a
  DynaRun shortcut that has it. Install v1.3.2 (or let the update do it), then start DynaRun again.

## v1.3.1 — 2026-10-09

- **Taskbar icon after an update:** updating from v1.2.x left the existing DynaRun shortcuts (already pointing to
  DynaRunFix) without the AppUserModelID that v1.3.0 gives the running DynaRun, so Windows found no matching shortcut:
  the running DynaRun showed its window icon (a chart) instead of DynaRun's own icon, as a second taskbar button next
  to the pinned one. The installer now writes our shortcuts again with the ID (name, icon, hotkey and *Run as
  administrator* kept). Install v1.3.1 once over v1.3.0 (or let the update do it), then start DynaRun.
- **Docs:** README and changelog: a resolution change while DynaRun is open is DynaRun's own behaviour; the first-start
  layout fix explained.

## v1.3.0 — 2026-10-09

- **Taskbar:** the installer pins DynaRun to the taskbar (check box on the first page, on by default;
  `/notaskbar` turns it off). Windows 7 to 10 pin the launcher shortcut directly, Windows XP / Vista put it
  in Quick Launch, and Windows 11, which lets no program pin itself, gets it through Microsoft's taskbar layout
  policy (needs the administrator prompt the installer shows anyway; the icon appears after the next sign-in; an
  existing layout policy of an organization is left alone and the last page tells how to pin by hand). The
  running DynaRun groups under the pinned icon (explicit AppUserModelID on the launcher shortcuts and in DynaRun).
- **Start at sign-in:** second check box, on by default (`/noautostart` turns it off): a `Run` value for the
  current user starts DynaRun through the launcher. Uninstall removes it (also for other accounts).
- **Screen after a start at sign-in:** the sign-in start (`DynaRunFix.exe /autostart`) waits
  until the taskbar exists and the screen size and work area have been unchanged for 3 s (at most 60 s) before it
  starts DynaRun. If DynaRun's screen
  ever looks wrong, close DynaRun and start it again: the last page of the installer and the README say so.
- **Screen right after an install:** the first start of DynaRun after installing DynaRunFix (the installer's
  *Start DynaRun* button, or the first start at sign-in) could lay out the main screen for the full screen height:
  the right gauge cut off, the dashboard shifted; a second start was fine. The launcher let DynaRun run first and
  only then loaded `dynafix.dll`, and the very first load of a newly installed dll is slow (a few seconds on a fresh
  Windows 11), so DynaRun had made its first windows before the fix was there (all versions since v1.2.2). The
  launcher now loads `dynafix.dll` before DynaRun starts. The log shows `dynafix.dll loaded in … ms` and
  `windows DynaRun had before dynafix: N` (0 = in time). Verified on a fresh zh-TW Windows 11 VM.
- **Known issue (DynaRun's own behaviour):** changing the screen resolution while DynaRun is open leaves the main
  screen laid out for the old size (DynaRun lays out its fixed 4:3 screen once, at start); restart DynaRun.
- **Updates:** once a day, when DynaRun is started (and is not already running), the installed copy of the
  setup asks GitHub (`api.github.com/repos/timliudev/DynaRunFix/releases/latest`) whether there is a newer release;
  nothing else is sent. The launcher waits up to 5 s for the answer; with a newer release it asks *Update now?*
  before DynaRun opens (a slower answer is asked at the next start; logged in `%TEMP%\dynafix.log`). *Yes* downloads that
  release's `DynaRunFix-Setup.exe` from this repository only, checks it against the SHA-256 GitHub lists for it,
  installs it (one administrator prompt) and opens DynaRun; the taskbar pin, the start at sign-in and the desktop
  shortcut stay as they are (`/keep`). *No*, a cancelled prompt or a failed update asks again a day later. Offline,
  or on Windows XP (no TLS 1.2), nothing happens. Versions before this one do not check: install it once by hand.
- **Log:** every line in `%TEMP%\dynafix.log` now starts with the date and time (`YYYY-MM-DD HH:MM:SS.mmm`). The
  launcher logs how it was started (arguments, `/autostart`, `/restart` or plain, parent process, start-up flags
  and show command, working folder, DynaRunFix version); each DynaRun process logs a header line with the DynaRunFix
  version and commit, the version of `DynaRun V3.exe` and the Windows version with its update revision. The log is
  kept below 10 MB: the oldest lines are dropped first (the newest 8 MB stay), checked at each start.
- **Dev builds** show the short commit hash (`-dirty` with uncommitted changes) instead of `dev` in the installer
  title, Programs and Features and the log; they still never check for updates.
- `/quiet` applies both options unless switched off with `/notaskbar` / `/noautostart`. The first page and the
  window are a little taller; the last page says what was set up.

## v1.2.3 — 2026-10-07

- **Removed `tools/msi-novbs`:** DynaRunFix no longer ships anything that changes Dyna Pro's setup. If Windows
  has VBScript switched off, turn it back on under *Optional features*, or use `DynaRunFix-Setup.exe`, which runs
  Dyna Pro's setup without its VBScript wizard pages.
- **Licences:** the installer and the zip now include the miniz licence (`LICENSE-miniz.txt`); miniz is used by
  the installer to unpack Dyna Pro's setup.
- **Docs:** the first-start notes describe only what can be observed from outside DynaRun; the README asks users
  to check their licence agreement with Dyna Pro.

## v1.2.2 — 2026-10-07

- **First start:** the language picked on the first start and the optional features picked on the system
  selection (climate monitor, water cooler, cooling fans, AFR analyser) are kept; Dyna Pro's configuration
  helper used to reset them to English / off. ([details](docs/MANUAL.md#first-time-setup))
- **Fonts:** all of DynaRun's text is drawn in Windows' own UI font for the language (Microsoft JhengHei UI on
  Traditional Chinese, Segoe UI on English Windows) instead of a mix of Arial, MS Sans Serif and MingLiU.
  ([details](docs/MANUAL.md#fonts-windows-ui-font-everywhere))
- **Garbled text:** Windows installed in English with the locale changed to Traditional Chinese later
  (FontAssoc without `ANSI(00)=YES`) now also runs DynaRun through Locale Emulator: every window, tooltips
  included, shows Chinese. ([details](docs/MANUAL.md#garbled-chinese-text-with-a-traditional-chinese-system-locale))
- **Status bar:** the taskbar no longer hides DynaRun's bottom status bar; DynaRun is laid out for the screen's
  work area. ([details](docs/MANUAL.md#status-bar-hidden-behind-the-taskbar))
- **Launcher:** the fix is attached before DynaRun's first window, so the splash screen and the first forms are
  covered too.
- **Installer:**
  - DynaRun running: *Close DynaRun and install* instead of replacing files under a running DynaRun;
    `/quiet` exits with code 6, `/quiet /close` closes DynaRun first.
  - The wizard stays in front of Explorer while DynaRun's setup and the permission prompt run.
  - Works when started from a network drive.
  - New icon.
- **Tool (removed in v1.2.3):** `tools/msi-novbs`
  made Dyna Pro's `Setup.msi` installable without VBScript (for when Windows disables it, planned for about
  2027). `DynaRunFix-Setup.exe` does not need it: it runs that setup without its VBScript wizard pages.

## v1.2.1 — 2026-10-07

- Setup: shows Dyna Pro's download page while downloading, reports the real download size, and recognises the
  DynaRun setup by its UpgradeCode instead of the file name.

## v1.2.0 — 2026-10-07

- One-click setup: downloads and installs DynaRun V3 when it is missing (password and license from Dyna Pro).
- First start no longer hangs after the system selection.
- Windows' UTF-8 option: code-page manifest plus Locale Emulator for a Traditional Chinese system locale.

## v1.1.0 — 2026-10-07

- `DynaRunFix-Setup.exe`: one-click installer for the fix (shortcuts, machine-wide ActiveX registration,
  uninstaller).

## v1.0.0 — 2026-10-07

- Stops the main-screen flicker of DynaRun V3 on Windows 10/11.
- .Dpr files stored in OneDrive (*Always keep on this device*) open again.

---

## 更新紀錄

### v1.7.1 — 2026-10-17

修正:

- **同時安裝驅動程式的完整安裝,系統管理員權限被詢問了兩次,而不是一次。** 先前的說明(v1.5.0 與驅動程式文件)寫「系統管理員帳戶只會問一次」,勾選驅動程式時其實不成立:Windows 為安裝(修正;DynaRun 還沒安裝時先裝它的安裝程式)問一次,為準備驅動程式又問一次。現在由**同一個**提升權限的步驟一次做完,所以系統管理員帳戶在整個安裝只會看到一次提示,重新開機後不會再問(登入時的排程工作本來就以提升的權限執行)。標準使用者在重新開機前要輸入系統管理員密碼,重新開機後和以前一樣再被要求一次。紅色的「Windows 安全性」「無法驗證發行者」視窗不是 UAC 提示,仍然會出現兩次。

變更(安裝精靈):

- **勾選驅動程式時,驅動程式的頁面排在安裝之前:** 授權合約頁之後(DynaRun V3 已安裝時,則是第一頁之後),精靈先說明驅動程式這一步要做什麼、尋找或下載並檢查驅動程式,再顯示重新開機選單的圖片和「安裝並重新開機」按鈕;按下後在同一個提升權限的步驟裡安裝修正、準備驅動程式,然後重新開機。這些頁面的「略過」只安裝修正、不裝驅動程式;重新開機那一頁按「取消」(或關閉視窗)不會改變任何東西。安裝直接進入重新開機時,不會顯示「完成」頁;它的提示(手動釘選到工作列、「非 Unicode 程式的語言」的警告)改在重新開機那一頁顯示。
- 修正安裝失敗時,不會準備驅動程式,顯示原本的錯誤頁。修正已裝好、但驅動程式的準備失敗時,驅動程式的錯誤頁會說明修正本身已安裝,並指出可用開始功能表的捷徑再試驅動程式。只裝驅動程式的情況(第一頁只提供驅動程式、`/driver`、開始功能表捷徑)沒有改變:重新開機那一頁仍是「重新開機」,而且只問一次。
- 內部:提升權限的階段(`/machine`、`/full`)可帶 `/with-driver <zip> [/unverified <sha256>]`,在同一個行程裡於修正之後執行驅動程式的準備;單獨的 `/driver-prep` 沒有改變。

### v1.7.0 — 2026-10-17

新增(安裝精靈):

- **提示:有新版 DynaRun V3。** DynaRun 已安裝且電腦有網路時,第一頁會在背景詢問 Dyna Pro 的軟體頁面有沒有比已安裝版本(Windows Installer 的記錄)更新的版本(頁面的下載表目前寫 3.26.0;解析很寬容:找不到版本就沒有提示)。頁面沒寫版本時,會向網站上的 zip 要第一個位元組,把它的 ETag / 長度 / 日期和上次完成的下載所記錄的比對。有的話會出現一行說明與「下載新版」(同一個取得流程,再走一般安裝;電腦上現有的舊版安裝檔不會被拿來用)。完全不會自動執行、不會擋任何事,每個結果是一行 log。
- **驅動程式可以選資料夾:** 驅動程式的「選擇檔案…」可以選 zip,或已解開資料夾裡的 ftdibus.inf,走同樣的兩層檢查與警告頁。
- **已安裝 DynaRun 與修正、只缺驅動程式:** 第一頁只提供驅動程式(以及「重新安裝修正…」)。「修正是最新的」指的是:解除安裝項目的版本就是這個安裝程式的版本,而且安裝資料夾裡有啟動器與 dll。

變更(安裝精靈,DynaRun 與驅動程式):

- **DynaRun 安裝檔與驅動程式共用同一個取得流程**(同樣的頁面、同樣的程式):網路檢查(Windows 的網路清單;明確沒有連線就直接顯示錯誤頁)、找電腦上現有的、下載(連線逾時 10 秒、接收 30 秒,退避不變),附進度、「暫停」/「繼續」與「繼續下載」、檢查、結果。兩者共用一個錯誤頁(標題「無法自動下載…」、第一個真正的錯誤、手動步驟、「重試」/「繼續下載」、「選擇檔案…」,以及只有驅動程式可略過時才有的「略過」)。兩個流程重複的錯誤文字合併成一個函式。
- **所有頁面的按鈕一致:** 主要動作在最右邊(預設按鈕),離開的方式緊鄰它的左邊(「取消」,離開的意義是略過/稍後/關閉時用該字),再往左是另一個前進的方式(「選擇檔案…」、「下載新版」),最左下角只有一個連結式動作(「開啟官網」、「已裝在其他位置…」…)。標題列的 X 與 Esc 的作用等同「取消」。用字統一:「開啟官網」、「選擇檔案…」、「重試」、「下一步」。雜湊警告頁刻意維持「取消」為預設按鈕。
- 以固定表格指名的檔案之安全解壓、複製與檢查移到 `package.c` 共用(驅動程式的 18 個檔案;DynaRun 的 `Setup.msi` 走共用的大小上限與釋放目標檢查)。
- 按鈕用字統一:沒按到 7 時的頁面提供「重試」,不再是「再試一次」。
- 驅動程式說明與 README 現在寫明:DynaRunFix 不驗證驅動程式檔的數位簽章(只比對 SHA-256,「仍要安裝」時再看 INF 裡的 Dyna Pro 裝置 ID)。

修正:

- 取消「自動找到的驅動程式副本」的警告後,之後的自動搜尋才會跳過這類副本;用「選擇檔案…」自己選的檔案或資料夾一律照實檢查,也不影響這個記憶。
- 「下載新版」現在真的會下載:以前電腦上已有較舊的 DynaRun 安裝檔時,可能被直接拿來用,結果又裝回舊版。
- 電腦上找到的 DynaRun 安裝檔如果是損毀的檔案,不再一直重複提供:精靈會說明,並提供「重試」(下載)或「選擇檔案…」。
- 完全沒有網路時,先前拒絕了電腦上的副本,關閉精靈後可能刪到你自己選的檔案;現在下載路徑一律是精靈自己的檔案。
- 新版提示和下載同時進行時可能互相干擾(連到錯的位址或誤判被拒絕);現在每個請求各用自己的緩衝區。
- 過長的驅動程式檔名或資料夾名稱,可能讓說明重新開機選單那一步的頁面緩衝區溢位(英文介面)。
- 重新開啟 BitLocker 保護失敗時,精靈會忘記自己曾經暫停它;現在會保留記錄,下次取消、繼續或解除安裝時再試一次。
- 安裝程式從網路磁碟機複製自己、或備份登錄機碼時,過長的暫存資料夾或安裝資料夾路徑不再有超出緩衝區的風險。
- 下載錯誤頁的手動步驟改由要取得的東西決定(DynaRun 頁面不再出現驅動程式的字眼);重新開機選單圖片上的外框改放在各行之間的空隙,不再壓到第 6、8 行的文字;驅動程式的最後一步會事先說明 Windows 會跳出紅色安全性視窗兩次。
- Log:資料夾就叫資料夾、資料夾只走一遍、多行文字會寫成多行 UTF-8(太長的原本會被固定緩衝區整行丟掉),安裝成功後的 `bcdedit /deletevalue ... -> 1` 改記為「不存在:沒有東西要移除」,不再像是錯誤。
- 文件:README 的記憶體內匯入導向清單現在也列出 `dynapro.dll` 的 `WaitForSingleObject`(只有開啟選用的 USB 開關時,供 `DPSetData` 監看的輔助執行緒使用)。

### v1.6.0 — 2026-10-17

新增(安裝精靈、驅動程式步驟):

- **授權合約頁拆成兩頁。** 先出現新的「確認安裝檔」頁,完整顯示精靈所知道的安裝檔資訊(來源、完整路徑、大小、zip 與其中 `Setup.msi` 的 SHA-256、MSI 的 ProductVersion 與 Manufacturer),放在可以複製文字的唯讀框裡;接著授權合約頁只剩標題、一行說明、合約文字和接受核取方塊。
- **下載(DynaRun 與驅動程式)。** 自動重試與退避(1、2、4、8、15 秒,共六次;有資料進來就重新計數)、錯誤頁顯示失敗過程中的第一個錯誤、「暫停」/「繼續」、「繼續下載」(依 ETag 與 Range 從 `.part` 接續,並顯示已下載量)、驅動程式下載也有「開啟官網手動下載」、驅動程式也有進度文字、大小未知時進度條會移動,以及不可暫停的「正在檢查驅動程式」階段。
- **驅動程式:用哪一份。** 精靈先找現成的:它自己的暫存、安裝程式所在資料夾、下載(往下一層,已解開的資料夾往下兩層)、桌面、文件;任何檔名或打包方式、只要驅動程式檔案相符的 zip,或已經解開的資料夾,都會直接使用、不用下載。檢查的是內容:Windows 會安裝的 18 個檔案(內建的 SHA-256 表);zip 本身的 SHA-256 也會比對,但只做記錄。
- **驅動程式:檔案不同時。** 會顯示警告頁(「驅動程式和已知版本不同」),列出不同的檔案與 zip 的雜湊,而且在套件看起來仍是 Dyna Pro 的時候提供「仍要安裝」(不是預設)。這個選擇只綁定那一個 zip(或資料夾),並以 `accepted.txt`(暫存檔案的 SHA-256)存在到驅動程式步驟結束;之後不接受其他任何東西。
- **啟動 DynaRun。** 驅動程式的最後一頁有「開始使用 DynaRun」。DynaRun 以一般(未提升)使用者身分透過桌面 shell 啟動,即使精靈是提升權限的也一樣;做不到時就不啟動,並建議使用桌面圖示。
- 驅動程式搜尋與你的選擇會以帶時間的行寫入 `%TEMP%\DynaRunFix-setup.log`。

變更(安裝精靈、驅動程式步驟):

- **版面。** 文字高度改為實際量測(原本的緩衝區過小造成授權頁重疊)、頁面需要更多空間時視窗會變高、子控制項彼此裁切、狀態列(包括紅字「沒有安裝:需要按『是』」)一定保留空間:授權文字、圖片和資訊框會讓出位置,設定狀態列時會重新排版。
- **重新開機選單的圖片標出要按的那一行**:在「7) 停用驅動程式強制簽章」外畫橘色框,旁邊有紅色的「按 7」標籤,在縮放後的點陣圖上即時繪製。
- **驅動程式:安全解開。** 只取出 18 個固定的檔案,目的路徑由程式自己的表格決定(絕不用項目名稱),放進只有系統管理員能更改的新資料夾;拒絕連結;有大小上限;複製到那裡之後才計算雜湊,安裝的就是這些副本。
- **驅動程式:BitLocker。** 無法讀取其狀態時,以及磁碟機仍在加密或解密中且有金鑰保護裝置時也會暫停;暫停之後會檢查;失敗時警告頁會說明修復金鑰(aka.ms/myrecoverykey)並提供「仍要重新開機」/「稍後」。`bitlocker-before.txt` 與 `driver.log` 記錄所見;修復密碼寫入前先遮蔽。
- **開機自動啟動等驅動程式。** 使用驅動程式選項時,開機自動啟動要等驅動程式步驟結束才登錄,所以 DynaRun 不會在接續安裝的那次登入時啟動。

修正:

- 驅動程式可能在準備步驟之後馬上就安裝,沒有等重新開機(現在有測試把關步驟順序)。安裝步驟在這次啟動仍檢查簽章時也會拒絕執行,並保留已設定的重新開機(精靈會提供「再試一次」)。
- 對這台電腦上找到、內容不符的檔案取消警告頁後,不會再出現同一個警告;這次執行中之後的嘗試都會從 Dyna Pro 官網下載。
- 驅動程式不同的警告頁一定先顯示「不確定請按取消」的建議,再列出不同的檔案(完整列出,或只列開頭並註明還有幾個);之前清單太長時頁面會被截斷,連建議也看不到。
- 同一次執行中第二次下載驅動程式時,會再顯示進度(之前檢查階段出現過一次之後,進度條與 MB 計數就不動了)。

### v1.5.0 — 2026-10-16

新增:

- **安裝精靈可安裝 Dyna Pro USB 驅動程式(Windows 10/11)。** 主安裝程式新增一個選項「同時安裝 Dyna Pro USB 驅動程式(需重新開機一次)」(預設勾選;
  驅動程式已安裝或 Windows 低於 10 時隱藏;`/quiet` 不會安裝驅動程式):從 dynapro.co.uk 下載 Dyna Pro 官方的 `Dyna Pro USB.zip`(不內附;會檢查 SHA-256,
  檔案有變就拒絕安裝),要求系統管理員權限(使用系統管理員帳戶時只會問一次;標準使用者在重新啟動後可能會再被詢問),
  **只有在 BitLocker 保護為開啟時**才暫停它,直到驅動程式安裝完成(最多 3 次重新啟動作為保險,之後自動恢復;安裝結束或取消時也會恢復,重試時會重新計算次數),
  要求下次開機顯示進階啟動選單,並登錄一個工作,讓精靈在你登入後繼續。驅動程式的第一頁會說明 BitLocker 會被暫停。你在選單按 7;
  沒按到的話精靈會告知並提供「再試一次」。接著 Windows 會跳出紅色警告,而且**會出現兩次**(兩個驅動程式檔各一次),每次都請選「仍然安裝此驅動程式軟體」。
  驅動程式強制簽章在下次正常重新啟動後恢復。之後也可以從開始功能表(「安裝 Dyna Pro USB 驅動程式」)或用 `DynaRunFix-Setup.exe /driver` 啟動;
  `DynaRunFix-Setup.exe /driver-cancel` 會撤回工作、一次性開機選項與 BitLocker 暫停。檔案放在 `%ProgramData%\DynaRunFix\driver`,記錄檔是 `%ProgramData%\DynaRunFix\driver.log`;
  解除安裝 DynaRunFix 會移除開始功能表捷徑、尚未完成的工作與該資料夾,並在 BitLocker 是 DynaRunFix 暫停的情況下把它恢復(即使已沒有等待中的工作),但不會移除已安裝的驅動程式。見 [docs/DRIVER.md](docs/DRIVER.md)。尚未在實體電腦上驗證;BitLocker 的流程尚未在真實硬體上試過。
- **別的資料夾的 DynaRun 不再擋住安裝;關不掉的會明確說明。** 只有已安裝的那個 `DynaRun V3.exe`(或讀不到路徑的,或已安裝的 DynaRun 路徑還不知道時)會擋住安裝。
  只有擋住的程式都能關閉(同一個登入工作階段、讀得到路徑)時,精靈才提供「關閉 DynaRun 並安裝」;別的使用者的 DynaRun 則顯示說明、
  不提供關閉按鈕,對方關閉後頁面會自動更新。`/quiet /close` 不會嘗試關閉關不掉的程式,直接以結束代碼 6 結束。
- **結束代碼 6 會留下原因記錄:** 每個擋住的程式一行(名稱、處理序編號、工作階段、能否關閉、路徑),寫在安裝資料夾的 `setup.log`,
  該資料夾尚未建立或沒有系統管理員權限時寫在 `%TEMP%\DynaRunFix-setup.log`。`/quiet` 不會顯示任何視窗。

變更與修正(v1.4.0 審查後):

- **驅動程式工作資料夾:** `%ProgramData%\DynaRunFix` 會全新建立,只有系統管理員可寫入;舊的或使用中的資料夾絕不重複使用(檢查過資料夾後才寫 `driver.log`)。
  資料夾無法準備好時精靈會告知,請重新啟動 Windows 再試(出現此錯誤後重試需要重新啟動)。
- **舊版 Windows:** 精靈與 `docs/DRIVER.md` 提供 Windows 8 / 8.1、Vista / 7 與 XP 的手動步驟。
- **驅動程式文件:** INF 與目錄檔的說明改成 Windows 實際回報的內容。
- **USB 監看記錄:** `DPSetData watchdog on (usbfix)` 這一行現在會顯示設了幾個 hook(`N hook(s) set`)。
- **安裝程式記錄檔:** 逐行記錄檔(`setup.log`、`%TEMP%\DynaRunFix-setup.log`)絕不透過連結寫入:該位置若是符號連結或有多個硬連結的檔案,會被拒絕。
- **修正:DynaRun 在非預設資料夾。** 啟動器以自己路徑的長度去讀安裝程式記錄的 DynaRun 路徑,所以較長的記錄路徑(自訂安裝資料夾)會讀取失敗,
  啟動器改用預設位置,最後出現「無法啟動 DynaRun」。現在會完整讀取路徑;不是文字、是空的或放不下的值不會被使用。
- **更新檢查:** 和下載一樣自己處理重新導向:每一跳(https,且是 GitHub 的主機之一)都在送出要求之前先檢查。v1.4.0 的檢查是由 WinINet 自己跟隨重新導向,
  只檢查最後到達的網址(沒有從 GitHub 以外的主機讀取任何資料);重複的主機清單也移除了。
- **啟動器:** 只有同一個登入工作階段的 DynaRun 才算執行中(快速切換使用者時,別人的 DynaRun 會造成錯誤的「系統管理員權限」訊息);
  無法判斷桌面殼層 token 是否已提升時,不再當成一般啟動繼續,而是顯示找不到可用 token 的訊息並停止。
- **首次設定:** Dyna Pro 的設定程式以非零結束碼結束時,不寫入選購功能值(讀不到結束碼時維持原樣)。README 現在精確說明何時寫入:每當 DynaRun 啟動該設定程式時
  (通常只有第一次啟動),而不是「只在第一次啟動」。
- **選用的 `DYNAFIX_USBFIX`:** 放棄或完成的呼叫的最後錯誤碼會傳給呼叫端(放棄時為 `ERROR_TIMEOUT`);種類不明的 handle 的等待不再被縮短(只有 event 會);
  `dynapro.dll` 以檔名辨識,而不是路徑結尾(`xdynapro.dll` 不是它),被截斷的模組路徑不會比對成功。
- **`DYNAFIX_*` 開關:** 值必須剛好是預期的(`1`、`0`、數字、`clip`):`10`、`1x` 或以 1 開頭的長字串不再開啟任何功能,過長的值也不會再從未初始化的緩衝區讀取。
- **系統程式庫:** `version.dll`、`rstrtmgr.dll`、`psapi.dll` 只從系統資料夾載入。
- **安裝程式:** 從網路磁碟以提升權限啟動時在 `%TEMP%` 做的安裝程式副本使用唯一名稱、以新建方式建立,並一直保持開啟(沒有人能更改)、與原檔比對,直到提升的行程結束;
  DynaRun 的資料夾或 exe 一般使用者可以更改時,解除安裝不會刪除 `DynaRun V3.exe` 旁的 manifest,也不會排程在下次重新開機時刪除。
- **CI:** Release 只接受 `vN.N.N` 形式的標籤,版本號傳給建置時有加上引號。
- **README／文件:** 揭露 MSVBVM60 螢幕大小的特徵掃描(以 Microsoft 程式碼中的一段指令位元組特徵找出這些值,只在記憶體中,只有等於真實螢幕大小時才修改,找不到特徵就不動);
  「不修改任何 Dyna Pro 或 THB 檔案」改為「不修改任何檔案的內容」,並寫明 manifest 與短暫改變的修改時間;log 不含模組或匯入位址(可能出現視窗與 GDI 控制代碼的值);
  圖表資源載入使用 `LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE`,會載入程式碼的要求不會被導向;相容性設定「以系統管理員身分執行」改為觀察結果
  (Windows 11 上的測試:經由啟動器時程式以未提升的權限執行、沒有提示;直接啟動則會要求提升);解除安裝的工作列原則範圍(執行解除安裝的使用者、記錄的帳號、
  其他配置檔仍是我們的帳號)以及 DynaRunFix 在登錄與安裝資料夾留下什麼的完整清單;強化測試是建置並執行,不只是建置。README 也說明了驅動程式精靈、安裝程式的參數與結束代碼,
  以及驅動程式選項會在電腦上改變什麼。
- **測試:** 新增重新導向目標規則、`DYNAFIX_*` 解析與 `dynapro.dll` 檔名比對、啟動器讀取登錄路徑(含型別不符或剛好填滿緩衝區的值)、工作階段規則與未知 token 狀態、
  版本號先後順序、系統 DLL 載入、BitLocker 暫停判斷與安裝程式記錄檔寫入的單元測試。

### v1.4.0 — 2026-10-14

- **解除安裝:** 從已安裝的程式執行,不再在 `%TEMP%` 留下 `DynaRunFix-uninstall-<n>.exe`(UAC 取消時也不會;先前版本留下的副本會被清掉)。
  沒有備份可還原的捷徑(舊版沒有記錄)會改回指向 `DynaRun V3.exe`,不再停留在已移除的啟動器上;資料夾以長名稱或 8.3 短名稱記錄的備份也能還原。

- **DynaRunFix 一律不會以高於「從桌面正常啟動」的權限執行 DynaRun。** DynaRun 不需要系統管理員權限。啟動器會忽略 `DynaRun V3.exe` 的相容性設定「以系統管理員身分執行」;
  如果啟動器本身是以系統管理員身分執行的,就先用桌面殼層的 token 重新啟動自己並結束,讓 DynaRun(和 Locale Emulator)與更新檢查得到從桌面雙擊會得到的權限(UAC 下是一般、未提升的 token)。
  殼層的 token 本身已提升時(UAC 關閉、內建 Administrator:平常的桌面就是完整系統管理員),啟動器沿用它繼續並寫入 log;
  只有完全取不到殼層 token 時才會顯示訊息、不啟動 DynaRun。啟動器是以系統管理員身分啟動、且 token 為一般權限,或相容性設定被忽略時,會顯示一次性通知
  (英文 / 繁體中文,可關閉)。如果已在執行的 DynaRun 是以高於啟動器的權限啟動的,啟動器會請你關閉它,再透過 DynaRunFix 重新啟動。
  安裝程式改指向啟動器的捷徑不再帶有「以系統管理員身分執行」旗標。請不要直接以系統管理員身分執行 `DynaRun V3.exe`:
  它可能在啟動時卡住(「System Initializing. Please Wait. 115」),並改動它的使用者元件註冊。
- **不再把 COM 註冊放進全機登錄。** 安裝程式不再把 DynaRun 的使用者 COM 註冊複製到 `HKLM`(DynaRun 不以提升權限執行後就不需要了)。
  舊版留下的複本與紀錄維持原樣,解除安裝也會保留(`com-copied.txt`、`com-backup`)。
- **沒有系統管理員權限時,圖表右鍵選單也使用所選語言:** 繪圖元件(Gigasoft ProEssentials,`Pesgo32e.ocx` / `PEGRP32E.dll`)依檔名索取語言資源檔 `PEGRC32D.dll`;
  DynaRun 想把它複製到 `SysWOW64`,沒有系統管理員權限時失敗,選單因而是英文。現在 `dynafix.dll` 只針對這一次要求,改用
  `%APPDATA%\Dyna Pro Dynamometers\Dyna Run V3\Language\<語言>\` 的副本(只載入資源、驗證過的路徑、DynaRun 以提升權限執行時不處理),
  不寫入系統資料夾。`DYNAFIX_PEGRC=0` 可關閉。
- **安裝程式,字碼頁 manifest:** 為了讓 Windows 讀取 manifest,安裝程式會短暫改變 exe 的修改時間後還原(exe 的內容不會被改動)。
  原本的時間會先記錄下來,萬一還原失敗,下次執行時會補回。DynaRun 的資料夾或 exe 一般使用者可以更改時不會加 manifest(安裝資料夾的 `setup.log` 會說明),此時 DynaRun 的路徑也不會記進全機登錄。Windows XP 無法偵測該路徑中的連結(連接點)。
- **安全性強化:**
  - DynaRun 安裝檔(MSI)會先複製到只有系統管理員能寫入的資料夾,並在那裡再檢查一次,才以系統管理員權限執行。
  - 解除安裝會先要求系統管理員權限,取消的話什麼都不會改變。只有 manifest 與安裝程式自己的副本完全相同才會刪除;還原 exe 原始修改時間失敗時,會保留該紀錄;解除安裝從已安裝的程式(安裝資料夾)執行,不在 `%TEMP%` 另存副本。DynaRun(或啟動器 / Locale Emulator)執行中時會先請你關閉,
    不會留下 `.old` 檔或 `le` 資料夾;仍被占用的會在下次重新開機時刪除。
  - 更新:檢查只用 HTTPS,而且只連 GitHub 的主機(每次轉址都檢查)。安裝前會再向 GitHub 確認該版本及其 SHA-256,下載的安裝檔也一直保持開啟,無法在檢查與啟動之間被調包。
    詢問:「是」＝DynaRun 關閉後安裝,「否」＝略過這個版本,「取消」或關閉鈕＝明天再問。
  - `DynaRunFix.exe /restart <pid>` 等 15 秒後結束,不會去接上舊的行程。
  - 安裝精靈(密碼用完即從記憶體清除、系統程式庫以完整路徑載入)與 `tools/fetch-le.ps1`(授權檔的 SHA-256)的一些小項目。測試:`tests\build-hardening-tests.cmd`。
  - Windows 7 到 10 的工作列釘選手法標明出自 Gee Law(Firefox 也用同一手法);不含任何 Mozilla 程式碼。
- **`DYNAFIX_USBFIX=1`(選用,實驗性,預設關閉,尚未用實體硬體驗證):** 嘗試避免 `dynapro.dll` 的 `DPSetData` 呼叫不返回時,DynaRun 的視窗永久卡死。
  - mode 3 的 `DPSetData` 改在 `dynafix.dll` 的輔助執行緒執行,視窗執行緒以監看計時等待;若 2 秒後仍卡住且 `DPConnStatus()` 回報連線已斷,
    `DPSetData` 就傳回 0,預期由 DynaRun 自己的重新連線邏輯接手;連線回報正常時呼叫會繼續執行。關於 `dynapro.dll` 各函式的事實,來自對 DynaRun 自己的呼叫做 API 邊界記錄;
    DynaRun 對 0 的反應是預期,未經驗證。
  - 不修改磁碟上任何 Dyna Pro 的檔案。`dynafix.dll` 呼叫 `dynapro.dll` 匯出的 `DPSetData` / `DPConnStatus`,
    並在 DynaRun 執行期間、只有設了開關時,於記憶體中為 `DPSetData` 加上監看:呼叫若沒有及時返回,DynaRun 會取回控制權,該次呼叫被放棄(留在輔助執行緒上結束)。
    只有 mode 3 的呼叫會經過監看(呼叫端排隊),其他 mode 直接交給原函式。傳入 `p` 的三個值在正常結束時會複製回去。假設:`dynapro.dll` 能接受由輔助執行緒呼叫,且 `p` 是三個值(在 DynaRun 的呼叫中觀察到的)。
  - 取捨(照實說明):DynaRun 的視窗約 2 秒內沒有反應才恢復;被放棄的 `DPSetData` 還卡著時,對 `dynapro.dll` 的其他呼叫(`DPConnStatus`、`DPConnect`)會與它重疊,`dynapro.dll` 如何處理尚未驗證;
    被放棄的呼叫可能永遠不返回,此時之後所有 `DPSetData` 呼叫都傳回 0,直到重新啟動 DynaRun,並在丟棄 300 次以上且過 20 秒後顯示一次性提示建議重新啟動。
  - `dynafix.log` 會有 `DPSetData watchdog on (usbfix)` 一行(含設了幾個 hook)、`usbfix: ...` 與 `the abandoned call returned after N ms`。
- **安全性與正確性修正:**
  - 啟動器不再可能以高於正常啟動的權限啟動 DynaRun(見第一項);更新檢查也不再以提升權限執行。
  - 解除安裝也會移除目前使用者的工作列配置原則值;還原捷徑時,以提升權限執行就不再信任使用者層級的值;
    讀自其他使用者登錄值的路徑一律做長度檢查後才複製;以提升權限執行的解除安裝不再從 `%TEMP%` 中可預測的檔案執行。
  - 下載:每一次轉址都檢查 HTTPS 與主機(更新下載只能轉址到 GitHub 自己的主機)、使用 TLS 1.2 以上(Windows 7 需要 KB3140245)、大小上限 300 MB、提前中斷的下載不會被當成完成。系統程式庫只從系統資料夾(System32)載入。
  - 精靈取消或失敗時,會從 `%TEMP%\DynaRunFix` 刪除已解密的 Dyna Pro `Setup.msi`。
  - 首次啟動:只有明確讀到是綠色(已勾選)的選項按鈕才會寫入;`dynafix.dll` 讀不到 DynaRun 的選項按鈕(或無法確定看到的內容)時**什麼都不寫**,只記 log(以前可能寫入使用者沒有選的功能旗標)。
  - `dynafix.dll` 在 DynaRun 自己的行程中清除 `__COMPAT_LAYER`,使啟動器設的 `RunAsInvoker` 不會讓 Dyna Pro 設定程式的一次性 UAC 提示消失;
    圖表語言的導向一律只載入資源,而且無法判斷是否提升權限時一律拒絕。
  - `dynafix.log` 不再記錄其他模組匯入表欄位的絕對位址,只記模組與 API 名稱;啟動器與 dll 開啟它時也不跟隨連結(符號連結、連接點)。
  - manifest 的版本是 `1.0.0.0`;安裝程式的產品版本是 `1.4.0`。
- **其他:** 螢幕大小調整記為 `screen-size values adjusted (work area): AxB -> CxD`;Locale Emulator 的聲明(`THIRD-PARTY.txt`)也附上其原始碼 fork 的連結,
  `build.cmd` 會在它過期時重新產生;不再建置、不再附 `msgspy`(視窗訊息記錄工具),原始碼仍在 `tools/msgspy`;
  移除了手動的全機 COM 註冊腳本及其說明文件(不再需要:DynaRun 一律以非提升權限執行)。
- **安裝程式,真偽確認:** 從 Dyna Pro 下載時必須全程使用 HTTPS 並停留在 `dynapro.co.uk`(每一次轉址都檢查),使用 TLS 1.2 以上(Windows 7 需要 KB3140245),大小上限 300 MB,任何憑證錯誤都會中止並顯示明確訊息。
  授權合約頁面會顯示安裝檔的完整路徑、大小、SHA-256 與 MSI 的版本 / 製造商供你比對(僅顯示)。版本現在附有 `SHA256SUMS.txt` 與建置來源證明
  (`gh attestation verify <檔案> --repo timliudev/DynaRunFix`);工作流程使用的第三方 action 已固定到 commit SHA。
- **文件:** 移除了 DynaRun 自己視窗的截圖(改用簡短文字說明);README 精確說明 DynaRunFix 在記憶體中改了什麼、安裝程式改了什麼、
  首次啟動寫入哪些值(就是使用者自己的選擇),並提醒 `dynafix.log` 可能含有檔案路徑與視窗標題。

### v1.3.6 — 2026-10-13

- **連線診斷:** 連不上 Dyna Pro 時,DynaRun 約 13 秒內什麼都不顯示,之後才跳出「USB資料連線錯誤」對話框。
  `dynafix.dll` 現在在記憶體中觀察連線狀況(不改任何檔案,也不改變 DynaRun 的行為),並寫入 `%TEMP%\dynafix.log`:
  - DynaRun 每次連線嘗試的結果有變化時,以及恢復連線時;
  - 裝置被其他程式占用而打不開(`error 32`),Windows 查得到時一併記下占用的程式名稱;
  - 連線連續失敗 2 秒時記一行(請檢查 USB 線與裝置電源)。

  後兩種情況會在畫面右下角出現一個小提示(「Dyna Pro 裝置正被其他程式使用 ...」/「無法連線到 Dyna Pro。請檢查 USB 線與裝置電源。」;
  非中文 Windows 顯示英文)。提示不搶焦點、不卡住 DynaRun,10 秒後或點一下就消失,且有頻率限制。`DYNAFIX_CONNHINT=0` 關閉提示(log 照寫);
  `DYNAFIX_CONNHINT_MS=<毫秒>` 調整 2000 ms(500-60000)。
- **更新詢問可能讓 DynaRun 開不起來:** 沒帶 `/noupdate` 時,啟動器會在 DynaRun 之前執行 `DynaRunFix-Setup.exe /update` 然後結束;
  若沒人回答(例如工具或腳本啟動 DynaRunFix.exe),DynaRun 就不會開,而已記下新版時每次啟動都會這樣。現在一律先啟動 DynaRun(並掛上修正),
  之後才在背景檢查更新;已知的新版由 `DynaRunFix-Setup.exe /offer` 在執行中的 DynaRun 旁詢問,每天最多一次:「是」＝DynaRun 關閉後安裝,
  「否」＝明天再問,「取消」＝略過該版本(`UpdateSkip`)。`/autostart`、`/restart`、`/noupdate`、設了 `DYNAFIX_NOUPDATE=1`,
  或接上已在執行的 DynaRun 時,一律不問。啟動器每次啟動 `DynaRunFix-Setup.exe` 前,都會先在 `%TEMP%\dynafix.log` 記下參數與原因。
- **重新載入的 OCX 字型:** `dynafix.dll` 以基底位址記住已處理過字型匯入的模組,卸載後也不忘記;DynaRun 卸載後在同一位址
  再載入的 OCX 因此第二次用回原本的字型。現在透過 Windows 的載入通知忘記已卸載的模組,並以模組集合而非數量判斷有無變化,
  同時一個載入、一個卸載也不會漏掉。記錄檔有 `module tracking: ...` 行。Windows Vista 以前沒有此通知,改比對模組清單的簽章。
- **啟動穩定性(匯入修補):** 以前每個匯入位置都是 `VirtualProtect(可寫)`、寫入、`VirtualProtect(還原)`。其他修補同一批匯入的工具
  (記錄工具、相容性 shim)也在自己的執行緒這樣做,一方還原保護時可能讓另一方寫入時頁面變成唯讀,DynaRun 就因存取違規而當掉。
  現在已經可寫的頁面改用 interlocked 寫入、不動保護屬性;唯讀頁面改成可寫後就維持可寫。
- **匯入 hook 依名稱比對:** dynafix 所有匯入 hook(字型、文字度量、檔案屬性、THBResize、`GetProcAddress`)現在依匯入名稱找位置,
  不再依位置裡存的位址,所以已被 shim 或其他工具 hook 過的位置不會再被默默略過(以前 `GetProcAddress` hook 可能因此被略過,首次啟動的修正也跟著失效)。
  遇到既有 hook 會串接上去,同一位置不會 hook 兩次;只有沒有匯入名稱表的模組才退回依位址比對;每個無法修補的位置和每個模組的統計都會記在 log。
  位置裡已有其他工具的 hook 時,保存的「真正」指標會經過它(不論先處理哪個模組);之後若又遇到*另一個*外來 hook,該位置不修補並記錄,不會跳過任何工具。
  每個修補成功的位置記為 `patched ... (by name)`。
- **首次啟動設定程式的比對太寬鬆:** 原本 DynaRun 以 `ShellExecuteA` 開啟的路徑中,只要有任何元件以 `Setup_` 開頭
  (例如 `Setup_Guide.pdf`、資料夾 `Setup_files\`),就被當成 Dyna Pro 首次啟動的設定程式:dynafix 會等它結束、改寫登錄值,
  然後重新啟動 DynaRun。現在必須是完整路徑吻合 `...\System Data\Setup_<數字>.exe`(不分大小寫、可含引號與 `/`),
  且資料夾是使用者的 `...\Dyna Pro Dynamometers\Dyna Run V3\System Data`(依 `%APPDATA%` 或系統的 AppData 資料夾)或 DynaRun 執行檔旁的 `System Data`;
  其他一律原封不動交給系統的 `ShellExecuteA`。只包裝 shell32 的 `ShellExecuteA`:其他模組若也匯出同名函式,不再被誤當成真正的函式。
- **啟動器,命令列:** 以前用固定長度比對 `/autostart`、`/restart` 並讀固定位置,參數很短時(例如 `/a`、`/`、`/autostar`)可能讀超過結尾。
  現在旗標以完整的第一個字、不分大小寫、有邊界檢查的方式辨識;未知、過短、空白或很長的參數都不會再讀到命令列以外。
- **啟動器,以系統管理員重新啟動:** 啟動器以系統管理員身分重新啟動自己時,以前只傳程式路徑,`/noupdate` 和 `/autostart` 會遺失。
  現在會傳下原本給的旗標(放在其餘命令列之前,其餘部分保持原樣),並在 `%TEMP%\dynafix.log` 記錄這次提升權限的啟動。`/restart <pid>` 不傳,因為已經等過了。
- **啟動器,接上執行中的 DynaRun:** 以前把找到的第一個視窗的執行緒當成 DynaRun 的 GUI 執行緒,可能是另一個執行緒上的輔助視窗。
  現在檢查該行程所有最上層視窗,取可見的 Visual Basic 表單(`ThunderRT6Form*`)的執行緒,找不到才依序退回其他 `ThunderRT6*` 視窗與任何視窗;
  log 會記下採用的視窗類別(`gui-thread chosen from window class`)。
- **log 安全:** log 中出現很長的路徑或視窗文字(例如首次啟動設定程式的長路徑)時,可能寫出 `dynafix.dll` 256 位元組
  行緩衝區的範圍。現在每行最多 1000 個字元(超過的以 `...` 結尾),緩衝區也足以容納最長的格式化結果,一定以換行結束;其他把文字寫進 log 的地方也一併加上
  長度限制(非拉丁文字的視窗標題放不進緩衝區時,以前不會被正確結尾)。
- **log 寫入:** `dynafix.dll` 與 launcher 現在每行用單次原子附加寫入,並在每一行以及「超過 10 MB 時裁成最新 8 MB」
  的動作外面取同一個具名 mutex(`Local\DynaRunFix.dynafix.log`,最多等 2 秒),兩個行程不會再互相蓋掉、交錯或遺失對方的行,
  裁剪時也不會弄壞檔案。沒拿到鎖就不裁剪。
- **穩定性(內部,行為不變):** `dynafix.dll` 在 DynaRun 裡安裝的 hook 只會裝一次,即使 DLL 被重複進入也一樣;連線診斷用的一次性 hook
  以原子操作取得並發布,兩個執行緒不會再重複安裝或讀到只設了一半的指標;啟動器在每條路徑上都會關閉啟動用的事件與函式庫控制代碼;
  同一個視窗訊息不再被啟動器與 DLL 的 hook 各處理一次;log、內建圖片表與字型/線條對照表加上鎖保護;
  走訪匯入/匯出表前先檢查 PE 標頭並防範無效記憶體,損壞或非預期的模組不會再讓 DynaRun 當掉。
- **視窗子類別安全:** 表單銷毀時,只有在 dynafix 的視窗程序仍在最上層時才還原舊程序,且讀取時使用與安裝時相同的
  A/W 版本(Unicode 視窗以 A 版讀取會得到轉接碼,永遠不等於 dynafix 的程序,檢查就無法成立)。之後疊在 dynafix 上面的子類別不會被動到,
  鏈結保持完整。
- **`DYNAFIX_STATS=1`(診斷):** THBResize 防護的計數(表單 `WM_SIZE`、重複次數、`0x591` 放行/略過、表單顯示/隱藏),
  每 5 秒以 `stats(5s)` 行寫入 log,用來確認啟動後隱藏/顯示迴圈真的停了。
- **建置與測試:** `build.cmd` 在執行 Windows PowerShell 前先清空 `PSModulePath`(從 PowerShell 7 啟動建置時,下載 Locale Emulator
  那一步會出現「Get-FileHash is not recognized」)。新增測試:`tests\build-shexec-tests.cmd` 建置並執行首次啟動設定程式比對的單元測試;
  `tests\build-tests.cmd` 建置 log 測試程式(見 `tests\README.md`)。

### v1.3.5 — 2026-10-10

- **跑完後的看圖視窗沒填滿螢幕:** 「Dyna Pro測試後看圖程式」視窗最大化時,為了讓開工作列而縮小的同時也維持了長寬比
  (這原本只該用在沒有標題列的主儀表板),寬度少了約 76 px,右邊露出桌面,再按最大化也一樣(log:
  `clipped to the work area 1936x1216 -> 1860x1168`)。有標題列的表單現在只縮到工作區大小,寬度填滿。

### v1.3.4 — 2026-10-10

- **安裝程式找安裝檔:** 在「下載」、桌面、「文件」和安裝程式旁邊搜尋時,原本只要 zip 裡有 `.msi` 就拿來用(例如別家調校軟體的 zip),
  解開後才跳出「這不是 DynaRun V3 安裝檔」。Dyna Pro 的 zip 有密碼,所以現在只收有密碼的 zip
  (DynaRun 的 MSI 照舊)。安裝程式自己找到、但其實不是 DynaRun 的檔案會直接跳過,改試下一個或改從官網下載;錯誤頁只在你用
  「選擇檔案…」自己選的檔案不對時才出現,自己選的話任何 zip 都可以。

### v1.3.3 — 2026-10-09

- **工作列(真正的原因):** v1.3.0 起 `dynafix.dll` 在 DynaRun 啟動前就載入,而它在 `DllMain` 裡向還沒載入的 shell32 找
  `SetCurrentProcessExplicitAppUserModelID`,所以 AppUserModelID 從來沒設上。執行中的 DynaRun 因此不會合併到釘選的 DynaRun 圖示下,
  而是另外一個按鈕、顯示 exe 的表格圖示(v1.3.0 到 v1.3.2,不論捷徑怎麼設)。現在改在 dynafix 第一次在 DynaRun 裡被呼叫時設定
  (仍在第一個視窗之前),需要時先載入 shell32;log 會記 `AppUserModelID DynaRunFix.DynaRunV3 set: 00000000`。已在 Windows 11 驗證:
  從釘選的圖示或桌面捷徑開,DynaRun 就是那個釘選圖示、顯示 DynaRun 的 logo,沒有多出來的按鈕。

### v1.3.2 — 2026-10-09

- **工作列圖示:** `DynaRun V3.exe` 本身只有視窗用的表格圖示,藍色 logo 在 Dyna Pro 安裝程式建立的捷徑所用的圖示檔裡。
  安裝程式在沒有桌面捷徑時建立的捷徑用的是 exe 的圖示,而執行中的 DynaRun 會用帶有同一個 AppUserModelID 的捷徑圖示,
  所以 v1.3.1 之後工作列仍可能顯示表格。現在我們的捷徑若用的是 exe 的圖示,會改用 DynaRun 捷徑上的 logo。
  裝好 v1.3.2(或讓自動更新處理)後重開 DynaRun 即可。

### v1.3.1 — 2026-10-09

- **更新後的工作列圖示:** 從 v1.2.x 更新時,原本已經指向 DynaRunFix 的捷徑沒有補上 v1.3.0 給執行中 DynaRun 的 AppUserModelID,
  Windows 找不到對應的捷徑,執行中的 DynaRun 就顯示視窗圖示(表格圖案)而不是 DynaRun 原本的圖示,而且在釘選的圖示旁另外多一個按鈕。
  現在安裝程式會把我們的捷徑重新寫一次並加上 ID(名稱、圖示、快捷鍵、以系統管理員身分執行都保留)。在 v1.3.0 上再裝一次 v1.3.1
  (或讓自動更新處理),再開 DynaRun 即可。
- **文件:** README 與更新紀錄:DynaRun 開著時改解析度會變形是 DynaRun 本身的行為;說明首次開啟版面的修正。

### v1.3.0 — 2026-10-09

- **工作列:** 安裝程式會把 DynaRun 釘選到工作列(第一頁的勾選項,預設勾選;`/notaskbar` 可關閉)。Windows 7 到 10 直接釘選
  啟動器捷徑,Windows XP / Vista 放進「快速啟動」,Windows 11 不允許程式自行釘選,改用微軟的工作列配置原則
  (使用安裝程式本來就會出現的系統管理員確認;登出再登入後才會出現圖示;組織已設定配置原則時不會動它,最後一頁會說明如何手動釘選)。
  執行中的 DynaRun 會歸在釘選的圖示下(啟動器捷徑和 DynaRun 使用相同的明確 AppUserModelID)。
- **開機自動啟動:** 第二個勾選項,預設勾選(`/noautostart` 可關閉):在目前使用者的 `Run` 登錄值經由啟動器啟動 DynaRun。
  解除安裝時會移除(其他帳號也一併處理)。
- **登入啟動後的畫面:** 登入時的自動啟動(`DynaRunFix.exe /autostart`)會等到工作列出現、螢幕大小與工作區連續 3 秒沒變
  (最多 60 秒)才啟動 DynaRun。如果 DynaRun 的畫面顯示不正常,關掉 DynaRun 再開一次即可
  (安裝程式最後一頁和 README 都有說明)。
- **安裝後第一次開的畫面:** 裝好 DynaRunFix 後第一次開 DynaRun(安裝程式的「開始使用 DynaRun」,或第一次登入自動啟動)
  可能會照整個螢幕高度排版:右邊儀表被切掉、版面偏移,再開一次就正常。原因是啟動器先讓 DynaRun 開始跑才載入
  `dynafix.dll`,而剛安裝的 dll 第一次載入很慢(全新 Windows 11 上要好幾秒),DynaRun 在修正掛上前就建立了最初的視窗
  (v1.2.2 起各版都有)。現在啟動器在 DynaRun 啟動前就先載入 `dynafix.dll`。log 會記 `dynafix.dll loaded in … ms` 與
  `windows DynaRun had before dynafix: N`(0 = 來得及)。已在全新繁中 Windows 11 虛擬機驗證。
- **已知問題(DynaRun 本身的行為):** DynaRun 開著時改變螢幕解析度,主畫面仍照舊的大小排版(DynaRun 只在啟動時排一次固定的 4:3 版面);重開 DynaRun 即可。
- **更新:** 每天一次，開啟 DynaRun（而且它還沒在執行）時，安裝好的安裝程式會向 GitHub
  （`api.github.com/repos/timliudev/DynaRunFix/releases/latest`）查詢有沒有新版本，不會送出其他資料。啟動器最多等 5 秒，
  有新版就在 DynaRun 開啟前問「現在更新嗎？」（回應較慢時下次開啟再問；過程記錄在 `%TEMP%\dynafix.log`）。按「是」只會從本專案下載該版本的 `DynaRunFix-Setup.exe`，
  用 GitHub 列出的 SHA-256 核對後安裝（一次系統管理員確認），然後開啟 DynaRun；工作列釘選、開機自動啟動和桌面捷徑維持原樣（`/keep`）。
  按「否」、取消確認或更新失敗，隔天會再問。沒有網路或 Windows XP（不支援 TLS 1.2）時什麼都不做。這一版之前的版本不會檢查，需要手動安裝一次。
- **Log:** `%TEMP%\dynafix.log` 每行開頭都有日期和時間(`YYYY-MM-DD HH:MM:SS.mmm`);啟動器會記錄自己是怎麼被啟動的(參數、`/autostart`、`/restart` 或一般啟動、父行程、啟動旗標與顯示方式、工作資料夾、DynaRunFix 版本);每個 DynaRun 行程另有一行標頭:DynaRunFix 版本與 commit、`DynaRun V3.exe` 的版本、Windows 版本與更新修訂號。log 保持在 10 MB 以下,最舊的行先被丟掉(留下最新的 8 MB,每次啟動時檢查)。
- **開發版**在安裝程式標題、「程式和功能」與 log 顯示短 commit hash(有未提交變更時加 `-dirty`)而不是 `dev`;仍然不會檢查更新。
- `/quiet` 預設套用這兩項,可用 `/notaskbar`、`/noautostart` 關閉。第一頁和視窗稍微變高;最後一頁會說明設定了什麼。

### v1.2.3 — 2026-10-07

- **移除 `tools/msi-novbs`:** DynaRunFix 不再提供任何修改 Dyna Pro 安裝檔的工具。Windows 停用 VBScript 時,
  請到「選用功能」重新啟用,或使用 `DynaRunFix-Setup.exe`(它執行 Dyna Pro 安裝檔時不顯示用到 VBScript 的精靈頁面)。
- **授權:** 安裝程式與 zip 附上 miniz 的授權(`LICENSE-miniz.txt`);安裝程式用 miniz 解開 Dyna Pro 的安裝檔。
- **文件:** 首次啟動說明只描述從 DynaRun 外部觀察得到的行為;README 提醒使用者自行確認與 Dyna Pro 的授權條款。

### v1.2.2 — 2026-10-07

- **首次啟動:** 保留第一次啟動時選的語言,以及系統選擇畫面勾選的選購功能(大氣監測、水冷、冷卻風扇、空燃比分析儀);
  以前會被 Dyna Pro 的設定程式改回英文、關閉。([說明](docs/MANUAL.md#首次設定))
- **字型:** DynaRun 所有文字改用 Windows 該語言的介面字型(繁中:Microsoft JhengHei UI;英文 Windows:Segoe UI),
  不再混用 Arial、MS Sans Serif、新細明體。([說明](docs/MANUAL.md#字型全部使用-windows-介面字型))
- **亂碼:** 以英文安裝、之後才改成繁中地區的 Windows(FontAssoc 缺 `ANSI(00)=YES`)也改用 Locale Emulator 啟動 DynaRun,
  所有視窗(含工具提示)都是中文。([說明](docs/MANUAL.md#系統地區是繁體中文仍然亂碼))
- **狀態列:** 工作列不再遮住 DynaRun 底部的狀態列,DynaRun 依螢幕的工作區排版。([說明](docs/MANUAL.md#狀態列被工作列遮住))
- **啟動器:** 在 DynaRun 第一個視窗出現前就掛上修正,啟動畫面和最先載入的表單也涵蓋在內。
- **安裝程式:**
  - DynaRun 執行中:改為「關閉 DynaRun 並安裝」,不再在執行中的 DynaRun 底下換檔案;`/quiet` 以代碼 6 結束,`/quiet /close` 先關閉 DynaRun。
  - DynaRun 安裝程式和權限提示執行時,精靈保持在檔案總管前面。
  - 從網路磁碟機執行也能安裝。
  - 新圖示。
- **工具(v1.2.3 已移除):** `tools/msi-novbs` 讓 Dyna Pro 的
  `Setup.msi` 不需要 VBScript 也能安裝(微軟預計約 2027 年預設停用 VBScript)。`DynaRunFix-Setup.exe` 不需要它:
  它執行該安裝檔時本來就不顯示用到 VBScript 的精靈頁面。

### v1.2.1 — 2026-10-07

- 安裝程式:下載時顯示 Dyna Pro 下載頁、顯示實際下載大小,改用 UpgradeCode 而非檔名辨識 DynaRun 安裝檔。

### v1.2.0 — 2026-10-07

- 一鍵安裝:沒有 DynaRun V3 時自動下載安裝(密碼與授權來自 Dyna Pro)。
- 首次啟動選擇系統後不再卡住。
- Windows UTF-8 選項:字碼頁 manifest,繁中系統地區再加上 Locale Emulator。

### v1.1.0 — 2026-10-07

- `DynaRunFix-Setup.exe`:修正的一鍵安裝程式(捷徑、系統層級 ActiveX 註冊、解除安裝)。

### v1.0.0 — 2026-10-07

- 修正 DynaRun V3 在 Windows 10/11 主畫面閃爍。
- 存在 OneDrive(「永遠保留在此裝置」)的 .Dpr 檔可以正常開啟。
