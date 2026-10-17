// The setup wizard: one window, one question at a time, for users who never installed software.
//
//   DynaRun V3 installed:  Hello -> Install (one UAC prompt) -> Done
//   not installed:         Hello -> get "Dyna Pro Dynamometers.zip" (a network check first; found in Downloads/Desktop, or
//                          downloaded from dynapro.co.uk with pause / resume; one error page, with the site's download page
//                          one click away and "Choose file...")
//                          -> password -> extract -> check it is DynaRun (by the MSI's UpgradeCode, never by
//                          file name) -> Dyna Pro's license -> Install (msiexec /qb and the fix, one UAC prompt)
//                          -> Done -> "Start DynaRun"
// Long steps run on a worker thread and report back with WM_PROGRESS / WM_JOBDONE.
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include "drf_app.h"
#include "drf_package.h"
#include "drf_driver.h"
#include "version.h"

#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)

enum { ID_TITLE = 100, ID_BODY, ID_EDIT, ID_RICH, ID_CHECK, ID_PROG, ID_STATUS, ID_EXTRA, ID_SECOND, ID_PRIMARY, ID_KEEPUP, ID_PIN, ID_AUTO, ID_DRV, ID_IMG, ID_INFO, ID_THIRD };
enum { P_HELLO, P_FETCH, P_FAIL, P_PASSWORD, P_EXTRACT, P_CHECK, P_LICENSE, P_RUNNING, P_INSTALL, P_DONE, P_ERROR,
       P_DRV_INTRO, P_DRV_PRESS7, P_DRV_HASH, P_DRV_BDE, P_DRV_PREP, P_DRV_REBOOT, P_DRV_RETRY, P_DRV_WARN, P_DRV_INSTALL, P_DRV_DONE, P_DRV_ERROR,   // driver.c
       P_COUNT };
enum { JOB_ACQUIRE, JOB_EXTRACT, JOB_INSTALL_ALL, JOB_INSTALL_FIX, JOB_DRV_PREP, JOB_DRV_INSTALL, JOB_DRV_CANCEL, JOB_COUNT };   // the order is not used anywhere: JOBS[] says what each job is
#define WM_PROGRESS (WM_APP + 1)
#define WM_JOBDONE  (WM_APP + 2)
#define WM_HINT     (WM_APP + 3)                    // the DynaRun update hint is known (wParam: 0 none, 1 newer version, 2 the site's file differs)
#define TIMER_RUNNING 1

static int g_page, g_job, g_dpi, g_lastpct = -1, g_pending, g_lastrun;   // g_pending: install job waiting for DynaRun to close
static volatile LONG g_cancel;
static install_opts g_opts;                             // what the pages ask for (starts as the command line gave it); only the UI thread changes it
static install_opts g_jobopts;                          // the copy a job runs with (start_job): the worker never reads g_opts while the pages may change it
static install_result g_instres;                        // what the install job reports besides its code (the pin hint); written by the worker, read once the job is done
// The process exit code of wizard(): what the user's run came to. It is RC_CANCEL while nothing is decided (the window is closed before the end), and set
// where the outcome is decided: a page that says "it worked" (Done, the driver is installed, the restart for the driver) makes it RC_OK, an error page
// (the acquire flow's, "Something went wrong", "The driver was not installed") RC_FAIL. set_page takes it back to RC_CANCEL first, so it always belongs to
// the page on screen; wherever the window ends (a button, Esc, Alt+F4, a job that ends while the window is closing) wizard() returns this value.
static int g_result = RC_CANCEL;
static BOOL g_close_asked;                              // g_opts.close was set by the "Close DynaRun and install" button: it covers that one install job only (job_done takes it back)
static BOOL g_busy, g_closing, g_installed, g_pkg_ours, g_pkg_found, g_status_err, g_badfile, g_nolicense;   // g_nolicense: the setup's license text could not be read
static int g_nfound;   // packages pkg_find_local has handed us so far
static HANDLE g_thread;
static WCHAR g_pkg[MAX_PATH], g_msi[MAX_PATH], g_ver[64], g_note[1100];
static zipent g_zip;
static char g_pw[256];
static char *g_rtf;
static WCHAR g_info[2200];   // what the license page shows about the package (path, size, SHA-256, MSI version/manufacturer)
static DWORD g_neterr, g_total;
static BOOL g_paused, g_pause_req;                      // download paused (worker ended, .part kept) / pause asked, worker still stopping
static int g_hint;                                       // DynaRun update hint (pkg_update_hint): 0 none, 1 a newer version is published, 2 the site's zip differs from the last download
static WCHAR g_hint_inst[40], g_hint_new[40];            // the installed and the published version (hint 1)
// Which way through the pages this run takes. One value; it replaces the three flags (the driver ticked on the first page, the driver-only first page, the
// /driver and /driver-continue command line) whose combinations were never all possible. It changes only in start (wizard), go_install, skip_driver and the
// "Reinstall the fix..." link.
//   FLOW_MAIN              the fix (and, when DynaRun is not installed, DynaRun itself): the first page, no driver in it. The default.
//   FLOW_MAIN_WITH_DRIVER  the driver is ticked on the first page: its package is fetched and checked BEFORE the install, and the one elevated stage installs the
//                          fix and prepares the driver (one UAC prompt). The user can still skip it (back to FLOW_MAIN).
//   FLOW_DRIVER_ONLY       only the driver is the job: /driver (straight to the driver pages), or DynaRun and its fix are installed (this version) and only the
//                          driver is missing (the first page offers just that, and "Reinstall the fix..." turns it into FLOW_MAIN).
//   FLOW_DRIVER_CONTINUE   /driver-continue, the sign-in task after the restart: the driver pages that follow the restart.
enum { FLOW_MAIN, FLOW_MAIN_WITH_DRIVER, FLOW_DRIVER_ONLY, FLOW_DRIVER_CONTINUE };
static int g_flow = FLOW_MAIN;
static int g_inst_job;                                    // the install job (JOB_INSTALL_ALL / JOB_INSTALL_FIX) that waits for the driver package in FLOW_MAIN_WITH_DRIVER
static BOOL g_fix_done;                                   // progress, not flow: FLOW_MAIN_WITH_DRIVER, the fix is installed (the driver step after it may still have failed)
static BOOL g_checking, g_marquee, g_drv_allow;          // the non-pausable "checking" phase / marquee bar (size unknown) / the user chose "install anyway" for a zip with another hash
static int g_drv_how;                                    // DRV_SRC_*: the driver zip was downloaded, or found on this computer (nothing downloaded)
static WCHAR g_drv_hex[65];                              // SHA-256 and size of a driver zip that is not the pinned file (the warning page shows them)
static ULONGLONG g_drv_size;
static WCHAR g_drv_rep[1500];                           // the driver files that differ from the pinned ones, one per line (the warning page lists them)
static int g_drv_bl = BL_UNKNOWN;                              // BitLocker hint for the press-7 page, asked in the worker (WMI can take seconds): 1 on, 0 off, -1 cannot be told
// What the acquire flow needs to know about the thing it gets: the DynaRun V3 setup and the Dyna Pro USB driver go through the same pages (looking for
// a copy on this computer, download with pause / resume, one error page); only these texts, the "partial download" test, the search and the check differ.
typedef struct {
    const WCHAR *name_en, *name_zh;          // "the DynaRun V3 setup" / "DynaRun V3 安裝檔"
    const WCHAR *get_en, *get_zh;            // what to click on the website
    const WCHAR *pick_en, *pick_zh;          // what "Choose file..." takes
    BOOL (*partial)(DWORD *have);            // a .part that can be continued
} acq_desc;
static const acq_desc ACQ_SETUP = { L"the DynaRun V3 setup", L"DynaRun V3 安裝檔", L"\"Dyna Run V3 Software\"", L"「Dyna Run V3 Software」",
                                  L"the zip (or the MSI) you downloaded", L"你下載的 zip（或 MSI）", pkg_download_partial };
static const acq_desc ACQ_DRIVER = { L"the Dyna Pro USB driver", L"驅動程式", L"\"Dyna Pro USB Drivers\"", L"「Dyna Pro USB Drivers」",
                                   L"Dyna Pro USB.zip, or ftdibus.inf in the folder where it is unpacked", L"Dyna Pro USB.zip，或已解開它的資料夾裡的 ftdibus.inf", driver_partial };
static const acq_desc *g_acq = &ACQ_SETUP, *g_dlacq;     // the thing being got now; the one g_dltotal belongs to
static BOOL g_acq_fresh;                                 // "Download new version" (update hint): no package already on this computer is taken, it is downloaded
static BOOL g_acq_local_ok;                              // the DynaRun search may hand out another local package (pkg_find_local remembers 8 files)
#define ACQ_NAME(d) T((d)->name_en, (d)->name_zh)
static DWORD g_dltotal, g_dlhave;                       // size of the file being downloaded (server's, 0 = not known) and bytes received so far
static HFONT g_font, g_big;
static HBRUSH g_white, g_grey;
// the Dyna Pro USB driver (driver.c): g_drv_avail: the check box on the first page is offered (Windows 10+, driver not there yet); g_drv: ticked
static BOOL g_drv_avail, g_drv, g_drv_have, g_drv_noreboot;   // g_drv_have: already installed; g_drv_noreboot: Windows would not restart by itself
static WCHAR g_drvzip[MAX_PATH];                              // the checked zip ("" = the staged copy, after a restart)
static HBITMAP g_bmp[2], g_shown;                             // the two screenshots (decoded once) and the scaled one in the picture control
static int g_bw[2], g_bh[2], g_img, g_shown_img = -1, g_shown_w, g_shown_h;
static WCHAR pgbuf[2600];             // the text of the page being built (one page at a time, on the UI thread)
// What one page is (PAGES[], below): its builder, what its buttons do, what closing the window does.
typedef struct { UINT key; int id; BOOL (*when)(void); } hotkey;     // Alt+key clicks the control id (when is NULL, or says the shortcut is on)
enum { PF_NOCLOSE = 1 };
typedef struct {
    void (*enter)(void);
    void (*primary)(void);
    void (*second)(BOOL button);
    void (*third)(void);
    void (*extra)(void);
    void (*close)(void);
    const hotkey *keys;
    DWORD flags;
} page_desc;
static const page_desc PAGES[P_COUNT];
static void layout(void);
static void set_page(int p);
static void place(int id, int x, int y, int w, int h);
#define S(x) MulDiv((x), g_dpi, 96)
#define H(id) GetDlgItem(g_hwnd, (id))

// The rest of this file is split by flow into src\wizard\*.h. They are fragments of THIS translation unit (static functions and file-local state refer to
// each other), not headers and not separate .c files: build.cmd and the tests compile only wizard.c. A fragment uses what the ones before it define; what a
// flow calls in a later one is declared here.
static BOOL can_skip_driver(void);                  // driver.h
static void skip_driver(void);
static void acq_driver_result(int rc);
static void choose_driver_source(void);
static void pin_hint(WCHAR *b);                     // main.h
static void acp_warning(void);
static BOOL can_start_dynarun(void);
static void start_job(int job, int page);
static void install(int job);
static void go_install(int job);
static void on_close(void);
static void end_wizard(void);

#include "wizard/layout.h"                  // the window: UI helpers, screenshots, controls, layout
#include "wizard/acquire.h"                 // getting the DynaRun setup (and the driver package): download, password, check, license
#include "wizard/driver.h"                  // the driver pages and jobs
#include "wizard/main.h"                    // the main flow, the jobs, the page table, the window procedure, wizard()
