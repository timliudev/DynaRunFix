// Part of setup.c (uninstall): included by it, in one compilation unit; never compiled on its own.
// The uninstall question's Yes / No buttons become "Fix only" / "Remove all".
static HHOOK g_cbt;
static LRESULT CALLBACK relabel(int code, WPARAM w, LPARAM l)
{
    HHOOK h = g_cbt;
    if (code == HCBT_ACTIVATE) {
        SetDlgItemTextW((HWND)w, IDYES, TS(ST_FIX_ONLY));
        SetDlgItemTextW((HWND)w, IDNO, TS(ST_REMOVE_ALL));
        UnhookWindowsHookEx(g_cbt); g_cbt = NULL;
    }
    return CallNextHookEx(h, code, w, l);
}

// Copies of the uninstaller that earlier builds (1.4.0-rc1/rc2) left in %TEMP%; a running one stays
static const WCHAR TEMP_PREFIX[] = L"DynaRunFix-uninstall-";
static void drop_old_temp_copies(void)
{
    WCHAR t[MAX_PATH], f[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE h; DWORD n = GetTempPathW(MAX_PATH - 60, t);
    if (!n || n >= MAX_PATH - 60) return;
    lstrcatW(t, TEMP_PREFIX);
    h = FindFirstFileW(cat3(f, t, L"*.exe", NULL), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if ((fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || lstrlenW(t) + lstrlenW(fd.cFileName) >= MAX_PATH) continue;
        GetTempPathW(MAX_PATH - 60, f); lstrcatW(f, fd.cFileName);
        DeleteFileW(f);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int uninstall(const install_opts *given)
{
    static WCHAR p[MAX_PATH + 160], prod[40], sid[200], args[260]; int rc; BOOL all = FALSE;
    install_opts mine = *given, *o = &mine;      // the "close DynaRun" consent asked below is this uninstall's own
    const WCHAR *elev = NULL;
    // The uninstaller runs from the installed program (no copy in %TEMP%): the elevated stage renames what it cannot delete (this very
    // file, while the user-side stage still runs) and Windows deletes it at the next restart.
    cat3(p, g_dir, L"\\DynaRunFix-Setup.exe", NULL);
    drop_old_temp_copies();                        // copies that earlier builds left in %TEMP%
    if (!exists(p) && !reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"DynaRunExe", g_exe, MAX_PATH, 0)) {
        info(o, TS(ST_DYNARUNFIX_IS_NOT_INSTALLED));
        return RC_OK;
    }
    if (!o->quiet && !dynarun_product(prod)) {
        if (msg(TS(ST_UNINSTALL_DYNARUNFIX), MB_YESNO | MB_ICONQUESTION) != IDYES) return RC_CANCEL;
    } else if (!o->quiet) {
        g_cbt = SetWindowsHookExW(WH_CBT, relabel, NULL, GetCurrentThreadId());
        rc = msg(TS(ST_UNINSTALL_DYNARUNFIX_FIX_ONLY_THE), MB_YESNOCANCEL | MB_ICONQUESTION);
        if (g_cbt) { UnhookWindowsHookEx(g_cbt); g_cbt = NULL; }
        if (rc == IDCANCEL) return RC_CANCEL;
        all = rc == IDNO;
    }
    // Running DynaRun / launcher / LEProc hold files of the folder: ask first, the elevated stage then closes them.
    // /quiet without /close goes on; what is still in use is deleted at the next restart.
    if (!o->quiet && running_programs()) {
        if (msg(TS(ST_DYNARUN_IS_RUNNING_AND_MUST), MB_OKCANCEL | MB_ICONWARNING) != IDOK) return RC_CANCEL;
        o->close = TRUE;
    }
    reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"DynaRunExe", g_exe, MAX_PATH, 0);
    if (!g_exe[0]) locate_dynarun();
    CoInitialize(NULL);
    if (!user_sid(sid, 200)) sid[0] = 0;
    // The elevated stage comes first: when the UAC prompt is cancelled (or fails to start) nothing has been changed yet and the
    // user's shortcuts, Run value and pins still work with the installed launcher.
    if (is_admin()) rc = machine_uninstall(o, sid);
    else {
        trust_ctx t;
        // What runs elevated is the installed program in Program Files (administrators-only folder), never a copy in a folder the user can
        // write. Only when there is no installed copy (a broken install) is this file itself run, if it is not one of our %TEMP% copies.
        zero(&t, sizeof(t)); lstrcpynW(t.user_sid, sid, 200);
        if (exists(p)) {
            if (g_is_writable(&t, g_dir) || g_is_writable(&t, p)) {
                error2(o, TS(ST_THE_DYNARUNFIX_FOLDER_CAN_BE), g_dir);
                return RC_FAIL;
            }
            elev = p;
        }
        wsprintfW(args, L"/unmachine %s", sid);
        rc = run_elevated(sid[0] ? args : L"/unmachine", elev, o);
    }
    if (rc == RC_CANCEL) { msg(no_admin(), MB_ICONWARNING); return RC_CANCEL; }
    if (rc) {                                                           // the elevated stage did not finish: nothing user-side is undone either
        if (!o->quiet) { wsprintfW(p, TS(ST_THE_UNINSTALL_DID_NOT_FINISH), rc); msg(p, MB_ICONERROR); }
        return rc;
    }
    restore_shortcuts(HKEY_CURRENT_USER, USERKEY);
    RegDeleteKeyW(HKEY_CURRENT_USER, USERKEY);
    drop_run(HKEY_CURRENT_USER, FALSE);
    drop_policy(HKEY_CURRENT_USER);                                     // the Windows 11 taskbar policy of this user, when it is still ours
    unretarget_shell(FALSE);                                            // our shortcuts that no backup covered, and pins the user made
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    if (all) {
        rc = remove_dynarun();
        if (rc == ERROR_INSTALL_USEREXIT) { info(o, TS(ST_DYNARUNFIX_HAS_BEEN_UNINSTALLED_DYNARUN)); return RC_CANCEL; }
        if (rc > 0 && rc != ERROR_SUCCESS_REBOOT_REQUIRED) {
            wsprintfW(p, TS(ST_DYNARUNFIX_HAS_BEEN_UNINSTALLED_BUT), rc);
            msg(p, MB_ICONERROR);
            return RC_MSI_FAILED;
        }
        info(o, TS(ST_DYNARUNFIX_AND_DYNARUN_V3_HAVE));
        return RC_OK;
    }
    info(o, TS(ST_DYNARUNFIX_HAS_BEEN_UNINSTALLED));
    return RC_OK;
}

