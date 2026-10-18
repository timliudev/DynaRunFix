// Part of setup.c (DLL loading and small helpers): included by it, in one compilation unit; never compiled on its own.
/* ---------- DLLs: only from the system folder ---------- */

// msi.dll and wininet.dll are delay-loaded (build.cmd: /DELAYLOAD) and loaded here by their full path in the system folder, never
// searched for (a planted msi.dll / wininet.dll next to the setup, e.g. in Downloads, is not loaded). The linker flag
// /DEPENDENTLOADFLAG:0x800 and SetDefaultDllDirectories (WinMainCRTStartup) do the same for every other DLL on systems that have them
// (Windows 7 with KB2533623 and later); on older systems the delay-load hook and the full paths below are what protects.
static HMODULE load_system_dll(const WCHAR *name)
{
    WCHAR p[MAX_PATH]; UINT n = GetSystemDirectoryW(p, MAX_PATH - 40);
    if (!n || n >= MAX_PATH - 40 || lstrlenW(name) > 30) return NULL;
    lstrcatW(p, L"\\"); lstrcatW(p, name);
    return LoadLibraryW(p);
}

static FARPROC WINAPI dll_hook(unsigned notify, PDelayLoadInfo info)
{
    WCHAR n[40];
    if (notify != dliNotePreLoadLibrary || !info || !info->szDll) return NULL;
    if (MultiByteToWideChar(CP_ACP, 0, info->szDll, -1, n, 40) <= 0) return NULL;
    return (FARPROC)load_system_dll(n);          // NULL: the delay-load helper then raises its usual error
}
const PfnDliHook __pfnDliNotifyHook2 = dll_hook;

/* ---------- small helpers (no C runtime) ---------- */

// A SHA-256 as the command line and the wizard carry it: exactly 64 hexadecimal digits (any case), nothing else.
static BOOL hex64_ok(const WCHAR *s)
{
    int i;
    if (!s) return FALSE;
    for (i = 0; i < 64; i++) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) return FALSE;
    return s[64] == 0;
}

void zero(void *p, SIZE_T n) { volatile char *z = (volatile char *)p; while (n--) *z++ = 0; }
void install_opts_default(install_opts *o) { zero(o, sizeof(*o)); o->pin = TRUE; o->autostart = TRUE; }
void *alloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
void release(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
static WCHAR *cat3(WCHAR *d, const WCHAR *a, const WCHAR *b, const WCHAR *c)
{ lstrcpyW(d, a); lstrcatW(d, b); if (c) lstrcatW(d, c); return d; }
BOOL exists(const WCHAR *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

BOOL path_join(WCHAR *out, int limit, const WCHAR *dir, const WCHAR *name)
{
    if (lstrlenW(dir) + lstrlenW(name) + 2 >= limit) return FALSE;
    if (out != dir) lstrcpyW(out, dir);
    lstrcatW(out, L"\\"); lstrcatW(out, name);
    return TRUE;
}

int run_process(const WCHAR *exe, WCHAR *cmd, const WCHAR *cwd, DWORD flags, BOOL hide, DWORD ms, DWORD *code)
{
    STARTUPINFOW si; PROCESS_INFORMATION pi; int r = PROC_EXITED;
    zero(&si, sizeof(si)); si.cb = sizeof(si);
    if (hide) { si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; }
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, flags, NULL, cwd, &si, &pi)) return PROC_NOSTART;
    if (WaitForSingleObject(pi.hProcess, ms) == WAIT_OBJECT_0 || ms == INFINITE) GetExitCodeProcess(pi.hProcess, code);
    else { TerminateProcess(pi.hProcess, 1); r = PROC_TIMEOUT; }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return r;
}

int dir_walk(const WCHAR *dir, WCHAR *buf, int bufcch, int limit, walk_fn fn, void *ctx)
{
    WIN32_FIND_DATAW fd; HANDLE f; int r = WALK_DONE;
    if (lstrlenW(dir) + 3 > bufcch) return WALK_NOOPEN;
    lstrcpyW(buf, dir); lstrcatW(buf, L"\\*");
    if ((f = FindFirstFileW(buf, &fd)) == INVALID_HANDLE_VALUE) return WALK_NOOPEN;
    do {
        if (fd.cFileName[0] == '.' || !path_join(buf, limit, dir, fd.cFileName)) continue;
        if (!fn(buf, &fd, ctx)) { r = WALK_STOPPED; break; }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return r;
}

BOOL app_key_open(BOOL machine, REGSAM sam, HKEY *k)
{ return !RegOpenKeyExW(machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, machine ? APPKEY : USERKEY, 0, sam, k); }
BOOL app_key_create(BOOL machine, HKEY *k)
{ return !RegCreateKeyExW(machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, machine ? APPKEY : USERKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, k, NULL); }
int msg(const WCHAR *text, UINT flags) { return MessageBoxW(g_hwnd, text, TITLE, flags | MB_SETFOREGROUND); }
static void info(const install_opts *o, const WCHAR *text) { if (!o->quiet) msg(text, MB_ICONINFORMATION); }
static void error2(const install_opts *o, const WCHAR *text, const WCHAR *what)
{ static WCHAR b[1024]; if (o->quiet) return; lstrcpynW(b, text, 600); if (what) { lstrcatW(b, L"\n\n"); lstrcatW(b, what); } msg(b, MB_ICONERROR); }

BOOL is_admin(void)
{
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY; PSID sid; BOOL r = FALSE;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid)) {
        if (!CheckTokenMembership(NULL, sid, &r)) r = FALSE;
        FreeSid(sid);
    }
    return r;
}

BOOL user_sid(WCHAR *out, int cch)
{
    HANDLE tok; static BYTE buf[256]; DWORD n; WCHAR *s; BOOL r = FALSE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return FALSE;
    if (GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n) && ConvertSidToStringSidW(((TOKEN_USER *)buf)->User.Sid, &s)) {
        lstrcpynW(out, s, cch); LocalFree(s); r = TRUE;
    }
    CloseHandle(tok);
    return r;
}

static void *read_file(const WCHAR *p, DWORD *n)
{
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL); void *d;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    *n = GetFileSize(h, NULL);
    d = alloc(*n + 1);
    if (d && !ReadFile(h, d, *n, n, NULL)) { release(d); d = NULL; }
    CloseHandle(h);
    return d;
}

// In-use files (dynafix.dll inside a running DynaRun) cannot be overwritten or deleted, but can be
// renamed; the renamed file goes away at the next restart.
static BOOL move_aside(const WCHAR *p)
{
    static WCHAR old[MAX_PATH + 32];
    wsprintfW(old, L"%s.%lu.old", p, GetTickCount());
    if (!MoveFileExW(p, old, MOVEFILE_REPLACE_EXISTING)) return FALSE;
    // "delete at restart" is administrators-only: in the elevated stage the renamed file is registered for that; without
    // the right, delete it right away if nothing holds it (a stale .old is all that can be left)
    if (!MoveFileExW(old, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) DeleteFileW(old);
    return TRUE;
}

static BOOL write_file(const WCHAR *p, const void *d, DWORD n)
{
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); DWORD w; BOOL ok;
    if (h == INVALID_HANDLE_VALUE && exists(p) && move_aside(p))
        h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    ok = WriteFile(h, d, n, &w, NULL) && w == n;
    CloseHandle(h);
    return ok;
}

// Appends one line to a log. A link is never followed: a reparse point (symbolic link, junction) or a file with several hard links
// is refused, so the elevated stage cannot be made to write somewhere else through a file planted in %TEMP%.
static void append_line(const WCHAR *p, const WCHAR *line)
{
    static char u[4096]; int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, sizeof(u) - 2, NULL, NULL); DWORD w;
    BY_HANDLE_FILE_INFORMATION bi;
    HANDLE h = CreateFileW(p, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    if (n <= 0 || !GetFileInformationByHandle(h, &bi) || (bi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        bi.nNumberOfLinks > 1) { CloseHandle(h); return; }
    u[n - 1] = '\r'; u[n] = '\n';
    WriteFile(h, u, n + 1, &w, NULL);
    CloseHandle(h);
}

static BOOL same_bytes(const WCHAR *p, const void *d, DWORD n)   // the file on disk is exactly d
{
    DWORD got, i; BYTE *f = read_file(p, &got); BOOL r = f && got == n;
    for (i = 0; r && i < n; i++) r = f[i] == ((const BYTE *)d)[i];
    release(f);
    return r;
}

BOOL same_file(const WCHAR *a, const WCHAR *b)
{
    DWORD n; void *d = read_file(a, &n); BOOL r = d && same_bytes(b, d, n);
    release(d);
    return r;
}

static BOOL extract(int id, const WCHAR *p)     // written and read back: the installed file is the payload
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA); const void *d; DWORD n;
    if (!r) return FALSE;
    d = LockResource(LoadResource(NULL, r)); n = SizeofResource(NULL, r);
    return write_file(p, d, n) && same_bytes(p, d, n);
}

static BOOL name_in(const WCHAR *list, const WCHAR *n)
{
    if (list) for (; *list; list += lstrlenW(list) + 1) if (!lstrcmpiW(list, n)) return TRUE;
    return FALSE;
}

// Deletes dir and everything in it, except the top-level names in keep (a multi-string; dir then stays). A link (junction,
// symbolic link) is removed itself and never followed. What is in use is renamed aside and goes at the next restart.
void delete_tree(const WCHAR *dir, const WCHAR *keep, int depth)
{
    WCHAR *p; WIN32_FIND_DATAW fd; HANDLE f; DWORD a = GetFileAttributesW(dir); BOOL kept = FALSE;
    if (a == INVALID_FILE_ATTRIBUTES) return;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) { if (!RemoveDirectoryW(dir)) MoveFileExW(dir, NULL, MOVEFILE_DELAY_UNTIL_REBOOT); return; }
    if (depth > 40 || !(p = alloc(2 * MAX_PATH * sizeof(WCHAR)))) return;
    if (lstrlenW(dir) + 3 < 2 * MAX_PATH) {
        cat3(p, dir, L"\\*", NULL);
        f = FindFirstFileW(p, &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                if (!lstrcmpW(fd.cFileName, L".") || !lstrcmpW(fd.cFileName, L"..")) continue;
                if ((depth == 0 && name_in(keep, fd.cFileName)) || !path_join(p, 2 * MAX_PATH, dir, fd.cFileName)) { kept = TRUE; continue; }   // kept, or a path too long for the buffer
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { if (!RemoveDirectoryW(p)) MoveFileExW(p, NULL, MOVEFILE_DELAY_UNTIL_REBOOT); }
                    else delete_tree(p, NULL, depth + 1);
                }
                else if (!DeleteFileW(p) && !move_aside(p)) MoveFileExW(p, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);   // locked: gone at the next restart
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    if (!kept && !RemoveDirectoryW(dir)) MoveFileExW(dir, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
    release(p);
}

static DWORD reg_str(HKEY root, const WCHAR *path, const WCHAR *name, WCHAR *out, DWORD cch, REGSAM extra)
{
    HKEY k; DWORD type, n = (cch - 1) * sizeof(WCHAR); LONG r;
    out[0] = 0;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | extra, &k)) return 0;
    r = RegQueryValueExW(k, name, NULL, &type, (BYTE *)out, &n);
    RegCloseKey(k);
    if (r || (type != REG_SZ && type != REG_EXPAND_SZ)) { out[0] = 0; return 0; }
    out[n / sizeof(WCHAR)] = 0;
    return lstrlenW(out);
}

static void set_str(HKEY k, const WCHAR *name, const WCHAR *v)
{ RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (lstrlenW(v) + 1) * sizeof(WCHAR)); }
static void set_dword(HKEY k, const WCHAR *name, DWORD v) { RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&v, 4); }

static BOOL contains_i(const WCHAR *s, const WCHAR *sub)
{
    int n = lstrlenW(sub);
    for (; lstrlenW(s) >= n; s++) if (CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, s, n, sub, n) == CSTR_EQUAL) return TRUE;
    return FALSE;
}

// The real Windows version (GetVersion lies to programs without a matching manifest).
static void os_ver(DWORD *major, DWORD *minor, DWORD *build)
{
    typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOW *);
    RtlGetVersion_t f = (RtlGetVersion_t)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"); OSVERSIONINFOW v;
    zero(&v, sizeof(v)); v.dwOSVersionInfoSize = sizeof(v);
    if (f) f(&v);
    *major = v.dwMajorVersion; *minor = v.dwMinorVersion; *build = v.dwBuildNumber;
}

DWORD os_build(void) { DWORD a, b, c; os_ver(&a, &b, &c); return c; }

