// msgspy.dll - in-process window message logger (x86, no CRT; runs on XP..Win11)
#include <windows.h>

#pragma data_seg(".shared")
DWORD g_pid = 0;
DWORD g_t0 = 0;
DWORD g_until = 0;
char  g_log[MAX_PATH] = {0};
#pragma data_seg()
#pragma comment(linker, "/SECTION:.shared,RWS")
#pragma comment(linker, "/EXPORT:CwpProc=_CwpProc@12")
#pragma comment(linker, "/EXPORT:RetProc=_RetProc@12")
#pragma comment(linker, "/EXPORT:GetMsgProc=_GetMsgProc@12")

static HANDLE g_h = INVALID_HANDLE_VALUE;
HINSTANCE g_inst;

static void out(const char *s)
{
    DWORD n;
    if (g_h == INVALID_HANDLE_VALUE) {
        g_h = CreateFileA(g_log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_h == INVALID_HANDLE_VALUE) return;
    }
    SetFilePointer(g_h, 0, NULL, FILE_END); WriteFile(g_h, s, lstrlenA(s), &n, NULL);
}

static int skip(UINT m)
{
    switch (m) {
    case 0x0D: case 0x0E: case 0x0F: case 0x14: case 0x20: case 0x21: case 0x3D: case 0x7F:
    case 0x84: case 0x85: case 0x86: case 0xA0: case 0x118: case 0x200: case 0x2A1: case 0x2A3:
    case 0x87: case 0x31: case 0x210: case 0x281: case 0x282: case 0x00AE: case 0x0093: case 0x0094:
        return 1;
    }
    if (m >= 0x132 && m <= 0x138) return 1; // WM_CTLCOLOR*
    return 0;
}

static void logmsg(char kind, HWND h, UINT m, WPARAM w, LPARAM l)
{
    char line[512], cls[64], extra[200];
    DWORD t = GetTickCount();
    if (t > g_until || skip(m)) return;
    cls[0] = 0; extra[0] = 0;
    GetClassNameA(h, cls, sizeof(cls));
    if ((m == 0x46 || m == 0x47) && l) {            // WM_WINDOWPOSCHANGING/CHANGED
        WINDOWPOS *p = (WINDOWPOS *)l;
        wsprintfA(extra, " pos=%d,%d sz=%d,%d fl=%04X after=%p", p->x, p->y, p->cx, p->cy, p->flags, p->hwndInsertAfter);
    } else if (m == 0x24 && l) {                    // WM_GETMINMAXINFO
        MINMAXINFO *p = (MINMAXINFO *)l;
        wsprintfA(extra, " maxsz=%d,%d maxpos=%d,%d trk=%d,%d..%d,%d", p->ptMaxSize.x, p->ptMaxSize.y,
                  p->ptMaxPosition.x, p->ptMaxPosition.y, p->ptMinTrackSize.x, p->ptMinTrackSize.y,
                  p->ptMaxTrackSize.x, p->ptMaxTrackSize.y);
    } else if ((m == 0x81 || m == 0x01) && l) {     // WM_NCCREATE / WM_CREATE
        CREATESTRUCTA *c = (CREATESTRUCTA *)l;
        char nm[80]; nm[0] = 0;
        if (c->lpszName && !IsBadReadPtr(c->lpszName, 1)) {
            if (IsWindowUnicode(h)) WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)c->lpszName, -1, nm, sizeof(nm), NULL, NULL);
            else lstrcpynA(nm, c->lpszName, sizeof(nm));
        }
        wsprintfA(extra, " cs=%d,%d %dx%d style=%08X ex=%08X parent=%p name='%s'", c->x, c->y, c->cx, c->cy, c->style, c->dwExStyle, c->hwndParent, nm);
    } else if (m == 0x0C && l && !IsBadReadPtr((void *)l, 1)) { // WM_SETTEXT
        char nm[80]; nm[0] = 0;
        if (IsWindowUnicode(h)) WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)l, -1, nm, sizeof(nm), NULL, NULL);
        else lstrcpynA(nm, (const char *)l, sizeof(nm));
        for (char *q = nm; *q; q++) if (*q == '\r' || *q == '\n') *q = ' ';
        wsprintfA(extra, " text='%s'", nm);
    } else if (m == 0x05) {                         // WM_SIZE
        wsprintfA(extra, " type=%d %dx%d", (int)w, LOWORD(l), HIWORD(l));
    }
    wsprintfA(line, "%6u %c %08X %-20s %04X w=%08X l=%08X vis=%d%s\r\n", t - g_t0, kind, (UINT)(UINT_PTR)h, cls,
              m, (UINT)w, (UINT)l, IsWindowVisible(h) ? 1 : 0, extra);
    out(line);
}

__declspec(dllexport) LRESULT CALLBACK CwpProc(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION) { CWPSTRUCT *c = (CWPSTRUCT *)l; logmsg('S', c->hwnd, c->message, c->wParam, c->lParam); }
    return CallNextHookEx(NULL, code, w, l);
}

__declspec(dllexport) LRESULT CALLBACK RetProc(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION) {
        CWPRETSTRUCT *c = (CWPRETSTRUCT *)l;
        if (c->message == 0x24 || c->message == 0x46) logmsg('R', c->hwnd, c->message, c->wParam, c->lParam);
    }
    return CallNextHookEx(NULL, code, w, l);
}

__declspec(dllexport) LRESULT CALLBACK GetMsgProc(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION && w == PM_REMOVE) { MSG *m = (MSG *)l; logmsg('P', m->hwnd, m->message, m->wParam, m->lParam); }
    return CallNextHookEx(NULL, code, w, l);
}

__declspec(dllexport) void Setup(DWORD pid, DWORD secs, const char *log)
{
    g_pid = pid; g_t0 = GetTickCount(); g_until = g_t0 + secs * 1000; lstrcpynA(g_log, log, MAX_PATH);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p)
{
    if (r == DLL_PROCESS_ATTACH) g_inst = h;
    if (r == DLL_PROCESS_DETACH && g_h != INVALID_HANDLE_VALUE) CloseHandle(g_h);
    return TRUE;
}
