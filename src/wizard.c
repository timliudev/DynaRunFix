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
#define ACQ_NAME(d) T((d)->name_en, (d)->name_zh)

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

// Everything the wizard knows about the run, in one place. OWNED BY THE UI THREAD: only the window procedure and what it calls read or write it.
// A worker thread never touches it: what a job needs is copied into g_in by start_job, what it produces is written to g_out, and job_done merges
// g_out into this struct after the thread has been joined (see JOBS[].merge in main.h). The hint thread does the same with g_hint_out and WM_HINT.
typedef struct {
    int page, pending, lastrun;                             // pending: install job waiting for DynaRun to close
    install_opts opts;                                      // what the pages ask for (starts as the command line gave it); only the UI thread changes it
    install_result instres;                                 // what the install job reports besides its code (the pin hint); merged from g_out when the job is done
    // The process exit code of wizard(): what the user's run came to. It is RC_CANCEL while nothing is decided (the window is closed before the end), and set
    // where the outcome is decided: a page that says "it worked" (Done, the driver is installed, the restart for the driver) makes it RC_OK, an error page
    // (the acquire flow's, "Something went wrong", "The driver was not installed") RC_FAIL. set_page takes it back to RC_CANCEL first, so it always belongs to
    // the page on screen; wherever the window ends (a button, Esc, Alt+F4, a job that ends while the window is closing) wizard() returns this value.
    int result;
    BOOL close_asked;                                       // opts.close was set by the "Close DynaRun and install" button: it covers that one install job only (job_done takes it back)
    BOOL busy, closing, installed, pkg_ours, pkg_found, status_err, badfile, nolicense;   // nolicense: the setup's license text could not be read
    int nfound;                                             // packages pkg_find_local has handed us so far
    HANDLE thread;
    WCHAR pkg[MAX_PATH], msi[MAX_PATH], ver[64], note[1100];
    zipent zip;
    char pw[256];
    char *rtf;
    WCHAR info[2200];                                       // what the license page shows about the package (path, size, SHA-256, MSI version/manufacturer)
    DWORD neterr, total;
    BOOL paused, pause_req;                                 // download paused (worker ended, .part kept) / pause asked, worker still stopping
    int hint;                                               // DynaRun update hint (pkg_update_hint): 0 none, 1 a newer version is published, 2 the site's zip differs from the last download
    WCHAR hint_inst[40], hint_new[40];                      // the installed and the published version (hint 1)
    int flow;                                               // FLOW_*
    int inst_job;                                           // the install job (JOB_INSTALL_ALL / JOB_INSTALL_FIX) that waits for the driver package in FLOW_MAIN_WITH_DRIVER
    BOOL fix_done;                                          // progress, not flow: FLOW_MAIN_WITH_DRIVER, the fix is installed (the driver step after it may still have failed)
    BOOL checking, marquee, drv_allow;                      // the non-pausable "checking" phase / marquee bar (size unknown) / the user chose "install anyway" for a zip with another hash
    int drv_how;                                            // DRV_SRC_*: the driver zip was downloaded, or found on this computer (nothing downloaded)
    WCHAR drv_hex[65];                                      // SHA-256 and size of a driver zip that is not the pinned file (the warning page shows them)
    ULONGLONG drv_size;
    WCHAR drv_rep[1500];                                    // the driver files that differ from the pinned ones, one per line (the warning page lists them)
    int drv_bl;                                             // BitLocker hint for the press-7 page, asked in the worker (WMI can take seconds): 1 on, 0 off, -1 cannot be told
    const acq_desc *acq, *dlacq;                            // the thing being got now; the one dltotal belongs to
    BOOL acq_fresh;                                         // "Download new version" (update hint): no package already on this computer is taken, it is downloaded
    BOOL acq_local_ok;                                      // the DynaRun search may hand out another local package (pkg_find_local remembers 8 files)
    DWORD dltotal, dlhave;                                  // size of the file being downloaded (server's, 0 = not known) and bytes received so far
    // the Dyna Pro USB driver (driver.c): drv_avail: the check box on the first page is offered (Windows 10+, driver not there yet); drv: ticked
    BOOL drv_avail, drv, drv_have, drv_noreboot;            // drv_have: already installed; drv_noreboot: Windows would not restart by itself
    WCHAR drvzip[MAX_PATH];                                 // the checked zip ("" = the staged copy, after a restart)
} wizard_state;
static wizard_state wz;                                 // zero at start (BSS: no bytes in the exe); wizard() sets the few non-zero starting values

// What a job's worker reads: copied from wz by start_job, so the worker never looks at state the pages go on changing.
typedef struct {
    int job;
    install_opts opts;                                      // the options the job runs with
    int flow;
    const acq_desc *acq; BOOL acq_local_ok;
    WCHAR msi[MAX_PATH];
    zipent zip;
    char pw[256];                                           // the worker wipes it after the extraction (the UI's copy is wiped by start_job)
    WCHAR drvzip[MAX_PATH]; BOOL drv_allow; WCHAR drv_hex[65];   // the driver zip, and the SHA-256 the user accepted for it
} job_in;
// What a job's worker produces: written only by the worker, read by the UI only after the join in job_done, which merges it into wz (JOBS[].merge).
// start_job starts it as a copy of the matching fields of wz, so a field the job does not write keeps its value.
typedef struct {
    int lastpct;                                            // on_progress: the last percentage posted (the worker's own: nobody else reads or resets it)
    DWORD neterr;
    WCHAR pkg[MAX_PATH]; BOOL pkg_found, pkg_ours;          // JOB_ACQUIRE for the DynaRun setup
    WCHAR drvzip[MAX_PATH]; int drv_how; WCHAR drv_hex[65]; ULONGLONG drv_size; WCHAR drv_rep[1500]; int drv_bl;   // JOB_ACQUIRE for the driver
    install_result instres;                                 // JOB_INSTALL_*
} job_out;
static job_in g_in;
static job_out g_out;
// The update hint's answer: written by the hint thread before it posts WM_HINT, read by the UI when the message arrives.
typedef struct { WCHAR inst[40], nw[40]; } hint_out;
static hint_out g_hint_out;

static volatile LONG g_cancel;                          // the one value both sides use while a job runs: the UI sets 1 (cancel) / 2 (pause), the worker polls it
static int g_dpi;                                       // the window's own resources and layout numbers (UI thread only, not part of the run's state)
static HFONT g_font, g_big;
static HBRUSH g_white, g_grey;
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
