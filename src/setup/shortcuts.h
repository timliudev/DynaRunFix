// Part of setup.c (shortcut folders and shortcut files): included by it, in one compilation unit; never compiled on its own.
// The installer copies nothing to HKLM\SOFTWARE\Classes (promote_com_in lives in tests\promote_com.h).

/* ---------- shortcuts ---------- */

typedef UINT (WINAPI *MsiGetShortcutTargetW_t)(LPCWSTR, LPWSTR, LPWSTR, LPWSTR);
typedef int (WINAPI *MsiGetComponentPathW_t)(LPCWSTR, LPCWSTR, LPWSTR, DWORD *);
static MsiGetShortcutTargetW_t pMsiGetShortcutTarget;
static MsiGetComponentPathW_t pMsiGetComponentPath;

// Looks up the two MSI functions, once (more calls do nothing). msi.dll is loaded by its full path in the system folder (load_system_dll), never
// searched for. Both pointers stay NULL when msi.dll or a function is missing: classify then skips its check of advertised shortcuts.
static void msi_api_init(void)
{
    static BOOL done; HMODULE msi;
    if (done) return;
    done = TRUE;
    if (!(msi = load_system_dll(L"msi.dll"))) return;
    pMsiGetShortcutTarget = (MsiGetShortcutTargetW_t)GetProcAddress(msi, "MsiGetShortcutTargetW");
    pMsiGetComponentPath = (MsiGetComponentPathW_t)GetProcAddress(msi, "MsiGetComponentPathW");
    if (!pMsiGetComponentPath) pMsiGetShortcutTarget = NULL;
}

static IShellLinkW *load_link(const WCHAR *p)
{
    IShellLinkW *sl; IPersistFile *pf; HRESULT hr = E_FAIL;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) return NULL;
    if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) { hr = IPersistFile_Load(pf, p, STGM_READ); IPersistFile_Release(pf); }
    if (SUCCEEDED(hr)) return sl;
    IShellLinkW_Release(sl);
    return NULL;
}

enum { LNK_OTHER, LNK_DYNARUN, LNK_OURS };
// A shortcut target that is the launcher of some DynaRunFix install: "...\DynaRunFix\DynaRunFix.exe". The path of this install
// (g_launcher) is compared first; this also catches the launcher as an older version or Windows (32-bit program on 64-bit Windows:
// "Program Files" vs "Program Files (x86)") recorded it, so such a shortcut is still taken for ours when it is restored or pointed back.
static BOOL launcher_path_like(const WCHAR *t)
{
    static const WCHAR tail[] = L"\\DynaRunFix\\DynaRunFix.exe"; int n = lstrlenW(t), m = (int)(sizeof(tail) / sizeof(WCHAR)) - 1;
    return n > m && !lstrcmpiW(t + n - m, tail);
}

static int classify(const WCHAR *p)
{
    WCHAR t[MAX_PATH], prod[40], feat[40], comp[40]; DWORD n = MAX_PATH; IShellLinkW *sl = load_link(p); int r = LNK_OTHER;
    if (!sl) return r;
    t[0] = 0;
    IShellLinkW_GetPath(sl, t, MAX_PATH, NULL, 0);
    IShellLinkW_Release(sl);
    if (!lstrcmpiW(t, g_launcher) || launcher_path_like(t)) return LNK_OURS;
    if (!lstrcmpiW(t, g_exe)) return LNK_DYNARUN;
    // the original setup is an MSI: its shortcuts are "advertised" and point to the product, not the exe
    msi_api_init();
    if (pMsiGetShortcutTarget && !pMsiGetShortcutTarget(p, prod, feat, comp) &&
        pMsiGetComponentPath(prod, comp, t, &n) >= 0 && !lstrcmpiW(t, g_exe)) r = LNK_DYNARUN;
    return r;
}

// Windows 7+: an explicit AppUserModelID on the launcher shortcuts, so that a running DynaRun (same ID, set by
// dynafix.dll) groups under the pinned icon; other targets get none.
static void set_appid(IShellLinkW *sl, const WCHAR *id)
{
    IPropertyStore *ps; PROPVARIANT pv; DWORD ma, mi, b;
    os_ver(&ma, &mi, &b);
    if (ma < 6 || (ma == 6 && mi < 1) || FAILED(IShellLinkW_QueryInterface(sl, &IID_IPropertyStore, (void **)&ps))) return;
    zero(&pv, sizeof(pv));
    if (id) { pv.vt = VT_LPWSTR; pv.pwszVal = (LPWSTR)id; }                 // VT_EMPTY removes it
    if (SUCCEEDED(IPropertyStore_SetValue(ps, &PKEY_AUMID, &pv))) IPropertyStore_Commit(ps);
    IPropertyStore_Release(ps);
}

// DynaRun's own icon. DynaRun V3.exe holds only the chart icon of its windows; the blue Dyna Run logo is in the icon
// file Dyna Pro's MSI gives its shortcuts. Windows shows a running DynaRun with the icon of the shortcut that carries
// its AppUserModelID, so every shortcut of ours gets that logo, taken from a DynaRun shortcut that does not use the exe.
static BOOL g_icontried; static WCHAR g_icon[MAX_PATH]; static int g_iconidx;
static BOOL is_lnk(const WCHAR *p) { int n = lstrlenW(p); return n >= 4 && !lstrcmpiW(p + n - 4, L".lnk"); }

// The shell folders that can hold a DynaRun shortcut: ONE list, for every walk and check that looks at them (the icon search, the fix, the
// uninstall's point-back, the search for our own shortcut, and the check of the paths a backup is put back to). A row says which of them
// include the folder (SFU_*) and which of those also look into its subfolders (deep); nothing else lists a shortcut folder, so a folder that
// is not here is neither scanned nor accepted by restore_shortcuts.
enum { SFU_FIX = 1, SFU_UNRET = 2, SFU_FIND = 4, SFU_ICON = 8, SFU_RESTORE = 16 };
enum { SF_DESKTOP, SF_STARTMENU, SF_PROGRAMS, SF_QUICKLAUNCH, SF_OWN, SF_COUNT };      // the index in SF[]; the first two are searched first
enum { SF_USER = 1, SF_COMMON = 2, SF_BOTH = 3 };                                       // which of the two folders of a row (bit 0 the user's, bit 1 all users')
typedef struct {
    int csidl[2];                   // [0] the user's folder, [1] the all users' folder (0: there is none)
    const WCHAR *sub;               // below that folder
    const WCHAR *reg, *rel;         // the folder of another account (unretarget_user): its name in User Shell Folders, and the default below the profile
    unsigned uses, deep;
} shortcut_folder;
static const WCHAR QUICKLAUNCH[] = L"\\Microsoft\\Internet Explorer\\Quick Launch";     // incl. pinned taskbar items
static const shortcut_folder SF[SF_COUNT] = {
    { { CSIDL_DESKTOPDIRECTORY, CSIDL_COMMON_DESKTOPDIRECTORY }, NULL, L"Desktop", L"Desktop",
      SFU_FIX | SFU_UNRET | SFU_FIND | SFU_ICON | SFU_RESTORE, 0 },
    // the Start menu root (not just Programs): some DynaRun installs put their shortcut there. The fix and the point-back look at the root
    // alone and at Programs on its own; the two searches look below the whole Start menu.
    { { CSIDL_STARTMENU, CSIDL_COMMON_STARTMENU }, NULL, L"Start Menu", L"AppData\\Roaming\\Microsoft\\Windows\\Start Menu",
      SFU_FIX | SFU_UNRET | SFU_FIND | SFU_ICON | SFU_RESTORE, SFU_FIND | SFU_ICON },
    { { CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS }, NULL, L"Programs", L"AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs",
      SFU_FIX | SFU_UNRET | SFU_RESTORE, SFU_FIX | SFU_UNRET },
    { { CSIDL_APPDATA, 0 }, QUICKLAUNCH, L"AppData", L"AppData\\Roaming",
      SFU_FIX | SFU_UNRET | SFU_RESTORE, SFU_FIX | SFU_UNRET },
    { { CSIDL_APPDATA, 0 }, L"\\DynaRunFix", NULL, NULL, SFU_RESTORE, 0 },              // desktop_link(): the shortcut it creates when there is none
};

// What callers of sf_path append to the folder themselves afterwards (their `extra`): the shortcut name, and below the Quick Launch folder the pinned items' folder.
#define SF_LNK_LEN        15        // the length of: \DynaRun V3.lnk
#define SF_PINNED_LEN     20        // the length of: \User Pinned\TaskBar

// Appends sub to out (a folder path in a MAX_PATH buffer) when the result, `extra` more characters and the terminator still fit; a row with a sub folder keeps
// 50 characters free for the names below it. FALSE (out unchanged) when it does not fit. Separate from sf_path so that a test can give it any length.
static BOOL sf_fit(WCHAR *out, const WCHAR *sub, int extra)
{
    int need = extra;
    if (sub) { need += lstrlenW(sub); if (need < 50) need = 50; }
    if (lstrlenW(out) + need >= MAX_PATH) return FALSE;
    if (sub) lstrcatW(out, sub);
    return TRUE;
}

// The folder of row id (common: the all users' one) in out (MAX_PATH characters); FALSE when the shell has none or the path is too long for what goes below
// it, including the `extra` characters the caller appends itself.
static BOOL sf_path(int id, BOOL common, WCHAR *out, int extra)
{
    const shortcut_folder *f = &SF[id]; int csidl = f->csidl[common ? 1 : 0];
    if (!csidl || !SHGetSpecialFolderPathW(NULL, out, csidl, FALSE)) return FALSE;
    return sf_fit(out, f->sub, extra);
}

// fn(folder, row, common, deep, ctx) is called for each folder that the use (one SFU_*) lists, in the scope (SF_USER, SF_COMMON, SF_BOTH): a row's
// user folder before its all users' one. fn returns FALSE to stop; then this returns TRUE.
typedef BOOL (*sf_fn)(const WCHAR *dir, int id, BOOL common, BOOL deep, void *ctx);
static BOOL shortcut_folders(unsigned use, int scope, sf_fn fn, void *ctx)
{
    WCHAR d[MAX_PATH]; int i, id, c;
    for (i = 0; i < SF_COUNT; i++) {
        id = use == SFU_ICON && i < 2 ? 1 - i : i;              // the icon is looked for in the Start menu before the Desktop
        if (!(SF[id].uses & use)) continue;
        for (c = 0; c < 2; c++)
            if ((scope & (1 << c)) && sf_path(id, c, d, 0) && !fn(d, id, c, (SF[id].deep & use) != 0, ctx)) return TRUE;
    }
    return FALSE;
}

// DynaRun's own icon. DynaRun V3.exe holds only the chart icon of its windows; the blue Dyna Run logo is in the icon
// file Dyna Pro's MSI gives its shortcuts. Windows shows a running DynaRun with the icon of the shortcut that carries
// its AppUserModelID, so every shortcut of ours gets that logo, taken from a DynaRun shortcut that does not use the exe.
// The walks below (dir_walk) skip names that start with '.' and paths longer than their buffer; all but the fix do not follow links.
typedef struct { WCHAR p[MAX_PATH], ic[MAX_PATH], x[MAX_PATH]; BOOL recurse, found; } icon_walk;
static BOOL find_icon(const WCHAR *dir, BOOL recurse);
static BOOL icon_entry(const WCHAR *p, const WIN32_FIND_DATAW *fd, void *vc)
{
    icon_walk *w = vc; IShellLinkW *sl; int idx;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return TRUE;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (w->recurse) w->found = find_icon(p, TRUE); return !w->found; }
    if (!is_lnk(p) || classify(p) == LNK_OTHER || !(sl = load_link(p))) return TRUE;
    w->ic[0] = 0; idx = 0;
    IShellLinkW_GetIconLocation(sl, w->ic, MAX_PATH, &idx);
    IShellLinkW_Release(sl);
    if (w->ic[0] && lstrcmpiW(w->ic, g_exe) && ExpandEnvironmentStringsW(w->ic, w->x, MAX_PATH) && exists(w->x)) {
        lstrcpyW(g_icon, w->ic); g_iconidx = idx; w->found = TRUE;
    }
    return !w->found;
}

static BOOL find_icon(const WCHAR *dir, BOOL recurse)
{
    icon_walk *w = alloc(sizeof(*w)); BOOL r;
    if (!w) return FALSE;
    w->recurse = recurse;
    dir_walk(dir, w->p, MAX_PATH, MAX_PATH + 1, icon_entry, w);      // paths of at most MAX_PATH characters (dir_walk's limit is "shorter than")
    r = w->found;
    release(w);
    return r;
}

static BOOL icon_folder(const WCHAR *dir, int id, BOOL common, BOOL deep, void *ctx) { return !find_icon(dir, deep); }

static void dynarun_icon(void)
{
    if (g_icontried) return;
    g_icontried = TRUE;
    shortcut_folders(SFU_ICON, SF_BOTH, icon_folder, NULL);
}

static BOOL save_link(const WCHAR *p, const WCHAR *target, const WCHAR *icon, int idx, WORD hotkey, int show, BOOL runas)
{
    IShellLinkW *sl; IPersistFile *pf; IShellLinkDataList *dl; WCHAR wd[MAX_PATH], *e; HRESULT hr = E_FAIL; DWORD fl;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) return FALSE;
    lstrcpyW(wd, g_exe); for (e = wd + lstrlenW(wd); e > wd && *e != '\\'; e--); *e = 0;
    IShellLinkW_SetPath(sl, target);
    IShellLinkW_SetWorkingDirectory(sl, wd);
    if (target == g_launcher && (!icon || !icon[0] || !lstrcmpiW(icon, g_exe))) {   // the exe's chart icon: DynaRun's logo instead
        dynarun_icon();
        if (g_icon[0]) { icon = g_icon; idx = g_iconidx; }
    }
    IShellLinkW_SetIconLocation(sl, icon, idx);
    if (target == g_launcher) IShellLinkW_SetDescription(sl, L"DynaRun V3 (DynaRunFix)");
    set_appid(sl, target == g_launcher ? APPID : NULL);
    if (hotkey) IShellLinkW_SetHotkey(sl, hotkey);
    if (show) IShellLinkW_SetShowCmd(sl, show);
    if (runas && SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IShellLinkDataList, (void **)&dl))) {
        if (SUCCEEDED(IShellLinkDataList_GetFlags(dl, &fl))) IShellLinkDataList_SetFlags(dl, fl | SLDF_RUNAS_USER);
        IShellLinkDataList_Release(dl);
    }
    if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) { hr = IPersistFile_Save(pf, p, TRUE); IPersistFile_Release(pf); }
    IShellLinkW_Release(sl);
    return SUCCEEDED(hr);
}

// Points shortcut p at target; keeps its name, icon and hotkey. Not the "Run as administrator" flag (the launcher never elevates DynaRun).
static BOOL relink(const WCHAR *p, const WCHAR *target)
{
    IShellLinkW *sl = load_link(p); IShellLinkDataList *dl; WCHAR icon[MAX_PATH]; int idx = 0, show = 0;
    WORD hotkey = 0; DWORD fl = 0;
    if (!sl) return FALSE;
    icon[0] = 0;
    IShellLinkW_GetIconLocation(sl, icon, MAX_PATH, &idx);
    IShellLinkW_GetHotkey(sl, &hotkey);
    IShellLinkW_GetShowCmd(sl, &show);
    if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IShellLinkDataList, (void **)&dl))) { IShellLinkDataList_GetFlags(dl, &fl); IShellLinkDataList_Release(dl); }
    IShellLinkW_Release(sl);
    if (!icon[0]) { lstrcpyW(icon, g_exe); idx = 0; }
    (void)fl;   // SLDF_RUNAS_USER is not carried over to the retargeted shortcut
    return save_link(p, target, icon, idx, hotkey, show, FALSE);
}

// Points an existing DynaRun shortcut at the launcher. The original file is saved in <backup>\Shortcuts.
static void retarget(const WCHAR *p, HKEY backup)
{
    DWORD n; void *orig = read_file(p, &n);
    if (!orig) return;
    if (!RegSetValueExW(backup, p, 0, REG_BINARY, orig, n)) relink(p, g_launcher);
    release(orig);
}

typedef struct { WCHAR p[2 * MAX_PATH]; HKEY backup; BOOL recurse; int found; } scan_walk;
static int scan(const WCHAR *dir, BOOL recurse, HKEY backup);
static BOOL scan_lnk(const WCHAR *p, const WIN32_FIND_DATAW *fd, void *vc)      // (this walk follows links; the others do not)
{
    scan_walk *w = vc; int c;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (w->recurse) w->found += scan(p, TRUE, w->backup); return TRUE; }
    if (!is_lnk(p)) return TRUE;
    if ((c = classify(p)) == LNK_DYNARUN) retarget(p, w->backup);
    // ours from an older version (before v1.3.0 they had no AppUserModelID): written again with the ID, else the
    // running DynaRun (ID set by dynafix.dll) matches no shortcut and Windows shows its window icon, not DynaRun's
    else if (c == LNK_OURS) relink(p, g_launcher);
    if (c != LNK_OTHER) w->found++;
    return TRUE;
}

static int scan(const WCHAR *dir, BOOL recurse, HKEY backup)   // returns number of DynaRun shortcuts
{
    scan_walk *w = alloc(sizeof(*w)); int found;
    if (!w) return 0;
    w->backup = backup; w->recurse = recurse;
    dir_walk(dir, w->p, 2 * MAX_PATH, 2 * MAX_PATH + 1, scan_lnk, w);
    found = w->found;
    release(w);
    return found;
}

typedef struct { WCHAR p[2 * MAX_PATH]; WCHAR *out; BOOL recurse, found; } ours_walk;
static BOOL find_ours(const WCHAR *dir, BOOL recurse, WCHAR *out);
static BOOL ours_lnk(const WCHAR *p, const WIN32_FIND_DATAW *fd, void *vc)
{
    ours_walk *w = vc;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return TRUE;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (w->recurse) w->found = find_ours(p, TRUE, w->out); return !w->found; }
    if (is_lnk(p) && classify(p) == LNK_OURS) { lstrcpyW(w->out, p); w->found = TRUE; }
    return !w->found;
}

static BOOL find_ours(const WCHAR *dir, BOOL recurse, WCHAR *out)    // the first shortcut of ours below dir
{
    ours_walk *w = alloc(sizeof(*w)); BOOL r;
    if (!w) return FALSE;
    w->out = out; w->recurse = recurse;
    dir_walk(dir, w->p, 2 * MAX_PATH, 2 * MAX_PATH + 1, ours_lnk, w);
    r = w->found;
    release(w);
    return r;
}

// The first shortcut of ours where a user sees it: the Desktop, then the Start menu (the user's folder before the all users' one).
static BOOL ours_folder(const WCHAR *dir, int id, BOOL common, BOOL deep, void *out) { return !find_ours(dir, deep, out); }
static BOOL find_our_shortcut(WCHAR *out) { return shortcut_folders(SFU_FIND, SF_BOTH, ours_folder, out); }

typedef struct { HKEY backup; int desk; } fix_ctx;
static BOOL fix_folder(const WCHAR *dir, int id, BOOL common, BOOL deep, void *vc)
{
    fix_ctx *c = vc; int n = scan(dir, deep, c->backup);
    if (id == SF_DESKTOP) c->desk = n;
    return TRUE;
}

static int fix_shortcuts(BOOL common, HKEY backup)     // returns the number of DynaRun shortcuts on the Desktop
{
    fix_ctx c;
    c.backup = backup; c.desk = 0;
    shortcut_folders(SFU_FIX, common ? SF_COMMON : SF_USER, fix_folder, &c);
    return c.desk;
}

// restore_shortcuts writes files at paths that come from the registry. A path is used only when it is a .lnk file below one of the
// shortcut folders the backups were made from (SF[]: the rows that list SFU_RESTORE; user's or all users', as the backup is), with no ".."
// component and no link (junction) as its folder. When this process is elevated and the backups are in
// HKCU, which a program of the same user can write, the file must also be below the user's profile folder.
static BOOL under_dir(const WCHAR *path, const WCHAR *dir)
{
    int n = lstrlenW(dir);
    while (n > 0 && dir[n - 1] == '\\') n--;
    return n > 0 && lstrlenW(path) > n + 1 && path[n] == '\\' && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, path, n, dir, n) == CSTR_EQUAL;
}

static BOOL has_dotdot(const WCHAR *p)
{
    const WCHAR *s = p;
    for (; *p; p++) if (p[0] == '.' && p[1] == '.' && (p == s || p[-1] == '\\') && (p[2] == 0 || p[2] == '\\')) return TRUE;
    return FALSE;
}

// The syntax part of lnk_path_ok, without any folder lookup (tested with Windows 7 style paths): an absolute drive or UNC path that ends in
// ".lnk", has no ".." component and lies below one of dirs[0..n) (case-insensitive; trailing backslashes of a folder do not matter).
static BOOL lnk_name_in_dirs(const WCHAR *name, const WCHAR *const *dirs, int n)
{
    int i, len = lstrlenW(name);
    if (len < 8 || len >= MAX_PATH || lstrcmpiW(name + len - 4, L".lnk") || name[len - 5] == '\\' || has_dotdot(name)) return FALSE;      // not a file called just ".lnk"
    if (!(((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':' && name[2] == '\\') &&
        !(name[0] == '\\' && name[1] == '\\' && name[2] != '?' && name[2] != '.')) return FALSE;      // an absolute drive or UNC path, no device path
    for (i = 0; i < n; i++) if (dirs[i] && dirs[i][0] && under_dir(name, dirs[i])) return TRUE;
    return FALSE;
}

// d: the long form of a folder or file name (8.3 components expanded) when Windows can tell, else unchanged
static void long_form(WCHAR *d)
{
    WCHAR t[MAX_PATH]; DWORD n = GetLongPathNameW(d, t, MAX_PATH);
    if (n && n < MAX_PATH) lstrcpyW(d, t);
}

typedef struct { WCHAR dirs[10][MAX_PATH]; const WCHAR *list[10]; int n; } restore_dirs;
static BOOL restore_folder(const WCHAR *dir, int id, BOOL common, BOOL deep, void *vc)
{
    restore_dirs *r = vc;
    lstrcpyW(r->dirs[r->n], dir); r->list[r->n] = r->dirs[r->n]; r->n++;
    if (id != SF_OWN) {                          // (the folder of desktop_link() is listed as the shell reports it)
        lstrcpyW(r->dirs[r->n], dir); long_form(r->dirs[r->n]);      // the backup may hold the long form of a folder that the shell reports in 8.3 form
        r->list[r->n] = r->dirs[r->n]; r->n++;
    }
    return TRUE;
}

static BOOL lnk_path_ok(const WCHAR *name, BOOL common)
{
    static restore_dirs rd;
    WCHAR d[MAX_PATH], *e, par[MAX_PATH], lname[MAX_PATH]; int len = lstrlenW(name); DWORD a;
    if (len < 8 || len >= MAX_PATH) return FALSE;
    if (!common && is_admin()) {                                  // elevated: the user's own registry is not trusted beyond their profile
        if (!SHGetSpecialFolderPathW(NULL, d, CSIDL_PROFILE, FALSE)) return FALSE;
        if (!under_dir(name, d)) {
            lstrcpyW(lname, name); long_form(lname); long_form(d);        // 8.3 form on one side only
            if (!under_dir(lname, d)) return FALSE;
        }
    }
    rd.n = 0;
    shortcut_folders(SFU_RESTORE, common ? SF_COMMON : SF_USER, restore_folder, &rd);
    lstrcpyW(par, name); for (e = par + len; e > par && *e != '\\'; e--); *e = 0;
    if (!lnk_name_in_dirs(name, rd.list, rd.n)) {
        lstrcpyW(lname, par); long_form(lname);                    // the same, with the folder of the file in long form
        if (!path_join(lname, MAX_PATH, lname, e + 1)) return FALSE;
        if (!lnk_name_in_dirs(lname, rd.list, rd.n)) return FALSE;
    }
    a = GetFileAttributesW(par);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT)) return FALSE;
    a = GetFileAttributesW(name);
    return a == INVALID_FILE_ATTRIBUTES || !(a & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
}

// The backup stays (key not deleted) when a shortcut could not be put back or was refused: nothing is lost, a later uninstall can retry.
static void restore_shortcuts(HKEY root, const WCHAR *key)
{
    HKEY k; DWORD i, nmax = 0, dmax = 0, n, dn, type; WCHAR *name; BYTE *data; WCHAR sub[64]; BOOL failed = FALSE, common = root == HKEY_LOCAL_MACHINE;
    if (RegOpenKeyExW(root, cat3(sub, key, L"\\Shortcuts", NULL), 0, KEY_READ, &k)) return;
    RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &nmax, &dmax, NULL, NULL);
    name = alloc((nmax + 2) * sizeof(WCHAR)); data = alloc(dmax + 2);
    if (!name || !data) failed = TRUE;
    for (i = 0; name && data && (n = nmax + 1, dn = dmax, !RegEnumValueW(k, i, name, &n, NULL, &type, data, &dn)); i++) {
        if (type != REG_BINARY || !lnk_path_ok(name, common)) { failed = TRUE; setup_log(L"a shortcut backup was not restored: its path is not a .lnk file in a shell folder"); continue; }
        if (classify(name) != LNK_OURS && exists(name)) continue;   // changed by the user since: leave it
        if (!dn) { if (exists(name) && !DeleteFileW(name)) failed = TRUE; }       // a shortcut we created
        else if (dn < 76 || data[0] != 0x4C || data[1] || data[2] || data[3] || !write_file(name, data, dn)) failed = TRUE;   // a .lnk starts with its 0x4C header size
    }
    release(name); release(data);
    RegCloseKey(k);
    if (!failed) RegDeleteKeyW(root, sub);
}

// Uninstall, other accounts: their backups are in their own HKCU, so their shortcuts that still start
// the launcher (which is about to be deleted) are pointed back at DynaRun V3.exe instead. Only files that
// are our shortcuts are rewritten, so a folder read from another user's registry can do no harm; reparse
// points (junctions) are not followed.
typedef struct { WCHAR p[2 * MAX_PATH]; BOOL recurse; } unret_walk;
static void unretarget(const WCHAR *dir, BOOL recurse);
static BOOL unret_lnk(const WCHAR *p, const WIN32_FIND_DATAW *fd, void *vc)
{
    unret_walk *w = vc;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return TRUE;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (w->recurse) unretarget(p, TRUE); return TRUE; }
    if (is_lnk(p) && g_exe[0] && classify(p) == LNK_OURS) relink(p, g_exe);
    return TRUE;
}

static void unretarget(const WCHAR *dir, BOOL recurse)
{
    unret_walk *w = alloc(sizeof(*w));
    if (!w) return;
    w->recurse = recurse;
    dir_walk(dir, w->p, 2 * MAX_PATH, 2 * MAX_PATH + 1, unret_lnk, w);
    release(w);
}

// What is left of our shortcuts in the shell folders after the backups were put back (a backup that is missing, as when an older version did
// not record one, or was refused): they point to DynaRun V3.exe again instead of the launcher that is being removed. The folders include the
// pins the user made of the launcher (Quick Launch).
static BOOL unret_folder(const WCHAR *dir, int id, BOOL common, BOOL deep, void *ctx) { unretarget(dir, deep); return TRUE; }
static void unretarget_shell(BOOL common)
{
    if (!g_exe[0]) return;
    shortcut_folders(SFU_UNRET, common ? SF_COMMON : SF_USER, unret_folder, NULL);
}

