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
#include <windows.h>
#include <shellapi.h>
#include <intrin.h>

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
static BOOL g_pinned, g_patched, g_vbpatched, g_fsopatched;
static LONG g_skipped, g_masked;
static PostMessageA_t g_realPost;
static char g_logpath[MAX_PATH];

static void logf(const char *fmt, DWORD a, DWORD b, DWORD c)
{
    char line[256]; DWORD n; HANDLE h;
    if (!g_logpath[0]) return;
    wsprintfA(line, fmt, a, b, c);
    h = CreateFileA(g_logpath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, line, lstrlenA(line), &n, NULL);
    CloseHandle(h);
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
// Under Locale Emulator (started by the launcher when Windows' UTF-8 option is on) fonts are created
// with the Big5 charset, so faces without CJK glyphs (DynaRun's forms mostly use Arial) are mapped to
// MingLiU. The launcher sets DYNAFIX_FONT (e.g. "Microsoft JhengHei"); those faces are swapped for it
// in the CreateFont* calls made by MSVBVM60, ole32 (VB's StdFont), MFC42 (OCX controls) and
// the ProEssentials chart (Pesgo32e.ocx / PEGRP32E.dll).
// MingLiU's line height is 1.0 em, JhengHei's 1.27 em, and DynaRun's multi-line labels are sized for
// MingLiU, so the em height of large fonts (16 px and up) is scaled by DYNAFIX_FONT_SCALE percent
// (default 90) to keep them fitting.
//
// Without LE, on a DBCS system locale (e.g. zh-TW, code page 950) whose FontAssoc key lacks
// "Associated Charset\ANSI(00)=YES" (common when Windows was installed in English and the locale
// changed later), GDI converts text drawn in ANSI_CHARSET fonts with code page 1252, so DynaRun's
// Big5 labels come out as Latin letters. The launcher then sets DYNAFIX_CHARSET (136 for Big5) and
// ANSI_CHARSET fonts are created with that charset instead, which is what FontAssoc would do.

static FARPROC r_cfiA, r_cfiW, r_cfA, r_cfW;
static WCHAR g_fontW[LF_FACESIZE];
static char g_fontA[LF_FACESIZE];
static LONG g_fontlog;
static int g_fontscale = 90;   // % of the requested em height (DYNAFIX_FONT_SCALE)
static BYTE g_charset;         // DYNAFIX_CHARSET: replaces ANSI_CHARSET, 0 = off
#define CS(c) (g_charset && (c) == ANSI_CHARSET ? g_charset : (c))

static const WCHAR *g_swap[] = {
    L"Arial", L"Times New Roman", L"MS Sans Serif", L"Microsoft Sans Serif", L"MS Shell Dlg", L"MS Shell Dlg 2",
    L"\x65B0\x7D30\x660E\x9AD4", L"\x7D30\x660E\x9AD4", L"PMingLiU", L"MingLiU", 0   // 新細明體 細明體
};

// Shrink only large text (small text gets hard to read), and draw the new face with ClearType.
static void fit(LONG *height, BYTE *quality)
{
    if (*height <= -16) *height = MulDiv(*height, g_fontscale, 100);
    *quality = CLEARTYPE_QUALITY;
}

static BOOL swap_face(const WCHAR *face)
{
    const WCHAR **s;
    for (s = g_swap; *s; s++) if (!lstrcmpiW(face, *s)) return TRUE;
    return FALSE;
}

static void log_face(const WCHAR *face, BOOL swapped)
{
    char a[LF_FACESIZE * 2];
    if (InterlockedIncrement(&g_fontlog) > 200) return;
    WideCharToMultiByte(CP_UTF8, 0, face, -1, a, sizeof(a), NULL, NULL);
    logf("font '%s' %s %u\r\n", (DWORD)(UINT_PTR)a, (DWORD)(UINT_PTR)(swapped ? "-> swapped" : "kept"), 0);
}

static HFONT WINAPI H_CreateFontIndirectW(const LOGFONTW *lf)
{
    LOGFONTW f;
    BOOL sw = lf && g_fontW[0] && swap_face(lf->lfFaceName);
    if (lf) log_face(lf->lfFaceName, sw);
    if (!sw && (!lf || CS(lf->lfCharSet) == lf->lfCharSet)) return ((HFONT (WINAPI *)(const LOGFONTW *))r_cfiW)(lf);
    f = *lf;
    f.lfCharSet = CS(f.lfCharSet);
    if (sw) { lstrcpynW(f.lfFaceName, g_fontW, LF_FACESIZE); fit(&f.lfHeight, &f.lfQuality); }
    return ((HFONT (WINAPI *)(const LOGFONTW *))r_cfiW)(&f);
}

static HFONT WINAPI H_CreateFontIndirectA(const LOGFONTA *lf)
{
    LOGFONTA f; WCHAR w[LF_FACESIZE];
    BOOL sw;
    if (!lf) return ((HFONT (WINAPI *)(const LOGFONTA *))r_cfiA)(lf);
    MultiByteToWideChar(CP_ACP, 0, lf->lfFaceName, -1, w, LF_FACESIZE);
    sw = g_fontW[0] && swap_face(w);
    log_face(w, sw);
    if (!sw && CS(lf->lfCharSet) == lf->lfCharSet) return ((HFONT (WINAPI *)(const LOGFONTA *))r_cfiA)(lf);
    f = *lf;
    f.lfCharSet = CS(f.lfCharSet);
    if (sw) { lstrcpynA(f.lfFaceName, g_fontA, LF_FACESIZE); fit(&f.lfHeight, &f.lfQuality); }
    return ((HFONT (WINAPI *)(const LOGFONTA *))r_cfiA)(&f);
}

typedef HFONT (WINAPI *CreateFontW_t)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR);
typedef HFONT (WINAPI *CreateFontA_t)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR);

static HFONT WINAPI H_CreateFontW(int h, int w, int esc, int ori, int wt, DWORD it, DWORD ul, DWORD so, DWORD cs,
                                  DWORD op, DWORD cp, DWORD q, DWORD pf, LPCWSTR face)
{
    LONG hh = h; BYTE qq = (BYTE)q;
    BOOL sw = face && g_fontW[0] && swap_face(face);
    if (face) log_face(face, sw);
    if (sw) { fit(&hh, &qq); face = g_fontW; }
    return ((CreateFontW_t)r_cfW)(hh, w, esc, ori, wt, it, ul, so, CS(cs), op, cp, qq, pf, face);
}

static HFONT WINAPI H_CreateFontA(int h, int w, int esc, int ori, int wt, DWORD it, DWORD ul, DWORD so, DWORD cs,
                                  DWORD op, DWORD cp, DWORD q, DWORD pf, LPCSTR face)
{
    LONG hh = h; BYTE qq = (BYTE)q; WCHAR fw[LF_FACESIZE];
    BOOL sw = FALSE;
    if (face) { MultiByteToWideChar(CP_ACP, 0, face, -1, fw, LF_FACESIZE); sw = g_fontW[0] && swap_face(fw); log_face(fw, sw); }
    if (sw) { fit(&hh, &qq); face = g_fontA; }
    return ((CreateFontA_t)r_cfA)(hh, w, esc, ori, wt, it, ul, so, CS(cs), op, cp, qq, pf, face);
}

static hook_t g_font_hooks[] = {
    { "CreateFontA",         &r_cfA,  (FARPROC)H_CreateFontA },
    { "CreateFontW",         &r_cfW,  (FARPROC)H_CreateFontW },
    { "CreateFontIndirectA", &r_cfiA, (FARPROC)H_CreateFontIndirectA },
    { "CreateFontIndirectW", &r_cfiW, (FARPROC)H_CreateFontIndirectW },
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

// Redirect the imports of `mod` that resolve to one of hooks[]. Returns the number patched.
static int patch_imports(const char *mod, hook_t *hooks)
{
    BYTE *base = (BYTE *)GetModuleHandleA(mod);
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    hook_t *h;
    int n = 0;
    if (!base) return 0;
    nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
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
    }
    if (g_vbpatched && !g_fsopatched && GetModuleHandleA("scrrun.dll")) {
        g_fsopatched = TRUE;
        patch_imports("scrrun.dll", g_attr_hooks);
    }
}

// Modules whose CreateFont* imports get the face swap; some (the chart) load later, so each
// one is retried until it is present.
static const char *g_fontmods[] = { "MSVBVM60.DLL", "ole32.dll", "MFC42.DLL", "Pesgo32e.ocx", "PEGRP32E.dll", 0 };
static BOOL g_fontmodpatched[5], g_fontpatched;

static void patch_font(void)
{
    HMODULE g, gf;
    hook_t *h;
    int i, left = 0;
    if ((!g_fontW[0] && !g_charset) || !GetModuleHandleA("MSVBVM60.DLL")) return;
    if (!r_cfiW) {
        g = GetModuleHandleA("gdi32.dll"); gf = GetModuleHandleA("gdi32full.dll");
        for (h = g_font_hooks; h->name; h++) {
            *h->real = GetProcAddress(g, h->name);
            h->alt = gf ? GetProcAddress(gf, h->name) : NULL;
        }
    }
    for (i = 0; g_fontmods[i]; i++) {
        if (g_fontmodpatched[i]) continue;
        if (GetModuleHandleA(g_fontmods[i])) { g_fontmodpatched[i] = TRUE; patch_imports(g_fontmods[i], g_font_hooks); }
        else left++;
    }
    g_fontpatched = !left;
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
            logf("dynafix active pid=%u tid=%u ansi-codepage=%u\r\n", GetCurrentProcessId(), GetCurrentThreadId(), GetACP());
            wsprintfA(ev, "Local\\dynafix_ready_%u", GetCurrentProcessId());
            e = OpenEventA(EVENT_MODIFY_STATE, FALSE, ev);
            if (e) SetEvent(e);   // handle stays open: a later launcher sees the fix is already active
        }
        if (!g_patched) patch_thbresize();
        if (!g_fsopatched) patch_attr();
        if (!g_fontpatched) patch_font();
        if (c->message == WM_SIZE) note_size(c->hwnd, c->wParam, c->lParam);
        else if (c->message == WM_NCDESTROY) { RemovePropA(c->hwnd, P_W); RemovePropA(c->hwnd, P_L); RemovePropA(c->hwnd, P_DUP); }
    }
    return CallNextHookEx(NULL, code, w, l);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p)
{
    if (r == DLL_PROCESS_ATTACH) {
        g_self = h;
        DisableThreadLibraryCalls(h);
        if (GetEnvironmentVariableA("TEMP", g_logpath, MAX_PATH - 16))
            lstrcatA(g_logpath, "\\dynafix.log");
        if (GetEnvironmentVariableW(L"DYNAFIX_FONT", g_fontW, LF_FACESIZE) >= LF_FACESIZE) g_fontW[0] = 0;
        WideCharToMultiByte(CP_ACP, 0, g_fontW, -1, g_fontA, LF_FACESIZE, NULL, NULL);
        {
            char v[8]; int i, n = 0;
            if (GetEnvironmentVariableA("DYNAFIX_FONT_SCALE", v, sizeof(v)) && v[0]) {
                for (i = 0; v[i] >= '0' && v[i] <= '9'; i++) n = n * 10 + v[i] - '0';
                if (n >= 50 && n <= 150) g_fontscale = n;
            }
            if (GetEnvironmentVariableA("DYNAFIX_CHARSET", v, sizeof(v)) && v[0]) {
                for (i = 0, n = 0; v[i] >= '0' && v[i] <= '9'; i++) n = n * 10 + v[i] - '0';
                if (n > 0 && n < 255) g_charset = (BYTE)n;
            }
        }
    }
    return TRUE;
}
