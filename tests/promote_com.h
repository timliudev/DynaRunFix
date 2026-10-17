// promote_com_in: copies COM registrations from HKU\<sid>\Software\Classes to HKLM\SOFTWARE\Classes, each only after the checks in src\setup.c
// (clsid_ok, progid_ok, typelib_ok, ...). The installer no longer does this, so the function is not part of the shipped code; it is kept here, compiled
// by tests\test_hardening.c (include it after src\setup.c), so that the checks stay exercised.

// The checks and helpers below (moved here from src\setup.c: the installer no longer calls them) are what promote_com_in and the tests use.

// DynaRun's installer registers its ActiveX controls for the installing user only (HKCU). Elevated DynaRun needs them in
// HKLM. A registration is copied only when every file it names (InprocServer32, LocalServer32, and the type library) is
//   - a regular file that exists, given as an absolute path on a local drive (environment variables are expanded; relative,
//     UNC and device paths are refused),
//   - inside DynaRun's own folder (the one the installer found; only when that folder is not changeable by ordinary users)
//     or inside the Windows System32 / SysWOW64 folder,
//   - reached through no link (junction, symbolic link) below that folder, and
//   - neither the file nor its folder changeable by ordinary users (owner, ACL: see user_writable).
// No file name is trusted. The registration is checked in the user's key before the copy and again in HKLM after it (the user
// may change their key in between); a copy that fails the second check is removed.

enum { K_CLSID = 1, K_PROGID, K_TYPELIB };
#define MAXKEYS 1024
static WCHAR *g_keys[MAXKEYS], *g_ids[MAXKEYS]; static int g_kinds[MAXKEYS], g_nkeys;

static void add_key(const WCHAR *k, int kind, const WCHAR *id)
{
    int i;
    for (i = 0; i < g_nkeys; i++) if (!lstrcmpiW(g_keys[i], k)) return;
    if (g_nkeys < MAXKEYS && (g_keys[g_nkeys] = alloc((lstrlenW(k) + 1) * sizeof(WCHAR)))) {
        lstrcpyW(g_keys[g_nkeys], k); g_kinds[g_nkeys] = kind;
        g_ids[g_nkeys] = id && (g_ids[g_nkeys] = alloc((lstrlenW(id) + 1) * sizeof(WCHAR))) ? lstrcpyW(g_ids[g_nkeys], id) : NULL;
        g_nkeys++;
    }
}

static void free_keys(void)
{
    while (g_nkeys > 0) { g_nkeys--; release(g_keys[g_nkeys]); release(g_ids[g_nkeys]); g_keys[g_nkeys] = g_ids[g_nkeys] = NULL; }
}

static BOOL has_key(HKEY root, const WCHAR *p)
{ HKEY k; if (RegOpenKeyExW(root, p, 0, KEY_READ | WOW, &k)) return FALSE; RegCloseKey(k); return TRUE; }

// roots: the folders a COM server may be in
static int g_com_nfolder;     // roots[0 .. g_com_nfolder-1] are DynaRun's folder (any file type), the rest are system folders (.ocx servers only)

static int com_roots(WCHAR roots[][MAX_PATH])
{
    WCHAR *e; int n = 0;
    g_com_nfolder = 0;
    if (g_exe_trusted && g_exe[0]) {
        lstrcpyW(roots[n], g_exe); for (e = roots[n] + lstrlenW(roots[n]); e > roots[n] && *e != '\\'; e--); *e = 0;
        if (roots[n][0]) g_com_nfolder = ++n;
    }
    if (GetWindowsDirectoryW(roots[n], MAX_PATH - 20)) {
        lstrcpyW(roots[n + 1], roots[n]);
        lstrcatW(roots[n], L"\\System32"); lstrcatW(roots[n + 1], L"\\SysWOW64");
        n += exists(roots[n + 1]) ? 2 : 1;
    }
    return n;
}

static BOOL under(const WCHAR *path, const WCHAR *root)      // path is a file or folder below root
{
    int n = lstrlenW(root);
    return n && lstrlenW(path) > n + 1 && path[n] == '\\' && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, path, n, root, n) == CSTR_EQUAL;
}

// raw: the default value of an InprocServer32 / LocalServer32 key (maybe quoted, with arguments, with %variables%) or of a
// type library's win32 key (maybe followed by "\<resource number>")
static BOOL com_path_ok(const WCHAR *raw, BOOL typelib, WCHAR roots[][MAX_PATH], int nr, const WCHAR **why)
{
    static WCHAR q[2 * MAX_PATH], e[2 * MAX_PATH], full[MAX_PATH], lng[MAX_PATH], par[MAX_PATH];
    WCHAR *p, *s, c; int i, root = -1; DWORD a; HANDLE h; BOOL quoted;
    *why = L"";
    lstrcpynW(q, raw, 2 * MAX_PATH);
    for (p = q; *p == ' '; p++);
    quoted = *p == '"';
    if (quoted) { p++; for (s = p; *s && *s != '"'; s++); *s = 0; }
    i = (int)ExpandEnvironmentStringsW(p, e, 2 * MAX_PATH);
    if (i <= 0 || i > 2 * MAX_PATH || lstrlenW(e) >= MAX_PATH - 1) { *why = L"path empty or too long"; return FALSE; }
    if (typelib && (a = GetFileAttributesW(e)) == INVALID_FILE_ATTRIBUTES) {      // "file.tlb\2": a resource number
        for (s = e + lstrlenW(e); s > e && *s != '\\'; s--);
        if (s > e && s[1]) { for (p = s + 1; *p >= '0' && *p <= '9'; p++); if (!*p) *s = 0; }
    } else if (!quoted && GetFileAttributesW(e) == INVALID_FILE_ATTRIBUTES)         // unquoted, with arguments: "C:\x\a.exe /Embedding"
        for (s = e; *s; s++) if (s[0] == ' ' && (s[1] == '/' || s[1] == '-')) { *s = 0; break; }
    for (s = e + lstrlenW(e); s > e && s[-1] == ' '; s--) s[-1] = 0;
    if (!((e[0] >= 'A' && e[0] <= 'Z') || (e[0] >= 'a' && e[0] <= 'z')) || e[1] != ':' || e[2] != '\\') { *why = L"not an absolute local path"; return FALSE; }
    for (s = e + 2; *s; s++)
        if (*s == ':' || *s == '*' || *s == '?' || *s == '<' || *s == '>' || *s == '|' || *s == '"' || *s == '%') { *why = L"unusable character in the path"; return FALSE; }
    i = (int)GetFullPathNameW(e, MAX_PATH, full, NULL);
    if (i <= 0 || i >= MAX_PATH) { *why = L"path too long"; return FALSE; }
    if (!GetLongPathNameW(full, lng, MAX_PATH)) { *why = L"file does not exist"; return FALSE; }
    for (i = 0; i < nr; i++) if (under(lng, roots[i])) root = i;
    if (root < 0) { *why = L"outside DynaRun's folder and the system folders"; return FALSE; }
    // System32 / SysWOW64: only ActiveX controls (.ocx; a type library may also be .tlb / .olb), so that other software's DLLs
    // (shell extensions and the like) are never made machine-wide. Any file type inside DynaRun's own folder.
    if (root >= g_com_nfolder) {
        s = lng + lstrlenW(lng);
        if (!(lstrlenW(lng) > 4 && (!lstrcmpiW(s - 4, L".ocx") || (typelib && (!lstrcmpiW(s - 4, L".tlb") || !lstrcmpiW(s - 4, L".olb")))))) {
            *why = L"in a system folder and not an .ocx file"; return FALSE;
        }
    }
    // no link (junction, symbolic link) anywhere below the folder, and the end must be a plain file
    for (s = lng + lstrlenW(roots[root]) + 1; ; s++) {
        if (*s != '\\' && *s) continue;
        c = *s; *s = 0; a = GetFileAttributesW(lng); *s = c;
        if (a == INVALID_FILE_ATTRIBUTES) { *why = L"file does not exist"; return FALSE; }
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) { *why = L"a link (junction or symbolic link) is in the path"; return FALSE; }
        if (!c) { if (a & FILE_ATTRIBUTE_DIRECTORY) { *why = L"a folder, not a file"; return FALSE; } break; }
    }
    h = CreateFileW(lng, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { *why = L"file cannot be opened"; return FALSE; }
    i = GetFileType(h) == FILE_TYPE_DISK;
    CloseHandle(h);
    if (!i) { *why = L"not a disk file"; return FALSE; }
    lstrcpyW(par, lng); for (s = par + lstrlenW(par); s > par && *s != '\\'; s--); *s = 0;
    if (g_is_writable(lng) || g_is_writable(par)) { *why = L"the file or its folder can be changed by ordinary users"; return FALSE; }
    return TRUE;
}

// 0: no default value, 1: a string of usable size, 2: there but not usable
static int value_state(HKEY root, const WCHAR *path)
{
    HKEY k; DWORD type, n = 0; LONG r;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | WOW, &k)) return 0;
    r = RegQueryValueExW(k, NULL, NULL, &type, NULL, &n);
    RegCloseKey(k);
    if (r == ERROR_FILE_NOT_FOUND) return 0;
    if (r) return 2;
    return (type == REG_SZ || type == REG_EXPAND_SZ) && n && n < 2 * MAX_PATH * sizeof(WCHAR) ? 1 : 2;
}

static BOOL has_subkey(HKEY root, const WCHAR *rel, const WCHAR *const *names, const WCHAR **which)
{
    HKEY k; DWORD i, n; WCHAR name[260]; int j; BOOL r = FALSE;
    if (RegOpenKeyExW(root, rel, 0, KEY_READ | WOW, &k)) return FALSE;
    for (i = 0; !r && (n = 260, !RegEnumKeyExW(k, i, name, &n, NULL, NULL, NULL, NULL)); i++)
        for (j = 0; names[j]; j++) if (!lstrcmpiW(name, names[j])) { r = TRUE; if (which) *which = names[j]; }
    RegCloseKey(k);
    return r;
}

// CLSID\{...}: every server it has must pass com_path_ok, and it has no handler, redirection or surrogate
static BOOL clsid_ok(HKEY root, const WCHAR *rel, WCHAR roots[][MAX_PATH], int nr, const WCHAR **why)
{
    static const WCHAR *const bad[] = { L"InprocHandler", L"InprocHandler32", L"InprocServer", L"LocalServer", L"TreatAs", L"AutoTreatAs", L"ScriptletURL", L"Elevation", NULL };
    static const WCHAR *const srv[] = { L"InprocServer32", L"LocalServer32", NULL };
    static WCHAR path[600], v[2 * MAX_PATH]; HKEY k; int j, st, found = 0; LONG r;
    *why = L"";
    if (has_subkey(root, rel, bad, NULL)) { *why = L"has a handler or redirection key"; return FALSE; }
    if (RegOpenKeyExW(root, rel, 0, KEY_QUERY_VALUE | WOW, &k)) { *why = L"cannot be opened"; return FALSE; }
    r = RegQueryValueExW(k, L"AppID", NULL, NULL, NULL, NULL);
    RegCloseKey(k);
    if (r != ERROR_FILE_NOT_FOUND) { *why = L"names an AppID (surrogate or remote activation)"; return FALSE; }
    for (j = 0; srv[j]; j++) {
        cat3(path, rel, L"\\", srv[j]);
        if (!(st = value_state(root, path))) continue;
        if (st == 2 || !reg_str(root, path, NULL, v, 2 * MAX_PATH, WOW)) { *why = L"server path unusable"; return FALSE; }
        found++;
        if (!com_path_ok(v, FALSE, roots, nr, why)) return FALSE;
    }
    if (!found) *why = L"no server";
    return found > 0;
}

// ProgID: no shell verbs or other hooks; it points back to the CLSID that is being promoted
static BOOL progid_ok(HKEY root, const WCHAR *prog, const WCHAR *id, const WCHAR **why)
{
    static const WCHAR *const bad[] = { L"shell", L"shellex", L"protocol", L"OLEScript", L"ScriptEngine", L"PersistentHandler", L"DefaultIcon", NULL };
    static WCHAR path[600], v[128];
    *why = L"";
    if (has_subkey(root, prog, bad, NULL)) { *why = L"ProgID has shell or handler keys"; return FALSE; }
    if (!reg_str(root, cat3(path, prog, L"\\CLSID", NULL), NULL, v, 128, WOW) || !id || lstrcmpiW(v, id)) { *why = L"ProgID does not name this CLSID"; return FALSE; }
    return TRUE;
}

// TypeLib\{guid}: every file it names must pass com_path_ok (and there must be one)
static BOOL typelib_ok(HKEY root, const WCHAR *rel, WCHAR roots[][MAX_PATH], int nr, const WCHAR **why)
{
    static const WCHAR *const plat[] = { L"win32", L"win64", NULL };
    static WCHAR path[700], v[2 * MAX_PATH]; HKEY t, k; DWORD i, j, n; WCHAR ver[64], lcid[64]; int p, st, found = 0; BOOL ok = TRUE;
    *why = L"";
    if (RegOpenKeyExW(root, rel, 0, KEY_READ | WOW, &t)) { *why = L"type library cannot be opened"; return FALSE; }
    for (i = 0; ok && (n = 64, !RegEnumKeyExW(t, i, ver, &n, NULL, NULL, NULL, NULL)); i++) {
        if (RegOpenKeyExW(t, ver, 0, KEY_READ | WOW, &k)) continue;
        for (j = 0; ok && (n = 64, !RegEnumKeyExW(k, j, lcid, &n, NULL, NULL, NULL, NULL)); j++)
            for (p = 0; ok && plat[p]; p++) {
                cat3(path, lcid, L"\\", plat[p]);
                if (!(st = value_state(k, path))) continue;
                if (st == 2 || !reg_str(k, path, NULL, v, 2 * MAX_PATH, WOW)) { *why = L"type library path unusable"; ok = FALSE; }
                else { found++; ok = com_path_ok(v, TRUE, roots, nr, why); }
            }
        RegCloseKey(k);
    }
    RegCloseKey(t);
    if (ok && !found) { *why = L"type library names no file"; return FALSE; }
    return ok;
}

static BOOL typelib_complete(HKEY hm, const WCHAR *tlb)   // some version\lcid\win32 has a path
{
    WCHAR p[300], ver[64], lcid[64], v[MAX_PATH]; HKEY t, k; DWORD i, j, n; BOOL r = FALSE;
    if (RegOpenKeyExW(hm, cat3(p, L"TypeLib\\", tlb, NULL), 0, KEY_READ | WOW, &t)) return FALSE;
    for (i = 0; !r && (n = 64, !RegEnumKeyExW(t, i, ver, &n, NULL, NULL, NULL, NULL)); i++) {
        if (RegOpenKeyExW(t, ver, 0, KEY_READ | WOW, &k)) continue;
        for (j = 0; !r && (n = 64, !RegEnumKeyExW(k, j, lcid, &n, NULL, NULL, NULL, NULL)); j++)
            r = reg_str(k, cat3(p, lcid, L"\\win32", NULL), NULL, v, MAX_PATH, WOW) > 0;
        RegCloseKey(k);
    }
    RegCloseKey(t);
    return r;
}

static void copy_tree(HKEY s, HKEY d)
{
    DWORD i, nmax = 0, dmax = 0, n, dn, type; WCHAR *name; BYTE *data; HKEY cs, cd;
    RegQueryInfoKeyW(s, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &nmax, &dmax, NULL, NULL);
    name = alloc((nmax + 300) * sizeof(WCHAR)); data = alloc(dmax + 2);
    if (!name || !data) goto out;
    for (i = 0; n = nmax + 1, dn = dmax, !RegEnumValueW(s, i, name, &n, NULL, &type, data, &dn); i++)
        RegSetValueExW(d, name, 0, type, data, dn);
    for (i = 0; n = 300, !RegEnumKeyExW(s, i, name, &n, NULL, NULL, NULL, NULL); i++) {
        if (RegOpenKeyExW(s, name, 0, KEY_READ | WOW, &cs)) continue;
        if (!RegCreateKeyExW(d, name, 0, NULL, 0, KEY_ALL_ACCESS | WOW, NULL, &cd, NULL)) { copy_tree(cs, cd); RegCloseKey(cd); }
        RegCloseKey(cs);
    }
out:
    release(name); release(data);
}

typedef LONG (WINAPI *RegDeleteKeyExW_t)(HKEY, LPCWSTR, REGSAM, DWORD);

static void delete_key_tree(HKEY root, const WCHAR *key)       // the key and everything below it, in the 64-bit view
{
    RegDeleteKeyExW_t del = (RegDeleteKeyExW_t)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegDeleteKeyExW");
    HKEY k; WCHAR name[260], *sub; DWORD n; int guard = 0;
    if (!RegOpenKeyExW(root, key, 0, KEY_ALL_ACCESS | WOW, &k)) {
        while (guard++ < 10000 && (n = 260, !RegEnumKeyExW(k, 0, name, &n, NULL, NULL, NULL, NULL))) {
            if ((sub = alloc((lstrlenW(key) + n + 2) * sizeof(WCHAR)))) { cat3(sub, key, L"\\", name); delete_key_tree(root, sub); release(sub); }
            else break;
        }
        RegCloseKey(k);
    }
    if (del) del(root, key, WOW, 0); else RegDeleteKeyW(root, key);
}

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
