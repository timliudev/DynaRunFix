// What every module of DynaRunFix-Setup.exe needs: the language switch T(), the paths, the install options, the command line, the basic helpers of
// setup.c and the install entry points. The other drf_*.h headers (util, package, driver, update, wizard) are included where they are used; setup.h includes
// them all (update.c includes only that). The drf_ prefix keeps these names apart from the src/setup/*.h fragments (util.h there).
#pragma once
#include <windows.h>
#include "errors.h"                         // RC_*, PK_*, PKC_*, DRV_E_* / DRV_W_*, FIX_DONE: the result codes

extern BOOL g_zh;
// What the user asked for. Built from the command line (setup.c) or by the wizard's pages (wizard.c) and handed down explicitly: the install, the
// uninstall and run_elevated read nothing else. Only quiet, close, pin and keep reach the elevated stage (run_elevated forwards them as switches).
typedef struct {
    BOOL quiet;                             // /quiet: no message boxes
    BOOL close;                             // close a running DynaRun before installing (/close, or the wizard's consent for one install job)
    BOOL pin;                               // pin DynaRun to the taskbar (default on; /notaskbar)
    BOOL autostart;                         // start it at sign-in (default on; /noautostart)
    BOOL autostart_later;                   // the driver follows: the sign-in start is registered once the driver steps are over (autostart_flush)
    BOOL keep;                              // /keep: leave the pin, the sign-in start and the desktop shortcut as they are
} install_opts;
void install_opts_default(install_opts *o);     // pin and autostart on, the rest off
// What the install reports besides its exit code (written by the user stage, read by the wizard).
typedef struct {
    int pin;                                // PIN_*: 0 not asked, 1 pinned, 2 appears after the next sign-in, 3 could not pin
} install_result;
// The command line, parsed once (setup.c, cmd_parse). The verb table (setup.c) hands it to the verb's function.
typedef struct {
    int argc; WCHAR **argv;                 // as CommandLineToArgvW gave them; argv[0] is the program, argv[1] the verb
    install_opts opts;                      // the switches that apply to every verb: /quiet /close /notaskbar /noautostart /keep
    BOOL pin;                               // /pin seen (the elevated stage sets up the Windows 11 pin only when it is asked to)
    const WCHAR *dzip;                      // /with-driver <zip>: only from argv[4] on (after the verb, the SID and the path), any value is taken
    const WCHAR *daccept;                   // /unverified <sha256> from argv[4] on, at any position (/machine, /full): the last one with 64 characters
    const WCHAR *daccept4;                  // ... only as argv[4] argv[5] (/driver-prep): the same value, or NULL
} cmdline;
typedef int (*verb_fn)(cmdline *c);         // returns the exit code of the process
typedef struct { const WCHAR *name; int min_args; verb_fn run; } verb;   // argv[1] (any case) is name and argc >= min_args
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
int install_fix(const install_opts *o, install_result *res);                      // DynaRun is installed: elevated stage + user shortcuts. RC_OK, RC_CANCEL
int install_all(const install_opts *o, install_result *res, const WCHAR *msi);          // runs the DynaRun MSI, then install_fix's work. RC_OK, RC_CANCEL
// The same, and the driver's preparation (driver_prep) in the SAME elevated stage: one UAC prompt for both. zip/accept as for driver_prep.
// RC_OK; RC_CANCEL; another small code: the fix failed (the driver was not touched); >= DRV_E_HASH: the fix is installed, the driver step gave this code (DRV_E_*, DRV_W_BDE).
int install_fix_driver(const install_opts *o, install_result *res, const WCHAR *zip, const WCHAR *accept);
int install_all_driver(const install_opts *o, install_result *res, const WCHAR *msi, const WCHAR *zip, const WCHAR *accept);
enum { RUN_DYNARUN = 1, RUN_LAUNCHER = 2, RUN_LEPROC = 4, RUN_CANCLOSE = 8, RUN_NOCLOSE = 16 };
int running_programs(void);                 // RUN_* bits of the programs that hold files the setup replaces. RUN_CANCLOSE: one of them can be closed
                                            // by the setup; RUN_NOCLOSE: one cannot (another session, or its exe path cannot be told)

void autostart_flush(void);                 // registers the sign-in start that user_stage deferred (HKCU AutostartPending); no-op otherwise
