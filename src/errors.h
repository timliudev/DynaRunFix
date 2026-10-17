// The result codes of DynaRunFix-Setup.exe (setup.c, wizard.c, package.c, driver.c), in one place. Included by setup.h.
// The VALUES are part of the program's behaviour (process exit codes, what the elevated stage reports to the wizard, tests\test_hardening.c locks them):
// do not renumber. The same number means different things in different domains; each meaning has its own name here.
#pragma once

/* ---- 1. Install / uninstall result and process exit code ----
   Returned by the install and uninstall stages (setup.c), by run_elevated (the exit code of the elevated process) and by wizard(); the process
   exit code of DynaRunFix-Setup.exe is the same number. */
enum {
    RC_OK = 0,                  // done
    RC_FAIL = 1,                // failed (the reason was shown, or logged with /quiet)
    RC_CANCEL = 2,              // cancelled: the UAC prompt was declined, a question was answered with Cancel, the Windows Installer was cancelled; also wizard(): closed before the end
                                // (the same number as PK_NET: a different domain, see 2.)
    RC_OVER_SHOULDER = 3,       // machine_full only: the elevated account is not the user (over-the-shoulder elevation): the caller runs the DynaRun MSI as the user instead
    RC_MSI_FAILED = 4,          // the DynaRun MSI could not be staged, is not DynaRun's, or its install / uninstall failed
    RC_NO_DYNARUN = 5,          // the MSI was installed, but DynaRun V3.exe was not found afterwards
    RC_RUNNING = 6              // DynaRun (or the launcher / LEProc) still runs, nothing was changed (the same number as PK_PASSWORD: a different domain)
};

/* ---- 2. Download / zip / package result (package.c, driver_fetch, driver_check) ---- */
// PK_CERT: certificate / TLS failure; PK_URL: not https, or redirected off the allowed host
enum { PK_OK, PK_CANCEL, PK_NET, PK_HTTP, PK_IO, PK_FORMAT, PK_PASSWORD, PK_CERT, PK_URL };   // 0..8

/* ---- 3. Status of one file of a pinned table (pkg_check.st) ---- */
enum { PKC_OK = 0, PKC_DIFFER = 1, PKC_MISSING = 2 };

/* ---- 4. The driver step (driver.c): result of driver_prep / driver_install / driver_cancel, also an exit code of the elevated stage ----
   DRV_SUCCESS is RC_OK. DRV_E_* 20..29 are failures; DRV_W_BDE 30 is "done, but BitLocker could not be suspended" (the wizard asks); DRV_W_HASH 31 is a
   driver_fetch / driver_check result (some files differ, the user is asked), never an exit code. RC_CANCEL (2) is also what the driver functions return
   when the UAC prompt was declined. DRV_NOVERB is verb_run's (setup.c) "argv[1] is no verb of the table". */
enum {
    DRV_SUCCESS = 0,
    DRV_E_FIRST = 20,           // the first driver code; see FIX_DONE
    DRV_E_HASH = 20, DRV_E_EXTRACT = 21, DRV_E_INF = 22, DRV_E_COPY = 23, DRV_E_BCD = 24, DRV_E_TASK = 25, DRV_E_PNP = 26, DRV_E_DIR = 27, DRV_E_SID = 28,
    DRV_E_ENFORCED = 29,
    DRV_W_BDE = 30,
    DRV_W_HASH = 31,
    DRV_NOVERB = -1000
};

/* The elevated install stage (install_fix_driver / install_all_driver, setup.c) "installed the fix" when its result is one of
     RC_OK (0)                            the fix is installed, no driver step asked for
     DRV_SUCCESS (0)                      the same number: the fix and the driver's preparation are done
     DRV_E_HASH .. DRV_E_ENFORCED (20..29), DRV_W_BDE (30), DRV_W_HASH (31, not a stage result, would count)
                                          the fix is installed and the driver step gave this code
     and every number above DRV_W_HASH (the rule is "at least DRV_E_FIRST", with no upper bound: any exit code of the elevated process from 20 on).
   NOT done: RC_FAIL, RC_CANCEL, RC_OVER_SHOULDER, RC_MSI_FAILED, RC_NO_DYNARUN, RC_RUNNING (1..6), 7..19 and every negative number (DRV_NOVERB too). */
#define FIX_DONE(rc) ((int)(rc) == (int)RC_OK || (int)(rc) >= (int)DRV_E_FIRST)   /* int casts: the codes are enumerators of different enums (C5287) */

/* ---- 5. Smaller answers ---- */
enum { RM_NONE = -1 };                              // remove_dynarun(): DynaRun V3 is not installed (else the msiexec exit code)
enum { BL_UNKNOWN = -1, BL_OFF = 0, BL_ON = 1 };    // driver_bitlocker(): BitLocker protection of the system drive (not elevated: cannot be told)
enum { PIN_NOT_ASKED = 0, PIN_DONE = 1, PIN_NEXT_SIGNIN = 2, PIN_FAILED = 3 };   // install_result.pin: after the install, 0 not asked, 1 pinned, 2 appears after the next sign-in, 3 could not pin
enum { HINT_NONE = 0, HINT_NEWER = 1, HINT_ZIPDIFF = 2 };   // pkg_update_hint: nothing to say, the site names a newer version, the site's zip differs from the last download
