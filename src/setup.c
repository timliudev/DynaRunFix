// DynaRunFix-Setup.exe - installs DynaRunFix for end users (Windows XP .. 11).
//
//   DynaRunFix-Setup.exe                      wizard (wizard.c): installs DynaRun V3 first when it is
//                                             missing (official download, password, license), then the fix
//   DynaRunFix-Setup.exe /quiet               install the fix only, no UI (DynaRun must be installed);
//                                             exit code 6 (RC_RUNNING), nothing changed, while DynaRun runs;
//   DynaRunFix-Setup.exe /quiet /close        the same, but closes a running DynaRun first
//   DynaRunFix-Setup.exe /uninstall [/quiet]  uninstall (also run from "Programs and Features")
//
// Install, in two stages so that per-user changes land in the right profile even when an
// administrator's credentials are typed into the UAC prompt:
//   machine stage (elevated, "/machine <user SID> <DynaRun exe>"): copies DynaRunFix.exe,
//     dynafix.dll and this program to Program Files\DynaRunFix, copies DynaRun's per-user COM
//     registrations of that user to HKLM (tools/register-machine-wide.ps1 in C), adds the
//     code-page manifest when the system ANSI code page is UTF-8, puts Locale Emulator in le\ (the
//     launcher uses it only with the UTF-8 option on a zh-TW system), retargets all-users shortcuts,
//     registers the uninstaller.
//   user stage (not elevated): retargets the user's DynaRun shortcuts to the launcher (the original
//     .lnk bytes are kept in the registry and put back on uninstall); creates a desktop shortcut
//     when there is none.
// The machine-wide COM keys are kept on uninstall: removing them would break elevated DynaRun again.
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <sddl.h>
#include <tlhelp32.h>
#include "setup.h"
#include "version.h"

#ifndef SLDF_RUNAS_USER
#define SLDF_RUNAS_USER 0x2000
#endif
#define WOW KEY_WOW64_64KEY       // DynaRun's COM keys: physical view, like 64-bit reg.exe sees them
#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)

static const WCHAR APPKEY[] = L"SOFTWARE\\DynaRunFix";
static const WCHAR USERKEY[] = L"Software\\DynaRunFix";
static const WCHAR UNINSTKEY[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DynaRunFix";
static const WCHAR DYNARUN_REL[] = L"\\Dyna Pro Dynamometers\\DynaRun V3.exe";
static const WCHAR *COM_FILES[] = {
    L"comctl32.ocx", L"comdlg32.ocx", L"csimctl5.ocx", L"cwpid.ocx", L"cwui.ocx", L"dguard2.ocx",
    L"filev090.ocx", L"mscomctl.ocx", L"mscomm32.ocx", L"msdatgrd.ocx", L"msflxgrd.ocx", L"mshflxgd.ocx",
    L"numled.ocx", L"pesgo32e.ocx", L"richtx32.ocx", L"shcmb090.ocx", L"tabctl32.ocx", L"thbres25.dll" };

BOOL g_zh, g_quiet, g_close;
HWND g_hwnd;
WCHAR g_self[MAX_PATH], g_dir[MAX_PATH], g_launcher[MAX_PATH], g_exe[MAX_PATH];
#define TITLE T(L"DynaRunFix Setup", L"DynaRunFix 安裝程式")

/* ---------- small helpers (no C runtime) ---------- */

void zero(void *p, SIZE_T n) { volatile char *z = (volatile char *)p; while (n--) *z++ = 0; }
void *alloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
void release(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
static WCHAR *cat3(WCHAR *d, const WCHAR *a, const WCHAR *b, const WCHAR *c)
{ lstrcpyW(d, a); lstrcatW(d, b); if (c) lstrcatW(d, c); return d; }
BOOL exists(const WCHAR *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
int msg(const WCHAR *text, UINT flags) { return MessageBoxW(g_hwnd, text, TITLE, flags | MB_SETFOREGROUND); }
static void info(const WCHAR *text) { if (!g_quiet) msg(text, MB_ICONINFORMATION); }
static void error2(const WCHAR *text, const WCHAR *what)
{ static WCHAR b[1024]; if (g_quiet) return; lstrcpynW(b, text, 600); if (what) { lstrcatW(b, L"\n\n"); lstrcatW(b, what); } msg(b, MB_ICONERROR); }

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
    MoveFileExW(old, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
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

static void append_line(const WCHAR *p, const WCHAR *line)
{
    static char u[2048]; int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, sizeof(u) - 2, NULL, NULL); DWORD w;
    HANDLE h = CreateFileW(p, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE || n <= 0) { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); return; }
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

static BOOL same_file(const WCHAR *a, const WCHAR *b)
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

static void touch(const WCHAR *p)    // makes Windows re-read the exe's external manifest
{
    FILETIME ft; SYSTEMTIME st;
    HANDLE h = CreateFileW(p, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    GetSystemTime(&st); SystemTimeToFileTime(&st, &ft);
    SetFileTime(h, NULL, NULL, &ft);
    CloseHandle(h);
}

static void delete_tree(const WCHAR *dir)
{
    WCHAR *p = alloc(2 * MAX_PATH * sizeof(WCHAR)); WIN32_FIND_DATAW fd; HANDLE f;
    if (!p) return;
    cat3(p, dir, L"\\*", NULL);
    f = FindFirstFileW(p, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!lstrcmpW(fd.cFileName, L".") || !lstrcmpW(fd.cFileName, L"..")) continue;
            cat3(p, dir, L"\\", fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) delete_tree(p);
            else if (!DeleteFileW(p)) move_aside(p);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    if (!RemoveDirectoryW(dir)) MoveFileExW(dir, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
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

/* ---------- paths ---------- */

static void init_paths(void)
{
    GetModuleFileNameW(NULL, g_self, MAX_PATH);
    if (!SHGetSpecialFolderPathW(NULL, g_dir, CSIDL_PROGRAM_FILES, FALSE)) lstrcpyW(g_dir, L"C:\\Program Files");
    lstrcatW(g_dir, L"\\DynaRunFix");
    cat3(g_launcher, g_dir, L"\\DynaRunFix.exe", NULL);
}

BOOL locate_dynarun(void)
{
    static const WCHAR *vars[] = { L"ProgramFiles(x86)", L"ProgramFiles" };
    WCHAR pf[MAX_PATH]; int i;
    if (reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"DynaRunExe", g_exe, MAX_PATH, 0) && exists(g_exe)) return TRUE;
    for (i = 0; i < 2; i++)
        if (GetEnvironmentVariableW(vars[i], pf, MAX_PATH) && exists(cat3(g_exe, pf, DYNARUN_REL, NULL))) return TRUE;
    g_exe[0] = 0;
    return FALSE;
}

/* ---------- programs that hold the files the setup replaces ---------- */

typedef BOOL (WINAPI *QueryFullProcessImageNameW_t)(HANDLE, DWORD, LPWSTR, PDWORD);

static void process_path(DWORD pid, WCHAR *out, DWORD cch)   // the exe of a process, or "" when it cannot be read
{
    static QueryFullProcessImageNameW_t q; static BOOL tried; HANDLE h; MODULEENTRY32W me;
    out[0] = 0;
    if (!tried) { tried = TRUE; q = (QueryFullProcessImageNameW_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryFullProcessImageNameW"); }
    // Vista and later: also for an elevated process when this one is not (PROCESS_QUERY_LIMITED_INFORMATION)
    if (q && (h = OpenProcess(0x1000, FALSE, pid))) {
        if (!q(h, 0, out, &cch)) out[0] = 0;
        CloseHandle(h);
        if (out[0]) return;
    }
    h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);       // XP: the first module is the exe
    if (h == INVALID_HANDLE_VALUE) return;
    me.dwSize = sizeof(me);
    if (Module32FirstW(h, &me)) lstrcpynW(out, me.szExePath, cch);
    CloseHandle(h);
}

// A running DynaRun V3 has dynafix.dll (and under Locale Emulator LoaderDll.dll / LocaleEmulator.dll)
// of the install folder loaded. Windows lets the setup rename such files and put new ones in place,
// but that DynaRun keeps the old code, and "Start DynaRun" would only find it already running, so
// DynaRun is closed first (the user agrees in the wizard, or passes /close). DynaRun is matched by
// name (any folder; it may run elevated), the launcher and LEProc only when they run from the install folder.
typedef struct { DWORD pid; int bit; } runproc;

static int scan_programs(runproc *list, int max, int *count)
{
    PROCESSENTRY32W pe; HANDLE s; WCHAR path[MAX_PATH], dir[MAX_PATH + 2]; int r = 0, bit, len;
    *count = 0;
    s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return 0;
    cat3(dir, g_dir, L"\\", NULL); len = lstrlenW(dir);
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(s, &pe)) do {
        bit = !lstrcmpiW(pe.szExeFile, L"DynaRun V3.exe") ? RUN_DYNARUN : !lstrcmpiW(pe.szExeFile, L"DynaRunFix.exe") ? RUN_LAUNCHER :
              !lstrcmpiW(pe.szExeFile, L"LEProc.exe") ? RUN_LEPROC : 0;
        if (!bit) continue;
        if (bit != RUN_DYNARUN) {
            process_path(pe.th32ProcessID, path, MAX_PATH);
            // a path that cannot be read counts as ours: better than a half-updated install
            if (path[0] && !(lstrlenW(path) > len && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, path, len, dir, len) == CSTR_EQUAL)) continue;
        }
        r |= bit;
        if (*count < max) { list[*count].pid = pe.th32ProcessID; list[*count].bit = bit; (*count)++; }
    } while (Process32NextW(s, &pe));
    CloseHandle(s);
    return r;
}

int running_programs(void)
{ runproc l[16]; int n; return scan_programs(l, 16, &n); }

static BOOL CALLBACK close_window(HWND h, LPARAM pid)
{
    DWORD p;
    GetWindowThreadProcessId(h, &p);
    if (p == (DWORD)pid && IsWindowVisible(h)) PostMessageW(h, WM_CLOSE, 0, 0);
    return TRUE;
}

static void wait_gone(runproc *l, int n, int bits, DWORD ms)
{
    HANDLE h[16]; int i, k = 0;
    for (i = 0; i < n; i++) if ((l[i].bit & bits) && (h[k] = OpenProcess(SYNCHRONIZE, FALSE, l[i].pid))) k++;
    if (k) WaitForMultipleObjects(k, h, TRUE, ms);
    while (k) CloseHandle(h[--k]);
}

// Closes DynaRun as if its window were closed (WM_CLOSE); what is still running 10 s later (e.g. a
// question DynaRun asks on exit) is ended. Runs in the elevated stage: an elevated DynaRun accepts
// neither messages nor TerminateProcess from a process that is not elevated.
static void close_programs(void)
{
    runproc l[16]; int n, i; HANDLE h;
    if (!scan_programs(l, 16, &n)) return;
    for (i = 0; i < n; i++) if (l[i].bit == RUN_DYNARUN) EnumWindows(close_window, l[i].pid);
    wait_gone(l, n, RUN_DYNARUN, 10000);
    wait_gone(l, n, RUN_LAUNCHER | RUN_LEPROC, 2000);     // LEProc waits for DynaRun, the launcher ends by itself
    scan_programs(l, 16, &n);
    for (i = 0; i < n; i++)
        if ((h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, l[i].pid))) {
            TerminateProcess(h, 1); WaitForSingleObject(h, 5000); CloseHandle(h);
        }
}

/* ---------- COM registrations: HKU\<sid>\Software\Classes -> HKLM\SOFTWARE\Classes ---------- */

static WCHAR *g_keys[1024]; static int g_nkeys;

static void add_key(const WCHAR *k)
{
    int i;
    for (i = 0; i < g_nkeys; i++) if (!lstrcmpiW(g_keys[i], k)) return;
    if (g_nkeys < 1024 && (g_keys[g_nkeys] = alloc((lstrlenW(k) + 1) * sizeof(WCHAR)))) lstrcpyW(g_keys[g_nkeys++], k);
}

static BOOL has_key(HKEY root, const WCHAR *p)
{ HKEY k; if (RegOpenKeyExW(root, p, 0, KEY_READ | WOW, &k)) return FALSE; RegCloseKey(k); return TRUE; }

static BOOL listed(WCHAR *server)
{
    WCHAR *n = server, *p; int i;
    for (p = server; *p; p++) { if (*p == '\\' || *p == '/') n = p + 1; if (*p == '"') *p = 0; }
    for (i = 0; i < (int)(sizeof(COM_FILES) / sizeof(*COM_FILES)); i++) if (!lstrcmpiW(n, COM_FILES[i])) return TRUE;
    return FALSE;
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

static void reg_export(const WCHAR *key, const WCHAR *file)   // backup with the native reg.exe
{
    static WCHAR exe[MAX_PATH], cmd[1024]; STARTUPINFOW si; PROCESS_INFORMATION pi;
    GetWindowsDirectoryW(exe, MAX_PATH); lstrcatW(exe, L"\\Sysnative\\reg.exe");
    if (!exists(exe)) { GetWindowsDirectoryW(exe, MAX_PATH); lstrcatW(exe, L"\\System32\\reg.exe"); }
    wsprintfW(cmd, L"reg.exe export \"HKLM\\SOFTWARE\\Classes\\%s\" \"%s\"", key, file);
    zero(&si, sizeof(si)); si.cb = sizeof(si);
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
}

static void promote_com(const WCHAR *sid)
{
    static const WCHAR *views[] = { L"WOW6432Node\\CLSID", L"CLSID" };
    static WCHAR path[600], rel[300], id[128], v[MAX_PATH], prog[300], log[MAX_PATH], bak[MAX_PATH], line[700];
    HKEY hu, hm, list, s, d; DWORD i, n; int vi, j; WCHAR *dot;

    if (RegOpenKeyExW(HKEY_USERS, cat3(path, sid, L"\\Software\\Classes", NULL), 0, KEY_READ | WOW, &hu)) return;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes", 0, KEY_ALL_ACCESS | WOW, &hm)) { RegCloseKey(hu); return; }
    for (vi = 0; vi < 2; vi++) {
        if (RegOpenKeyExW(hu, views[vi], 0, KEY_READ | WOW, &list)) continue;
        for (i = 0; n = 128, !RegEnumKeyExW(list, i, id, &n, NULL, NULL, NULL, NULL); i++) {
            cat3(rel, views[vi], L"\\", id);
            if (!reg_str(hu, cat3(path, rel, L"\\InprocServer32", NULL), NULL, v, MAX_PATH, WOW) || !listed(v)) continue;
            if (!reg_str(hm, path, NULL, v, MAX_PATH, WOW)) add_key(rel);
            if (reg_str(hu, cat3(path, rel, L"\\ProgID", NULL), NULL, prog, 250, WOW)) {
                for (j = 0; j < 2; j++) {     // "Lib.Class.1" and its version-independent "Lib.Class"
                    if (j) { for (dot = prog + lstrlenW(prog); dot > prog && dot[-1] >= '0' && dot[-1] <= '9'; dot--); if (dot > prog && dot[-1] == '.' && *dot) dot[-1] = 0; else break; }
                    if (has_key(hu, prog) && !reg_str(hm, cat3(path, prog, L"\\CLSID", NULL), NULL, v, MAX_PATH, WOW)) add_key(prog);
                }
            }
            if (reg_str(hu, cat3(path, rel, L"\\TypeLib", NULL), NULL, v, 128, WOW) &&
                has_key(hu, cat3(path, L"TypeLib\\", v, NULL)) && !typelib_complete(hm, v)) add_key(path);
        }
        RegCloseKey(list);
    }
    cat3(log, g_dir, L"\\com-copied.txt", NULL);
    for (j = 0; j < g_nkeys; j++) {
        if (has_key(hm, g_keys[j])) {           // keep what was there before
            cat3(bak, g_dir, L"\\com-backup", NULL); CreateDirectoryW(bak, NULL);
            wsprintfW(bak + lstrlenW(bak), L"\\%lu-%d.reg", GetTickCount(), j);
            reg_export(g_keys[j], bak);
        }
        if (RegOpenKeyExW(hu, g_keys[j], 0, KEY_READ | WOW, &s)) continue;
        if (!RegCreateKeyExW(hm, g_keys[j], 0, NULL, 0, KEY_ALL_ACCESS | WOW, NULL, &d, NULL)) {
            copy_tree(s, d); RegCloseKey(d);
            append_line(log, cat3(line, L"HKLM\\SOFTWARE\\Classes\\", g_keys[j], NULL));
        }
        RegCloseKey(s);
    }
    RegCloseKey(hm); RegCloseKey(hu);
}

/* ---------- shortcuts ---------- */

typedef UINT (WINAPI *MsiGetShortcutTargetW_t)(LPCWSTR, LPWSTR, LPWSTR, LPWSTR);
typedef int (WINAPI *MsiGetComponentPathW_t)(LPCWSTR, LPCWSTR, LPWSTR, DWORD *);
static MsiGetShortcutTargetW_t pMsiGetShortcutTarget;
static MsiGetComponentPathW_t pMsiGetComponentPath;

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
static int classify(const WCHAR *p)
{
    WCHAR t[MAX_PATH], prod[40], feat[40], comp[40]; DWORD n = MAX_PATH; IShellLinkW *sl = load_link(p); int r = LNK_OTHER;
    if (!sl) return r;
    t[0] = 0;
    IShellLinkW_GetPath(sl, t, MAX_PATH, NULL, 0);
    IShellLinkW_Release(sl);
    if (!lstrcmpiW(t, g_launcher)) return LNK_OURS;
    if (!lstrcmpiW(t, g_exe)) return LNK_DYNARUN;
    // the original setup is an MSI: its shortcuts are "advertised" and point to the product, not the exe
    if (pMsiGetShortcutTarget && !pMsiGetShortcutTarget(p, prod, feat, comp) &&
        pMsiGetComponentPath(prod, comp, t, &n) >= 0 && !lstrcmpiW(t, g_exe)) r = LNK_DYNARUN;
    return r;
}

static BOOL save_link(const WCHAR *p, const WCHAR *icon, int idx, WORD hotkey, int show, BOOL runas)
{
    IShellLinkW *sl; IPersistFile *pf; IShellLinkDataList *dl; WCHAR wd[MAX_PATH], *e; HRESULT hr = E_FAIL; DWORD fl;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) return FALSE;
    lstrcpyW(wd, g_exe); for (e = wd + lstrlenW(wd); e > wd && *e != '\\'; e--); *e = 0;
    IShellLinkW_SetPath(sl, g_launcher);
    IShellLinkW_SetWorkingDirectory(sl, wd);
    IShellLinkW_SetIconLocation(sl, icon, idx);
    IShellLinkW_SetDescription(sl, L"DynaRun V3 (DynaRunFix)");
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

// Points an existing DynaRun shortcut at the launcher; keeps its name, icon, hotkey and
// "Run as administrator" flag. The original file is saved in <backup>\Shortcuts.
static void retarget(const WCHAR *p, HKEY backup)
{
    IShellLinkW *sl = load_link(p); IShellLinkDataList *dl; WCHAR icon[MAX_PATH]; int idx = 0, show = 0;
    WORD hotkey = 0; DWORD fl = 0, n; void *orig;
    if (!sl) return;
    icon[0] = 0;
    IShellLinkW_GetIconLocation(sl, icon, MAX_PATH, &idx);
    IShellLinkW_GetHotkey(sl, &hotkey);
    IShellLinkW_GetShowCmd(sl, &show);
    if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IShellLinkDataList, (void **)&dl))) { IShellLinkDataList_GetFlags(dl, &fl); IShellLinkDataList_Release(dl); }
    IShellLinkW_Release(sl);
    if (!icon[0]) { lstrcpyW(icon, g_exe); idx = 0; }
    if (!(orig = read_file(p, &n))) return;
    if (!RegSetValueExW(backup, p, 0, REG_BINARY, orig, n)) save_link(p, icon, idx, hotkey, show, (fl & SLDF_RUNAS_USER) != 0);
    release(orig);
}

static int scan(const WCHAR *dir, BOOL recurse, HKEY backup)   // returns number of DynaRun shortcuts
{
    WCHAR *p = alloc(2 * MAX_PATH * sizeof(WCHAR)); WIN32_FIND_DATAW fd; HANDLE f; int found = 0, c; WCHAR *ext;
    if (!p) return 0;
    f = FindFirstFileW(cat3(p, dir, L"\\*", NULL), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == '.') continue;
            cat3(p, dir, L"\\", fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (recurse) found += scan(p, TRUE, backup); continue; }
            ext = p + lstrlenW(p) - 4;
            if (ext < p || lstrcmpiW(ext, L".lnk")) continue;
            if ((c = classify(p)) == LNK_DYNARUN) retarget(p, backup);
            if (c != LNK_OTHER) found++;
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    release(p);
    return found;
}

static int fix_shortcuts(BOOL common, HKEY backup)
{
    static const int user[] = { CSIDL_DESKTOPDIRECTORY, CSIDL_PROGRAMS, CSIDL_APPDATA },
                     all[] = { CSIDL_COMMON_DESKTOPDIRECTORY, CSIDL_COMMON_PROGRAMS };
    WCHAR d[MAX_PATH]; int i, desk = 0, n = common ? 2 : 3, csidl;
    HMODULE msi = LoadLibraryW(L"msi.dll");
    if (msi) {
        pMsiGetShortcutTarget = (MsiGetShortcutTargetW_t)GetProcAddress(msi, "MsiGetShortcutTargetW");
        pMsiGetComponentPath = (MsiGetComponentPathW_t)GetProcAddress(msi, "MsiGetComponentPathW");
        if (!pMsiGetComponentPath) pMsiGetShortcutTarget = NULL;
    }
    for (i = 0; i < n; i++) {
        csidl = common ? all[i] : user[i];
        if (!SHGetSpecialFolderPathW(NULL, d, csidl, FALSE)) continue;
        if (csidl == CSIDL_APPDATA) lstrcatW(d, L"\\Microsoft\\Internet Explorer\\Quick Launch");  // incl. pinned taskbar items
        if (i == 0) desk = scan(d, FALSE, backup); else scan(d, TRUE, backup);
    }
    return desk;
}

static void restore_shortcuts(HKEY root, const WCHAR *key)
{
    HKEY k; DWORD i, nmax = 0, dmax = 0, n, dn, type; WCHAR *name; BYTE *data; WCHAR sub[64];
    if (RegOpenKeyExW(root, cat3(sub, key, L"\\Shortcuts", NULL), 0, KEY_READ, &k)) return;
    RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &nmax, &dmax, NULL, NULL);
    name = alloc((nmax + 2) * sizeof(WCHAR)); data = alloc(dmax + 2);
    for (i = 0; name && data && (n = nmax + 1, dn = dmax, !RegEnumValueW(k, i, name, &n, NULL, &type, data, &dn)); i++) {
        if (classify(name) != LNK_OURS && exists(name)) continue;   // changed by the user since: leave it
        if (!dn) DeleteFileW(name);                                 // a shortcut we created
        else write_file(name, data, dn);
    }
    release(name); release(data);
    RegCloseKey(k);
    RegDeleteKeyW(root, sub);
}

/* ---------- stages ---------- */

static int machine_install(const WCHAR *sid)
{
    static WCHAR p[MAX_PATH + 16];
    HKEY k;
    if (g_close) close_programs();
    if (running_programs()) return RC_RUNNING;          // checked again here: DynaRun may have been started since
    CreateDirectoryW(g_dir, NULL);
    if (!extract(1, g_launcher) || !extract(2, cat3(p, g_dir, L"\\dynafix.dll", NULL)) ||
        !extract(4, cat3(p, g_dir, L"\\LICENSE-miniz.txt", NULL))) {
        error2(T(L"Cannot write the program files. Close DynaRun and try again.", L"無法寫入程式檔案。請關閉 DynaRun 後再試一次。"), g_dir);
        return 1;
    }
    {   // Locale Emulator, resources 10.. (see setup.rc)
        static const WCHAR *le[] = { L"LEProc.exe", L"LoaderDll.dll", L"LocaleEmulator.dll", L"LECommonLibrary.dll",
                                     L"LEConfig.xml", L"COPYING", L"COPYING.LESSER", L"THIRD-PARTY.txt" };
        int i;
        CreateDirectoryW(cat3(p, g_dir, L"\\le", NULL), NULL);
        for (i = 0; i < (int)(sizeof(le) / sizeof(le[0])); i++)
            if (!extract(10 + i, cat3(p, g_dir, L"\\le\\", le[i]))) {
                error2(T(L"Cannot write the program files. Close DynaRun and try again.", L"無法寫入程式檔案。請關閉 DynaRun 後再試一次。"), p);
                return 1;
            }
    }
    cat3(p, g_dir, L"\\DynaRunFix-Setup.exe", NULL);
    if (lstrcmpiW(p, g_self) && ((!CopyFileW(g_self, p, FALSE) && !(move_aside(p) && CopyFileW(g_self, p, FALSE))) || !same_file(g_self, p))) {
        error2(T(L"Cannot copy the uninstaller.", L"無法複製解除安裝程式。"), p);
        return 1;
    }
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, APPKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) return 1;
    set_str(k, L"InstallDir", g_dir);
    set_str(k, L"DynaRunExe", g_exe);

    promote_com(sid);

    // garbled text under "Beta: Use Unicode UTF-8": external manifest with activeCodePage=Legacy
    if (GetACP() == CP_UTF8 && !exists(cat3(p, g_exe, L".manifest", NULL)) && extract(3, p)) {
        set_dword(k, L"ManifestInstalled", 1);
        touch(g_exe);
    }

    { HKEY b; if (!RegCreateKeyExW(k, L"Shortcuts", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &b, NULL)) { fix_shortcuts(TRUE, b); RegCloseKey(b); } }
    RegCloseKey(k);

    if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, UNINSTKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) {
        set_str(k, L"DisplayName", L"DynaRunFix");
        set_str(k, L"DisplayVersion", WIDEN(DRF_VERSION));
        set_str(k, L"Publisher", L"DynaRunFix (github.com/timliudev/DynaRunFix)");
        set_str(k, L"URLInfoAbout", L"https://github.com/timliudev/DynaRunFix");
        set_str(k, L"InstallLocation", g_dir);
        cat3(p, g_exe, L",0", NULL); set_str(k, L"DisplayIcon", p);
        cat3(p, L"\"", g_dir, L"\\DynaRunFix-Setup.exe\" /uninstall"); set_str(k, L"UninstallString", p);
        set_dword(k, L"NoModify", 1); set_dword(k, L"NoRepair", 1); set_dword(k, L"EstimatedSize", 100);
        RegCloseKey(k);
    }
    return 0;
}

static const WCHAR TEMP_PREFIX[] = L"DynaRunFix-uninstall-";   // copy of the uninstaller in %TEMP%
static BOOL is_temp_copy(void)
{
    WCHAR *n = g_self + lstrlenW(g_self), c; int len = lstrlenW(TEMP_PREFIX); BOOL r;
    while (n > g_self && n[-1] != '\\') n--;
    if (lstrlenW(n) < len) return FALSE;
    c = n[len]; n[len] = 0; r = !lstrcmpiW(n, TEMP_PREFIX); n[len] = c;
    return r;
}

static int machine_uninstall(void)
{
    static WCHAR p[MAX_PATH + 16]; HKEY k; DWORD man = 0, n = 4;
    restore_shortcuts(HKEY_LOCAL_MACHINE, APPKEY);
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, APPKEY, 0, KEY_READ, &k)) { RegQueryValueExW(k, L"ManifestInstalled", NULL, NULL, (BYTE *)&man, &n); RegCloseKey(k); }
    if (man == 1 && DeleteFileW(cat3(p, g_exe, L".manifest", NULL))) touch(g_exe);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, APPKEY);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, UNINSTKEY);
    delete_tree(g_dir);
    if (is_temp_copy()) MoveFileExW(g_self, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
    return 0;
}

// Runs "<self> <args>" elevated (one UAC prompt) and returns its exit code; 2 = cancelled.
static int run_elevated(const WCHAR *args)
{
    SHELLEXECUTEINFOW se; DWORD rc = 1, err; static WCHAR a[1024], root[4], tmp[MAX_PATH]; const WCHAR *exe = g_self;
    lstrcpyW(a, args); if (g_quiet) lstrcatW(a, L" /quiet"); if (g_close) lstrcatW(a, L" /close");
    // Mapped network drives (e.g. a VM's shared folder) do not exist for the elevated process: run a copy in %TEMP%
    lstrcpynW(root, g_self, 4);
    if (root[1] == ':' && GetDriveTypeW(root) == DRIVE_REMOTE && GetTempPathW(MAX_PATH - 40, tmp)) {
        wsprintfW(tmp + lstrlenW(tmp), L"DynaRunFix-Setup-%lu.exe", GetTickCount());
        if (CopyFileW(g_self, tmp, FALSE)) exe = tmp;
    }
    zero(&se, sizeof(se));
    se.cbSize = sizeof(se); se.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    se.hwnd = g_hwnd; se.lpVerb = L"runas"; se.lpFile = exe; se.lpParameters = a; se.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&se)) {
        err = GetLastError();
        if (exe != g_self) DeleteFileW(exe);
        return err == ERROR_CANCELLED ? 2 : 1;
    }
    WaitForSingleObject(se.hProcess, INFINITE);
    GetExitCodeProcess(se.hProcess, &rc);
    CloseHandle(se.hProcess);
    if (exe != g_self) DeleteFileW(exe);
    return (int)rc;
}

static const WCHAR *no_admin(void)
{ return T(L"Administrator permission was not granted. Nothing was changed.", L"未取得系統管理員權限，沒有做任何變更。"); }

// Elevated: install DynaRun from its MSI, then the machine stage. The MSI installs per user (as the
// original setup does), so it must run as the user who asked for it: with over-the-shoulder
// elevation (another account typed into the UAC prompt) return 3 and let the caller run it.
static int machine_full(const WCHAR *sid, const WCHAR *msi)
{
    static WCHAR me[200], txt[300]; int rc;
    if (!user_sid(me, 200) || lstrcmpiW(me, sid)) return 3;
    if (g_close) close_programs();
    if (running_programs()) return RC_RUNNING;
    rc = run_msiexec(msi);
    if (rc != 0 && rc != ERROR_SUCCESS_REBOOT_REQUIRED) {
        if (rc == ERROR_INSTALL_USEREXIT) return 2;
        wsprintfW(txt, T(L"The DynaRun V3 setup failed (Windows Installer error %d).", L"DynaRun V3 安裝失敗（Windows Installer 錯誤 %d）。"), rc);
        if (rc == ERROR_INSTALL_ALREADY_RUNNING)
            lstrcatW(txt, T(L"\n\nAnother installation is running. Wait until it finishes and try again.", L"\n\n有其他程式正在安裝，請等它完成後再試一次。"));
        msg(txt, MB_ICONERROR);
        return 4;
    }
    if (!locate_dynarun()) { msg(T(L"DynaRun V3 was installed, but DynaRun V3.exe was not found.", L"DynaRun V3 已安裝，但找不到 DynaRun V3.exe。"), MB_ICONERROR); return 5; }
    return machine_install(sid);
}

// Not elevated: the current user's shortcuts; a desktop shortcut when there is none at all.
static void user_stage(void)
{
    static WCHAR p[MAX_PATH]; HKEY b, s;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, USERKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &b, NULL)) return;
    if (!RegCreateKeyExW(b, L"Shortcuts", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL)) {
        int desk = fix_shortcuts(FALSE, s);
        if (!desk && SHGetSpecialFolderPathW(NULL, p, CSIDL_COMMON_DESKTOPDIRECTORY, FALSE))
            desk = scan(p, FALSE, s);   // already handled by the machine stage; only counted here
        if (!desk && SHGetSpecialFolderPathW(NULL, p, CSIDL_DESKTOPDIRECTORY, FALSE) &&
            save_link(lstrcatW(p, L"\\DynaRun V3.lnk"), g_exe, 0, 0, 0, FALSE))
            RegSetValueExW(s, p, 0, REG_BINARY, (const BYTE *)"", 0);
        RegCloseKey(s);
    }
    RegCloseKey(b);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}

int install_fix(void)
{
    static WCHAR sid[200], args[600]; int rc;
    if (!user_sid(sid, 200)) return 1;
    if (running_programs() && !g_close) return RC_RUNNING;   // before the UAC prompt; /quiet without /close ends here
    CoInitialize(NULL);
    if (is_admin()) rc = machine_install(sid);
    else {
        wsprintfW(args, L"/machine %s \"%s\"", sid, g_exe);
        if ((rc = run_elevated(args)) == 2) { msg(no_admin(), MB_ICONWARNING); return 2; }
    }
    if (rc) return rc;
    user_stage();
    return 0;
}

int install_all(const WCHAR *msi)
{
    static WCHAR sid[200], args[MAX_PATH + 300]; int rc;
    if (!user_sid(sid, 200)) return 1;
    if (running_programs() && !g_close) return RC_RUNNING;
    CoInitialize(NULL);
    pkg_prepare_documents(msi);         // as this user, before the SYSTEM-side install (error 1305 otherwise)
    if (is_admin()) rc = machine_full(sid, msi);
    else {
        wsprintfW(args, L"/full %s \"%s\"", sid, msi);
        rc = run_elevated(args);
    }
    if (rc == 3) {                      // over-the-shoulder elevation: setup as this user, then the fix
        rc = run_msiexec(msi);
        if (rc == ERROR_INSTALL_USEREXIT) return 2;
        if (rc && rc != ERROR_SUCCESS_REBOOT_REQUIRED) return 4;
        if (!locate_dynarun()) return 5;
        return install_fix();
    }
    if (rc == 2) return 2;
    if (rc) return rc;
    locate_dynarun();
    user_stage();
    return 0;
}

static int uninstall(void)
{
    static WCHAR tmp[MAX_PATH], p[MAX_PATH], cmd[MAX_PATH + 64]; int rc;
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    // the uninstaller lives in the folder it removes: continue from a copy in %TEMP%
    cat3(p, g_dir, L"\\DynaRunFix-Setup.exe", NULL);
    if (!lstrcmpiW(p, g_self)) {
        GetTempPathW(MAX_PATH, tmp);
        wsprintfW(tmp + lstrlenW(tmp), L"%s%lu.exe", TEMP_PREFIX, GetTickCount());
        wsprintfW(cmd, L"\"%s\" /uninstall%s", tmp, g_quiet ? L" /quiet" : L"");
        zero(&si, sizeof(si)); si.cb = sizeof(si);
        if (CopyFileW(g_self, tmp, FALSE) && CreateProcessW(tmp, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForInputIdle(pi.hProcess, 5000);   // exit right away so this file is not locked
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            return 0;
        }
    }
    if (!exists(p) && !reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"DynaRunExe", g_exe, MAX_PATH, 0)) {
        info(T(L"DynaRunFix is not installed.", L"DynaRunFix 尚未安裝。"));
        return 0;
    }
    if (!g_quiet && msg(T(L"Uninstall DynaRunFix and restore the original DynaRun shortcuts?\n\nDynaRun V3 itself stays installed.",
                          L"要解除安裝 DynaRunFix，並還原 DynaRun 原本的捷徑嗎？\n\nDynaRun V3 本身會保留。"), MB_YESNO | MB_ICONQUESTION) != IDYES) return 2;
    reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"DynaRunExe", g_exe, MAX_PATH, 0);
    CoInitialize(NULL);
    restore_shortcuts(HKEY_CURRENT_USER, USERKEY);
    RegDeleteKeyW(HKEY_CURRENT_USER, USERKEY);
    rc = is_admin() ? machine_uninstall() : run_elevated(L"/unmachine");
    if (rc == 2) { msg(no_admin(), MB_ICONWARNING); return 2; }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    if (!rc) info(T(L"DynaRunFix has been uninstalled.", L"DynaRunFix 已解除安裝。"));
    return rc;
}

void WinMainCRTStartup(void)
{
    int argc, i, rc; WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_zh = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
    init_paths();
    for (i = 1; i < argc; i++) if (!lstrcmpiW(argv[i], L"/quiet")) g_quiet = TRUE; else if (!lstrcmpiW(argv[i], L"/close")) g_close = TRUE;
    if (argc >= 4 && !lstrcmpiW(argv[1], L"/machine")) {
        lstrcpynW(g_exe, argv[3], MAX_PATH);
        CoInitialize(NULL);
        rc = machine_install(argv[2]);
    } else if (argc >= 4 && !lstrcmpiW(argv[1], L"/full")) {
        CoInitialize(NULL);
        rc = machine_full(argv[2], argv[3]);
    } else if (argc >= 2 && !lstrcmpiW(argv[1], L"/unmachine")) {
        reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"DynaRunExe", g_exe, MAX_PATH, 0);
        CoInitialize(NULL);
        rc = machine_uninstall();
    } else if (argc >= 2 && !lstrcmpiW(argv[1], L"/uninstall")) rc = uninstall();
    else if (g_quiet) rc = locate_dynarun() ? install_fix() : 1;
    else rc = wizard();
    // the elevated part (/machine, /full) ends here: let the wizard that started it take the foreground back
    AllowSetForegroundWindow(ASFW_ANY);
    ExitProcess(rc);
}
