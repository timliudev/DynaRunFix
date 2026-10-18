// dynafix.dll - Win10/11 compatibility shim for DynaRun V3 (x86, no CRT)
//
// Observed: Win10/11 sends WM_SIZE (SIZE_MAXIMIZED) when a hidden, already-maximized window is
// shown again, even though nothing changed; Win7/XP do not. After each WM_SIZE of DynaRun's main
// form, THBRes25.dll (the THBResize 2.5 control) posts the form the private message 0x591, and
// each 0x591 is followed by: main form hidden, a blank form shown, main form shown maximized
// again -> WM_SIZE again -> a repeating hide/show cycle (the "flicker").
//
// Fix: watch WM_SIZE on VB forms; if it is identical to the previous one for that
// window, THBRes25's PostMessageA(0x591) for it is skipped. For that, THBRes25.dll's
// PostMessageA import-table entry is redirected, in memory, inside this process
// (no file on disk is changed).
//
// Second fix: files kept in OneDrive on Win10/11 can carry cloud attribute bits
// (e.g. 0x80000 PINNED, "Always keep on this device") that do not exist on Win7/XP.
// Observed: a .Dpr with such a bit is silently not opened by DynaRun (Process Monitor shows the
// attribute query but no read). The file-attribute APIs imported by MSVBVM60.DLL and scrrun.dll
// are redirected to wrappers that clear those bits. Only import tables in this process are
// patched, in memory; nothing on disk changes.
//
// Third fix: on the first start DynaRun runs System Data\Setup_<n>.exe (VB Declare ShellExecuteA)
// and keeps waiting for its result (observed: one CPU core at 100%). Windows' installer detection runs that helper elevated; DynaRun then
// keeps showing the system selection and only picks up the new configuration on its next start.
// ShellExecuteA, which MSVBVM60 resolves with GetProcAddress, gets a wrapper: for Setup_<n>.exe it
// waits for the helper to finish (one UAC prompt) and restarts DynaRun through the launcher. The helper
// also resets DynaRun's language to English; the language chosen in the first-start dialog is restored,
// and it switches off all optional features; the ones picked on the selection screen are switched on again.
//
// Fourth fix: the maximized main form (no caption) covers the taskbar, which hides its bottom status bar;
// it is kept inside the monitor's work area (see fit_minmax).
#include <windows.h>
#include <shellapi.h>
#include <intrin.h>
#include <restartmanager.h>
#include "version.h"
#include "setup_match.h"
#include "dfutil.h"

// DYNAFIX_* switches: see dfutil.h (the whole value must be the expected one; an over-long value is not read)
static BOOL env_flag(const char *name, char want) { char v[4]; DWORD n = GetEnvironmentVariableA(name, v, sizeof(v)); return df_env_flag(v, n, sizeof(v), want); }
// A system DLL, from System32 only (LOAD_LIBRARY_SEARCH_SYSTEM32; where that flag is not known - Windows XP without KB2533623 - by its
// full path in the system folder): never from the folder of DynaRun or the current folder, where a same-named file could sit.
static HMODULE load_sys(const char *name)
{
    HMODULE m = LoadLibraryExA(name, NULL, 0x00000800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    if (!m && GetLastError() == ERROR_INVALID_PARAMETER) {
        char p[MAX_PATH + 16]; UINT n = GetSystemDirectoryA(p, MAX_PATH);
        if (n && n < MAX_PATH) { lstrcatA(p, "\\"); lstrcatA(p, name); m = LoadLibraryA(p); }
    }
    return m;
}
static BOOL env_num(const char *name, long *out) { char v[12]; DWORD n = GetEnvironmentVariableA(name, v, sizeof(v)); return df_env_num(v, n, sizeof(v), out); }

#pragma comment(linker, "/EXPORT:CwpProc=_CwpProc@12")

#define THB_MSG 0x591
#define P_W     "dynafix.w"
#define P_L     "dynafix.l"
#define P_DUP   "dynafix.dup"
#define LANG_KEY "Software\\DynaPro\\Operation_Data"

// FILE_ATTRIBUTE_RECALL_ON_OPEN | PINNED | UNPINNED | RECALL_ON_DATA_ACCESS
#define CLOUD_ATTRS 0x005C0000

typedef BOOL (WINAPI *PostMessageA_t)(HWND, UINT, WPARAM, LPARAM);

static HINSTANCE g_self;
static BOOL g_fullscreen, g_waclip;   // DYNAFIX_FULLSCREEN=1: off; DYNAFIX_WORKAREA=clip: shrink the height only
static BOOL g_patched, g_vbpatched, g_fsopatched, g_fontpatched;
static BOOL g_usbfix;                 // DYNAFIX_USBFIX=1: DPSetData cannot hang DynaRun for good on a lost connection (see H_DPSetData)
static volatile LONG g_sd_state;      // DPSetData watchdog: 0 idle, 1 running, 2 abandoned (GUI thread gave up), 3 finished (see H_DPSetData)
static LONG g_pinned;                 // 1 once this process has installed the dll's own hooks (InterlockedCompareExchange)
// Guards the caches that GDI/text hooks mutate (log_face / log_text seen[], g_stock[], g_fontmap, g_linemap): the hooks
// run in whatever thread draws. Initialised in DllMain (kernel32 only); re-entrant, and nothing under it calls a hooked
// API that would take it on another thread.
static CRITICAL_SECTION g_cs;
static LONG g_skipped, g_masked;
static PostMessageA_t g_realPost;
static char g_logpath[MAX_PATH];

// dynafix.dll and the launcher both append to dynafix.log (and any number of threads in each). One named mutex
// ("Local\DynaRunFix.dynafix.log", open to every user, so an elevated and a normal process share it) is held around
// every append and around the trim. It waits at most 2 s: a line is then written anyway (an append is atomic by
// itself), but the log is never trimmed without the lock. If the mutex cannot be created there is no locking at all.
static HANDLE g_logmtx;

// The mutex gets an explicit DACL: the current user, SYSTEM and Administrators (an elevated and a normal process of the
// same user share it). If the DACL cannot be built the mutex is not created at all (no locking), never created open to everyone.
static HANDLE create_log_mutex(void)
{
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID sys = NULL, adm = NULL; TOKEN_USER *tu = NULL; HANDLE tok, mtx = NULL; DWORD need = 0;
    BYTE aclbuf[512]; ACL *acl = (ACL *)aclbuf; SECURITY_DESCRIPTOR sd; SECURITY_ATTRIBUTES sa;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return NULL;
    GetTokenInformation(tok, TokenUser, NULL, 0, &need);
    if (need && need <= 256) tu = (TOKEN_USER *)HeapAlloc(GetProcessHeap(), 0, need);
    if (tu && GetTokenInformation(tok, TokenUser, tu, need, &need) &&
        AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &sys) &&
        AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adm) &&
        InitializeAcl(acl, sizeof(aclbuf), ACL_REVISION) &&
        AddAccessAllowedAce(acl, ACL_REVISION, MUTEX_ALL_ACCESS, tu->User.Sid) &&
        AddAccessAllowedAce(acl, ACL_REVISION, MUTEX_ALL_ACCESS, sys) &&
        AddAccessAllowedAce(acl, ACL_REVISION, MUTEX_ALL_ACCESS, adm) &&
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) && SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE)) {
        sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = &sd; sa.bInheritHandle = FALSE;
        mtx = CreateMutexA(&sa, FALSE, "Local\\DynaRunFix.dynafix.log");
    }
    if (sys) FreeSid(sys);
    if (adm) FreeSid(adm);
    if (tu) HeapFree(GetProcessHeap(), 0, tu);
    CloseHandle(tok);
    return mtx;
}

static HANDLE log_lock(void)
{
    HANDLE m = g_logmtx; DWORD r;
    if (!m) {
        HANDLE n;
        n = create_log_mutex();
        if (!n) n = INVALID_HANDLE_VALUE;
        m = (HANDLE)InterlockedCompareExchangePointer((PVOID volatile *)&g_logmtx, n, NULL);   // lost the race: use the winner's
        if (m) { if (n != INVALID_HANDLE_VALUE) CloseHandle(n); } else m = n;
    }
    if (m == INVALID_HANDLE_VALUE) return NULL;
    r = WaitForSingleObject(m, 2000);
    return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED ? m : NULL;   // an abandoned mutex is ours now; the log itself is fine
}

// The log is opened without following a link planted in its place: a reparse point (symbolic link, junction) or a file with
// more than one hard link is refused (nothing is logged then). Returns INVALID_HANDLE_VALUE on any doubt.
static HANDLE open_log(DWORD access, DWORD disp)
{
    BY_HANDLE_FILE_INFORMATION bi;
    HANDLE h = CreateFileA(g_logpath, access | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, disp, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return h;
    if (!GetFileInformationByHandle(h, &bi) || (bi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || bi.nNumberOfLinks != 1) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

// above 10 MB, dynafix.log is cut to its newest 8 MB, from a full line on (the launcher does the same; whichever runs first)
static void trim_log(void)
{
    HANDLE h, m; DWORD size, n, i; char *buf;
    h = open_log(GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING);
    if (h == INVALID_HANDLE_VALUE) return;
    size = GetFileSize(h, NULL);
    if (size != INVALID_FILE_SIZE && size > 10485760 && (m = log_lock())) {
        size = GetFileSize(h, NULL);   // the other process may have trimmed while we waited
        if (size != INVALID_FILE_SIZE && size > 10485760 && (buf = (char *)HeapAlloc(GetProcessHeap(), 0, 8388608))) {
            SetFilePointer(h, size - 8388608, NULL, FILE_BEGIN);
            if (ReadFile(h, buf, 8388608, &n, NULL)) {
                for (i = 0; i < n && buf[i] != '\n'; i++);
                if (i < n) { i++; SetFilePointer(h, 0, NULL, FILE_BEGIN); WriteFile(h, buf + i, n - i, &n, NULL); SetEndOfFile(h); }
            }
            HeapFree(GetProcessHeap(), 0, buf);
        }
        ReleaseMutex(m);
    }
    CloseHandle(h);
}

// A line is at most LOG_MAX chars; a longer one is cut and ends "...\r\n". wsprintfA stops at 1024 chars itself.
#define LOG_MAX 1000
// Write one import slot. Other tools (loggers, compatibility shims) patch the same imports from other threads, and a
// "VirtualProtect(RW); store; VirtualProtect(old)" can race with them on the same page. So an already writable page is written
// without touching its protection. A page that is not writable is made writable keeping its execute right (PAGE_EXECUTE_READWRITE if
// it had one, else PAGE_READWRITE: an import page shared with code must not lose execute) and its original protection is put back
// after the store. A tool that changes the same page in exactly that moment can still collide with the restore (not observed).
static void put_slot(DWORD *slot, DWORD v)
{
    MEMORY_BASIC_INFORMATION mbi; DWORD old = 0, tmp, np; BOOL changed = FALSE;
    if (!VirtualQuery(slot, &mbi, sizeof(mbi)) || !(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        np = (VirtualQuery(slot, &mbi, sizeof(mbi)) && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ))) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
        changed = VirtualProtect(slot, sizeof(*slot), np, &old);
    }
    InterlockedExchange((volatile LONG *)slot, (LONG)v);
    if (changed) VirtualProtect(slot, sizeof(*slot), old, &tmp);
}

static void logf(const char *fmt, DWORD a, DWORD b, DWORD c)
{
    char line[LOG_MAX + 100]; DWORD n; HANDLE h, m; SYSTEMTIME t;
    if (!g_logpath[0]) return;
    GetLocalTime(&t);
    n = wsprintfA(line, "%04u-%02u-%02u %02u:%02u:%02u.%03u ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    wsprintfA(line + n, fmt, a, b, c);
    n = lstrlenA(line);
    if (n > LOG_MAX) { lstrcpyA(line + LOG_MAX - 5, "...\r\n"); n = LOG_MAX; }
    m = log_lock();
    h = open_log(FILE_APPEND_DATA, OPEN_ALWAYS);   // one WriteFile per line: appended atomically
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, line, n, &n, NULL); CloseHandle(h); }
    if (m) ReleaseMutex(m);
}

// UTF-8 text of w in d[size], cut to fit (WideCharToMultiByte alone fails and leaves d unterminated when it does not fit)
static void w2u(char *d, int size, const WCHAR *w)
{
    int n = lstrlenW(w), r = 0;
    while (n > 0 && !(r = WideCharToMultiByte(CP_UTF8, 0, w, n, d, size - 1, NULL, NULL))) n--;
    d[r] = 0;
}

// second header line: DynaRunFix version, DynaRun's file version, Windows version (version.dll is loaded on demand: no static import)
typedef BOOL (WINAPI *GFVIA_t)(LPCSTR, LPDWORD);
typedef BOOL (WINAPI *GFVA_t)(LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL (WINAPI *VQVA_t)(LPCVOID, LPCSTR, LPVOID *, PUINT);
typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOW *);

static void log_versions(void)
{
    char exe[MAX_PATH], app[40], win[48], msg[200]; DWORD h, sz, ubr, n = sizeof(ubr); HKEY k; HMODULE v, nt;
    lstrcpyA(app, "?"); lstrcpyA(win, "?");
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    if ((v = load_sys("version.dll"))) {
        GFVIA_t gsz = (GFVIA_t)GetProcAddress(v, "GetFileVersionInfoSizeA");
        GFVA_t get = (GFVA_t)GetProcAddress(v, "GetFileVersionInfoA");
        VQVA_t q = (VQVA_t)GetProcAddress(v, "VerQueryValueA");
        if (gsz && get && q && (sz = gsz(exe, &h))) {
            void *buf = HeapAlloc(GetProcessHeap(), 0, sz); VS_FIXEDFILEINFO *fi; UINT fl;
            if (buf) {
                if (get(exe, 0, sz, buf) && q(buf, "\\", (LPVOID *)&fi, &fl) && fl >= sizeof(*fi))
                    wsprintfA(app, "%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
                HeapFree(GetProcessHeap(), 0, buf);
            }
        }
        FreeLibrary(v);
    }
    if ((nt = GetModuleHandleA("ntdll.dll"))) {
        RtlGetVersion_t rgv = (RtlGetVersion_t)GetProcAddress(nt, "RtlGetVersion");
        OSVERSIONINFOW vi;
        vi.dwOSVersionInfoSize = sizeof(vi);
        if (rgv && rgv(&vi) == 0) {
            n = wsprintfA(win, "%u.%u.%u", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
            n = sizeof(ubr);
            if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_QUERY_VALUE, &k)) {
                DWORD type = 0;
                if (!RegQueryValueExA(k, "UBR", NULL, &type, (BYTE *)&ubr, &n) && type == REG_DWORD) wsprintfA(win + lstrlenA(win), ".%u", ubr);
                RegCloseKey(k);
            }
        }
    }
    wsprintfA(msg, "DynaRunFix %s (%s), DynaRun V3.exe %s, Windows %s\r\n", DRF_DISPLAY, DRF_COMMIT, app, win);
    logf("%s", (DWORD)(UINT_PTR)msg, 0, 0);
}

// DYNAFIX_STATS=1 (diagnostic): every 5 s the number of form WM_SIZE / duplicates / 0x591 passed on / 0x591 skipped /
// form show and hide events since the previous line is logged, to check that the guard ends the hide/show loop.
static BOOL g_stats;
static LONG s_size, s_dup, s_thb, s_skip, s_show, s_hide;
static UINT_PTR g_statstimer;

static void CALLBACK stats_tick(HWND unused, UINT m, UINT_PTR id, DWORD t)
{
    LONG a = InterlockedExchange(&s_size, 0), b = InterlockedExchange(&s_dup, 0), c = InterlockedExchange(&s_thb, 0);
    LONG d = InterlockedExchange(&s_skip, 0), e = InterlockedExchange(&s_show, 0), f = InterlockedExchange(&s_hide, 0);
    char line[160];
    if (!(a | b | c | d | e | f)) return;
    wsprintfA(line, "stats(5s): form WM_SIZE %d (duplicate %d), THBResize 0x591 passed %d skipped %d, forms shown %d hidden %d\r\n", a, b, c, d, e, f);
    logf("%s", (DWORD)(UINT_PTR)line, 0, 0);
}

static BOOL WINAPI HookedPostMessageA(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == THB_MSG && GetPropA(h, P_DUP)) {
        RemovePropA(h, P_DUP);     // one duplicate WM_SIZE covers one 0x591; the next WM_SIZE decides again
        InterlockedIncrement(&s_skip);
        if (InterlockedIncrement(&g_skipped) <= 20)
            logf("skipped THBResize 0x591 after duplicate WM_SIZE hwnd=%08X %u %u\r\n", (DWORD)(UINT_PTR)h, 0, 0);
        return TRUE;
    }
    if (m == THB_MSG) InterlockedIncrement(&s_thb);
    return g_realPost(h, m, w, l);
}

// ---- cloud attribute bits ----

static FARPROC r_gfaA, r_gfaW, r_gfaxA, r_gfaxW, r_ffA, r_ffW, r_fnA, r_fnW;

static DWORD strip(DWORD a, const char *api, void *ret)
{
    if (a == INVALID_FILE_ATTRIBUTES || !(a & CLOUD_ATTRS)) return a;
    if (InterlockedIncrement(&g_masked) <= 20) {
        char mod[MAX_PATH]; HMODULE m;
        lstrcpyA(mod, "?");
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ret, &m))
            GetModuleFileNameA(m, mod, MAX_PATH);
        logf("cleared cloud attributes %08X in %s, caller %s\r\n", a, (DWORD)(UINT_PTR)api, (DWORD)(UINT_PTR)mod);
    }
    return a & ~CLOUD_ATTRS;
}

static DWORD WINAPI H_GetFileAttributesA(LPCSTR p)
{ return strip(((DWORD (WINAPI *)(LPCSTR))r_gfaA)(p), "GetFileAttributesA", _ReturnAddress()); }
static DWORD WINAPI H_GetFileAttributesW(LPCWSTR p)
{ return strip(((DWORD (WINAPI *)(LPCWSTR))r_gfaW)(p), "GetFileAttributesW", _ReturnAddress()); }

static BOOL WINAPI H_GetFileAttributesExA(LPCSTR p, GET_FILEEX_INFO_LEVELS l, LPVOID d)
{
    BOOL ok = ((BOOL (WINAPI *)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID))r_gfaxA)(p, l, d);
    WIN32_FILE_ATTRIBUTE_DATA *a = d;
    if (ok && l == GetFileExInfoStandard) a->dwFileAttributes = strip(a->dwFileAttributes, "GetFileAttributesExA", _ReturnAddress());
    return ok;
}
static BOOL WINAPI H_GetFileAttributesExW(LPCWSTR p, GET_FILEEX_INFO_LEVELS l, LPVOID d)
{
    BOOL ok = ((BOOL (WINAPI *)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID))r_gfaxW)(p, l, d);
    WIN32_FILE_ATTRIBUTE_DATA *a = d;
    if (ok && l == GetFileExInfoStandard) a->dwFileAttributes = strip(a->dwFileAttributes, "GetFileAttributesExW", _ReturnAddress());
    return ok;
}

static HANDLE WINAPI H_FindFirstFileA(LPCSTR p, LPWIN32_FIND_DATAA d)
{
    HANDLE h = ((HANDLE (WINAPI *)(LPCSTR, LPWIN32_FIND_DATAA))r_ffA)(p, d);
    if (h != INVALID_HANDLE_VALUE) d->dwFileAttributes = strip(d->dwFileAttributes, "FindFirstFileA", _ReturnAddress());
    return h;
}
static HANDLE WINAPI H_FindFirstFileW(LPCWSTR p, LPWIN32_FIND_DATAW d)
{
    HANDLE h = ((HANDLE (WINAPI *)(LPCWSTR, LPWIN32_FIND_DATAW))r_ffW)(p, d);
    if (h != INVALID_HANDLE_VALUE) d->dwFileAttributes = strip(d->dwFileAttributes, "FindFirstFileW", _ReturnAddress());
    return h;
}
static BOOL WINAPI H_FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA d)
{
    BOOL ok = ((BOOL (WINAPI *)(HANDLE, LPWIN32_FIND_DATAA))r_fnA)(h, d);
    if (ok) d->dwFileAttributes = strip(d->dwFileAttributes, "FindNextFileA", _ReturnAddress());
    return ok;
}
static BOOL WINAPI H_FindNextFileW(HANDLE h, LPWIN32_FIND_DATAW d)
{
    BOOL ok = ((BOOL (WINAPI *)(HANDLE, LPWIN32_FIND_DATAW))r_fnW)(h, d);
    if (ok) d->dwFileAttributes = strip(d->dwFileAttributes, "FindNextFileW", _ReturnAddress());
    return ok;
}

// ---- VB Declare functions ----
// DynaRun's own API calls (VB "Declare") are resolved at run time by MSVBVM60 with GetProcAddress,
// so they are in no import table: MSVBVM60's import of GetProcAddress is redirected instead.

static FARPROC r_gpa, r_shexec;

// After the first-time setup helper DynaRun keeps showing the system selection and only picks up the
// new configuration on its next start, so start it again: the launcher next to this dll waits for this
// process to end ("/restart <pid>") and starts DynaRun with the fix attached.
static void start_hint(int kind, DWORD err, const char *path);   // defined with the other hint code below

static BOOL restart_dynarun(void)   // returns only when the restart failed (on success this process ends)
{
    char exe[MAX_PATH + 40], *p; STARTUPINFOA si; PROCESS_INFORMATION pi; int i;
    GetModuleFileNameA(g_self, exe, MAX_PATH);
    for (p = exe + lstrlenA(exe); p > exe && p[-1] != '\\'; p--);
    lstrcpyA(p, "DynaRunFix.exe");
    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) return FALSE;
    { char cmd[MAX_PATH + 60]; volatile char *z = (volatile char *)&si; for (i = 0; i < (int)sizeof(si); i++) z[i] = 0; si.cb = sizeof(si);
      wsprintfA(cmd, "\"%s\" /restart %u", exe, GetCurrentProcessId());
      if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return FALSE; }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    logf("restarting DynaRun to load the new configuration %u %u %u\r\n", 0, 0, 0);
    ExitProcess(0);
    return TRUE;
}

// ---- optional features picked on the first-start system selection ----
// The selection screen's "optional features" group (bottom right) holds four buttons, top to bottom:
// auto climate monitor, water cooler, cooling air fans / AFR extraction, internal AFR analyser. A picked
// one is drawn green. DynaRun stores only the climate / AFR model (Calibration_Data) and the helper then
// writes all System_Setup enable flags as "0", so the picked features end up off. They are switched on
// afterwards with the same REG_SZ "-1" values that 工程模式 -> 系統組態設定 -> 存檔並離開 writes.
#define SETUP_KEY "Software\\DynaPro\\System_Setup"
#define N_OPT 4
#include "dynafix_tables.inc.c"

#include "dynafix_options.inc.c"

// The helpers live in "System Data" under the per-user data folder (%APPDATA%\Dyna Pro Dynamometers\Dyna Run V3);
// the folder next to DynaRun's exe is accepted too, for installs that keep it there.
static BOOL is_setup_helper(const char *file)
{
    char exe[MAX_PATH + 16], d1[MAX_PATH + 16], d2[MAX_PATH + 80], d3[MAX_PATH + 80], *p; const char *dirs[3]; DWORD n;
    dirs[0] = dirs[1] = dirs[2] = NULL;
    n = GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (n && n < MAX_PATH) {
        for (p = exe + n; p > exe && p[-1] != '\\'; p--);
        lstrcpyA(p, "System Data"); dirs[0] = exe;
    }
    n = GetEnvironmentVariableA("APPDATA", d1, MAX_PATH);
    if (n && n < MAX_PATH) {
        wsprintfA(d2, "%s\\Dyna Pro Dynamometers\\Dyna Run V3\\System Data", d1); dirs[1] = d2;
    }
    {   // the shell's idea of the roaming AppData folder (what a VB6 app asks for), in case %APPDATA% differs
        typedef HRESULT (WINAPI *gfp_t)(HWND, int, HANDLE, DWORD, LPSTR);
        HMODULE sh = GetModuleHandleA("shell32.dll");
        gfp_t gfp = sh ? (gfp_t)GetProcAddress(sh, "SHGetFolderPathA") : NULL;
        if (gfp && gfp(NULL, 0x001a /* CSIDL_APPDATA */, NULL, 0, d1) == S_OK) {
            wsprintfA(d3, "%s\\Dyna Pro Dynamometers\\Dyna Run V3\\System Data", d1); dirs[2] = d3;
        }
    }
    return setup_helper_match(file, dirs, 3);
}

typedef BOOL (WINAPI *sx_t)(SHELLEXECUTEINFOA *);

// ShellExecuteExA for the setup helper WITHOUT elevation. Windows' installer detection (the file names say "Setup") would ask for administrator permission for
// the helper, which only writes HKCU\Software\DynaPro (docs\FIRST_START.md). The layer "RunAsInvoker" in __COMPAT_LAYER makes Windows start it as the user
// that runs DynaRun, as DynaRunFix's launcher does for DynaRun itself. The variable is set for the one call and put back at once (the child takes its
// environment when it is created, inside the call); it is not left in DynaRun's environment, whose other child processes are not changed (see clear_runasinvoker).
// The process environment is shared, so a program that DynaRun's other threads start in those few milliseconds would get the layer too; DynaRun starts nothing
// else at that moment (it is waiting on the form's OK), and g_helper_busy keeps a second helper out.
static BOOL shexec_as_invoker(sx_t sx, SHELLEXECUTEINFOA *se)
{
    char old[512], nw[560]; DWORD n, err; BOOL ok, was_set;
    old[0] = 0;
    SetLastError(0);
    n = GetEnvironmentVariableA("__COMPAT_LAYER", old, sizeof(old));
    was_set = n != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;       // n == 0 is both "not set" and "set to an empty value": put back exactly what was there
    if (n >= sizeof(old) || !df_compat_with_invoker(n ? old : NULL, nw, sizeof(nw))) {
        logf("setup helper: __COMPAT_LAYER is too long to add RunAsInvoker, started the usual way\r\n", 0, 0, 0);
        return sx(se);
    }
    SetEnvironmentVariableA("__COMPAT_LAYER", nw);
    ok = sx(se); err = GetLastError();
    SetEnvironmentVariableA("__COMPAT_LAYER", was_set ? old : NULL);     // NULL removes it; an empty value stays an empty value
    SetLastError(err);
    if (ok) logf("setup helper started without elevation (RunAsInvoker)\r\n", 0, 0, 0);
    return ok;
}

static LONG g_helper_busy;   // 1 while the setup helper is being waited for (the message loop below can bring a second request)

static HINSTANCE WINAPI H_ShellExecuteA(HWND w, LPCSTR verb, LPCSTR file, LPCSTR params, LPCSTR dir, INT show)
{
    typedef HINSTANCE (WINAPI *se_t)(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT);
    int i;
    // First-time setup helper (System Data\Setup_<n>.exe): run it to the end with the message loop
    // running, then restart DynaRun. It is started without elevation (shexec_as_invoker): no UAC prompt.
    // Only that exact file qualifies (see is_setup_helper); everything else goes straight to ShellExecuteA.
    if (is_setup_helper(file))
        do {
            SHELLEXECUTEINFOA se; sx_t sx = (sx_t)GetProcAddress(GetModuleHandleA("shell32.dll"), "ShellExecuteExA"); MSG m;
            char lang[64]; DWORD n = sizeof(lang) - 1, t, code = 0; HKEY k; BOOL on[N_OPT], quit = FALSE, failed = FALSE; WPARAM qcode = 0;
            if (!sx) break;
            if (InterlockedCompareExchange(&g_helper_busy, 1, 0)) {   // a second OK click while the first helper is still being waited for
                logf("setup helper is already running: this second request is not started %u %u %u\r\n", 0, 0, 0);
                return (HINSTANCE)42;
            }
            // Picked optional features, read while the selection form is still open. If its buttons cannot be read reliably
            // nothing is switched on and nothing is written: guessing could enable features the user did not choose.
            for (i = 0; i < N_OPT; i++) on[i] = FALSE;
            if (!read_options(on)) {
                for (i = 0; i < N_OPT; i++) on[i] = FALSE;
                logf("optional features: could not be read reliably, nothing is switched on (set them in DynaRun's system configuration) %u %u %u\r\n", 0, 0, 0);
            }
            // The helper writes Operation_Data\Default_Language = "English", over the language DynaRun stored
            // from the first-start language dialog a moment earlier; put DynaRun's value back afterwards.
            lang[0] = 0;
            if (!RegOpenKeyExA(HKEY_CURRENT_USER, LANG_KEY, 0, KEY_QUERY_VALUE, &k)) {
                if (RegQueryValueExA(k, "Default_Language", NULL, &t, (BYTE *)lang, &n) || t != REG_SZ) n = 0;
                lang[n] = 0;
                RegCloseKey(k);
            }
            { volatile char *z = (volatile char *)&se; for (i = 0; i < (int)sizeof(se); i++) z[i] = 0; }
            se.cbSize = sizeof(se); se.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC; se.hwnd = w;
            se.lpVerb = verb; se.lpFile = file; se.lpParameters = params; se.lpDirectory = dir; se.nShow = show;
            if (!shexec_as_invoker(sx, &se)) { logf("setup helper %s did not start: error %u %u\r\n", (DWORD)(UINT_PTR)file, GetLastError(), 0); InterlockedExchange(&g_helper_busy, 0); return (HINSTANCE)(UINT_PTR)SE_ERR_ACCESSDENIED; }
            logf("setup helper %s started, waiting for it %u %u\r\n", (DWORD)(UINT_PTR)file, 0, 0);
            if (se.hProcess) {
                while (!quit && MsgWaitForMultipleObjects(1, &se.hProcess, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
                    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
                        if (m.message == WM_QUIT) { quit = TRUE; qcode = m.wParam; break; }   // not swallowed: handed on below
                        TranslateMessage(&m); DispatchMessageA(&m);
                    }
                if (!quit && GetExitCodeProcess(se.hProcess, &code) && code != 0 && code != STILL_ACTIVE) failed = TRUE;   // exit code not observable: not counted as failed
                CloseHandle(se.hProcess);
            }
            if (quit) {
                PostQuitMessage((int)qcode);
                logf("setup helper: DynaRun is closing, not waiting for the helper any longer %u %u %u\r\n", 0, 0, 0);
                InterlockedExchange(&g_helper_busy, 0);
                return (HINSTANCE)42;
            }
            logf("setup helper finished, exit code %u %u %u\r\n", code, 0, 0);
            if (lang[0] && !RegOpenKeyExA(HKEY_CURRENT_USER, LANG_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k)) {
                char now[64]; DWORD m2 = sizeof(now) - 1;
                if (RegQueryValueExA(k, "Default_Language", NULL, &t, (BYTE *)now, &m2) || t != REG_SZ) m2 = 0;
                now[m2] = 0;
                if (lstrcmpA(now, lang) && !RegSetValueExA(k, "Default_Language", 0, REG_SZ, (const BYTE *)lang, lstrlenA(lang) + 1))
                    logf("kept the chosen language %s (helper had set %s) %u\r\n", (DWORD)(UINT_PTR)lang, (DWORD)(UINT_PTR)now, 0);
                RegCloseKey(k);
            }
            if (failed) logf("setup helper ended with a non-zero exit code: the optional features are not switched on %u %u %u\r\n", 0, 0, 0);
            else enable_options(on);
            if (!restart_dynarun()) {
                logf("DynaRun could not be restarted automatically: it has to be started again by hand %u %u %u\r\n", 0, 0, 0);
                start_hint(4, 0, NULL);
            }
            InterlockedExchange(&g_helper_busy, 0);
            return (HINSTANCE)42;
        } while (0);
    return ((se_t)r_shexec)(w, verb, file, params, dir, show);
}

// ---- connection diagnostics (dynapro.dll) ----
// Observed: when the Dyna Pro device cannot be opened (another program holds it) or does not answer, DynaRun shows
// nothing; after about 13 s (observed) it opens "USB data connection error" (Abort/Retry/Ignore),
// and with Ignore it keeps retrying. Nothing here changes the results of dynapro or DynaRun, it only reports:
//  - DPConnect (a VB Declare, so found through the GetProcAddress hook) logs its return value on every change.
//  - DPConnect failing without a break for DYNAFIX_CONNHINT_MS (default 2000) is logged and shows a short hint.
//  - the device open (dynapro.dll's CreateFileA import entry is observed: a device path, share mode 0); when it fails with a
//    sharing violation / access denied it is logged and a hint names the program that holds the device, if it
//    can be found (Windows' Restart Manager; it may not know a device path, then the hint has no name).
// Hints are small topmost windows that never take the focus, made on a thread of their own, gone after 10 s or on a
// click. DYNAFIX_CONNHINT=0 keeps the log lines only.
typedef struct { int kind; DWORD err; char path[MAX_PATH]; } hintreq_t;
typedef struct { WCHAR text[320]; HFONT font; } hintwin_t;

static BOOL g_hints = TRUE;
static DWORD g_hintms = 2000;
static FARPROC r_dpconnect, r_cfa;
static DWORD *g_cfaslot;                       // dynapro's IAT slot for CreateFileA
static BOOL g_cfalost;                         // another tool replaced our hook in that slot (logged once)
static LONG g_dppatched;                      // 0 = not yet, 1 = one thread is patching (claimed), 2 = done (g_cfaslot / r_cfa published)
static volatile LONG g_hintbusy;
static LONG g_connret = -1;                    // last DPConnect result (-1: none yet)
static DWORD g_conncalls, g_failn, g_failfirst, g_faillast, g_failhint, g_cfn, g_cflog, g_cfhint, g_cflast;

static BOOL zh_ui(void) { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE; }

#include "dynafix_hint.inc.c"

// dynapro's CreateFileA: only the device (\\?\ path) is looked at; everything else passes untouched
static HANDLE WINAPI H_CreateFileA(LPCSTR path, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD fl, HANDLE tmpl)
{
    HANDLE h = ((HANDLE (WINAPI *)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE))r_cfa)(path, acc, share, sa, disp, fl, tmpl);
    DWORD err = GetLastError();
    if (path && path[0] == '\\' && path[1] == '\\' && path[2] == '?' && path[3] == '\\') {
        DWORD now = GetTickCount();
        if (h == INVALID_HANDLE_VALUE) {
            g_cfn++; g_cflast = now;
            if (!g_cflog || now - g_cflog >= 5000) {
                char p[140];
                g_cflog = now ? now : 1;
                lstrcpynA(p, path, sizeof(p));
                logf("dynapro: opening the device failed, error %u (32 = in use by another program, 5 = access denied), %u failure(s) so far; %s\r\n", err, g_cfn, (DWORD)(UINT_PTR)p);
            }
            if ((err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) && (!g_cfhint || now - g_cfhint >= 60000)) {
                g_cfhint = now ? now : 1;
                start_hint(1, err, path);
            }
        } else if (g_cfn) {
            logf("dynapro: the device opened after %u failure(s)\r\n", g_cfn, 0, 0);
            g_cfn = 0; g_cflog = 0;
        }
    }
    SetLastError(err);
    return h;
}

static DWORD g_conn_abt;

static LONG WINAPI H_DPConnect(LONG mode)
{
    LONG r;
    DWORD now = GetTickCount();
    // USBFIX: DynaRun reconnecting while an abandoned DPSetData is still pending in dynapro.dll. The call is passed through
    // unchanged (and logged): blocking or dropping it would also stop the reconnect that lets the abandoned call end. The two do
    // overlap inside dynapro.dll then; whether that is harmless there is not known.
    if (g_sd_state == 2 && now - g_conn_abt >= 5000) {
        g_conn_abt = now;
        logf("DPConnect(%d) called while an abandoned DPSetData is still pending: passed through %u %u\r\n", (DWORD)mode, 0, 0);
    }
    r = ((LONG (WINAPI *)(LONG))r_dpconnect)(mode);
    now = GetTickCount();
    g_conncalls++;
    if (r != g_connret) {
        logf("DPConnect(%d) returned %d (call %u)\r\n", (DWORD)mode, (DWORD)r, g_conncalls);
        g_connret = r;
    }
    if (r == 0) {
        if (!g_failn || now - g_faillast > 5000) { g_failn = 0; g_failfirst = now; g_failhint = 0; }   // a long gap starts a new run
        g_failn++; g_faillast = now;
        if (!g_failhint && now - g_failfirst >= g_hintms) {
            g_failhint = 1;
            logf("DPConnect failed %u times in a row over %u ms: check the USB cable and the device's power\r\n", g_failn, now - g_failfirst, 0);
            // a device that another program holds already got its own hint
            if (!g_cfn || now - g_cflast > 5000) start_hint(2, 0, NULL);
        }
    } else if (g_failn) {
        logf("DPConnect succeeded again after %u failure(s) over %u ms\r\n", g_failn, now - g_failfirst, 0);
        g_failn = 0; g_failhint = 0;
    }
    return r;
}

static void patch_dynapro(void);   // defined after seh_run (it walks dynapro.dll's PE headers)

// called from CwpProc: patch dynapro once it is loaded, and chain on again if another hook took the slot over
static void conn_tick(void)
{
    static DWORD last;
    DWORD now = GetTickCount();
    if (now - last < 500) return;
    last = now;
    if (InterlockedCompareExchange(&g_dppatched, 2, 2) != 2) { patch_dynapro(); return; }   // acquire: g_cfaslot / r_cfa are final
    // If another tool takes the slot over later, it is left alone and logged once: chaining on again could end in a loop (that tool
    // may call our hook as its "next"), and the device hints are only a convenience.
    if (g_cfaslot && !g_cfalost && *g_cfaslot != (DWORD)(UINT_PTR)H_CreateFileA) {
        g_cfalost = TRUE;
        logf("dynapro.dll CreateFileA slot was changed by another hook: not chained again, device hints may stop %u %u %u\r\n", 0, 0, 0);
    }
}

static void patch_vb_screen(void);
static FARPROC setdata_attach(HMODULE m, FARPROC f);

static FARPROC WINAPI H_GetProcAddress(HMODULE m, LPCSTR name)
{
    typedef FARPROC (WINAPI *gpa_t)(HMODULE, LPCSTR);
    FARPROC f = ((gpa_t)r_gpa)(m, name);
    if (!f || (UINT_PTR)name < 0x10000) return f;          // ordinal
    // only shell32's own export (a Declare Lib "shell32"); another module exporting that name is left alone
    if (!lstrcmpA(name, "ShellExecuteA") && m && m == GetModuleHandleA("shell32.dll")) { r_shexec = f; return (FARPROC)H_ShellExecuteA; }
    if (!lstrcmpA(name, "DPConnect") && !r_dpconnect) {
        char mod[MAX_PATH]; DWORD n = GetModuleFileNameA(m, mod, MAX_PATH);
        if (df_is_dynapro(mod, n, MAX_PATH)) { r_dpconnect = f; patch_dynapro(); return (FARPROC)H_DPConnect; }
    }
    if (g_usbfix && !lstrcmpA(name, "DPSetData")) return setdata_attach(m, f);
    return f;
}

// An import hook, matched by the import's NAME (see patch_base).
//   *real = the function the hook calls on: the export, or the hook another tool left in the slot (chained)
//   alt   = the export of the API-set / forwarding module (kernelbase, gdi32full), only to tell it from a foreign hook
//   dll   = the module that exports `name`; imports from it, its forwarding module and api-ms-win-* / ext-ms-win-* match
//   raw, chain: set by the patcher (the plain export; the foreign hook *real was upgraded to, if any)
typedef struct { const char *name; FARPROC *real; FARPROC hook; FARPROC alt; const char *dll; FARPROC raw; FARPROC chain; } hook_t;

// ---- font face ----
// DynaRun's forms and controls ask for Arial, MS Sans Serif, Times New Roman, MingLiU, ...; their text
// is drawn in Windows' own UI font for the language instead: the face Windows itself uses for dialogs
// (Microsoft JhengHei UI on Traditional Chinese, Segoe UI on English Windows, ...), picked by the code
// page DynaRun runs with (under Locale Emulator that is LE's, i.e. Big5):
//   950 Microsoft JhengHei UI, 936 Microsoft YaHei UI, 932 Yu Gothic UI / Meiryo UI, 949 Malgun Gothic,
//   otherwise the system's message font (Segoe UI; Tahoma on XP).
// An older Windows without the face falls back along the list, finally to the message font.
// DYNAFIX_FONT=<face> picks a face, DYNAFIX_FONT=off switches the swap off.
//
// Only the faces in g_swap are replaced: symbol, digital and fixed-pitch faces (Wingdings, Courier New,
// 7-segment fonts, ...) and narrow / heavy display faces stay. The CreateFont* and GetStockObject imports
// of every module that draws DynaRun's text are redirected: DynaRun's own files, OCX controls (MSCOMCTL,
// MSFlexGrid, SSTab, the ProEssentials chart, ...), MSVBVM60, OLE (VB's StdFont), MFC42, THBResize and
// comctl32 (tooltips). Windows' own dialogs (file open, message boxes, menus) already use the UI font.
// Swapped fonts get the code page's charset (Big5 for 950), whatever charset the form asked for.
// DynaRun's labels and text boxes are sized for the original faces, and the UI faces have a taller line
// (JhengHei UI 1.27 em, Segoe UI 1.33, Arial 1.12, MingLiU 1.0): text of 16 px and up shrinks to
// DYNAFIX_FONT_SCALE percent (default 90). DYNAFIX_FONT_LINE=1 (experimental) gives text of 12 px and up
// the line height of the face it replaces instead (shrinking at most to DYNAFIX_FONT_SCALE percent).
//
// Without LE, on a DBCS system locale (e.g. zh-TW, code page 950) whose FontAssoc key lacks
// "Associated Charset\ANSI(00)=YES" (common when Windows was installed in English and the locale
// changed later), GDI converts text drawn in ANSI_CHARSET fonts with code page 1252, so DynaRun's
// Big5 labels come out as Latin letters. The launcher then sets DYNAFIX_CHARSET (136 for Big5) and
// ANSI_CHARSET fonts are created with that charset instead, which is what FontAssoc would do.

static FARPROC r_cfiA, r_cfiW, r_cfA, r_cfW, r_gso, r_gow;
static WCHAR g_fontW[LF_FACESIZE];
static char g_fontA[LF_FACESIZE];
static BOOL g_fontoff, g_fontready;
static LONG g_fontlog;
static int g_fontscale = 90;   // % of the requested em height for large text (DYNAFIX_FONT_SCALE)
static BOOL g_fontline;        // DYNAFIX_FONT_LINE=1: keep the replaced face's line height
static int g_uiline;           // line height of the UI face, 1/1000 em
static BYTE g_charset;         // DYNAFIX_CHARSET: replaces ANSI_CHARSET, 0 = off
static BYTE g_dbcs;            // charset of a DBCS code page, for swapped ANSI_CHARSET fonts
#define CS(c) (g_charset && (c) == ANSI_CHARSET ? g_charset : (c))


// 1: face to be swapped, 2: already the UI face (a copy of a swapped font, or asked for directly), 0: keep
static int swap_face(const WCHAR *face, BYTE cs)
{
    int i;
    if (!g_fontW[0] || g_fontoff || cs == SYMBOL_CHARSET || cs == OEM_CHARSET) return 0;
    if (!lstrcmpiW(face, g_fontW)) return 2;
    for (i = 0; g_swap[i].face; i++) if (!lstrcmpiW(face, g_swap[i].face)) return 1;
    return 0;
}

static int line_of(const WCHAR *face)
{
    int i;
    for (i = 0; g_swap[i].face; i++) if (!lstrcmpiW(face, g_swap[i].face)) return g_swap[i].line;
    return 1150;
}

// Size, charset and ClearType for the UI face. DynaRun's forms carry all kinds of charsets (0, 134 GB2312,
// 186 Baltic, ...) that the UI face may not have, and Windows would then pick another face (MingLiU) for
// it: the UI face gets the code page's charset (DEFAULT_CHARSET outside DBCS code pages).
// Large text (16 px and up) shrinks to DYNAFIX_FONT_SCALE percent (default 90); with DYNAFIX_FONT_LINE=1
// a swapped font of 12 px and up instead gets the line height of the face it replaces (a positive height
// is a cell height already). A copy of an already swapped font (kind 2) is not scaled again.
static void fit(LONG *height, BYTE *cs, BYTE *quality, int kind, const WCHAR *orig)
{
    if (kind == 1 && !g_fontline && *height <= -16) *height = MulDiv(*height, g_fontscale, 100);
    if (kind == 1 && g_fontline && *height <= -12 && g_uiline) {
        LONG em = -*height, cell = MulDiv(em, line_of(orig), 1000), ui = MulDiv(cell, 1000, g_uiline);
        if (ui < MulDiv(em, g_fontscale, 100)) *height = -MulDiv(em, g_fontscale, 100);
        else if (ui < em) *height = cell;          // cell height: the line is as tall as the original's
    }
    if (g_dbcs) *cs = g_dbcs;
    else if (*cs != ANSI_CHARSET) *cs = DEFAULT_CHARSET;
    *quality = CLEARTYPE_QUALITY;
}

// File name of the module that contains `addr` (for the log).
static const char *mod_name(const void *addr, char *buf)
{
    HMODULE m; char *p;
    lstrcpyA(buf, "?");
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m))
        GetModuleFileNameA(m, buf, MAX_PATH);
    for (p = buf + lstrlenA(buf); p > buf && p[-1] != '\\'; p--);
    return p;
}

// One line per face / charset / calling module (the same font is made again and again).
static void log_face(const WCHAR *face, LONG height, DWORD cs, int swapped, void *ret)
{
    static DWORD seen[200]; static LONG nseen;
    char a[LF_FACESIZE * 2], m[MAX_PATH], line[LF_FACESIZE * 2 + 60]; const char *mn; const WCHAR *p; DWORD k = cs * 31 + swapped; LONG i;
    mn = mod_name(ret, m);
    for (p = face; *p; p++) k = k * 131 + (*p | 0x20);
    for (i = 0; mn[i]; i++) k = k * 131 + (BYTE)(mn[i] | 0x20);
    EnterCriticalSection(&g_cs);
    for (i = 0; i < nseen; i++) if (seen[i] == k) break;
    if (i < nseen || nseen >= 200 || InterlockedIncrement(&g_fontlog) > 200) { LeaveCriticalSection(&g_cs); return; }
    seen[nseen++] = k;
    LeaveCriticalSection(&g_cs);
    w2u(a, sizeof(a), face);
    wsprintfA(line, "font '%s' %d charset %u %s", a, height, cs, swapped == 1 ? "-> swapped" : swapped ? "UI face" : "kept");
    logf("%s, from %s %u\r\n", (DWORD)(UINT_PTR)line, (DWORD)(UINT_PTR)mn, 0);
}

static HFONT font_w(const LOGFONTW *lf, void *ret)
{
    LOGFONTW f;
    int sw = lf ? swap_face(lf->lfFaceName, lf->lfCharSet) : 0;
    if (lf) log_face(lf->lfFaceName, lf->lfHeight, lf->lfCharSet, sw, ret);
    if (!sw && (!lf || CS(lf->lfCharSet) == lf->lfCharSet)) return ((HFONT (WINAPI *)(const LOGFONTW *))r_cfiW)(lf);
    f = *lf;
    f.lfCharSet = CS(f.lfCharSet);
    if (sw) { lstrcpynW(f.lfFaceName, g_fontW, LF_FACESIZE); fit(&f.lfHeight, &f.lfCharSet, &f.lfQuality, sw, lf->lfFaceName); }
    return ((HFONT (WINAPI *)(const LOGFONTW *))r_cfiW)(&f);
}

static HFONT WINAPI H_CreateFontIndirectW(const LOGFONTW *lf) { return font_w(lf, _ReturnAddress()); }

// ANSI face name (LOGFONTA's is char[32] and may have no terminator; CreateFontA's is any string) to a terminated WCHAR[LF_FACESIZE];
// a name of 32 characters or more is cut instead of failing and leaving w unterminated.
static void face_w(WCHAR *w, const char *a)
{
    int n = 0, r;
    while (n < LF_FACESIZE - 1 && a[n]) n++;
    r = n ? MultiByteToWideChar(CP_ACP, 0, a, n, w, LF_FACESIZE - 1) : 0;
    w[r] = 0;
}

static HFONT WINAPI H_CreateFontIndirectA(const LOGFONTA *lf)
{
    LOGFONTA f; WCHAR w[LF_FACESIZE];
    int sw;
    if (!lf) return ((HFONT (WINAPI *)(const LOGFONTA *))r_cfiA)(lf);
    face_w(w, lf->lfFaceName);
    sw = swap_face(w, lf->lfCharSet);
    log_face(w, lf->lfHeight, lf->lfCharSet, sw, _ReturnAddress());
    if (!sw && CS(lf->lfCharSet) == lf->lfCharSet) return ((HFONT (WINAPI *)(const LOGFONTA *))r_cfiA)(lf);
    f = *lf;
    f.lfCharSet = CS(f.lfCharSet);
    if (sw) { lstrcpynA(f.lfFaceName, g_fontA, LF_FACESIZE); fit(&f.lfHeight, &f.lfCharSet, &f.lfQuality, sw, w); }
    return ((HFONT (WINAPI *)(const LOGFONTA *))r_cfiA)(&f);
}

typedef HFONT (WINAPI *CreateFontW_t)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR);
typedef HFONT (WINAPI *CreateFontA_t)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR);

static HFONT WINAPI H_CreateFontW(int h, int w, int esc, int ori, int wt, DWORD it, DWORD ul, DWORD so, DWORD cs,
                                  DWORD op, DWORD cp, DWORD q, DWORD pf, LPCWSTR face)
{
    LONG hh = h; BYTE qq = (BYTE)q, cc = (BYTE)CS(cs);
    int sw = face ? swap_face(face, (BYTE)cs) : 0;
    if (face) log_face(face, h, cs, sw, _ReturnAddress());
    if (sw) { fit(&hh, &cc, &qq, sw, face); face = g_fontW; }
    return ((CreateFontW_t)r_cfW)(hh, w, esc, ori, wt, it, ul, so, cc, op, cp, qq, pf, face);
}

static HFONT WINAPI H_CreateFontA(int h, int w, int esc, int ori, int wt, DWORD it, DWORD ul, DWORD so, DWORD cs,
                                  DWORD op, DWORD cp, DWORD q, DWORD pf, LPCSTR face)
{
    LONG hh = h; BYTE qq = (BYTE)q, cc = (BYTE)CS(cs); WCHAR fw[LF_FACESIZE];
    int sw = 0;
    if (face) { face_w(fw, face); sw = swap_face(fw, (BYTE)cs); log_face(fw, h, cs, sw, _ReturnAddress()); }
    if (sw) { fit(&hh, &cc, &qq, sw, fw); face = g_fontA; }
    return ((CreateFontA_t)r_cfA)(hh, w, esc, ori, wt, it, ul, so, cc, op, cp, qq, pf, face);
}

// A stock font whose face is swapped (DEFAULT_GUI_FONT is MS Shell Dlg) is handed out as a copy made
// with font_w. Copies live as long as the process; one a caller deleted is made again.
static HFONT g_stock[DEFAULT_GUI_FONT + 1];
typedef int (WINAPI *GetObjectW_t)(HANDLE, int, LPVOID);

static HGDIOBJ WINAPI H_GetStockObject(int i)
{
    HGDIOBJ o = ((HGDIOBJ (WINAPI *)(int))r_gso)(i), r = o;
    LOGFONTW lf;
    if (!r_gow || !o || i < OEM_FIXED_FONT || i > DEFAULT_GUI_FONT || i == DEFAULT_PALETTE) return o;
    EnterCriticalSection(&g_cs);
    if (g_stock[i] && ((GetObjectW_t)r_gow)(g_stock[i], sizeof(lf), &lf)) r = g_stock[i];
    else if (((GetObjectW_t)r_gow)(o, sizeof(lf), &lf) == sizeof(lf) && swap_face(lf.lfFaceName, lf.lfCharSet) == 1) {   // else not SYSTEM_FONT & co.
        g_stock[i] = font_w(&lf, _ReturnAddress());
        logf("stock font %u replaced by %08X %u\r\n", (DWORD)i, (DWORD)(UINT_PTR)g_stock[i], 0);
        if (g_stock[i]) r = g_stock[i];
    }
    LeaveCriticalSection(&g_cs);
    return r;
}

static hook_t g_font_hooks[] = {
    { "CreateFontA",         &r_cfA,  (FARPROC)H_CreateFontA,         NULL, "gdi32" },
    { "CreateFontW",         &r_cfW,  (FARPROC)H_CreateFontW,         NULL, "gdi32" },
    { "CreateFontIndirectA", &r_cfiA, (FARPROC)H_CreateFontIndirectA, NULL, "gdi32" },
    { "CreateFontIndirectW", &r_cfiW, (FARPROC)H_CreateFontIndirectW, NULL, "gdi32" },
    { "GetStockObject",      &r_gso,  (FARPROC)H_GetStockObject,      NULL, "gdi32" },
    { 0 }
};

static hook_t g_attr_hooks[] = {
    { "GetFileAttributesA",   &r_gfaA,  (FARPROC)H_GetFileAttributesA,   NULL, "kernel32" },
    { "GetFileAttributesW",   &r_gfaW,  (FARPROC)H_GetFileAttributesW,   NULL, "kernel32" },
    { "GetFileAttributesExA", &r_gfaxA, (FARPROC)H_GetFileAttributesExA, NULL, "kernel32" },
    { "GetFileAttributesExW", &r_gfaxW, (FARPROC)H_GetFileAttributesExW, NULL, "kernel32" },
    { "FindFirstFileA",       &r_ffA,   (FARPROC)H_FindFirstFileA,       NULL, "kernel32" },
    { "FindFirstFileW",       &r_ffW,   (FARPROC)H_FindFirstFileW,       NULL, "kernel32" },
    { "FindNextFileA",        &r_fnA,   (FARPROC)H_FindNextFileA,        NULL, "kernel32" },
    { "FindNextFileW",        &r_fnW,   (FARPROC)H_FindNextFileW,        NULL, "kernel32" },
    { 0 }
};
static hook_t g_proc_hooks[] = {
    { "GetProcAddress",       &r_gpa,   (FARPROC)H_GetProcAddress, NULL, "kernel32" },
    { 0 }
};

// The module that exports the same functions through forwarding (kernel32 -> kernelbase, gdi32 -> gdi32full).
static HMODULE sibling_module(const char *dll)
{
    if (!lstrcmpiA(dll, "kernel32")) return GetModuleHandleA("kernelbase.dll");
    if (!lstrcmpiA(dll, "gdi32")) return GetModuleHandleA("gdi32full.dll");
    return NULL;
}

// Look up the functions to hook: *real = the export of h->dll, h->alt = the forwarding module's export.
static void resolve(hook_t *h)
{
    for (; h->name; h++) {
        char dn[32]; HMODULE m, sib;
        wsprintfA(dn, "%s.dll", h->dll);
        m = GetModuleHandleA(dn); sib = sibling_module(h->dll);
        *h->real = m ? GetProcAddress(m, h->name) : NULL;
        h->alt = sib ? GetProcAddress(sib, h->name) : NULL;
        h->raw = NULL; h->chain = NULL;
    }
}

// ---- IAT patching, by import NAME ----
// A slot is found by the name in the import name table (OriginalFirstThunk), walked in parallel with the address table
// (FirstThunk), never by what the slot holds: a shim or another tool may have replaced the slot already. The import's DLL
// must be the one that exports the function (kernel32, gdi32, ...), its forwarding module (kernelbase, gdi32full) or an
// api-ms-win-* / ext-ms-win-* API set (ordinal imports are skipped: no hook needs one).
// Chaining: when a slot holds something other than the export, i.e. another tool's hook X, *h->real is upgraded from the plain
// export to X (InterlockedExchangePointer, so a hook already running sees the old or the new value, both callable), and the
// hook calls on through X. Slots patched earlier, while *h->real was still the export, now chain through X as well: X sees
// their calls too, which is fine. Whichever module is walked first therefore never decides which tool is bypassed. One
// *h->real serves all slots of a hook, so a slot holding a different foreign hook Y once *h->real is on X is NOT patched
// (logged, counted as failed): patching it would bypass Y. A slot that already points to our hook is left alone
// (idempotent, never chained onto ourselves).
// A descriptor without an import name table (bound / stripped) cannot be matched by name: its slots are matched by
// address as before (logged), which a foreign hook defeats.
// Delay imports are matched by name the same way, but their slots hold a loader stub, which is replaced, not chained.
static BOOL dll_ok(const char *imp, const char *home)
{
    char lo[80], want[40];
    lstrcpynA(lo, imp, sizeof(lo)); CharLowerA(lo);
    if (!lstrcmpA(lo, "kernelbase.dll")) return !lstrcmpiA(home, "kernel32");
    if (!lstrcmpA(lo, "gdi32full.dll")) return !lstrcmpiA(home, "gdi32");
    if (!lstrcmpA(lo, "win32u.dll")) return FALSE;
    if (!lstrcmpA(lo, "ucrtbase.dll")) return FALSE;
    if (lo[0] == 'a' && lo[1] == 'p' && lo[2] == 'i' && lo[3] == '-') return !lstrcmpA(lstrcpynA(want, lo, 12), "api-ms-win-");
    if (lo[0] == 'e' && lo[1] == 'x' && lo[2] == 't' && lo[3] == '-') return !lstrcmpA(lstrcpynA(want, lo, 12), "ext-ms-win-");
    wsprintfA(want, "%s.dll", home);
    CharLowerA(want);
    return !lstrcmpA(lo, want);
}

// Point the slot at h->hook. Returns 1 patched, 0 left alone (already ours), -1 failed (logged).
static int patch_slot(const char *mod, hook_t *h, IMAGE_THUNK_DATA *t)
{
    FARPROC cur = (FARPROC)t->u1.Function;
    if (cur == h->hook) return 0;
    if (!*h->real || *h->real == h->hook) {
        logf("%s import of %s not patched: target function not resolved %u\r\n", (DWORD)(UINT_PTR)mod, (DWORD)(UINT_PTR)h->name, 0);
        return -1;
    }
    if (!h->raw) h->raw = *h->real;
    if (cur && cur != h->raw && cur != h->alt && cur != h->chain) {
        if (!h->chain) {
            h->chain = cur;
            InterlockedExchangePointer((PVOID volatile *)h->real, (PVOID)cur);
            logf("%s import of %s already held a hook: chained onto it\r\n", (DWORD)(UINT_PTR)mod, (DWORD)(UINT_PTR)h->name, 0);
        } else {
            logf("%s import of %s not patched: slot holds another hook (already chained onto one)\r\n", (DWORD)(UINT_PTR)mod, (DWORD)(UINT_PTR)h->name, 0);
            return -1;
        }
    }
    put_slot((DWORD *)&t->u1.Function, (DWORD)(UINT_PTR)h->hook);
    logf("patched %s import of %s (by name)\r\n", (DWORD)(UINT_PTR)mod, (DWORD)(UINT_PTR)h->name, 0);
    return 1;
}

static void log_summary(const char *mod, const char *tbl, int n, int failed)
{
    char s[96];
    if (!n && !failed) return;
    wsprintfA(s, "%s [%s]", mod, tbl);
    logf("import patch %s: %u patched, %u failed\r\n", (DWORD)(UINT_PTR)s, (DWORD)n, (DWORD)failed);
}

// ---- walking the headers of other modules ----
// A module can be unloaded on another thread while its tables are read (an OCX), and a module may have odd headers: every
// walk checks the headers and stays inside SizeOfImage, and runs under seh_run.
// Why not __try/__except: the compiler's x86 SEH needs the CRT (_except_handler3, _SEH_prolog and a load config for
// /SAFESEH), none of which exists with /NODEFAULTLIB (tried: LNK2001). seh_run is the same thing by hand: one
// EXCEPTION_REGISTRATION_RECORD on the stack whose handler, for any exception, sets the context back to the resume point
// (a plain ExceptionContinueExecution, so no unwinding is needed: the code in between is ours and holds nothing). The
// handler is not in a SafeSEH table, hence /SAFESEH:NO in build.cmd for dynafix.dll.
typedef void (__stdcall *seh_fn)(void *);
typedef struct { void *next, *handler; DWORD esp, ebp, eip; } seh_rec;

static EXCEPTION_DISPOSITION __cdecl seh_handler(EXCEPTION_RECORD *er, void *frame, CONTEXT *ctx, void *dc)
{
    seh_rec *r = (seh_rec *)frame;
    (void)dc;
    if ((er->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND | EXCEPTION_NONCONTINUABLE)) ||
        er->ExceptionCode == EXCEPTION_BREAKPOINT || er->ExceptionCode == EXCEPTION_SINGLE_STEP) return ExceptionContinueSearch;
    ctx->Eip = r->eip; ctx->Esp = r->esp; ctx->Ebp = r->ebp;
    return ExceptionContinueExecution;
}

#pragma warning(disable: 4733)        // "handler not registered as safe": see above
// Calls fn(arg); 1 if it returned, 0 if it raised an exception (the registers ebx/esi/edi are restored either way).
static __declspec(naked) int __stdcall seh_run(seh_fn fn, void *arg)
{
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        sub esp, 20                         // seh_rec at esp
        mov eax, fs:[0]
        mov [esp], eax
        mov dword ptr [esp + 4], offset seh_handler
        mov [esp + 8], esp
        mov [esp + 12], ebp
        mov dword ptr [esp + 16], offset resume
        mov fs:[0], esp
        push [ebp + 12]
        call dword ptr [ebp + 8]
        mov eax, 1
        jmp done
    resume:
        xor eax, eax
    done:
        mov ecx, [esp]
        mov fs:[0], ecx
        lea esp, [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 8
    }
}

// The 32-bit headers of the module at `base`, or NULL (no MZ / PE signature, not PE32, e_lfanew or section table outside the image).
static IMAGE_NT_HEADERS *nt_of(BYTE *base)
{
    IMAGE_NT_HEADERS *nt; LONG lfanew;
    if (!base || ((IMAGE_DOS_HEADER *)base)->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    lfanew = ((IMAGE_DOS_HEADER *)base)->e_lfanew;
    if (lfanew <= 0 || lfanew > 0x10000000) return NULL;
    nt = (IMAGE_NT_HEADERS *)(base + lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return NULL;
    if ((DWORD)lfanew + sizeof(IMAGE_NT_HEADERS) > nt->OptionalHeader.SizeOfImage) return NULL;
    return nt;
}

// RVA of data directory `idx` if it exists, is not empty and lies inside the image, else 0.
static DWORD dir_rva(IMAGE_NT_HEADERS *nt, DWORD idx)
{
    IMAGE_DATA_DIRECTORY *d; DWORD soi = nt->OptionalHeader.SizeOfImage;
    if (nt->OptionalHeader.NumberOfRvaAndSizes <= idx) return 0;
    d = &nt->OptionalHeader.DataDirectory[idx];
    if (!d->VirtualAddress || !d->Size || d->VirtualAddress >= soi || d->Size > soi - d->VirtualAddress) return 0;
    return d->VirtualAddress;
}

// Redirect the imports of the module at `base` named like one of hooks[] (see above); *pn counts the slots patched.
static void patch_base_walk(BYTE *base, const char *mod, const char *tbl, hook_t *hooks, int *pn)
{
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    hook_t *h;
    DWORD rva, soi;
    int n = 0, failed = 0, r;
    if (!(nt = nt_of(base)) || !(rva = dir_rva(nt, IMAGE_DIRECTORY_ENTRY_IMPORT))) return;   // resource-only
    soi = nt->OptionalHeader.SizeOfImage;
    imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva);
    for (; (BYTE *)(imp + 1) <= base + soi && imp->Name; imp++) {
        const char *dll;
        IMAGE_THUNK_DATA *names, *t;
        BOOL relevant = FALSE;
        if (!imp->FirstThunk || imp->FirstThunk >= soi || imp->Name >= soi - 1) continue;
        dll = (const char *)(base + imp->Name);
        for (h = hooks; h->name; h++) if (dll_ok(dll, h->dll)) { relevant = TRUE; break; }
        if (!relevant) continue;
        t = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        if (!imp->OriginalFirstThunk || imp->OriginalFirstThunk >= soi) {      // no import name table: by address
            logf("%s: import descriptor %s has no name table: matched by address %u\r\n", (DWORD)(UINT_PTR)mod, (DWORD)(UINT_PTR)dll, 0);
            for (; (BYTE *)(t + 1) <= base + soi && t->u1.Function; t++)
                for (h = hooks; h->name; h++)
                    if (dll_ok(dll, h->dll) && *h->real && ((FARPROC)t->u1.Function == *h->real || (FARPROC)t->u1.Function == h->alt || (FARPROC)t->u1.Function == h->raw)) {
                        if ((r = patch_slot(mod, h, t)) > 0) n++; else if (r < 0) failed++;
                        break;
                    }
            continue;
        }
        names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        for (; (BYTE *)(names + 1) <= base + soi && (BYTE *)(t + 1) <= base + soi && names->u1.AddressOfData; names++, t++) {
            const char *name;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (names->u1.AddressOfData >= soi - 2) continue;
            name = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            for (h = hooks; h->name; h++)
                if (!lstrcmpA(name, h->name) && dll_ok(dll, h->dll)) {
                    if ((r = patch_slot(mod, h, t)) > 0) n++; else if (r < 0) failed++;
                    break;
                }
        }
    }
    log_summary(mod, tbl, n, failed);
    *pn += n;
}

typedef struct { BYTE *base; const char *mod, *tbl; hook_t *hooks; int n; } walk_t;
static void __stdcall walk_base(void *a) { walk_t *w = (walk_t *)a; patch_base_walk(w->base, w->mod, w->tbl, w->hooks, &w->n); }

static int patch_base(BYTE *base, const char *mod, const char *tbl, hook_t *hooks)
{
    walk_t w; w.base = base; w.mod = mod; w.tbl = tbl; w.hooks = hooks; w.n = 0;
    if (!seh_run(walk_base, &w)) logf("patching the imports of %s failed (module gone?) %u %u\r\n", (DWORD)(UINT_PTR)mod, 0, 0);
    return w.n;
}

static int patch_imports(const char *mod, const char *tbl, hook_t *hooks) { return patch_base((BYTE *)GetModuleHandleA(mod), mod, tbl, hooks); }

// Delay-loaded imports (oleaut32 makes VB's StdFont fonts through them): their slots hold a loader stub
// until the first call, so they are matched by name and set to the hook either way.
static void patch_delay_walk(BYTE *base, const char *mod, const char *tbl, hook_t *hooks, int *pn)
{
    IMAGE_NT_HEADERS *nt; IMAGE_DELAYLOAD_DESCRIPTOR *d; hook_t *h; DWORD rva, soi; int n = 0, failed = 0;
    if (!(nt = nt_of(base)) || !(rva = dir_rva(nt, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT))) return;
    soi = nt->OptionalHeader.SizeOfImage;
    for (d = (IMAGE_DELAYLOAD_DESCRIPTOR *)(base + rva); (BYTE *)(d + 1) <= base + soi && d->DllNameRVA; d++) {
        const char *dll;
        IMAGE_THUNK_DATA *names, *t;
        if (!d->Attributes.RvaBased) continue;   // old VA-based tables
        if (d->DllNameRVA >= soi - 1) continue;
        dll = (const char *)(base + d->DllNameRVA);
        if (!d->ImportNameTableRVA || !d->ImportAddressTableRVA) {
            logf("%s: delay import descriptor %s has no name table: not patched %u\r\n", (DWORD)(UINT_PTR)mod, (DWORD)(UINT_PTR)dll, 0);
            failed++;
            continue;
        }
        if (d->ImportNameTableRVA >= soi || d->ImportAddressTableRVA >= soi) continue;
        names = (IMAGE_THUNK_DATA *)(base + d->ImportNameTableRVA);
        t = (IMAGE_THUNK_DATA *)(base + d->ImportAddressTableRVA);
        for (; (BYTE *)(names + 1) <= base + soi && (BYTE *)(t + 1) <= base + soi && names->u1.AddressOfData; names++, t++) {
            const char *name;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (names->u1.AddressOfData >= soi - 2) continue;
            name = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            for (h = hooks; h->name; h++)
                if (!lstrcmpA(name, h->name) && dll_ok(dll, h->dll)) {
                    FARPROC cur = (FARPROC)t->u1.Function;
                    if (cur == h->hook) break;
                    if (!*h->real) { logf("%s delay import of %s not patched: target function not resolved %u\r\n", (DWORD)(UINT_PTR)mod, (DWORD)(UINT_PTR)h->name, 0); failed++; break; }
                    put_slot((DWORD *)&t->u1.Function, (DWORD)(UINT_PTR)h->hook);
                    logf("patched %s delay import of %s (by name)\r\n", (DWORD)(UINT_PTR)mod, (DWORD)(UINT_PTR)h->name, 0);
                    n++;
                    break;
                }
        }
    }
    log_summary(mod, tbl, n, failed);
    *pn += n;
}

static void __stdcall walk_delay(void *a) { walk_t *w = (walk_t *)a; patch_delay_walk(w->base, w->mod, w->tbl, w->hooks, &w->n); }

static int patch_delay(BYTE *base, const char *mod, const char *tbl, hook_t *hooks)
{
    walk_t w; w.base = base; w.mod = mod; w.tbl = tbl; w.hooks = hooks; w.n = 0;
    if (!seh_run(walk_delay, &w)) logf("patching the delay imports of %s failed (module gone?) %u %u\r\n", (DWORD)(UINT_PTR)mod, 0, 0);
    return w.n;
}

// Chain on dynapro's KERNEL32 CreateFileA import slot (by name; what is there now, e.g. another tool's hook, is called next).
static void __stdcall walk_dynapro(void *a)
{
    BYTE *b = (BYTE *)a; IMAGE_NT_HEADERS *nt; IMAGE_IMPORT_DESCRIPTOR *imp; DWORD rva, soi;
    if (!(nt = nt_of(b)) || !(rva = dir_rva(nt, IMAGE_DIRECTORY_ENTRY_IMPORT))) return;
    soi = nt->OptionalHeader.SizeOfImage;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(b + rva); (BYTE *)(imp + 1) <= b + soi && imp->Name; imp++) {
        IMAGE_THUNK_DATA *names, *t;
        DWORD nrva = imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk;
        if (imp->Name >= soi || nrva >= soi || imp->FirstThunk >= soi) continue;
        names = (IMAGE_THUNK_DATA *)(b + nrva);
        t = (IMAGE_THUNK_DATA *)(b + imp->FirstThunk);
        if (lstrcmpiA((const char *)(b + imp->Name), "kernel32.dll")) continue;
        for (; (BYTE *)(names + 1) <= b + soi && (BYTE *)(t + 1) <= b + soi && names->u1.AddressOfData; names++, t++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal) || names->u1.AddressOfData >= soi - 2) continue;
            if (lstrcmpA((const char *)((IMAGE_IMPORT_BY_NAME *)(b + names->u1.AddressOfData))->Name, "CreateFileA")) continue;
            if (t->u1.Function == (DWORD)(UINT_PTR)H_CreateFileA) return;
            InterlockedExchangePointer((PVOID volatile *)&r_cfa, (PVOID)(UINT_PTR)t->u1.Function);   // before the slot is switched to H_CreateFileA
            put_slot((DWORD *)&t->u1.Function, (DWORD)(UINT_PTR)H_CreateFileA);
            InterlockedExchangePointer((PVOID volatile *)&g_cfaslot, (PVOID)&t->u1.Function);
            logf("patched dynapro.dll import of CreateFileA %u %u %u\r\n", 0, 0, 0);
            return;
        }
    }
}

static void patch_dynapro(void)
{
    BYTE *b = (BYTE *)GetModuleHandleA("dynapro.dll");
    if (!b) return;
    if (InterlockedCompareExchange(&g_dppatched, 1, 0) != 0) return;     // claim: one thread patches, the others see 1 or 2 and leave
    if (!seh_run(walk_dynapro, b)) logf("patching the imports of dynapro.dll failed (module gone?) %u %u %u\r\n", 0, 0, 0);
    InterlockedExchange(&g_dppatched, 2);                                // publish (full barrier): the slot and r_cfa were written above
}

// ---- DPSetData watchdog (DYNAFIX_USBFIX=1) ----
// EXPERIMENTAL, off by default, not verified with real hardware under all conditions.
// DynaRun calls dynapro.dll's DPSetData on its GUI thread. Observed: when the USB device stops answering, the call does not
// return and DynaRun freezes. With
// DYNAFIX_USBFIX=1, DPSetData (mode 3) runs on a helper thread of this dll while the GUI thread waits for it in 100 ms steps and
// watches the connection through the exported DPConnStatus: when the call has run 2 s or more and DPConnStatus reports the
// connection down, the GUI thread stops waiting and DPSetData returns 0; DynaRun's own reconnect logic (DPConnect) then takes over.
// The abandoned call stays on the helper thread. Its WaitForSingleObject calls are redirected (see H_WaitForSingleObject) so that
// an event wait returns after 250 ms slices once the call is abandoned; the call can then end after DynaRun has reconnected
// (logged as "the abandoned call returned after N ms").
// What this does and does not guarantee (honest list):
//  - While an abandoned call is pending, every DPSetData call, whatever its mode, returns 0 at once without entering dynapro.dll;
//    DynaRun sees "no data written" for those calls. If the abandoned call never returns, that stays so until DynaRun is restarted:
//    after SD_DROP_CAP consecutive drops (and 20 s) a one-time notice suggests restarting DynaRun, and the drops keep being logged.
//  - Other dynapro exports are NOT held back: DynaRun's GUI thread keeps calling DPConnStatus and DPConnect while the helper
//    thread is inside dynapro.dll (DPConnStatus is called here every 100 ms from the 2 s mark on; DPConnect is passed through, see
//    H_DPConnect). So calls into dynapro.dll do overlap during a hang. Whether that is safe inside dynapro.dll is not known; this is
//    the price of letting the reconnect happen at all.
//  - Concurrent mode-3 callers are serialised (g_sd_cs), one at a time; only a call that finds a pending abandoned one is dropped.
//    Calls with another mode are passed straight through on the caller's thread and may overlap a running mode-3 call.
//  - The GUI thread waits with a plain WaitForSingleObject (at most about 2 s once the connection is down), so it does not pump
//    messages meanwhile: a message pump here would let DynaRun's own handlers run inside the DPSetData call.
//  - Assumptions: dynapro.dll tolerates DPSetData being called from a thread other than DynaRun's own (the helper thread); p holds
//    3 LONGs (that is what was observed in DynaRun's calls). p is copied for the helper, exactly 3 LONGs, into a padded buffer; on normal completion the helper's copy is
//    written back to p before DPSetData returns, so a caller reading p afterwards sees the same data as without the fix. After an
//    abandon, p is not touched again (it may be gone).
// If the hook cannot be set, the watchdog is not enabled (an abandoned call could never end).
// No file of Dyna Pro is modified. The exported DPSetData / DPConnStatus of dynapro.dll are called, and while DynaRun runs,
// dynapro.dll's WaitForSingleObject import-table entry is redirected in memory (by name, like the other imports here; it acts for
// the helper thread only). The DPSetData that DynaRun looks up through GetProcAddress is replaced by the wrapper. Only DPSetData
// calls with mode 3 (the only mode observed in use) go through the watchdog. Without the flag none of this is installed.
static FARPROC r_dpsetdata, r_dpstatus, r_wfso;
static HANDLE g_sd_go, g_sd_done;
static DWORD g_sd_tid;
static LONG g_sd_once;                      // 0 not yet, 1 being set up, 2 on, 3 off (setup failed)
static CRITICAL_SECTION g_sd_cs;            // serialises mode-3 callers
// The helper's copy of p: exactly 3 LONGs are copied in and out (that is what DynaRun passes, an assumption). Should DPSetData ever
// write more than that, it lands in the padding of this struct (a whole page of LONGs) and not in the statics around it.
static struct { LONG p[3]; LONG pad[253]; } g_sd_buf;
#define g_sd_p g_sd_buf.p
static LONG g_sd_hasp, g_sd_res;
static LONG g_sd_dropped;                   // consecutive calls dropped while a call is abandoned
static BOOL g_sd_notice;                    // the restart notice was shown (once per process)
static DWORD g_sd_dropt, g_sd_othert, g_sd_abt;
static DWORD g_sd_err;                      // GetLastError() as the helper's call left it (handed to the caller with the result)
#define SD_DROP_CAP 300

typedef LONG (WINAPI *setdata_t)(LONG, LONG *);
typedef LONG (WINAPI *dpstatus_t)(void);
typedef FARPROC (WINAPI *sd_gpa_t)(HMODULE, LPCSTR);
typedef LONG (NTAPI *NtQueryObject_t)(HANDLE, int, PVOID, ULONG, PULONG);

// Is h an event? 1 yes, 0 no (another kind of object), -1 cannot be told. Asked of the system (NtQueryObject, ObjectTypeInformation,
// a documented class: the type name comes first in the result), nothing about dynapro.dll is read.
static int handle_is_event(HANDLE h)
{
    static NtQueryObject_t q; static BOOL init;
    struct { USHORT Length, MaximumLength; PWSTR Buffer; } *us; BYTE buf[256]; ULONG len = 0; const WCHAR *b;
    us = (void *)buf;
    if (!init) { HMODULE nt = GetModuleHandleA("ntdll.dll"); q = nt ? (NtQueryObject_t)GetProcAddress(nt, "NtQueryObject") : NULL; init = TRUE; }
    if (!q || q(h, 2 /* ObjectTypeInformation */, buf, sizeof(buf), &len) < 0) return -1;
    if (!us->Buffer || us->Length != 5 * sizeof(WCHAR)) return 0;
    b = us->Buffer;
    return (b[0] | 0x20) == 'e' && (b[1] | 0x20) == 'v' && (b[2] | 0x20) == 'e' && (b[3] | 0x20) == 'n' && (b[4] | 0x20) == 't' ? 1 : 0;
}

static DWORD WINAPI H_WaitForSingleObject(HANDLE h, DWORD ms)
{
    int ev = -2;
    if (GetCurrentThreadId() != g_sd_tid || ms != INFINITE) return ((DWORD (WINAPI *)(HANDLE, DWORD))r_wfso)(h, ms);
    // The helper's infinite waits are made to return every 250 ms. Once the call is abandoned, a wait on an event returns
    // WAIT_TIMEOUT after a slice instead of waiting on, so the helper can end after DynaRun has reconnected. A wait on anything else
    // (a mutex, a semaphore, a thread...) is not cut short: handing a timeout to a wait for ownership could break what the caller
    // relies on, so such a call stays blocked, and so does one whose kind cannot be told (-1). The substitution
    // for an event is an ASSUMPTION (that dynapro.dll treats a timeout of such a wait as "not signalled yet" and ends the call), not a verified fact.
    for (;;) {
        DWORD r = ((DWORD (WINAPI *)(HANDLE, DWORD))r_wfso)(h, 250);
        if (r != WAIT_TIMEOUT) {
            if (r == WAIT_FAILED) Sleep(250);   // closed handle: no busy spin
            return r;
        }
        if (g_sd_state == 2) {
            if (ev == -2) {
                ev = handle_is_event(h);
                logf("DPSetData: the abandoned call waits on %s object %u %u\r\n", (DWORD)(UINT_PTR)(ev == 1 ? "an event" : ev == 0 ? "a non-event" : "an unknown kind of"), 0, 0);
            }
            if (ev == 1) return WAIT_TIMEOUT;
        }
    }
}

static DWORD WINAPI setdata_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        LONG prev;
        DWORD t;
        WaitForSingleObject(g_sd_go, INFINITE);
        t = GetTickCount();
        SetLastError(0);
        g_sd_res = ((setdata_t)r_dpsetdata)(3, g_sd_hasp ? g_sd_p : NULL);
        g_sd_err = GetLastError();
        prev = InterlockedCompareExchange(&g_sd_state, 3, 1);
        if (prev == 1) SetEvent(g_sd_done);
        else if (prev == 2) {
            logf("DPSetData: the abandoned call returned after %u ms\r\n", GetTickCount() - t, 0, 0);
            InterlockedExchange(&g_sd_dropped, 0);
            InterlockedExchange(&g_sd_state, 0);
        }
    }
}

// A call that is not let into dynapro.dll while an abandoned call is pending: counted, logged at most every 5 s, returns 0.
// After SD_DROP_CAP consecutive drops, 20 s or more after the abandon, a one-time notice suggests restarting DynaRun.
static LONG sd_drop(void)
{
    DWORD now = GetTickCount();
    LONG d = InterlockedIncrement(&g_sd_dropped);
    if (d >= SD_DROP_CAP && now - g_sd_abt >= 20000 && !g_sd_notice) {
        g_sd_notice = TRUE;
        logf("DPSetData: %u calls dropped, the abandoned call has not returned for %u ms: suggesting a restart of DynaRun\r\n", (DWORD)d, now - g_sd_abt, 0);
        start_hint(3, 0, NULL);
    }
    if (now - g_sd_dropt >= 5000) {
        g_sd_dropt = now;
        logf("DPSetData: %u call(s) dropped while an abandoned call is still running\r\n", (DWORD)d, 0, 0);
    }
    SetLastError(ERROR_TIMEOUT);   // 0 returned without the call having been made: not a stale error code of something else
    return 0;
}

// The helper finished while the caller was still waiting: hand its result and its copy of p back.
static LONG sd_finish(LONG *p, DWORD *le)
{
    LONG r = g_sd_res, i;
    *le = g_sd_err;
    if (p && g_sd_hasp) for (i = 0; i < 3; i++) p[i] = g_sd_p[i];
    InterlockedExchange(&g_sd_dropped, 0);
    InterlockedExchange(&g_sd_state, 0);
    return r;
}

static LONG WINAPI H_DPSetData(LONG mode, LONG *p)
{
    DWORD t0, el, le;
    LONG prev, i, r;
    if (g_sd_state == 2) return sd_drop();   // an abandoned call is still pending in dynapro.dll: no call of any mode may enter it
    if (mode != 3) {   // only mode 3 goes through the watchdog; anything else runs on the caller's thread, unchanged
        DWORD now = GetTickCount();
        if (now - g_sd_othert >= 5000) { g_sd_othert = now; logf("DPSetData called with mode %u: passed on directly, no watchdog %u %u\r\n", (DWORD)mode, 0, 0); }
        return ((setdata_t)r_dpsetdata)(mode, p);
    }
    EnterCriticalSection(&g_sd_cs);          // concurrent mode-3 callers take turns
    if (InterlockedCompareExchange(&g_sd_state, 1, 0) != 0) {   // abandoned while this caller waited its turn
        LeaveCriticalSection(&g_sd_cs);
        return sd_drop();
    }
    g_sd_hasp = p != NULL;
    if (p) for (i = 0; i < 3; i++) g_sd_p[i] = p[i];
    ResetEvent(g_sd_done);
    SetEvent(g_sd_go);
    t0 = GetTickCount();
    for (;;) {
        if (WaitForSingleObject(g_sd_done, 100) == WAIT_OBJECT_0) {
            r = sd_finish(p, &le);
            LeaveCriticalSection(&g_sd_cs);
            SetLastError(le);
            return r;
        }
        el = GetTickCount() - t0;
        if (el >= 2000) {
            BOOL alive = ((dpstatus_t)r_dpstatus)() != 0;   // read once per decision
            if (alive) continue;
            prev = InterlockedCompareExchange(&g_sd_state, 2, 1);
            if (prev == 1) {
                g_sd_abt = GetTickCount();
                InterlockedExchange(&g_sd_dropped, 0);
                logf("usbfix: DPSetData blocked %u ms with the device connection down: returned 0, DynaRun reconnects\r\n", el, 0, 0);
                LeaveCriticalSection(&g_sd_cs);
                SetLastError(ERROR_TIMEOUT);
                return 0;
            }
            WaitForSingleObject(g_sd_done, INFINITE);   // the helper finished at this very moment
            r = sd_finish(p, &le);
            LeaveCriticalSection(&g_sd_cs);
            SetLastError(le);
            return r;
        }
    }
}

static FARPROC setdata_attach(HMODULE m, FARPROC f)
{
    char mod[MAX_PATH]; DWORD n;
    static hook_t tbl[] = { { "WaitForSingleObject", &r_wfso, (FARPROC)H_WaitForSingleObject, NULL, "kernel32" }, { 0 } };
    HANDLE th;
    if (!f) return f;
    n = GetModuleFileNameA(m, mod, MAX_PATH);
    if (!df_is_dynapro(mod, n, MAX_PATH)) return f;
    if (InterlockedCompareExchange(&g_sd_once, 1, 0) != 0) return g_sd_once == 2 ? (FARPROC)H_DPSetData : f;
    r_dpsetdata = f;
    r_dpstatus = ((sd_gpa_t)r_gpa)(m, "DPConnStatus");
    if (!r_dpstatus) { logf("DPSetData watchdog off: DPConnStatus not found %u %u %u\r\n", 0, 0, 0); InterlockedExchange(&g_sd_once, 3); return f; }
    InitializeCriticalSection(&g_sd_cs);
    g_sd_go = CreateEventA(NULL, FALSE, FALSE, NULL);
    g_sd_done = CreateEventA(NULL, FALSE, FALSE, NULL);
    th = g_sd_go && g_sd_done ? CreateThread(NULL, 0, setdata_thread, NULL, 0, &g_sd_tid) : NULL;
    if (!th) { logf("DPSetData watchdog off: helper thread not created %u %u %u\r\n", 0, 0, 0); InterlockedExchange(&g_sd_once, 3); return f; }
    CloseHandle(th);
    resolve(tbl);
    n = patch_imports("dynapro.dll", "setdata", tbl);
    if (n <= 0) {
        // Without the redirect an abandoned call could never end: every later DPSetData would be dropped for good. Better not to start.
        logf("DPSetData watchdog off: the hook could not be set %u %u %u\r\n", 0, 0, 0);
        InterlockedExchange(&g_sd_once, 3);
        return f;
    }
    logf("DPSetData watchdog on (usbfix), %u hook(s) set\r\n", (DWORD)n, 0, 0);
    InterlockedExchange(&g_sd_once, 2);
    return (FARPROC)H_DPSetData;
}

// Redirect THBRes25.dll's import of user32!PostMessageA.
static void patch_thbresize(void)
{
    static hook_t post[] = { { "PostMessageA", (FARPROC *)&g_realPost, (FARPROC)HookedPostMessageA, NULL, "user32" }, { 0 } };
    if (!GetModuleHandleA("THBRes25.dll")) return;
    g_patched = TRUE;
    resolve(post);
    if (!patch_imports("THBRes25.dll", "thbresize", post))
        logf("THBRes25 PostMessageA import not found %u %u %u\r\n", 0, 0, 0);
}

static void patch_attr(void)
{
    if (!g_vbpatched && GetModuleHandleA("MSVBVM60.DLL")) {
        g_vbpatched = TRUE;
        resolve(g_attr_hooks);
        patch_imports("MSVBVM60.DLL", "attr", g_attr_hooks);
        resolve(g_proc_hooks);
        if (!patch_imports("MSVBVM60.DLL", "proc", g_proc_hooks)) logf("MSVBVM60 GetProcAddress import not found: ShellExecute fix inactive %u %u %u\r\n", 0, 0, 0);
        patch_vb_screen();
    }
    if (g_vbpatched && !g_fsopatched && GetModuleHandleA("scrrun.dll")) {
        g_fsopatched = TRUE;
        patch_imports("scrrun.dll", "attr", g_attr_hooks);
    }
}

// ---- diagnosis: text VB draws itself (labels) ----
// Logs each distinct string MSVBVM60 draws (first 150): font, size, box. Windowless labels such as the
// copyright line have no window whose font could be read.
static FARPROC r_dtA, r_dtW, r_etoA, r_etoW, r_toA;
static LONG g_textlog;

static void log_text(const char *api, HDC dc, const WCHAR *w, int n, int x, int y, const RECT *r)
{
    static DWORD seen[160]; static int nseen;
    char t[100], a[LF_FACESIZE * 2], line[300]; LOGFONTW lf; HFONT f; TEXTMETRICW tm; DWORD k = 7; int i;
    if (n < 0) n = lstrlenW(w);
    if (n > 40) n = 40;
    for (i = 0; i < n; i++) k = k * 131 + w[i];
    EnterCriticalSection(&g_cs);
    for (i = 0; i < nseen; i++) if (seen[i] == k) break;
    if (i < nseen || nseen >= 160 || InterlockedIncrement(&g_textlog) > 150) { LeaveCriticalSection(&g_cs); return; }
    seen[nseen++] = k;
    LeaveCriticalSection(&g_cs);
    i = WideCharToMultiByte(CP_UTF8, 0, w, n, t, sizeof(t) - 1, NULL, NULL); t[i] = 0;
    for (i = 0; t[i]; i++) if (t[i] == '\r' || t[i] == '\n') t[i] = '|';
    a[0] = 0; lf.lfHeight = 0; tm.tmHeight = 0;
    if ((f = (HFONT)GetCurrentObject(dc, OBJ_FONT)) && GetObjectW(f, sizeof(lf), &lf) == sizeof(lf))
        w2u(a, sizeof(a), lf.lfFaceName);
    GetTextMetricsW(dc, &tm);
    if (r) wsprintfA(line, "%s '%s' font '%s' %d (line %d) box %dx%d", api, t, a, lf.lfHeight, tm.tmHeight, r->right - r->left, r->bottom - r->top);
    else wsprintfA(line, "%s '%s' font '%s' %d (line %d) at %d,%d", api, t, a, lf.lfHeight, tm.tmHeight, x, y);
    logf("text %s %u %u\r\n", (DWORD)(UINT_PTR)line, 0, 0);
}

static void log_textA(const char *api, HDC dc, const char *s, int n, int x, int y, const RECT *r)
{
    WCHAR w[64]; int m;
    if (n < 0) n = lstrlenA(s);
    m = MultiByteToWideChar(CP_ACP, 0, s, n > 60 ? 60 : n, w, 63);
    w[m] = 0;
    log_text(api, dc, w, m, x, y, r);
}

static int WINAPI H_DrawTextA(HDC dc, LPCSTR s, int n, LPRECT r, UINT fl)
{ if (s) log_textA("DrawTextA", dc, s, n, 0, 0, r); return ((int (WINAPI *)(HDC, LPCSTR, int, LPRECT, UINT))r_dtA)(dc, s, n, r, fl); }
static int WINAPI H_DrawTextW(HDC dc, LPCWSTR s, int n, LPRECT r, UINT fl)
{ if (s) log_text("DrawTextW", dc, s, n, 0, 0, r); return ((int (WINAPI *)(HDC, LPCWSTR, int, LPRECT, UINT))r_dtW)(dc, s, n, r, fl); }
static BOOL WINAPI H_ExtTextOutA(HDC dc, int x, int y, UINT o, const RECT *r, LPCSTR s, UINT n, const INT *dx)
{ if (s && n) log_textA("ExtTextOutA", dc, s, n, x, y, (o & ETO_CLIPPED) ? r : NULL); return ((BOOL (WINAPI *)(HDC, int, int, UINT, const RECT *, LPCSTR, UINT, const INT *))r_etoA)(dc, x, y, o, r, s, n, dx); }
static BOOL WINAPI H_ExtTextOutW(HDC dc, int x, int y, UINT o, const RECT *r, LPCWSTR s, UINT n, const INT *dx)
{ if (s && n) log_text("ExtTextOutW", dc, s, n, x, y, (o & ETO_CLIPPED) ? r : NULL); return ((BOOL (WINAPI *)(HDC, int, int, UINT, const RECT *, LPCWSTR, UINT, const INT *))r_etoW)(dc, x, y, o, r, s, n, dx); }
static BOOL WINAPI H_TextOutA(HDC dc, int x, int y, LPCSTR s, int n)
{ if (s && n) log_textA("TextOutA", dc, s, n, x, y, NULL); return ((BOOL (WINAPI *)(HDC, int, int, LPCSTR, int))r_toA)(dc, x, y, s, n); }

// VB lays out its labels (line spacing, AutoSize, TextHeight) with GetTextMetrics. The UI face has a taller
// line than the faces the labels were made for (JhengHei UI 1.27 em, Arial 1.12, MingLiU 1.0), so a two-line
// label (the copyright line) no longer fits. For the UI face VB is told a line of DYNAFIX_FONT_LINEH/1000 em
// (default 1120, Arial's): lines move closer together, the text itself keeps its size.
static FARPROC r_gtmA, r_gtmW;
static int g_lineh = 1120;

static void tight(HDC dc, LONG *h, LONG *asc, LONG *il)
{
    WCHAR face[LF_FACESIZE]; LONG em = *h - *il, nh;
    if (!g_fontW[0] || g_fontoff || g_lineh <= 0 || !GetTextFaceW(dc, LF_FACESIZE, face) || lstrcmpiW(face, g_fontW)) return;
    nh = MulDiv(em, g_lineh, 1000);
    if (nh >= *h) return;
    *asc -= *h - nh; *il -= *h - nh; *h = nh;
    if (*il < 0) *il = 0;
}

static BOOL WINAPI H_GetTextMetricsA(HDC dc, LPTEXTMETRICA tm)
{
    BOOL ok = ((BOOL (WINAPI *)(HDC, LPTEXTMETRICA))r_gtmA)(dc, tm);
    if (ok) tight(dc, &tm->tmHeight, &tm->tmAscent, &tm->tmInternalLeading);
    return ok;
}
static BOOL WINAPI H_GetTextMetricsW(HDC dc, LPTEXTMETRICW tm)
{
    BOOL ok = ((BOOL (WINAPI *)(HDC, LPTEXTMETRICW))r_gtmW)(dc, tm);
    if (ok) tight(dc, &tm->tmHeight, &tm->tmAscent, &tm->tmInternalLeading);
    return ok;
}

static hook_t g_text_hooks[] = {
    { "GetTextMetricsA", &r_gtmA, (FARPROC)H_GetTextMetricsA, NULL, "gdi32" },
    { "GetTextMetricsW", &r_gtmW, (FARPROC)H_GetTextMetricsW, NULL, "gdi32" },
    { "DrawTextA",   &r_dtA,  (FARPROC)H_DrawTextA,   NULL, "user32" },
    { "DrawTextW",   &r_dtW,  (FARPROC)H_DrawTextW,   NULL, "user32" },
    { "ExtTextOutA", &r_etoA, (FARPROC)H_ExtTextOutA, NULL, "gdi32" },
    { "ExtTextOutW", &r_etoW, (FARPROC)H_ExtTextOutW, NULL, "gdi32" },
    { "TextOutA",    &r_toA,  (FARPROC)H_TextOutA,    NULL, "gdi32" },
    { 0 }
};

static void patch_text(void)
{
    static BOOL done;
    if (done || !GetModuleHandleA("MSVBVM60.DLL")) return;
    done = TRUE;
    resolve(g_text_hooks);
    patch_imports("MSVBVM60.DLL", "text", g_text_hooks);
}

// Windows' UI font for the code page: the first face of the list that is installed.
static int CALLBACK font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM l)
{ *(BOOL *)l = TRUE; return 0; }

static BOOL installed(const WCHAR *face)
{
    LOGFONTW lf; HDC dc = GetDC(NULL); BOOL found = FALSE; int i;
    { volatile char *z = (volatile char *)&lf; for (i = 0; i < (int)sizeof(lf); i++) z[i] = 0; }
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    if (dc) { EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)font_found, (LPARAM)&found, 0); ReleaseDC(NULL, dc); }
    return found;
}

static void pick_font(void)
{
    static const struct { UINT cp; BYTE cs; const WCHAR *face[3]; } t[] = {
        { 950, CHINESEBIG5_CHARSET, { L"Microsoft JhengHei UI", L"Microsoft JhengHei", 0 } },
        { 936, GB2312_CHARSET,      { L"Microsoft YaHei UI", L"Microsoft YaHei", 0 } },
        { 932, SHIFTJIS_CHARSET,    { L"Yu Gothic UI", L"Meiryo UI", 0 } },
        { 949, HANGUL_CHARSET,      { L"Malgun Gothic", 0 } },
    };
    UINT acp = GetACP(); int i, j; char a[LF_FACESIZE * 2];
    g_fontready = TRUE;
    for (i = 0; i < 4; i++) if (t[i].cp == acp) g_dbcs = t[i].cs;
    if (g_fontoff) { logf("font swap off (DYNAFIX_FONT=off) %u %u %u\r\n", 0, 0, 0); return; }
    for (i = 0; i < 4 && !g_fontW[0]; i++)
        for (j = 0; t[i].cp == acp && t[i].face[j] && !g_fontW[0]; j++)
            if (installed(t[i].face[j])) lstrcpynW(g_fontW, t[i].face[j], LF_FACESIZE);
    if (!g_fontW[0]) {
        NONCLIENTMETRICSW m;
        { volatile char *z = (volatile char *)&m; for (i = 0; i < (int)sizeof(m); i++) z[i] = 0; }
        m.cbSize = sizeof(m);
        if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, m.cbSize, &m, 0)) {
            m.cbSize = sizeof(m) - sizeof(int);           // XP's NONCLIENTMETRICS has no iPaddedBorderWidth
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, m.cbSize, &m, 0);
        }
        lstrcpynW(g_fontW, m.lfMessageFont.lfFaceName, LF_FACESIZE);
    }
    // the ANSI name may need more bytes than LF_FACESIZE (double-byte code pages): then none is used (the system default face), never a cut-off name
    if (!WideCharToMultiByte(CP_ACP, 0, g_fontW, -1, g_fontA, LF_FACESIZE, NULL, NULL)) g_fontA[0] = 0;
    g_fontA[LF_FACESIZE - 1] = 0;
    {   // its line height, measured at 100 px
        LOGFONTW lf; TEXTMETRICW tm; HDC dc = CreateCompatibleDC(NULL); HFONT f, o;
        { volatile char *z = (volatile char *)&lf; for (i = 0; i < (int)sizeof(lf); i++) z[i] = 0; }
        lf.lfHeight = -100; lf.lfCharSet = g_dbcs ? g_dbcs : DEFAULT_CHARSET;
        lstrcpynW(lf.lfFaceName, g_fontW, LF_FACESIZE);
        if (dc && (f = CreateFontIndirectW(&lf))) {
            o = SelectObject(dc, f);
            if (GetTextMetricsW(dc, &tm)) g_uiline = tm.tmHeight * 10;
            SelectObject(dc, o); DeleteObject(f);
        }
        if (dc) DeleteDC(dc);
    }
    w2u(a, sizeof(a), g_fontW);
    logf("UI font for code page %u: '%s' (charset %u)\r\n", acp, (DWORD)(UINT_PTR)a, g_dbcs);
    logf("UI font line height %u/1000 em %u %u\r\n", g_uiline, 0, 0);
}

static HMODULE g_fontdone[256];
static int g_nfontdone;
static DWORD g_nmods, g_modsig;

// Which modules were looked at is remembered by base address, so a module that is unloaded and loaded again
// (an OCX with its form, wshom.ocx) at the same address must be forgotten when it goes. ntdll tells: the
// notification runs under the loader lock, so it only notes the base address (spin lock around plain memory,
// no API calls, no log) and flags a new look; patch_font does the rest. Without the notification
// (LdrRegisterDllNotification is Vista and later) a signature of the module list notices changes instead.
typedef struct { ULONG Flags; const void *FullDllName, *BaseDllName; PVOID DllBase; ULONG SizeOfImage; } ldrnote_t;
typedef VOID (NTAPI *ldrcb_t)(ULONG, const ldrnote_t *, PVOID);
typedef LONG (NTAPI *ldrreg_t)(ULONG, ldrcb_t, PVOID, PVOID *);
static volatile LONG g_modlock, g_moddirty;
static HMODULE g_unloaded[64];
static volatile int g_nunloaded;                  // > 64: the list overflowed, forget everything
static BOOL g_ldrnote;
static volatile LONG g_modgen;

static void modlock(void) { while (InterlockedCompareExchange(&g_modlock, 1, 0)) YieldProcessor(); }
static void modunlock(void) { InterlockedExchange(&g_modlock, 0); }

static VOID NTAPI ldr_note(ULONG reason, const ldrnote_t *d, PVOID ctx)
{
    if (reason == 2 /* LDR_DLL_NOTIFICATION_REASON_UNLOADED */ && d) {
        modlock();
        if (g_nunloaded < 64) g_unloaded[g_nunloaded] = (HMODULE)d->DllBase;
        if (g_nunloaded <= 64) g_nunloaded++;
        g_modgen++;
        modunlock();
    }
    InterlockedExchange(&g_moddirty, 1);
}

static void ldr_register(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    ldrreg_t reg = nt ? (ldrreg_t)GetProcAddress(nt, "LdrRegisterDllNotification") : NULL;
    PVOID cookie;
    g_ldrnote = reg && reg(0, ldr_note, NULL, &cookie) == 0;      // the dll stays loaded: no unregister
    logf("module tracking: %s %u %u\r\n", (DWORD)(UINT_PTR)(g_ldrnote ? "loader notifications" : "module list signature"), 0, 0);
}

static BOOL in_dir(const char *path, const char *dir)
{
    int n = lstrlenA(dir);
    return n && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, path, n, dir, n) == CSTR_EQUAL;
}

static BOOL font_module(HMODULE m, const char *path, const char *appdir, const char *selfdir)
{
    const char *name = path + lstrlenA(path); int i, n;
    while (name > path && name[-1] != '\\') name--;
    n = lstrlenA(name);
    if (m == g_self || in_dir(path, selfdir)) return FALSE;
    if (in_dir(path, appdir) || (n > 4 && !lstrcmpiA(name + n - 4, ".ocx"))) return TRUE;
    for (i = 0; g_fontmods[i]; i++) if (!lstrcmpiA(name, g_fontmods[i])) return TRUE;
    return FALSE;
}

// folder of module m, with the trailing backslash
static void mod_dir(HMODULE m, char *dir)
{
    char *p;
    if (!GetModuleFileNameA(m, dir, MAX_PATH)) { dir[0] = 0; return; }
    for (p = dir + lstrlenA(dir); p > dir && p[-1] != '\\'; p--);
    *p = 0;
}

static void patch_font(void)
{
    typedef BOOL (WINAPI *epm_t)(HANDLE, HMODULE *, DWORD, LPDWORD);
    static epm_t epm;
    static BOOL tried;
    HMODULE g, mods[256]; DWORD need = 0, sig; LONG gen; int k, nun; char path[MAX_PATH], appdir[MAX_PATH], selfdir[MAX_PATH];
    int i, j;
    if (!g_fontready) pick_font();
    patch_text();
    if ((!g_fontW[0] || g_fontoff) && !g_charset) return;
    if (!tried) { tried = TRUE; ldr_register(); }
    if (!r_cfiW) {
        g = GetModuleHandleA("gdi32.dll");
        resolve(g_font_hooks);
        r_gow = GetProcAddress(g, "GetObjectW");
        // kernel32 has it since Windows 7, psapi.dll before
        if (!(epm = (epm_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32EnumProcessModules")))
            epm = (epm_t)GetProcAddress(load_sys("psapi.dll"), "EnumProcessModules");
    }
    if (!epm) return;
    InterlockedExchange(&g_moddirty, 0);          // an event during the look below flags the next one
    modlock(); gen = g_modgen; nun = g_nunloaded; g_nunloaded = 0; modunlock();
    if (!epm(GetCurrentProcess(), mods, sizeof(mods), &need)) { InterlockedExchange(&g_moddirty, 1); return; }
    need /= sizeof(HMODULE);
    if (need > 256) need = 256;
    for (i = 0, sig = need; i < (int)need; i++) sig = sig * 31 + (DWORD)(UINT_PTR)mods[i];
    if (nun > 64) {                               // too many to note: forget all, patching again is harmless
        logf("module tracking: forgot all %u modules %u %u\r\n", g_nfontdone, 0, 0);
        g_nfontdone = 0; g_nmods = 0;
    } else if (nun) {                             // modules unloaded since the last look: forget them
        for (k = 0; k < nun; k++)
            for (j = 0; j < g_nfontdone; j++)
                if (g_fontdone[j] == g_unloaded[k]) {
                    logf("module tracking: forgot an unloaded module %u %u %u\r\n", 0, 0, 0);
                    g_fontdone[j] = g_fontdone[--g_nfontdone];
                    break;
                }
        g_nmods = 0;
    }
    if (need == g_nmods && sig == g_modsig && !g_moddirty) return;   // nothing loaded or unloaded since the last look
    g_nmods = need; g_modsig = sig;
    for (j = 0; j < g_nfontdone; ) {              // forget modules that are no longer in the list
        for (i = 0; i < (int)need && mods[i] != g_fontdone[j]; i++);
        if (i < (int)need) { j++; continue; }
        logf("module tracking: forgot a module, no longer loaded %u %u %u\r\n", 0, 0, 0);
        g_fontdone[j] = g_fontdone[--g_nfontdone];
    }
    mod_dir(NULL, appdir); mod_dir(g_self, selfdir);
    for (i = 0; i < (int)need; i++) {
        for (j = 0; j < g_nfontdone && g_fontdone[j] != mods[i]; j++);
        if (j < g_nfontdone || g_nfontdone >= 256) continue;
        if (GetModuleFileNameA(mods[i], path, MAX_PATH)) {
            if (font_module(mods[i], path, appdir, selfdir)) {
                const char *name = path + lstrlenA(path);
                while (name > path && name[-1] != '\\') name--;
                logf("module tracking: patching %s %u %u\r\n", (DWORD)(UINT_PTR)name, 0, 0);
                patch_base((BYTE *)mods[i], name, "font", g_font_hooks);
                patch_delay((BYTE *)mods[i], name, "font", g_font_hooks);
            }
        } else { InterlockedExchange(&g_moddirty, 1); continue; }   // going away, or not yet usable: look again
        modlock();
        if (gen == g_modgen) g_fontdone[g_nfontdone++] = mods[i];   // not if one went since the look
        else InterlockedExchange(&g_moddirty, 1);
        modunlock();
    }
}

// ---- fonts handed to controls ----
// VB makes the fonts of the first forms (OLE StdFont, cached and shared by description) before its first
// window, i.e. before dynafix is attached and the imports are patched; under Locale Emulator they come out
// as e.g. Arial with the Big5 charset, which Windows draws with MingLiU. So the font a control gets is
// checked too: WM_SETFONT with a face that is swapped (or the UI face with another charset) is followed by
// WM_SETFONT with a UI-face copy, and the controls of a window are checked when it is first activated.
typedef struct { HFONT from, to; } fontmap_t;
static fontmap_t g_fontmap[256];
static int g_nfontmap;
static BOOL g_settingfont;

static HFONT ui_copy(HFONT f)
{
    LOGFONTW lf; int i, k; HFONT to, r = NULL;
    if (!f || !r_cfiW || GetObjectW(f, sizeof(lf), &lf) != sizeof(lf)) return NULL;
    if (!(k = swap_face(lf.lfFaceName, lf.lfCharSet))) return NULL;
    if (k == 2 && (g_dbcs ? lf.lfCharSet == g_dbcs : lf.lfCharSet == ANSI_CHARSET || lf.lfCharSet == DEFAULT_CHARSET)) return NULL;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < g_nfontmap && g_fontmap[i].from != f; i++);
    if (i < g_nfontmap) r = g_fontmap[i].to;
    else if ((to = font_w(&lf, _ReturnAddress()))) {
        if (g_nfontmap < 256) { g_fontmap[g_nfontmap].from = f; g_fontmap[g_nfontmap++].to = to; }
        r = to;
    }
    LeaveCriticalSection(&g_cs);
    return r;
}

// Multi-line text boxes space their lines by the font's own line height, and DynaRun's message box
// (無USB通訊連線: 5 lines in a box sized for 4.5 MingLiU lines) would show the top half of its hidden last
// line. Such a box gets a copy of the UI font whose line is as tall as Arial's (DYNAFIX_FONT_LINEH).
static fontmap_t g_linemap[64];
static int g_nlinemap;

static BOOL multiline_edit(HWND h)
{
    char cls[32];
    return (GetWindowLongA(h, GWL_STYLE) & ES_MULTILINE) && GetClassNameA(h, cls, sizeof(cls)) &&
           (!lstrcmpiA(cls, "Edit") || !lstrcmpA(cls, "ThunderRT6TextBox"));
}

static HFONT line_copy(HFONT f)
{
    LOGFONTW lf; int i; HFONT to, r = NULL;
    if (!f || !r_cfiW || g_lineh <= 0 || GetObjectW(f, sizeof(lf), &lf) != sizeof(lf)) return NULL;
    if (lstrcmpiW(lf.lfFaceName, g_fontW) || lf.lfHeight >= 0) return NULL;     // positive: a line copy already
    EnterCriticalSection(&g_cs);
    for (i = 0; i < g_nlinemap && g_linemap[i].from != f; i++);
    if (i < g_nlinemap) r = g_linemap[i].to;
    else {
        lf.lfHeight = MulDiv(-lf.lfHeight, g_lineh, 1000);                     // cell height
        if ((to = ((HFONT (WINAPI *)(const LOGFONTW *))r_cfiW)(&lf))) {
            if (g_nlinemap < 64) { g_linemap[g_nlinemap].from = f; g_linemap[g_nlinemap++].to = to; }
            r = to;
        }
    }
    LeaveCriticalSection(&g_cs);
    return r;
}

// The font a control should have instead of f, or NULL to keep f.
static HFONT target_font(HWND h, HFONT f)
{
    HFONT to = g_fontoff ? NULL : ui_copy(f), l;
    if (!g_fontoff && multiline_edit(h) && (l = line_copy(to ? to : f))) to = l;
    return to;
}

static void set_ui_font(HWND h, HFONT f, LPARAM redraw)
{
    HFONT to = target_font(h, f);
    if (!to || g_settingfont) return;
    g_settingfont = TRUE;
    SendMessageW(h, WM_SETFONT, (WPARAM)to, redraw);
    g_settingfont = FALSE;
}

static LRESULT CALLBACK RetProc(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION) {
        CWPRETSTRUCT *c = (CWPRETSTRUCT *)l;
        if (c->message == WM_SETFONT && c->wParam && !g_fontoff) set_ui_font(c->hwnd, (HFONT)c->wParam, c->lParam);
    }
    return CallNextHookEx(NULL, code, w, l);
}

#define P_SWEPT "dynafix.f"

// Log (the first 400 controls) and fix the font of each control of a window.
static BOOL CALLBACK sweep_child(HWND h, LPARAM l)
{
    char cls[40], t[60], a[LF_FACESIZE * 2], line[200]; WCHAR w[30]; LOGFONTW lf; HFONT f, to; static LONG nlog;
    f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    to = target_font(h, f);
    if (++nlog <= 400) {
        if (!GetClassNameA(h, cls, sizeof(cls))) cls[0] = 0;
        w[0] = 0; GetWindowTextW(h, w, 30);
        w2u(t, sizeof(t), w);
        if (f && GetObjectW(f, sizeof(lf), &lf) == sizeof(lf)) {
            w2u(a, sizeof(a), lf.lfFaceName);
            RECT rc; GetClientRect(h, &rc);
            wsprintfA(line, "'%s' %d cs %u%s, box %dx%d style %08X", a, lf.lfHeight, lf.lfCharSet, to ? " -> UI font" : "",
                      rc.right, rc.bottom, (DWORD)GetWindowLongA(h, GWL_STYLE));
        } else lstrcpyA(line, f ? "?" : "system font");
        logf("  %s '%s': %s\r\n", (DWORD)(UINT_PTR)cls, (DWORD)(UINT_PTR)t, (DWORD)(UINT_PTR)line);
    }
    if (to) set_ui_font(h, f, TRUE);
    return TRUE;
}

static void sweep_fonts(HWND h)
{
    char t[100]; WCHAR w[40];
    if (GetPropA(h, P_SWEPT)) return;
    SetPropA(h, P_SWEPT, (HANDLE)1);
    w[0] = 0; GetWindowTextW(h, w, 40);
    w2u(t, sizeof(t), w);
    logf("fonts of window '%s' hwnd=%08X %u\r\n", (DWORD)(UINT_PTR)t, (DWORD)(UINT_PTR)h, 0);
    EnumChildWindows(h, sweep_child, 0);
}

static void note_size(HWND h, WPARAM w, LPARAM l)
{
    char cls[32];
    BOOL dup;
    if (!GetClassNameA(h, cls, sizeof(cls)) || lstrcmpA(cls, "ThunderRT6FormDC")) return;
    dup = GetPropA(h, P_W) == (HANDLE)(w + 1) && GetPropA(h, P_L) == (HANDLE)l;
    SetPropA(h, P_W, (HANDLE)(w + 1));
    SetPropA(h, P_L, (HANDLE)l);
    if (dup) SetPropA(h, P_DUP, (HANDLE)1); else RemovePropA(h, P_DUP);
    if (g_stats) { InterlockedIncrement(&s_size); if (dup) InterlockedIncrement(&s_dup); }
}

// ---- keep the main form off the taskbar ----
// DynaRun's main form is maximized (WindowState) but has no caption; Windows maximizes such a window
// over the whole monitor, so the taskbar covers DynaRun's bottom status bar. The hook rewrites the
// MINMAXINFO the form is about to get and a subclass clips its WINDOWPOS (see fit_pos), so the maximized
// form fills the monitor's work area instead. On an edge with an auto-hide taskbar 2 px are left free,
// or the taskbar could not pop up over it. DYNAFIX_FULLSCREEN=1 keeps DynaRun's own behaviour.

#ifndef ABM_GETAUTOHIDEBAREX
#define ABM_GETAUTOHIDEBAREX 0x0000000b
#endif

static HWND g_dumpform;
static UINT_PTR g_dumptimer;
static LONG g_dumps;

// Diagnosis: 3 s after the main form was last resized, log where its direct children are (first 4 times).
static void CALLBACK dump_children(HWND unused, UINT m, UINT_PTR id, DWORD t)
{
    HWND c; RECT r; POINT o = { 0, 0 }; char cls[40], tx[40], line[160]; WCHAR w[20]; int n = 0;
    KillTimer(NULL, id); g_dumptimer = 0;
    if (!IsWindow(g_dumpform) || InterlockedIncrement(&g_dumps) > 4) return;
    ClientToScreen(g_dumpform, &o); GetClientRect(g_dumpform, &r);
    logf("children of form %08X, client %ux%u:\r\n", (DWORD)(UINT_PTR)g_dumpform, r.right, r.bottom);
    for (c = GetWindow(g_dumpform, GW_CHILD); c && n < 150; c = GetWindow(c, GW_HWNDNEXT), n++) {
        if (!GetClassNameA(c, cls, sizeof(cls))) cls[0] = 0;
        w[0] = 0; GetWindowTextW(c, w, 20);
        w2u(tx, sizeof(tx), w);
        GetWindowRect(c, &r); OffsetRect(&r, -o.x, -o.y);
        wsprintfA(line, "%s '%s' %d,%d %dx%d%s", cls, tx, r.left, r.top, r.right - r.left, r.bottom - r.top, IsWindowVisible(c) ? "" : " hidden");
        logf("  %s %u %u\r\n", (DWORD)(UINT_PTR)line, 0, 0);
    }
}

static void arm_dump(HWND h)
{
    if (g_dumps >= 4 || !IsWindowVisible(h)) return;
    g_dumpform = h;
    if (g_dumptimer) KillTimer(NULL, g_dumptimer);
    g_dumptimer = SetTimer(NULL, 0, 3000, dump_children);
}
static LONG g_walog;
typedef UINT_PTR (WINAPI *SHAppBarMessage_t)(DWORD, PAPPBARDATA);

static BOOL is_main_form(HWND h, BOOL any)
{
    char cls[32];
    return (any || !g_fullscreen) && !(GetWindowLongA(h, GWL_STYLE) & WS_CHILD)
        && GetClassNameA(h, cls, sizeof(cls)) && !lstrcmpA(cls, "ThunderRT6FormDC");
}

#include "dynafix_screen.inc.c"

static void patch_vb_screen(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA("MSVBVM60.DLL");
    POINT o = { 0, 0 }; RECT wa, mon; int scx, scy; static BOOL done;
    if (!base || g_fullscreen || g_waclip || done) return;
    done = TRUE;
    if (!work_area(MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY), &wa, &mon) || EqualRect(&wa, &mon)) return;
    scx = GetSystemMetrics(SM_CXSCREEN); scy = GetSystemMetrics(SM_CYSCREEN);
    {
        vbwalk_t w; w.base = base; w.wa = &wa; w.scx = scx; w.scy = scy;
        if (!seh_run(walk_vb, &w)) logf("scanning MSVBVM60.DLL for VB's screen size failed %u %u %u\r\n", 0, 0, 0);
    }
}

static void fit_minmax(HWND h, MINMAXINFO *mm)
{
    RECT wa, mon; LONG bx, by;
    if (!work_area(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &wa, &mon) || EqualRect(&wa, &mon)) return;
    bx = mm->ptMaxPosition.x < 0 ? -mm->ptMaxPosition.x : 0;   // the frame Windows puts off-screen
    by = mm->ptMaxPosition.y < 0 ? -mm->ptMaxPosition.y : 0;
    mm->ptMaxPosition.x = wa.left - mon.left - bx;              // relative to the monitor
    mm->ptMaxPosition.y = wa.top - mon.top - by;
    mm->ptMaxSize.x = wa.right - wa.left + 2 * bx;
    mm->ptMaxSize.y = wa.bottom - wa.top + 2 * by;
    if (InterlockedIncrement(&g_walog) <= 10)
        logf("form %08X maximizes to the work area %ux%u\r\n", (DWORD)(UINT_PTR)h, mm->ptMaxSize.x, mm->ptMaxSize.y);
}

// Observed: the WINDOWPOS of the maximized form is reset to the full
// screen height (and 4:3 width) after THBResize has handled them (observed), so the hook alone cannot change it: the form is subclassed once, when it
// is first maximized (THBResize has subclassed it by then), and the WINDOWPOS is clipped after THBResize.
#define P_WP  "dynafix.wp"
#define P_WPU "dynafix.wpu"
static UINT g_refit;
static LONG g_cliplog, g_replog;
static HWND g_repform;
static UINT_PTR g_reptimer;

// Shrinking keeps the old pixels below the new layout (the form class has no CS_VREDRAW), and DynaRun only
// repaints part of it: a band of plain background and a strip of the old full-height gradient stay. Once the
// size has settled, the whole form is repainted.
static void CALLBACK repaint_form(HWND unused, UINT m, UINT_PTR id, DWORD t)
{
    KillTimer(NULL, id); g_reptimer = 0;
    if (!IsWindow(g_repform)) return;
    RedrawWindow(g_repform, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    if (InterlockedIncrement(&g_replog) <= 10) logf("form %08X repainted after the resize %u %u\r\n", (DWORD)(UINT_PTR)g_repform, 0, 0);
}

static BOOL fit_pos(HWND h, WINDOWPOS *p)
{
    RECT r, wa, mon, f = { 0, 0, 0, 0 }, cur;
    if ((p->flags & SWP_NOSIZE) || !(GetWindowLongA(h, GWL_STYLE) & WS_MAXIMIZE)) return FALSE;
    if (p->flags & SWP_NOMOVE) { if (!GetWindowRect(h, &cur)) return FALSE; p->x = cur.left; p->y = cur.top; }
    SetRect(&r, p->x, p->y, p->x + p->cx, p->y + p->cy);
    if (!work_area(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &wa, &mon) || EqualRect(&wa, &mon)) return FALSE;
    AdjustWindowRectEx(&f, GetWindowLongA(h, GWL_STYLE) & ~WS_MAXIMIZE, FALSE, GetWindowLongA(h, GWL_EXSTYLE));
    wa.left += f.left; wa.top += f.top; wa.right += f.right; wa.bottom += f.bottom;
    if (r.left >= wa.left && r.top >= wa.top && r.right <= wa.right && r.bottom <= wa.bottom) return FALSE;
    if (r.left < wa.left) OffsetRect(&r, wa.left - r.left, 0);     // taskbar on the left / top: move first
    if (r.top < wa.top) OffsetRect(&r, 0, wa.top - r.top);
    if (r.right > wa.right) r.right = wa.right;
    if (r.bottom > wa.bottom) r.bottom = wa.bottom;
    // keep THBResize's aspect ratio, like a smaller screen; only for the caption-less dashboard: a form with a caption
    // (the run viewer after a run) is a normal window and just fills the work area
    if (!g_waclip && p->cx > 0 && p->cy > 0 && (GetWindowLongA(h, GWL_STYLE) & WS_CAPTION) != WS_CAPTION) {
        int w = r.right - r.left, hh = r.bottom - r.top;
        if (w * p->cy > hh * p->cx) w = MulDiv(hh, p->cx, p->cy); else hh = MulDiv(w, p->cy, p->cx);
        r.right = r.left + w; r.bottom = r.top + hh;
    }
    if (InterlockedIncrement(&g_cliplog) <= 20) {
        char t[60]; wsprintfA(t, "%ux%u -> %ux%u", p->cx, p->cy, r.right - r.left, r.bottom - r.top);
        logf("form %08X clipped to the work area %s %u\r\n", (DWORD)(UINT_PTR)h, (DWORD)(UINT_PTR)t, 0);
    }
    p->flags = (p->flags & ~SWP_NOMOVE) | SWP_NOCOPYBITS;
    p->x = r.left; p->y = r.top; p->cx = r.right - r.left; p->cy = r.bottom - r.top;
    return TRUE;
}

static LRESULT CALLBACK FormProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC old = (WNDPROC)GetPropA(h, P_WP);
    BOOL u = GetPropA(h, P_WPU) != NULL;
    LRESULT r;
    if (!old) return u ? DefWindowProcW(h, m, w, l) : DefWindowProcA(h, m, w, l);
    if (m == g_refit) {   // first fit after subclassing: re-apply the current size, clipped
        RECT c;
        if ((GetWindowLongA(h, GWL_STYLE) & WS_MAXIMIZE) && GetWindowRect(h, &c))
            SetWindowPos(h, NULL, c.left, c.top, c.right - c.left, c.bottom - c.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        return 0;
    }
    if (m == WM_NCDESTROY) {
        // Unhook only if FormProc is still the top of the chain (read with the same A/W flavor it was installed with:
        // the other flavor returns a conversion thunk that never equals FormProc). If another subclass was installed
        // above us, leave it alone: it keeps calling FormProc, which keeps forwarding to 'old' (a local copy, so the
        // props can go now). Nothing is freed and no message follows WM_NCDESTROY, so FormProc is never reached stateless.
        if ((WNDPROC)(u ? GetWindowLongPtrW(h, GWLP_WNDPROC) : GetWindowLongPtrA(h, GWLP_WNDPROC)) == FormProc)
            u ? SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)old) : SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)old);
        RemovePropA(h, P_WP); RemovePropA(h, P_WPU);
        return u ? CallWindowProcW(old, h, m, w, l) : CallWindowProcA(old, h, m, w, l);
    }
    r = u ? CallWindowProcW(old, h, m, w, l) : CallWindowProcA(old, h, m, w, l);
    if (m == WM_WINDOWPOSCHANGING && fit_pos(h, (WINDOWPOS *)l)) {
        g_repform = h;
        if (g_reptimer) KillTimer(NULL, g_reptimer);
        g_reptimer = SetTimer(NULL, 0, 500, repaint_form);
    }
    return r;
}

static void subclass_form(HWND h)
{
    BOOL u; LONG_PTR old;
    if (GetPropA(h, P_WP) || !(GetWindowLongA(h, GWL_STYLE) & WS_MAXIMIZE)) return;
    if (!g_refit) g_refit = RegisterWindowMessageA("dynafix.refit");
    u = IsWindowUnicode(h);
    old = u ? GetWindowLongPtrW(h, GWLP_WNDPROC) : GetWindowLongPtrA(h, GWLP_WNDPROC);
    if (!old || old == (LONG_PTR)FormProc) return;
    SetPropA(h, P_WPU, (HANDLE)(UINT_PTR)u);
    SetPropA(h, P_WP, (HANDLE)old);
    u ? SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)FormProc) : SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)FormProc);
    logf("form %08X subclassed %u %u\r\n", (DWORD)(UINT_PTR)h, 0, 0);
    PostMessageA(h, g_refit, 0, 0);
}

// The same AppUserModelID as the launcher shortcuts (setup.c), so a running DynaRun groups under the pinned icon and
// shows its logo. Set when dynafix is first called in DynaRun, before its first window (a process-wide ID must come
// before any window). Not in DllMain: since the dll is loaded before DynaRun starts, shell32 is not there yet then
// (v1.3.0 to v1.3.2 never set the ID), and loading it under the loader lock is not allowed.
static void set_appid(void)
{
    WCHAR n[MAX_PATH]; int l = GetModuleFileNameW(NULL, n, MAX_PATH); HMODULE sh; HRESULT hr = E_NOTIMPL;
    HRESULT (WINAPI *f)(PCWSTR);
    if (env_flag("DYNAFIX_NOAPPID", '1')) return;
    if (l <= 14 || n[l - 15] != '\\' || lstrcmpiW(n + l - 14, L"DynaRun V3.exe")) return;
    if (!(sh = GetModuleHandleW(L"shell32.dll"))) sh = LoadLibraryW(L"shell32.dll");
    f = sh ? (HRESULT (WINAPI *)(PCWSTR))GetProcAddress(sh, "SetCurrentProcessExplicitAppUserModelID") : NULL;   // Windows 7+
    if (f) hr = f(L"DynaRunFix.DynaRunV3");
    logf("AppUserModelID DynaRunFix.DynaRunV3 set: %08X (80004001 = not on this Windows) %u %u\r\n", (DWORD)hr, 0, 0);
}

static BOOL CALLBACK count_win(HWND h, LPARAM l) { (void)h; (*(int *)l)++; return TRUE; }

// The launcher's early hook and the dll's own hook (installed below, to stay after the launcher exits) are both CwpProc on
// the GUI thread, and the first one calls the second through CallNextHookEx with the same CWPSTRUCT: the second skips the work.
// (GUI thread only, so a plain static is enough; it is restored on the way out for nested messages.)
static LPARAM g_cwp_l;
static void cwp_work(CWPSTRUCT *c);

#include "dynafix_pegrc.inc.c"


__declspec(dllexport) LRESULT CALLBACK CwpProc(int code, WPARAM w, LPARAM l)
{
    LRESULT r; LPARAM prev;
    if (code != HC_ACTION || l == g_cwp_l) return CallNextHookEx(NULL, code, w, l);
    prev = g_cwp_l; g_cwp_l = l;
    cwp_work((CWPSTRUCT *)l);
    r = CallNextHookEx(NULL, code, w, l);
    g_cwp_l = prev;
    return r;
}

static void cwp_work(CWPSTRUCT *c)
{
    {
        if (InterlockedCompareExchange(&g_pinned, 1, 0) == 0) {
            // Keep ourselves loaded and hooked after the launcher exits (once per process).
            char self[MAX_PATH], ev[64];
            HANDLE e;
            GetModuleFileNameA(g_self, self, MAX_PATH);
            LoadLibraryA(self);
            SetWindowsHookExA(WH_CALLWNDPROC, (HOOKPROC)CwpProc, g_self, GetCurrentThreadId());
            SetWindowsHookExA(WH_CALLWNDPROCRET, (HOOKPROC)RetProc, g_self, GetCurrentThreadId());
            logf("dynafix active pid=%u tid=%u ansi-codepage=%u\r\n", GetCurrentProcessId(), GetCurrentThreadId(), GetACP());
            set_appid();
            if (g_stats) { g_statstimer = SetTimer(NULL, 0, 5000, stats_tick); logf("DYNAFIX_STATS=1: guard counters every 5 s %u %u %u\r\n", 0, 0, 0); }
            {   // more than 0: DynaRun made windows (splash screen...) before dynafix was there, i.e. it was attached late
                int nw = 0;
                EnumThreadWindows(GetCurrentThreadId(), count_win, (LPARAM)&nw);
                logf("windows DynaRun had before dynafix: %u (first message %04X) %u\r\n", nw, c->message, 0);
            }
            log_versions();
            wsprintfA(ev, "Local\\dynafix_ready_%u", GetCurrentProcessId());
            e = OpenEventA(EVENT_MODIFY_STATE, FALSE, ev);
            if (e) SetEvent(e);   // handle stays open: a later launcher sees the fix is already active
        }
        if (!g_patched) patch_thbresize();
        conn_tick();
        if (!g_fsopatched) patch_attr();
        peg_check();
        // once MSVBVM60 is there, then whenever a window is made (a new form may have loaded new controls)
        if ((!g_fontpatched || c->message == WM_NCCREATE) && GetModuleHandleA("MSVBVM60.DLL")) { g_fontpatched = TRUE; patch_font(); }
        if (c->message == WM_SIZE) { note_size(c->hwnd, c->wParam, c->lParam); if (c->wParam == SIZE_MAXIMIZED && is_main_form(c->hwnd, TRUE)) arm_dump(c->hwnd); }
        else if (c->message == WM_SHOWWINDOW && g_stats && c->lParam == 0) {   // lParam 0: ShowWindow() call, not a parent's
            char cls[32];
            if (GetClassNameA(c->hwnd, cls, sizeof(cls)) && !lstrcmpA(cls, "ThunderRT6FormDC") && !(GetWindowLongA(c->hwnd, GWL_STYLE) & WS_CHILD))
                InterlockedIncrement(c->wParam ? &s_show : &s_hide);
        }
        else if (c->message == WM_GETMINMAXINFO) { if (is_main_form(c->hwnd, FALSE)) fit_minmax(c->hwnd, (MINMAXINFO *)c->lParam); }
        else if (c->message == WM_WINDOWPOSCHANGING) { if (is_main_form(c->hwnd, FALSE)) subclass_form(c->hwnd); }
        else if (c->message == WM_ACTIVATE && LOWORD(c->wParam) != WA_INACTIVE) sweep_fonts(c->hwnd);
        else if (c->message == WM_NCDESTROY) { RemovePropA(c->hwnd, P_W); RemovePropA(c->hwnd, P_L); RemovePropA(c->hwnd, P_DUP); RemovePropA(c->hwnd, P_SWEPT); }
    }
}


BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p)
{
    if (r == DLL_PROCESS_ATTACH) {
        BOOL noappid;
        g_self = h;
        InitializeCriticalSection(&g_cs);
        DisableThreadLibraryCalls(h);
        g_stats = env_flag("DYNAFIX_STATS", '1');
        if (env_flag("DYNAFIX_PEGRC", '0')) g_pegrc = FALSE;
        g_usbfix = env_flag("DYNAFIX_USBFIX", '1');
        noappid = env_flag("DYNAFIX_NOAPPID", '1');   // diagnostic switch
        {   // a TEMP too long for the buffer: no log at all (the buffer content would be undefined)
            DWORD tn = GetEnvironmentVariableA("TEMP", g_logpath, MAX_PATH - 16);
            if (tn && tn < MAX_PATH - 16) lstrcatA(g_logpath, "\\dynafix.log"); else g_logpath[0] = 0;
        }
        trim_log();
        clear_runasinvoker();    // after the log path is known (it logs what it did)
        if (noappid) logf("DYNAFIX_NOAPPID=1: the explicit AppUserModelID is not set %u %u %u\r\n", 0, 0, 0);
        if (GetEnvironmentVariableW(L"DYNAFIX_FONT", g_fontW, LF_FACESIZE) >= LF_FACESIZE) g_fontW[0] = 0;
        g_fullscreen = env_flag("DYNAFIX_FULLSCREEN", '1');
        { char v[8]; DWORD n = GetEnvironmentVariableA("DYNAFIX_WORKAREA", v, sizeof(v)); g_waclip = df_env_word(v, n, sizeof(v), "clip"); }
        { long n;
          if (env_flag("DYNAFIX_CONNHINT", '0')) g_hints = FALSE;
          if (env_num("DYNAFIX_CONNHINT_MS", &n) && n >= 500 && n <= 60000) g_hintms = (DWORD)n;
        }
        g_fontoff = !lstrcmpiW(g_fontW, L"off") || !lstrcmpiW(g_fontW, L"none") || !lstrcmpW(g_fontW, L"0");
        {
            long n;
            if (env_num("DYNAFIX_FONT_SCALE", &n) && n >= 50 && n <= 150) g_fontscale = (int)n;
            g_fontline = env_flag("DYNAFIX_FONT_LINE", '1');
            if (env_num("DYNAFIX_FONT_LINEH", &n) && (!n || (n >= 900 && n <= 1500))) g_lineh = (int)n;   // 0 = off
            if (env_num("DYNAFIX_CHARSET", &n) && n > 0 && n < 255) g_charset = (BYTE)n;
        }
    }
    return TRUE;
}
