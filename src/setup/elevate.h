// Part of setup.c (elevation): included by it, in one compilation unit; never compiled on its own.
static int memcmp_(const BYTE *x, const BYTE *y, DWORD n) { while (n--) if (*x++ != *y++) return 1; return 0; }

// A copy of src as the new file dst (CREATE_NEW: nothing that exists is used), then held: the returned handle is opened for reading
// with FILE_SHARE_READ only, so as long as it is open nobody can write to, rename or delete the copy, and the content read through it
// is compared with src. INVALID_HANDLE_VALUE when any step fails (dst is deleted again). The copy can be run while the handle is open.
static HANDLE copy_and_hold(const WCHAR *src, const WCHAR *dst)
{
    static BYTE a[65536], b[65536];
    HANDLE s, d, h = INVALID_HANDLE_VALUE; DWORD na, nb; BOOL ok = TRUE; BY_HANDLE_FILE_INFORMATION bi;
    s = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s == INVALID_HANDLE_VALUE) return h;
    d = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (d == INVALID_HANDLE_VALUE) { CloseHandle(s); return h; }
    while (ok && ReadFile(s, a, sizeof(a), &na, NULL) && na) ok = WriteFile(d, a, na, &nb, NULL) && nb == na;
    CloseHandle(d);
    if (ok) {
        h = CreateFileW(dst, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            if (!GetFileInformationByHandle(h, &bi) || (bi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) ok = FALSE;
            SetFilePointer(s, 0, NULL, FILE_BEGIN);
            while (ok) {                              // the held copy has the content of the original
                BOOL r1 = ReadFile(s, a, sizeof(a), &na, NULL), r2 = ReadFile(h, b, sizeof(b), &nb, NULL);
                if (!r1 || !r2 || na != nb || memcmp_(a, b, na)) { ok = FALSE; break; }
                if (!na) break;
            }
            if (!ok) { CloseHandle(h); h = INVALID_HANDLE_VALUE; }
        }
    }
    CloseHandle(s);
    if (h == INVALID_HANDLE_VALUE) DeleteFileW(dst);
    return h;
}

// Runs "<exe> <args>" elevated (one UAC prompt) and returns its exit code; 2 = cancelled. exe: the program to run, NULL = this one.
int run_elevated(const WCHAR *args, const WCHAR *exe_path, const install_opts *o)
{
    SHELLEXECUTEINFOW se; DWORD rc = RC_FAIL, err, tn; static WCHAR a[1024], root[4], tmp[MAX_PATH]; const WCHAR *exe = exe_path ? exe_path : g_self; HANDLE hold = INVALID_HANDLE_VALUE; LARGE_INTEGER q;
    lstrcpyW(a, args); if (o->quiet) lstrcatW(a, L" /quiet"); if (o->close) lstrcatW(a, L" /close");
    if (o->pin) lstrcatW(a, L" /pin");                // the elevated stage sets up the Windows 11 pin
    if (o->keep) lstrcatW(a, L" /keep");
    // Mapped network drives (e.g. a shared folder) do not exist for the elevated process: run a copy in %TEMP%
    lstrcpynW(root, g_self, 4);
    if (!exe_path && root[1] == ':' && GetDriveTypeW(root) == DRIVE_REMOTE && (tn = GetTempPathW(MAX_PATH - 60, tmp)) != 0 && tn < MAX_PATH - 60) {   // room for the longest name below (17 + 3 x 10 digits + 2 + 4 + NUL)
        q.QuadPart = 0; QueryPerformanceCounter(&q);
        wsprintfW(tmp + lstrlenW(tmp), L"DynaRunFix-Setup-%lu-%lu-%lu.exe", GetCurrentProcessId(), GetTickCount(), q.LowPart);
        // the copy is held open (nobody can change it) from here until the elevated process has ended: what the UAC prompt names is what runs
        if ((hold = copy_and_hold(g_self, tmp)) != INVALID_HANDLE_VALUE) exe = tmp;
    }
    zero(&se, sizeof(se));
    se.cbSize = sizeof(se); se.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    se.hwnd = g_hwnd; se.lpVerb = L"runas"; se.lpFile = exe; se.lpParameters = a; se.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&se)) {
        err = GetLastError();
        if (hold != INVALID_HANDLE_VALUE) { CloseHandle(hold); DeleteFileW(tmp); }
        return err == ERROR_CANCELLED ? RC_CANCEL : RC_FAIL;
    }
    WaitForSingleObject(se.hProcess, INFINITE);
    GetExitCodeProcess(se.hProcess, &rc);
    CloseHandle(se.hProcess);
    if (hold != INVALID_HANDLE_VALUE) { CloseHandle(hold); DeleteFileW(tmp); }
    return (int)rc;
}

