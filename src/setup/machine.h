// Part of setup.c (machine stage): included by it, in one compilation unit; never compiled on its own.
/* ---------- stages ---------- */

// the command line of the elevated stage is not trusted: a user SID is a SID of a real account with a profile on this machine
BOOL valid_user_sid(const WCHAR *sid)
{
    static WCHAR key[300]; PSID s; BOOL ok = FALSE;
    if (lstrlenW(sid) < 8 || lstrlenW(sid) > 120 || (lstrcmpiW(lstrcpynW(key, sid, 9), L"S-1-5-21") && lstrcmpiW(lstrcpynW(key, sid, 9), L"S-1-12-1"))) return FALSE;
    if (!ConvertStringSidToSidW(sid, &s)) return FALSE;
    LocalFree(s);
    cat3(key, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\", sid, NULL);
    { HKEY k; if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE | WOW, &k)) { RegCloseKey(k); ok = TRUE; } }
    return ok;
}

static int machine_install_locked(const install_opts *o, const WCHAR *sid)
{
    static WCHAR p[MAX_PATH + 16];
    HKEY k; trust_ctx t;
    if (!valid_user_sid(sid)) { error2(o, TS(ST_THE_USER_ACCOUNT_IS_NOT), sid); return RC_FAIL; }
    zero(&t, sizeof(t)); lstrcpynW(t.user_sid, sid, 200);
    if (still_running(o, L"install")) return RC_RUNNING;   // checked again here: DynaRun may have been started since
    if (!check_exe(&t)) { error2(o, TS(ST_DYNARUN_V3_EXE_WAS_NOT), g_exe); return RC_FAIL; }
    CreateDirectoryW(g_dir, NULL);
    if (!extract(1, g_launcher) || !extract(2, cat3(p, g_dir, L"\\dynafix.dll", NULL)) ||
        !extract(4, cat3(p, g_dir, L"\\LICENSE-miniz.txt", NULL)) || !extract(5, cat3(p, g_dir, L"\\LICENSE.txt", NULL))) {
        error2(o, TS(ST_CANNOT_WRITE_THE_PROGRAM_FILES), g_dir);
        return RC_FAIL;
    }
    {   // Locale Emulator, resources 10.. (see setup.rc)
        static const WCHAR *le[] = { L"LEProc.exe", L"LoaderDll.dll", L"LocaleEmulator.dll", L"LECommonLibrary.dll",
                                     L"LEConfig.xml", L"COPYING", L"COPYING.LESSER", L"THIRD-PARTY.txt" };
        int i;
        CreateDirectoryW(cat3(p, g_dir, L"\\le", NULL), NULL);
        for (i = 0; i < (int)(sizeof(le) / sizeof(le[0])); i++)
            if (!extract(10 + i, cat3(p, g_dir, L"\\le\\", le[i]))) {
                error2(o, TS(ST_CANNOT_WRITE_THE_PROGRAM_FILES), p);
                return RC_FAIL;
            }
    }
    cat3(p, g_dir, L"\\DynaRunFix-Setup.exe", NULL);
    if (lstrcmpiW(p, g_self) && ((!CopyFileW(g_self, p, FALSE) && !(move_aside(p) && CopyFileW(g_self, p, FALSE))) || !same_file(g_self, p))) {
        error2(o, TS(ST_CANNOT_COPY_THE_UNINSTALLER), p);
        return RC_FAIL;
    }
    driver_machine_stage(p);                            // the Start menu shortcut to the driver installation (p: the installed setup)
    if (!app_key_create(APP_MACHINE, &k)) return RC_FAIL;
    set_str(k, L"InstallDir", g_dir);
    if (t.exe_trusted) set_str(k, L"DynaRunExe", g_exe);     // a path that ordinary users can change is not recorded for the elevated stages (uninstall) to use
    else RegDeleteValueW(k, L"DynaRunExe");

    touch_report(o, recover_exe_time());                   // a time stamp an earlier run could not put back
    // the per-user COM registrations are not copied to HKLM any more (records of older versions are left alone)
    if (!t.exe_trusted) setup_log(L"DynaRun's folder or exe can be changed by ordinary users (or its path contains a link): no manifest was written");

    // garbled text under "Beta: Use Unicode UTF-8": external manifest with activeCodePage=Legacy
    if (GetACP() == CP_UTF8 && t.exe_trusted && !exists(cat3(p, g_exe, L".manifest", NULL)) && extract(3, p)) {
        set_dword(k, L"ManifestInstalled", 1);
        touch_report(o, touch(g_exe));
    }

    { HKEY b; if (!RegCreateKeyExW(k, L"Shortcuts", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &b, NULL)) { fix_shortcuts(TRUE, b); RegCloseKey(b); } }
    taskbar_policy(o, sid, k);
    RegCloseKey(k);

    if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, UNINSTKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) {
        set_str(k, L"DisplayName", L"DynaRunFix");
        set_str(k, L"DisplayVersion", WIDEN(DRF_DISPLAY));
        set_str(k, L"Publisher", L"DynaRunFix (github.com/timliudev/DynaRunFix)");
        set_str(k, L"URLInfoAbout", L"https://github.com/timliudev/DynaRunFix");
        set_str(k, L"InstallLocation", g_dir);
        cat3(p, g_dir, L"\\DynaRunFix-Setup.exe,0", NULL); set_str(k, L"DisplayIcon", p);   // our icon, not DynaRun's
        cat3(p, L"\"", g_dir, L"\\DynaRunFix-Setup.exe\" /uninstall"); set_str(k, L"UninstallString", p);
        set_dword(k, L"NoModify", 1); set_dword(k, L"NoRepair", 1); set_dword(k, L"EstimatedSize", 100);
        RegCloseKey(k);
    }
    return RC_OK;
}

static int machine_install(const install_opts *o, const WCHAR *sid)
{
    int rc;
    if (!stage_lock()) { error2(o, busy_text(), NULL); return RC_FAIL; }
    rc = machine_install_locked(o, sid);
    stage_unlock();
    return rc;
}

// Text of the manifest that DynaRunFix 1.3.6 wrote (its embedded file had a different comment and the assembly version 3.26.0.0).
static const char MANIFEST_V136[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<!--\n"
    "  External application manifest for \"DynaRun V3.exe\" (the executable has no embedded manifest,\n"
    "  so Windows uses this file when it sits next to the exe).\n"
    "\n"
    "  activeCodePage=Legacy makes DynaRun use the system locale's ANSI code page (e.g. 950/Big5 for\n"
    "  zh-TW) even when \"Beta: Use Unicode UTF-8 for worldwide language support\" is enabled.\n"
    "  VB6 programs store translated strings in the ANSI code page, so with UTF-8 (65001) they show\n"
    "  garbled text.\n"
    "-->\n"
    "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
    "  <assemblyIdentity type=\"win32\" name=\"DynaPro.DynaRunV3\" version=\"3.26.0.0\" processorArchitecture=\"x86\"/>\n"
    "  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n"
    "    <windowsSettings>\n"
    "      <activeCodePage xmlns=\"http://schemas.microsoft.com/SMI/2019/WindowsSettings\">Legacy</activeCodePage>\n"
    "    </windowsSettings>\n"
    "  </application>\n"
    "</assembly>\n";

// The file on disk is d, line endings (CR) not counted: a checkout with CRLF and one with LF build the same manifest.
static BOOL same_text(const WCHAR *p, const void *d, DWORD n)
{
    DWORD got, i = 0, j = 0; BYTE *f = read_file(p, &got); const BYTE *x = d; BOOL r = f != NULL;
    while (r) {
        while (i < got && f[i] == '\r') i++;
        while (j < n && x[j] == '\r') j++;
        if (i >= got || j >= n) { r = i >= got && j >= n; break; }
        r = f[i++] == x[j++];
    }
    release(f);
    return r;
}

// skip_sid: the user who started the uninstall (their own part is done by the user-side stage afterwards)
// The manifest next to DynaRun V3.exe is deleted only when it is a file DynaRunFix wrote: the same text as the embedded one or, for
// files of earlier versions, the same with the older assembly identity version (3.26.0.0) or the text of release 1.3.6.
static BOOL manifest_is_ours(const WCHAR *p)
{
    static const char NEWV[] = "version=\"1.0.0.0\"", OLDV[] = "version=\"3.26.0.0\"";
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(3), (LPCWSTR)RT_RCDATA); const char *d; char *c; DWORD n, i, nl = sizeof(NEWV) - 1, ol = sizeof(OLDV) - 1, j; BOOL ok = FALSE;
    if (!r) return FALSE;
    d = LockResource(LoadResource(NULL, r)); n = SizeofResource(NULL, r);
    if (!d || !n) return FALSE;
    if (same_text(p, d, n)) return TRUE;
    if (same_text(p, MANIFEST_V136, sizeof(MANIFEST_V136) - 1)) return TRUE;
    for (i = 0; i + nl <= n; i++) {
        for (j = 0; j < nl && d[i + j] == NEWV[j]; j++);
        if (j < nl) continue;
        if ((c = alloc(n + ol))) {
            for (j = 0; j < i; j++) c[j] = d[j];
            for (j = 0; j < ol; j++) c[i + j] = OLDV[j];
            for (j = i + nl; j < n; j++) c[j - nl + ol] = d[j];
            ok = same_text(p, c, n - nl + ol);
            release(c);
        }
        break;
    }
    return ok;
}

// The user's Windows 11 taskbar policy (sid: the user's SID; their hive is loaded when they are signed in): removed when it is ours
static void drop_policy_of(const WCHAR *sid)
{
    HKEY k;
    if (!sid || !sid[0] || !valid_user_sid(sid)) return;
    if (!RegOpenKeyExW(HKEY_USERS, sid, 0, KEY_READ | KEY_WRITE, &k)) { drop_policy(k); RegCloseKey(k); }
}

static int machine_uninstall(const install_opts *o, const WCHAR *skip_sid)
{
    static WCHAR p[MAX_PATH + 16], polsid[200]; static const WCHAR KEEP[] = L"com-copied.txt\0com-backup\0com-skipped.txt\0setup.log\0";   // the record of the COM copies stays
    HKEY k; DWORD man = 0, n = 4; BOOL keep = FALSE, deleted = FALSE, stuck = FALSE; trust_ctx t;
    if (!stage_lock()) { error2(o, busy_text(), NULL); return RC_FAIL; }
    zero(&t, sizeof(t));
    if (skip_sid && skip_sid[0] && valid_user_sid(skip_sid)) lstrcpynW(t.user_sid, skip_sid, 200); else skip_sid = NULL;
    if (o->close) close_programs();                  // the user agreed (or /close): DynaRun keeps files of the folder open
    if (!g_exe[0]) locate_dynarun();                 // not recorded (an untrusted path is not): the default folders
    check_exe(&t);                                   // g_exe is from HKLM; t.exe_trusted says whether to run it
    if (touch_report(o, recover_exe_time()) == TOUCH_STUCK) stuck = TRUE;
    restore_shortcuts(HKEY_LOCAL_MACHINE, APPKEY);
    unretarget_shell(TRUE);
    // the taskbar policy (Windows 11) of the user who runs the uninstall and of the user the install recorded: read before the key goes
    polsid[0] = 0; reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"TaskbarPolicySid", polsid, 200, 0);
    drop_policy_of(skip_sid); drop_policy_of(polsid);
    unretarget_all_users(skip_sid);
    if (app_key_open(APP_MACHINE, KEY_READ, &k)) { RegQueryValueExW(k, L"ManifestInstalled", NULL, NULL, (BYTE *)&man, &n); RegCloseKey(k); }
    if (man == 1 && g_exe[0] && !t.exe_trusted && exists(cat3(p, g_exe, L".manifest", NULL)))
        setup_log(L"the manifest next to DynaRun V3.exe was left in place: DynaRun's folder or exe can be changed by ordinary users, so nothing is deleted or scheduled for deletion there");
    else if (man == 1 && g_exe[0] && exists(cat3(p, g_exe, L".manifest", NULL))) {
        if (!manifest_is_ours(p)) setup_log(L"the manifest next to DynaRun V3.exe is not the file DynaRunFix wrote (it was changed): left in place");
        else if (DeleteFileW(p)) deleted = TRUE;
        else if (move_aside(p)) {                    // in use: renamed at once (a reinstall before the restart writes its own file), the renamed one goes at the next restart
            deleted = TRUE;
            warn(o, TS(ST_THE_MANIFEST_FILE_NEXT_TO));
        }
        else {                                       // cannot even be renamed: it goes at the next restart, and the record that it is ours stays
            MoveFileExW(p, NULL, MOVEFILE_DELAY_UNTIL_REBOOT); keep = TRUE;
            warn(o, TS(ST_THE_MANIFEST_FILE_NEXT_TO));
        }
    }
    if (deleted) { if (t.exe_trusted) { if (touch_report(o, touch(g_exe)) == TOUCH_STUCK) stuck = TRUE; } else setup_log(L"the manifest state was not refreshed: DynaRun's folder or exe can be changed by ordinary users"); }
    // Before the key goes: the driver part ends a waiting task and resumes BitLocker if DynaRunFix suspended it (the flag is in the key).
    // An installed driver stays.
    driver_machine_uninstall();
    // stuck: the original time stamp of DynaRun V3.exe could not be put back; ExeOrigTimePath / ExeOrigTime stay for the next install or
    // uninstall (touch_report says so) - the key is not deleted then
    if (!keep && !stuck) RegDeleteKeyW(HKEY_LOCAL_MACHINE, APPKEY);
    else if (app_key_open(APP_MACHINE, KEY_SET_VALUE, &k)) {     // what is no longer needed; ManifestInstalled, DynaRunExe (and the time stamp record) stay
        RegDeleteValueW(k, L"InstallDir"); RegDeleteValueW(k, L"TaskbarPolicy"); RegDeleteValueW(k, L"TaskbarPolicySid"); RegCloseKey(k);
    }
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, UNINSTKEY);
    delete_tree(g_dir, KEEP, 0);
    stage_unlock();
    return RC_OK;
}

