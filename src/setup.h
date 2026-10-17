// Shared declarations of DynaRunFix-Setup.exe (setup.c, wizard.c, package.c).
#pragma once
#include <windows.h>
#include <wininet.h>
#include "errors.h"                         // RC_*, PK_*, PKC_*, DRV_E_* / DRV_W_*, FIX_DONE: the result codes

extern BOOL g_zh, g_quiet, g_close;         // g_close: close a running DynaRun before installing (/close)
extern BOOL g_pin, g_autostart;                         // pin DynaRun to the taskbar / start it at sign-in (default on; /notaskbar, /noautostart)
extern BOOL g_autostart_later;                          // the driver follows: the sign-in start is registered once the driver steps are over (autostart_flush)
extern BOOL g_keep;                                     // /keep: leave the pin, the sign-in start and the desktop shortcut as they are
extern int g_pinresult;                                 // after the install: 0 not asked, 1 pinned, 2 appears after the next sign-in, 3 could not pin
extern HWND g_hwnd;                                     // wizard window, owner of UAC prompts
extern WCHAR g_self[MAX_PATH], g_dir[MAX_PATH], g_launcher[MAX_PATH], g_exe[MAX_PATH];
#define T(en, zh) (g_zh ? (zh) : (en))
enum { DRV_MODE_NONE, DRV_MODE_STANDALONE, DRV_MODE_CONTINUE };
extern int g_drvmode;                                   // the wizard starts at the driver pages: /driver, /driver-continue

/* setup.c */
void zero(void *p, SIZE_T n);
void *alloc(SIZE_T n);
void release(void *p);
BOOL exists(const WCHAR *p);
int msg(const WCHAR *text, UINT flags);
BOOL is_admin(void);
void user_log(const WCHAR *line);           // one line in setup.log (administrator, install folder there) or %TEMP%\DynaRunFix-setup.log
DWORD os_build(void);                       // Windows build number (real, not the compatibility-shimmed one)
BOOL user_sid(WCHAR *out, int cch);
BOOL fix_current(void);                      // the fix is installed and is the version of this setup (uninstall entry, launcher and dll there)
BOOL locate_dynarun(void);                  // fills g_exe from the registry or the default folders; no UI
void start_dynarun(void);                  // the wizard's "Start DynaRun": through our shortcut, like a double click
int install_fix(void);                      // DynaRun is installed: elevated stage + user shortcuts. RC_OK, RC_CANCEL
int install_all(const WCHAR *msi);          // runs the DynaRun MSI, then install_fix's work. RC_OK, RC_CANCEL
// The same, and the driver's preparation (driver_prep) in the SAME elevated stage: one UAC prompt for both. zip/accept as for driver_prep.
// RC_OK; RC_CANCEL; another small code: the fix failed (the driver was not touched); >= DRV_E_HASH: the fix is installed, the driver step gave this code (DRV_E_*, DRV_W_BDE).
int install_fix_driver(const WCHAR *zip, const WCHAR *accept);
int install_all_driver(const WCHAR *msi, const WCHAR *zip, const WCHAR *accept);
// shared with driver.c
void delete_tree(const WCHAR *dir, const WCHAR *keep, int depth);   // a link is removed itself, never followed; in-use files go at the next restart
BOOL same_file(const WCHAR *a, const WCHAR *b);
int run_elevated(const WCHAR *args, const WCHAR *exe_path);   // "<exe or this program> <args>" with one UAC prompt; its exit code, RC_CANCEL = cancelled
BOOL valid_user_sid(const WCHAR *sid);      // a SID of a real account with a profile here
BOOL path_user_writable(const WCHAR *path, const WCHAR *sid);   // could ordinary users (or the user sid) change what is at path (or is that unknown)
BOOL enable_privilege(const WCHAR *name);
enum { RUN_DYNARUN = 1, RUN_LAUNCHER = 2, RUN_LEPROC = 4, RUN_CANCLOSE = 8, RUN_NOCLOSE = 16 };
int running_programs(void);                 // RUN_* bits of the programs that hold files the setup replaces. RUN_CANCLOSE: one of them can be closed
                                            // by the setup; RUN_NOCLOSE: one cannot (another session, or its exe path cannot be told)

/* update.c */
int update_check(void);                     // /checkupdate: notes a newer release in HKCU\Software\DynaRunFix; no UI
int update_offer(void);                       // /offer: the launcher started DynaRun; asks beside it, installs once it is closed
int update_run(const WCHAR *args);          // /update <launcher args>: asks, installs a noted release, starts the launcher

void autostart_flush(void);                 // registers the sign-in start that user_stage deferred (HKCU AutostartPending); no-op otherwise

/* wizard.c */
int wizard(void);


/* package.c */
#define DYNAPRO_PAGE L"https://dynapro.co.uk/Software_Release.htm"
#define DYNAPRO_HOST L"dynapro.co.uk"
// Hosts a download from GitHub may be served from (releases/download/... redirects to the asset CDN): every hop is checked against them.
#define GITHUB_HOSTS L"github.com;api.github.com;objects.githubusercontent.com;release-assets.githubusercontent.com"
#define DYNAPRO_ZIP  L"https://dynapro.co.uk/Site/graphics/Technical_Info/Software/Dyna%20Pro%20Dynamometers.zip"
typedef void (*progress_fn)(DWORD done, DWORD total);

typedef struct {
    WCHAR path[MAX_PATH];
    DWORD data;                             // offset of the (encrypted) entry data
    DWORD csize, usize, crc, flags, method, dostime;
} zipent;

BOOL pkg_find_local(WCHAR *out);            // a DynaRun MSI, or a zip with an .msi inside, in Downloads/Desktop/Documents or next to us
BOOL pkg_find_zip_if(DWORD maxsize, BOOL (*accept)(const WCHAR *path), WCHAR *out);   // the first zip (at most maxsize bytes) in the same places that accept() approves
extern void (*g_pkg_log)(const WCHAR *line);   // set to log the folders the zip searches look in (NULL: silent)
BOOL pkg_find_dirs(BOOL (*accept)(const WCHAR *dir), WCHAR *out);   // the first folder accept() approves, in the same places (two levels down in Downloads)
BOOL pkg_find_dir_in(const WCHAR *root, int depth, BOOL (*accept)(const WCHAR *dir), WCHAR *out);   // accept() for root and the folders up to `depth` levels below it
BOOL pkg_find_zip_named(const WCHAR *prefix, WCHAR *out);   // the newest zip whose name starts with prefix in the same places
void pkg_reject(const WCHAR *path);       // not the DynaRun setup after all: pkg_find_local skips it
int pkg_download(WCHAR *out, progress_fn cb, volatile LONG *cancel, DWORD *err);
BOOL pkg_url_ok(const WCHAR *u, const WCHAR *hosts);   // https and on one of the ';'-separated hosts (NULL = any)
HINTERNET pkg_open_following(HINTERNET in, const WCHAR *url, const WCHAR *host, const WCHAR *hdr, DWORD *status, DWORD *err, BOOL *refused);   // redirects followed by hand, each hop checked
BOOL pkg_redirect_target(const WCHAR *cur, const WCHAR *loc, const WCHAR *host, WCHAR *out, int cch);   // where a redirect from cur to loc goes, if allowed
BOOL pkg_partial(const WCHAR *name, const WCHAR *etagval, DWORD *have);   // a partial download pkg_fetch can continue (.part and its ETag are there); *have: bytes so far
BOOL pkg_download_partial(DWORD *have);     // ... of the DynaRun zip
enum { NET_UNKNOWN = 0, NET_NO = 1, NET_YES = 2 };
HINTERNET pkg_net_open(DWORD connect_ms, DWORD receive_ms);   // a session with these timeouts, TLS 1.2+ (NULL: none)
int ver_compare(const WCHAR *a, const WCHAR *b);              // <0 / 0 / >0: a older / the same / newer than b (not versions: 0)
BOOL pkg_page_version(const char *html, DWORD n, WCHAR *out, int cch);   // the DynaRun V3 version the Dyna Pro software page names
BOOL dynarun_version(WCHAR *out, int cch);                    // the installed DynaRun V3's version (Windows Installer record), FALSE when not installed that way
int pkg_update_hint(WCHAR *installed, WCHAR *latest, int cch, void (*log)(const WCHAR *line));   // 0 nothing, 1 the site names a newer version, 2 the site's zip differs from the last download
int pkg_net_state(void);                    // NET_NO: Windows says it is disconnected (the download is not even tried); NET_YES / NET_UNKNOWN: try
int pkg_fetch(const WCHAR *url, const WCHAR *name, const WCHAR *etagval, const WCHAR *host, WCHAR *out, progress_fn cb, volatile LONG *cancel, DWORD *err);
int zip_open(const WCHAR *zip, zipent *e);  // first *.msi entry
// Every entry of the central directory, in order: fn(name as stored (not 0-terminated), its length, entry, ctx). fn returns FALSE to stop.
// PK_OK: all visited; PK_CANCEL: fn stopped it; PK_FORMAT: not a (supported, no Zip64) zip; PK_IO. e->data is valid (local header read).
typedef BOOL (*zip_visit_fn)(const char *name, DWORD nlen, const zipent *e, void *ctx);
int zip_walk(const WCHAR *zip, zip_visit_fn fn, void *ctx);
// An entry name that may be created below a folder: relative, '/' separators, no "..", ".", empty, "x:" / backslash / control or
// reserved characters, no device names, no trailing dot or space. *isdir: the name ends in '/'.
BOOL zip_name_safe(const char *name, DWORD nlen, BOOL *isdir);
// The files of a package named in a pinned table (the driver's 18 files): safe unpacking / copying and checking (package.c). Used by the driver; the DynaRun setup
// uses pkg_prepare_target / pkg_extract_checked. Nothing a zip says about a path is used, destinations are built from the table; links are refused; sizes capped.
typedef struct { const WCHAR *rel; DWORD size; const WCHAR *sha; } pkg_file;   // name below the package folder ('/' separators), size, SHA-256 (lower case)
#define PKG_MAXF 24
typedef struct { int ok, differ, missing; BYTE st[PKG_MAXF]; WCHAR hex[PKG_MAXF][65]; } pkg_check;   // st: 0 ok, 1 differs, 2 missing; hex: what the file hashes to
int pkg_entry_index(const pkg_file *t, int n, const char *name, DWORD nlen);   // the table file a zip entry name is (after a folder boundary, any case), or -1
BOOL pkg_is_plain_dir(const WCHAR *p);
BOOL pkg_file_path(WCHAR *out, const WCHAR *dir, const WCHAR *sub, const pkg_file *t, int i);
BOOL pkg_dirs_plain(const WCHAR *dir, const WCHAR *top, const pkg_file *t, int n);
int pkg_zip_has(const WCHAR *zip, const pkg_file *t, int n, DWORD maxfile);   // how many table files the zip has (central directory only), -1: not a zip
int pkg_stage_files(const WCHAR *zip, const WCHAR *dir, const WCHAR *top, const pkg_file *t, int n, DWORD maxfile, DWORD maxtotal, volatile LONG *cancel, int *missing);
int pkg_stage_from_dir(const WCHAR *src, const WCHAR *dir, const WCHAR *top, const pkg_file *t, int n, DWORD maxfile, DWORD maxtotal, int *missing);
void pkg_check_files(const WCHAR *dir, const WCHAR *sub, const pkg_file *t, int n, WCHAR (*expect)[65], pkg_check *c);
BOOL pkg_prepare_target(const WCHAR *path);   // free a path that is going to be written (a plain file is removed, a link removed itself, a folder refused)
int pkg_extract_checked(const zipent *e, const char *pw, const WCHAR *out, DWORD maxsize, progress_fn cb, volatile LONG *cancel);   // zip_extract with a size cap and a free target
BOOL zip_password_plausible(const zipent *e, const char *pw);
int zip_extract(const zipent *e, const char *pw, const WCHAR *out, progress_fn cb, volatile LONG *cancel);
BOOL pkg_sha256(const WCHAR *path, WCHAR *hex, ULONGLONG *size);   // lower-case hex (65 WCHARs) and size of a file, read-only
void msi_info(const WCHAR *msi, WCHAR *version, WCHAR *manufacturer, int cch);   // ProductVersion / Manufacturer (Windows Installer API, read-only; "" when absent)
char *msi_license_rtf(const WCHAR *msi);    // RTF of the setup's license page (heap) or NULL
enum { MSI_NONE, MSI_OTHER, MSI_DYNARUN };
int msi_check(const WCHAR *msi, WCHAR *version, int cch);   // MSI_NONE: not an MSI at all. DynaRun: by UpgradeCode
WCHAR *msi_external_files(const WCHAR *msi);   // multi-string (heap) of cabinet files kept outside the MSI, or NULL
int run_msiexec(const WCHAR *msi);          // msiexec exit code
BOOL dynarun_product(WCHAR *out);           // product code of the installed DynaRun V3 (39 chars)
int remove_dynarun(void);                   // Dyna Pro's uninstall of DynaRun V3; msiexec exit code, -1 none
void pkg_temp_dir(WCHAR *out);              // %TEMP%\DynaRunFix (created)
int pkg_prepare_documents(const WCHAR *msi);   // download OneDrive "online-only" copies the setup overwrites

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
int driver_prep(const WCHAR *zip, const WCHAR *accept);         // elevated: stage the driver, arm the restart menu and the task (zip "-": the staged copy); 0 or DRV_E_*, 2 = UAC cancelled
int driver_prep_stage(const WCHAR *sid, const WCHAR *zip, const WCHAR *accept);   // elevated, inside the install stage (setup.c): driver_prep for sid, no further elevation
int driver_bitlocker(void);                // BitLocker protection of the system drive, not elevated: 1 on, 0 off / none, -1 cannot be told (needs administrator rights)
int driver_install(void);                   // elevated: pnputil, then clean up. 0 or DRV_E_*, 2 = UAC cancelled
int driver_cancel(void);                    // elevated: task, restart menu and BitLocker suspension back. 2 = UAC cancelled
BOOL driver_reboot(void);                   // restarts Windows (not elevated)
int driver_ci_state(void);                  // DRV_CI_*
int driver_verb(int argc, WCHAR **argv);    // command line verbs; DRV_NOVERB when argv[1] is none of ours, else the exit code
void driver_machine_stage(const WCHAR *installed_setup);   // elevated install: the Start menu shortcut "Install Dyna Pro USB driver"
int drv_file_count(void);                   // the files that get installed (pinned: name below "Dyna Pro USB\", size, SHA-256)
int drv_file_find(const WCHAR *rel);        // index of that name (any case, / or \), or -1
BOOL drv_file_info(int i, const WCHAR **rel, DWORD *size, const WCHAR **sha);
BOOL drv_file_match(int i, DWORD size, const WCHAR *sha);   // is a file of this size and SHA-256 the pinned file i?
int drv_check_dir(const WCHAR *dir, int *differ, int *missing);   // how many pinned files below dir\Dyna Pro USB\ are right; the others differ / are missing
BOOL drv_inf_matches(const BYTE *data, DWORD n);          // pure helpers, unit-tested (tests\test_driver.c)
BOOL drv_ci_enforced(DWORD code_integrity_options);
BOOL drv_bde_suspend(int protection_status, int conversion_status, int key_protectors);   // BDE_PS_* (driver.c), conversion status and protector count (-1 unknown): suspend BitLocker? A volume mid-conversion with protectors is suspended although it reads "off"
BOOL drv_bde_suspend_now(int protection_status, int conversion_status, int key_protectors, BOOL suspended_by_us);   // ... at prep time: also when our own flag says we suspended it earlier (a retry)
int drv_task_xml(WCHAR *out, int cch, const WCHAR *sid, const WCHAR *exe);
BOOL drv_sys_tool(const WCHAR *windir, BOOL wow64, const WCHAR *tool, WCHAR *out, int cch);
BOOL drv_pnputil_ok(DWORD code);
BOOL drv_pnputil_args(WCHAR *out, int cch, const WCHAR *inf);
void driver_machine_uninstall(void);        // elevated uninstall: the shortcut, a waiting task, BitLocker resumed if we suspended it, the staged files (an installed driver is left); call before the DynaRunFix key is deleted
