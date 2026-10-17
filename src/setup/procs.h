// Part of setup.c (paths, running programs, user log): included by it, in one compilation unit; never compiled on its own.
/* ---------- paths ---------- */

static void init_paths(void)
{
    GetModuleFileNameW(NULL, g_self, MAX_PATH);
    if (!SHGetSpecialFolderPathW(NULL, g_dir, CSIDL_PROGRAM_FILES, FALSE)) lstrcpyW(g_dir, L"C:\\Program Files");
    lstrcatW(g_dir, L"\\DynaRunFix");
    cat3(g_launcher, g_dir, L"\\DynaRunFix.exe", NULL);
}

BOOL locate_dynarun(void)
{
    static const int csidl[] = { CSIDL_PROGRAM_FILESX86, CSIDL_PROGRAM_FILES };
    WCHAR pf[MAX_PATH]; int i;
    if (reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"DynaRunExe", g_exe, MAX_PATH, 0) && exists(g_exe)) return TRUE;
    // the program folders from the system (not from environment variables, which a process can set for the elevated stage)
    for (i = 0; i < 2; i++)
        if (SHGetSpecialFolderPathW(NULL, pf, csidl[i], FALSE) && lstrlenW(pf) + lstrlenW(DYNARUN_REL) < MAX_PATH && exists(cat3(g_exe, pf, DYNARUN_REL, NULL))) return TRUE;
    g_exe[0] = 0;
    return FALSE;
}

/* ---------- programs that hold the files the setup replaces ---------- */

typedef BOOL (WINAPI *QueryFullProcessImageNameW_t)(HANDLE, DWORD, LPWSTR, PDWORD);

static void process_path(DWORD pid, WCHAR *out, DWORD cch)   // the exe of a process, or "" when it cannot be read
{
    static QueryFullProcessImageNameW_t q; static BOOL tried; HANDLE h; MODULEENTRY32W me;
    out[0] = 0;
    if (!tried) { tried = TRUE; q = (QueryFullProcessImageNameW_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryFullProcessImageNameW"); }
    // Vista and later: also for an elevated process when this one is not (PROCESS_QUERY_LIMITED_INFORMATION)
    if (q && (h = OpenProcess(0x1000, FALSE, pid))) {
        if (!q(h, 0, out, &cch)) out[0] = 0;
        CloseHandle(h);
        if (out[0]) return;
    }
    h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);       // XP: the first module is the exe
    if (h == INVALID_HANDLE_VALUE) return;
    me.dwSize = sizeof(me);
    if (Module32FirstW(h, &me)) lstrcpynW(out, me.szExePath, cch);
    CloseHandle(h);
}

// A running DynaRun V3 has dynafix.dll (and under Locale Emulator LoaderDll.dll / LocaleEmulator.dll)
// of the install folder loaded. Windows lets the setup rename such files and put new ones in place,
// but that DynaRun keeps the old code, and "Start DynaRun" would only find it already running, so
// DynaRun is closed first (the user agrees in the wizard, or passes /close).
// What holds up the install (judge_proc): the DynaRun V3.exe that is the known one (g_exe; a DynaRun of another folder does not use
// our files), and the launcher and LEProc when they run from the install folder. A program whose exe path cannot be read counts as
// ours (better than a half-updated install). The setup closes only what it can tell is ours and that runs in its own session; the
// rest still holds up the install, but "close" would not help, and the wizard and the log say so.
typedef struct { DWORD pid; int bit; DWORD sess; int why; WCHAR path[MAX_PATH]; } runproc;
enum { JUDGE_SKIP, JUDGE_CLOSE, JUDGE_OTHERSESSION, JUDGE_NOPATH, JUDGE_NOEXE };   // not in the way / can be closed / cannot: why

static BOOL same_path(const WCHAR *a, const WCHAR *b)     // case-insensitive; 8.3 and long names of one file match
{
    WCHAR x[MAX_PATH], y[MAX_PATH]; DWORD m, n;
    if (!a[0] || !b[0]) return FALSE;
    if (!lstrcmpiW(a, b)) return TRUE;
    m = GetLongPathNameW(a, x, MAX_PATH); n = GetLongPathNameW(b, y, MAX_PATH);
    return m && m < MAX_PATH && n && n < MAX_PATH && !lstrcmpiW(x, y);
}

// bit: RUN_* of the program; path: its exe ("" when unreadable); exe: the known DynaRun V3.exe ("" when not known yet);
// dir: the install folder with a trailing backslash
static int judge_proc(int bit, BOOL same_session, const WCHAR *path, const WCHAR *exe, const WCHAR *dir)
{
    int len = lstrlenW(dir);
    if (bit == RUN_DYNARUN) {
        if (!path[0]) return JUDGE_NOPATH;
        if (!exe[0]) return JUDGE_NOEXE;
        if (!same_path(path, exe)) return JUDGE_SKIP;
    } else {
        if (!path[0]) return JUDGE_NOPATH;
        if (!(lstrlenW(path) > len && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, path, len, dir, len) == CSTR_EQUAL)) return JUDGE_SKIP;
    }
    return same_session ? JUDGE_CLOSE : JUDGE_OTHERSESSION;
}

// The programs in the way (up to max in list); the result has their RUN_* bits, with RUN_CANCLOSE / RUN_NOCLOSE for the two kinds.
static int scan_programs(runproc *list, int max, int *count)
{
    PROCESSENTRY32W pe; HANDLE s; WCHAR path[MAX_PATH], dir[MAX_PATH + 2]; int r = 0, bit, why; DWORD mysess = 0, sess;
    *count = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &mysess);
    s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return 0;
    cat3(dir, g_dir, L"\\", NULL);
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(s, &pe)) do {
        bit = !lstrcmpiW(pe.szExeFile, L"DynaRun V3.exe") ? RUN_DYNARUN : !lstrcmpiW(pe.szExeFile, L"DynaRunFix.exe") ? RUN_LAUNCHER :
              !lstrcmpiW(pe.szExeFile, L"LEProc.exe") ? RUN_LEPROC : 0;
        if (!bit) continue;
        if (!ProcessIdToSessionId(pe.th32ProcessID, &sess)) sess = (DWORD)-1;
        process_path(pe.th32ProcessID, path, MAX_PATH);
        why = judge_proc(bit, sess == mysess, path, g_exe, dir);
        if (why == JUDGE_SKIP) continue;
        r |= bit | (why == JUDGE_CLOSE ? RUN_CANCLOSE : RUN_NOCLOSE);
        if (list && *count < max) {
            list[*count].pid = pe.th32ProcessID; list[*count].bit = bit; list[*count].sess = sess; list[*count].why = why;
            lstrcpyW(list[*count].path, path); (*count)++;
        }
    } while (Process32NextW(s, &pe));
    CloseHandle(s);
    return r;
}

int running_programs(void)
{ int n; return scan_programs(NULL, 0, &n); }

// Why the setup stops with RC_RUNNING (nothing is shown with /quiet): one line per program in a log. The elevated stage writes
// setup.log in the install folder; before that folder exists (or without administrator rights) %TEMP%\DynaRunFix-setup.log.
static void run_log(const WCHAR *line)
{
    static WCHAR p[MAX_PATH];
    if (is_admin() && g_dir[0] && exists(g_dir)) { setup_log(line); return; }
    if (GetTempPathW(MAX_PATH - 24, p)) append_line(lstrcatW(p, L"DynaRunFix-setup.log"), line);
}

// For the other files (the wizard side of the driver step): the line with a time stamp, so that durations can be read from the log.
// The wizard thread, its worker and the update-hint thread all log: one lock covers the formatting (static buffers) and the write.
static CRITICAL_SECTION g_ulog_cs;
static volatile LONG g_ulog_state;                     // 0 not set up, 1 being set up, 2 ready

void user_log(const WCHAR *line)
{
    static WCHAR l[2 * MAX_PATH + 400]; SYSTEMTIME t; const WCHAR *p, *q;
    if (g_ulog_state != 2) {
        if (InterlockedCompareExchange(&g_ulog_state, 1, 0) == 0) { InitializeCriticalSection(&g_ulog_cs); g_ulog_state = 2; }
        else while (g_ulog_state != 2) Sleep(1);
    }
    EnterCriticalSection(&g_ulog_cs);
    GetLocalTime(&t);
    for (p = line; *p; p = *q ? q + 1 : q) {                            // a text of several lines is several lines, each with its time; the log is UTF-8 (append_line)
        int n, pre, cnt;
        for (q = p; *q && *q != '\n'; q++);
        n = (int)(q - p);
        while (n > 0 && p[n - 1] == '\r') n--;
        if (n <= 0) continue;
        wsprintfW(l, L"[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        pre = lstrlenW(l); cnt = n + 1;
        if (cnt > (int)(sizeof(l) / sizeof(l[0])) - pre) cnt = (int)(sizeof(l) / sizeof(l[0])) - pre;
        lstrcpynW(l + pre, p, cnt);
        run_log(l);
    }
    LeaveCriticalSection(&g_ulog_cs);
}

static void log_running(const WCHAR *stage, const runproc *l, int n, int r, BOOL close)
{
    static const WCHAR *const names[] = { L"DynaRun V3.exe", L"DynaRunFix.exe", L"LEProc.exe" };
    static const WCHAR *const reasons[] = { L"can be closed", L"cannot be closed: runs in another session",
                                            L"cannot be closed: its exe path cannot be read", L"cannot be closed: the DynaRun folder is not known" };
    WCHAR t[900]; SYSTEMTIME st; int i, k; DWORD me = 0;
    GetLocalTime(&st); ProcessIdToSessionId(GetCurrentProcessId(), &me);
    wsprintfW(t, L"%04u-%02u-%02u %02u:%02u:%02u  %s: stopped, exit code 6 (DynaRun is running), nothing was changed. %s", st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond, stage,
              (r & RUN_NOCLOSE) ? (close ? L"/close was not used: a program that cannot be closed is running." : L"A program that cannot be closed is running.")
                                : (close ? L"It was still running after /close." : L"Run with /close to close it."));
    run_log(t);
    for (i = 0; i < n; i++) {
        k = l[i].bit == RUN_DYNARUN ? 0 : l[i].bit == RUN_LAUNCHER ? 1 : 2;
        wsprintfW(t, L"  %s pid %lu, session %lu (this setup: %lu), %s%s%s", names[k], l[i].pid, l[i].sess, me, reasons[l[i].why - JUDGE_CLOSE],
                  l[i].path[0] ? L", " : L"", l[i].path);
        run_log(t);
    }
}

static BOOL CALLBACK close_window(HWND h, LPARAM pid)
{
    DWORD p;
    GetWindowThreadProcessId(h, &p);
    if (p == (DWORD)pid && IsWindowVisible(h)) PostMessageW(h, WM_CLOSE, 0, 0);
    return TRUE;
}

static void wait_gone(runproc *l, int n, int bits, DWORD ms)
{
    HANDLE h[16]; int i, k = 0;
    for (i = 0; i < n; i++) if ((l[i].bit & bits) && l[i].why == JUDGE_CLOSE && (h[k] = OpenProcess(SYNCHRONIZE, FALSE, l[i].pid))) k++;
    if (k) WaitForMultipleObjects(k, h, TRUE, ms);
    while (k) CloseHandle(h[--k]);
}

// Closes DynaRun as if its window were closed (WM_CLOSE); what is still running 10 s later (e.g. a
// question DynaRun asks on exit) is ended. Runs in the elevated stage: an elevated DynaRun accepts
// neither messages nor TerminateProcess from a process that is not elevated.
static void close_programs(void)
{
    runproc *l = alloc(16 * sizeof(runproc)); int n, i; HANDLE h;      // on the heap: a stack of that size would need the C runtime's probe
    if (!l || !(scan_programs(l, 16, &n) & RUN_CANCLOSE)) { release(l); return; }
    for (i = 0; i < n; i++) if (l[i].bit == RUN_DYNARUN && l[i].why == JUDGE_CLOSE) EnumWindows(close_window, l[i].pid);
    wait_gone(l, n, RUN_DYNARUN, 10000);
    wait_gone(l, n, RUN_LAUNCHER | RUN_LEPROC, 2000);     // LEProc waits for DynaRun, the launcher ends by itself
    scan_programs(l, 16, &n);
    for (i = 0; i < n; i++)
        if (l[i].why == JUDGE_CLOSE && (h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, l[i].pid))) {
            TerminateProcess(h, 1); WaitForSingleObject(h, 5000); CloseHandle(h);
        }
    release(l);
}

// The elevated install stages: closes DynaRun first when the user agreed (or /close), then TRUE (and the reason logged) when something
// still holds up the install. Nothing is closed when part of it cannot be closed: the install stops anyway, and a test run would end for nothing.
static BOOL still_running(const install_opts *o, const WCHAR *stage)
{
    runproc *l = alloc(16 * sizeof(runproc)); int n, r = scan_programs(l, l ? 16 : 0, &n);
    if (o->close && r && !(r & RUN_NOCLOSE)) { close_programs(); r = scan_programs(l, l ? 16 : 0, &n); }
    if (r && l) log_running(stage, l, n, r, o->close);
    release(l);
    return r != 0;
}

// Before the UAC prompt: TRUE (logged) when /quiet without /close ends here. With /close the elevated stage decides: it can read more.
static BOOL running_early(const install_opts *o, const WCHAR *stage)
{
    runproc *l = alloc(16 * sizeof(runproc)); int n, r = scan_programs(l, l ? 16 : 0, &n);
    if (r && !o->close && l) log_running(stage, l, n, r, o->close);
    release(l);
    return r && !o->close;
}

