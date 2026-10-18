# DynaRunFix manual

The full manual of [DynaRunFix](../README.md): what it changes, installing and uninstalling, command-line options, the Windows 10/11 problems it solves, what it verifies, building and licences. The short overview is in the [README](../README.md); the Dyna Pro USB driver has its own guide, [DRIVER.md](DRIVER.md).

[繁體中文在下方](#繁體中文)

> This project is not affiliated with or endorsed by Dyna Pro Dynamometers Ltd. *THBResize* (`THBRes25.dll`),
> the resize control DynaRun uses, is a product of THB Componentware; DynaRunFix is not affiliated with THB
> Componentware either. DynaRunFix does not contain or redistribute any of their files, it modifies the content of no Dyna Pro or
> THB program file (see "To be plain" below), and it does not bypass, disable or alter DynaRun's licence activation or copy protection.
>
> To be plain about what it does change: DynaRunFix **changes DynaRun's behaviour while it runs**, in memory, inside
> the DynaRun process only. The content of no Dyna Pro or THB file is modified. In detail, it
>
> - redirects import-table entries of modules loaded in that process: `PostMessageA` in `THBRes25.dll`; the
>   file-attribute APIs, `GetProcAddress` and the text APIs imported by Microsoft's `MSVBVM60.DLL` and `scrrun.dll`; the font
>   APIs in `DynaRun V3.exe`, the other modules in DynaRun's folder and its OCX controls; the font-related imports of
>   `THBRes25.dll`, `PEGRP32E.dll`, `MSVBVM60.DLL`, `ole32`, `oleaut32`, `MFC42`, `comctl32` and `dynapro.dll` (delay-load imports
>   included); `CreateFileA` in `dynapro.dll` (to observe the device open; results are passed on unchanged) and, only with the
>   opt-in USB switch, `WaitForSingleObject` in `dynapro.dll` for the `DPSetData` watchdog (below; it acts on the watchdog's helper thread only: once a
>   call is abandoned, that thread's infinite waits on an event return after 250 ms slices, every other call is passed on unchanged); and `LoadLibraryExA` / `LoadLibraryExW` in the graph control's own modules `Pesgo32e.ocx` and
>   `PEGRP32E.dll` (Gigasoft ProEssentials), only for the request for its language resource `PEGRC32D.dll`, see
>   [Graph menu in the wrong language](#graph-popup-menu-stays-english-without-administrator-rights);
> - replaces the results of DynaRun's `GetProcAddress` lookups of `ShellExecuteA` and `DPConnect` with its own wrappers, and,
>   only with the opt-in USB switch, of `DPSetData`;
> - subclasses DynaRun's main form (`WM_WINDOWPOSCHANGING`, `WM_GETMINMAXINFO`, and it sends `WM_SETFONT` again), installs two
>   window-message hooks on DynaRun's GUI thread (`WH_CALLWNDPROC`, `WH_CALLWNDPROCRET`) and stores a few window properties
>   (`SetProp`, for example the previous `WM_SIZE`) on DynaRun's own windows;
> - adjusts the VB6 runtime's in-memory screen size values: it locates them by an instruction-byte pattern in Microsoft's MSVBVM60
>   code (in memory only), changes them only if they equal the real screen size, and leaves them alone if the pattern is not found (see [Status bar hidden behind the taskbar](#status-bar-hidden-behind-the-taskbar));
> - **writes DynaRun's own settings** under `HKCU\Software\DynaPro` (`Default_Language` and `System_Setup`: the user's own
>   choices on the first start, see [First-time setup](#first-time-setup)), and `%TEMP%\dynafix.log`.
>
> The installer points DynaRun's shortcuts at the launcher (the original shortcut files are backed up) and, only when
> Windows' UTF-8 option is on, adds a manifest file next to `DynaRun V3.exe` (see [Usage](#usage); for that it briefly changes the modification time of that exe and puts it back, the exe's content is never changed). DynaRunFix never runs
> DynaRun with more rights than a normal start from your desktop (with UAC off, that normal start is already full administrator), or does not start it (see [Usage](#usage)). DynaRunFix does not touch any licensing,
> activation or copy-protection mechanism or component.
>
> Only if you tick the driver option of the setup wizard (or run `DynaRunFix-Setup.exe /driver`; `/quiet` never does), DynaRunFix also
> changes the system while the Dyna Pro USB driver is installed: it saves the driver it downloaded from dynapro.co.uk in
> `%ProgramData%\DynaRunFix`, suspends BitLocker (only when protection is on or its status cannot be read), makes Windows show its advanced start-up menu once at the
> next start, registers a scheduled task that continues the wizard when you sign in and restarts the computer; after you pressed 7
> in that menu and answered Windows' red warning, it installs the driver. Driver signature checking is off for that one start only. The
> task and the boot option are removed and BitLocker is turned on again when the installation ends or is cancelled. Without that option
> none of this happens. Details: [Usage](#usage) and [docs/DRIVER.md](DRIVER.md).
>
> You need your own legally installed copy of DynaRun V3, and it is up to you to make sure that using this
> fix is consistent with your licence agreement with Dyna Pro. DynaRun V3 is licensed to you by Dyna Pro
> under their own terms; the MIT licence of this project covers only DynaRunFix.

## Symptom

On Windows 10/11 the DynaRun V3 main dashboard flashes continuously (the whole window disappears and
reappears several times per second). The same installation is stable on Windows XP and Windows 7.
DPI settings, compatibility modes and whether the USB dyno is connected make no difference.

## Cause (as observed)

DynaRun V3 is a Visual Basic 6 application; its forms are scaled by the third-party *THBResize 2.5* control
(`THBRes25.dll`). Logging the window messages of DynaRun's main form from outside shows:

1. After each `WM_SIZE` of the form, a private message `0x591` is posted to it.
2. Each `0x591` is followed by the same sequence of windows: the main form is hidden, a blank form appears,
   the main form is shown maximized again and the blank form is closed.
3. On **Windows 7/XP** showing the hidden, already-maximized form again produces no `WM_SIZE` when its size is
   unchanged, so the sequence runs once and stops.
4. On **Windows 10/11** the same step produces another `WM_SIZE (SIZE_MAXIMIZED)` with the unchanged size (the
   window-position change carries the "state changed" flag), which is followed by another `0x591` → a repeating
   hide/show loop.

Message logs and the comparison between Windows versions: [docs/ROOT_CAUSE.md](ROOT_CAUSE.md).

## The fix

`dynafix.dll` is loaded into the running DynaRun process by the `DynaRunFix.exe` launcher. Inside that
process only, it redirects THBRes25.dll's import of `PostMessageA` and drops the `0x591` message when it
was triggered by a `WM_SIZE` that is identical to the previous one for the same form. That restores the
Windows 7 behaviour; genuine size changes still go through. No file is changed; the dll writes a
small log, `%TEMP%\dynafix.log` (every line starts with the date and time; a line is at most 1000 characters, longer ones end in `...`;
when the dll or the launcher starts and the file is above 10 MB, it is cut to its newest 8 MB, from a full line on; it is not trimmed
while DynaRun runs, so within one session it can grow beyond 10 MB; the launcher and the dll write to it in turn, so their lines are interleaved in time order) and,
whenever DynaRun starts its first-time setup helper (normally only on the very first start), DynaRun's own language and feature settings into DynaRun's registry key `HKCU\Software\DynaPro` (see below). The log names modules and APIs but no module or import addresses (it can show window and GDI handle values), and can contain file paths and window titles: look through it before you post it anywhere. The same dll also fixes the [first-time setup](#first-time-setup) and
[OneDrive files that will not open](#some-dpr-files-will-not-open-empty-file-run-properties-no-curves).

## What's new

v1.8.0 (released 2026-10-18):
- New: a version check of Dyna Pro's setup. 3.26.0 (the tested version) installs as before; another 3.x, or a version that cannot be read, shows a notice (tested only with 3.26.0, report problems at [GitHub issues](https://github.com/timliudev/DynaRunFix/issues)) and the install goes on; **DynaRun 4 or later** gets its own page that explains it may not need the fix and offers *Install original software only* (Dyna Pro's setup alone: no fix, no driver) next to *Install the fix anyway*.
- Changed: the *Check the installer* page shows the source, the MSI's version and manufacturer first, in a smaller font, and fits without scrolling; for a setup found on this computer, *Download it* takes the file from Dyna Pro's website instead.
- Changed: **the first start no longer asks for permission** (no UAC prompt) for Dyna Pro's configuration helper `Setup_<n>.exe`: it only writes `HKCU\Software\DynaPro`, so it is started without elevation.
- Changed (setup wizard): while Dyna Pro's setup installs, the wizard stays directly behind msiexec's progress window (not always on top); after the restart the driver pages that wait for a click are kept on top of start-up windows (not while the driver installs, so Windows' warning stays visible); when a long step ends and the wizard is not in front, its taskbar button flashes and Windows shows a notification.

v1.7.2 (released 2026-10-18):
- Fix: a very long `%APPDATA%` path (about 200 characters or more) no longer overflows a buffer in the shortcut and taskbar pin code; the folder that does not fit is skipped.
- Fix: the setup wizard ignores a "job finished" window message that its own worker thread did not send (it could crash the wizard). Also: "Close DynaRun and install" applies to one install only, and a failed driver preparation removes the staged driver files. The rest is an internal source reorganisation without intended changes; see the [changelog](../CHANGELOG.md).

v1.7.1 (released 2026-10-17):
- Fix: a full install with the driver now asks for administrator permission only once. Before, on an administrator account it asked twice (once for the fix, once to prepare the driver), although the documentation said once. The driver pages (download and check, the restart picture) now come before the install; *Install and restart* then installs the fix and prepares the driver in one step, and *Skip* installs the fix without the driver. After the restart an administrator account is not asked again; a standard user still is (administrator password).

v1.7.0 (released 2026-10-17):
- New: a hint on the first page when Dyna Pro's software page publishes a newer DynaRun V3 than the installed one, with *Download new version* (nothing is automatic). For the driver, *Choose file...* also takes the ftdibus.inf of an unpacked folder.
- New: when DynaRun and the fix are installed and only the driver is missing, the first page offers just the driver.
- Changed: DynaRun's setup and the driver share one download flow and one error page, and the buttons are the same on every page.
- Fix: *Download new version* really downloads; a damaged DynaRun package found on the computer is not offered again; a file you chose is no longer deleted when the wizard is closed without a network; the update hint and a download no longer disturb each other; over-long names and paths no longer risk overrunning buffers; a failed BitLocker resume is remembered and retried; cancelling the warning for an automatically found driver copy skips only such copies, a file you chose is always checked.

v1.6.0 (released 2026-10-17):
- New: the setup wizard checks the driver before using it. It looks for a copy on this computer first (its own cache, the setup's folder, Downloads, Desktop, Documents; a zip or an unpacked folder) and downloads only when none matches. The 18 driver files are compared with a table of SHA-256 values; when they differ, a warning page lists them and *Install anyway* is offered (not the default). See [docs/DRIVER.md](DRIVER.md).
- New: the license page is two pages (a *Check the installer* page with the details of the setup file, then the license). Downloads retry by themselves, can be paused and continued, and show their progress; the driver's last page has *Start DynaRun*.
- Changed: the driver files are unpacked safely (only the 18 known files, into a folder only administrators can change), BitLocker is also suspended when its status cannot be read and the suspension is verified, the restart-menu picture marks the line to press, and auto-start waits until the driver step has ended.
- Fix: the driver warning page always shows the advice to click *Cancel* if in doubt, and the list of differing files or its start; the progress of a second driver download is shown; cancelling the warning for a copy found on this computer does not bring the same warning back.

All versions: [CHANGELOG.md](../CHANGELOG.md).

## Usage

1. Download **`DynaRunFix-Setup.exe`** from [Releases](https://github.com/timliudev/DynaRunFix/releases)
   and double-click it. Windows SmartScreen may say *"Windows protected your PC"* because the file is not
   code-signed: click **More info → Run anyway**.

   | 1. Click *More info* | 2. Click *Run anyway* |
   |---|---|
   | ![SmartScreen: Windows protected your PC](img/smartscreen-1.png) | ![SmartScreen with the Run anyway button](img/smartscreen-2.png) |

2. Follow the window. If DynaRun V3 is not installed yet, it downloads the DynaRun V3 setup from
   [Dyna Pro's website](https://dynapro.co.uk/Software_Release.htm) (or uses a
   DynaRun setup zip or `Setup.msi` already in *Downloads*, on the desktop, in *Documents* or next to the
   installer), asks for the **setup password you got
   from Dyna Pro**, shows a *Check the installer* page (a box you can copy from, in a smaller font so that all of it fits without scrolling: first where the file came from, the MSI's version and manufacturer, then the file's full path, size and SHA-256; for a setup found on this computer the page also has *Download it*, which gets the file from Dyna Pro's website instead), then Dyna Pro's license (read from the setup and shown by DynaRunFix itself) and installs DynaRun V3 and the fix only after you accept it (if the
   license cannot be read from the setup, you must confirm that you accept Dyna Pro's license terms instead).
   If DynaRun V3 is already installed, it only installs the fix. If DynaRun V3 is installed in a folder the setup does not find, the first page has *Installed elsewhere...* to point it at `DynaRun V3.exe`. Windows asks once for permission: click **Yes**. That one prompt is for the whole install; when the driver is ticked too it covers the driver's preparation as well (the driver pages come before the install, see below).
   **Version check.** The MSI's ProductVersion is classified in one place (`dynarun_ver_class` in `src\package.c`; the UpgradeCode and product name check stays). Exactly 3.26.0, the version the fix was tested with: nothing changes.
   Another 3.x (or older): a notice on the *Check the installer* and license pages says it was tested only with 3.26.0, that there may be additional problems, and where to report them (<https://github.com/timliudev/DynaRunFix/issues>); the install goes on.
   A version that cannot be read: a notice too, the install goes on. Version 4 or later (possibly no longer a Visual Basic 6 program, perhaps native Windows 10/11, so it may not need the fix): after *Check the installer* a page says so and
   suggests installing Dyna Pro's software alone first and trying it. *Install the fix anyway* (right-most) stays disabled until *I understand that DynaRunFix has not been tested with this version* is ticked. *Install original software only*
   goes on to the license page and then installs Dyna Pro's setup alone (one administrator prompt, the same checked copy of the MSI): no fix, no change of shortcuts, no taskbar pin, no start at sign-in, and **no USB driver**
   (the driver step belongs to the fix: it exists for V3's unsigned FTDI driver and was never checked with V4; the driver box of the first page is ignored). If V4 then shows the old problems, run this setup again: it finds the installed DynaRun and offers the fix.
   **Windows and notifications.** While the elevated install runs, the wizard keeps itself directly behind the window of the install that has the foreground (the UAC prompt, Dyna Pro's progress window "Please wait while Windows configures ...", the driver warning), above all other windows,
   but never always-on-top: you can switch to another program, and a program you switched to is left alone. When a long step ends (download, extraction, install, driver steps) or the wizard waits for you after the restart, and it is not the window in front, its taskbar button flashes
   and Windows shows a notification (a temporary tray icon; it goes after 10 seconds or when you return). After the restart the driver pages that wait for your click (*Last step*, *7 was not pressed*) are kept on top of the programs that start with Windows; they stop being on top when you click *Install*, so that Windows' red warning is not hidden.
   A download that is interrupted is retried by itself (after 1, 2, 4, 8 and 15 seconds; the count starts over whenever data arrived), the page shows
   the first error of the failed attempts, the *Pause* button stops it cleanly and *Resume* (or, after an error, *Continue download*) carries on from
   the part that is already there; *Open website* and *Choose file...* remain as ways around it. When Windows says the computer is not connected to a
   network at all the error page comes at once (no waiting through retries); a host that does not answer is given up after 10 seconds. The driver goes through
   the same pages (a copy on the computer is looked for first, then the download); its *Choose file...* takes the zip, or ftdibus.inf in the folder where it is unpacked.
   The first page of the wizard has up to three check boxes: pin DynaRun to the taskbar, start DynaRun when Windows starts and, on
   Windows 10/11 when the driver is not installed yet, **Also install the Dyna Pro USB driver (one restart needed)** (see below).
   If DynaRun V3 and this version of the fix are already installed and only the driver is missing, the first page offers just the driver (and
   *Reinstall the fix...*). When DynaRun V3 is installed and the computer is online, the first page also asks Dyna Pro's software page in the background
   whether a newer DynaRun V3 is published (the page names the version next to the download link: 3.26.0 today; if it ever stops naming one, the zip on
   the site is asked for its first byte, nothing more is downloaded, and its ETag / length / date are compared with those of the last completed download) and, if so, shows a line and *Download new version*. It is only a hint: nothing is downloaded or
   installed unless you click it, a failed check says nothing, and each result is one line in the setup's log (`%TEMP%\DynaRunFix-setup.log`; `setup.log` in the install folder when run as administrator).
3. Start DynaRun with its usual **DynaRun V3** icon. On the very first start (system selection) pick your model and click OK; DynaRun then restarts by itself.
   Windows no longer asks for permission here (before v1.8.0 it asked once for Dyna Pro's configuration helper).

   First time on this PC? The Dyna Pro USB driver is needed too. On Windows 10/11 the setup wizard can install it (the check box on its first
   page; it downloads the driver, restarts the computer once, you press **7** in the menu that appears and choose *Install this driver
   software anyway* twice, and it continues by itself after you sign in), later also with the Start menu shortcut *Install Dyna Pro USB
   driver* or `DynaRunFix-Setup.exe /driver`. Or install it by hand: see [docs/DRIVER.md](DRIVER.md).

| | | |
|---|---|---|
| ![1. Start](img/setup-start.png) | ![2. Download from Dyna Pro](img/setup-download.png) | ![3. Setup password](img/setup-password.png) |
| 4. Dyna Pro license page (shown by DynaRunFix itself, with the license text read from the setup; not reproduced here; the *Check the installer* page with the file's details comes just before it) | ![5. Installing](img/setup-installing.png) | ![6. Done](img/setup-done.png) |

*Fresh Windows 11 (Traditional Chinese), DynaRunFix 1.3.2.*

Nothing of Dyna Pro's is included in DynaRunFix: the setup comes from Dyna Pro's site and the password from
Dyna Pro. The DynaRun setup runs with basic UI (`msiexec /qb!`), so Dyna Pro's own wizard pages, including its license page, are skipped (DynaRunFix shows the license itself). The password is used only to open the zip and is not stored. Before the elevated install, the setup is copied to a folder
that only administrators can write to and checked again there; DynaRun is installed from that copy.
The setup is recognised by its content, not its file name: only an MSI that identifies itself as DynaRun V3 (upgrade code and product name) is ever installed; for the zip this is
checked after the password has opened it. When looking in *Downloads* and the other folders by itself, the
installer only takes a DynaRun MSI or a password-protected zip with an MSI inside (Dyna Pro's zip has a password);
a file it found there that turns out not to be DynaRun's is skipped without a message and the next one, or the
download, is used. Another zip can still be chosen with *Choose file...*; a file you chose that is not the DynaRun
setup is refused with a clear message.

What the installer does for the fix:
- installs `DynaRunFix.exe`, `dynafix.dll`, `LICENSE.txt`, `LICENSE-miniz.txt`, the licence files `le\COPYING`, `le\COPYING.LESSER`
  and `le\THIRD-PARTY.txt` (see [Third-party licences](#third-party-licences)) and a copy of `DynaRunFix-Setup.exe` (the
  uninstaller) to `Program Files (x86)\DynaRunFix` on 64-bit Windows (`Program Files\DynaRunFix` on 32-bit Windows);
- points the existing DynaRun V3 shortcuts (desktop, Start menu, pinned taskbar, all users and current
  user) to the launcher, keeping their name and icon (the *Run as administrator* flag is not carried over: DynaRun is never started with more rights
  than a normal start from your desktop (with UAC off, that normal start is already full administrator); the original shortcut files are backed up); creates a desktop
  shortcut if there is none. Shortcuts of ours get DynaRun's blue logo (from the icon file of Dyna Pro's own
  shortcuts; `DynaRun V3.exe` itself only has the chart icon of its windows), so the running DynaRun shows that logo
  on the taskbar;
- only if Windows' *UTF-8 for worldwide language support* option is on: adds the
  [code-page manifest](#garbled-chinese-or-other-dbcs-text-with-windows-utf-8-option) next to `DynaRun V3.exe` (the exe's
  modification time is briefly changed and restored so that Windows reads the manifest, and the installer briefly creates the
  `DynaRun V3.exe` process suspended and terminates it at once so that Windows reads the manifest; no DynaRun code runs and the exe's content is not changed).
  Not done when DynaRun's folder or exe can be changed by ordinary users, or when its path is not a plain local one: then no manifest is written and DynaRun's path is not recorded
  in the machine-wide registry for the elevated steps (uninstall) to use; `setup.log` in the install folder says so. On Windows XP links (junctions) in that path cannot be detected, only the permission checks apply;
- puts [Locale Emulator](#garbled-chinese-or-other-dbcs-text-with-windows-utf-8-option) in `DynaRunFix\le`
  (used only on a Traditional Chinese system with the UTF-8 option on, or whose FontAssoc lacks `ANSI(00)=YES`);
- on Windows 10/11, adds the Start menu shortcut *Install Dyna Pro USB driver* (all users; it runs `DynaRunFix-Setup.exe /driver`);
- registers an uninstaller in *Programs and Features* / *Installed apps*.

What DynaRunFix keeps on the machine besides the files above (uninstall removes these where it can; the files `com-copied.txt`, `com-backup`, `com-skipped.txt` and `setup.log` stay in the install folder, which then is not deleted):
- `HKLM\SOFTWARE\DynaRunFix`: `InstallDir`; `DynaRunExe` (DynaRun's path, only when its folder and exe cannot be changed by ordinary users); `ManifestInstalled` (the manifest was added); `ExeOrigTimePath` / `ExeOrigTime` (only while the original modification time of `DynaRun V3.exe` still has to be restored); `TaskbarPolicy` / `TaskbarPolicySid` (the Windows 11 pin); and the subkey `Shortcuts` (backup of the original machine-wide shortcut files), besides the entry under `Uninstall`.
- `HKCU\Software\DynaRunFix` of each user who ran DynaRun through the fix: `Shortcuts` (backup of that user's original shortcut files); `UpdateChecked`, `UpdateVersion`, `UpdateSha256`, `UpdateSkip`, `UpdateAsk`, `UpdateETag`, `DownloadETag`, `DriverETag` (the update check and the downloads); `AutostartPending` (the sign-in start that waits for the end of the driver step); `DownloadETagDone`, `DriverETagDone`, `UpdateETagDone` (what the last completed downloads were; the "newer DynaRun?" hint compares with the first); `NoticeElevatedLauncher`, `NoticeRunAsAdminLayer` (written only when you answer *No* to showing a one-time notice again).
- On Windows 11 with the pin option: `StartLayoutFile` and `LockedStartLayout` = 1 under `HKEY_USERS\<user>\Software\Policies\Microsoft\Windows\Explorer` and `TaskbarLayout.xml` in the install folder. `LockedStartLayout` is the value Microsoft documents together with `StartLayoutFile`: while both are set, Windows treats the layout as locked. They are removed on uninstall (or when you install again without the pin option) only if `StartLayoutFile` still names our file; an organization's own layout is never touched.
- In the install folder besides the program files: `TaskbarLayout.xml` and `DynaRun V3.lnk` (the shortcut the pin points at; with the pin option), and `setup.log` (what the administrator step did or skipped).
- Whenever the wizard runs: the folder `%TEMP%\DynaRunFix\` (the downloads `*.zip` / `.part`, the unpacked `Setup.msi`, and, while a driver zip is looked into, the scratch folder `drvcheck` and `drvdigest.tmp`; removed when the step ends) and `%TEMP%\DynaRunFix-setup.log` (time-stamped lines of the file searches, the update hint and your choices, written by the wizard without administrator rights).
- Only if the driver option was used: the folder `%ProgramData%\DynaRunFix` (changeable by administrators only) with a copy of the setup, the staged driver files in `driver\` (only the 18 files that get installed), `driver.log`, `bitlocker-before.txt` (secrets masked) and, only while an "Install anyway" is in force, `accepted.txt` (the SHA-256 of exactly the files accepted; deleted when the driver step ends); the value `DriverBitLockerSuspended` = 1 under `HKLM\SOFTWARE\DynaRunFix` (only while BitLocker is suspended by the driver option); the scheduled task `DynaRunFix-DriverContinue` and the one-time boot option `onetimeadvancedoptions` (both only until the installation ends or is cancelled); and the BitLocker suspension itself (only if protection was on; see the driver option below). The driver, once installed, is part of Windows' driver store.

Uninstalling restores the original shortcut files and removes the manifest it added only if the file is identical to the installer's own copy (a manifest that differs is left alone); if
restoring the exe's modification time fails, the record of the original time is kept and the restore is tried again at the next run; the uninstall runs from the installed program (the install folder is administrators-only; no copy is made in `%TEMP%`), and the install folder with its setup file is removed by Windows at the next restart if it is still in use; other accounts'
shortcuts that still start the fix are pointed back at `DynaRun V3.exe`. It asks whether to remove the fix only or
DynaRun V3 too (*Remove all* then runs Dyna Pro's own uninstaller of DynaRun V3). It asks for administrator permission first;
if you cancel that prompt, nothing is changed. If DynaRun is running, it asks to close it first. It also removes the driver Start menu shortcut, a driver installation that is still pending (the scheduled task and the one-time boot option) and `%ProgramData%\DynaRunFix` (so `driver.log` goes too). If DynaRunFix suspended BitLocker for the driver installation and it is still suspended, uninstalling resumes it (also when no task is waiting any more, and also with `/keep`). An installed driver stays. Older versions copied DynaRun's COM
registrations to the machine-wide registry; uninstalling keeps those copies and their records (`com-copied.txt`, `com-backup`
in the install folder). DynaRun's exe and program content stay unchanged (the original shortcut files are backed up and restored; the only file added is the manifest, and only with the UTF-8 option on), and your data files are never changed.

Windows XP, 7, 10 and 11 are supported (the driver option needs Windows 10 or 11). Command-line options:
- `/quiet`: no window; installs only the fix and needs DynaRun V3 installed (exit code 1 if it is not found). It never installs the driver.
- `/close`: with `/quiet`, closes a running DynaRun first (see below).
- `/uninstall`: uninstalls (also what *Programs and Features* runs); with `/quiet` it asks nothing and removes only the fix (DynaRun V3 stays), a running DynaRun is closed only if `/close` is also given, and files that are in use are removed at the next restart; `/notaskbar`, `/noautostart`: leave out the taskbar pin and the start with Windows (below); `/keep`: leave the pin, the start at sign-in and the desktop shortcut as they are (what an update uses).
- `/driver`: opens the driver pages of the wizard directly (download, restart, install), without installing DynaRun or the fix; this is what the Start menu shortcut runs.
- `/driver-cancel`: takes back a started driver installation (the scheduled task, the one-time boot option and the BitLocker suspension); it installs and removes no driver. It must be run from an administrator command prompt: otherwise it exits with code 1 and does nothing.
- Internal (do not run them by hand): `/driver-continue` (what the scheduled task `DynaRunFix-DriverContinue` starts after the restart), `/driver-prep`, `/driver-install`, `/machine`, `/full`, `/msi-only`, `/unmachine`, `/checkupdate`, `/update`, `/offer`.

Exit codes of the setup:

| Code | Meaning |
|---|---|
| 0 | Done: the install, update or uninstall finished, or the wizard ended on a success page (for `/driver` too: the driver is installed, was already installed, or it is ready for the restart that continues the installation). |
| 1 | Error, for example: `/quiet` and DynaRun V3 not found, the user account could not be read, the install folder could not be written, another setup step is already running, or the window could not be created. Also: the wizard ended on an error page (the download failed, the setup could not be installed, or, for `/driver`, the driver was not installed or Windows is too old for the automatic installation). The message (not shown with `/quiet`) and `setup.log` say what. |
| 2 | Cancelled: the wizard was closed before it reached a success or an error page, the administrator prompt was declined, DynaRun's own setup was cancelled, or the uninstall question was answered *No*. |
| 3 | Only inside the elevated stage: the administrator prompt was answered with another account than the one the install is for. The setup handles this itself (it runs DynaRun's setup as the starting user), so it is not normally seen. |
| 4 | DynaRun V3's own setup (or its removal) failed, or its file could not be prepared (copied and checked) for the install. |
| 5 | DynaRun V3 was installed, but `DynaRun V3.exe` was not found afterwards. |
| 6 | DynaRun (or the launcher / Locale Emulator of the install folder) is running: nothing was changed (see below). |

The driver steps report problems in the wizard and in `driver.log`.

Up to three options on the first page of the wizard, all on by default (`/quiet` applies the first two unless switched off with
`/notaskbar` / `/noautostart`, and never the third):
- **Pin DynaRun to the taskbar.** Windows 7 to 10 pin the launcher shortcut directly; Windows XP / Vista add it to
  Quick Launch. Windows 11 lets no program pin itself, so the installer uses Microsoft's
  [taskbar layout policy](https://learn.microsoft.com/windows/configuration/taskbar/pinned-apps) for the user
  (`TaskbarLayout.xml` in the install folder; `StartLayoutFile` / `LockedStartLayout` under
  `HKEY_USERS\<user>\Software\Policies\Microsoft\Windows\Explorer`, set by the administrator step): the icon
  appears after the next sign-out / sign-in. If an organization has already set a layout file it is left alone,
  and the last page tells how to pin by hand (right-click the desktop icon → *Show more options* → *Pin to taskbar*). Without the driver option this is on the last page (*Done*); with it, the install goes straight on to the restart and the same hint is on the page before the restart.
  A running DynaRun groups under the pinned icon (the launcher shortcuts and DynaRun share an AppUserModelID). The pinning
  technique for Windows 7 to 10 is the one described by Gee Law ([blog post](https://geelaw.blog/entries/msedge-pins/)); Firefox uses the same approach; no Mozilla code is included.
- **Start DynaRun when Windows starts:** the value `DynaRunFix` under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
  starts the launcher (`DynaRunFix.exe /autostart`) at sign-in. Installing again with the box cleared removes it; uninstall removes the Run value for every account, the taskbar policy values of the account that runs the uninstall, of the account the install recorded and of every other account whose taskbar layout file is still ours (their registry hives are loaded briefly for that), and points the pinned
  launcher shortcuts back at `DynaRun V3.exe`. When the driver option is used as well, the value is **not** written at once: it is noted
  (`AutostartPending` under `HKCU\Software\DynaRunFix`) and written when the driver step has ended (installed, failed, cancelled or the wizard
  closed; not while the restart for the driver is still to come), so DynaRun does not start at the sign-in that continues the driver installation. If you choose *Skip* for the driver, the install itself writes the value.
- **Also install the Dyna Pro USB driver (one restart needed).** Shown only on Windows 10/11 and only when the driver is not installed
  yet. Steps with screenshots: [docs/DRIVER.md](DRIVER.md). In order, the wizard
  1. (the driver pages come before the install: after the license page, or right after the first page when DynaRun V3 is already installed) uses a `Dyna Pro USB.zip` (or the folder it is unpacked in) that is already on the computer (its own earlier download, the setup's folder, Downloads, Desktop, Documents;
     any file name) when it holds the right driver files, else downloads it (about 2 MB) over HTTPS from `dynapro.co.uk` only. The check is
     of the content: each of the 18 files Windows installs (2 INFs, 2 catalogs, the i386 / amd64 `.sys` and `.dll` files they name) must have the
     SHA-256 built into the program; other files of the zip are ignored and not unpacked. If they differ, a warning page says which and offers
     "Install anyway" (not the default) when the package still looks like Dyna Pro's; the choice is logged;
  2. when you click *Install and restart* (the restart page, with the picture of the menu), asks for administrator permission **once**. In that one elevated step it first installs the fix (after DynaRun V3's own setup, if the wizard installs that too; if this fails, the driver is not touched), then saves the setup and the unpacked driver in `%ProgramData%\DynaRunFix` (a folder only administrators
     can change). Items 3 and 4 are part of the same step;
  3. **only if BitLocker protection is on or its status cannot be read** - or the drive is still being encrypted or decrypted (also paused) and has a key protector, which
     Windows reports as "off" although the protector already applies - suspends it (`manage-bde -protectors -disable` for the system drive with
     `-RebootCount 3`, so protection turns on again by itself after 3 restarts at the latest; the wizard also turns it on again when the
     installation ends or is cancelled, and remembers that in `HKLM\SOFTWARE\DynaRunFix`, value `DriverBitLockerSuspended`). While it is
     suspended the drive is not protected. Protection that is already off with a fully encrypted drive (you suspended it yourself), or that was
     never on, is left as it is. The suspension is checked afterwards; if it did not work (the restart menu would ask for the recovery key) the
     wizard shows a warning page with the way to find the key (aka.ms/myrecoverykey) and lets you choose *Restart anyway* or *Later* (which takes
     everything back); its *Find the recovery key* button opens that page. The wizard's first driver page says this before anything happens. `driver.log` and `bitlocker-before.txt` (the output of
     `manage-bde -status` and `-protectors -get`, **recovery passwords masked** before anything is written) record what it saw and did;
  4. asks Windows for its advanced start-up menu at the next start only (`bcdedit /set {current} onetimeadvancedoptions on`) and registers the
     scheduled task `DynaRunFix-DriverContinue`, which continues the wizard when you sign in;
  5. restarts the computer when that step is done (with the driver alone, started from the first page or by `/driver`, the button is *Restart now* and the step is the driver's preparation only). In the menu you press **7** (*Disable driver signature enforcement*); that is off
     for that one start only;
  6. after you sign in, installs the driver (`pnputil`) with no further UAC prompt on an administrator account (the task runs elevated; a standard user is asked for an administrator's password here). Windows shows its red warning (*Windows Security*, "can't verify the publisher"; this is not UAC) twice (once for each of the two driver files), choose
     *Install this driver software anyway* each time. The task and the boot option are then removed and BitLocker is turned on again. The last page
     offers *Start DynaRun*, which starts it as your normal user (not elevated, although that page runs with administrator rights); if that is not
     possible you are told to use the desktop icon.
  *Skip* on the driver pages installs the fix without the driver. If the fix is installed but the driver preparation then fails, the error page says that the fix is installed and the Start menu shortcut can try the driver again.
  If you do not go on, choose *Cancel* in the wizard or run `DynaRunFix-Setup.exe /driver-cancel`: that takes back the task, the boot option and
  the BitLocker suspension. Nothing of this is done without ticking the option (or using `/driver`).
  Other pages: *Check the installer* has an *Open website* button; on Windows 8 and older (Windows 8 / 8.1, Vista / 7, XP) `/driver` shows *The driver
  was not installed* with the manual steps for that Windows and an *Open the manual steps* button; if the driver is already installed it shows *The driver is already
  installed*. The driver search and your choices are logged to `%TEMP%\DynaRunFix-setup.log`.

**Updates.** DynaRun always opens first; the update never delays it. Once a day, after DynaRun was started, the
installed setup asks GitHub's API for the latest release of this repository (nothing else is sent) in the background.
The check talks HTTPS only, and only to GitHub's own hosts (the API, `github.com` and GitHub's release-asset hosts; every
redirect is checked, for the check as well as for the download). When a newer one is known, the next start of DynaRun shows (beside the running DynaRun, at most once a day)
*Yes* (install it after you close DynaRun), *No* (skip this version; a newer release is offered again) or *Cancel* / the window's
close button (ask again tomorrow). *Yes* downloads that release's `DynaRunFix-Setup.exe` (from `github.com/timliudev/DynaRunFix`; a redirect may only go to GitHub's own hosts: `github.com`, `api.github.com`, `objects.githubusercontent.com`, `release-assets.githubusercontent.com`),
checks its SHA-256 against the one GitHub lists (the release is checked again just before the install, and the downloaded file is held open so it cannot be swapped) and installs it with `/quiet /keep` (one administrator prompt; `/keep`
leaves the pin, the start at sign-in and the desktop shortcut as they are) once DynaRun is closed. No update question
is ever asked for `/autostart`, `/restart`, `/noupdate`, or when `DYNAFIX_NOUPDATE=1` is set, and not when the
launcher only attaches to a DynaRun that is already running. Every start of the setup by the launcher is written to
`%TEMP%\dynafix.log` first, with its arguments and the reason. Without internet, on Windows XP / Vista (no TLS 1.2), or on Windows 7 without update KB3140245 (TLS 1.2 support; the check asks
for TLS 1.2 or later, like the downloads), nothing happens.

DynaRun must be closed while the fix is installed or updated: a running DynaRun keeps the old
`dynafix.dll` (and Locale Emulator's dlls) loaded. The installer checks for a running `DynaRun V3.exe`
(and for `DynaRunFix.exe` / `LEProc.exe` from its own folder) before it changes anything. A `DynaRun V3.exe` that runs from another
folder does not use the files being replaced and does not hold up the install. The wizard
then offers *Close DynaRun and install*: DynaRun is closed like with its own close button, and ended
if it is still running 10 seconds later (a test run in progress stops, unsaved data is lost).
`/quiet` changes nothing and ends with exit code **6**; `/quiet /close` closes DynaRun the same way first.
Only a DynaRun of the same Windows session whose path can be read is closed. If one of another user's session (or one whose path cannot be
read) is running, closing from here would not help: the wizard says so and offers no close button, and `/quiet /close` ends with exit code 6 without
closing anything; that user has to close DynaRun first. Why it stopped with 6 (which program, its process and session number, whether it could be closed) is
written to `setup.log` in the install folder, or, before that folder exists or without administrator rights, to `%TEMP%\DynaRunFix-setup.log`.
For unattended installs run `/quiet /close` while nobody is working in DynaRun.
After copying, each installed file is read back and compared with the installer's copy.

If *Documents* is in OneDrive and an earlier copy of DynaRun's manuals or example files there is
"online-only", the installer reads those files first so that OneDrive downloads them; Windows Installer
cannot do that itself and would stop with error 1305. File contents and OneDrive settings are not changed.

**DynaRunFix never runs DynaRun with more rights than a normal start from the desktop**, and DynaRun does not need administrator
rights. The launcher adds `RunAsInvoker` to `__COMPAT_LAYER` in the environment of the DynaRun process it starts, which takes precedence over the compatibility setting *Run as administrator* for `DynaRun V3.exe` (observed, see [below](#run-as-administrator-is-not-needed)); if the launcher itself is run as
administrator, it first starts itself again with the token of the desktop shell (Explorer) and ends, so DynaRun (and Locale Emulator)
get exactly what a double-click on the desktop would give them: the user's normal, non-elevated token under UAC. If the shell's token
is itself elevated (User Account Control off, or the built-in Administrator: the normal desktop is already full administrator), the
launcher carries on with it and writes that to `dynafix.log`; there is nothing to drop, and `dynafix.dll` then skips the graph
language redirect (see below). Only if no shell token can be had at all does the launcher show a message and not start DynaRun. When
the token was non-elevated, or the compatibility setting was ignored, a one-time notice is shown
(English or Traditional Chinese; answer *No* to *Show this notice again next time?* to switch it off). If a DynaRun that
is already running was started with more rights than the launcher has, the launcher asks you to close it and start it again through DynaRunFix.
The compatibility variable `__COMPAT_LAYER=RunAsInvoker` that the launcher sets for DynaRun is removed again by `dynafix.dll` inside DynaRun's
process, so that DynaRun's other child processes are not changed. The one exception is Dyna Pro's configuration helper `Setup_<n>.exe` of the first start: `dynafix.dll` starts it with `RunAsInvoker`
for that one start only (the variable is set for the call and put back), so Windows' installer detection does not raise a permission prompt for it; it only writes `HKCU\Software\DynaPro`.
Do not run `DynaRun V3.exe` itself as administrator, see [below](#run-as-administrator-is-not-needed).

### Installing the Dyna Pro USB driver

The Dyna Pro dynamometer connects by USB, and Windows needs Dyna Pro's USB driver to talk to it. Install it **once on every PC that has never been connected to a Dyna Pro**. Without it Windows lists the device under *Other devices* and DynaRun cannot reach the dynamometer. DynaRunFix does not contain the driver; it comes from Dyna Pro (`dynapro.co.uk`). The full steps with screenshots are in [docs/DRIVER.md](DRIVER.md); this section is the short version.

**With the setup wizard (Windows 10 / 11).**
1. Run `DynaRunFix-Setup.exe` and leave **Also install the Dyna Pro USB driver (one restart needed)** ticked on the first page. The check box is not shown when the driver is already installed or Windows is older than 10. If DynaRun V3 and the fix are already installed, the first page offers just the driver. Later, you can start the same steps from the Start menu shortcut **Install Dyna Pro USB driver** (**安裝 Dyna Pro USB 驅動程式** on a Traditional Chinese Windows) or with `DynaRunFix-Setup.exe /driver`.
2. The wizard first looks for a `Dyna Pro USB.zip` (any file name), or a folder where it was unpacked, that is already on the computer: its own earlier download, the folder the setup runs from, *Downloads* (and one folder below it), *Desktop* and *Documents*. If it finds one that holds the right driver files, it uses it without downloading. Otherwise it downloads the official zip (about 2 MB) from `dynapro.co.uk`. The files are checked by content (SHA-256), not by name.
3. The wizard shows the page with the picture of the start-up menu (photograph it with your phone if you like). Click *Install and restart* (*Restart now* when only the driver is installed). Windows asks for administrator permission **once** (**Yes**): that one prompt covers installing the fix and preparing the driver, and none follows after the restart. If BitLocker protection is on, the wizard suspends it until the driver is installed (the drive is not protected meanwhile) and turns it on again when the installation ends or is cancelled. *Skip* on the driver pages installs the fix without the driver.
4. The computer restarts. In the start-up menu that appears, press **7** (*Disable driver signature enforcement*). This only applies to that one start.
5. Sign in. The wizard continues by itself and installs the driver (a standard user is asked for an administrator's password here; an administrator account is not asked). Windows shows a red *Windows Security* warning ("can't verify the publisher"; not UAC) **twice**; choose **Install this driver software anyway** each time. The last page offers *Start DynaRun*.

**If the warning page appears.** When the files differ from the ones the installer expects (Dyna Pro may have updated the driver, or the file is damaged), the wizard shows a warning with the source, the zip's SHA-256 and the files that differ. *Install anyway* is offered (it is not the default button) only if the package still looks like Dyna Pro's; it applies to that one zip or folder for this one installation. If you **cancel** the warning for a copy the wizard found by itself, it does not use that copy again: later tries (including retry) download the driver from Dyna Pro's website instead. A file you picked yourself with *Choose file...* is always checked as it is. Note that DynaRunFix does not check the driver files' digital signature; it compares the SHA-256 values and, for *Install anyway*, that the bus INF names the Dyna Pro device ID. Whether to trust a package that differs is your decision.

**If you miss 7 or change your mind.** If you miss 7, the wizard tells you after you sign in and offers **Retry**. To stop, choose **Cancel** in the wizard, or run `DynaRunFix-Setup.exe /driver-cancel` from an administrator command prompt: this takes back the scheduled task, the one-time boot option and the BitLocker suspension (it installs and removes no driver). `/quiet` never installs the driver.

**By hand (also for Windows 8 and older).** Download [Dyna Pro USB.zip](https://dynapro.co.uk/Site/graphics/Technical_Info/Software/Dyna%20Pro%20USB.zip), unzip it, restart with driver signature enforcement disabled, then in *Device Manager* use *Update driver* > *Browse my computer for drivers* and pick the unzipped folder (with *Include subfolders*). On Windows 8 and older the wizard's `/driver` shows *The driver was not installed* with an *Open the manual steps* button. All steps, including BitLocker and the older-Windows variants, are in [docs/DRIVER.md](DRIVER.md#steps). The BitLocker path is not yet verified on a real PC, and the manual steps for older Windows are untested.

**Check that it worked.** Connect the Dyna Pro by USB and switch it on. Open *Device Manager* (**Win + X**): the device is listed under *Universal Serial Bus controllers* as **DynaPro Dynamometer**, no longer under *Other devices*. If the driver is already installed, the setup no longer offers the driver check box and `/driver` shows *The driver is already installed*.

**Common problems.**
- *"The hash for the file is not present in the specified catalog file"*: Windows was restarted normally before the driver was installed, or 7 was not pressed. Repeat the restart with 7 and install in that same session.
- The start-up menu asks for a BitLocker recovery key: enter it (<https://aka.ms/myrecoverykey>), or restart normally and let the wizard suspend BitLocker (or suspend it yourself) and try again.
- The drive stays unprotected after you stopped halfway: choose *Cancel* in the wizard, or run `Resume-BitLocker -MountPoint "C:"` in an administrator PowerShell.
- The device is not under *Other devices*: check the USB cable and that the Dyna Pro is switched on; unplug and plug it in again.
- The wizard says the working folder could not be prepared: restart Windows and try again.
- Uninstalling DynaRunFix does not remove the driver. The wizard's log is `%ProgramData%\DynaRunFix\driver.log`; the file searches and your choices are in `%TEMP%\DynaRunFix-setup.log`.

### Manual use (zip)

The zip on the Releases page contains the same files (plus `le\`, the manifest and `LICENSE-miniz.txt`) for manual use: keep `DynaRunFix.exe` and
`dynafix.dll` **in the same folder** and start DynaRun through `DynaRunFix.exe`
(`DynaRunFix.exe "D:\path\to\DynaRun V3.exe"` for another location). If DynaRun is already running, the
launcher attaches the fix to it.

Check `%TEMP%\dynafix.log`: it should contain `patched THBRes25.dll import of PostMessageA`.

### What DynaRunFix verifies, and what it does not

- **The DynaRun V3 setup (Dyna Pro's file):** DynaRunFix downloads it only over HTTPS from `dynapro.co.uk` (the connection is
  refused if the certificate is invalid; HTTPS and `dynapro.co.uk` are checked again on every redirect hop, so a redirect away from
  `dynapro.co.uk` or to plain HTTP is refused), with TLS 1.2 or later (Windows 7 needs update KB3140245 for that) and a cap of 300 MB,
  and before installing it
  checks that the file identifies itself as the DynaRun V3 setup (Windows Installer upgrade code and product name). Dyna Pro
  does not sign its setup and does not publish checksums, so **DynaRunFix cannot prove that the file is exactly what Dyna Pro
  released**. Use only files you got from Dyna Pro's official site. On its *Check the installer* page (just before the license page) the installer shows where the file came from,
  the MSI's version and manufacturer, and the file's full path, size and SHA-256, so you can compare them with whatever Dyna Pro gives you; it does
  not block anything based on them.
- **DynaRunFix's own updates:** the installer is checked against the SHA-256 that GitHub lists for the release file. Each
  release's notes include `SHA256SUMS.txt` (also attached to the release), and the release files carry a build provenance
  attestation (which repository and workflow built them). To check a downloaded file yourself:
  `gh attestation verify DynaRunFix-Setup.exe --repo timliudev/DynaRunFix`. This shows where a file was built, not that it is safe;
  the installer is not code-signed (see the SmartScreen note above).
- **The Dyna Pro USB driver (Dyna Pro's file):** downloaded only over HTTPS from `dynapro.co.uk` (the same connection and redirect checks as for the
  DynaRun setup), or taken from a copy already on the computer. Only the 18 files Windows installs are unpacked, each to a path built from the
  program's own table (never from the zip's entry names), into a new folder only administrators can change, with limits on size; links in the
  way are refused. Their SHA-256 is compared with the values built into the installer after they are copied there, and those same copies are
  installed (checked again just before the install). A package whose files differ is installed only after your explicit "Install anyway" on a
  warning page, for that one zip and that one installation. The driver itself is not DynaRunFix's: it is Dyna Pro's package, which DynaRunFix
  neither signs nor changes.

## First-time setup

On its first start DynaRun asks for the language, then for the dyno model and optional features
("Dyna Pro系統選擇"). After **OK** DynaRun starts `Setup_<nnn>.exe`, Dyna Pro's per-model configuration helper; Windows' installer detection would ask for
permission for it, so `dynafix.dll` starts it without elevation (since v1.8.0 there is no prompt; the helper only writes `HKCU\Software\DynaPro`, see [FIRST_START](FIRST_START.md)). DynaRun then restarts by itself with the chosen model, language and optional
features (climate monitor, water cooler, cooling fans, AFR analyser).

Without the fix DynaRun 3.26.0 hangs at this point on every Windows version, XP included: after the helper has
run, the selection window stays and one CPU core runs at 100 %. After a forced restart the language is English
and the optional features are off (the helper leaves the language value in the registry as `English` and the features' enable flags as `0`). `dynafix.dll` wraps DynaRun's `ShellExecuteA` call in memory:
it starts the helper without elevation (`__COMPAT_LAYER` gets `RunAsInvoker` for that one call; the log says `setup helper started without elevation (RunAsInvoker)`), waits for it, puts the chosen language back, switches on the picked features with the same values
*Engineering mode → System configuration → Save and exit* writes, and restarts DynaRun through
`DynaRunFix.exe /restart <pid>`. Each step is logged to `%TEMP%\dynafix.log` (the log never contains module or import addresses, and a symbolic link or junction in its place is not followed; every line starts with the date and time; a line is at most 1000 characters, longer ones end in `...`). `dynafix.dll` and the launcher can write to it at the same time: they take turns through a named lock (`Local\DynaRunFix.dynafix.log`), so lines are never mixed up, but their lines are interleaved. The size is checked only when the dll or the launcher starts: above 10 MB the file is cut to its newest 8 MB, from a full line on.

Screenshots of DynaRun's own windows are not included. What you see: first DynaRun's language selection
(English or Traditional Chinese), then its system selection screen (the machine model and the optional
modules, here climate monitor and AFR analyser picked), and after the automatic restart the main window in
the chosen language.

The `System_Setup` values written for the optional features are exactly the user's own choices on DynaRun's first-start
system selection screen. Observed with Process Monitor (registry operations only, on a fresh DynaRun 3.26.0 first start): the enable flags written are the same values, of the same type
(`-1` as text), that DynaRun's own *Save and exit* writes for the same selection. DynaRun itself additionally adjusts a few AFR display settings when it saves,
which DynaRunFix does not touch (they stay at their defaults until the configuration dialog is saved once). Nothing is written beyond what the screen
lets the user pick. The values are written whenever DynaRun starts that helper (DynaRunFix keeps no first-start flag of its own; normally that is only the first start), and not when the helper ends with a non-zero exit code (logged). If `dynafix.dll` cannot read the option buttons reliably (for example because of an unusual theme, scaling or
high-contrast setting), it writes no feature values at all and only logs this (only buttons that are clearly read as green, i.e. picked, are written; with any doubt, nothing is); the features can then be switched on in DynaRun's
*Engineering mode → System configuration*.

Root causes, registry values and test status: [docs/FIRST_START.md](FIRST_START.md).

## Other Windows 10/11 problems

### "Run as administrator" is not needed

DynaRun does not need administrator rights for any of its screens, so the launcher starts it exactly as a normal start from the desktop would (not elevated on a normal UAC desktop; with UAC off or on the built-in Administrator the desktop itself is elevated and so is DynaRun, never more); the compatibility setting *Run as administrator* does not elevate it when it is started through the launcher.
Observed on Windows 11 with UAC on, with that setting set on a small test program named `DynaRun V3.exe`: started through the launcher it ran non-elevated, with no UAC prompt (the launcher logs that it found the setting), while starting the same file directly made Windows ask for elevation. The launcher only changes the environment of the process it starts; if Windows still demanded elevation, starting would fail (the launcher then says it cannot start DynaRun) and nothing would run elevated.
 Do not run `DynaRun V3.exe` itself as administrator either: DynaRun's setup registers
several ActiveX controls (MSComm, MSCOMCTL, MSHFlexGrid, ...) only for the current user (`HKCU\Software\Classes`), elevated processes
ignore per-user COM registrations (Windows' rule), so an elevated DynaRun can stall at startup ("System Initializing. Please
Wait. 115") and alter its per-user component registrations. Start DynaRun through the DynaRunFix shortcut instead.

### Garbled Chinese text with a Traditional Chinese system locale

When Windows was installed in English and the system locale changed to Chinese (Taiwan) later, the
`HKLM\SYSTEM\CurrentControlSet\Control\FontAssoc\Associated Charset` key may lack `ANSI(00)=YES`. GDI
then draws DynaRun's Big5 labels in ANSI-charset fonts with code page 1252, which shows Latin letters
(`Aw³ï¥B·P...`). For a Traditional Chinese locale the launcher detects this and starts DynaRun through
[Locale Emulator](#garbled-chinese-or-other-dbcs-text-with-windows-utf-8-option) (with `dynafix.dll` attached as usual):
LE gives DynaRun the Big5 code page and charset in every window, including tooltips and OCX controls.
For other DBCS locales (and if LE is missing) `dynafix.dll` creates DynaRun's ANSI-charset fonts with the
locale's charset instead (`DYNAFIX_CHARSET`). Nothing in the registry is changed.
If the system locale is not Chinese at all, DynaRun's Chinese cannot be shown: set *Language for non-Unicode
programs* to Chinese (Traditional, Taiwan); the Chinese installer says so on its last page when it detects it (without the driver option on the last page, *Done*; with it, on the page before the restart).

### Garbled Chinese (or other DBCS) text with Windows' UTF-8 option

With *Region → Administrative → Change system locale → "Beta: Use Unicode UTF-8 for worldwide language
support"* enabled, the system ANSI code page is 65001 and VB6 programs show garbled DBCS text.

Copy [`manifest/DynaRun V3.exe.manifest`](../manifest/DynaRun%20V3.exe.manifest) next to `DynaRun V3.exe`
(administrator rights needed in Program Files). It sets `activeCodePage=Legacy`, so DynaRun uses the system
locale's legacy code page (950 for zh-TW) while the rest of the system stays UTF-8. Windows caches the
"no manifest" result per executable, so after copying the file update the exe's modification time once
(elevated PowerShell; it is the change of the modification time that matters, not the new value):

```
(Get-Item 'C:\Program Files (x86)\Dyna Pro Dynamometers\DynaRun V3.exe').LastWriteTime = Get-Date
```

`DynaRunFix-Setup.exe` does this for you, puts the exe's modification time back afterwards, and briefly creates the `DynaRun V3.exe` process suspended
and terminates it so that Windows reads the manifest (no DynaRun code runs).

`%TEMP%\dynafix.log` then shows `ansi-codepage=950`. This fixes text converted inside the process (menus,
text boxes, message boxes, charts). Labels, graphic buttons and some captions are drawn through Windows
with the system code page and stay garbled; for those, DynaRunFix runs DynaRun through
[Locale Emulator](https://github.com/xupefei/Locale-Emulator) (LGPL-3.0):

- Put LE in a `le` folder next to `DynaRunFix.exe` (`DynaRunFix-Setup.exe` and the release zip already do; to fetch it yourself run
  `tools\fetch-le.ps1 -OutDir <folder>\le`). It is not installed: no context menu, nothing in the GAC.
  `le\LEConfig.xml` holds DynaRunFix's zh-TW profile.
- When the system code page is UTF-8, the system locale is Traditional Chinese (legacy code page 950) and
  `le\LEProc.exe` exists, `DynaRunFix.exe` starts DynaRun through LE and attaches the flicker fix right
  away. It does the same for a Traditional Chinese locale whose FontAssoc lacks `ANSI(00)=YES`
  ([above](#garbled-chinese-text-with-a-traditional-chinese-system-locale)). Otherwise it starts DynaRun
  directly, as before.
- Keep the manifest as well: only the combination shows all text correctly (manifest alone: labels
  garbled; LE alone: menus and message boxes garbled).
- Under LE fonts are created with the Big5 charset, so Windows would draw Arial and similar faces with
  MingLiU; `dynafix.dll` draws them in Microsoft JhengHei UI instead (see [Fonts](#fonts-windows-ui-font-everywhere)).

Verified on Windows 11 (UTF-8 option on) with DynaRun 3.26.0: menus, dialogs, labels, buttons, the
viewer and files in folders with Chinese names all work. Switching the UTF-8 option off also fixes
everything without LE. On systems without the UTF-8 option none of this is needed. `DynaRunFix-Setup.exe` adds the manifest
(and removes it on uninstall) only when the option is on.

### Fonts: Windows' UI font everywhere

DynaRun's forms ask for Arial, MS Sans Serif, Times New Roman or MingLiU (新細明體), and Windows substitutes
others for some of them, so one window mixed several faces. `dynafix.dll` now draws DynaRun's text in the
face Windows itself uses for dialogs in that language, picked by the code page DynaRun runs with (under Locale
Emulator that is Big5):

| Code page | Face (first one installed) |
|---|---|
| 950 Traditional Chinese | Microsoft JhengHei UI, Microsoft JhengHei |
| 936 Simplified Chinese | Microsoft YaHei UI, Microsoft YaHei |
| 932 Japanese | Yu Gothic UI, Meiryo UI |
| 949 Korean | Malgun Gothic |
| other (English, ...) | the system's message font: Segoe UI (Tahoma on XP) |

- Applies on every path: native Traditional Chinese Windows, a locale changed later (Locale Emulator or
  `DYNAFIX_CHARSET`), English Windows.
- Swapped faces: Arial, Times New Roman, MS Sans Serif, Microsoft Sans Serif, MS Shell Dlg, Tahoma, Verdana,
  Segoe UI, MingLiU / PMingLiU / 新細明體 and the other languages' old UI faces (SimSun, MS UI Gothic, Gulim, ...).
  Symbol, digital and fixed-pitch faces (Wingdings, 7-segment fonts, Courier New, ...) and narrow or heavy faces
  (Arial Narrow, Arial Black) stay as they are.
- Covers DynaRun itself, its OCX controls (toolbars, lists, grids, tabs, the chart), VB's fonts, THBResize and
  the common controls (tooltips), including the stock GUI font. Menus, message boxes and the file dialogs are
  drawn by Windows and already use the UI font.
- The UI faces have a taller line than the old ones, so text of 16 px and up is scaled to 90 % to keep
  multi-line labels fitting (`DYNAFIX_FONT_SCALE`, 50-150). VB is also told the line height of Arial
  (1.12 em) for the UI face, so lines of multi-line labels stay as close together as before; the text keeps
  its size (`DYNAFIX_FONT_LINEH`, in 1/1000 em, `0` = off).
- `DYNAFIX_FONT=<face>` picks another face, `DYNAFIX_FONT=off` (or `none`, `0`) keeps DynaRun's own fonts. Only DynaRun's
  process is affected; no file or setting is changed.

- DynaRun loads and unloads OCX controls while it runs. `dynafix.dll` follows the loader's notifications, forgets a module
  when it is unloaded and redirects its fonts again when it is loaded again (log lines `module tracking: ...`).
Status: verified on Windows 11 with DynaRun 3.26.0 (native Traditional Chinese, and English Windows with the
locale changed to Traditional Chinese through Locale Emulator).

### Graph popup menu stays English without administrator rights

The graph control (Gigasoft ProEssentials, `Pesgo32e.ocx` on `PEGRP32E.dll`) shows its right-click menu in the language of a
resource file `PEGRC32D.dll`, which it asks for by name when it loads. DynaRun's installer puts the copy for the chosen language in
`%APPDATA%\Dyna Pro Dynamometers\Dyna Run V3\Language\<language>\`, and at every start DynaRun tries to copy it into the Windows
system folder (`SysWOW64`). That is refused without administrator rights, so the menu stayed English.

`dynafix.dll` answers the control's request with the per-user file instead; nothing is written to the system folder.

- Only for the one file name `PEGRC32D.dll` (bare, or with the system folder's path). The redirected load is always a resource-only
  load: a request that would load code (none of the data-file / image-resource flags) is not redirected, and for a resource request, whatever flags the control passed, `dynafix.dll` loads the per-user file with `LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE`, so the
  file is never run as code.
- The path is built by `dynafix.dll` from the profile folder and `HKCU\Software\DynaPro\Operation_Data\Default_Language`
  (a plain folder name), never from the request. It must be a regular file with no reparse point on the way below the profile.
  If anything fails, the original request goes on unchanged. The file is not pinned to a hash or version: it is whatever DynaRun's
  installer put there.
- Not done when DynaRun is elevated (DynaRun's own copy works then) or when that cannot be determined (it fails closed), and
  `DYNAFIX_PEGRC=0` switches it off.
- The control asks while it is loading, so its imports are patched from the loader's notification for the module, before its entry
  point runs. DynaRun's own copy attempt (and its access-denied) is left as it is.
- `dynafix.log` gets `PEGRC32D: ... redirected to ...` (or `not redirected, <reason>`).

### Status bar hidden behind the taskbar

DynaRun's main window is maximized but has no title bar. Windows maximizes such a window over the whole
monitor, so a taskbar at the bottom covers DynaRun's status bar (recording mode, date, time). This is DynaRun's
own behaviour on every Windows version, XP included; with the taskbar on the side the status bar is visible.

`dynafix.dll` makes DynaRun see the primary monitor's work area (the screen minus the taskbar) as its screen
size: it adjusts the VB6 runtime's in-memory screen size values to the work area before DynaRun's first form loads. It finds those values by an instruction-byte pattern in Microsoft's MSVBVM60 code, in memory only; it changes them only if they equal the real screen size, and if the pattern is not found it leaves them alone (and logs that).
DynaRun's main window and dashboard then come out sized for the work area, with nothing out of place (observed). The
runtime takes the values when the first form loads, so the launcher attaches the fix right as DynaRun starts (through
Locale Emulator, LEProc runs in a job object that reports DynaRun's start at once). As a fallback the maximized window is
kept inside the work area. With an auto-hide taskbar 2 pixels are left free on that edge. `DYNAFIX_FULLSCREEN=1` keeps the
original full-screen window; `DYNAFIX_WORKAREA=clip` only keeps the window inside the work area without adjusting the
screen size values. The log shows `screen-size values adjusted (work area): 2048x1280 -> 2048x1232` when it applies.

Before the fix the taskbar covers DynaRun's status bar (recording mode, date, time); after it the status bar sits
above the taskbar (observed on Windows 11, 2560x1600 at 125 %; no screenshots are included).

Status: verified on Windows 11 (1920x1080, taskbar at the bottom) and on a Windows 11 PC at 2560x1600 / 125 %,
both through Locale Emulator. Windows XP and auto-hide / multi-monitor setups not tested yet.

### DynaRun's screen looks wrong

If DynaRun's screen ever looks wrong, close DynaRun and start it again.

* **After changing the screen resolution (or scaling, or connecting another monitor) while DynaRun is open:** DynaRun
  lays out its screen once, at start, for the screen size it has at that
  moment (observed). If the size changes later, part of the controls are stretched while the gauges stay as they were, so
  the screen looks shifted; going back to the old resolution puts everything back. The original DynaRun does the same;
  restart DynaRun after such a change.
* **The first start right after installing DynaRunFix** could be laid out too big before v1.3.0 (the right gauge cut
  off): the launcher loaded `dynafix.dll` only after DynaRun had started, and the first load of a newly installed dll
  is slow. Since v1.3.0 the dll is loaded before DynaRun starts; the log shows `windows DynaRun had before dynafix: 0`.
  Details: [docs/FIRST_START.md](FIRST_START.md).
* **At sign-in** Windows may still be setting up the desktop: the start at sign-in waits until the taskbar is there and
  the screen size has not changed for 3 seconds (at most 60 s; `launcher: autostart waited ...` in `%TEMP%\dynafix.log`).

### DynaRun says nothing when it cannot reach the Dyna Pro

If the device cannot be opened (another program, e.g. a second DynaRun or a logger, already has it open: only one
program can use it at a time) or does not answer (cable, power), DynaRun shows nothing for about 13 s and then the
"USB data connection error" dialog (Abort / Retry / Ignore); after *Ignore* it keeps trying to connect in the
background.

If the Dyna Pro was never connected to this PC, the cause may be the missing USB driver: Device Manager then lists the device under *Other devices*.
Install the driver with the setup wizard (option on its first page, or `DynaRunFix-Setup.exe /driver`) or by hand, see [docs/DRIVER.md](DRIVER.md) (Windows 8 and older: [older Windows](DRIVER.md#older-windows)).

`dynafix.dll` adds diagnostics in memory (it redirects the `CreateFileA` import entry of `dynapro.dll` to watch the device open; results are passed on unchanged and no file is modified):

* `%TEMP%\dynafix.log` gets every change in the result of DynaRun's connection attempts, a failed device open
  (`error 32` = in use by another program, `5` = access denied) with the holding program's name and PID when Windows
  can tell, and a line after 2 s of failed connection attempts.
* A small hint window appears at the bottom right: *"The Dyna Pro device is in use by another program (name, PID)"*
  or *"Cannot connect to the Dyna Pro. Check the USB cable and the device's power."* (Traditional Chinese on a Chinese
  Windows). It never takes the focus or blocks DynaRun, closes after 10 s or on a click, and is rate-limited (one per
  minute for "in use", one per failure run for the cable hint).
* `DYNAFIX_CONNHINT=0` turns the hints off (the log lines stay). `DYNAFIX_CONNHINT_MS=<ms>` sets how long reconnects must
  fail before the cable hint (default 2000, 500-60000).
* The holder's name comes from Windows' Restart Manager, which knows files better than device paths: when it cannot
  tell, the hint just has no name.


### DynaRun freezes when the USB device stops answering (opt-in, EXPERIMENTAL)

**Experimental, off by default, not yet verified with real hardware.** `DYNAFIX_USBFIX` is **off by default** (opt-in through an environment variable,
nothing changes without it). Use it only if you accept the trade-offs below.

What it is for: when the Dyna Pro stops answering in the middle of a run, DynaRun's window can stop responding for good if
its `DPSetData` call into `dynapro.dll` does not return. That a `DPSetData` call never returns once the device stops answering is an
assumption, not something that was measured.

Set `DYNAFIX_USBFIX=1` before DynaRunFix is started (system or user environment variable, or `set` in the shell that starts it):

* `dynafix.dll` runs DynaRun's `DPSetData` calls with mode 3 on a helper thread while DynaRun's window thread waits for the
  call with a watchdog. Only if the call is still blocked after about 2 s **and** `DPConnStatus()` reports the connection as down,
  `DPSetData` returns 0 to DynaRun. DynaRun is expected to treat 0 as "nothing written" and to run its own reconnect logic (`DPConnect`);
  this reaction is expected, it has not been verified with real hardware. As long as the connection is reported up, the call is left
  to run on, however long it takes. Calls with any other mode are passed straight to the original on the caller's thread.

Where the facts come from: the behaviour of `DPSetData`, `DPConnStatus` and `DPConnect` that this relies on was observed by logging
DynaRun's own calls at the API boundary (DynaRun 3.26.0). Dyna Pro does not document these functions; where something is only
assumed it is said so here. There is no version check.

How it works, plainly: no Dyna Pro file on disk is modified. `dynafix.dll` calls the exported `DPSetData` and `DPConnStatus` of
`dynapro.dll` and, while DynaRun runs and only with the switch set, wraps `DPSetData` in a watchdog in memory: if a call does not return in time, DynaRun gets control back and the call is abandoned (the call itself is left to end on a helper thread).
If the watchdog cannot be set up it is not switched on (`DPSetData watchdog off: ...` in the log).

Trade-offs:

* While the watchdog waits, DynaRun's window stays unresponsive and does not process messages; it recovers after about 2 s, but only
  when the connection is reported down at that point.
* Calls into `dynapro.dll` are **not** strictly one at a time: from the 2 s mark `dynafix.dll` itself calls `DPConnStatus` every
  100 ms, and DynaRun's own `DPConnStatus` and `DPConnect` calls run inside `dynapro.dll` while an abandoned
  `DPSetData` is still stuck (`DPConnect` is passed through unchanged, because blocking it would stop the reconnect that lets the
  abandoned call end). How `dynapro.dll` behaves with that overlap has not been verified.
* Callers of mode 3 queue: they enter the watchdog one at a time.
* The abandoned call stays on the helper thread. `dynafix.dll` makes that thread's waits return every 250 ms, but a timeout
  (`WAIT_TIMEOUT`) is substituted only for waits on **event** handles; other kinds of handle keep waiting. This lets the call end after
  DynaRun has reconnected (`the abandoned call returned after N ms` in the log).
* While an abandoned call has not returned yet, **every** `DPSetData` call, whatever its mode, is dropped: it returns 0 without
  entering `dynapro.dll`, to avoid overlapping `DPSetData` calls. DynaRun sees "no data written" for those calls.
* The abandoned call may never return. Then all further `DPSetData` calls return 0 until DynaRun is restarted; after 300 or more
  dropped calls and at least 20 s, `dynafix.dll` shows a one-time notice suggesting to restart DynaRun.
* Assumptions, stated plainly: that `dynapro.dll` tolerates `DPSetData` being called from a helper thread instead of DynaRun's own thread (it has not been checked), and that `p` holds three values, which is what was observed in DynaRun's calls. Exactly three values are copied, into a buffer with padding around it, so nothing else in `dynafix.dll` can be overwritten.
* The three values DynaRun passes in `p` are copied for the helper thread and copied back when the call completes normally, so
  the caller sees the same data as without the fix; after an abandon `p` is not touched again.

`dynafix.log` shows `DPSetData watchdog on (usbfix), N hook(s) set`, `usbfix: DPSetData blocked ...`,
`the abandoned call returned after N ms` and the count of dropped calls.

### Some .Dpr files will not open (empty File Run Properties, no curves)

Files stored in OneDrive and marked *Always keep on this device* carry the attribute `0x80000`
(`FILE_ATTRIBUTE_PINNED`), which does not exist on Windows 7/XP. DynaRun silently does nothing with such a
file: Process Monitor shows it querying the file's attributes but never opening the file for reading. The file's contents (such as a date value stored in it) are not the cause: a byte-identical copy without the attribute opens fine.

`dynafix.dll` fixes this in memory: the file-attribute APIs imported by `MSVBVM60.DLL` and `scrrun.dll`
return the attributes without the cloud bits (pinned, unpinned, recall-on-open, recall-on-data-access).
Files and OneDrive settings are not changed. The log shows `cleared cloud attributes 00080020 in
GetFileAttributesA` when it applies. Verified on Windows 11 with DynaRun 3.26.0.

### Setup warns "This setup uses VBScript custom actions" (Windows 11 25H2)

Windows 11 25H2 shows this warning when Dyna Pro's `Setup.msi` is started with its wizard. Once Windows disables
VBScript by default (planned for about 2027) the interactive setup may no longer work. `DynaRunFix-Setup.exe` runs
the setup with basic UI (`msiexec /qb!`), without the wizard pages, so this only matters when you start Dyna Pro's
`Setup.msi` yourself.
If VBScript has been switched off, turn it back on under *Optional features* before running that setup, or ask
Dyna Pro for a setup that does not need it. Details and timeline: [docs/VBSCRIPT.md](VBSCRIPT.md).

## Building

Requires Visual Studio 2019 or newer with the C++ desktop workload. Run:

```
build.cmd
```

Output goes to `build\`: `DynaRunFix.exe`, `dynafix.dll` and
`DynaRunFix-Setup.exe`, which embeds the launcher, the dll, the manifest, Locale Emulator and miniz's licence
(set `DRF_VERSION` for its version string). The first build downloads Locale Emulator into `build\le` through
`tools\fetch-le.ps1` (PowerShell, internet access, SHA-256 checked). The installer uses
[miniz](https://github.com/richgel999/miniz) 3.1.2 (MIT, `third_party/miniz`) to unpack the DynaRun setup. The binaries are 32-bit, have no C runtime dependency and target Windows XP and later.

All of dynafix's import hooks (fonts, text metrics, file attributes, `GetProcAddress`, THBResize) find their import slot by the
import's name (import name table walked together with the address table), not by the address the slot holds, and chain onto a hook
another tool (a logger, a compatibility shim) already installed, so the fixes work in either load order (a slot holding a second,
different foreign hook after the chain is set is left unpatched and logged rather than bypassing that tool). Imports from
kernel32/kernelbase/gdi32/gdi32full/user32 and `api-ms-win-*` API sets match; a module without an import name table falls back to
the old address match. `dynafix.log` shows `patched ... (by name)`, `... already held a hook: chained onto it`,
`... not patched: ...` for each slot that could not be patched, and a per-module `import patch <module> [<table>]: N patched, M failed` line.
`tests\build-shexec-tests.cmd` builds and runs the unit tests (the first-start helper path matching, `src\setup_match.h`; the `DYNAFIX_*` switch and `dynapro.dll` name parsing and the `RunAsInvoker` layer word, `src\dfutil.h`; parts of the launcher); it exits with 0 if all pass. `tests\build-tests.cmd` builds the log test harness and
`tests\build-hardening-tests.cmd` builds and runs the installer hardening tests, and `tests\build-driver-tests.cmd` the driver tests (see `tests\README.md`).
Diagnostic `DYNAFIX_STATS=1`: every 5 s `dynafix.log` gets one `stats(5s)` line (only when something happened) with the form
`WM_SIZE` count, how many were duplicates, how many THBResize `0x591` messages were passed on or skipped and how many forms were
shown or hidden. After start-up the lines stop, which shows that the guard has ended the hide/show loop.

## Diagnostic tool: msgspy

`tools/msgspy` (source only; it is not built by `build.cmd` and not part of the release) is the window-message logger used
to find the cause. Build it with `cl tools\msgspy\msgspy.c`, and `cl /LD tools\msgspy\msgspy_dll.c` for `msgspy.dll`, which must
sit next to `msgspy.exe`. With DynaRun already running it hooks DynaRun's GUI thread and
writes every relevant message (with decoded `WM_SIZE`, `WM_WINDOWPOSCHANGING/CHANGED`, `WM_GETMINMAXINFO`,
`WM_CREATE`, `WM_SETTEXT`) to `msgspy_<COMPUTERNAME>.log` next to the executable.

Privacy: that log contains the computer name (also in its file name) and the titles and text of DynaRun's windows. Look through it before you share it.

```
msgspy.exe [seconds] [message-to-post-hex]    # seconds: default 10
msgspy.exe 3          # log 3 seconds
msgspy.exe 4 591      # log 4 seconds and post 0x591 once to the maximized form
```

## License

MIT, see [LICENSE](../LICENSE).

### Third-party licences

The installer and the release zip carry the licence texts of the third-party software they contain:

- `LICENSE.txt`: DynaRunFix itself (MIT).
- `LICENSE-miniz.txt`: [miniz](https://github.com/richgel999/miniz) (MIT), used by the installer to unpack the DynaRun setup.
- `le\COPYING` and `le\COPYING.LESSER`: GNU GPL v3 and LGPL v3, the licence of [Locale Emulator](https://github.com/xupefei/Locale-Emulator)
  (unmodified binaries, started by DynaRunFix as a separate program).
- `le\THIRD-PARTY.txt`: the copyright notice of Locale Emulator, the SHA-256 of the release it was taken from and where the corresponding
  source code is.

The taskbar pin technique for Windows 7 to 10 was described by Gee Law ([blog post](https://geelaw.blog/entries/msedge-pins/)); no code from that article or from Mozilla is included.

---

## 繁體中文

讓 *DynaRun V3* 馬力機軟體(Dyna Pro Dynamometers,S68 等機型)**原生在 Windows 10/11 執行時主畫面不再閃爍**的小型相容性修正。

> 本專案與 Dyna Pro Dynamometers Ltd 無任何關係,也未經其認可。DynaRun 用來縮放表單的 *THBResize*(`THBRes25.dll`)
> 是 THB Componentware 的產品;DynaRunFix 與 THB Componentware 同樣無任何關係。DynaRunFix 不包含、不散布他們的任何檔案,也不修改任何 Dyna Pro 或 THB 程式檔案的內容(見下方「直白地說」),
> 也不會繞過、停用或改變 DynaRun 的授權啟用或防拷機制。
>
> 直白地說 DynaRunFix 會改變什麼:它會**在 DynaRun 執行時改變 DynaRun 的行為**,只在 DynaRun 行程的記憶體中;不修改任何 Dyna Pro 或 THB 檔案的內容。具體來說,它
>
> - 把該行程中已載入模組的匯入表項目導向自己的函式:`THBRes25.dll` 的 `PostMessageA`;Microsoft 的 `MSVBVM60.DLL` 與 `scrrun.dll`
>   匯入的檔案屬性 API、`GetProcAddress` 與文字 API;`DynaRun V3.exe`、DynaRun 資料夾內其他模組及其 OCX 元件的字型 API;
>   `THBRes25.dll`、`PEGRP32E.dll`、`MSVBVM60.DLL`、`ole32`、`oleaut32`、`MFC42`、`comctl32` 與 `dynapro.dll` 中字型相關的匯入(含延遲載入的匯入);`dynapro.dll` 的 `CreateFileA`(只用來觀察裝置開啟,
>   結果原樣傳回)以及,只有在開啟選用的 USB 開關時才有的 `dynapro.dll` 的 `WaitForSingleObject`,供 `DPSetData` 監看使用(見下文;只作用於監看的輔助執行緒:呼叫被放棄後,該執行緒對事件的無限等待改為每 250 毫秒傳回一次,其餘呼叫原樣傳遞);還有繪圖元件自己的模組 `Pesgo32e.ocx` 與 `PEGRP32E.dll`
>   (Gigasoft ProEssentials)的 `LoadLibraryExA` / `LoadLibraryExW`,只針對它索取語言資源檔 `PEGRC32D.dll` 的那一次要求,見
>   [沒有系統管理員權限時,圖表右鍵選單是英文](#沒有系統管理員權限時圖表右鍵選單是英文);
> - 把 DynaRun 以 `GetProcAddress` 查到的 `ShellExecuteA`、`DPConnect` 換成自己的包裝函式,只有在開啟選用的 USB 開關時才再加上 `DPSetData`;
> - 對 DynaRun 的主表單做子類別化(`WM_WINDOWPOSCHANGING`、`WM_GETMINMAXINFO`,並重新送出 `WM_SETFONT`),在 DynaRun 的 GUI 執行緒上安裝兩個視窗訊息掛鉤
>   (`WH_CALLWNDPROC`、`WH_CALLWNDPROCRET`),並在 DynaRun 自己的視窗上存放少數視窗屬性(`SetProp`,例如上一次的 `WM_SIZE`);
> - 調整 VB6 執行階段在記憶體中的螢幕大小值:以 Microsoft `MSVBVM60` 程式碼中的一段指令位元組特徵找出這些值(只在記憶體中),只有它們等於真實螢幕大小時才修改,找不到特徵就不動它(見[狀態列被工作列遮住](#狀態列被工作列遮住));
> - **寫入 DynaRun 自己的設定**,位於 `HKCU\Software\DynaPro`(`Default_Language` 與 `System_Setup`:使用者在首次啟動時自己的選擇,見[首次設定](#首次設定)),以及 `%TEMP%\dynafix.log`。
>
> 安裝程式會把 DynaRun 的捷徑改為經由啟動器執行(會備份原本的捷徑檔),並且只有在開啟 Windows UTF-8 選項時,
> 才在 `DynaRun V3.exe` 旁加上一個 manifest 檔(見[使用方式](#使用方式);為此會短暫改變該 exe 的修改時間後還原,exe 的內容絕不會被改動)。DynaRunFix 絕不會以高於「從你的桌面正常啟動」的權限執行 DynaRun(UAC 關閉時,正常啟動本來就是完整系統管理員),否則就不啟動(見[使用方式](#使用方式))。
> DynaRunFix 不碰任何授權、啟用或防拷機制或元件。
>
> 只有在你勾選安裝精靈的驅動程式選項(或執行 `DynaRunFix-Setup.exe /driver`;`/quiet` 絕不會),DynaRunFix 才會在安裝 Dyna Pro USB 驅動程式期間額外改變系統:
> 把從 dynapro.co.uk 下載的驅動程式存放在 `%ProgramData%\DynaRunFix`,暫停 BitLocker(只有保護為開啟或無法讀取其狀態時),讓 Windows 在下次開機時顯示一次進階啟動選單,
> 登錄一個在你登入後讓精靈繼續的排程工作,並重新啟動電腦;你在該選單按 7、並回應 Windows 的紅色警告之後,才會安裝驅動程式。驅動程式簽章檢查只在那一次開機期間關閉。
> 安裝結束或取消時,工作與開機選項會被移除,BitLocker 也會恢復。沒有勾選該選項就不會做這些事。詳見[使用方式](#使用方式)與 [docs/DRIVER.md](DRIVER.md#繁體中文)。
>
> 你必須自備合法安裝的 DynaRun V3,並請自行確認使用本修正符合你與 Dyna Pro 之間的授權條款。
> DynaRun V3 由 Dyna Pro 依其條款另行授權給你;本專案的 MIT 授權只涵蓋 DynaRunFix 本身。

### 症狀
Win10/11 上主儀表板每秒閃好幾次(整個視窗消失又出現);同一套安裝在 XP / Win7 正常。
改 DPI、相容模式、有無接 USB 馬力機都沒差。

### 原因(觀察結果)
DynaRun V3 是 VB6 程式,表單由第三方元件 THBResize 2.5(`THBRes25.dll`)縮放。從外部記錄 DynaRun 主表單的視窗訊息可以看到:
1. 表單每收到一次 `WM_SIZE`,之後就會有一個私有訊息 `0x591` 被 post 到表單。
2. 每個 `0x591` 之後都是同一串視窗變化:主表單隱藏 → 出現空白表單 → 主表單最大化顯示 → 空白表單關閉。
3. Win7/XP 重新顯示「已最大化、尺寸沒變」的表單時**不會**產生 `WM_SIZE`,跑一輪就停。
4. Win10/11 在同樣情況**仍會**產生尺寸相同的 `WM_SIZE (SIZE_MAXIMIZED)`,接著又出現 `0x591` → 反覆的隱藏/顯示迴圈 → 閃爍。

訊息紀錄與各 Windows 版本的比較見 [docs/ROOT_CAUSE.md](ROOT_CAUSE.md)。

### 修正方式
啟動器 `DynaRunFix.exe` 把 `dynafix.dll` 載入 DynaRun 行程,只在記憶體中把 THBRes25 對 `PostMessageA` 的呼叫導向修正函式:
若這次 `0x591` 是由「與上一次完全相同的 `WM_SIZE`」引起的就不送出,行為就跟 Win7 一樣。真正的尺寸變化照常處理。
不修改任何檔案,只寫一個 log:`%TEMP%\dynafix.log`(每行開頭有日期與時間;每行最多 1000 個字元,更長的以 `...` 結尾;dll 或啟動器啟動時若檔案超過 10 MB,就裁成最新的 8 MB,從完整的一行開始;
DynaRun 執行中不會裁剪,所以單次執行期間檔案可能超過 10 MB;啟動器與 dll 輪流寫入,兩者的行依時間交錯),以及在 DynaRun 啟動首次設定程式時(通常只有第一次啟動),把 DynaRun 自己的語言和選購功能設定寫入 DynaRun 的登錄機碼 `HKCU\Software\DynaPro`(見下方)。
log 只記模組與 API 名稱、不含模組或匯入位址(可能出現視窗與 GDI 控制代碼的值),寫入時不跟隨符號連結或連接點,但可能含有檔案路徑與視窗標題,貼到網路上之前請先檢查。同一個 dll 也修正[首次設定](#首次設定)與 [OneDrive 檔案打不開](#部分-dpr-打不開file-run-properties-全空沒有曲線)的問題。

### 更新內容
v1.8.0(2026-10-18 發行):
- 新增:檢查 Dyna Pro 安裝檔的版本。3.26.0(測試過的版本)照舊安裝;其他 3.x 或讀不到版本時會顯示提示(只在 3.26.0 測試過,有問題請到 [GitHub issues](https://github.com/timliudev/DynaRunFix/issues) 回報),安裝繼續;**DynaRun 4 以上**有獨立的頁面,說明它可能不需要修正,並提供「僅安裝原版軟體」(只安裝 Dyna Pro 的安裝檔:不裝修正、不裝驅動程式)與「仍要安裝修正」。
- 變更:「確認安裝檔」頁先顯示來源、MSI 的版本與製造商,字型較小、不需捲動就能看完;在這台電腦上找到的安裝檔,可按「改從官網下載」改用 Dyna Pro 官網的檔案。
- 變更:**第一次啟動不再詢問是否允許變更**(沒有 UAC 提示):Dyna Pro 的設定程式 `Setup_<編號>.exe` 只寫入 `HKCU\Software\DynaPro`,所以改為不提升權限啟動。
- 變更(安裝精靈):Dyna Pro 的安裝程式執行時,精靈會緊貼在 msiexec 進度視窗後面(不是永遠在最上層);重新開機後等待點擊的驅動程式頁面會蓋過開機啟動的視窗(驅動程式安裝中則不會,Windows 的警告才看得到);長時間的步驟結束而精靈不在最前面時,工作列按鈕會閃爍,並顯示 Windows 通知。

v1.7.2(2026-10-18 發行):
- 修正:%APPDATA% 路徑非常長(約 200 個字元以上)時,捷徑與工作列釘選程式碼不再造成緩衝區溢位;放不下的資料夾會被略過。
- 修正:安裝精靈會忽略不是它自己的工作執行緒送出的「工作完成」視窗訊息(先前可能使精靈當機)。另外:「關閉 DynaRun 並安裝」只對一次安裝有效,驅動程式準備失敗時會移除已放置的驅動程式檔。其餘是內部原始碼整理,不打算改變行為;見[更新紀錄](../CHANGELOG.md)。

v1.7.1(2026-10-17 發行):
- 修正:同時安裝驅動程式的完整安裝，現在只會要求一次系統管理員權限。先前在系統管理員帳戶上會問兩次（一次裝修正、一次準備驅動程式），和文件寫的「問一次」不符。驅動程式的頁面（下載與檢查、重新開機選單的圖片）現在排在安裝之前；按「安裝並重新開機」後，一步完成修正的安裝與驅動程式的準備；選「略過」則只安裝修正、不裝驅動程式。重新開機後，系統管理員帳戶不會再被詢問；標準使用者仍會被詢問（要輸入系統管理員密碼）。

v1.7.0(2026-10-17 發行):
- 新增:Dyna Pro 的軟體頁面有比已安裝版本更新的 DynaRun V3 時,第一頁會提示並提供「下載新版」(完全不會自動執行)。驅動程式的「選擇檔案…」也可以選已解開資料夾裡的 ftdibus.inf。
- 新增:已安裝 DynaRun 與修正、只缺驅動程式時,第一頁只提供驅動程式。
- 變更:DynaRun 安裝檔與驅動程式共用同一個下載流程與錯誤頁,所有頁面的按鈕一致。
- 修正:「下載新版」現在真的會下載;電腦上找到損毀的 DynaRun 安裝檔不再一直重複提供;沒有網路時關閉精靈不會刪到自己選的檔案;新版提示和下載不再互相干擾;過長的名稱與路徑不再有超出緩衝區的風險;重新開啟 BitLocker 失敗時會記住並再試;取消自動找到的驅動程式副本的警告只會跳過這類副本,自己選的檔案一律照實檢查。

v1.6.0(2026-10-17 發行):
- 新增:安裝精靈在使用驅動程式前會先檢查。先找電腦上現有的(它自己的暫存、安裝程式所在資料夾、下載、桌面、文件;zip 或已解開的資料夾),都不符才下載。18 個驅動程式檔案會和內建的 SHA-256 表比對;不同時警告頁會列出來,並提供「仍要安裝」(不是預設)。見 [docs/DRIVER.md](DRIVER.md#繁體中文)。
- 新增:授權合約頁拆成兩頁(先是列出安裝檔資訊的「確認安裝檔」頁,再是授權合約)。下載會自動重試、可暫停與繼續,並顯示進度;驅動程式的最後一頁有「開始使用 DynaRun」。
- 變更:驅動程式安全解開(只取出 18 個已知檔案,放進只有系統管理員能更改的資料夾)、無法讀取 BitLocker 狀態時也會暫停並檢查暫停是否成功、重新開機選單的圖片標出要按的那一行、開機自動啟動等驅動程式步驟結束才登錄。
- 修正:驅動程式警告頁一定先顯示「不確定請按取消」的建議,再列出不同的檔案(或其開頭);同一次執行中第二次下載驅動程式會顯示進度;對這台電腦上找到的檔案取消警告後,不會再出現同一個警告。

所有版本的紀錄:[CHANGELOG.md](../CHANGELOG.md#更新紀錄)。

### 使用方式
1. 從 [Releases](https://github.com/timliudev/DynaRunFix/releases) 下載 **`DynaRunFix-Setup.exe`**，雙擊執行。
   因為檔案沒有數位簽章，Windows SmartScreen 可能顯示「Windows 已保護您的電腦」：請按 **其他資訊 → 仍要執行**。

   | 1. 按「其他資訊」 | 2. 按「仍要執行」 |
   |---|---|
   | ![SmartScreen：Windows 已保護您的電腦](img/smartscreen-1.png) | ![SmartScreen 的「仍要執行」按鈕](img/smartscreen-2.png) |

2. 照畫面操作。還沒安裝 DynaRun V3 時，會從 [Dyna Pro 官網](https://dynapro.co.uk/Software_Release.htm)下載安裝檔
   （「下載」、桌面、「文件」或安裝程式旁邊已經有 DynaRun 安裝檔 zip 或 `Setup.msi` 就直接用），請你輸入 **Dyna Pro 給的安裝密碼**，
   先顯示「確認安裝檔」頁（可以複製文字的框，字型較小、不需捲動就能看完：先是檔案的來源、MSI 的版本與製造商，再是檔案的完整路徑、大小與 SHA-256；在這台電腦上找到的安裝檔，頁面另有「改從官網下載」，改用 Dyna Pro 官網的檔案），再顯示 Dyna Pro 的授權合約（取自安裝檔，由 DynaRunFix 自己顯示），你接受後才安裝 DynaRun V3 和修正（安裝檔裡的合約讀不出來時，必須勾選同意 Dyna Pro 的授權條款才能繼續）。已經裝好 DynaRun V3 時，只會安裝修正。DynaRun V3 裝在安裝程式找不到的資料夾時，第一頁有「已裝在其他位置…」可以指定 `DynaRun V3.exe`。
   Windows 會詢問一次是否允許變更，請按 **是**。這是整個安裝只問的一次；同時勾選驅動程式時，它也涵蓋驅動程式的準備（驅動程式的頁面會排在安裝之前，見下方）。
   **版本檢查。** 安裝檔 MSI 的 ProductVersion 只在一個地方分類(`src\package.c` 的 `dynarun_ver_class`;升級代碼與產品名稱的檢查照舊)。剛好是 3.26.0(修正測試過的版本):沒有任何變化。
   其他 3.x(或更舊):「確認安裝檔」頁與授權合約頁會顯示提示——只在 3.26.0 測試過、可能有額外問題、請到 <https://github.com/timliudev/DynaRunFix/issues> 回報;安裝繼續。
   讀不到版本:同樣顯示提示,安裝繼續。4 以上(可能已不是 Visual Basic 6 程式,也許是原生 Windows 10/11,所以可能不需要修正):「確認安裝檔」之後會有一頁說明,並建議先單獨安裝 Dyna Pro 的軟體試用。
   必須勾選「我了解 DynaRunFix 未在此版本測試過」,最右邊的「仍要安裝修正」才能按。「僅安裝原版軟體」會先到授權合約頁,再只安裝 Dyna Pro 的安裝檔(一次系統管理員確認,同樣是檢查過的 MSI 副本):
   不裝修正、不改捷徑、不釘選工作列、不設開機自動啟動,也**不裝 USB 驅動程式**(驅動程式步驟屬於修正:它是為 V3 未簽章的 FTDI 驅動程式而設,沒有以 V4 檢查過;第一頁的驅動程式核取方塊會被忽略)。
   之後如果 V4 出現舊的問題,再執行一次本程式:它會找到已安裝的 DynaRun 並提供安裝修正。
   **視窗順序與通知。** 提升權限的安裝進行時,精靈會讓自己緊貼在目前取得前景、屬於安裝流程的視窗後面(UAC 提示、Dyna Pro 的進度視窗「Please wait while Windows configures ...」、驅動程式警告),並在其他視窗之上,
   但不是永遠在最上層:你可以切換到別的程式,切過去的程式不會被干擾。長時間的步驟(下載、解壓縮、安裝、驅動程式步驟)結束,或重新開機後精靈等你操作,而它不是最前面的視窗時,工作列按鈕會閃爍,
   並顯示 Windows 通知(暫時的系統匣圖示;10 秒後或你回來時消失)。重新開機後等待你點擊的驅動程式頁面(「最後一步」、「沒有按 7」)會蓋過開機啟動的程式;按下「安裝」後就不再置頂,Windows 的紅色警告才不會被擋住。
   下載中斷時會自動重試（依序等 1、2、4、8、15 秒；只要有資料進來就重新計數），頁面會顯示失敗過程中的第一個錯誤；「暫停」會乾淨地停止，
   「繼續」（錯誤之後是「繼續下載」）會從已經下載的部分接續；「開啟官網」與「選擇檔案…」仍然可用。Windows 說電腦完全沒有連上網路時，錯誤頁會立刻出現（不用等重試）；主機不回應時 10 秒就放棄。驅動程式走同樣的頁面（先找電腦上現有的，再下載）；它的「選擇檔案…」可以選 zip，或已解開它的資料夾裡的 ftdibus.inf。
   精靈第一頁最多有三個核取方塊:釘選到工作列、開機時自動啟動 DynaRun,以及(Windows 10/11 且尚未安裝驅動程式時)**同時安裝 Dyna Pro USB 驅動程式（需重新開機一次）**(見下方)。
   DynaRun V3 和這個版本的修正都已安裝、只缺驅動程式時，第一頁只提供驅動程式（以及「重新安裝修正…」）。DynaRun V3 已安裝且電腦有網路時，第一頁還會在背景詢問 Dyna Pro 軟體頁面有沒有新版 DynaRun V3（那一頁在下載連結旁寫著版本，目前是 3.26.0；萬一不再寫版本，就向網站上的 zip 要第一個位元組（不會再下載其他內容），把它的 ETag / 長度 / 日期和上次完成的下載比對），有的話會顯示一行說明與「下載新版」。這只是提示：不按就不會下載或安裝任何東西，檢查失敗也不說什麼，每個結果在安裝程式的記錄檔（`%TEMP%\DynaRunFix-setup.log`；以系統管理員身分執行時是安裝資料夾裡的 `setup.log`）裡是一行。
3. 以後照常點 **DynaRun V3** 圖示啟動。第一次啟動（選擇系統）時，選好機型按 OK，DynaRun 會自己重新啟動。
   Windows 不會再詢問是否允許變更(v1.8.0 之前會為 Dyna Pro 的設定程式問一次)。

   這台電腦第一次連接 Dyna Pro?還需要 Dyna Pro USB 驅動程式。Windows 10/11 上可以由安裝精靈安裝(精靈第一頁的核取方塊:它會下載驅動程式、重新開機一次,
   你在出現的選單按 **7**、並兩次選擇「仍然安裝此驅動程式軟體」,登入後它會自動繼續);之後也可以用開始功能表捷徑「安裝 Dyna Pro USB 驅動程式」或
   `DynaRunFix-Setup.exe /driver`。也可以手動安裝,見 [docs/DRIVER.md](DRIVER.md#繁體中文)。

| | | |
|---|---|---|
| ![1. 開始](img/setup-start.png) | ![2. 從 Dyna Pro 官網下載](img/setup-download.png) | ![3. 輸入安裝密碼](img/setup-password.png) |
| 4. Dyna Pro 授權合約(由 DynaRunFix 自己顯示,內容取自安裝檔;此處不重現;顯示檔案詳細資料的「確認安裝檔」頁就在它之前) | ![5. 安裝中](img/setup-installing.png) | ![6. 完成](img/setup-done.png) |

*全新的 Win11(繁體中文),DynaRunFix 1.3.2。*

DynaRunFix 不包含任何 Dyna Pro 的檔案：安裝檔來自 Dyna Pro 官網，密碼由 Dyna Pro 提供。DynaRun 安裝檔以基本介面
（`msiexec /qb!`）執行，所以 Dyna Pro 自己的精靈頁面（包含授權頁）都會略過（授權合約由 DynaRunFix 自己顯示）。密碼只用來打開 zip，不會被儲存。以系統管理員權限安裝之前，安裝檔會先複製到只有系統管理員能寫入的資料夾並在那裡再檢查一次，DynaRun 就是從那份副本安裝的。
安裝檔是依內容辨識，不看檔名：只有自稱是 DynaRun V3 的 MSI（升級代碼與產品名稱相符）才會被安裝；zip 要等輸入密碼打開後才能檢查。安裝程式自己在「下載」等資料夾找檔案時，只會拿 DynaRun 的 MSI，
或是有密碼、裡面有 MSI 的 zip（Dyna Pro 的 zip 有密碼）；自己找到但其實不是 DynaRun 的檔案會直接跳過、不顯示訊息，
改用下一個或改從官網下載。其他 zip 仍可用「選擇檔案…」指定；你自己選的檔案如果不是 DynaRun 安裝檔，會被拒絕並清楚告訴你原因。

安裝修正時會做這些事：
- 把 `DynaRunFix.exe`、`dynafix.dll`、`LICENSE.txt`、`LICENSE-miniz.txt`、授權檔 `le\COPYING`、`le\COPYING.LESSER`、`le\THIRD-PARTY.txt`（見[第三方授權](#第三方授權)）和一份 `DynaRunFix-Setup.exe`（解除安裝用）安裝到 `Program Files (x86)\DynaRunFix`（64 位元 Windows；32 位元 Windows 為 `Program Files\DynaRunFix`）；
- 把現有的 DynaRun V3 捷徑（桌面、開始功能表、釘選到工作列；所有使用者與目前使用者）改為經由啟動器執行，名稱和圖示都保留(「以系統管理員身分執行」旗標不會保留:DynaRun 絕不會以高於「從你的桌面正常啟動」的權限執行(UAC 關閉時,正常啟動本來就是完整系統管理員);原本的捷徑檔會備份)；沒有桌面捷徑時會建立一個。我們的捷徑一律用 DynaRun 的藍色 logo（取自 Dyna Pro 原本捷徑的圖示檔；`DynaRun V3.exe` 本身只有視窗用的表格圖示），所以執行中的 DynaRun 在工作列上顯示的就是這個 logo；
- 只有開啟 Windows「使用 Unicode UTF-8 提供全球語言支援」時，才在 `DynaRun V3.exe` 旁加上[字碼頁 manifest](#開啟系統-utf-8-選項時中文亂碼)(exe 的修改時間會短暫改變後還原,並短暫以暫停狀態建立 `DynaRun V3.exe` 行程後立刻結束它,讓 Windows 讀取 manifest；不會執行任何 DynaRun 程式碼,exe 的內容不會被改動)。DynaRun 的資料夾或 exe 一般使用者可以更改、或路徑不是單純的本機路徑時不會加，也不會把 DynaRun 的路徑記進全機登錄供系統管理員那幾步（解除安裝）使用，安裝資料夾裡的 `setup.log` 會說明；Windows XP 無法偵測該路徑中的連結（連接點），只做權限檢查；
- 把 [Locale Emulator](#開啟系統-utf-8-選項時中文亂碼) 放到 `DynaRunFix\le`（只有繁中系統開了 UTF-8 選項，或 FontAssoc 缺 `ANSI(00)=YES` 時才會用到）；
- 在 Windows 10/11 上,加入開始功能表捷徑「安裝 Dyna Pro USB 驅動程式」(所有使用者;它執行 `DynaRunFix-Setup.exe /driver`)；
- 在「程式和功能」／「已安裝的應用程式」登錄解除安裝項目。

DynaRunFix 除了上述檔案之外，還會在這台電腦上留下這些東西（解除安裝會盡可能移除；`com-copied.txt`、`com-backup`、`com-skipped.txt` 與 `setup.log` 會留在安裝資料夾，該資料夾因此不會被刪除）：
- `HKLM\SOFTWARE\DynaRunFix`：`InstallDir`；`DynaRunExe`（DynaRun 的路徑，只有在它的資料夾和 exe 一般使用者無法更改時才記錄）；`ManifestInstalled`（已加上 manifest）；`ExeOrigTimePath`／`ExeOrigTime`（只在 `DynaRun V3.exe` 的原始修改時間還需要還原時存在）；`TaskbarPolicy`／`TaskbarPolicySid`（Windows 11 的釘選）；子機碼 `Shortcuts`（全機捷徑檔原本內容的備份）；另外還有 `Uninstall` 底下的項目。
- 每位透過修正執行 DynaRun 的使用者的 `HKCU\Software\DynaRunFix`：`Shortcuts`（該使用者原本捷徑檔的備份）；`UpdateChecked`、`UpdateVersion`、`UpdateSha256`、`UpdateSkip`、`UpdateAsk`、`UpdateETag`、`DownloadETag`、`DriverETag`（更新檢查與下載）；`AutostartPending`（等驅動程式步驟結束才寫入的開機自動啟動）；`DownloadETagDone`、`DriverETagDone`、`UpdateETagDone`（最後一次完成的下載是什麼；「有新版 DynaRun?」的提示會和第一個比對）；`NoticeElevatedLauncher`、`NoticeRunAsAdminLayer`（只有在你對一次性通知回答「否」之後才寫入）。
- Windows 11 且選了釘選時：`HKEY_USERS\<使用者>\Software\Policies\Microsoft\Windows\Explorer` 下的 `StartLayoutFile` 與 `LockedStartLayout` = 1，以及安裝資料夾裡的 `TaskbarLayout.xml`。`LockedStartLayout` 是微軟文件中與 `StartLayoutFile` 搭配的值：兩者都設定時，Windows 會把這個配置視為鎖定。解除安裝（或不選釘選重新安裝）時，只有在 `StartLayoutFile` 仍指向我們的檔案時才會移除；組織自己設定的配置絕不會動。
- 安裝資料夾裡除了程式檔之外：`TaskbarLayout.xml` 與 `DynaRun V3.lnk`（釘選所指向的捷徑，選了釘選時才有），以及 `setup.log`（系統管理員那一步做了或略過了什麼）。
- 精靈每次執行都會有:資料夾 `%TEMP%\DynaRunFix\`（下載的 `*.zip` / `.part`、解開的 `Setup.msi`，以及檢查驅動程式 zip 時的暫存資料夾 `drvcheck` 和 `drvdigest.tmp`；該步驟結束時刪除）與 `%TEMP%\DynaRunFix-setup.log`（檔案搜尋、新版提示與你的選擇，每行有時間，精靈以一般權限寫入）。
- 只有用過驅動程式選項時:資料夾 `%ProgramData%\DynaRunFix`（只有系統管理員能更改），內有安裝程式的副本、暫存的驅動程式檔案（`driver\`，只有會被安裝的 18 個檔案）、`driver.log`、`bitlocker-before.txt`（已遮蔽機密）；只有在「仍要安裝」生效期間才有 `accepted.txt`（被接受的那些檔案的 SHA-256，驅動程式步驟結束時刪除）；`HKLM\SOFTWARE\DynaRunFix` 下的值 `DriverBitLockerSuspended` = 1（只在驅動程式選項暫停 BitLocker 期間）；排程工作 `DynaRunFix-DriverContinue` 與一次性開機選項 `onetimeadvancedoptions`（兩者只在安裝結束或取消之前存在）；以及 BitLocker 暫停本身（只有保護原本為開啟時，見下方的驅動程式選項）。驅動程式一旦安裝，就屬於 Windows 的驅動程式存放區。

解除安裝會把捷徑檔還原成原本的內容，並移除它加上的 manifest（只有檔案與安裝程式自己的副本完全相同才會刪，不同的就留著）；若還原 exe 修改時間失敗，會保留原始時間的紀錄，下次執行再試；解除安裝是從已安裝的程式執行（安裝資料夾只有系統管理員能寫入，不會在 `%TEMP%` 另存副本），若安裝資料夾與裡面的安裝程式仍在使用中，會由 Windows 在下次重新開機時移除；其他帳號仍指向修正版的捷徑會改回指向 `DynaRun V3.exe`。解除安裝時可選「只移除修正」或「全部移除」（接著用 Dyna Pro 自己的解除安裝程式移除 DynaRun V3）。解除安裝會先要求系統管理員權限，取消該確認的話什麼都不會改變；DynaRun 執行中時會先請你關閉。解除安裝也會移除驅動程式的開始功能表捷徑、尚未完成的驅動程式安裝（排程工作與一次性開機選項）以及 `%ProgramData%\DynaRunFix`（`driver.log` 也一併刪除）。如果 BitLocker 是 DynaRunFix 為了安裝驅動程式而暫停的、且仍在暫停,解除安裝會把它恢復（即使已沒有等待中的工作,使用 `/keep` 時也一樣）。已安裝的驅動程式會保留。舊版曾把 DynaRun 的 COM 註冊複製到全機登錄；解除安裝會保留這些複本及其紀錄（安裝資料夾裡的 `com-copied.txt`、`com-backup`）。
DynaRun 的 exe 與程式內容不會被改動（捷徑檔會備份並還原；唯一新增的檔案是 manifest，而且只在開啟 UTF-8 選項時），也不會修改你的資料檔。

支援 XP、7、10、11（驅動程式選項需要 Windows 10 或 11）。命令列參數：
- `/quiet`：不顯示視窗；只安裝修正，需要已經裝好 DynaRun V3（找不到時以結束代碼 1 結束）。它絕不會安裝驅動程式。
- `/close`：配合 `/quiet`，先關閉執行中的 DynaRun（見下方）。
- `/uninstall`：解除安裝（「程式和功能」執行的也是它）；加上 `/quiet` 時不問任何問題、只移除修正（DynaRun V3 保留），執行中的 DynaRun 只有同時加 `/close` 才會被關閉，使用中的檔案在下次重新啟動時移除；`/notaskbar`、`/noautostart`：不釘選到工作列、不在開機時啟動（見下方）；`/keep`：讓釘選、開機自動啟動和桌面捷徑維持原樣（更新時使用）。
- `/driver`：直接開啟精靈的驅動程式頁面（下載、重新開機、安裝），不安裝 DynaRun 或修正；開始功能表捷徑執行的就是它。
- `/driver-cancel`：撤回已開始的驅動程式安裝（排程工作、一次性開機選項與 BitLocker 暫停）；它不安裝也不移除驅動程式。必須在以系統管理員身分開啟的命令提示字元中執行，否則以結束代碼 1 結束，什麼都不做。
- 內部使用（請勿手動執行）：`/driver-continue`（重新開機後排程工作 `DynaRunFix-DriverContinue` 啟動的）、`/driver-prep`、`/driver-install`、`/machine`、`/full`、`/msi-only`、`/unmachine`、`/checkupdate`、`/update`、`/offer`。

安裝程式的結束代碼：

| 代碼 | 意義 |
|---|---|
| 0 | 完成：安裝、更新或解除安裝已做完，或精靈結束在成功頁（`/driver` 也一樣：驅動程式已安裝、本來就已安裝，或已準備好讓安裝繼續的重新開機）。 |
| 1 | 錯誤，例如：`/quiet` 但找不到 DynaRun V3、讀不到使用者帳戶、安裝資料夾無法寫入、已有另一個安裝步驟在執行，或無法建立視窗。另外：精靈結束在錯誤頁（下載失敗、安裝檔無法安裝，或對 `/driver` 而言，驅動程式沒有安裝、或 Windows 太舊無法自動安裝）。訊息（`/quiet` 時不顯示）與 `setup.log` 會說明原因。 |
| 2 | 已取消：精靈在到達成功頁或錯誤頁之前被關閉、系統管理員確認被拒絕、DynaRun 自己的安裝程式被取消，或解除安裝的詢問回答了「否」。 |
| 3 | 只出現在提升權限的那一步：系統管理員確認是用另一個帳戶回答的，不是這次安裝所屬的帳戶。安裝程式自己會處理（以啟動的使用者身分執行 DynaRun 的安裝程式），所以通常看不到。 |
| 4 | DynaRun V3 自己的安裝程式（或其移除）失敗，或安裝檔無法準備好（複製並檢查）。 |
| 5 | DynaRun V3 已安裝，但之後找不到 `DynaRun V3.exe`。 |
| 6 | DynaRun（或安裝資料夾裡的啟動器／Locale Emulator）正在執行：沒有改變任何東西（見下方）。 |

驅動程式的步驟若有問題，會顯示在精靈與 `driver.log`。

精靈第一頁最多有三個選項，預設都勾選（`/quiet` 預設套用前兩個，可用 `/notaskbar`、`/noautostart` 關閉，第三個則絕不套用）：
- **釘選到工作列。** Windows 7 到 10 直接釘選啟動器捷徑；Windows XP / Vista 放進「快速啟動」。Windows 11 不允許程式自行釘選，
  所以安裝程式改用微軟的[工作列配置原則](https://learn.microsoft.com/windows/configuration/taskbar/pinned-apps)
  （安裝資料夾裡的 `TaskbarLayout.xml`；系統管理員那一步會在 `HKEY_USERS\<使用者>\Software\Policies\Microsoft\Windows\Explorer`
  設定 `StartLayoutFile`／`LockedStartLayout`）：登出再登入後圖示才會出現。組織已經設定配置檔時不會去動它，
  最後一頁會說明手動釘選的方式（在桌面圖示按右鍵 →「顯示其他選項」→「釘選到工作列」）。沒有勾選驅動程式選項時，這段說明在最後一頁（「完成」）；勾選時安裝直接進入重新開機，同樣的說明會出現在重新開機之前的那一頁。
  執行中的 DynaRun 會歸在釘選的圖示下（啟動器捷徑和 DynaRun 使用相同的 AppUserModelID）。Windows 7 到 10 的釘選手法是 Gee Law 描述的方式（[部落格文章](https://geelaw.blog/entries/msedge-pins/)；Firefox 也用同一手法）；不含任何 Mozilla 程式碼。
- **開機時自動啟動 DynaRun：** `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 的 `DynaRunFix` 值在登入時啟動啟動器（`DynaRunFix.exe /autostart`）。
  取消勾選後重新安裝會移除它；解除安裝會移除所有帳號的這個值、執行解除安裝的帳號、安裝時記錄的帳號，以及工作列配置檔仍是我們的其他帳號的工作列原則值（為此會短暫載入它們的登錄 hive），並把釘選的啟動器捷徑改回指向 `DynaRun V3.exe`。同時使用驅動程式選項時，這個值**不會**馬上寫入：先記在 `HKCU\Software\DynaRunFix` 的 `AutostartPending`，
  等驅動程式步驟結束（裝好、失敗、取消或關閉精靈；重新開機還沒發生時不算）才寫入，所以 DynaRun 不會在接續驅動程式安裝的那次登入時啟動。對驅動程式選「略過」時，由安裝本身寫入這個值。
- **同時安裝 Dyna Pro USB 驅動程式（需重新開機一次）。** 只在 Windows 10/11 且尚未安裝驅動程式時顯示。步驟與畫面見 [docs/DRIVER.md](DRIVER.md#繁體中文)。精靈依序會：
  1. （驅動程式的頁面排在安裝之前：授權合約頁之後，或已安裝 DynaRun V3 時，在第一頁之後）電腦上已有 `Dyna Pro USB.zip` 或已解開它的資料夾（它自己先前下載的、安裝程式所在資料夾、下載、桌面、文件;檔名不限）且內容正確就直接使用,否則只從 `dynapro.co.uk` 以 HTTPS 下載（約 2 MB）。檢查的是內容：Windows 會安裝的 18 個檔案（2 個 INF、2 個 catalog、它們所指定的 i386 / amd64 `.sys` 與 `.dll`）各自要符合程式內建的 SHA-256；zip 裡的其他檔案不看也不解開。若不同,警告頁會說明是哪些檔案,並在套件看起來仍是 Dyna Pro 的時候提供「仍要安裝」（不是預設按鈕）；選擇會被記錄；
  2. 你按「安裝並重新開機」（重新開機選單圖片那一頁）時，**只要求一次**系統管理員權限。在這一個提升權限的步驟裡，先安裝修正（需要時先執行 DynaRun V3 自己的安裝；失敗的話就不處理驅動程式），再把安裝程式與解開的驅動程式存放在 `%ProgramData%\DynaRunFix`（只有系統管理員能更改的資料夾）。第 3、4 項也在同一步驟內；
  3. **只有 BitLocker 保護為開啟或無法讀取其狀態時**（或磁碟機仍在加密或解密中，含暫停，且有金鑰保護裝置；Windows 這時會報成「關閉」，但保護裝置已經在作用）才暫停它（對系統磁碟機執行 `manage-bde -protectors -disable` 並加上 `-RebootCount 3`，所以最多 3 次重新啟動後保護會自動恢復；安裝結束或取消時精靈也會讓它恢復，
     並在 `HKLM\SOFTWARE\DynaRunFix` 的值 `DriverBitLockerSuspended` 記住這件事）。暫停期間磁碟機沒有受到保護。保護為關閉且磁碟機已完全加密（你自己暫停的），或本來就沒開的，維持原狀。暫停之後會檢查；如果沒有成功（重新開機選單會要求修復金鑰），精靈會顯示警告頁，說明查詢金鑰的方式（aka.ms/myrecoverykey），讓你選「仍要重新開機」或「稍後」（會撤回所有已設定的項目）；「查詢修復金鑰」按鈕會開啟那個頁面。精靈的第一個驅動程式頁面會在做任何事之前先說明這一點。`driver.log` 與 `bitlocker-before.txt`（`manage-bde -status` 和 `-protectors -get` 的輸出，**寫入前先遮蔽修復密碼**）記錄看到與做了什麼；
  4. 要求 Windows 只在下次開機時顯示進階啟動選單（`bcdedit /set {current} onetimeadvancedoptions on`），並登錄排程工作 `DynaRunFix-DriverContinue`，在你登入後讓精靈繼續；
  5. 上述步驟完成後重新啟動電腦（只裝驅動程式時，從第一頁或 `/driver` 開始，按鈕是「重新開機」，那一步只準備驅動程式）。你在選單按 **7**（停用驅動程式強制簽章）；它只在那一次開機期間關閉；
  6. 登入後安裝驅動程式（`pnputil`），系統管理員帳戶不會再出現 UAC 提示（排程工作以提升的權限執行；標準使用者這時會被要求輸入系統管理員密碼）；Windows 會跳出紅色警告（「Windows 安全性」的「無法驗證發行者」視窗，不是 UAC）兩次（兩個驅動程式檔各一次），每次都請選「仍然安裝此驅動程式軟體」。接著移除工作與開機選項，BitLocker 也恢復。最後一頁提供「開始使用 DynaRun」，以你的一般使用者身分啟動（不是提升權限，儘管那一頁是以系統管理員權限執行）；做不到時會請你改用桌面圖示。
  在驅動程式的頁面按「略過」，就只安裝修正、不裝驅動程式。修正裝好、但驅動程式的準備失敗時，錯誤頁會說明修正已安裝，之後可用開始功能表的捷徑再試驅動程式。
  不繼續的話，請在精靈按「取消」，或執行 `DynaRunFix-Setup.exe /driver-cancel`：它會撤回工作、開機選項與 BitLocker 暫停。沒有勾選該選項（或使用 `/driver`）就不會做這些事。
  其他頁面：「確認安裝檔」頁有「開啟官網」按鈕；在 Windows 8 與更舊的版本（Windows 8 / 8.1、Vista / 7、XP）上，`/driver` 會顯示「驅動程式沒有安裝」，附上該版 Windows 的手動步驟與「開啟手動安裝說明」按鈕；驅動程式已經安裝時會顯示「驅動程式已安裝」。驅動程式搜尋與你的選擇會記錄在 `%TEMP%\DynaRunFix-setup.log`。

**更新。** DynaRun 一律先開啟，更新不會拖慢它。每天一次，DynaRun 啟動後，安裝好的安裝程式會在背景向 GitHub API 查詢本專案的最新版本（不會送出其他資料）。
檢查只用 HTTPS，而且只連 GitHub 自己的主機（API、`github.com` 與 GitHub 的發行檔案主機；每一次重新導向都會檢查，查詢與下載都一樣）。
已知有新版時，下次開啟 DynaRun 會在執行中的 DynaRun 旁詢問（每天最多一次）：「是」＝等您關閉 DynaRun 後安裝，「否」＝略過這個版本（更新的版本仍會再問），「取消」或按視窗的關閉鈕＝明天再問。
「是」會下載該版本的 `DynaRunFix-Setup.exe`（從 `github.com/timliudev/DynaRunFix`；重新導向只能去 GitHub 自己的主機：`github.com`、`api.github.com`、`objects.githubusercontent.com`、`release-assets.githubusercontent.com`），用 GitHub 列出的 SHA-256 核對（安裝前會再向 GitHub 確認一次該版本，下載的檔案也一直保持開啟，無法被調包），DynaRun 關閉後以 `/quiet /keep` 安裝
（一次系統管理員確認；`/keep` 讓釘選、開機自動啟動和桌面捷徑維持原樣）。`/autostart`、`/restart`、`/noupdate`，或設了 `DYNAFIX_NOUPDATE=1` 時，
一律不問更新；接上已在執行的 DynaRun 時也不問。啟動器每次啟動安裝程式前，都會先在 `%TEMP%\dynafix.log` 記下參數與原因。沒有網路、Windows XP / Vista（不支援 TLS 1.2），或沒有更新 KB3140245 的 Windows 7（檢查和下載一樣要求 TLS 1.2 以上）時什麼都不做。

安裝或更新修正時，DynaRun 必須是關閉的：執行中的 DynaRun 會一直用已經載入的舊 `dynafix.dll`（和 Locale Emulator 的 dll）。
安裝程式在改動任何東西之前，會先檢查是否有執行中的 `DynaRun V3.exe`（以及從安裝資料夾執行的 `DynaRunFix.exe`／`LEProc.exe`）。
精靈會顯示「關閉 DynaRun 並安裝」：像按 DynaRun 自己的關閉鈕一樣關掉它，10 秒後還沒結束就強制結束（正在進行的測試會中斷，沒存檔的資料會遺失）。
`/quiet` 則不做任何變更，以結束代碼 **6** 結束；`/quiet /close` 會先用同樣方式關閉 DynaRun 再安裝。
從別的資料夾執行的 `DynaRun V3.exe` 不會用到要更換的檔案，不會擋住安裝。只有同一個登入工作階段、且讀得到路徑的 DynaRun 才會被關閉；別的使用者（或讀不到路徑）的 DynaRun 從這裡關不掉：精靈會說明並且不提供關閉按鈕，`/quiet /close` 也不關任何程式，直接以結束代碼 6 結束，需由該使用者先關閉 DynaRun。以結束代碼 6 結束的原因（哪個程式、處理序與工作階段編號、能否關閉）會寫在安裝資料夾的 `setup.log`；該資料夾尚未建立或沒有系統管理員權限時，寫在 `%TEMP%\DynaRunFix-setup.log`。無人值守的安裝請在沒人使用 DynaRun 時執行 `/quiet /close`。複製完成後，每個安裝的檔案都會讀回來和安裝程式內的版本比對。

如果「文件」放在 OneDrive，而之前留下的 DynaRun 手冊或範例檔是「只在線上」，安裝程式會先讀取這些檔案讓 OneDrive 下載下來；
Windows Installer 自己做不到，會出現錯誤 1305。不會改變檔案內容或 OneDrive 設定。

**DynaRunFix 一律不會以高於「從桌面正常啟動」的權限執行 DynaRun**，而 DynaRun 本來就不需要系統管理員權限。啟動器會在它所啟動的 DynaRun 行程環境的 `__COMPAT_LAYER` 加上 `RunAsInvoker`，其優先於 `DynaRun V3.exe` 的相容性設定「以系統管理員身分執行」(觀察結果，見[下方](#不需要以系統管理員身分執行))；
如果啟動器本身是以系統管理員身分執行的，就先用桌面殼層(檔案總管)的 token 重新啟動自己並結束，所以 DynaRun(和 Locale Emulator)得到的，正是從桌面雙擊會得到的權限：UAC 下是使用者一般、未提升的 token。
殼層的 token 本身已提升時（UAC 關閉，或使用內建的 Administrator：平常的桌面就是完整系統管理員），啟動器沿用它繼續執行並寫入 `dynafix.log`，沒有什麼需要放掉；此時 `dynafix.dll` 會略過圖表語言的轉向（見下方）。
只有完全取不到殼層 token 時，啟動器才會顯示訊息、不啟動 DynaRun。token 是一般權限，或相容性設定被忽略時，會顯示一次性通知（英文或繁體中文；對「下次還要顯示這個通知嗎？」回答「否」即可關閉）。
如果已經在執行的 DynaRun 是以高於啟動器的權限啟動的，啟動器會請你關閉它，再透過 DynaRunFix 重新啟動。
啟動器為 DynaRun 設的相容性變數 `__COMPAT_LAYER=RunAsInvoker`，會由 `dynafix.dll` 在 DynaRun 行程內再清除，DynaRun 的其他子行程不受影響。唯一的例外是首次啟動的 Dyna Pro 設定程式 `Setup_<編號>.exe`：`dynafix.dll` 只在啟動它的那一次呼叫加上 `RunAsInvoker`
（呼叫前設定、呼叫後立刻還原），Windows 的安裝程式偵測就不會對它要求權限；它只寫入 `HKCU\Software\DynaPro`。
請不要直接以系統管理員身分執行 `DynaRun V3.exe`，見[下方](#不需要以系統管理員身分執行)。

#### 安裝 Dyna Pro USB 驅動程式

Dyna Pro 測功機以 USB 連接，Windows 需要 Dyna Pro 的 USB 驅動程式才能與它通訊。**每一台從未連接過 Dyna Pro 的電腦都要安裝一次**。沒有驅動程式時，Windows 會把裝置列在「其他裝置」底下，DynaRun 也連不到測功機。DynaRunFix 不含驅動程式，它來自 Dyna Pro(`dynapro.co.uk`)。完整步驟與畫面見 [docs/DRIVER.md](DRIVER.md#繁體中文)，這裡是精簡版。

**使用安裝精靈(Windows 10 / 11)。**
1. 執行 `DynaRunFix-Setup.exe`，讓第一頁的**同時安裝 Dyna Pro USB 驅動程式（需重新開機一次）**維持勾選。驅動程式已安裝，或 Windows 版本早於 10 時，不會顯示這個核取方塊。DynaRun V3 與修正都已安裝時，第一頁只提供驅動程式。之後也可以用開始功能表捷徑「安裝 Dyna Pro USB 驅動程式」或 `DynaRunFix-Setup.exe /driver` 進行同樣的步驟。
2. 精靈會先找電腦上已有的 `Dyna Pro USB.zip`(檔名不限)或已解壓縮的資料夾:它自己先前的下載、安裝程式所在的資料夾、「下載」(及其下一層資料夾)、桌面與「文件」。找到內容正確的驅動程式檔案就直接使用，不再下載；否則從 `dynapro.co.uk` 下載官方 zip(約 2 MB)。檢查的是檔案內容(SHA-256)，不是檔名。
3. 精靈會顯示啟動選單圖片的那一頁（可以先用手機拍下來）。按「安裝並重新開機」（只裝驅動程式時是「重新開機」）。Windows 要求系統管理員權限時按**是**，**只會問一次**：這一次同時涵蓋修正的安裝和驅動程式的準備，重新開機後不會再問。若 BitLocker 保護已開啟，精靈會暫停它直到驅動程式裝好(這段期間磁碟機沒有受到保護)，並在安裝結束或取消時恢復。在驅動程式的頁面按「略過」，就只安裝修正、不裝驅動程式。
4. 電腦重新開機。在出現的啟動選單按 **7**(停用驅動程式強制簽章)。這只在那一次開機有效。
5. 登入後精靈會自動繼續並安裝驅動程式（標準使用者這時會被要求輸入系統管理員密碼，系統管理員帳戶不會）。Windows 會跳出紅色「Windows 安全性」警告（「無法驗證發行者」，不是 UAC）**兩次**，每次都選「仍然安裝此驅動程式軟體」。最後一頁提供「啟動 DynaRun」。

**出現警告頁時。** 檔案與安裝程式預期的不同(Dyna Pro 可能更新了驅動程式，或檔案已損壞)時，精靈會顯示警告，列出來源、zip 的 SHA-256 與不同的檔案。只有套件看起來仍是 Dyna Pro 的才會提供「仍然安裝」(不是預設按鈕)，且只適用於那一個 zip 或資料夾、那一次安裝。若你對精靈自己找到的副本**取消**警告，之後(包括重試)不會再使用該副本，而是改從 Dyna Pro 官網下載。你用「選擇檔案…」自己選的檔案則一律照原樣檢查。請注意 DynaRunFix 不檢查驅動程式檔案的數位簽章；它比對 SHA-256，並且在「仍然安裝」時檢查 bus INF 是否含有 Dyna Pro 的裝置 ID。是否信任不同的套件由你自己決定。

**錯過 7 或想中止時。** 錯過 7 的話，登入後精靈會告知並提供**重試**。要中止請在精靈按「取消」，或在以系統管理員身分開啟的命令提示字元執行 `DynaRunFix-Setup.exe /driver-cancel`：它會撤回排程工作、一次性開機選項與 BitLocker 暫停(不安裝也不移除驅動程式)。`/quiet` 絕不會安裝驅動程式。

**手動安裝(Windows 8 及更舊的版本也適用)。** 下載 [Dyna Pro USB.zip](https://dynapro.co.uk/Site/graphics/Technical_Info/Software/Dyna%20Pro%20USB.zip) 並解壓縮，以停用驅動程式強制簽章的方式重新開機，再到「裝置管理員」選「更新驅動程式」>「瀏覽電腦上的驅動程式」，指定解壓縮後的資料夾(勾選「包含子資料夾」)。在 Windows 8 及更舊的版本上，精靈的 `/driver` 會顯示「驅動程式沒有安裝」與「開啟手動安裝步驟」按鈕。完整步驟(含 BitLocker 與舊版 Windows 的做法)見 [docs/DRIVER.md](DRIVER.md#繁體中文)。 BitLocker 的路徑尚未在實體電腦上驗證，舊版 Windows 的手動步驟也未經測試。

**確認是否安裝成功。** 以 USB 連接 Dyna Pro 並開機。開啟「裝置管理員」(**Win + X**):裝置會列在「Universal Serial Bus 控制器」底下，名稱為 **DynaPro Dynamometer**，不再在「其他裝置」。驅動程式已安裝時，安裝程式不再提供驅動程式核取方塊，`/driver` 會顯示「驅動程式已安裝」。

**常見問題。**
- 「指定的類別目錄檔案中沒有該檔案的雜湊」:在安裝驅動程式前就正常重新開機過，或沒有按 7。重做「重新開機並按 7」，並在同一次開機內安裝。
- 啟動選單要求輸入 BitLocker 復原金鑰:輸入金鑰(<https://aka.ms/myrecoverykey>)，或正常重新開機，讓精靈暫停 BitLocker(或自行暫停)後再試。
- 中途停止後磁碟機一直沒有受到保護:在精靈按「取消」，或以系統管理員身分開啟 PowerShell 執行 `Resume-BitLocker -MountPoint "C:"`。
- 「其他裝置」底下沒有裝置:檢查 USB 線與 Dyna Pro 是否已開機，拔掉再插一次。
- 精靈說無法準備工作資料夾:重新啟動 Windows 再試。
- 解除安裝 DynaRunFix 不會移除驅動程式。精靈的記錄是 `%ProgramData%\DynaRunFix\driver.log`；檔案搜尋與你的選擇記在 `%TEMP%\DynaRunFix-setup.log`。

#### 手動使用（zip）
Releases 的 zip 內含同樣的檔案（另外還有 `le\`、manifest 與 `LICENSE-miniz.txt`）：把 `DynaRunFix.exe` 和 `dynafix.dll` 放在**同一個資料夾**，從 `DynaRunFix.exe` 啟動
（裝在其他路徑：`DynaRunFix.exe "D:\路徑\DynaRun V3.exe"`）。DynaRun 若已在執行，會直接套用修正。

`%TEMP%\dynafix.log` 出現 `patched THBRes25.dll import of PostMessageA` 即代表生效。

#### DynaRunFix 驗證了什麼、沒驗證什麼

- **DynaRun V3 安裝檔(Dyna Pro 的檔案):** DynaRunFix 只透過 HTTPS 從 `dynapro.co.uk` 下載(憑證無效時會拒絕；每一次重新導向都會再檢查是否仍是 HTTPS 與 `dynapro.co.uk`，導向到其他位置或明文 HTTP 都會被拒絕)，
  使用 TLS 1.2 以上(Windows 7 需要更新 KB3140245)，大小上限 300 MB，
  安裝前會檢查檔案自稱是 DynaRun V3 的安裝檔(Windows Installer 的升級代碼與產品名稱)。Dyna Pro 的安裝檔沒有數位簽章,也沒有公布雜湊值,
  所以 **DynaRunFix 無法證明檔案與 Dyna Pro 發布的完全一致**。請只使用從 Dyna Pro 官方網站取得的檔案。「確認安裝檔」頁（授權合約頁之前）會顯示檔案的來源、MSI 的版本與製造商,
  以及檔案的完整路徑、大小與 SHA-256,讓你與 Dyna Pro 提供的資訊自行比對;安裝程式不會依此阻擋任何事。
- **DynaRunFix 自己的更新:** 安裝檔會用 GitHub 為該版本檔案列出的 SHA-256 核對。每個版本的說明都附有 `SHA256SUMS.txt`(也作為檔案附在版本上),
  版本檔案並附有建置來源證明(由哪個儲存庫、哪個工作流程建置)。自行檢查下載的檔案:
  `gh attestation verify DynaRunFix-Setup.exe --repo timliudev/DynaRunFix`。這只能顯示檔案建置自何處,不代表檔案安全;安裝程式沒有程式碼簽章(見上方 SmartScreen 說明)。
- **Dyna Pro USB 驅動程式(Dyna Pro 的檔案):** 只透過 HTTPS 從 `dynapro.co.uk` 下載(連線與重新導向的檢查和 DynaRun 安裝檔相同),或使用電腦上已有的副本。
  只會解開 Windows 要安裝的 18 個檔案,每個檔案的目的路徑都由程式自己的表格決定(不採用 zip 內的項目名稱),放進只有系統管理員能更改的新資料夾,並有大小上限;路徑上若有連結(link)會被拒絕。複製到那裡之後才比對它們的 SHA-256 與安裝程式內建的值,安裝的就是這些副本(安裝前再檢查一次)。檔案不同的套件,只有在警告頁上由你明確按「仍要安裝」後才會安裝,而且只對那一個 zip、那一次安裝有效。驅動程式本身不是 DynaRunFix 的:它是 Dyna Pro 的套件,DynaRunFix 不簽署也不更改它。

### 首次設定
首次啟動時 DynaRun 會先問語言,再顯示「Dyna Pro系統選擇」(機型與選購功能)。按 **OK** 後 DynaRun 會啟動
`Setup_<編號>.exe`(Dyna Pro 各機型的設定程式);Windows 的安裝程式偵測本來會為它詢問是否允許變更,所以 `dynafix.dll` 以不提升權限的方式啟動它(v1.8.0 起沒有提示;它只寫入 `HKCU\Software\DynaPro`,見 [FIRST_START](FIRST_START.md))。之後 DynaRun 會自動以選好的機型、語言和選購功能
(大氣監測、水冷、冷卻風扇、空燃比分析儀)重新啟動。

沒有修正時,DynaRun 3.26.0 在任何 Windows 版本(包括 XP)都會卡在這裡:設定程式執行完後,選擇視窗不消失、CPU 一核 100%。
強制重新啟動後語言變成英文,選購功能也是關閉的(設定程式執行後,登錄裡的語言值是 `English`,選購功能的啟用旗標是 `0`)。
`dynafix.dll` 只在記憶體中包裝 DynaRun 的 `ShellExecuteA`:等設定程式結束、寫回選的語言、以和「工程模式 → 系統組態設定 → 存檔並離開」相同的值啟用勾選的功能,
再透過 `DynaRunFix.exe /restart <pid>` 重新啟動 DynaRun。每一步都記錄在 `%TEMP%\dynafix.log`(log 不含模組或匯入位址，檔案位置若被換成符號連結或連接點也不會跟隨；每行有日期與時間;每行最多 1000 個字元,更長的以 `...` 結尾)。`dynafix.dll` 與 launcher 可能同時寫這個檔案,它們透過一個具名鎖(`Local\DynaRunFix.dynafix.log`)輪流寫,行不會互相混在一起,但兩者的行會交錯。只有在 dll 或 launcher 啟動時才檢查大小:超過 10 MB 就裁成最新的 8 MB,從完整的一行開始。

本文件不附 DynaRun 自己視窗的截圖。實際畫面:先出現 DynaRun 的語言選擇(英文或繁體中文),接著是它的系統選擇畫面
(選機型與選配模組,例如勾選大氣監測、空燃比分析儀),自動重新啟動後主畫面會是所選的語言。

為選購功能寫入的 `System_Setup` 值,就是使用者自己在 DynaRun 首次啟動的系統選擇畫面上的選擇,用 Process Monitor 觀察(只看登錄檔操作,在全新的 DynaRun 3.26.0 首次啟動上):寫入的啟用旗標,與 DynaRun 自己按「存檔並離開」時對同一組選擇寫入的值相同(型別相同,文字 `-1`)。
DynaRun 自己存檔時另外會調整幾個 AFR 顯示設定,DynaRunFix 不會動它們(在設定對話框存檔一次之前維持預設值)。
不會寫入該畫面讓使用者選的範圍以外的東西。每當 DynaRun 啟動該設定程式時就會寫入(DynaRunFix 自己沒有「第一次啟動」旗標;通常只有第一次啟動會這樣),設定程式以非零結束碼結束時則不寫入(會記 log)。`dynafix.dll` 無法可靠讀取選項按鈕時(例如特殊的佈景主題、縮放或高對比設定),完全不寫入功能值,只記 log（只有明確讀到是綠色、也就是已勾選的按鈕才會寫入，有任何疑慮就什麼都不寫）;
之後可在 DynaRun 的「工程模式 → 系統組態設定」自行開啟功能。

根本原因、登錄值與測試狀態見 [docs/FIRST_START.md](FIRST_START.md)。

### 不需要以系統管理員身分執行

DynaRun 的所有畫面都不需要系統管理員權限，所以啟動器完全比照從桌面正常啟動的權限來啟動它（一般 UAC 桌面下不提升；UAC 關閉或使用內建 Administrator 時桌面本身就是提升的，DynaRun 也一樣，絕不更高）；經由啟動器啟動時，相容性設定「以系統管理員身分執行」不會使它提升。
觀察結果：在開啟 UAC 的 Windows 11 上，對一個名為 `DynaRun V3.exe` 的小型測試程式設定該相容性設定：經由啟動器啟動時，它以未提升的權限執行、沒有 UAC 提示（啟動器會在 log 記下發現了該設定）；直接啟動同一個檔案時，Windows 則會要求提升。啟動器只改變它所啟動行程的環境；如果 Windows 仍要求提升，啟動會失敗（啟動器會說明無法啟動 DynaRun），而且不會有任何東西以提升的權限執行。
也請不要直接以系統管理員身分執行 `DynaRun V3.exe`：DynaRun 的安裝程式把 MSComm、MSCOMCTL、MSHFlexGrid 等 ActiveX 元件只註冊在目前使用者（`HKCU\Software\Classes`），
以系統管理員執行的程式會忽略 HKCU 的 COM 註冊（Windows 的規則），所以以系統管理員執行的 DynaRun 可能在啟動時卡住（「System Initializing. Please Wait. 115」），
並改動它的使用者元件註冊。請改從 DynaRunFix 的捷徑啟動。

### 系統地區是繁體中文仍然亂碼
Windows 以英文安裝、之後才把系統地區改成中文(台灣)時,`HKLM\SYSTEM\CurrentControlSet\Control\FontAssoc\Associated Charset`
可能沒有 `ANSI(00)=YES`。GDI 會用字碼頁 1252 繪製 ANSI 字元集字型裡的 Big5 標籤,變成 `Aw³ï¥B·P...` 這類拉丁字母。
系統地區是繁體中文時,啟動器偵測到這種情況就改用 Locale Emulator 啟動 DynaRun(照常掛上 `dynafix.dll`),
所有視窗(含工具提示和 OCX 控制項)都以 Big5 字碼頁與字元集顯示。其他 DBCS 地區(或找不到 LE 時)則由 `dynafix.dll`
只在 DynaRun 內把 ANSI 字元集字型改用該地區的字元集建立(`DYNAFIX_CHARSET`)。不改登錄。
系統地區根本不是中文時無法顯示 DynaRun 的中文,請把「非 Unicode 程式的語言」設為「中文(繁體,台灣)」;中文版安裝程式偵測到時會在最後一頁提示（沒有勾選驅動程式選項時在最後一頁「完成」；勾選時在重新開機之前的那一頁）。

### 開啟系統 UTF-8 選項時中文亂碼
「地區 → 系統管理 → 變更系統地區設定 → Beta:使用 Unicode UTF-8 提供全球語言支援」開啟時,系統 ANSI 字碼頁是 65001,VB6 程式的中文會變亂碼。
把 [`manifest/DynaRun V3.exe.manifest`](../manifest/DynaRun%20V3.exe.manifest) 複製到 `DynaRun V3.exe` 旁邊(Program Files 需要系統管理員權限),
DynaRun 就會改用系統地區的舊字碼頁(zh-TW 是 950),其他程式維持 UTF-8。Windows 會快取「這支 exe 沒有 manifest」的判斷,
複製完要用系統管理員 PowerShell 更新一次 exe 的修改時間(重點是修改時間「有改變」,不是新的值):
`(Get-Item 'C:\Program Files (x86)\Dyna Pro Dynamometers\DynaRun V3.exe').LastWriteTime = Get-Date`。
`DynaRunFix-Setup.exe` 會替你做這件事,之後把 exe 的修改時間改回原值,並短暫以暫停狀態建立 `DynaRun V3.exe` 行程後立刻結束它,讓 Windows 讀取 manifest(不會執行任何 DynaRun 程式碼)。
之後 `%TEMP%\dynafix.log` 會顯示 `ansi-codepage=950`。選單、文字框、訊息框、圖表都會正常;
標籤、圖形按鈕和部分標題是經由 Windows 用系統字碼頁繪製的,仍會亂碼,這部分由 DynaRunFix 透過
[Locale Emulator](https://github.com/xupefei/Locale-Emulator)(LGPL-3.0)啟動 DynaRun 來解決:

- 把 LE 放在 `DynaRunFix.exe` 旁邊的 `le` 資料夾(`DynaRunFix-Setup.exe` 會自動安裝,release zip 也已附;要自己下載就執行 `tools\fetch-le.ps1 -OutDir <資料夾>\le`)。
  不需要安裝:不加右鍵選單、不寫入 GAC。`le\LEConfig.xml` 是 DynaRunFix 的 zh-TW 設定。
- 系統字碼頁是 UTF-8、系統地區是繁體中文(舊字碼頁 950),且有 `le\LEProc.exe` 時,`DynaRunFix.exe` 會透過 LE 啟動 DynaRun,並立刻掛上防閃爍修正;
  系統地區是繁體中文但 FontAssoc 缺 `ANSI(00)=YES` 時也一樣(見[上方](#系統地區是繁體中文仍然亂碼))。其他情況照舊直接啟動。
- manifest 也要保留:兩者一起才會全部正常(只有 manifest:標籤亂碼;只有 LE:選單和訊息框亂碼)。
- 透過 LE 時字型會以 Big5 字元集建立,Windows 會把 Arial 等字型換成細明體;`dynafix.dll` 改用 Microsoft JhengHei UI 繪製
  (見[字型](#字型全部使用-windows-介面字型))。

已在 Win11(開啟 UTF-8 選項)+ DynaRun 3.26.0 驗證:選單、對話框、標籤、按鈕、看圖程式、中文資料夾裡的檔案都正常。
關閉 UTF-8 選項也能全部正常,不需要 LE。沒開 UTF-8 選項的電腦完全不需要這一步。`DynaRunFix-Setup.exe` 只在開啟該選項時才加上 manifest(解除安裝時移除)。

### 字型:全部使用 Windows 介面字型

DynaRun 的表單指定 Arial、MS Sans Serif、Times New Roman 或新細明體,其中一部分又被 Windows 替換,同一個視窗裡混著好幾種字型。
現在 `dynafix.dll` 讓 DynaRun 的文字都用 Windows 該語言對話框所用的字型,依 DynaRun 執行時的字碼頁決定(透過 Locale Emulator 時為 Big5):

| 字碼頁 | 字型(取第一個已安裝的) |
|---|---|
| 950 繁體中文 | Microsoft JhengHei UI、Microsoft JhengHei |
| 936 簡體中文 | Microsoft YaHei UI、Microsoft YaHei |
| 932 日文 | Yu Gothic UI、Meiryo UI |
| 949 韓文 | Malgun Gothic |
| 其他(英文等) | 系統訊息字型:Segoe UI(XP 為 Tahoma) |

- 所有情況都套用:原生繁中 Windows、後來才改地區(Locale Emulator 或 `DYNAFIX_CHARSET`)、英文 Windows。
- 替換的字型:Arial、Times New Roman、MS Sans Serif、Microsoft Sans Serif、MS Shell Dlg、Tahoma、Verdana、Segoe UI、
  細明體/新細明體,以及其他語言的舊介面字型(SimSun、MS UI Gothic、Gulim 等)。符號、數位、等寬字型(Wingdings、七段顯示字型、
  Courier New 等)與窄體/粗黑字型(Arial Narrow、Arial Black)不變。
- 範圍包含 DynaRun 本身、OCX 元件(工具列、清單、表格、頁籤、圖表)、VB 字型、THBResize 與通用控制項(工具提示),也包含系統預設 GUI 字型。
  選單、訊息框和開檔對話框由 Windows 繪製,本來就是介面字型。
- 介面字型行高較高,16 px 以上的字縮為 90%,多行標籤才放得下(`DYNAFIX_FONT_SCALE`,50–150)。介面字型的行高也會以 Arial 的行高(1.12 em)回報給 VB,
  多行標籤的行距維持原本的樣子,字本身大小不變(`DYNAFIX_FONT_LINEH`,單位 1/1000 em,`0` 為關閉)。
- `DYNAFIX_FONT=<字型>` 指定其他字型,`DYNAFIX_FONT=off`(或 `none`、`0`)保留 DynaRun 原本的字型。只影響 DynaRun 的程序,不改任何檔案或設定。
- DynaRun 執行中會載入、卸載 OCX 元件。dynafix.dll 依 Windows 的載入通知,在模組卸載時忘記它,再次載入時重新處理字型(記錄檔 module tracking: ...)。

狀態:已在 Win11 + DynaRun 3.26.0 驗證(原生繁中,以及英文安裝、地區改為繁中並經 Locale Emulator)。

### 沒有系統管理員權限時,圖表右鍵選單是英文

繪圖元件(Gigasoft ProEssentials,`Pesgo32e.ocx` 與 `PEGRP32E.dll`)的右鍵選單語言來自語言資源檔 `PEGRC32D.dll`,元件在載入時依檔名尋找。
DynaRun 的安裝程式把所選語言的副本放在 `%APPDATA%\Dyna Pro Dynamometers\Dyna Run V3\Language\<語言>\`,
而 DynaRun 每次啟動都會試著把它複製到 Windows 系統資料夾(`SysWOW64`);沒有系統管理員權限時被拒絕,選單就一直是英文。

`dynafix.dll` 改把元件的要求導向使用者目錄中的那個檔案,不會寫入系統資料夾。

- 只針對 `PEGRC32D.dll` 這個檔名(單純檔名,或系統資料夾的完整路徑)。導向後的載入一律只載入資源:會載入程式碼的要求(沒有資料檔／影像資源旗標)不會被導向;資源要求則不論元件傳入什麼旗標,
  `dynafix.dll` 都以 `LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE` 載入使用者目錄中的檔案,因此該檔案不會被當成程式碼執行。
- 路徑由 `dynafix.dll` 以使用者設定檔資料夾與 `HKCU\Software\DynaPro\Operation_Data\Default_Language`(必須是單純的資料夾名稱)組成,
  不取自要求內容;必須是一般檔案,且設定檔資料夾以下的路徑沒有重新分析點。任何一項不符就照原要求繼續。不鎖定檔案雜湊或版本:
  就是 DynaRun 安裝程式放在那裡的檔案。
- DynaRun 以系統管理員身分執行時不處理(那時 DynaRun 自己的複製可成功),無法判斷時也不處理(失敗時採保守做法);`DYNAFIX_PEGRC=0` 可關閉。
- 元件在載入過程中就會索取該檔,因此在模組載入通知時(元件進入點執行之前)修改它的匯入表。DynaRun 自己的複製動作(及其被拒絕)維持原樣。
- `dynafix.log` 會有 `PEGRC32D: ... redirected to ...`(或 `not redirected, <原因>`)。

### 狀態列被工作列遮住
DynaRun 主視窗是最大化但沒有標題列的視窗,Windows 會把這種視窗放大到整個螢幕,所以放在底部的工作列會蓋住
DynaRun 的狀態列(紀錄模式、日期、時間)。這是 DynaRun 原本的行為,XP 也一樣;工作列放在側邊時就看得到。

`dynafix.dll` 讓 DynaRun 把主螢幕的工作區(扣掉工作列的範圍)當成螢幕大小:在 DynaRun 載入第一個表單之前,
把 VB6 執行階段在記憶體中的螢幕大小值調整成工作區大小;這些值是以 Microsoft `MSVBVM60` 程式碼中的一段指令位元組特徵找出來的(只在記憶體中),只有它們等於真實螢幕大小時才修改,找不到特徵就不動它(並記 log)。DynaRun 的主視窗和整個儀表版面就會依工作區大小排出來,
所有元件位置都正確(觀察結果)。執行階段在載入第一個表單時取用這個值,所以 launcher
在 DynaRun 一啟動就掛上修正(經過 Locale Emulator 時,LEProc 放在 job 物件中,DynaRun 一建立就會立即通知)。
另外仍保留把最大化視窗限制在工作區內的保險。工作列自動隱藏時該邊留 2 像素。`DYNAFIX_FULLSCREEN=1` 保留原本的
全螢幕視窗;`DYNAFIX_WORKAREA=clip` 只把視窗限制在工作區內,不調整螢幕大小值。生效時 log 會出現 `screen-size values adjusted (work area): 2048x1280 -> 2048x1232`。

修正前工作列會蓋住 DynaRun 的狀態列(紀錄模式、日期、時間);修正後狀態列在工作列上方
(在 Win11、2560x1600、125% 觀察到;不附截圖)。

狀態:已在 Win11(1920x1080、工作列在底部)及 Win11 2560x1600 / 125% 的電腦上驗證,兩者都經 Locale Emulator。XP、工作列自動隱藏、多螢幕尚未測試。

### DynaRun 的畫面顯示不正常

如果 DynaRun 的畫面顯示不正常，關掉 DynaRun 再開一次即可。

* **DynaRun 開著時改了螢幕解析度（或縮放比例、接上其他螢幕）：** DynaRun 只在啟動時依當下的螢幕大小排一次版面（觀察結果）。
  之後再改，部分控制項會被拉伸，儀表仍維持原本的大小，畫面就會偏掉；改回原本的解析度就恢復。原版 DynaRun 也一樣，
  改完請重開 DynaRun。
* **裝好 DynaRunFix 後第一次開：** v1.3.0 以前可能排得太大（右邊儀表被切掉）：啟動器在 DynaRun 開始跑之後才載入 `dynafix.dll`，
  而剛安裝的 dll 第一次載入很慢。v1.3.0 起在 DynaRun 啟動前就先載入，log 會有 `windows DynaRun had before dynafix: 0`。
  詳見 [docs/FIRST_START.md](FIRST_START.md)。
* **開機登入時** Windows 可能還在準備桌面：登入時的自動啟動會等到工作列出現、且螢幕大小連續 3 秒沒變才啟動（最多等 60 秒；
  `%TEMP%\dynafix.log` 會有 `launcher: autostart waited ...`）。

### 連不上 Dyna Pro 時 DynaRun 沒有任何提示

裝置打不開(已被其他程式開啟,例如第二個 DynaRun 或記錄軟體;同一時間只能有一個程式使用它)
或沒有回應(線材、電源)時,DynaRun 約 13 秒內都不顯示任何東西,之後才跳出「USB資料連線錯誤」
(中止/重試/略過);按「略過」後仍會在背景持續嘗試連線。

如果這台電腦從來沒有連接過 Dyna Pro,原因可能是缺少 USB 驅動程式:裝置管理員會把裝置列在「其他裝置」。
請用安裝精靈(第一頁的選項,或 `DynaRunFix-Setup.exe /driver`)或手動安裝驅動程式,見 [docs/DRIVER.md](DRIVER.md#繁體中文)(Windows 8 與更舊的版本:[舊版 Windows](DRIVER.md#舊版-windows))。

`dynafix.dll` 在記憶體中加上診斷(把 `dynapro.dll` 的 `CreateFileA` 匯入表項目導向自己的函式來觀察開啟裝置,結果原樣傳回,不修改任何檔案):

* `%TEMP%\dynafix.log` 會記下 DynaRun 連線嘗試結果的每次變化、開啟裝置失敗(`error 32` = 被其他程式占用,`5` = 存取被拒)
  與 Windows 查得到的占用程式名稱和 PID,以及連線連續失敗 2 秒後的一行。
* 畫面右下角出現小提示:「Dyna Pro 裝置正被其他程式使用(名稱, PID)」或「無法連線到 Dyna Pro。請檢查 USB 線與裝置電源。」
  不搶焦點、不卡住 DynaRun,10 秒後或點一下就消失,並有頻率限制(「被占用」每分鐘一次,線材提示每次連續失敗一次)。
* `DYNAFIX_CONNHINT=0` 關閉提示(log 照寫)。`DYNAFIX_CONNHINT_MS=<毫秒>` 設定重連失敗多久才提示線材(預設 2000,500-60000)。
* 占用程式的名稱來自 Windows 的重新啟動管理員(Restart Manager),它對檔案比對裝置路徑熟:查不到時提示就不含名稱。


### USB 裝置不再回應時 DynaRun 卡死(選用,實驗性)

**實驗性、預設關閉、尚未用實體硬體驗證。** `DYNAFIX_USBFIX` **預設關閉**(須自行用環境變數開啟,不設定就完全不改變行為)。
只有在接受下列取捨時才使用。

用途:量測中 Dyna Pro 突然不再回應時,如果 DynaRun 呼叫 `dynapro.dll` 的 `DPSetData` 一直不返回,DynaRun 的視窗可能永遠沒有反應。
裝置不再回應後 `DPSetData` 呼叫是否永遠不返回,是假設,並未實測。

在啟動 DynaRunFix 前設定 `DYNAFIX_USBFIX=1`(系統或使用者環境變數,或在啟動它的命令列先 `set`):

* `dynafix.dll` 把 DynaRun 的 mode 3 `DPSetData` 呼叫放到輔助執行緒執行,DynaRun 的視窗執行緒則以監看計時等待。
  只有在呼叫約 2 秒後仍卡住,**而且** `DPConnStatus()` 回報連線已斷時,`DPSetData` 才對 DynaRun 傳回 0。預期 DynaRun 會把 0 視為「沒有寫入」並執行自己的重新連線邏輯(`DPConnect`);
  這個反應只是預期,並未用實體硬體驗證。只要連線仍回報為正常,不論多久,呼叫都繼續執行。其他 mode 的呼叫直接在呼叫端的執行緒交給原函式。

事實的來源:這裡所依據的 `DPSetData`、`DPConnStatus`、`DPConnect` 行為,來自對 DynaRun 自己的呼叫做 API 邊界記錄(DynaRun 3.26.0)。
Dyna Pro 沒有公開這些函式的文件;只是假設的部分,文中都會說明。不檢查版本。

運作方式(直白地說):不修改磁碟上任何 Dyna Pro 的檔案。`dynafix.dll` 呼叫 `dynapro.dll` 匯出的 `DPSetData`、`DPConnStatus`,
並且在 DynaRun 執行期間、只有設了開關時,於記憶體中為 `DPSetData` 加上監看:呼叫若沒有及時返回,DynaRun 會取回控制權,該次呼叫被放棄(呼叫本身留在輔助執行緒上結束)。
監看無法設定時不會啟用(log 會有 `DPSetData watchdog off: ...`)。

取捨:

* 監看等待期間 DynaRun 的視窗沒有反應、也不處理訊息;約 2 秒後才恢復,而且只有在那時連線回報為斷線的情況下。
* 呼叫 `dynapro.dll` **並非**嚴格一次一個:從第 2 秒起 `dynafix.dll` 自己每 100 毫秒呼叫一次 `DPConnStatus`,被放棄的 `DPSetData` 還卡著時,
  DynaRun 自己的 `DPConnStatus`、`DPConnect` 呼叫也會同時在 `dynapro.dll` 內執行(`DPConnect` 原樣放行,因為擋住它就等於擋住讓被放棄的呼叫結束的重新連線)。
  `dynapro.dll` 在這種重疊下的行為尚未驗證。
* mode 3 的呼叫端會排隊:一次只有一個進入監看。
* 被放棄的呼叫會留在輔助執行緒上,`dynafix.dll` 讓該執行緒的等待每 250 毫秒返回,但只有對**事件(Event)**控制代碼的等待才會替換成逾時(`WAIT_TIMEOUT`),其他種類的控制代碼照常等待;
  這樣呼叫才能在 DynaRun 重新連線後結束(log 的 `the abandoned call returned after N ms`)。
* 被放棄的呼叫尚未返回期間,**所有** `DPSetData` 呼叫(不論 mode)都會被丟棄:直接傳回 0、不進入 `dynapro.dll`,以避免 `DPSetData` 呼叫互相重疊;DynaRun 會把這些呼叫視為「沒有寫入資料」。
* 被放棄的呼叫可能永遠不返回。此時之後所有的 `DPSetData` 呼叫都傳回 0,直到重新啟動 DynaRun;丟棄達 300 次以上且已過 20 秒後,`dynafix.dll` 會顯示一次性提示,建議重新啟動 DynaRun。
* 假設,照實說明:`dynapro.dll` 能接受 `DPSetData` 由輔助執行緒而不是 DynaRun 自己的執行緒呼叫(未經驗證),以及 `p` 是三個值(這是在 DynaRun 的呼叫中觀察到的)。複製的剛好是三個值,放進四周有緩衝空間的暫存區,不會蓋到 `dynafix.dll` 的其他資料。
* DynaRun 傳入 `p` 的三個值會複製給輔助執行緒,呼叫正常結束時再複製回去,所以呼叫端看到的資料與沒有這個修正時相同;被放棄之後就不再碰 `p`。

`dynafix.log` 會出現 `DPSetData watchdog on (usbfix), N hook(s) set`、`usbfix: DPSetData blocked ...`、
`the abandoned call returned after N ms` 與被丟棄呼叫的次數。

### 部分 .Dpr 打不開(File Run Properties 全空、沒有曲線)
放在 OneDrive 且設成「永遠保留在此裝置」的檔案帶有屬性 `0x80000`(`FILE_ATTRIBUTE_PINNED`),這是 Win7/XP 沒有的屬性。
DynaRun 對這種檔案什麼都不做,也不顯示錯誤:Process Monitor 可以看到它只查了檔案屬性,完全沒開檔讀取。跟檔案內容無關(例如檔案中儲存的某個日期值):
內容完全相同、只是沒有這個屬性的副本就能正常開啟。

`dynafix.dll` 只在記憶體中處理:`MSVBVM60.DLL` 和 `scrrun.dll` 匯入的檔案屬性 API 回傳時,會去掉雲端相關位元
(pinned、unpinned、recall-on-open、recall-on-data-access)。不修改任何檔案,也不改 OneDrive 設定。
生效時 log 會出現 `cleared cloud attributes 00080020 in GetFileAttributesA`。已在 Win11 + DynaRun 3.26.0 驗證。

### 安裝程式出現「此安裝程式使用 VBScript 自訂動作」警告(Win11 25H2)
Win11 25H2 以精靈方式執行 Dyna Pro 的 `Setup.msi` 時會出現這個警告。微軟預計約 2027 年預設停用 VBScript,屆時直接點兩下的互動式安裝可能無法完成。
`DynaRunFix-Setup.exe` 以基本介面(`msiexec /qb!`)執行安裝檔、不顯示精靈頁面,只有自己直接執行 Dyna Pro 的 `Setup.msi` 時才會遇到。
若 VBScript 已被停用,請先到「選用功能」重新啟用再執行該安裝程式,或向 Dyna Pro 索取不需要 VBScript 的安裝檔。時程與細節見 [docs/VBSCRIPT.md](VBSCRIPT.md)。

### 建置
安裝 Visual Studio 2019 以上(含 C++ 桌面開發),執行 `build.cmd`,產物在 `build\`:`DynaRunFix.exe`、`dynafix.dll` 和 `DynaRunFix-Setup.exe`(內含啟動器、dll、manifest、Locale Emulator 和 miniz 授權;版本字串用環境變數 `DRF_VERSION` 指定)。第一次建置會透過 `tools\fetch-le.ps1` 把 Locale Emulator 下載到 `build\le`(需要 PowerShell 與網路,會檢查 SHA-256)。安裝程式用 [miniz](https://github.com/richgel999/miniz) 3.1.2(MIT 授權,在 `third_party/miniz`)解開 DynaRun 安裝檔。32 位元、不依賴 C 執行階段、XP 以上皆可執行。

dynafix 所有的匯入掛鉤(字型、文字度量、檔案屬性、`GetProcAddress`、THBResize)都是依匯入的名稱(同時走匯入名稱表與位址表)找出匯入表欄位,不是依欄位目前存放的位址,
並且會接在其他工具(記錄工具、相容性 shim)已經安裝的掛鉤之後,所以不論載入順序都有效(鏈結完成後欄位又被另一個不同的外來掛鉤佔住時,不會修補並記錄下來,不會繞過那個工具)。
來自 kernel32/kernelbase/gdi32/gdi32full/user32 與 `api-ms-win-*` API 集合的匯入都會比對;沒有匯入名稱表的模組退回舊的位址比對。
`dynafix.log` 會出現 `patched ... (by name)`、`... already held a hook: chained onto it`、無法修補的欄位的 `... not patched: ...`,以及每個模組一行 `import patch <module> [<table>]: N patched, M failed`。

`tests\build-shexec-tests.cmd` 會建置並執行單元測試(首次啟動設定程式的路徑比對 `src\setup_match.h`、`DYNAFIX_*` 開關、`dynapro.dll` 檔名的解析與 `RunAsInvoker` 相容層字詞 `src\dfutil.h`、啟動器的部分功能),全數通過時結束碼為 0。`tests\build-tests.cmd` 建置 log 測試程式，`tests\build-hardening-tests.cmd` 建置並執行安裝程式強化測試,`tests\build-driver-tests.cmd` 建置並執行驅動程式測試(見 `tests\README.md`)。
診斷用的 `DYNAFIX_STATS=1`:每 5 秒 `dynafix.log` 會有一行 `stats(5s)`(只在有事發生時),內容是表單 `WM_SIZE` 的次數、其中重複的次數、THBResize `0x591` 訊息被放行或略過的次數,以及表單被顯示或隱藏的次數。
啟動之後這些行就停止,表示防護已經結束了隱藏/顯示迴圈。

### 診斷工具:msgspy
`tools/msgspy` 是找出原因時用的視窗訊息記錄工具,只提供原始碼(`build.cmd` 不會建置,也不在 Release 內)。用 `cl tools\msgspy\msgspy.c` 建置 `msgspy.exe`,用 `cl /LD tools\msgspy\msgspy_dll.c` 建置 `msgspy.dll`(要和 `msgspy.exe` 放在一起)。DynaRun 要先在執行中;它掛到 DynaRun 的 GUI 執行緒,把相關訊息(含解碼後的 `WM_SIZE`、`WM_WINDOWPOSCHANGING/CHANGED`、`WM_GETMINMAXINFO`、`WM_CREATE`、`WM_SETTEXT`)寫到執行檔旁的 `msgspy_<電腦名稱>.log`。

隱私:這個 log 含有電腦名稱(檔名也有)以及 DynaRun 視窗的標題與文字,分享前請先檢查。

```
msgspy.exe [秒數] [要送出的訊息(16 進位)]    # 秒數預設 10
msgspy.exe 3          # 記錄 3 秒
msgspy.exe 4 591      # 記錄 4 秒,並對最大化的表單送一次 0x591
```

### 授權
MIT,見 [LICENSE](../LICENSE)。

### 第三方授權

安裝程式與 release zip 附有其中第三方軟體的授權文字:

- `LICENSE.txt`:DynaRunFix 本身(MIT)。
- `LICENSE-miniz.txt`:[miniz](https://github.com/richgel999/miniz)(MIT),安裝程式用它解開 DynaRun 安裝檔。
- `le\COPYING` 與 `le\COPYING.LESSER`:GNU GPL v3 與 LGPL v3,即 [Locale Emulator](https://github.com/xupefei/Locale-Emulator) 的授權(未修改的執行檔,由 DynaRunFix 當成獨立程式啟動)。
- `le\THIRD-PARTY.txt`:Locale Emulator 的著作權聲明、所取自的 release 的 SHA-256,以及對應原始碼的位置。

Windows 7 到 10 的工作列釘選手法由 Gee Law 描述([部落格文章](https://geelaw.blog/entries/msedge-pins/));不含該文章或 Mozilla 的任何程式碼。
