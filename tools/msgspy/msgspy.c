// msgspy.exe [seconds] - hooks DynaRun V3's GUI thread and logs messages next to the exe
#include <windows.h>
#include <tlhelp32.h>

typedef void (*SetupFn)(DWORD, DWORD, const char *);
static DWORD g_pid, g_tid; static HWND g_form;
static char g_log[MAX_PATH];

static void out(const char *s)
{
    DWORD n; HANDLE h = CreateFileA(g_log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) { SetFilePointer(h, 0, NULL, FILE_END); WriteFile(h, s, lstrlenA(s), &n, NULL); CloseHandle(h); }
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &n, NULL);
}

static BOOL CALLBACK enumw(HWND h, LPARAM l)
{
    DWORD pid; char cls[64], ttl[128], line[400]; RECT r;
    DWORD tid = GetWindowThreadProcessId(h, &pid);
    if (pid != g_pid) return TRUE;
    GetClassNameA(h, cls, 64); GetWindowTextA(h, ttl, 128); GetWindowRect(h, &r);
    if (!lstrcmpA(cls, "ThunderRT6Main")) g_tid = tid;
    if (!lstrcmpA(cls, "ThunderRT6FormDC") && (GetWindowLongA(h, GWL_STYLE) & WS_MAXIMIZE)) g_form = h;
    if (l) {
        wsprintfA(line, "# win %08X %-18s vis=%d style=%08X rect=%d,%d,%d,%d '%s'\r\n", (UINT)(UINT_PTR)h, cls,
                  IsWindowVisible(h) ? 1 : 0, GetWindowLongA(h, GWL_STYLE), r.left, r.top, r.right, r.bottom, ttl);
        out(line);
    }
    return TRUE;
}

static DWORD atou(const char *s) { DWORD v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }

void mainCRTStartup(void)
{
    char dir[MAX_PATH], dll[MAX_PATH], comp[64], line[400], *p, *cmd;
    DWORD secs = 10, n = 64, postmsg = 0; PROCESSENTRY32 pe; HANDLE snap; HMODULE hd; OSVERSIONINFOA ov; HDC dc; RECT wa;
    HHOOK h1, h2, h3;

    cmd = GetCommandLineA();
    if (*cmd == '"') { cmd++; while (*cmd && *cmd != '"') cmd++; if (*cmd) cmd++; } else while (*cmd && *cmd != ' ') cmd++;
    while (*cmd == ' ') cmd++;
    if (*cmd) secs = atou(cmd);
    while (*cmd && *cmd != ' ') cmd++; while (*cmd == ' ') cmd++;
    { const char *q = cmd; while (*q) { char c = *q++; postmsg = postmsg * 16 + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10); } }

    GetModuleFileNameA(NULL, dir, MAX_PATH); p = dir + lstrlenA(dir); while (p > dir && *p != '\\') p--; *p = 0;
    GetComputerNameA(comp, &n);
    wsprintfA(g_log, "%s\\msgspy_%s.log", dir, comp);
    wsprintfA(dll, "%s\\msgspy.dll", dir);
    DeleteFileA(g_log);

    pe.dwSize = sizeof(pe); snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Process32First(snap, &pe)) do { if (!lstrcmpiA(pe.szExeFile, "DynaRun V3.exe")) g_pid = pe.th32ProcessID; } while (Process32Next(snap, &pe));
    CloseHandle(snap);
    if (!g_pid) { out("DynaRun not running\r\n"); ExitProcess(1); }

    ov.dwOSVersionInfoSize = sizeof(ov); GetVersionExA(&ov);
    dc = GetDC(NULL);
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    wsprintfA(line, "# host=%s os=%u.%u.%u pid=%u screen=%dx%d virt=%dx%d monitors=%d dpi=%d bpp=%d work=%d,%d,%d,%d maxed=%dx%d\r\n",
              comp, ov.dwMajorVersion, ov.dwMinorVersion, ov.dwBuildNumber, g_pid,
              GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), GetSystemMetrics(SM_CXVIRTUALSCREEN),
              GetSystemMetrics(SM_CYVIRTUALSCREEN), GetSystemMetrics(SM_CMONITORS), GetDeviceCaps(dc, LOGPIXELSX),
              GetDeviceCaps(dc, BITSPIXEL), wa.left, wa.top, wa.right, wa.bottom,
              GetSystemMetrics(SM_CXMAXIMIZED), GetSystemMetrics(SM_CYMAXIMIZED));
    ReleaseDC(NULL, dc);
    out(line);
    EnumWindows(enumw, 1);
    if (!g_tid) { out("no ThunderRT6Main\r\n"); ExitProcess(2); }

    hd = LoadLibraryA(dll);
    if (!hd) { out("cannot load msgspy.dll\r\n"); ExitProcess(3); }
    ((SetupFn)GetProcAddress(hd, "Setup"))(g_pid, secs, g_log);
    h1 = SetWindowsHookExA(WH_CALLWNDPROC, (HOOKPROC)GetProcAddress(hd, "CwpProc"), hd, g_tid);
    h2 = SetWindowsHookExA(WH_CALLWNDPROCRET, (HOOKPROC)GetProcAddress(hd, "RetProc"), hd, g_tid);
    h3 = SetWindowsHookExA(WH_GETMESSAGE, (HOOKPROC)GetProcAddress(hd, "GetMsgProc"), hd, g_tid);
    wsprintfA(line, "# hooked tid=%u (%p %p %p) for %us\r\n", g_tid, h1, h2, h3, secs);
    out(line);
    PostThreadMessageA(g_tid, WM_NULL, 0, 0);   // wake the thread so the DLL gets mapped
    if (postmsg && g_form) {
        Sleep(500);
        wsprintfA(line, "# post %04X to %08X\r\n", postmsg, (UINT)(UINT_PTR)g_form); out(line);
        PostMessageA(g_form, postmsg, 0, 0);
    }
    Sleep(secs * 1000 + 500);
    UnhookWindowsHookEx(h1); UnhookWindowsHookEx(h2); UnhookWindowsHookEx(h3);
    EnumWindows(enumw, 1);
    out("# done\r\n");
    ExitProcess(0);
}
