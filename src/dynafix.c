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
#include <windows.h>
#include <intrin.h>

#pragma comment(linker, "/EXPORT:CwpProc=_CwpProc@12")

#define THB_MSG 0x591
#define P_W     "dynafix.w"
#define P_L     "dynafix.l"
#define P_DUP   "dynafix.dup"

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

// *real = kernel32 export, alt = kernelbase export (api-ms-win-* imports bind there)
typedef struct { const char *name; FARPROC *real; FARPROC hook; FARPROC alt; } hook_t;

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
    }
    if (g_vbpatched && !g_fsopatched && GetModuleHandleA("scrrun.dll")) {
        g_fsopatched = TRUE;
        patch_imports("scrrun.dll", g_attr_hooks);
    }
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
    }
    return TRUE;
}
