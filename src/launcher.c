// DynaRunFix.exe - starts DynaRun V3 (or attaches to a running one) and loads dynafix.dll into it.
// Usage: DynaRunFix.exe ["path\to\DynaRun V3.exe"]
#include <windows.h>
#include <tlhelp32.h>
#include "version.h"

#define DEFAULT_EXE "C:\\Program Files (x86)\\Dyna Pro Dynamometers\\DynaRun V3.exe"
#define LE_PROFILE  "7e3c1d2a-5b4f-4c6e-9a8d-1f2e3d4c5b6a"   // zh-TW profile in le\LEConfig.xml

static DWORD g_pid, g_tid, g_first;
static HWND g_wnd;

static BOOL CALLBACK findwin(HWND h, LPARAM l)
{
    DWORD pid, tid = GetWindowThreadProcessId(h, &pid);
    if (pid == g_pid) { g_tid = tid; g_wnd = h; return FALSE; }
    return TRUE;
}

static DWORD running(void)
{
    PROCESSENTRY32 pe; HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); DWORD pid = 0;
    pe.dwSize = sizeof(pe);
    if (Process32First(s, &pe)) do { if (!lstrcmpiA(pe.szExeFile, "DynaRun V3.exe")) pid = pe.th32ProcessID; } while (Process32Next(s, &pe));
    CloseHandle(s);
    return pid;
}

static BOOL is_dynarun(DWORD pid)
{
    PROCESSENTRY32 pe; HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); BOOL r = FALSE;
    pe.dwSize = sizeof(pe);
    if (Process32First(s, &pe)) do { if (pe.th32ProcessID == pid) r = !lstrcmpiA(pe.szExeFile, "DynaRun V3.exe"); } while (!r && Process32Next(s, &pe));
    CloseHandle(s);
    return r;
}

// one line in %TEMP%\dynafix.log, next to what dynafix.dll writes from inside DynaRun
static void llog(const char *fmt, DWORD a, DWORD b)
{
    char path[MAX_PATH], line[512]; DWORD n; HANDLE h;
    if (!GetEnvironmentVariableA("TEMP", path, MAX_PATH - 16)) return;
    lstrcatA(path, "\\dynafix.log");
    wsprintfA(line, fmt, a, b);
    h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, line, lstrlenA(line), &n, NULL);
    CloseHandle(h);
}

// above 10 MB, dynafix.log is cut to its newest 8 MB, from a full line on (dynafix.dll does the same; whichever runs first)
static void trim_log(void)
{
    HANDLE h; char path[MAX_PATH]; DWORD size, n, i; char *buf;
    if (!GetEnvironmentVariableA("TEMP", path, MAX_PATH - 16)) return;
    lstrcatA(path, "\\dynafix.log");
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
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

// the log line for this start: how we were called and by whom
static void log_start(const char *cmd)
{
    PROCESSENTRY32 pe; HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); DWORD me = GetCurrentProcessId(), pp = 0;
    char parent[64], args[160], cwd[160], msg[700]; STARTUPINFOA st;
    const char *how = "a plain start";
    lstrcpyA(parent, "?"); pe.dwSize = sizeof(pe);
    if (Process32First(s, &pe)) do { if (pe.th32ProcessID == me) pp = pe.th32ParentProcessID; } while (!pp && Process32Next(s, &pe));
    if (pp && Process32First(s, &pe)) do { if (pe.th32ProcessID == pp) lstrcpynA(parent, pe.szExeFile, sizeof(parent)); } while (lstrcmpA(parent, "?") == 0 && Process32Next(s, &pe));
    CloseHandle(s);
    if (CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, cmd, 10, "/autostart", 10) == CSTR_EQUAL && (!cmd[10] || cmd[10] == ' ')) how = "/autostart";
    else if (CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, cmd, 8, "/restart", 8) == CSTR_EQUAL && (!cmd[8] || cmd[8] == ' ')) how = "/restart";
    lstrcpynA(args, cmd, sizeof(args));
    { volatile char *z = (volatile char *)&st; int i; for (i = 0; i < (int)sizeof(st); i++) z[i] = 0; } st.cb = sizeof(st); GetStartupInfoA(&st);
    if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) lstrcpyA(cwd, "?");
    wsprintfA(msg, "launcher: started pid=%u, DynaRunFix %s (%s), %s, args [%s], parent %s (pid %u), startup flags %08X show %u, cwd [%s]\r\n", me, DRF_VERSION, DRF_COMMIT, how, args, parent, pp, st.dwFlags, st.wShowWindow, cwd);
    llog("%s", (DWORD)(UINT_PTR)msg, 0);
}

static void fail(const char *msg) { MessageBoxA(NULL, msg, "DynaRunFix", MB_ICONERROR); ExitProcess(1); }

static BOOL is_admin(void)
{
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY; PSID sid; BOOL r = FALSE;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid)) {
        if (!CheckTokenMembership(NULL, sid, &r)) r = FALSE;
        FreeSid(sid);
    }
    return r;
}

// TRUE if FontAssoc maps ANSI_CHARSET fonts to the locale's charset (Windows installed in that language)
static BOOL fontassoc(void)
{
    char v[8]; HKEY k; DWORD n = sizeof(v); BOOL assoc = FALSE;
    if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\FontAssoc\\Associated Charset", 0, KEY_QUERY_VALUE, &k)) {
        assoc = !RegQueryValueExA(k, "ANSI(00)", NULL, NULL, (BYTE *)v, &n) && !lstrcmpiA(v, "YES");
        RegCloseKey(k);
    }
    return assoc;
}

// DBCS system locale whose FontAssoc key does not map ANSI_CHARSET fonts (Windows installed in English,
// locale changed later): DynaRun's Big5 labels would be drawn as Latin letters. dynafix then creates
// those fonts with the locale's charset (DYNAFIX_CHARSET); the face is swapped for the UI font anyway.
static void charset_env(void)
{
    static const struct { UINT cp; const char *cs; } t[] = { { 950, "136" }, { 936, "134" }, { 932, "128" }, { 949, "129" } };
    UINT acp = GetACP(); int i;
    if (fontassoc()) return;
    for (i = 0; i < 4; i++) if (t[i].cp == acp) {
        if (!GetEnvironmentVariableA("DYNAFIX_CHARSET", NULL, 0)) SetEnvironmentVariableA("DYNAFIX_CHARSET", t[i].cs);
    }
}

static void elevate(const char *args)
{
    SHELLEXECUTEINFOA se; char self[MAX_PATH]; int i;
    if (is_admin()) return;
    GetModuleFileNameA(NULL, self, MAX_PATH);
    { volatile char *z = (volatile char *)&se; for (i = 0; i < (int)sizeof(se); i++) z[i] = 0; }
    se.cbSize = sizeof(se); se.lpVerb = "runas"; se.lpFile = self; se.lpParameters = args; se.nShow = SW_SHOWNORMAL;
    ExitProcess(ShellExecuteExA(&se) ? 0 : 1);
}

// TRUE if the compatibility setting "Run as administrator" (RUNASADMIN layer) is set for exe.
static BOOL runasadmin(const char *exe)
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    char v[512], *w; DWORD n, t; HKEY k; int r; BOOL found = FALSE;
    for (r = 0; r < 2 && !found; r++) {
        if (RegOpenKeyExA(roots[r], "Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
                          0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &k)) continue;
        n = sizeof(v) - 1;
        if (!RegQueryValueExA(k, exe, NULL, &t, (BYTE *)v, &n) && t == REG_SZ) {
            v[n] = 0;
            for (w = v; *w && !found; ) {          // space-separated layer names, e.g. "~ RUNASADMIN"
                char *end = w; char c;
                while (*end && *end != ' ') end++;
                c = *end; *end = 0;
                found = !lstrcmpiA(w, "RUNASADMIN");
                *end = c; w = *end ? end + 1 : end;
            }
        }
        RegCloseKey(k);
    }
    return found;
}

// First thread of process pid (DynaRun's GUI thread), 0 if none yet.
static DWORD first_thread(DWORD pid)
{
    THREADENTRY32 te; HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); DWORD tid = 0;
    if (s == INVALID_HANDLE_VALUE) return 0;
    te.dwSize = sizeof(te);
    if (Thread32First(s, &te)) do { if (te.th32OwnerProcessID == pid) tid = te.th32ThreadID; } while (!tid && Thread32Next(s, &te));
    CloseHandle(s);
    return tid;
}

// Hook DynaRun's GUI thread before it makes its first window, so dynafix.dll is in place before any font is
// made: fonts made before that (splash screen, forms loaded at start-up) would keep the old face/charset,
// and VB's OLE fonts are cached and reused. A thread can be hooked only once it has a message queue, which
// it gets with its first USER call (well before VB creates a window): poll from the moment it runs.
// Exits the launcher once the dll has reported in; returns if that did not happen.
static void early_hook(const char *dll, DWORD pid, DWORD tid, HANDLE proc)
{
    char ev[64]; HANDLE e, hs[2]; HMODULE hd; HHOOK hk; int i;
    wsprintfA(ev, "Local\\dynafix_ready_%u", pid);
    e = CreateEventA(NULL, TRUE, FALSE, ev);
    hd = LoadLibraryA(dll);
    for (hk = NULL, i = 0; hd && !hk && i < 5000; i++)
        if (!(hk = SetWindowsHookExA(WH_CALLWNDPROC, (HOOKPROC)GetProcAddress(hd, "CwpProc"), hd, tid))) Sleep(1);
    if (!hk) { llog("launcher: early hook failed (error %u) %u\r\n", GetLastError(), 0); return; }
    // the dll reports in (and keeps itself loaded) when DynaRun's first window gets a message
    hs[0] = e; hs[1] = proc;
    if (WaitForMultipleObjects(2, hs, FALSE, 60000) == WAIT_OBJECT_0) {
        llog("launcher: DynaRun pid=%u hooked from start (thread %u)\r\n", pid, tid);
        UnhookWindowsHookEx(hk);
        ExitProcess(0);
    }
    UnhookWindowsHookEx(hk);
    llog("launcher: early hook did not report in %u %u\r\n", 0, 0);
}

// Waits until Shell_TrayWnd exists and the screen size and work area have not changed for 3 s (polled every 250 ms);
// starts anyway after 60 s.
static void wait_desktop(void)
{
    DWORD t0 = GetTickCount(), still = t0, now; int sx = 0, sy = 0, x, y; RECT wa, w0 = { 0, 0, 0, 0 };
    for (;;) {
        now = GetTickCount();
        wa.left = wa.top = wa.right = wa.bottom = 0;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
        x = GetSystemMetrics(SM_CXSCREEN); y = GetSystemMetrics(SM_CYSCREEN);
        if (x != sx || y != sy || !EqualRect(&wa, &w0)) { sx = x; sy = y; w0 = wa; still = now; }
        if ((FindWindowA("Shell_TrayWnd", NULL) && now - still >= 3000) || now - t0 >= 60000) break;
        Sleep(250);
    }
    llog("launcher: autostart waited %u ms for the desktop (%u = gave up)\r\n", now - t0, now - t0 >= 60000);
}

void WinMainCRTStartup(void)
{
    char exe[MAX_PATH], dir[MAX_PATH], dll[MAX_PATH], le[MAX_PATH], line[3 * MAX_PATH], ev[64], *p, *cmd, *args;
    STARTUPINFOA si; PROCESS_INFORMATION pi; HMODULE hd; HHOOK hk; HANDLE e, h; HKEY k; DWORD n = MAX_PATH; int i;

    // dynafix.dll lives next to this launcher
    GetModuleFileNameA(NULL, dll, MAX_PATH); p = dll + lstrlenA(dll); while (p > dll && *p != '\\') p--; lstrcpyA(p, "\\dynafix.dll");

    cmd = GetCommandLineA();
    if (*cmd == '"') { cmd++; while (*cmd && *cmd != '"') cmd++; if (*cmd) cmd++; } else while (*cmd && *cmd != ' ') cmd++;
    while (*cmd == ' ') cmd++;
    trim_log();
    log_start(cmd);
    // "/autostart" (the sign-in Run value): at sign-in the desktop is still being set up (taskbar, resolution, DPI) and
    // DynaRun lays its screen out from the size it sees at start: wait for the taskbar and 3 s of an unchanged size
    if (!lstrcmpiA(cmd, "/autostart") || (cmd[0] == '/' && cmd[10] == ' ' && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, cmd, 10, "/autostart", 10) == CSTR_EQUAL)) {
        for (cmd += 10; *cmd == ' '; cmd++);
        if (!running()) wait_desktop();
    }
    // "/restart <pid>" (from dynafix.dll after the first-time setup): wait for that DynaRun to end, then start normally
    if (!lstrcmpiA(cmd, "/restart") || (cmd[0] == '/' && cmd[8] == ' ' && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, cmd, 8, "/restart", 8) == CSTR_EQUAL)) {
        DWORD old = 0; HANDLE h0;
        for (p = cmd + 8; *p == ' '; p++);
        while (*p >= '0' && *p <= '9') old = old * 10 + (*p++ - '0');
        if (old && (h0 = OpenProcess(SYNCHRONIZE, FALSE, old))) { WaitForSingleObject(h0, 15000); CloseHandle(h0); }
        cmd = p; while (*cmd == ' ') cmd++;
    }
    args = cmd;
    if (*cmd == '"') { cmd++; lstrcpynA(exe, cmd, MAX_PATH); p = exe; while (*p && *p != '"') p++; *p = 0; }
    else if (*cmd) lstrcpynA(exe, cmd, MAX_PATH);
    else {
        lstrcpyA(exe, DEFAULT_EXE);
        if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DynaRunFix", 0, KEY_QUERY_VALUE, &k)) {
            if (RegQueryValueExA(k, "DynaRunExe", NULL, NULL, (BYTE *)exe, &n)) lstrcpyA(exe, DEFAULT_EXE);
            RegCloseKey(k);
        }
    }
    lstrcpyA(dir, exe); p = dir + lstrlenA(dir); while (p > dir && *p != '\\') p--; *p = 0;

    g_pid = running();
    if (g_pid) {
        h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, g_pid);
        if (h) CloseHandle(h); else if (GetLastError() == ERROR_ACCESS_DENIED) elevate(args);
    } else {
        { volatile char *z = (volatile char *)&si; for (i = 0; i < (int)sizeof(si); i++) z[i] = 0; } si.cb = sizeof(si);
        // With Windows' UTF-8 option on, start DynaRun through Locale Emulator (le\LEProc.exe next to this
        // launcher, profile LE_PROFILE in le\LEConfig.xml) so GDI-drawn labels use the zh-TW code page too.
        // Only for a Traditional Chinese system locale (legacy code page 950): the LE profile is zh-TW.
        // Other locales get just the manifest, which selects their own legacy code page.
        lstrcpyA(le, dll); p = le + lstrlenA(le); while (p > le && *p != '\\') p--; lstrcpyA(p, "\\le\\LEProc.exe");
        if (!GetLocaleInfoA(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, ev, sizeof(ev))) ev[0] = 0;
        // Also for a zh-TW system locale whose FontAssoc lacks ANSI(00)=YES (Windows installed in English,
        // locale changed later): LE gives DynaRun the Big5 code page and charset for every window, including
        // tooltips and OCX controls that dynafix's font hooks do not reach.
        if (!lstrcmpA(ev, "950") && (GetACP() == CP_UTF8 || (GetACP() == 950 && !fontassoc())) &&
            GetFileAttributesA(le) != INVALID_FILE_ATTRIBUTES) {
            // LEProc cannot raise DynaRun itself; elevate first if DynaRun is set to run as administrator.
            if (runasadmin(exe)) elevate(args);
            wsprintfA(line, "\"%s\" -runas " LE_PROFILE " \"%s\"", le, exe);
            // LE creates DynaRun suspended (to load its own dll) and resumes it: catch it right away. LEProc runs in a
            // job, so DynaRun's creation is reported at once (polling the process list can be late: VB reads the
            // screen size before its first form, and dynafix must be in place by then); polling is the fallback.
            {
                HANDLE job = CreateJobObjectA(NULL, NULL), port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
                JOBOBJECT_ASSOCIATE_COMPLETION_PORT jp; BOOL injob = FALSE;
                if (job && port) {
                    jp.CompletionKey = job; jp.CompletionPort = port;
                    injob = SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &jp, sizeof(jp));
                }
                if (!CreateProcessA(le, line, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, dir, &si, &pi)) fail("Cannot start Locale Emulator");
                injob = injob && AssignProcessToJobObject(job, pi.hProcess);
                ResumeThread(pi.hThread);
                if (injob) {
                    DWORD msg, t0 = GetTickCount(); ULONG_PTR key; LPOVERLAPPED ov;
                    while (!g_pid && GetTickCount() - t0 < 30000 && GetQueuedCompletionStatus(port, &msg, &key, &ov, 30000))
                        if (msg == JOB_OBJECT_MSG_NEW_PROCESS && (DWORD)(UINT_PTR)ov != pi.dwProcessId && is_dynarun((DWORD)(UINT_PTR)ov))
                            g_pid = (DWORD)(UINT_PTR)ov;
                    if (g_pid) {   // its first thread, at once (a thread list of the whole system takes too long)
                        typedef LONG (NTAPI *gnt_t)(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, HANDLE *);
                        typedef DWORD (WINAPI *gti_t)(HANDLE);
                        gnt_t gnt = (gnt_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtGetNextThread");
                        gti_t gti = (gti_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadId");
                        HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, g_pid), ht = NULL;
                        if (hp && gnt && gti && !gnt(hp, NULL, THREAD_QUERY_INFORMATION, 0, 0, &ht)) { g_first = gti(ht); CloseHandle(ht); }
                        if (hp) CloseHandle(hp);
                    }
                }
                CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            }
            for (i = 0; i < 30000 && !g_pid && !(g_pid = running()); i++) Sleep(1);
            if (!g_pid) fail("DynaRun V3.exe did not start under Locale Emulator");
            {
                DWORD tid = g_first; HANDLE h0 = OpenProcess(SYNCHRONIZE, FALSE, g_pid);
                for (i = 0; i < 1000 && !tid && !(tid = first_thread(g_pid)); i++) Sleep(1);
                if (tid && h0) early_hook(dll, g_pid, tid, h0);
                if (h0) CloseHandle(h0);
            }
        } else {
            charset_env();
            if (!CreateProcessA(exe, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, dir, &si, &pi)) {
                if (GetLastError() == ERROR_ELEVATION_REQUIRED) elevate(args);
                fail("Cannot start DynaRun V3.exe");
            }
            g_pid = pi.dwProcessId;
            // Without the early hook the dll is attached once the first window shows up.
            ResumeThread(pi.hThread);
            early_hook(dll, g_pid, pi.dwThreadId, pi.hProcess);
            WaitForInputIdle(pi.hProcess, 30000);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
    }
    for (i = 0; i < 600 && !g_tid; i++) { EnumWindows(findwin, 0); if (!g_tid) Sleep(100); }
    if (!g_tid) fail("DynaRun window not found");
    llog("launcher: DynaRun pid=%u gui-thread=%u\r\n", g_pid, g_tid);

    wsprintfA(ev, "Local\\dynafix_ready_%u", g_pid);
    e = CreateEventA(NULL, TRUE, FALSE, ev);
    if (WaitForSingleObject(e, 0) == WAIT_OBJECT_0) ExitProcess(0);   // fix already active in this instance
    hd = LoadLibraryA(dll);
    if (!hd) fail("Cannot load dynafix.dll");
    hk = SetWindowsHookExA(WH_CALLWNDPROC, (HOOKPROC)GetProcAddress(hd, "CwpProc"), hd, g_tid);
    if (!hk) fail("SetWindowsHookEx failed");
    for (i = 0; i < 100; i++) {           // keep nudging the GUI thread until the DLL reports in
        // WH_CALLWNDPROC runs (and loads the dll) only for sent messages; a quiet window, such as the first-run
        // system selection, may not get any for a long time, so send one
        SendMessageTimeoutA(g_wnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, NULL);
        PostThreadMessageA(g_tid, WM_NULL, 0, 0);
        EnumWindows(findwin, 0);
        if (WaitForSingleObject(e, 100) == WAIT_OBJECT_0) break;
    }
    if (i == 100) llog("launcher: dynafix.dll did not report in (hook thread %u, error %u)\r\n", g_tid, GetLastError());
    UnhookWindowsHookEx(hk);
    ExitProcess(0);
}
