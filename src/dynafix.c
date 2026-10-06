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
#include <windows.h>

#pragma comment(linker, "/EXPORT:CwpProc=_CwpProc@12")

#define THB_MSG 0x591
#define P_W     "dynafix.w"
#define P_L     "dynafix.l"
#define P_DUP   "dynafix.dup"

typedef BOOL (WINAPI *PostMessageA_t)(HWND, UINT, WPARAM, LPARAM);

static HINSTANCE g_self;
static BOOL g_pinned, g_patched;
static LONG g_skipped;
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

// Redirect THBRes25.dll's import of user32!PostMessageA.
static void patch_thbresize(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA("THBRes25.dll");
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    FARPROC real;
    if (!base) return;
    g_patched = TRUE;
    real = GetProcAddress(GetModuleHandleA("user32.dll"), "PostMessageA");
    nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; imp->Name; imp++) {
        IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        if (lstrcmpiA((char *)(base + imp->Name), "USER32.dll")) continue;
        for (; t->u1.Function; t++) {
            if ((FARPROC)t->u1.Function == real) {
                DWORD old;
                g_realPost = (PostMessageA_t)real;
                VirtualProtect(&t->u1.Function, sizeof(t->u1.Function), PAGE_READWRITE, &old);
                t->u1.Function = (DWORD)(UINT_PTR)HookedPostMessageA;
                VirtualProtect(&t->u1.Function, sizeof(t->u1.Function), old, &old);
                logf("patched THBRes25 PostMessageA at %08X %u %u\r\n", (DWORD)(UINT_PTR)&t->u1.Function, 0, 0);
                return;
            }
        }
    }
    logf("THBRes25 PostMessageA import not found %u %u %u\r\n", 0, 0, 0);
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
            logf("dynafix active pid=%u tid=%u %u\r\n", GetCurrentProcessId(), GetCurrentThreadId(), 0);
            wsprintfA(ev, "Local\\dynafix_ready_%u", GetCurrentProcessId());
            e = OpenEventA(EVENT_MODIFY_STATE, FALSE, ev);
            if (e) { SetEvent(e); CloseHandle(e); }
        }
        if (!g_patched) patch_thbresize();
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
