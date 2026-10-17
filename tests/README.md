# Log tests

Checks the code that writes `%TEMP%\dynafix.log` (`logf` in `src\dynafix.c`, `llog` and `trim_log` in
`src\launcher.c` / `src\dynafix.c`). `logtest.c` `#include`s the real sources, so the shipped code is what runs.
Not part of `build.cmd` or the release.

```
build.cmd                                  (once: creates build\version.h)
tests\build-tests.cmd                      (builds build\tests\logtest.exe and logtest_l.exe, with /GS /RTC1)
powershell -File tests\run-tests.ps1       (needs python; writes only to a temp folder, -Work <dir> to choose it)
```

- **Long strings:** 10 to 5000 character strings, a 4000 character `Setup_x.exe` path through the ShellExecute hook
  (the shell call is stubbed, nothing is launched) and the cloud-attribute path. Every line must be at most 1000
  bytes, end in CRLF, long ones end in `...`, and the line after each must be intact.
- **Concurrent writers:** 4 processes (2 using `logf`, 2 using `llog`) with 2 threads each write numbered lines
  to one log that starts at over 10 MB, while the trim runs repeatedly. Every line must be well formed, and per writer
  the kept lines must be one unbroken run ending at the last line written (only the oldest lines may be cut by a trim).
- **System DLL loading:** `logtest.exe loadsys` checks that `load_sys` (`src\dynafix.c`) loads a system DLL by name and refuses a missing one.

To run the tests against another revision of the sources: `tests\build-tests.cmd <outdir> <dynafix.c> <launcher.c> [/DOLD_SRC]`
(`/DOLD_SRC` for a revision without `w2u`), then `run-tests.ps1 -Bin <outdir> [-Mode old]`.

# First-run helper path tests

`tests\build-shexec-tests.cmd` builds and runs `tests\test_shexec_match.c` (the path matching in `src\setup_match.h`), `tests\test_dfutil.c` (the `DYNAFIX_*` switch parsing and the `dynapro.dll` file name match in `src\dfutil.h`) and `tests\test_launcher.c` (the launcher's registry path read with long, mistyped and unterminated values, the same-session DynaRun lookup and its session rule, the token elevation query including an unknown state; it writes only `HKCU\Software\DynaRunFixTest-<pid>`); exit code 0 = all passed. The log tests above only check that an over-long Setup_ path is passed through and nothing is logged for it.

# Installer hardening tests

`tests\build-hardening-tests.cmd` (after `build.cmd`) builds and runs two programs that compile the shipped sources into themselves:
`test_hardening.c` (`src\setup.c`, `src\package.c`) and `test_update.c` (`src\update.c`, with its own `version.h` in `tests\fakever`).
`src\setup.c` is one compilation unit: it `#include`s the topic files in `src\setup\*.h`, so a test that includes `setup.c` gets all of them. What a test does not compile in (the wizard, the update check, `driver.c` in `test_hardening.c`) is stubbed in the test itself.
They create only `%TEMP%\drf-sec-<pid>`, `%TEMP%\drf-upd-<pid>` and `HKCU\Software\DynaRunFixTest-<pid>` and delete them again; HKLM and the real
DynaRun and DynaRunFix installs are not touched (the only look at the machine is read-only: the ACL of an installed DynaRun folder is printed).
The held-open case in `test_driver.c` keeps a file in an old folder open without sharing, so `make_base` cannot remove it; as in normal use, `delete_tree` then asks Windows to delete that locked file at the next restart (a harmless pending-delete entry for a file in the test folder). The test closes its handle and removes the folder itself.
`test_update.c` also checks the order of release numbers (1.9.9 < 1.10.0, no downgrade). The log line writer of the setup is checked not to follow hard links or symbolic links. They also cover the rules for where a redirect may lead (`pkg_redirect_target`, used by the update check and the downloads). They cover the exe modification time that is briefly changed (restore, crash recovery, 2 s rule, one stage at a time), `delete_tree`
with junctions, the administrators-only staging folder, the SID check, the re-check of the noted release, the log lock, that a file held open for
hashing cannot be changed, renamed or deleted but still starts, and the COM registration path rules and copy logic between two temporary keys
(junctions, relative/UNC paths, dot-dot, writable folders, `.ocx` only in system folders, AppID/TreatAs/ProgID verbs): the installer no longer
copies COM registrations, that code is only kept under test (`tests\promote_com.h`, included by `test_hardening.c` after `src\setup.c`). Exit code 0 = all passed.

# Driver tests

`tests\build-driver-tests.cmd` (after `build.cmd`) builds and runs `test_driver.c`, which compiles `src\setup.c` and `src\driver.c` into itself and links `build\package.obj`: the INF matcher (ANSI, UTF-16, other versions and the stock FTDI INF), the code integrity classifier, the scheduled task XML (escaping, size limits, well-formed UTF-16), the tool paths (`Sysnative`), the `pnputil` exit codes, the zip entry name rules and the multi-entry zip extraction (`zip_extract_all` in `tests\zip_extract_all.h`, test-only: nothing shipped unpacks a whole zip; zips made in the test: zip-slip, absolute and drive paths, device names, encrypted/unsupported/damaged entries, a real deflate stream), the shared package machinery of `package.c` (`pkg_*`: table-driven staging from a zip or an unpacked folder, size caps, links refused, target preparation), the DynaRun update hint (version comparison and the parsing of Dyna Pro's software page, on made-up pages; no network), and `pkg_find_dir_in`, `driver_layout_ok` and `driver_check` on synthetic folders and zips (made-up files only: a package whose files differ from the pinned ones, files missing, a bus INF without the device ID, not a zip, no such path). It creates only `%TEMP%\drf-drv-<pid>`, `%TEMP%\DynaRunFix\drvcheck` (the scratch folder of the driver check; `test_files()` uses and deletes it) and `%TEMP%\DynaRunFix\drvdigest.tmp` (`driver_check` on a folder) and starts no system tool except `powershell` to parse the task XML. `test_job_table()` reads `src\wizard.c` and its fragments `src\wizard\*.h` (one text, in the order they are included) relative to the current folder (the build script changes to the repo root) and, if it is not run from the repo root, only prints an info line and skips that check. Set `DRF_DRIVER_ZIP` to the real `Dyna Pro USB.zip` to check the pinned SHA-256 and the extraction against it. Exit code 0 = all passed.

Result codes (`RC_*`, `PK_*`, `DRV_E_*`) are named in `src\errors.h` (the tests lock their values); the module headers are `src\drf_*.h` (`setup.h` includes them all); `src\applog.h` is shared by the launcher and `update.c`.

`tests\VM_CHECKLIST.md` lists the manual checks these tests cannot reach (UAC, the wizard, the restart for the driver, the network). Run it in a throw-away VM, never on the development host.

The CI workflow (`.github/workflows/build.yml`) runs `build-tests.cmd`, `build-shexec-tests.cmd`, `build-hardening-tests.cmd` and `build-driver-tests.cmd` in the `build` job (read-only token); the release is made by a separate job. The scripts use a release Visual Studio; set `DRF_ALLOW_PRERELEASE=1` locally to allow a pre-release one.
