// What the launcher (launcher.c) and the updater (update.c, in the setup) share: the lock and the append of %TEMP%\dynafix.log,
// the security attributes of the objects shared with dynafix.dll, the clock, the registry value names of the update and the
// command-line verbs the launcher and the setup pass each other. Header-only (static functions, one copy per program): the two
// are built as separate programs and build.cmd stays as it is. dynafix.dll keeps its own copy of the log lock (built apart).
// Everything here crosses process boundaries: the mutex name, the value names and the verbs must not change.
#ifndef DRF_APPLOG_H
#define DRF_APPLOG_H
#include <windows.h>
#include <sddl.h>

#ifndef WIDEN
#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)
#endif

#define DAY 864000000000ULL                     // FILETIME units (100 ns)

/* ---------- HKCU\Software\DynaRunFix ---------- */
#define APP_KEY            "Software\\DynaRunFix"
#define RV_UPDATE_VERSION  "UpdateVersion"      // REG_SZ: tag of a newer release (noted by /checkupdate)
#define RV_UPDATE_SHA256   "UpdateSha256"       // REG_SZ: the SHA-256 GitHub lists for its DynaRunFix-Setup.exe
#define RV_UPDATE_CHECKED  "UpdateChecked"      // REG_QWORD: when GitHub last answered (once a day at most)
#define RV_UPDATE_ASK      "UpdateAsk"          // REG_QWORD: not offered before this time
#define RV_UPDATE_SKIP     "UpdateSkip"         // REG_SZ: the tag the user chose to skip
#define RV_UPDATE_ETAG     "UpdateETag"         // the download's ETag (pkg_fetch)

/* ---------- command-line verbs between the launcher and the setup ---------- */
#define VERB_CHECKUPDATE   "/checkupdate"       // launcher -> setup: look for a newer release, no UI
#define VERB_OFFER         "/offer"             // launcher -> setup: offer the noted release beside the running DynaRun
#define VERB_UPDATE        "/update"            // older launcher -> setup: ask, install, start the launcher with /noupdate
#define VERB_NOUPDATE      "/noupdate"          // setup -> launcher: do not deal with an update in this start
#define VERB_AUTOSTART     "/autostart"         // sign-in Run value -> launcher
#define VERB_RESTART       "/restart"           // dynafix.dll -> launcher: "/restart <pid>"
#define VERB_WASADMIN      "/wasadmin"          // elevated launcher -> its non-elevated copy

static ULONGLONG now(void)
{
    FILETIME f; ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&f);
    u.LowPart = f.dwLowDateTime; u.HighPart = f.dwHighDateTime;
    return u.QuadPart;
}

// Security attributes for the objects shared with dynafix.dll (the log mutex, the ready event): full access for the current
// user, SYSTEM and Administrators only (no NULL DACL: every other user and any low-integrity process could otherwise open them).
// Free *psd with LocalFree. FALSE: no explicit DACL could be built, the caller then takes the default one.
static BOOL make_sa(SECURITY_ATTRIBUTES *sa, PSECURITY_DESCRIPTOR *psd)
{
    HANDLE tk; DWORD buf[64], n = 0; char *sid = NULL, sddl[200]; BOOL ok = FALSE;
    *psd = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tk)) return FALSE;
    if (GetTokenInformation(tk, TokenUser, buf, sizeof(buf), &n) && ConvertSidToStringSidA(((TOKEN_USER *)buf)->User.Sid, &sid)) {
        if (lstrlenA(sid) < 120) {
            wsprintfA(sddl, "D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;%s)", sid);
            ok = ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl, SDDL_REVISION_1, psd, NULL);
        }
        LocalFree(sid);
    }
    CloseHandle(tk);
    if (ok) { sa->nLength = sizeof(*sa); sa->lpSecurityDescriptor = *psd; sa->bInheritHandle = FALSE; }
    return ok;
}

// dynafix.dll, the launcher and the updater all append to dynafix.log (and any number of threads in each). One named mutex
// ("Local\DynaRunFix.dynafix.log", DACL: current user, SYSTEM, Administrators) is held around every append and around the
// trim. It waits at most 2 s: a line is then written anyway (an append is atomic by itself), but the log is never trimmed
// without the lock. If the mutex cannot be created there is no locking at all.
static HANDLE g_logmtx;

static HANDLE log_lock(void)
{
    HANDLE m = g_logmtx; DWORD r;
    if (!m) {
        SECURITY_ATTRIBUTES sa; PSECURITY_DESCRIPTOR sd; HANDLE n; BOOL have = make_sa(&sa, &sd);
        n = CreateMutexA(have ? &sa : NULL, FALSE, "Local\\DynaRunFix.dynafix.log");
        if (sd) LocalFree(sd);
        if (!n) n = INVALID_HANDLE_VALUE;
        m = (HANDLE)InterlockedCompareExchangePointer((PVOID volatile *)&g_logmtx, n, NULL);   // lost the race: use the winner's
        if (m) { if (n != INVALID_HANDLE_VALUE) CloseHandle(n); } else m = n;
    }
    if (m == INVALID_HANDLE_VALUE) return NULL;
    r = WaitForSingleObject(m, 2000);
    return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED ? m : NULL;   // an abandoned mutex is ours now; the log itself is fine
}

// Opens the log. A reparse point (symbolic link, junction) is never followed: INVALID_HANDLE_VALUE then. no_links: also refuse
// a file with several hard links (the launcher when elevated).
static HANDLE log_open(const char *path, DWORD access, DWORD disp, BOOL no_links)
{
    HANDLE h = CreateFileA(path, access | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, disp, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BY_HANDLE_FILE_INFORMATION bi;
    if (h == INVALID_HANDLE_VALUE) return h;
    if (!GetFileInformationByHandle(h, &bi) || (bi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        (no_links && bi.nNumberOfLinks > 1)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
    return h;
}

// Appends n chars of line (a whole line, with its "\r\n") to the log at path, under the log lock: one WriteFile per line, so it
// is appended atomically.
static void log_append(const char *path, const char *line, DWORD n, BOOL no_links)
{
    HANDLE h, m = log_lock();
    h = log_open(path, FILE_APPEND_DATA, OPEN_ALWAYS, no_links);
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, line, n, &n, NULL); CloseHandle(h); }
    if (m) ReleaseMutex(m);
}

#endif
