// DynaRunFix.exe - starts DynaRun V3 (or attaches to a running one) and loads dynafix.dll into it.
// Usage: DynaRunFix.exe ["path\to\DynaRun V3.exe"]
#include <windows.h>
#include <tlhelp32.h>
#include "applog.h"
#include "version.h"

#define DEFAULT_EXE "C:\\Program Files (x86)\\Dyna Pro Dynamometers\\DynaRun V3.exe"
#define LE_PROFILE  "7e3c1d2a-5b4f-4c6e-9a8d-1f2e3d4c5b6a"   // zh-TW profile in le\LEConfig.xml

static DWORD g_pid, g_tid, g_first;
static BOOL g_update;   // this run started DynaRun and the update may be offered (see update_after_start)
static HWND g_wnd;

// TRUE if s starts with pre (case-insensitive, never reads past the end of s)
static BOOL starts(const char *s, const char *pre)
{
    for (; *pre; s++, pre++) {
        char a = *s, b = *pre;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return FALSE;   // also when s ended (a == 0)
    }
    return TRUE;
}

// "/name" as a whole first token of *cmd (case-insensitive): skipped, with the spaces after it.
static BOOL prefix(char **cmd, const char *name)
{
    char *c = *cmd; int n = lstrlenA(name);
    if (!starts(c, name) || (c[n] && c[n] != ' ' && c[n] != '\t')) return FALSE;
    for (c += n; *c == ' ' || *c == '\t'; c++);
    *cmd = c;
    return TRUE;
}
static BOOL flag(char **cmd, const char *name)
{
    return prefix(cmd, name);
}

// Picks DynaRun's VB main form among the top-level windows of g_pid; the GUI thread is the one that owns it.
// Other windows of the process (IME, helper or hidden windows) may belong to other threads. Ranking:
// visible ThunderRT6Form* (4), hidden ThunderRT6Form* (3), other ThunderRT6* (2), any other window (1).
static int g_best;
static char g_cls[64];
static BOOL CALLBACK findwin(HWND h, LPARAM l)
{
    DWORD pid, tid = GetWindowThreadProcessId(h, &pid); char cls[64]; int score;
    if (pid != g_pid || !tid) return TRUE;
    if (!GetClassNameA(h, cls, sizeof(cls))) cls[0] = 0;
    if (starts(cls, "ThunderRT6Form")) score = IsWindowVisible(h) ? 4 : 3;
    else score = starts(cls, "ThunderRT6") ? 2 : 1;
    if (score > g_best) { g_best = score; g_tid = tid; g_wnd = h; lstrcpyA(g_cls, cls); }
    return TRUE;
}

// Does a process belong to our session? have/mine: our own session id and whether it could be read; got/sid: the same for the process.
// A process whose session cannot be told does not count.
static BOOL in_session(BOOL have, DWORD mine, BOOL got, DWORD sid) { return !have || (got && sid == mine); }

// The DynaRun of this sign-in session (another user's DynaRun, e.g. after fast user switching, is not ours to attach to and does not count).
static DWORD running(void)
{
    PROCESSENTRY32 pe; HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); DWORD pid = 0, mine = 0, sid = 0;
    BOOL have = ProcessIdToSessionId(GetCurrentProcessId(), &mine), got;
    pe.dwSize = sizeof(pe);
    if (Process32First(s, &pe)) do {
        if (!lstrcmpiA(pe.szExeFile, "DynaRun V3.exe")) {
            got = ProcessIdToSessionId(pe.th32ProcessID, &sid);
            if (in_session(have, mine, got, sid)) pid = pe.th32ProcessID;
        }
    } while (Process32Next(s, &pe));
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

// GetTickCount64 where it exists (Vista+), else GetTickCount extended over its 49-day wrap (one thread only).
// Every wait of the launcher is a deadline on this clock, never a count of Sleep(1) (which lasts ~15 ms on a default timer).
typedef ULONGLONG (WINAPI *gtc64_t)(void);
static ULONGLONG tick64(void)
{
    static gtc64_t f; static BOOL init; static DWORD last, hi; DWORD t;
    if (!init) { f = (gtc64_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetTickCount64"); init = TRUE; }
    if (f) return f();
    t = GetTickCount(); if (t < last) hi++; last = t;
    return ((ULONGLONG)hi << 32) | t;
}

// Local\dynafix_ready_<pid>: the dll signals it when it is in place in DynaRun (explicit DACL, see make_sa in applog.h)
static HANDLE ready_event(DWORD pid)
{
    char ev[64]; SECURITY_ATTRIBUTES sa; PSECURITY_DESCRIPTOR sd; HANDLE e; BOOL have;
    wsprintfA(ev, "Local\\dynafix_ready_%u", pid);
    have = make_sa(&sa, &sd);
    e = CreateEventA(have ? &sa : NULL, TRUE, FALSE, ev);
    if (sd) LocalFree(sd);
    return e;
}

static BOOL g_elevated;    // this launcher process runs with an elevated token (normally only on the way to the relaunch; also true in the
                           // relaunched copy when the desktop shell itself is elevated)

// %TEMP%\dynafix.log into path (MAX_PATH). FALSE when TEMP is too long for that (or unusable): then nothing is logged.
static BOOL log_path(char *path)
{
    DWORD n = GetEnvironmentVariableA("TEMP", path, MAX_PATH - 16);
    if (!n) n = GetTempPathA(MAX_PATH - 16, path);          // TEMP is not set
    if (!n || n >= MAX_PATH - 16) return FALSE;              // failed, or the value did not fit (n = the size it needs)
    lstrcatA(path, "\\dynafix.log");
    return TRUE;
}

// Opens the log (log_open, applog.h); an elevated process also refuses a file with several hard links.
static HANDLE open_log(const char *path, DWORD access, DWORD disp) { return log_open(path, access, disp, g_elevated); }

// above 10 MB, dynafix.log is cut to its newest 8 MB, from a full line on (dynafix.dll does the same; whichever runs first).
// Never when elevated.
static void trim_log(void)
{
    HANDLE h, m; DWORD size, n, i; char *buf;
    char path[MAX_PATH];
    if (g_elevated || !log_path(path)) return;
    h = open_log(path, GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING);
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
// one line in %TEMP%\dynafix.log, next to what dynafix.dll writes from inside DynaRun
static void llog(const char *fmt, DWORD a, DWORD b)
{
    char path[MAX_PATH], line[LOG_MAX + 100]; DWORD n; SYSTEMTIME t;
    if (!log_path(path)) return;
    GetLocalTime(&t);
    n = wsprintfA(line, "%04u-%02u-%02u %02u:%02u:%02u.%03u ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    wsprintfA(line + n, fmt, a, b);
    n = lstrlenA(line);
    if (n > LOG_MAX) { lstrcpyA(line + LOG_MAX - 5, "...\r\n"); n = LOG_MAX; }
    log_append(path, line, n, g_elevated);   // one WriteFile per line: appended atomically
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
    { char *c = (char *)cmd; while (prefix(&c, VERB_NOUPDATE)); if (prefix(&c, VERB_AUTOSTART)) how = VERB_AUTOSTART; else if (prefix(&c, VERB_RESTART)) how = VERB_RESTART; }
    lstrcpynA(args, cmd, sizeof(args));
    { volatile char *z = (volatile char *)&st; int i; for (i = 0; i < (int)sizeof(st); i++) z[i] = 0; } st.cb = sizeof(st); GetStartupInfoA(&st);
    if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) lstrcpyA(cwd, "?");
    wsprintfA(msg, "launcher: started pid=%u, DynaRunFix %s (%s), %s, args [%s], parent %s (pid %u), startup flags %08X show %u, cwd [%s]\r\n", me, DRF_DISPLAY, DRF_COMMIT, how, args, parent, pp, st.dwFlags, st.wShowWindow, cwd);
    llog("%s", (DWORD)(UINT_PTR)msg, 0);
}

static void fail(const char *msg) { MessageBoxA(NULL, msg, "DynaRunFix", MB_ICONERROR); ExitProcess(1); }

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

// DynaRun never needs administrator rights, so it is never started with more rights than a normal start from the desktop.
// - If this launcher is itself elevated (the user chose "Run as administrator" on our shortcut) it does nothing but start a
//   copy of itself with the token of the desktop shell (Explorer: the user's own, normally non-elevated token) through
//   CreateProcessWithTokenW, with the same arguments plus "/wasadmin", and exit. If the shell token is itself elevated (UAC
//   off, built-in Administrator: the normal desktop is already full administrator) that is what a double-click would give
//   too, so the copy carries on with it (and logs that). Without any shell token it says so and exits: nothing else of ours is started.
// - The compatibility layer RUNASADMIN is ignored: "RunAsInvoker" is added to __COMPAT_LAYER in the environment the new
//   process is created with (appended to what is there; dynafix.dll takes it out again when it starts).
static HANDLE g_notice;    // the one-time notice (thread), waited for in finish()
static int g_notice_kind;  // 1 = launcher was started as administrator, 2 = RUNASADMIN compatibility layer set

typedef BOOL (WINAPI *cpwt_t)(HANDLE, DWORD, LPCWSTR, LPWSTR, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
typedef BOOL (WINAPI *ceb_t)(LPVOID *, HANDLE, BOOL);
typedef BOOL (WINAPI *deb_t)(LPVOID);

// Is the token elevated (TokenElevation, Vista+)? 1 yes, 0 no (also where the query does not exist: XP has no split token),
// -1 the query failed and the answer is not known.
static int token_state(HANDLE t)
{
    DWORD el = 0, n = 0;
    if (GetTokenInformation(t, (TOKEN_INFORMATION_CLASS)20, &el, sizeof(el), &n)) return el != 0;
#pragma warning(suppress: 4996)
    return GetLastError() == ERROR_INVALID_PARAMETER && (BYTE)GetVersion() < 6 ? 0 : -1;
}

// TRUE if the token is elevated; an unknown state counts as elevated (fail closed: the caller then does not carry on as if it were a normal start).
static BOOL token_elevated(HANDLE t) { return token_state(t) != 0; }

// The shell's token as a primary token (the user's non-elevated one under UAC), NULL if it cannot be had.
static HANDLE shell_token(void)
{
    HWND w = GetShellWindow(); DWORD pid = 0; HANDLE hp, ht = NULL, tp = NULL;
    if (!w) { llog("launcher: no desktop shell window\r\n", 0, 0); return NULL; }
    GetWindowThreadProcessId(w, &pid);
    hp = pid ? OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, FALSE, pid) : NULL;
    if (!hp) { llog("launcher: cannot open the shell process (error %u)\r\n", GetLastError(), 0); return NULL; }
    if (OpenProcessToken(hp, TOKEN_DUPLICATE | TOKEN_QUERY, &ht)) {
        if (!DuplicateTokenEx(ht, TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                              NULL, SecurityImpersonation, TokenPrimary, &tp)) tp = NULL;
        CloseHandle(ht);
    }
    if (!tp) llog("launcher: cannot get the shell token (error %u)\r\n", GetLastError(), 0);
    CloseHandle(hp);
    return tp;
}

// TRUE if s starts with pre (case-insensitive, ASCII; never reads past the end of s)
static BOOL wstarts(const WCHAR *s, const WCHAR *pre)
{
    for (; *pre; s++, pre++) {
        WCHAR a = *s, b = *pre;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return FALSE;
    }
    return TRUE;
}

// old (may be NULL) with the layer "RunAsInvoker" appended (not again if it is there as a word). Free with HeapFree.
static WCHAR *with_invoker(const WCHAR *old)
{
    static const WCHAR add[] = L"RunAsInvoker";
    size_t n = old ? lstrlenW(old) : 0, la = sizeof(add) / sizeof(WCHAR) - 1, i; WCHAR *r;
    r = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (n + la + 2) * sizeof(WCHAR));
    if (!r) return NULL;
    if (n) lstrcpyW(r, old); else r[0] = 0;
    for (i = 0; i + la <= n; i++)
        if ((i == 0 || r[i - 1] == ' ') && (i + la == n || r[i + la] == ' ') && wstarts(r + i, add)) return r;
    if (n) r[n++] = ' ';
    lstrcpyW(r + n, add);
    return r;
}

// Environment block of the token's user (free with HeapFree), NULL on failure, for the relaunch. CreateEnvironmentBlock builds it
// from the registry, so: __COMPAT_LAYER = what the block has + RunAsInvoker, and the DYNAFIX_* variables of this process
// (DYNAFIX_NOUPDATE, DYNAFIX_CHARSET, ...) replace those of the block.
static WCHAR *token_env(HANDLE tok)
{
    static const WCHAR cl[] = L"__COMPAT_LAYER=", dy[] = L"DYNAFIX_";
    char sys[MAX_PATH + 16]; HMODULE ue; ceb_t ceb; deb_t deb;
    LPVOID env = NULL; WCHAR *src, *cur, *r = NULL, *inv; const WCHAR *old = NULL; size_t n = 0, nc = 0, o, m, w;
    // by full path: not a userenv.dll of the current folder or the search path
    n = GetSystemDirectoryA(sys, MAX_PATH);
    if (!n || n >= MAX_PATH) return NULL;
    lstrcatA(sys, "\\userenv.dll");
    ue = LoadLibraryA(sys); ceb = ue ? (ceb_t)GetProcAddress(ue, "CreateEnvironmentBlock") : NULL;
    deb = ue ? (deb_t)GetProcAddress(ue, "DestroyEnvironmentBlock") : NULL;
    if (!ceb || !deb || !ceb(&env, tok, FALSE)) return NULL;
    src = (WCHAR *)env; n = 0;
    while (src[n]) n += lstrlenW(src + n) + 1;      // up to the closing empty string
    cur = GetEnvironmentStringsW();
    if (cur) while (cur[nc]) nc += lstrlenW(cur + nc) + 1;
    for (o = 0; src[o]; o += lstrlenW(src + o) + 1) if (wstarts(src + o, cl)) old = src + o + lstrlenW(cl);
    inv = with_invoker(old);
    if (inv && (r = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (n + nc + lstrlenW(cl) + lstrlenW(inv) + 2) * sizeof(WCHAR)))) {
        lstrcpyW(r, cl); lstrcatW(r, inv); w = lstrlenW(r) + 1;
        for (o = 0; src[o]; o += m + 1) {           // the block's own entries, but not the layer and not DYNAFIX_*
            m = lstrlenW(src + o);
            if (!wstarts(src + o, cl) && !wstarts(src + o, dy)) { lstrcpyW(r + w, src + o); w += m + 1; }
        }
        for (o = 0; cur && cur[o]; o += m + 1) {    // DYNAFIX_* of this process
            m = lstrlenW(cur + o);
            if (wstarts(cur + o, dy)) { lstrcpyW(r + w, cur + o); w += m + 1; }
        }
        r[w] = 0;
    }
    if (inv) HeapFree(GetProcessHeap(), 0, inv);
    if (cur) FreeEnvironmentStringsW(cur);
    deb(env);
    return r;
}

static void fail_w(const WCHAR *en, const WCHAR *zh)
{
    MessageBoxW(NULL, (GetUserDefaultUILanguage() & 0xFF) == LANG_CHINESE ? zh : en, L"DynaRunFix", MB_ICONERROR | MB_SETFOREGROUND);
    ExitProcess(1);
}

// This launcher is elevated: start it again, same arguments plus "/wasadmin", with the shell's token. Returns 0 when that
// cannot be done (the caller reports it; nothing else has been started), 1 when the copy has been started (this process
// ends), 2 when the shell token is itself elevated (UAC off, built-in Administrator: a normal start is already full
// administrator): nothing to drop, this process carries on.
static int relaunch_non_elevated(void)
{
    cpwt_t cpwt = (cpwt_t)GetProcAddress(GetModuleHandleA("advapi32.dll"), "CreateProcessWithTokenW");
    WCHAR self[MAX_PATH], *rest = GetCommandLineW(), *cmd, *env; DWORD n; HANDLE tok; STARTUPINFOW sw; PROCESS_INFORMATION pi; BOOL ok; int i;
    n = GetModuleFileNameW(NULL, self, MAX_PATH);
    if (!n || n >= MAX_PATH) { llog("launcher: elevated, and the own path is not usable\r\n", 0, 0); return 0; }
    if (!(tok = shell_token())) return 0;
    i = token_state(tok);
    if (i < 0) {   // the shell token cannot be classified: nothing is started with it, nor carried on with
        CloseHandle(tok);
        llog("launcher: the elevation state of the desktop shell token cannot be told (error %u); not starting anything\r\n", GetLastError(), 0);
        return 0;
    }
    if (i) {
        CloseHandle(tok);
        llog("launcher: the desktop shell token is elevated (UAC off, or the desktop runs as administrator); carrying on in this process, as a normal start would\r\n", 0, 0);
        return 2;
    }
    if (!cpwt) { CloseHandle(tok); llog("launcher: elevated, and CreateProcessWithTokenW is not available\r\n", 0, 0); return 0; }
    {   // CreateProcessWithTokenW needs SeImpersonatePrivilege enabled (an elevated administrator holds it; it is normally on already)
        HANDLE me; TOKEN_PRIVILEGES tp;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &me)) {
            tp.PrivilegeCount = 1; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            if (LookupPrivilegeValueA(NULL, "SeImpersonatePrivilege", &tp.Privileges[0].Luid)) AdjustTokenPrivileges(me, FALSE, &tp, 0, NULL, NULL);
            CloseHandle(me);
        }
    }
    if (*rest == '"') { rest++; while (*rest && *rest != '"') rest++; if (*rest) rest++; } else while (*rest && *rest != ' ' && *rest != '\t') rest++;
    while (*rest == ' ' || *rest == '\t') rest++;
    cmd = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (lstrlenW(self) + lstrlenW(rest) + 24) * sizeof(WCHAR));
    if (!cmd) { CloseHandle(tok); return 0; }
    lstrcpyW(cmd, L"\""); lstrcatW(cmd, self); lstrcatW(cmd, L"\" " WIDEN(VERB_WASADMIN) L" "); lstrcatW(cmd, rest);
    env = token_env(tok);
    { volatile char *z = (volatile char *)&sw; for (i = 0; i < (int)sizeof(sw); i++) z[i] = 0; } sw.cb = sizeof(sw);
    ok = cpwt(tok, 0, self, cmd, env ? CREATE_UNICODE_ENVIRONMENT : 0, env, NULL, &sw, &pi);
    llog("launcher: elevated; started itself with the desktop shell's token: %u (error %u)\r\n", ok, ok ? 0 : GetLastError());
    if (ok) { AllowSetForegroundWindow(pi.dwProcessId); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    if (env) HeapFree(GetProcessHeap(), 0, env);
    HeapFree(GetProcessHeap(), 0, cmd);
    CloseHandle(tok);
    return ok ? 1 : 0;
}

// CreateProcess for DynaRun (or LEProc), with RunAsInvoker added to __COMPAT_LAYER (the launcher's own environment is changed for
// the call and put back). exe may be NULL (command line only).
static BOOL spawn(const char *exe, char *cmdline, const char *dir, DWORD flags, PROCESS_INFORMATION *pi)
{
    STARTUPINFOA si; BOOL ok; WCHAR *old = NULL, *nw; DWORD olen, err; int i;
    { volatile char *z = (volatile char *)&si; for (i = 0; i < (int)sizeof(si); i++) z[i] = 0; } si.cb = sizeof(si);
    olen = GetEnvironmentVariableW(L"__COMPAT_LAYER", NULL, 0);     // size with the terminator, 0 = not set
    if (olen && (old = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, olen * sizeof(WCHAR)))) {
        if (!GetEnvironmentVariableW(L"__COMPAT_LAYER", old, olen)) { HeapFree(GetProcessHeap(), 0, old); old = NULL; }
    }
    nw = with_invoker(old);
    SetEnvironmentVariableW(L"__COMPAT_LAYER", nw ? nw : L"RunAsInvoker");
    ok = CreateProcessA(exe, cmdline, NULL, NULL, FALSE, flags, NULL, dir, &si, pi);
    err = GetLastError();
    SetEnvironmentVariableW(L"__COMPAT_LAYER", old);                // NULL: removed again
    if (nw) HeapFree(GetProcessHeap(), 0, nw);
    if (old) HeapFree(GetProcessHeap(), 0, old);
    SetLastError(err);
    return ok;
}

// One-time notice (EN / zh-TW), in its own thread so the start is not held up. Answering "No" to "Show this notice
// again next time?" writes HKCU\Software\DynaRunFix\<value> = 1, which suppresses it.
static const char *notice_value(void) { return g_notice_kind == 1 ? "NoticeElevatedLauncher" : "NoticeRunAsAdminLayer"; }
static DWORD WINAPI notice_thread(LPVOID unused)
{
    BOOL zh = (GetUserDefaultUILanguage() & 0xFF) == LANG_CHINESE; const WCHAR *head, *body, *ask; HKEY k; WCHAR msg[1024];
    (void)unused;
    if (g_notice_kind == 1)
        head = zh ? L"DynaRunFix 是以系統管理員身分啟動的。\n\n" : L"DynaRunFix was started as administrator.\n\n";
    else
        head = zh ? L"DynaRun V3.exe 設定了相容性「以系統管理員身分執行這個程式」，DynaRunFix 會忽略它。\n\n"
                  : L"The compatibility setting \"Run as administrator\" is set for DynaRun V3.exe; DynaRunFix ignores it.\n\n";
    body = zh ? L"DynaRun 不需要系統管理員權限，因此一律以一般權限啟動。\n\n"
                L"請勿在沒有 DynaRunFix 的情況下，直接以系統管理員身分執行「DynaRun V3.exe」：這可能損壞 DynaRun 的使用者元件註冊。\n\n"
              : L"DynaRun does not need administrator rights, so it is started normally (not elevated).\n\n"
                L"Do not run \"DynaRun V3.exe\" directly as administrator without DynaRunFix: that can damage DynaRun's per-user component registrations.\n\n";
    ask = zh ? L"下次還要顯示這個通知嗎？" : L"Show this notice again next time?";
    lstrcpyW(msg, head); lstrcatW(msg, body); lstrcatW(msg, ask);
    if (MessageBoxW(NULL, msg, L"DynaRunFix", MB_YESNO | MB_DEFBUTTON2 | MB_ICONINFORMATION | MB_SETFOREGROUND) == IDNO &&
        !RegCreateKeyExA(HKEY_CURRENT_USER, APP_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL)) {
        DWORD one = 1; RegSetValueExA(k, notice_value(), 0, REG_DWORD, (const BYTE *)&one, 4); RegCloseKey(k);
    }
    return 0;
}
static void show_notice(int kind)
{
    HKEY k; DWORD v = 0, n = 4, id;
    g_notice_kind = kind;
    if (!RegOpenKeyExA(HKEY_CURRENT_USER, APP_KEY, 0, KEY_QUERY_VALUE, &k)) {
        if (RegQueryValueExA(k, notice_value(), NULL, NULL, (BYTE *)&v, &n)) v = 0;
        RegCloseKey(k);
    }
    if (v) return;
    g_notice = CreateThread(NULL, 0, notice_thread, NULL, 0, &id);
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
// hd is dynafix.dll, loaded by the caller BEFORE DynaRun runs: the first load of a newly installed dll can take
// long, and DynaRun went on meanwhile and laid its main screen out for the full screen height before dynafix
// was there (seen only right after an install, or at the first sign-in after one).
// Exits the launcher (finish) once the dll has reported in; returns if that did not happen.
static void finish(void);
static void early_hook(HMODULE hd, DWORD pid, DWORD tid, HANDLE proc)
{
    HANDLE e, hs[2]; HHOOK hk = NULL; DWORD t0 = GetTickCount(), th; ULONGLONG dl = tick64() + 5000;
    HOOKPROC cwp = hd ? (HOOKPROC)GetProcAddress(hd, "CwpProc") : NULL;
    e = ready_event(pid);
    while (cwp && !(hk = SetWindowsHookExA(WH_CALLWNDPROC, cwp, hd, tid)) && tick64() < dl) Sleep(1);   // until the thread has a queue
    if (!hk) { llog("launcher: early hook failed (error %u) %u\r\n", GetLastError(), 0); if (e) CloseHandle(e); if (hd) FreeLibrary(hd); return; }
    th = GetTickCount() - t0;
    // the dll reports in (and keeps itself loaded) when DynaRun's first window gets a message
    hs[0] = e; hs[1] = proc;
    if (WaitForMultipleObjects(2, hs, FALSE, 60000) == WAIT_OBJECT_0) {
        llog("launcher: DynaRun pid=%u hooked from start (thread %u)\r\n", pid, tid);
        llog("launcher: hook set %u ms after DynaRun was resumed, dll reported in after %u ms\r\n", th, GetTickCount() - t0);
        UnhookWindowsHookEx(hk);
        CloseHandle(e);
        finish();
    }
    UnhookWindowsHookEx(hk);
    llog("launcher: early hook did not report in %u %u\r\n", 0, 0);
    if (e) CloseHandle(e);          // the caller loads the dll again for the fallback hook
    if (hd) FreeLibrary(hd);
}

static ULONGLONG reg_time(HKEY k, const char *name)
{
    ULONGLONG t = 0; DWORD n = sizeof(t), type;
    if (RegQueryValueExA(k, name, NULL, &type, (BYTE *)&t, &n) || type != REG_QWORD) t = 0;
    return t;
}

// "DynaRunFix-Setup.exe <what> <args>" next to this launcher (the installed copy), waiting up to wait ms for it:
// 0 when it is not there or did not start, 1 still running after wait, 2 ended.
// Every start is logged first, with the arguments and why (why: a short reason).
static int run_setup(const char *what, const char *args, DWORD wait, const char *why)
{
    char setup[MAX_PATH], line[4 * MAX_PATH], *p; STARTUPINFOA si; PROCESS_INFORMATION pi; int i, r;
    GetModuleFileNameA(NULL, setup, MAX_PATH); p = setup + lstrlenA(setup); while (p > setup && *p != '\\') p--; lstrcpyA(p, "\\DynaRunFix-Setup.exe");
    wsprintfA(line, "\"%s\" %s %s", setup, what, args);
    llog("launcher: run_setup: %s -- reason: %s\r\n", (DWORD_PTR)line, (DWORD_PTR)why);
    if (GetFileAttributesA(setup) == INVALID_FILE_ATTRIBUTES) return 0;
    { volatile char *z = (volatile char *)&si; for (i = 0; i < (int)sizeof(si); i++) z[i] = 0; } si.cb = sizeof(si);
    if (!CreateProcessA(setup, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 0;
    AllowSetForegroundWindow(pi.dwProcessId);
    r = WaitForSingleObject(pi.hProcess, wait) == WAIT_OBJECT_0 ? 2 : 1;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return r;
}

static BOOL env_on(const char *name)
{
    char v[4];
    return GetEnvironmentVariableA(name, v, sizeof(v)) && v[0] == '1';
}

// Once a day: look for a newer release (DynaRunFix-Setup.exe /checkupdate, in the background: the answer is noted in
// the registry and offered at a later start).
static void check_update(void)
{
    HKEY k; ULONGLONG t = 0, n = now();
    if (!RegOpenKeyExA(HKEY_CURRENT_USER, APP_KEY, 0, KEY_QUERY_VALUE, &k)) { t = reg_time(k, RV_UPDATE_CHECKED); RegCloseKey(k); }
    if (t <= n && n - t < DAY) return;
    llog("launcher: looking for an update (0 no setup, 1 started): %u %u\r\n", run_setup(VERB_CHECKUPDATE, "", 0, "the last check is more than a day old"), 0);
}

// DynaRun has been started by this launcher (and hooked): only now is an update dealt with, so that DynaRun never waits for
// it. A newer release that was noted (DynaRunFix-Setup.exe /checkupdate) is offered by "DynaRunFix-Setup.exe /offer", which
// runs on its own beside DynaRun (this launcher does not wait for it and does not exit early). The offer is made at most
// once a day (UpdateAsk is set here, whatever the answer) and never again for a version the user chose to skip (UpdateSkip).
static void update_after_start(void)
{
    char v[64], s[64]; DWORD n = sizeof(v), m = sizeof(s), type; HKEY k; BOOL has, skipped; ULONGLONG ask, next, t = now();
    if (env_on("DYNAFIX_NOUPDATE")) { llog("launcher: update check skipped (DYNAFIX_NOUPDATE) %u %u\r\n", 0, 0); return; }
    if (RegOpenKeyExA(HKEY_CURRENT_USER, APP_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k)) { check_update(); return; }
    has = !RegQueryValueExA(k, RV_UPDATE_VERSION, NULL, &type, (BYTE *)v, &n) && type == REG_SZ && n > 1;
    skipped = has && !RegQueryValueExA(k, RV_UPDATE_SKIP, NULL, &type, (BYTE *)s, &m) && type == REG_SZ && !lstrcmpA(s, v);
    ask = reg_time(k, RV_UPDATE_ASK);
    if (has && !skipped && t >= ask) {
        next = t + DAY;
        RegSetValueExA(k, RV_UPDATE_ASK, 0, REG_QWORD, (const BYTE *)&next, 8);   // not again today
        RegCloseKey(k);
        llog("launcher: offering the update to %s after DynaRun was started %u\r\n", (DWORD_PTR)v, 0);
        run_setup(VERB_OFFER, "", 0, "a newer release is noted, not skipped, not asked today; DynaRun is already running");
        return;
    }
    RegCloseKey(k);
    if (has) llog("launcher: noted update %s not offered (%s)\r\n", (DWORD_PTR)v, (DWORD_PTR)(skipped ? "skipped by the user" : "already asked today"));
    check_update();
}

// the launcher is done: DynaRun runs with dynafix.dll
static void finish(void)
{
    if (g_notice) WaitForSingleObject(g_notice, INFINITE);   // the one-time notice stays until it is answered
    if (g_update) update_after_start();
    ExitProcess(0);
}
// Waits until Shell_TrayWnd exists and the screen size and work area have not changed for 3 s (polled every 250 ms);
// starts anyway after 60 s.
static void wait_desktop(void)
{
    DWORD t0 = GetTickCount(), still = t0, tk; int sx = 0, sy = 0, x, y; RECT wa, w0 = { 0, 0, 0, 0 };
    for (;;) {
        tk = GetTickCount();
        wa.left = wa.top = wa.right = wa.bottom = 0;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
        x = GetSystemMetrics(SM_CXSCREEN); y = GetSystemMetrics(SM_CYSCREEN);
        if (x != sx || y != sy || !EqualRect(&wa, &w0)) { sx = x; sy = y; w0 = wa; still = tk; }
        if ((FindWindowA("Shell_TrayWnd", NULL) && tk - still >= 3000) || tk - t0 >= 60000) break;
        Sleep(250);
    }
    llog("launcher: autostart waited %u ms for the desktop (%u = gave up)\r\n", tk - t0, tk - t0 >= 60000);
}

// The DynaRun V3.exe the setup recorded (HKLM only: ordinary users cannot write there), DEFAULT_EXE when there is none or the value is not
// a usable string. size: the size of exe (MAX_PATH); a longer value does not fit and is not used (the default is, as for a missing one).
static void read_dynarun_exe(HKEY root, const char *sub, char *exe, DWORD size)
{
    HKEY k; DWORD ty = 0, n = size;                 // n: the size of THIS buffer, whatever an earlier call left in a variable
    lstrcpyA(exe, DEFAULT_EXE);
    if (RegOpenKeyExA(root, sub, 0, KEY_QUERY_VALUE, &k)) return;
    if (!RegQueryValueExA(k, "DynaRunExe", NULL, &ty, (BYTE *)exe, &n) && ty == REG_SZ && n && (n < size || !exe[size - 1])) {
        if (n < size) exe[n] = 0;                   // also when stored without a terminator
        if (!exe[0]) lstrcpyA(exe, DEFAULT_EXE);    // empty
    }
    else lstrcpyA(exe, DEFAULT_EXE);                // missing, not a string, or too long for the buffer (ERROR_MORE_DATA)
    RegCloseKey(k);
}

#ifndef DRF_NO_MAIN
// The launcher's arguments: the command line after the program name.
static char *own_args(void)
{
    char *cmd = GetCommandLineA();
    if (*cmd == '"') { cmd++; while (*cmd && *cmd != '"') cmd++; if (*cmd) cmd++; } else while (*cmd && *cmd != ' ') cmd++;
    while (*cmd == ' ') cmd++;
    return cmd;
}

// An elevated launcher starts nothing but its copy with the shell's token: no DynaRun, no LEProc, no updater. Sets g_elevated;
// returns only when this process carries on (not elevated, or the shell token is elevated too).
static void elevation_gate(BOOL wasadmin)
{
    HANDLE tk; int i, rl;
    g_elevated = !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tk) || (i = token_elevated(tk), CloseHandle(tk), i);
    if (!g_elevated) return;
    if (wasadmin) llog("launcher: /wasadmin ignored: this process is elevated, so it was not started by us with a non-elevated token\r\n", 0, 0);
    rl = relaunch_non_elevated();
    if (!rl)
        fail_w(L"DynaRunFix was started as administrator, but Windows did not give it the token of the desktop user, "
               L"so DynaRun is not started: DynaRunFix never runs DynaRun with more rights than a normal start from the desktop.\n\n"
               L"Possible reasons: the desktop (Explorer) is not running, or the \"Secondary Logon\" service is disabled.\n\n"
               L"Start DynaRunFix again in the normal way (not \"Run as administrator\").",
               L"DynaRunFix 是以系統管理員身分啟動的，但 Windows 沒有提供桌面使用者的權杖，"
               L"因此不會啟動 DynaRun：DynaRunFix 一律不會以高於「從桌面正常啟動」的權限執行 DynaRun。\n\n"
               L"可能原因：桌面（檔案總管）未執行，或「Secondary Logon」服務已停用。\n\n"
               L"請以一般方式（不要選「以系統管理員身分執行」）重新開啟 DynaRunFix。");
    if (rl == 1) ExitProcess(0);   // the copy carries on; rl == 2: the shell token is elevated too, carry on here
}

// The arguments after "/wasadmin" (already taken) and after log_start: "/noupdate"..., "/autostart", "/restart <pid>", then the
// DynaRun V3.exe to start (the recorded one when none is given) into exe, its folder into dir (both MAX_PATH). Sets g_update.
static void parse_args(char *cmd, char *exe, char *dir)
{
    char *p; BOOL ask = TRUE;
    while (flag(&cmd, VERB_NOUPDATE)) ask = FALSE;   // from the setup's /update: it has just asked
    // "/autostart" (the sign-in Run value): at sign-in the desktop is still being set up (taskbar, resolution, DPI) and
    // DynaRun lays its screen out from the size it sees at start: wait for the taskbar and 3 s of an unchanged size
    if (flag(&cmd, VERB_AUTOSTART)) {
        ask = FALSE;   // an unattended start (sign-in) never goes near the updater
        if (!running()) wait_desktop();
    }
    // "/restart <pid>" (from dynafix.dll after the first-time setup): wait for that DynaRun to end, then start normally
    if (prefix(&cmd, VERB_RESTART)) {
        DWORD old = 0; HANDLE h0;
        p = cmd;
        while (*p >= '0' && *p <= '9') old = old * 10 + (*p++ - '0');
        if (old && (h0 = OpenProcess(SYNCHRONIZE, FALSE, old))) {
            DWORD w = WaitForSingleObject(h0, 15000); CloseHandle(h0);
            if (w == WAIT_TIMEOUT) {    // the old DynaRun did not end: starting now would only attach to it, so do not start at all
                llog("launcher: /restart: DynaRun pid %u still runs after 15 s, not restarting\r\n", old, 0);
                ExitProcess(1);
            }
        }
        cmd = p; while (*cmd == ' ') cmd++;
        ask = FALSE;
    }
    // DynaRun always starts first; the update (check, offer) is dealt with after it runs (finish), only when it was started
    // here and neither "/noupdate", "/autostart" nor "/restart" was given
    g_update = ask;
    if (*cmd == '"') { cmd++; lstrcpynA(exe, cmd, MAX_PATH); p = exe; while (*p && *p != '"') p++; *p = 0; }
    else if (*cmd) lstrcpynA(exe, cmd, MAX_PATH);
    else {
        read_dynarun_exe(HKEY_LOCAL_MACHINE, "SOFTWARE\\DynaRunFix", exe, MAX_PATH);
    }
    lstrcpyA(dir, exe); p = dir + lstrlenA(dir); while (p > dir && *p != '\\') p--; *p = 0;
}

// With Windows' UTF-8 option on, DynaRun is started through Locale Emulator (le\LEProc.exe next to this launcher, profile
// LE_PROFILE in le\LEConfig.xml) so GDI-drawn labels use the zh-TW code page too. Only for a Traditional Chinese system locale
// (legacy code page 950): the LE profile is zh-TW. Other locales get just the manifest, which selects their own legacy code page.
// TRUE (le = its path, MAX_PATH) when DynaRun is to be started that way.
static BOOL use_le(const char *dll, char *le)
{
    char ev[64], *p;
    lstrcpyA(le, dll); p = le + lstrlenA(le); while (p > le && *p != '\\') p--; lstrcpyA(p, "\\le\\LEProc.exe");
    if (!GetLocaleInfoA(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, ev, sizeof(ev))) ev[0] = 0;
    // Also for a zh-TW system locale whose FontAssoc lacks ANSI(00)=YES (Windows installed in English,
    // locale changed later): LE gives DynaRun the Big5 code page and charset for every window, including
    // tooltips and OCX controls that dynafix's font hooks do not reach.
    return !lstrcmpA(ev, "950") && (GetACP() == CP_UTF8 || (GetACP() == 950 && !fontassoc())) &&
           GetFileAttributesA(le) != INVALID_FILE_ATTRIBUTES;
}

// DynaRun through Locale Emulator: sets g_pid, and hooks DynaRun early (early_hook) when its first thread is found in time.
static void start_le(HMODULE hd, const char *le, const char *exe, const char *dir)
{
    char line[3 * MAX_PATH]; PROCESS_INFORMATION pi; ULONGLONG dl;
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
        if (!spawn(le, line, dir, CREATE_SUSPENDED, &pi)) fail("Cannot start Locale Emulator");
        injob = injob && AssignProcessToJobObject(job, pi.hProcess);
        ResumeThread(pi.hThread);
        if (injob) {
            DWORD msg; ULONG_PTR key; LPOVERLAPPED ov; ULONGLONG t, jdl = tick64() + 30000;
            while (!g_pid && (t = tick64()) < jdl && GetQueuedCompletionStatus(port, &msg, &key, &ov, (DWORD)(jdl - t)))
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
    for (dl = tick64() + 30000; !g_pid && !(g_pid = running()) && tick64() < dl; ) Sleep(1);
    if (!g_pid) fail("DynaRun V3.exe did not start under Locale Emulator");
    {
        DWORD tid = g_first; HANDLE h0 = OpenProcess(SYNCHRONIZE, FALSE, g_pid);
        for (dl = tick64() + 3000; !tid && !(tid = first_thread(g_pid)) && tick64() < dl; ) Sleep(1);
        if (tid && h0) early_hook(hd, g_pid, tid, h0);
        if (h0) CloseHandle(h0);
    }
}

// DynaRun directly: sets g_pid and hooks DynaRun early (early_hook).
static void start_plain(HMODULE hd, const char *exe, const char *dir)
{
    PROCESS_INFORMATION pi;
    charset_env();
    if (!spawn(exe, NULL, dir, CREATE_SUSPENDED, &pi)) {
        if (GetLastError() == ERROR_ELEVATION_REQUIRED) llog("launcher: DynaRun V3.exe insists on elevation, not started\r\n", 0, 0);
        fail("Cannot start DynaRun V3.exe");
    }
    g_pid = pi.dwProcessId;
    // Without the early hook the dll is attached once the first window shows up.
    ResumeThread(pi.hThread);
    early_hook(hd, g_pid, pi.dwThreadId, pi.hProcess);
    WaitForInputIdle(pi.hProcess, 30000);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}

// dynafix.dll has not reported in from DynaRun g_pid (yet; an early hook that worked has finished already): find DynaRun's GUI
// thread, hook it with the dll at path dll, and finish.
static void hook_and_finish(const char *dll)
{
    char line[3 * MAX_PATH]; HMODULE hd; HHOOK hk; HANDLE e; BOOL reported; ULONGLONG dl;
    for (dl = tick64() + 60000; !g_tid; ) { g_best = 0; EnumWindows(findwin, 0); if (g_tid || tick64() >= dl) break; Sleep(100); }
    if (!g_tid) fail("DynaRun window not found");
    // just started: the VB main form may not exist yet (a splash or helper window comes first); wait a moment for it
    for (dl = tick64() + 2000; g_best < 4 && tick64() < dl; ) {
        DWORD t = g_tid; HWND w = g_wnd; int b = g_best; char c[64];
        lstrcpyA(c, g_cls); Sleep(100); g_best = 0; g_tid = 0; EnumWindows(findwin, 0);
        if (!g_tid) { g_tid = t; g_wnd = w; g_best = b; lstrcpyA(g_cls, c); }
    }
    llog("launcher: DynaRun pid=%u gui-thread=%u\r\n", g_pid, g_tid);
    wsprintfA(line, "launcher: gui-thread chosen from window class [%s], rank %u (4 = visible VB form, lower = fallback)\r\n", g_cls, g_best);
    llog("%s", (DWORD)(UINT_PTR)line, 0);

    e = ready_event(g_pid);
    if (e && WaitForSingleObject(e, 0) == WAIT_OBJECT_0) finish();   // fix already active in this instance
    hd = LoadLibraryA(dll);
    if (!hd) fail("Cannot load dynafix.dll");
    hk = SetWindowsHookExA(WH_CALLWNDPROC, (HOOKPROC)GetProcAddress(hd, "CwpProc"), hd, g_tid);
    if (!hk) fail("SetWindowsHookEx failed");
    for (reported = FALSE, dl = tick64() + 10000; tick64() < dl; ) {   // keep nudging the GUI thread until the DLL reports in
        // WH_CALLWNDPROC runs (and loads the dll) only for sent messages; a quiet window, such as the first-run
        // system selection, may not get any for a long time, so send one
        SendMessageTimeoutA(g_wnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, NULL);
        PostThreadMessageA(g_tid, WM_NULL, 0, 0);
        if (!IsWindow(g_wnd)) { DWORD keep = g_tid; g_best = 0; EnumWindows(findwin, 0); g_tid = keep; }   // closed: another window to nudge
        if (WaitForSingleObject(e, 100) == WAIT_OBJECT_0) { reported = TRUE; break; }
    }
    if (!reported) llog("launcher: dynafix.dll did not report in (hook thread %u, error %u)\r\n", g_tid, GetLastError());
    UnhookWindowsHookEx(hk);
    if (e) CloseHandle(e);
    finish();
}

void WinMainCRTStartup(void)
{
    char exe[MAX_PATH], dir[MAX_PATH], dll[MAX_PATH], le[MAX_PATH], *p, *cmd;
    HMODULE hd; HANDLE h; DWORD n; BOOL wasadmin;

    // From here on DLLs are looked for in System32 only (not the current folder, not the PATH). dynafix.dll is loaded by its full path.
    {
        typedef BOOL (WINAPI *sddd_t)(DWORD);
        sddd_t sddd = (sddd_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetDefaultDllDirectories");   // Vista+ / KB2533623
        if (sddd) sddd(0x00000800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    }

    // dynafix.dll lives next to this launcher
    n = GetModuleFileNameA(NULL, dll, MAX_PATH);
    if (!n || n >= MAX_PATH - 16) fail_w(L"The folder of DynaRunFix.exe is too deep.", L"DynaRunFix.exe 所在的資料夾路徑太長。");
    p = dll + lstrlenA(dll); while (p > dll && *p != '\\') p--; lstrcpyA(p, "\\dynafix.dll");

    cmd = own_args();
    // "/wasadmin": this is the copy an elevated launcher started with the shell's token (see relaunch_non_elevated). Only
    // believed when this process is not elevated (see elevation_gate); an elevated process that is given it ignores it.
    wasadmin = prefix(&cmd, VERB_WASADMIN);
    elevation_gate(wasadmin);
    trim_log();
    log_start(cmd);
    parse_args(cmd, exe, dir);

    g_pid = running();
    if (g_pid) {
        g_update = FALSE;   // attaching to a DynaRun that is already there: nothing is offered
        h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, g_pid);
        if (h) CloseHandle(h);
        else if (GetLastError() == ERROR_ACCESS_DENIED) {   // DynaRun is never raised
            llog("launcher: the running DynaRun cannot be opened (it runs with more rights than this launcher)\r\n", 0, 0);
            fail_w(L"DynaRun is running with administrator rights. Close it and start it again through DynaRunFix.", L"DynaRun 正以系統管理員權限執行。請關閉後，再透過 DynaRunFix 啟動。");
        }
    } else {
        {   // never with more rights than a normal start; a one-time notice says so when the launcher was started as administrator or the layer is set
            if (wasadmin && !g_elevated) { llog("launcher: was started as administrator and started itself again non-elevated\r\n", 0, 0); show_notice(1); }
            else if (runasadmin(exe)) { llog("launcher: RUNASADMIN compatibility layer is set for DynaRun and ignored\r\n", 0, 0); show_notice(2); }
        }
        {   // before DynaRun runs (see early_hook)
            DWORD t0 = GetTickCount();
            hd = LoadLibraryA(dll);
            llog("launcher: dynafix.dll loaded in %u ms (error %u)\r\n", GetTickCount() - t0, hd ? 0 : GetLastError());
        }
        if (use_le(dll, le)) start_le(hd, le, exe, dir);
        else start_plain(hd, exe, dir);
    }
    hook_and_finish(dll);
}
#endif
