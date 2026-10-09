// dynafix.dll - Win10/11 compatibility shim for DynaRun V3 (x86, no CRT)
//
// Win10/11 sends WM_SIZE (SIZE_MAXIMIZED) when a hidden, already-maximized window is
// shown again, even though nothing changed; Win7/XP do not. DynaRun's THBResize 2.5
// control (THBRes25.dll) answers every WM_SIZE by posting itself 0x591, and DynaRun's
// handler for that hides the main form, loads a blank form and shows the main form
// maximized again -> WM_SIZE again -> endless hide/show loop (the "flicker").
//
// Fix: watch WM_SIZE on VB forms; if it is identical to the previous one for that
// window, THBRes25's PostMessageA(0x591) for it is skipped. Only THBRes25's import
// table is patched, in memory, inside this process.
//
// Second fix: files kept in OneDrive on Win10/11 can carry cloud attribute bits
// (e.g. 0x80000 PINNED, "Always keep on this device") that do not exist on Win7/XP.
// DynaRun checks a .Dpr with VB's GetAttr (MSVBVM60 -> GetFileAttributesA), treats
// such a file as not a normal file and silently skips it. The file-attribute APIs
// imported by MSVBVM60.DLL and scrrun.dll are redirected to wrappers that clear
// those bits. Only import tables in this process are patched; nothing on disk changes.
//
// Third fix: on the first start DynaRun runs System Data\Setup_<n>.exe (VB Declare ShellExecuteA)
// and polls for its result. Windows' installer detection runs that helper elevated; DynaRun then
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
#include "version.h"

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
static BOOL g_pinned, g_patched, g_vbpatched, g_fsopatched, g_fontpatched;
static LONG g_skipped, g_masked;
static PostMessageA_t g_realPost;
static char g_logpath[MAX_PATH];

// above 10 MB, dynafix.log is cut to its newest 8 MB, from a full line on (the launcher does the same; whichever runs first)
static void trim_log(void)
{
    HANDLE h; DWORD size, n, i; char *buf;
    h = CreateFileA(g_logpath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    size = GetFileSize(h, NULL);
    if (size != INVALID_FILE_SIZE && size > 10485760 && (buf = (char *)HeapAlloc(GetProcessHeap(), 0, 8388608))) {
        SetFilePointer(h, size - 8388608, NULL, FILE_BEGIN);
        if (ReadFile(h, buf, 8388608, &n, NULL)) {
            for (i = 0; i < n && buf[i] != '\n'; i++);
            if (i < n) { i++; SetFilePointer(h, 0, NULL, FILE_BEGIN); WriteFile(h, buf + i, n - i, &n, NULL); SetEndOfFile(h); }
        }
        HeapFree(GetProcessHeap(), 0, buf);
    }
    CloseHandle(h);
}

static void logf(const char *fmt, DWORD a, DWORD b, DWORD c)
{
    char line[256]; DWORD n; HANDLE h; SYSTEMTIME t;
    if (!g_logpath[0]) return;
    GetLocalTime(&t);
    n = wsprintfA(line, "%04u-%02u-%02u %02u:%02u:%02u.%03u ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    wsprintfA(line + n, fmt, a, b, c);
    h = CreateFileA(g_logpath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, line, lstrlenA(line), &n, NULL);
    CloseHandle(h);
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
    if ((v = LoadLibraryA("version.dll"))) {
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

static BOOL WINAPI HookedPostMessageA(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == THB_MSG && GetPropA(h, P_DUP)) {
        if (InterlockedIncrement(&g_skipped) <= 20)
            logf("skipped THBResize 0x591 after duplicate WM_SIZE hwnd=%08X %u %u\r\n", (DWORD)(UINT_PTR)h, 0, 0);
        return TRUE;
    }
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
static void restart_dynarun(void)
{
    char exe[MAX_PATH + 40], *p; STARTUPINFOA si; PROCESS_INFORMATION pi; int i;
    GetModuleFileNameA(g_self, exe, MAX_PATH);
    for (p = exe + lstrlenA(exe); p > exe && p[-1] != '\\'; p--);
    lstrcpyA(p, "DynaRunFix.exe");
    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) return;
    { char cmd[MAX_PATH + 60]; volatile char *z = (volatile char *)&si; for (i = 0; i < (int)sizeof(si); i++) z[i] = 0; si.cb = sizeof(si);
      wsprintfA(cmd, "\"%s\" /restart %u", exe, GetCurrentProcessId());
      if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return; }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    logf("restarting DynaRun to load the new configuration %u %u %u\r\n", 0, 0, 0);
    ExitProcess(0);
}

// ---- optional features picked on the first-start system selection ----
// The selection screen's "optional features" group (bottom right) holds four buttons, top to bottom:
// auto climate monitor, water cooler, cooling air fans / AFR extraction, internal AFR analyser. A picked
// one is drawn green. DynaRun stores only the climate / AFR model (Calibration_Data) and the helper then
// writes all System_Setup enable flags as "0", so the picked features end up off. They are switched on
// afterwards with the same REG_SZ "-1" values that 工程模式 -> 系統組態設定 -> 存檔並離開 writes.
#define SETUP_KEY "Software\\DynaPro\\System_Setup"
#define CAL_KEY   "Software\\DynaPro\\Calibration_Data"
#define N_OPT 4
static const char *g_optname[N_OPT] = { "auto climate", "water cooler", "cooling fans", "AFR" };
static const char *g_optval[N_OPT][3] = {
    { "Auto_Climate_Enable", "Property_Auto_Climate", 0 },
    { "Water_Cooler_Enable", 0 },
    { "Cooling_Fan_Enable", 0 },
    { "Int_AF_Ratio_Enable", 0 },
};

typedef struct { HWND form, frame; RECT fr; int n; HWND btn[8]; } sel_t;

static void utf8_text(HWND h, char *a, int size)
{
    WCHAR w[60];
    w[0] = 0; GetWindowTextW(h, w, 60);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, a, size, NULL, NULL);
}

// The options frame is the lowest frame in the right half of the form.
static BOOL CALLBACK find_frame(HWND h, LPARAM l)
{
    sel_t *s = (sel_t *)l; char cls[40]; RECT r, f;
    if (!GetClassNameA(h, cls, sizeof(cls)) || lstrcmpA(cls, "ThunderRT6Frame") || !IsWindowVisible(h)) return TRUE;
    GetWindowRect(h, &r); GetWindowRect(s->form, &f);
    if ((r.left + r.right) / 2 > (f.left + f.right) / 2 && (!s->frame || r.top > s->fr.top)) { s->frame = h; s->fr = r; }
    return TRUE;
}

// The buttons are the controls lying inside the frame: not all of them are its children (the cooling fan
// button is not), so all controls of the form are checked by position.
static BOOL CALLBACK find_buttons(HWND h, LPARAM l)
{
    sel_t *s = (sel_t *)l; RECT r, q; POINT c; char cls[40]; int i;
    if (s->n >= 8 || !IsWindowVisible(h) || !GetClassNameA(h, cls, sizeof(cls)) || !lstrcmpA(cls, "ThunderRT6Frame")) return TRUE;
    GetWindowRect(h, &r);
    c.x = (r.left + r.right) / 2; c.y = (r.top + r.bottom) / 2;
    if (!PtInRect(&s->fr, c)) return TRUE;
    for (i = s->n++; i > 0; i--) {             // keep them sorted top to bottom
        GetWindowRect(s->btn[i - 1], &q);
        if (q.top <= r.top) break;
        s->btn[i] = s->btn[i - 1];
    }
    s->btn[i] = h;
    return TRUE;
}

static BOOL CALLBACK find_form(HWND h, LPARAM l)
{
    sel_t *s = (sel_t *)l;
    if (!IsWindowVisible(h)) return TRUE;
    s->form = h; s->frame = NULL;
    EnumChildWindows(h, find_frame, l);
    return !s->frame;                          // stop at the first form with a frame
}

// Which optional features are drawn as picked (green). Returns FALSE if the group was not found.
static BOOL read_options(BOOL *on)
{
    typedef COLORREF (WINAPI *gp_t)(HDC, int, int);
    gp_t gp = (gp_t)GetProcAddress(GetModuleHandleA("gdi32.dll"), "GetPixel");
    sel_t s; int i, k = 0;
    { volatile char *z = (volatile char *)&s; for (i = 0; i < (int)sizeof(s); i++) z[i] = 0; }
    EnumThreadWindows(GetCurrentThreadId(), find_form, (LPARAM)&s);
    if (!s.frame) { logf("optional features: selection form not found %u %u %u\r\n", 0, 0, 0); return FALSE; }
    EnumChildWindows(s.form, find_buttons, (LPARAM)&s);
    { char a[150]; utf8_text(s.frame, a, sizeof(a)); logf("optional features group '%s' has %u controls %u\r\n", (DWORD)(UINT_PTR)a, s.n, 0); }
    for (i = 0; i < s.n; i++) {
        char cls[40], a[150]; RECT r; HDC dc; COLORREF c = CLR_INVALID; BOOL g;
        GetClassNameA(s.btn[i], cls, sizeof(cls)); utf8_text(s.btn[i], a, sizeof(a));
        GetClientRect(s.btn[i], &r);
        if (gp && (dc = GetDC(s.btn[i]))) { c = gp(dc, 6, r.bottom / 2); ReleaseDC(s.btn[i], dc); }
        logf("  %s '%s' colour %06X", (DWORD)(UINT_PTR)cls, (DWORD)(UINT_PTR)a, c);
        // Controls of other groups (EB-150 / EB-250 engine dynamometers) also lie in this area, hidden
        // (clipped away): nothing of them can be read, so they are skipped.
        if (c == CLR_INVALID) { logf(" -> hidden, skipped %u %u %u\r\n", 0, 0, 0); continue; }
        g = GetGValue(c) >= 160 && GetRValue(c) < 100 && GetBValue(c) < 100;
        if (k < N_OPT) on[k] = g;
        logf(" -> %s %s %u\r\n", (DWORD)(UINT_PTR)(k < N_OPT ? g_optname[k] : "?"), (DWORD)(UINT_PTR)(g ? "picked" : "not picked"), 0);
        k++;
    }
    if (k != N_OPT) { logf("optional features: expected %u buttons, found %u %u\r\n", N_OPT, k, 0); return FALSE; }
    return TRUE;
}

static BOOL has_value(const char *key, const char *name)
{
    HKEY k; BOOL r = FALSE;
    if (!RegOpenKeyExA(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &k)) {
        r = !RegQueryValueExA(k, name, NULL, NULL, NULL, NULL);
        RegCloseKey(k);
    }
    return r;
}

static void enable_options(const BOOL *on)
{
    HKEY k; int i, j;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, SETUP_KEY, 0, KEY_SET_VALUE, &k)) {
        logf("optional features: cannot open System_Setup %u %u %u\r\n", 0, 0, 0);
        return;
    }
    for (i = 0; i < N_OPT; i++)
        for (j = 0; on[i] && g_optval[i][j]; j++)
            logf("enabled %s: System_Setup\\%s = \"-1\" (%s)\r\n", (DWORD)(UINT_PTR)g_optname[i], (DWORD)(UINT_PTR)g_optval[i][j],
                 (DWORD)(UINT_PTR)(RegSetValueExA(k, g_optval[i][j], 0, REG_SZ, (const BYTE *)"-1", 3) ? "failed" : "ok"));
    RegCloseKey(k);
}

static HINSTANCE WINAPI H_ShellExecuteA(HWND w, LPCSTR verb, LPCSTR file, LPCSTR params, LPCSTR dir, INT show)
{
    typedef HINSTANCE (WINAPI *se_t)(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT);
    typedef BOOL (WINAPI *sx_t)(SHELLEXECUTEINFOA *);
    const char *p; int i;
    // First-time setup helper (System Data\Setup_<n>.exe): run it to the end with the message loop
    // running, then restart DynaRun. A declined UAC prompt is reported to DynaRun as access denied.
    for (p = file; p && *p; p++)
        if ((p == file || p[-1] == '\\') && lstrlenA(p) >= 6 &&
            CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, p, 6, "Setup_", 6) == CSTR_EQUAL) {
            SHELLEXECUTEINFOA se; sx_t sx = (sx_t)GetProcAddress(GetModuleHandleA("shell32.dll"), "ShellExecuteExA"); MSG m;
            char lang[64]; DWORD n = sizeof(lang) - 1, t; HKEY k; BOOL on[N_OPT];
            if (!sx) break;
            // Picked optional features, read while the selection form is still open. If its buttons cannot be
            // read, the climate monitor / AFR analyser count as picked when their model dialog stored a model.
            for (i = 0; i < N_OPT; i++) on[i] = FALSE;
            if (!read_options(on)) {
                for (i = 0; i < N_OPT; i++) on[i] = FALSE;
                on[0] = has_value(CAL_KEY, "Auto_Climate_Model");
                on[3] = has_value(CAL_KEY, "Air_Fuel_Model");
                logf("optional features from the stored models: climate %u AFR %u %u\r\n", on[0], on[3], 0);
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
            if (!sx(&se)) { logf("setup helper %s did not start: error %u %u\r\n", (DWORD)(UINT_PTR)file, GetLastError(), 0); return (HINSTANCE)(UINT_PTR)SE_ERR_ACCESSDENIED; }
            logf("setup helper %s started, waiting for it %u %u\r\n", (DWORD)(UINT_PTR)file, 0, 0);
            if (se.hProcess) {
                while (MsgWaitForMultipleObjects(1, &se.hProcess, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
                    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
                CloseHandle(se.hProcess);
            }
            logf("setup helper finished %u %u %u\r\n", 0, 0, 0);
            if (lang[0] && !RegOpenKeyExA(HKEY_CURRENT_USER, LANG_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k)) {
                char now[64]; DWORD m2 = sizeof(now) - 1;
                if (RegQueryValueExA(k, "Default_Language", NULL, &t, (BYTE *)now, &m2) || t != REG_SZ) m2 = 0;
                now[m2] = 0;
                if (lstrcmpA(now, lang) && !RegSetValueExA(k, "Default_Language", 0, REG_SZ, (const BYTE *)lang, lstrlenA(lang) + 1))
                    logf("kept the chosen language %s (helper had set %s) %u\r\n", (DWORD)(UINT_PTR)lang, (DWORD)(UINT_PTR)now, 0);
                RegCloseKey(k);
            }
            enable_options(on);
            restart_dynarun();
            return (HINSTANCE)42;
        }
    return ((se_t)r_shexec)(w, verb, file, params, dir, show);
}

static void patch_vb_screen(void);

static FARPROC WINAPI H_GetProcAddress(HMODULE m, LPCSTR name)
{
    typedef FARPROC (WINAPI *gpa_t)(HMODULE, LPCSTR);
    FARPROC f = ((gpa_t)r_gpa)(m, name);
    if (!f || (UINT_PTR)name < 0x10000) return f;          // ordinal
    if (!lstrcmpA(name, "ShellExecuteA")) { r_shexec = f; return (FARPROC)H_ShellExecuteA; }
    return f;
}

// *real = kernel32 export, alt = kernelbase export (api-ms-win-* imports bind there)
typedef struct { const char *name; FARPROC *real; FARPROC hook; FARPROC alt; } hook_t;

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

// Faces that are swapped, with their line height (ascent + descent) in 1/1000 em.
static const struct { const WCHAR *face; short line; } g_swap[] = {
    { L"Arial", 1117 }, { L"Times New Roman", 1107 }, { L"MS Sans Serif", 1180 }, { L"Microsoft Sans Serif", 1150 },
    { L"MS Shell Dlg", 1150 }, { L"MS Shell Dlg 2", 1207 }, { L"MS Serif", 1150 }, { L"Tahoma", 1207 },
    { L"Verdana", 1215 }, { L"Segoe UI", 1330 },
    { L"\x65B0\x7D30\x660E\x9AD4", 1000 }, { L"\x7D30\x660E\x9AD4", 1000 },               // 新細明體 細明體
    { L"PMingLiU", 1000 }, { L"MingLiU", 1000 }, { L"Microsoft JhengHei", 1250 }, { L"Microsoft JhengHei UI", 1270 },
    { L"SimSun", 1000 }, { L"\x5B8B\x4F53", 1000 }, { L"NSimSun", 1000 },                  // 宋体
    { L"Microsoft YaHei", 1320 }, { L"Microsoft YaHei UI", 1320 },
    { L"MS UI Gothic", 1000 }, { L"MS PGothic", 1000 }, { L"\xFF2D\xFF33 \xFF30\x30B4\x30B7\x30C3\x30AF", 1000 },  // ＭＳ Ｐゴシック
    { L"Meiryo UI", 1300 }, { L"Yu Gothic UI", 1330 },
    { L"Gulim", 1000 }, { L"\xAD74\xB9BC", 1000 }, { L"Dotum", 1000 }, { L"\xB3CB\xC6C0", 1000 },  // 굴림 돋움
    { L"Malgun Gothic", 1330 }, { 0 }
};

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
    for (i = 0; i < nseen; i++) if (seen[i] == k) return;
    if (nseen >= 200 || InterlockedIncrement(&g_fontlog) > 200) return;
    seen[nseen++] = k;
    WideCharToMultiByte(CP_UTF8, 0, face, -1, a, sizeof(a), NULL, NULL);
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

static HFONT WINAPI H_CreateFontIndirectA(const LOGFONTA *lf)
{
    LOGFONTA f; WCHAR w[LF_FACESIZE];
    int sw;
    if (!lf) return ((HFONT (WINAPI *)(const LOGFONTA *))r_cfiA)(lf);
    MultiByteToWideChar(CP_ACP, 0, lf->lfFaceName, -1, w, LF_FACESIZE);
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
    if (face) { MultiByteToWideChar(CP_ACP, 0, face, -1, fw, LF_FACESIZE); sw = swap_face(fw, (BYTE)cs); log_face(fw, h, cs, sw, _ReturnAddress()); }
    if (sw) { fit(&hh, &cc, &qq, sw, fw); face = g_fontA; }
    return ((CreateFontA_t)r_cfA)(hh, w, esc, ori, wt, it, ul, so, cc, op, cp, qq, pf, face);
}

// A stock font whose face is swapped (DEFAULT_GUI_FONT is MS Shell Dlg) is handed out as a copy made
// with font_w. Copies live as long as the process; one a caller deleted is made again.
static HFONT g_stock[DEFAULT_GUI_FONT + 1];
typedef int (WINAPI *GetObjectW_t)(HANDLE, int, LPVOID);

static HGDIOBJ WINAPI H_GetStockObject(int i)
{
    HGDIOBJ o = ((HGDIOBJ (WINAPI *)(int))r_gso)(i);
    LOGFONTW lf;
    if (!r_gow || !o || i < OEM_FIXED_FONT || i > DEFAULT_GUI_FONT || i == DEFAULT_PALETTE) return o;
    if (g_stock[i] && ((GetObjectW_t)r_gow)(g_stock[i], sizeof(lf), &lf)) return g_stock[i];
    if (((GetObjectW_t)r_gow)(o, sizeof(lf), &lf) != sizeof(lf)) return o;
    if (swap_face(lf.lfFaceName, lf.lfCharSet) != 1) return o;    // not SYSTEM_FONT & co.
    g_stock[i] = font_w(&lf, _ReturnAddress());
    logf("stock font %u replaced by %08X %u\r\n", (DWORD)i, (DWORD)(UINT_PTR)g_stock[i], 0);
    return g_stock[i] ? g_stock[i] : o;
}

static hook_t g_font_hooks[] = {
    { "CreateFontA",         &r_cfA,  (FARPROC)H_CreateFontA },
    { "CreateFontW",         &r_cfW,  (FARPROC)H_CreateFontW },
    { "CreateFontIndirectA", &r_cfiA, (FARPROC)H_CreateFontIndirectA },
    { "CreateFontIndirectW", &r_cfiW, (FARPROC)H_CreateFontIndirectW },
    { "GetStockObject",      &r_gso,  (FARPROC)H_GetStockObject },
    { 0 }
};

static hook_t g_attr_hooks[] = {
    { "GetFileAttributesA",   &r_gfaA,  (FARPROC)H_GetFileAttributesA },
    { "GetFileAttributesW",   &r_gfaW,  (FARPROC)H_GetFileAttributesW },
    { "GetFileAttributesExA", &r_gfaxA, (FARPROC)H_GetFileAttributesExA },
    { "GetFileAttributesExW", &r_gfaxW, (FARPROC)H_GetFileAttributesExW },
    { "FindFirstFileA",       &r_ffA,   (FARPROC)H_FindFirstFileA },
    { "FindFirstFileW",       &r_ffW,   (FARPROC)H_FindFirstFileW },
    { "FindNextFileA",        &r_fnA,   (FARPROC)H_FindNextFileA },
    { "FindNextFileW",        &r_fnW,   (FARPROC)H_FindNextFileW },
    { 0 }
};
static hook_t g_proc_hooks[] = {
    { "GetProcAddress",       &r_gpa,   (FARPROC)H_GetProcAddress },
    { 0 }
};
static void resolve(hook_t *h)
{
    HMODULE k = GetModuleHandleA("kernel32.dll"), kb = GetModuleHandleA("kernelbase.dll");
    for (; h->name; h++) {
        *h->real = GetProcAddress(k, h->name);
        h->alt = kb ? GetProcAddress(kb, h->name) : NULL;
    }
}

// Redirect the imports of the module at `base` that resolve to one of hooks[]. Returns the number patched.
static int patch_base(BYTE *base, const char *mod, hook_t *hooks)
{
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    hook_t *h;
    int n = 0;
    if (!base) return 0;
    nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    if (!nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress) return 0;   // resource-only
    imp =(IMAGE_IMPORT_DESCRIPTOR *)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; imp->Name; imp++) {
        IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; t->u1.Function; t++)
            for (h = hooks; h->name; h++) {
                FARPROC f = (FARPROC)t->u1.Function;
                if (f == *h->real || (h->alt && f == h->alt)) {
                    DWORD old;
                    VirtualProtect(&t->u1.Function, sizeof(t->u1.Function), PAGE_READWRITE, &old);
                    t->u1.Function = (DWORD)(UINT_PTR)h->hook;
                    VirtualProtect(&t->u1.Function, sizeof(t->u1.Function), old, &old);
                    logf("patched %s import of %s at %08X\r\n", (DWORD)(UINT_PTR)mod, (DWORD)(UINT_PTR)h->name, (DWORD)(UINT_PTR)&t->u1.Function);
                    n++;
                    break;
                }
            }
    }
    return n;
}

static int patch_imports(const char *mod, hook_t *hooks) { return patch_base((BYTE *)GetModuleHandleA(mod), mod, hooks); }

// Delay-loaded imports (oleaut32 makes VB's StdFont fonts through them): their slots hold a loader stub
// until the first call, so they are matched by name and set to the hook either way.
static int patch_delay(BYTE *base, const char *mod, hook_t *hooks)
{
    IMAGE_NT_HEADERS *nt; IMAGE_DELAYLOAD_DESCRIPTOR *d; hook_t *h; DWORD rva; int n = 0;
    if (!base) return 0;
    nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT) return 0;
    if (!(rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress)) return 0;
    for (d = (IMAGE_DELAYLOAD_DESCRIPTOR *)(base + rva); d->DllNameRVA; d++) {
        IMAGE_THUNK_DATA *names, *t;
        if (!d->Attributes.RvaBased || !d->ImportNameTableRVA || !d->ImportAddressTableRVA) continue;   // old VA-based tables
        names = (IMAGE_THUNK_DATA *)(base + d->ImportNameTableRVA);
        t = (IMAGE_THUNK_DATA *)(base + d->ImportAddressTableRVA);
        for (; names->u1.AddressOfData; names++, t++) {
            const char *name;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            name = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            for (h = hooks; h->name; h++)
                if (*h->real && !lstrcmpA(name, h->name)) {
                    DWORD old;
                    VirtualProtect(&t->u1.Function, sizeof(t->u1.Function), PAGE_READWRITE, &old);
                    t->u1.Function = (DWORD)(UINT_PTR)h->hook;
                    VirtualProtect(&t->u1.Function, sizeof(t->u1.Function), old, &old);
                    logf("patched %s delay import of %s at %08X\r\n", (DWORD)(UINT_PTR)mod, (DWORD)(UINT_PTR)h->name, (DWORD)(UINT_PTR)&t->u1.Function);
                    n++;
                    break;
                }
        }
    }
    return n;
}

// Redirect THBRes25.dll's import of user32!PostMessageA.
static void patch_thbresize(void)
{
    static hook_t post[] = { { "PostMessageA", (FARPROC *)&g_realPost, (FARPROC)HookedPostMessageA }, { 0 } };
    if (!GetModuleHandleA("THBRes25.dll")) return;
    g_patched = TRUE;
    g_realPost = (PostMessageA_t)GetProcAddress(GetModuleHandleA("user32.dll"), "PostMessageA");
    if (!patch_imports("THBRes25.dll", post))
        logf("THBRes25 PostMessageA import not found %u %u %u\r\n", 0, 0, 0);
}

static void patch_attr(void)
{
    if (!g_vbpatched && GetModuleHandleA("MSVBVM60.DLL")) {
        g_vbpatched = TRUE;
        resolve(g_attr_hooks);
        patch_imports("MSVBVM60.DLL", g_attr_hooks);
        resolve(g_proc_hooks);
        patch_imports("MSVBVM60.DLL", g_proc_hooks);
        patch_vb_screen();
    }
    if (g_vbpatched && !g_fsopatched && GetModuleHandleA("scrrun.dll")) {
        g_fsopatched = TRUE;
        patch_imports("scrrun.dll", g_attr_hooks);
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
    for (i = 0; i < nseen; i++) if (seen[i] == k) return;
    if (nseen >= 160 || InterlockedIncrement(&g_textlog) > 150) return;
    seen[nseen++] = k;
    i = WideCharToMultiByte(CP_UTF8, 0, w, n, t, sizeof(t) - 1, NULL, NULL); t[i] = 0;
    for (i = 0; t[i]; i++) if (t[i] == '\r' || t[i] == '\n') t[i] = '|';
    a[0] = 0; lf.lfHeight = 0; tm.tmHeight = 0;
    if ((f = (HFONT)GetCurrentObject(dc, OBJ_FONT)) && GetObjectW(f, sizeof(lf), &lf) == sizeof(lf))
        WideCharToMultiByte(CP_UTF8, 0, lf.lfFaceName, -1, a, sizeof(a), NULL, NULL);
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
    { "GetTextMetricsA", &r_gtmA, (FARPROC)H_GetTextMetricsA },
    { "GetTextMetricsW", &r_gtmW, (FARPROC)H_GetTextMetricsW },
    { "DrawTextA",   &r_dtA,  (FARPROC)H_DrawTextA },
    { "DrawTextW",   &r_dtW,  (FARPROC)H_DrawTextW },
    { "ExtTextOutA", &r_etoA, (FARPROC)H_ExtTextOutA },
    { "ExtTextOutW", &r_etoW, (FARPROC)H_ExtTextOutW },
    { "TextOutA",    &r_toA,  (FARPROC)H_TextOutA },
    { 0 }
};

static void patch_text(void)
{
    static BOOL done;
    HMODULE u = GetModuleHandleA("user32.dll"), g = GetModuleHandleA("gdi32.dll"), gf = GetModuleHandleA("gdi32full.dll");
    hook_t *h;
    if (done || !GetModuleHandleA("MSVBVM60.DLL")) return;
    done = TRUE;
    for (h = g_text_hooks; h->name; h++) {
        HMODULE m = h->name[0] == 'D' ? u : g;
        *h->real = GetProcAddress(m, h->name);
        h->alt = h->name[0] == 'D' ? NULL : gf ? GetProcAddress(gf, h->name) : NULL;
    }
    patch_imports("MSVBVM60.DLL", g_text_hooks);
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
    WideCharToMultiByte(CP_ACP, 0, g_fontW, -1, g_fontA, LF_FACESIZE, NULL, NULL);
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
    WideCharToMultiByte(CP_UTF8, 0, g_fontW, -1, a, sizeof(a), NULL, NULL);
    logf("UI font for code page %u: '%s' (charset %u)\r\n", acp, (DWORD)(UINT_PTR)a, g_dbcs);
    logf("UI font line height %u/1000 em %u %u\r\n", g_uiline, 0, 0);
}

// Modules whose font imports are redirected: those in DynaRun's folder, OCX controls wherever they are,
// and these. Modules load later (an OCX with the first form that uses it), so the module list is checked
// again whenever a window is created. Not this dll's folder (Locale Emulator's dlls are there).
static const char *g_fontmods[] = {
    "MSVBVM60.DLL", "ole32.dll", "oleaut32.dll", "MFC42.DLL", "comctl32.dll", "THBRes25.dll", "PEGRP32E.dll", 0
};
static HMODULE g_fontdone[256];
static int g_nfontdone;
static DWORD g_nmods;

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
    HMODULE g, gf, mods[256]; DWORD need = 0; char path[MAX_PATH], appdir[MAX_PATH], selfdir[MAX_PATH];
    hook_t *h; int i, j;
    if (!g_fontready) pick_font();
    patch_text();
    if ((!g_fontW[0] || g_fontoff) && !g_charset) return;
    if (!r_cfiW) {
        g = GetModuleHandleA("gdi32.dll"); gf = GetModuleHandleA("gdi32full.dll");
        for (h = g_font_hooks; h->name; h++) {
            *h->real = GetProcAddress(g, h->name);
            h->alt = gf ? GetProcAddress(gf, h->name) : NULL;
        }
        r_gow = GetProcAddress(g, "GetObjectW");
        // kernel32 has it since Windows 7, psapi.dll before
        if (!(epm = (epm_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32EnumProcessModules")))
            epm = (epm_t)GetProcAddress(LoadLibraryA("psapi.dll"), "EnumProcessModules");
    }
    if (!epm || !epm(GetCurrentProcess(), mods, sizeof(mods), &need)) return;
    need /= sizeof(HMODULE);
    if (need > 256) need = 256;
    if (need == g_nmods) return;                  // nothing loaded or unloaded since the last look
    g_nmods = need;
    mod_dir(NULL, appdir); mod_dir(g_self, selfdir);
    for (i = 0; i < (int)need; i++) {
        for (j = 0; j < g_nfontdone && g_fontdone[j] != mods[i]; j++);
        if (j < g_nfontdone || g_nfontdone >= 256) continue;
        g_fontdone[g_nfontdone++] = mods[i];
        if (GetModuleFileNameA(mods[i], path, MAX_PATH) && font_module(mods[i], path, appdir, selfdir)) {
            const char *name = path + lstrlenA(path);
            while (name > path && name[-1] != '\\') name--;
            patch_base((BYTE *)mods[i], name, g_font_hooks);
            patch_delay((BYTE *)mods[i], name, g_font_hooks);
        }
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
    LOGFONTW lf; int i, k; HFONT to;
    if (!f || !r_cfiW || GetObjectW(f, sizeof(lf), &lf) != sizeof(lf)) return NULL;
    if (!(k = swap_face(lf.lfFaceName, lf.lfCharSet))) return NULL;
    if (k == 2 && (g_dbcs ? lf.lfCharSet == g_dbcs : lf.lfCharSet == ANSI_CHARSET || lf.lfCharSet == DEFAULT_CHARSET)) return NULL;
    for (i = 0; i < g_nfontmap; i++) if (g_fontmap[i].from == f) return g_fontmap[i].to;
    if (!(to = font_w(&lf, _ReturnAddress()))) return NULL;
    if (g_nfontmap < 256) { g_fontmap[g_nfontmap].from = f; g_fontmap[g_nfontmap++].to = to; }
    return to;
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
    LOGFONTW lf; int i; HFONT to;
    if (!f || !r_cfiW || g_lineh <= 0 || GetObjectW(f, sizeof(lf), &lf) != sizeof(lf)) return NULL;
    if (lstrcmpiW(lf.lfFaceName, g_fontW) || lf.lfHeight >= 0) return NULL;     // positive: a line copy already
    for (i = 0; i < g_nlinemap; i++) if (g_linemap[i].from == f) return g_linemap[i].to;
    lf.lfHeight = MulDiv(-lf.lfHeight, g_lineh, 1000);                         // cell height
    if (!(to = ((HFONT (WINAPI *)(const LOGFONTW *))r_cfiW)(&lf))) return NULL;
    if (g_nlinemap < 64) { g_linemap[g_nlinemap].from = f; g_linemap[g_nlinemap++].to = to; }
    return to;
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
        WideCharToMultiByte(CP_UTF8, 0, w, -1, t, sizeof(t), NULL, NULL);
        if (f && GetObjectW(f, sizeof(lf), &lf) == sizeof(lf)) {
            WideCharToMultiByte(CP_UTF8, 0, lf.lfFaceName, -1, a, sizeof(a), NULL, NULL);
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
    WideCharToMultiByte(CP_UTF8, 0, w, -1, t, sizeof(t), NULL, NULL);
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
        WideCharToMultiByte(CP_UTF8, 0, w, -1, tx, sizeof(tx), NULL, NULL);
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

// Work area of monitor m (and its full rect), less 2 px on every edge that has an auto-hide taskbar.
static BOOL work_area(HMONITOR m, RECT *wa, RECT *mon)
{
    static SHAppBarMessage_t sab; static BOOL tried;
    MONITORINFO mi; APPBARDATA ab; UINT e;
    mi.cbSize = sizeof(mi);
    if (!m || !GetMonitorInfoA(m, &mi)) return FALSE;
    *wa = mi.rcWork; *mon = mi.rcMonitor;
    if (!tried) { HMODULE s = LoadLibraryA("shell32.dll"); tried = TRUE; if (s) sab = (SHAppBarMessage_t)GetProcAddress(s, "SHAppBarMessage"); }
    if (!sab) return TRUE;
    for (e = ABE_LEFT; e <= ABE_BOTTOM; e++) {
        HWND bar;
        SecureZeroMemory(&ab, sizeof(ab)); ab.cbSize = sizeof(ab); ab.uEdge = e; ab.rc = mi.rcMonitor;
        bar = (HWND)sab(ABM_GETAUTOHIDEBAREX, &ab);                   // Vista and later: per monitor
        if (!bar && (mi.dwFlags & MONITORINFOF_PRIMARY)) {            // XP: primary monitor only
            SecureZeroMemory(&ab, sizeof(ab)); ab.cbSize = sizeof(ab); ab.uEdge = e;
            bar = (HWND)sab(ABM_GETAUTOHIDEBAR, &ab);
        }
        if (!bar) continue;
        if (e == ABE_LEFT   && wa->left   - mon->left   < 2) wa->left   = mon->left + 2;
        if (e == ABE_TOP    && wa->top    - mon->top    < 2) wa->top    = mon->top + 2;
        if (e == ABE_RIGHT  && mon->right  - wa->right  < 2) wa->right  = mon->right - 2;
        if (e == ABE_BOTTOM && mon->bottom - wa->bottom < 2) wa->bottom = mon->bottom - 2;
    }
    return TRUE;
}

// VB's Screen.Width / Height come from two globals in MSVBVM60 that its start-up code fills with
// GetSystemMetrics(SM_CXSCREEN / SM_CYSCREEN), before dynafix is loaded. DynaRun sets THBResize's maximum form
// size from them (MaxHeight = Screen.Height in pixels, MaxWidth = 4:3 of it) and draws the dashboard for that
// size. The globals are found from that start-up code (push 0 / call / push 1 / mov [cx],eax / call /
// push 20h / mov [cy],eax), checked against the real screen size and set to the primary monitor's work area,
// so DynaRun lays out everything as on a monitor of that size.
static void patch_vb_screen(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA("MSVBVM60.DLL");
    IMAGE_NT_HEADERS *nt; IMAGE_SECTION_HEADER *sec; WORD i;
    POINT o = { 0, 0 }; RECT wa, mon; int scx, scy; static BOOL done;
    if (!base || g_fullscreen || g_waclip || done) return;
    done = TRUE;
    if (!work_area(MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY), &wa, &mon) || EqualRect(&wa, &mon)) return;
    scx = GetSystemMetrics(SM_CXSCREEN); scy = GetSystemMetrics(SM_CYSCREEN);
    nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        BYTE *c, *e;
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        c = base + sec->VirtualAddress;
        e = c + sec->Misc.VirtualSize - 24;
        for (; c < e; c++) {
            int *px, *py; DWORD old;
            if (c[0] != 0x6A || c[1] != 0x00 || c[2] != 0xA3 || c[7] != 0xFF || c[9] != 0x6A || c[10] != 0x01 || c[11] != 0xA3
                || c[16] != 0xFF || c[18] != 0x6A || c[19] != 0x20 || c[20] != 0xA3) continue;
            px = *(int **)(c + 12); py = *(int **)(c + 21);
            if ((BYTE *)px < base || (BYTE *)px >= base + nt->OptionalHeader.SizeOfImage
                || (BYTE *)py < base || (BYTE *)py >= base + nt->OptionalHeader.SizeOfImage) continue;
            if (*px != scx || *py != scy) {
                logf("VB's screen size %ux%u is not the screen size, left alone %u\r\n", *px, *py, 0);
                return;
            }
            VirtualProtect(px, sizeof(int), PAGE_READWRITE, &old); *px = wa.right - wa.left; VirtualProtect(px, sizeof(int), old, &old);
            VirtualProtect(py, sizeof(int), PAGE_READWRITE, &old); *py = wa.bottom - wa.top; VirtualProtect(py, sizeof(int), old, &old);
            {
                char t[40]; wsprintfA(t, "%ux%u -> %ux%u", scx, scy, *px, *py);
                logf("VB's screen size %s (work area), globals at %08X %u\r\n", (DWORD)(UINT_PTR)t, (DWORD)(UINT_PTR)px, 0);
            }
            return;
        }
    }
    logf("VB's screen size not found in MSVBVM60.DLL %u %u %u\r\n", 0, 0, 0);
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

// THBResize, in the form's window procedure, sets every WINDOWPOS of the maximized form back to the full
// screen height (and 4:3 width), so the hook alone cannot change it: the form is subclassed once, when it
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
    if (!g_waclip && p->cx > 0 && p->cy > 0) {                        // keep THBResize's aspect ratio, like a smaller screen
        int w = r.right - r.left, hh = r.bottom - r.top;
        if (w * p->cy > hh * p->cx) w = MulDiv(hh, p->cx, p->cy); else hh = MulDiv(w, p->cy, p->cx);
        r.right = r.left + w; r.bottom = r.top + hh;
    }
    if (InterlockedIncrement(&g_cliplog) <= 20) {
        char t[40]; wsprintfA(t, "%ux%u -> %ux%u", p->cx, p->cy, r.right - r.left, r.bottom - r.top);
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
        if ((WNDPROC)GetWindowLongPtrA(h, GWLP_WNDPROC) == FormProc)
            u ? SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)old) : SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)old);
        RemovePropA(h, P_WP); RemovePropA(h, P_WPU);   // FormProc stays in the chain if someone sits above it
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
    logf("form %08X subclassed (window procedure was %08X) %u\r\n", (DWORD)(UINT_PTR)h, (DWORD)old, 0);
    PostMessageA(h, g_refit, 0, 0);
}

static BOOL CALLBACK count_win(HWND h, LPARAM l) { (void)h; (*(int *)l)++; return TRUE; }

__declspec(dllexport) LRESULT CALLBACK CwpProc(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION) {
        CWPSTRUCT *c = (CWPSTRUCT *)l;
        if (!g_pinned) {
            // Keep ourselves loaded and hooked after the launcher exits.
            char self[MAX_PATH], ev[64];
            HANDLE e;
            g_pinned = TRUE;
            GetModuleFileNameA(g_self, self, MAX_PATH);
            LoadLibraryA(self);
            SetWindowsHookExA(WH_CALLWNDPROC, (HOOKPROC)CwpProc, g_self, GetCurrentThreadId());
            SetWindowsHookExA(WH_CALLWNDPROCRET, (HOOKPROC)RetProc, g_self, GetCurrentThreadId());
            logf("dynafix active pid=%u tid=%u ansi-codepage=%u\r\n", GetCurrentProcessId(), GetCurrentThreadId(), GetACP());
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
        if (!g_fsopatched) patch_attr();
        // once MSVBVM60 is there, then whenever a window is made (a new form may have loaded new controls)
        if ((!g_fontpatched || c->message == WM_NCCREATE) && GetModuleHandleA("MSVBVM60.DLL")) { g_fontpatched = TRUE; patch_font(); }
        if (c->message == WM_SIZE) { note_size(c->hwnd, c->wParam, c->lParam); if (c->wParam == SIZE_MAXIMIZED && is_main_form(c->hwnd, TRUE)) arm_dump(c->hwnd); }
        else if (c->message == WM_GETMINMAXINFO) { if (is_main_form(c->hwnd, FALSE)) fit_minmax(c->hwnd, (MINMAXINFO *)c->lParam); }
        else if (c->message == WM_WINDOWPOSCHANGING) { if (is_main_form(c->hwnd, FALSE)) subclass_form(c->hwnd); }
        else if (c->message == WM_ACTIVATE && LOWORD(c->wParam) != WA_INACTIVE) sweep_fonts(c->hwnd);
        else if (c->message == WM_NCDESTROY) { RemovePropA(c->hwnd, P_W); RemovePropA(c->hwnd, P_L); RemovePropA(c->hwnd, P_DUP); RemovePropA(c->hwnd, P_SWEPT); }
    }
    return CallNextHookEx(NULL, code, w, l);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p)
{
    if (r == DLL_PROCESS_ATTACH) {
        BOOL noappid;
        g_self = h;
        DisableThreadLibraryCalls(h);
        {   // the same AppUserModelID as the launcher shortcuts (setup.c): a running DynaRun groups under the pinned icon
            WCHAR n[MAX_PATH]; int l = GetModuleFileNameW(NULL, n, MAX_PATH); char nv[4];
            noappid = GetEnvironmentVariableA("DYNAFIX_NOAPPID", nv, sizeof(nv)) && nv[0] == '1';   // diagnostic switch
            if (!noappid && l > 14 && n[l - 15] == '\\' && !lstrcmpiW(n + l - 14, L"DynaRun V3.exe")) {
                HRESULT (WINAPI *f)(PCWSTR) = (HRESULT (WINAPI *)(PCWSTR))GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SetCurrentProcessExplicitAppUserModelID");
                if (f) f(L"DynaRunFix.DynaRunV3");       // Windows 7+
            }
        }
        if (GetEnvironmentVariableA("TEMP", g_logpath, MAX_PATH - 16))
            lstrcatA(g_logpath, "\\dynafix.log");
        trim_log();
        if (noappid) logf("DYNAFIX_NOAPPID=1: the explicit AppUserModelID is not set %u %u %u\r\n", 0, 0, 0);
        if (GetEnvironmentVariableW(L"DYNAFIX_FONT", g_fontW, LF_FACESIZE) >= LF_FACESIZE) g_fontW[0] = 0;
        { char v[8]; g_fullscreen = GetEnvironmentVariableA("DYNAFIX_FULLSCREEN", v, sizeof(v)) && v[0] == '1';
          g_waclip = GetEnvironmentVariableA("DYNAFIX_WORKAREA", v, sizeof(v)) && !lstrcmpiA(v, "clip"); }
        g_fontoff = !lstrcmpiW(g_fontW, L"off") || !lstrcmpiW(g_fontW, L"none") || !lstrcmpW(g_fontW, L"0");
        {
            char v[8]; int i, n = 0;
            if (GetEnvironmentVariableA("DYNAFIX_FONT_SCALE", v, sizeof(v)) && v[0]) {
                for (i = 0; v[i] >= '0' && v[i] <= '9'; i++) n = n * 10 + v[i] - '0';
                if (n >= 50 && n <= 150) g_fontscale = n;
            }
            g_fontline = GetEnvironmentVariableA("DYNAFIX_FONT_LINE", v, sizeof(v)) && v[0] == '1';
            if (GetEnvironmentVariableA("DYNAFIX_FONT_LINEH", v, sizeof(v)) && v[0]) {   // 0 = off
                for (i = 0, n = 0; v[i] >= '0' && v[i] <= '9'; i++) n = n * 10 + v[i] - '0';
                if (!n || (n >= 900 && n <= 1500)) g_lineh = n;
            }
            if (GetEnvironmentVariableA("DYNAFIX_CHARSET", v, sizeof(v)) && v[0]) {
                for (i = 0, n = 0; v[i] >= '0' && v[i] <= '9'; i++) n = n * 10 + v[i] - '0';
                if (n > 0 && n < 255) g_charset = (BYTE)n;
            }
        }
    }
    return TRUE;
}
