// DynaRunFix-Setup.exe - installs DynaRunFix for end users (Windows XP .. 11).
//
//   DynaRunFix-Setup.exe                      wizard (wizard.c): installs DynaRun V3 first when it is
//                                             missing (official download, password, license), then the fix
//   DynaRunFix-Setup.exe /quiet               install the fix only, no UI (DynaRun must be installed);
//                                             exit code 6 (RC_RUNNING), nothing changed, while DynaRun runs;
//   DynaRunFix-Setup.exe /quiet /close        the same, but closes a running DynaRun first
//   /notaskbar, /noautostart                  with any install: do not pin DynaRun to the taskbar /
//                                             do not start it when the user signs in (both are on by default,
//                                             also with /quiet; the wizard's first page has the two check boxes)
//   /keep                                     with any install: leave the taskbar pin, the sign-in start and the
//                                             desktop shortcut as they are (an update, see update.c)
//   DynaRunFix-Setup.exe /uninstall [/quiet]  uninstall (also run from "Programs and Features")
//   DynaRunFix-Setup.exe /checkupdate         /update <args>, /offer: the installed copy, run by the launcher (update.c)
//   /pin                                      (elevated stage) also set up the Windows 11 taskbar pin
//   DynaRunFix-Setup.exe /driver              the Dyna Pro USB driver step on its own (driver.c; the wizard's last page)
//   DynaRunFix-Setup.exe /driver-cancel       undo the driver step's preparation (task, start-up menu option, BitLocker)
//   /driver-prep, /driver-install, /driver-continue   the driver step's own stages (elevated, or started by its scheduled task)
//   /machine ... /with-driver <zip> [/unverified <sha256>]   (also /full) the elevated stage prepares the driver (driver.c, prep) right after the
//                                             fix, in the same process: one UAC prompt for the fix and the driver; the exit code is the fix's if it
//                                             failed, else the driver's (0, DRV_E_*, DRV_W_BDE)
//
// Install, in two stages so that per-user changes land in the right profile even when an
// administrator's credentials are typed into the UAC prompt:
//   machine stage (elevated, "/machine <user SID> <DynaRun exe>"): copies DynaRunFix.exe,
//     dynafix.dll and this program to Program Files\DynaRunFix (copies no COM
//     registrations to HKLM), adds the
//     code-page manifest when the system ANSI code page is UTF-8, puts Locale Emulator in le\ (the
//     launcher uses it only with the UTF-8 option on a zh-TW system), retargets all-users shortcuts,
//     registers the uninstaller; on Windows 11 the taskbar pin (Microsoft's taskbar layout policy for the
//     user, needs admin; "/pin" in the arguments).
//   user stage (not elevated): retargets the user's DynaRun shortcuts to the launcher (the original
//     .lnk bytes are kept in the registry and put back on uninstall); creates a desktop shortcut
//     when there is none; pins DynaRun to the taskbar (per Windows version, see pin_taskbar); the
//     "Run" value that starts it at sign-in.
// Uninstall: the elevated stage runs first (a cancelled UAC prompt changes nothing), then the user's own
// shortcuts, Run value and pins are put back from the backups. The elevated stage puts back the all-users shortcuts and
// points other accounts' shortcuts that still start the launcher back at DynaRun V3.exe. Machine-wide COM keys that
// older versions copied are left alone (removing them could break elevated DynaRun); so are com-copied.txt and com-backup.
// Hardening of the elevated stage (see the comments at the functions): the vendor MSI is copied to an administrators-only
// folder and checked again there; the exe time stamp that is changed to refresh the manifest state is written to HKLM before it
// is changed, and put back at the next run if that failed.
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <exdisp.h>
#include <shldisp.h>
#include <servprov.h>
#include <commdlg.h>
#include <sddl.h>
#include <aclapi.h>
#include <tlhelp32.h>
#include <delayimp.h>
#include "setup.h"
#include "version.h"

// The rest of this file is split by topic into src\setup\*.h. They are fragments of THIS translation unit (static functions and file-local
// state refer to each other), not headers and not separate .c files: build.cmd and the tests compile only setup.c. The order matters:
// a fragment uses only what the ones before it declare (the few forward declarations are inside the fragments).
#include "setup/defs.h"                     // constants, globals, trust_ctx
#include "setup/util.h"                     // system-folder DLLs, memory, files, registry, shared helpers (path_join, run_process, dir_walk, app key)
#include "setup/stage.h"                    // one elevated stage at a time, setup.log of the install folder
#include "setup/trust.h"                    // who may change a path; the exe path of the elevated stage
#include "setup/exetime.h"                  // the borrowed exe time stamp (manifest refresh)
#include "setup/procs.h"                    // paths, programs that hold the files, the user log, closing DynaRun
#include "setup/shortcuts.h"                // shortcut folders, shortcut files, backups and restore
#include "setup/taskbar.h"                  // sign-in start, taskbar pin, unelevated start of DynaRun
#include "setup/users.h"                    // other accounts' hives (uninstall)
#include "setup/machine.h"                  // machine stage: install and uninstall
#include "setup/elevate.h"                  // held copy, run_elevated
#include "setup/msi_stage.h"                // the vendor MSI in an administrators-only folder, the full stage
#include "setup/userstage.h"                // user stage, the install entry points
#include "setup/uninstall.h"                // the uninstall
#include "setup/cmdline.h"                  // command line parsing, verb table

void WinMainCRTStartup(void)
{
    typedef BOOL (WINAPI *SetDefaultDllDirectories_t)(DWORD);
    SetDefaultDllDirectories_t sdd = (SetDefaultDllDirectories_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDefaultDllDirectories");
    int rc; cmdline c; install_result res;
    if (sdd) sdd(0x800);                    // LOAD_LIBRARY_SEARCH_SYSTEM32: no DLL is searched for in the exe's folder or the current folder
    c.argv = CommandLineToArgvW(GetCommandLineW(), &c.argc);
    g_zh = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
    init_paths();
    res.pin = PIN_NOT_ASKED;                // only the wizard shows it
    cmd_parse(&c);
    if (c.argc >= 2 && (!lstrcmpiW(c.argv[1], L"/machine") || !lstrcmpiW(c.argv[1], L"/full"))) c.opts.pin = c.pin;   // the elevated stage: as asked
    rc = verb_run(&c);
    if (rc == DRV_NOVERB) rc = c.opts.quiet ? (locate_dynarun() ? install_fix(&c.opts, &res) : RC_FAIL) : wizard(&c.opts);
    // the elevated part (/machine, /full) ends here: let the wizard that started it take the foreground back
    AllowSetForegroundWindow(ASFW_ANY);
    ExitProcess(rc);
}
