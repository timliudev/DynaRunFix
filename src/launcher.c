// DynaRunFix.exe - starts DynaRun V3 (or attaches to a running one) and loads dynafix.dll into it.
// Usage: DynaRunFix.exe ["path\to\DynaRun V3.exe"]
#include <windows.h>
#include <tlhelp32.h>

#define DEFAULT_EXE "C:\\Program Files (x86)\\Dyna Pro Dynamometers\\DynaRun V3.exe"
#define LE_PROFILE  "7e3c1d2a-5b4f-4c6e-9a8d-1f2e3d4c5b6a"   // zh-TW profile in le\LEConfig.xml

static DWORD g_pid, g_tid;

static BOOL CALLBACK findwin(HWND h, LPARAM l)
{
    DWORD pid, tid = GetWindowThreadProcessId(h, &pid);
    if (pid == g_pid) { g_tid = tid; return FALSE; }
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

void WinMainCRTStartup(void)
{
    char exe[MAX_PATH], dir[MAX_PATH], dll[MAX_PATH], le[MAX_PATH], line[3 * MAX_PATH], ev[64], *p, *cmd, *args;
    STARTUPINFOA si; PROCESS_INFORMATION pi; HMODULE hd; HHOOK hk; HANDLE e, h; HKEY k; DWORD n = MAX_PATH; int i;

    // dynafix.dll lives next to this launcher
    GetModuleFileNameA(NULL, dll, MAX_PATH); p = dll + lstrlenA(dll); while (p > dll && *p != '\\') p--; lstrcpyA(p, "\\dynafix.dll");

    cmd = GetCommandLineA();
    if (*cmd == '"') { cmd++; while (*cmd && *cmd != '"') cmd++; if (*cmd) cmd++; } else while (*cmd && *cmd != ' ') cmd++;
    while (*cmd == ' ') cmd++;
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
        if (GetACP() == CP_UTF8 && !lstrcmpA(ev, "950") && GetFileAttributesA(le) != INVALID_FILE_ATTRIBUTES) {
            // LEProc cannot raise DynaRun itself; elevate first if DynaRun is set to run as administrator.
            if (runasadmin(exe)) elevate(args);
            // dynafix swaps Arial/MingLiU for this face (inherited by DynaRun through LEProc)
            if (!GetEnvironmentVariableA("DYNAFIX_FONT", NULL, 0)) SetEnvironmentVariableA("DYNAFIX_FONT", "Microsoft JhengHei UI");
            wsprintfA(line, "\"%s\" -runas " LE_PROFILE " \"%s\"", le, exe);
            if (!CreateProcessA(le, line, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) fail("Cannot start Locale Emulator");
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            for (i = 0; i < 600 && !(g_pid = running()); i++) Sleep(50);
            if (!g_pid) fail("DynaRun V3.exe did not start under Locale Emulator");
        } else {
            if (!CreateProcessA(exe, NULL, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
                if (GetLastError() == ERROR_ELEVATION_REQUIRED) elevate(args);
                fail("Cannot start DynaRun V3.exe");
            }
            g_pid = pi.dwProcessId;
            WaitForInputIdle(pi.hProcess, 30000);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
    }
    for (i = 0; i < 600 && !g_tid; i++) { EnumWindows(findwin, 0); if (!g_tid) Sleep(100); }
    if (!g_tid) fail("DynaRun window not found");

    wsprintfA(ev, "Local\\dynafix_ready_%u", g_pid);
    e = CreateEventA(NULL, TRUE, FALSE, ev);
    if (WaitForSingleObject(e, 0) == WAIT_OBJECT_0) ExitProcess(0);   // fix already active in this instance
    hd = LoadLibraryA(dll);
    if (!hd) fail("Cannot load dynafix.dll");
    hk = SetWindowsHookExA(WH_CALLWNDPROC, (HOOKPROC)GetProcAddress(hd, "CwpProc"), hd, g_tid);
    if (!hk) fail("SetWindowsHookEx failed");
    for (i = 0; i < 100; i++) {           // keep nudging the GUI thread until the DLL reports in
        PostThreadMessageA(g_tid, WM_NULL, 0, 0);
        EnumWindows(findwin, 0);
        if (WaitForSingleObject(e, 100) == WAIT_OBJECT_0) break;
    }
    UnhookWindowsHookEx(hk);
    ExitProcess(0);
}
