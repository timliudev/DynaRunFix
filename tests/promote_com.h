// promote_com_in: copies COM registrations from HKU\<sid>\Software\Classes to HKLM\SOFTWARE\Classes, each only after the checks in src\setup.c
// (clsid_ok, progid_ok, typelib_ok, ...). The installer no longer does this, so the function is not part of the shipped code; it is kept here, compiled
// by tests\test_hardening.c (include it after src\setup.c), so that the checks stay exercised.

static const WCHAR *g_export_root = L"HKLM\\SOFTWARE\\Classes\\";     // where reg_export reads from (a test points it elsewhere)

static void reg_export(const WCHAR *key, const WCHAR *file)   // backup with the native reg.exe
{
    static WCHAR exe[MAX_PATH], cmd[1024]; STARTUPINFOW si; PROCESS_INFORMATION pi;
    GetWindowsDirectoryW(exe, MAX_PATH); lstrcatW(exe, L"\\Sysnative\\reg.exe");
    if (!exists(exe)) { GetWindowsDirectoryW(exe, MAX_PATH); lstrcatW(exe, L"\\System32\\reg.exe"); }
    wsprintfW(cmd, L"reg.exe export \"%s%s\" \"%s\"", g_export_root, key, file);
    zero(&si, sizeof(si)); si.cb = sizeof(si);
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
}

static void skipped(const WCHAR *key, const WCHAR *why)
{
    static WCHAR log[MAX_PATH], line[700];
    wsprintfW(line, L"%.400s: %s", key, why);
    append_line(cat3(log, g_dir, L"\\com-skipped.txt", NULL), line);
}

// hu: the user's Software\Classes, hm: HKLM\SOFTWARE\Classes (both opened with the 64-bit view). Returns the number of keys copied.
static int promote_com_in(HKEY hu, HKEY hm, WCHAR roots[][MAX_PATH], int nr)
{
    static const WCHAR *const views[] = { L"WOW6432Node\\CLSID", L"CLSID" };
    static WCHAR path[600], rel[300], id[128], v[MAX_PATH], prog[300], log[MAX_PATH], bak[MAX_PATH], line[700], tl[128], tlkey[200], bn[48];
    const WCHAR *why; HKEY list, s, d; DWORD i, n; int vi, j, copied = 0; WCHAR *dot; BOOL had, ok;

    g_nkeys = 0;
    for (vi = 0; vi < 2; vi++) {
        if (RegOpenKeyExW(hu, views[vi], 0, KEY_READ | WOW, &list)) continue;
        for (i = 0; n = 128, !RegEnumKeyExW(list, i, id, &n, NULL, NULL, NULL, NULL); i++) {
            cat3(rel, views[vi], L"\\", id);
            if (!clsid_ok(hu, rel, roots, nr, &why)) { if (lstrcmpW(why, L"no server")) skipped(rel, why); continue; }
            if (!reg_str(hm, cat3(path, rel, L"\\InprocServer32", NULL), NULL, v, MAX_PATH, WOW) &&
                !reg_str(hm, cat3(path, rel, L"\\LocalServer32", NULL), NULL, v, MAX_PATH, WOW)) add_key(rel, K_CLSID, id);
            if (reg_str(hu, cat3(path, rel, L"\\ProgID", NULL), NULL, prog, 250, WOW)) {
                for (j = 0; j < 2; j++) {     // "Lib.Class.1" and its version-independent "Lib.Class"
                    if (j) { for (dot = prog + lstrlenW(prog); dot > prog && dot[-1] >= '0' && dot[-1] <= '9'; dot--); if (dot > prog && dot[-1] == '.' && *dot) dot[-1] = 0; else break; }
                    if (has_key(hu, prog) && !reg_str(hm, cat3(path, prog, L"\\CLSID", NULL), NULL, v, MAX_PATH, WOW)) {
                        if (progid_ok(hu, prog, id, &why)) add_key(prog, K_PROGID, id); else skipped(prog, why);
                    }
                }
            }
            if (reg_str(hu, cat3(path, rel, L"\\TypeLib", NULL), NULL, tl, 128, WOW) &&
                has_key(hu, cat3(tlkey, L"TypeLib\\", tl, NULL)) && !typelib_complete(hm, tl)) {
                if (typelib_ok(hu, tlkey, roots, nr, &why)) add_key(tlkey, K_TYPELIB, NULL); else skipped(tlkey, why);
            }
        }
        RegCloseKey(list);
    }
    cat3(log, g_dir, L"\\com-copied.txt", NULL);
    for (j = 0; j < g_nkeys; j++) {
        had = has_key(hm, g_keys[j]);
        if (had) {                              // keep what was there before
            cat3(bak, g_dir, L"\\com-backup", NULL); CreateDirectoryW(bak, NULL);
            wsprintfW(bn, L"\\%lu-%d.reg", GetTickCount(), j);
            if (lstrlenW(bak) + lstrlenW(bn) < MAX_PATH) { lstrcatW(bak, bn); reg_export(g_keys[j], bak); }   // bounded: a long install path just skips the backup file
        }
        if (RegOpenKeyExW(hu, g_keys[j], 0, KEY_READ | WOW, &s)) continue;
        if (!RegCreateKeyExW(hm, g_keys[j], 0, NULL, 0, KEY_ALL_ACCESS | WOW, NULL, &d, NULL)) {
            copy_tree(s, d); RegCloseKey(d);
            // the copy in HKLM is what counts: only administrators can change it from here on
            ok = g_kinds[j] == K_CLSID ? clsid_ok(hm, g_keys[j], roots, nr, &why) :
                 g_kinds[j] == K_PROGID ? progid_ok(hm, g_keys[j], g_ids[j], &why) : typelib_ok(hm, g_keys[j], roots, nr, &why);
            if (ok) { append_line(log, cat3(line, L"HKLM\\SOFTWARE\\Classes\\", g_keys[j], NULL)); copied++; }
            else {
                skipped(g_keys[j], why);
                if (!had) delete_key_tree(hm, g_keys[j]);       // the key was not there before: it is not there now
            }
        }
        RegCloseKey(s);
    }
    free_keys();
    return copied;
}
