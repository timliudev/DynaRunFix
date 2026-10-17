// Part of setup.c (other accounts' hives): included by it, in one compilation unit; never compiled on its own.
BOOL enable_privilege(const WCHAR *name)
{
    HANDLE t; TOKEN_PRIVILEGES tp; BOOL r = FALSE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &t)) return FALSE;
    tp.PrivilegeCount = 1; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid))
        r = AdjustTokenPrivileges(t, FALSE, &tp, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(t);
    return r;
}

// One of the user's shell folders, from their registry (redirected or localized folders, e.g. XP's
// "「開始」功能表"); <profile>\<def> when it is not set. The values come from another user's hive: all copies are bounded.
// out holds cch characters; the result is shorter than cch or empty (then the caller skips the folder).
static void user_folder(HKEY hive, const WCHAR *profile, const WCHAR *name, const WCHAR *def, WCHAR *out, int cch)
{
    static WCHAR v[MAX_PATH], t[2 * MAX_PATH + 16]; DWORD n;
    out[0] = 0;
    if (hive && reg_str(hive, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders", name, v, MAX_PATH, 0)) {
        t[0] = 0;
        if (!lstrcmpiW(lstrcpynW(t, v, 14), L"%USERPROFILE%")) {          // that user's, not ours
            if (lstrlenW(profile) + lstrlenW(v + 13) + 1 <= (int)(sizeof(t) / sizeof(t[0]))) cat3(t, profile, v + 13, NULL); else t[0] = 0;
        }
        else lstrcpyW(t, v);                                               // v is at most MAX_PATH
        if (t[0]) {
            n = ExpandEnvironmentStringsW(t, out, cch);
            if (n && n <= (DWORD)cch) return;
        }
        out[0] = 0;
    }
    if (lstrlenW(profile) + 1 + lstrlenW(def) < cch) cat3(out, profile, L"\\", def);
}

#define HIVE_PREFIX L"DynaRunFix-uninstall"
#define FOLDER_CCH (2 * MAX_PATH)            // buffer of a folder path; 64 characters of it stay free for what is appended

// A hive that an earlier uninstall loaded and could not unload (it was killed) would keep that user's NTUSER.DAT locked.
static void unload_leftover_hives(void)
{
    static WCHAR names[16][80]; WCHAR name[260]; DWORD i, n; int k = 0;
    for (i = 0; k < 16 && (n = 260, !RegEnumKeyExW(HKEY_USERS, i, name, &n, NULL, NULL, NULL, NULL)); i++)
        if (n >= 20 && n < 80 && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, name, 20, HIVE_PREFIX, 20) == CSTR_EQUAL) lstrcpyW(names[k++], name);
    while (k) RegUnLoadKeyW(HKEY_USERS, names[--k]);
}

static void unretarget_user(const WCHAR *sid, const WCHAR *profile)
{
    static int seq;
    static WCHAR p[FOLDER_CCH], sub[300], tmphive[80]; HKEY hive = NULL; BOOL loaded = FALSE; int id;
    wsprintfW(tmphive, HIVE_PREFIX L"-%lu-%lu-%d", GetCurrentProcessId(), GetTickCount(), seq++);     // unique: never a name another load uses
    if (lstrlenW(profile) + 12 >= MAX_PATH) return;                                  // (the caller checked; NTUSER.DAT must fit)
    if (RegOpenKeyExW(HKEY_USERS, sid, 0, KEY_READ | KEY_WRITE, &hive)) {          // not signed in: load their hive
        hive = NULL;
        if (!RegLoadKeyW(HKEY_USERS, tmphive, cat3(p, profile, L"\\NTUSER.DAT", NULL)) &&
            !RegOpenKeyExW(HKEY_USERS, tmphive, 0, KEY_READ | KEY_WRITE, &hive)) loaded = TRUE;
        else hive = NULL;
    }
    for (id = 0; id < SF_COUNT; id++) {                                               // the same folders as unretarget_shell, of their account
        if (!(SF[id].uses & SFU_UNRET)) continue;
        user_folder(hive, profile, SF[id].reg, SF[id].rel, p, FOLDER_CCH - 64);
        if (!p[0]) continue;
        if (SF[id].sub) lstrcatW(p, SF[id].sub);
        unretarget(p, (SF[id].deep & SFU_UNRET) != 0);
    }
    if (hive) {
        drop_run(hive, TRUE); drop_policy(hive);                                    // start at sign-in, Windows 11 pin
        // their backups are of no use any more
        RegDeleteKeyW(hive, cat3(sub, USERKEY, L"\\Shortcuts", NULL));
        RegDeleteKeyW(hive, USERKEY);
        RegCloseKey(hive);
    }
    if (loaded) RegUnLoadKeyW(HKEY_USERS, tmphive);
}

// skip: the user who runs the uninstall; their own shortcuts, backups and Run value are put back by the user-side stage
static void unretarget_all_users(const WCHAR *skip)
{
    static const WCHAR LIST[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList";
    static WCHAR sid[200], key[300], raw[MAX_PATH], profile[MAX_PATH]; HKEY k; DWORD i, n;
    enable_privilege(L"SeRestorePrivilege"); enable_privilege(L"SeBackupPrivilege");   // to load the hives of users not signed in
    unload_leftover_hives();
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LIST, 0, KEY_READ | WOW, &k)) return;       // the 64-bit view: where the system keeps the profiles
    for (i = 0; n = 200, !RegEnumKeyExW(k, i, sid, &n, NULL, NULL, NULL, NULL); i++) {
        // people (local, domain, Microsoft Entra), not service accounts
        if (lstrcmpiW(lstrcpynW(key, sid, 9), L"S-1-5-21") && lstrcmpiW(lstrcpynW(key, sid, 9), L"S-1-12-1")) continue;
        if (skip && skip[0] && !lstrcmpiW(sid, skip)) continue;
        if (!reg_str(HKEY_LOCAL_MACHINE, cat3(key, LIST, L"\\", sid), L"ProfileImagePath", raw, MAX_PATH, WOW)) continue;
        if (ExpandEnvironmentStringsW(raw, profile, MAX_PATH) - 1 >= MAX_PATH || !exists(profile)) continue;
        unretarget_user(sid, profile);
    }
    RegCloseKey(k);
}

