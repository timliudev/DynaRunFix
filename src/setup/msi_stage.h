// Part of setup.c (vendor MSI staging and full stage): included by it, in one compilation unit; never compiled on its own.
static const WCHAR *no_admin(void)
{ return TS(ST_ADMINISTRATOR_PERMISSION_WAS_NOT_GRANTED); }

// Elevated: install DynaRun from its MSI, then the machine stage. The MSI installs per user (as the
// original setup does), so it must run as the user who asked for it: with over-the-shoulder
// elevation (another account typed into the UAC prompt) return 3 and let the caller run it.
//
// The MSI comes from a folder the user can write (Downloads, %TEMP%), and msiexec runs it with the rights of this stage:
// the file is first copied into a new folder only administrators and SYSTEM can use (%WINDIR%\Temp\DynaRunFix-...; created
// here with that ACL, so nobody else could have prepared it), checked there again (it must be Dyna Pro's DynaRun setup) and
// run from there; the copy is deleted afterwards. Cabinet files the MSI names outside itself are copied along.
static BOOL make_admin_dir(const WCHAR *base, WCHAR *out)      // base: the folder in which it is created (%WINDIR%\Temp)
{
    static const WCHAR sddl[] = L"D:P(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)";      // protected: Administrators and SYSTEM, full control
    PSECURITY_DESCRIPTOR sd; SECURITY_ATTRIBUTES sa; int i; BOOL ok = FALSE; LARGE_INTEGER q;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, NULL)) return FALSE;
    sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = sd; sa.bInheritHandle = FALSE;
    for (i = 0; i < 20 && !ok; i++) {              // a name that exists already is refused by CreateDirectory: try another
        q.QuadPart = 0; QueryPerformanceCounter(&q);
        lstrcpynW(out, base, MAX_PATH - 60);
        wsprintfW(out + lstrlenW(out), L"\\DynaRunFix-%lu-%lu-%lu", GetCurrentProcessId(), GetTickCount(), q.LowPart + i);
        ok = CreateDirectoryW(out, &sa);
    }
    LocalFree(sd);
    return ok;
}

static BOOL copy_new(const WCHAR *from, const WCHAR *to)         // to must not exist
{
    static BYTE buf[65536]; HANDLE in, out; DWORD n, w; BOOL ok = TRUE;
    in = CreateFileW(from, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (in == INVALID_HANDLE_VALUE) return FALSE;
    out = CreateFileW(to, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) { CloseHandle(in); return FALSE; }
    while (ok && ReadFile(in, buf, sizeof(buf), &n, NULL) && n) ok = WriteFile(out, buf, n, &w, NULL) && w == n;
    CloseHandle(in);
    ok = ok && FlushFileBuffers(out);
    CloseHandle(out);
    if (!ok) DeleteFileW(to);
    return ok;
}

static void drop_admin_dir(const WCHAR *dir) { delete_tree(dir, NULL, 0); }

static BOOL stage_msi(const WCHAR *msi, WCHAR *dir, WCHAR *staged)
{
    WCHAR *cabs, *c, src[MAX_PATH], dst[MAX_PATH], base[MAX_PATH], *e; int n;
    GetWindowsDirectoryW(base, MAX_PATH - 20); lstrcatW(base, L"\\Temp");
    if (!make_admin_dir(base, dir)) return FALSE;
    cat3(staged, dir, L"\\Setup.msi", NULL);
    if (!copy_new(msi, staged)) { drop_admin_dir(dir); return FALSE; }
    if ((cabs = msi_external_files(staged))) {          // the names are read from the copy that is going to be run (and checked), not from the user's file
        lstrcpynW(src, msi, MAX_PATH); for (e = src + lstrlenW(src); e > src && *e != '\\'; e--); n = (int)(e - src);
        for (c = cabs; *c; c += lstrlenW(c) + 1)
            if (n + 1 + lstrlenW(c) < MAX_PATH && dir[0] && lstrlenW(dir) + 1 + lstrlenW(c) < MAX_PATH) {
                lstrcpynW(src + n + 1, c, MAX_PATH - n - 1); src[n] = '\\';
                copy_new(src, cat3(dst, dir, L"\\", c));           // a missing one is msiexec's error to report
            }
        release(cabs);
    }
    return TRUE;
}

static int machine_full(const install_opts *o, const WCHAR *sid, const WCHAR *msi)
{
    static WCHAR me[200], txt[300], dir[MAX_PATH], staged[MAX_PATH]; int rc;
    if (!user_sid(me, 200) || lstrcmpiW(me, sid)) return RC_OVER_SHOULDER;
    if (!stage_lock()) { error2(o, busy_text(), NULL); return RC_FAIL; }
    if (still_running(o, L"full install")) { stage_unlock(); return RC_RUNNING; }
    if (!stage_msi(msi, dir, staged)) {
        stage_unlock();
        msg(TS(ST_THE_DYNARUN_V3_SETUP_COULD), MB_ICONERROR);
        return RC_MSI_FAILED;
    }
    if (msi_check(staged, NULL, 0) != MSI_DYNARUN) {            // the check of the copy that is going to be run
        drop_admin_dir(dir); stage_unlock();
        msg(TS(ST_THIS_IS_NOT_THE_DYNARUN), MB_ICONERROR);
        return RC_MSI_FAILED;
    }
    rc = run_msiexec(staged);
    drop_admin_dir(dir);
    stage_unlock();
    if (rc != 0 && rc != ERROR_SUCCESS_REBOOT_REQUIRED) {
        if (rc == ERROR_INSTALL_USEREXIT) return RC_CANCEL;
        wsprintfW(txt, TS(ST_THE_DYNARUN_V3_SETUP_FAILED), rc);
        if (rc == ERROR_INSTALL_ALREADY_RUNNING)
            lstrcatW(txt, TS(ST_ANOTHER_INSTALLATION_IS_RUNNING_WAIT));
        msg(txt, MB_ICONERROR);
        return RC_MSI_FAILED;
    }
    if (!locate_dynarun()) { msg(TS(ST_DYNARUN_V3_WAS_INSTALLED_BUT), MB_ICONERROR); return RC_NO_DYNARUN; }
    return machine_install(o, sid);
}

