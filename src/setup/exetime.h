// Part of setup.c (the borrowed exe time stamp): included by it, in one compilation unit; never compiled on its own.
// Observed in testing: Windows keeps using the manifest state it saw at the exe's last start until it sees a different LastWriteTime
// on a process creation. So: read the LastWriteTime, set another value, let Windows see it by creating the exe's process suspended
// and terminating it at once (none of the exe's code runs), then set the original value back. The exe's content and its
// timestamps end up exactly as they were. (Setting the value and restoring it without the process creation in between does not
// make Windows read the manifest.)
// The original time is written to HKLM\SOFTWARE\DynaRunFix (ExeOrigTimePath, ExeOrigTime) before it is changed and removed
// after it has been put back and read back; recover_exe_time puts back what a run that was killed or failed left.
enum { TOUCH_OK, TOUCH_NOTDONE, TOUCH_STUCK };     // NOTDONE: nothing was changed. STUCK: the original time could not be put back
static const WCHAR ORIGPATH[] = L"ExeOrigTimePath", ORIGTIME[] = L"ExeOrigTime";
static HKEY g_app_root = HKEY_LOCAL_MACHINE; static const WCHAR *g_app_key = APPKEY;     // where the record is kept (the tests use a key of their own)

static BOOL same_time(const FILETIME *a, const FILETIME *b) { return a->dwLowDateTime == b->dwLowDateTime && a->dwHighDateTime == b->dwHighDateTime; }

static BOOL time_set_verified(HANDLE h, const FILETIME *t)       // set, read back, retry a few times
{
    FILETIME c; int i;
    for (i = 0; i < 5; i++) {
        if (SetFileTime(h, NULL, NULL, t) && GetFileTime(h, NULL, NULL, &c) && same_time(&c, t)) return TRUE;
        Sleep(300);
    }
    return FALSE;
}

static HANDLE open_times(const WCHAR *p)
{ return CreateFileW(p, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL); }

static void forget_exe_time(HKEY k) { RegDeleteValueW(k, ORIGPATH); RegDeleteValueW(k, ORIGTIME); }

static int recover_exe_time(void)
{
    HKEY k; static WCHAR path[MAX_PATH]; FILETIME saved, cur; DWORD n = sizeof(saved), type; HANDLE h; int rc = TOUCH_OK;
    if (RegOpenKeyExW(g_app_root, g_app_key, 0, KEY_ALL_ACCESS, &k)) return TOUCH_OK;
    if (reg_str(g_app_root, g_app_key, ORIGPATH, path, MAX_PATH, 0) && is_dynarun_name(path) &&
        !RegQueryValueExW(k, ORIGTIME, NULL, &type, (BYTE *)&saved, &n) && type == REG_BINARY && n == sizeof(saved)) {
        if ((h = open_times(path)) == INVALID_HANDLE_VALUE) { if (!exists(path)) forget_exe_time(k); else rc = TOUCH_STUCK; }
        else {
            if (GetFileTime(h, NULL, NULL, &cur) && same_time(&cur, &saved)) forget_exe_time(k);
            else if (time_set_verified(h, &saved)) { forget_exe_time(k); setup_log(L"restored the time stamp of DynaRun V3.exe that an earlier run had left changed"); }
            else rc = TOUCH_STUCK;
            CloseHandle(h);
        }
    }
    RegCloseKey(k);
    return rc;
}

// The temporary time: now, but at least 4 s from the original (the file systems keep 2 s at the coarsest, FAT), on an even second.
// (After rounding down to an even second it is still more than 2 s away.)
static void pick_temp_time(const FILETIME *orig, FILETIME *out)
{
    ULARGE_INTEGER u, o; SYSTEMTIME st;
    GetSystemTimeAsFileTime(out);
    u.LowPart = out->dwLowDateTime; u.HighPart = out->dwHighDateTime; o.LowPart = orig->dwLowDateTime; o.HighPart = orig->dwHighDateTime;
    if ((u.QuadPart > o.QuadPart ? u.QuadPart - o.QuadPart : o.QuadPart - u.QuadPart) < 40000000ULL) u.QuadPart = o.QuadPart + 100000000ULL;
    out->dwLowDateTime = u.LowPart; out->dwHighDateTime = u.HighPart;
    if (FileTimeToSystemTime(out, &st)) { st.wSecond &= ~1; st.wMilliseconds = 0; SystemTimeToFileTime(&st, out); }
}

static int touch(const WCHAR *p)
{
    FILETIME orig, ft; HANDLE h; HKEY k; STARTUPINFOW si; PROCESS_INFORMATION pi; WCHAR *cmd; int rc = TOUCH_NOTDONE;
    if (!p[0] || !is_dynarun_name(p) || !stage_lock()) return TOUCH_NOTDONE;
    h = open_times(p);
    if (h != INVALID_HANDLE_VALUE) {
        if (GetFileTime(h, NULL, NULL, &orig) && !RegCreateKeyExW(g_app_root, g_app_key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) {
            if (!RegSetValueExW(k, ORIGPATH, 0, REG_SZ, (const BYTE *)p, (lstrlenW(p) + 1) * sizeof(WCHAR)) &&
                !RegSetValueExW(k, ORIGTIME, 0, REG_BINARY, (const BYTE *)&orig, sizeof(orig))) {
                pick_temp_time(&orig, &ft);
                if (SetFileTime(h, NULL, NULL, &ft)) {
                    zero(&si, sizeof si); si.cb = sizeof si;
                    if ((cmd = alloc((MAX_PATH + 16) * sizeof(WCHAR)))) {
                        cat3(cmd, L"\"", p, L"\"");
                        if (CreateProcessW(p, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
                            TerminateProcess(pi.hProcess, 0); WaitForSingleObject(pi.hProcess, 5000);
                            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
                        }
                        release(cmd);
                    }
                    if (time_set_verified(h, &orig)) { forget_exe_time(k); rc = TOUCH_OK; }
                    else rc = TOUCH_STUCK;                  // the record stays: the next install or uninstall puts it back
                } else forget_exe_time(k);                  // the time was not changed
            } else forget_exe_time(k);
            RegCloseKey(k);
        }
        CloseHandle(h);
    }
    stage_unlock();
    return rc;
}

// what the caller does with touch's result
static int touch_report(const install_opts *o, int t)
{
    if (t == TOUCH_STUCK)
        warn(o, TS(ST_THE_MODIFIED_TIME_OF_DYNARUN));
    else if (t == TOUCH_NOTDONE) setup_log(L"the manifest was not refreshed: DynaRun V3.exe could not be touched");
    return t;
}


