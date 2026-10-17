// Part of setup.c (ACL and exe trust checks): included by it, in one compilation unit; never compiled on its own.
static BOOL sid_in(PSID s, const WCHAR *const *list)
{
    PSID t; BOOL r = FALSE; int i;
    for (i = 0; !r && list[i]; i++) if (ConvertStringSidToSidW(list[i], &t)) { r = EqualSid(s, t); LocalFree(t); }
    return r;
}

#define WRITE_MASK (FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL)

// TRUE when ordinary users could change what is at path (or when that cannot be told). Not user-writable: owned by SYSTEM,
// the Administrators or TrustedInstaller, and no ACE that applies to the object itself gives Everyone, Users, Authenticated Users,
// Interactive, Power Users or the user who started the setup a write, delete or permission right.
static BOOL user_writable(const trust_ctx *t, const WCHAR *path)
{
    static const WCHAR *const owners[] = { L"S-1-5-18", L"S-1-5-32-544", L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464", NULL };
    static const WCHAR *const others[] = { L"S-1-1-0", L"S-1-5-32-545", L"S-1-5-11", L"S-1-5-4", L"S-1-5-32-547", NULL };
    const WCHAR *const me[2] = { t->user_sid, NULL };
    PSID owner = NULL; PACL dacl = NULL; PSECURITY_DESCRIPTOR sd = NULL; BOOL bad = TRUE; DWORD i; ACCESS_ALLOWED_ACE *a;
    if (GetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, NULL, &dacl, NULL, &sd)) return TRUE;
    if (owner && dacl && sid_in(owner, owners)) {            // no DACL = full access for everyone: bad
        bad = FALSE;
        for (i = 0; i < dacl->AceCount && !bad; i++) {
            if (!GetAce(dacl, i, (void **)&a)) { bad = TRUE; break; }
            if (a->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || (a->Header.AceFlags & INHERIT_ONLY_ACE) || !(a->Mask & WRITE_MASK)) continue;
            if (sid_in((PSID)&a->SidStart, others) || (t->user_sid[0] && sid_in((PSID)&a->SidStart, me))) bad = TRUE;
        }
    }
    LocalFree(sd);
    return bad;
}

static BOOL (*g_is_writable)(const trust_ctx *t, const WCHAR *path) = user_writable;     // the tests replace this

// driver.c: may ordinary users (and the user who started the setup, sid; NULL: no such user is known) change what is at path?
BOOL path_user_writable(const WCHAR *path, const WCHAR *sid)
{
    trust_ctx t;
    zero(&t, sizeof(t));
    if (sid) lstrcpynW(t.user_sid, sid, 200);
    return g_is_writable(&t, path);
}

static BOOL is_dynarun_name(const WCHAR *p)
{
    const WCHAR *n = p + lstrlenW(p);
    while (n > p && n[-1] != '\\') n--;
    return !lstrcmpiW(n, L"DynaRun V3.exe");
}

typedef DWORD (WINAPI *GetFinalPathNameByHandleW_t)(HANDLE, LPWSTR, DWORD, DWORD);

// The elevated stage does not trust the DynaRun path it was given on its command line. It must be an absolute path on a
// fixed drive, named DynaRun V3.exe, an existing file. t->exe_trusted says whether what the elevated stage writes there
// (manifest), starts (the borrowed time stamp) and registers (COM servers in that folder) is safe: no link in the path, and
// neither the folder nor the exe changeable by ordinary users. FALSE: the path is unusable; exe_trusted FALSE only: skip those steps.
static BOOL check_exe(trust_ctx *t)
{
    WCHAR root[4], fin[MAX_PATH + 8], dir[MAX_PATH], *e; HANDLE h; GetFinalPathNameByHandleW_t f; DWORD n;
    t->exe_trusted = FALSE;
    if (!g_exe[0] || lstrlenW(g_exe) >= MAX_PATH - 1 || !is_dynarun_name(g_exe)) return FALSE;
    if (!((g_exe[0] >= 'A' && g_exe[0] <= 'Z') || (g_exe[0] >= 'a' && g_exe[0] <= 'z')) || g_exe[1] != ':' || g_exe[2] != '\\') return FALSE;
    lstrcpynW(root, g_exe, 4);
    if (GetDriveTypeW(root) != DRIVE_FIXED) return FALSE;
    h = CreateFileW(g_exe, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    f = (GetFinalPathNameByHandleW_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetFinalPathNameByHandleW");
    if (GetFileType(h) != FILE_TYPE_DISK) { CloseHandle(h); return FALSE; }
    n = f ? f(h, fin, MAX_PATH + 8, 0) : 0;
    CloseHandle(h);
    // Windows XP has no GetFinalPathNameByHandleW: a junction in the path to DynaRun V3.exe cannot be detected there, only the
    // ACL checks below apply (limitation; later Windows versions check for links)
    if (!f) setup_log(L"Windows XP: links (junctions) in the path to DynaRun V3.exe cannot be detected");
    // with GetFinalPathNameByHandleW (Vista and later) the final path must be the path given: no link anywhere in it
    if (f && (!n || n >= MAX_PATH + 8 || fin[0] != '\\' || fin[1] != '\\' || fin[2] != '?' || fin[3] != '\\' || lstrcmpiW(fin + 4, g_exe))) return TRUE;
    lstrcpyW(dir, g_exe); for (e = dir + lstrlenW(dir); e > dir && *e != '\\'; e--); *e = 0;
    t->exe_trusted = !g_is_writable(t, dir) && !g_is_writable(t, g_exe);
    return TRUE;
}

