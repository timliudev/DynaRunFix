// driver.c: the Dyna Pro USB driver step.
#pragma once
#include "drf_app.h"
#include "drf_package.h"

/* driver.c: the Dyna Pro USB driver (FTDI 2.08.24 with the Dyna Pro device id; its INF files are not in the signed catalogs that ship with them, so
   Windows installs it only with driver signature enforcement off for one start: see docs/DRIVER.md) */
#define DYNAPRO_USB_ZIP  L"https://dynapro.co.uk/Site/graphics/Technical_Info/Software/Dyna%20Pro%20USB.zip"
#define DRIVER_DOC_URL   L"https://github.com/timliudev/DynaRunFix/blob/main/docs/DRIVER.md"
enum { DRV_CI_ENFORCED, DRV_CI_OFF, DRV_CI_UNKNOWN };   // driver signature enforcement now
BOOL driver_supported(void);                // Windows 10 (build 10240) or later: older systems install the driver by hand
BOOL driver_installed(void);                // a Dyna Pro ftdibus package is in the driver store (%windir%\INF\oem*.inf)
enum { DRV_SRC_DOWNLOADED = 0, DRV_SRC_LOCAL = 1, DRV_ZIPDIFF = 2, DRV_SRC_DIR = 4, DRV_SRC_PICKED = 8 };   // *how of driver_fetch: where the zip came from, and (flag) the zip as a whole is not the official one
// The zip: found on this computer (DRV_SRC_LOCAL; DRV_SRC_DIR: an already unpacked folder, `zip` is its path and hex a digest of the files' hashes) or downloaded. The check is of its CONTENT: the files Windows installs (driver.c, DRV_FILES).
// PK_OK: those files are the pinned ones (DRV_ZIPDIFF: although the zip as a whole differs, e.g. re-packed). DRV_W_HASH: some differ or are missing but
// the package passed the sanity checks - the user is asked ("install anyway"); hex / size: the SHA-256 and size of the zip, rep: the files that differ.
// DRV_E_HASH: not a usable driver package. Other PK_*: the download failed. cb((DWORD)-1, 1) tells the "checking" phase (after the download, not pausable).
int driver_fetch(WCHAR *zip, progress_fn cb, volatile LONG *cancel, DWORD *err, int *how, WCHAR *hex, ULONGLONG *size, WCHAR *rep, int repcch);
void driver_skip_local_other(void);          // the user declined a local copy that differs: the next driver_fetch skips such copies and downloads
BOOL driver_layout_ok(const WCHAR *dir);   // the folder holds ftdibus.inf with i386 and amd64 folders (an unpacked driver)
int driver_check(WCHAR *path, int *how, WCHAR *hex, ULONGLONG *size, WCHAR *rep, int repcch);   // a zip or an unpacked folder the user chose: the same check as for a copy that was found (PK_OK / DRV_W_HASH / DRV_E_HASH); path becomes the folder
const WCHAR *driver_pinned_sha256(void);    // the SHA-256 of the zip this program knows
void driver_forget(const WCHAR *zip);       // deletes the zip when it is our own cache file in %TEMP%\DynaRunFix (a refused download), never a file of the user's
BOOL driver_partial(DWORD *have);           // a partial driver download that driver_fetch can continue; *have: bytes so far
int driver_prep(const install_opts *o, const WCHAR *zip, const WCHAR *accept);         // elevated: stage the driver, arm the restart menu and the task (zip "-": the staged copy); 0 or DRV_E_*, 2 = UAC cancelled
int driver_prep_stage(const WCHAR *sid, const WCHAR *zip, const WCHAR *accept);   // elevated, inside the install stage (setup.c): driver_prep for sid, no further elevation
int driver_bitlocker(void);                // BitLocker protection of the system drive, not elevated: 1 on, 0 off / none, -1 cannot be told (needs administrator rights)
int driver_install(const install_opts *o);                   // elevated: pnputil, then clean up. 0 or DRV_E_*, 2 = UAC cancelled
int driver_cancel(const install_opts *o);                    // elevated: task, restart menu and BitLocker suspension back. 2 = UAC cancelled
BOOL driver_reboot(void);                   // restarts Windows (not elevated)
int driver_ci_state(void);                  // DRV_CI_*
// The driver's entries in the verb table (setup.c). The elevated three refuse a process that is not elevated (RC_FAIL).
int cmd_driver(cmdline *c);                 // /driver: the wizard at the driver pages
int cmd_driver_continue(cmdline *c);        // /driver-continue: the same, after the restart
int cmd_driver_prep(cmdline *c);            // /driver-prep <sid> <zip> [/unverified <sha256>]
int cmd_driver_install(cmdline *c);         // /driver-install
int cmd_driver_cancel(cmdline *c);          // /driver-cancel
void driver_machine_stage(const WCHAR *installed_setup);   // elevated install: the Start menu shortcut "Install Dyna Pro USB driver"
void driver_machine_uninstall(void);        // elevated uninstall: the shortcut, a waiting task, BitLocker resumed if we suspended it, the staged files (an installed driver is left); call before the DynaRunFix key is deleted
