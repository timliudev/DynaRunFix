// DynaRunFix-Setup.exe - installs DynaRunFix for end users (Windows XP .. 11).
//
//   DynaRunFix-Setup.exe                      wizard (wizard.c): installs DynaRun V3 first when it is
//                                             missing (official download, password, license), then the fix
//   DynaRunFix-Setup.exe /quiet               install the fix only, no UI (DynaRun must be installed);
//                                             exit code 6 (RC_RUNNING), nothing changed, while DynaRun runs;
//   DynaRunFix-Setup.exe /quiet /close        the same, but closes a running DynaRun first
//   /notaskbar, /noautostart                  with any install: do not pin DynaRun to the taskbar /
//                                             do not start it when the user signs in (both are on by default,
//                                             also with /quiet; the wizard's first page has the two check boxes)
//   /keep                                     with any install: leave the taskbar pin, the sign-in start and the
//                                             desktop shortcut as they are (an update, see update.c)
//   DynaRunFix-Setup.exe /uninstall [/quiet]  uninstall (also run from "Programs and Features")
//   DynaRunFix-Setup.exe /checkupdate         /update <args>, /offer: the installed copy, run by the launcher (update.c)
//
// Install, in two stages so that per-user changes land in the right profile even when an
// administrator's credentials are typed into the UAC prompt:
//   machine stage (elevated, "/machine <user SID> <DynaRun exe>"): copies DynaRunFix.exe,
//     dynafix.dll and this program to Program Files\DynaRunFix (copies no COM
//     registrations to HKLM), adds the
//     code-page manifest when the system ANSI code page is UTF-8, puts Locale Emulator in le\ (the
//     launcher uses it only with the UTF-8 option on a zh-TW system), retargets all-users shortcuts,
//     registers the uninstaller; on Windows 11 the taskbar pin (Microsoft's taskbar layout policy for the
//     user, needs admin; "/pin" in the arguments).
//   user stage (not elevated): retargets the user's DynaRun shortcuts to the launcher (the original
//     .lnk bytes are kept in the registry and put back on uninstall); creates a desktop shortcut
//     when there is none; pins DynaRun to the taskbar (per Windows version, see pin_taskbar); the
//     "Run" value that starts it at sign-in.
// Uninstall: the elevated stage runs first (a cancelled UAC prompt changes nothing), then the user's own
// shortcuts, Run value and pins are put back from the backups. The elevated stage puts back the all-users shortcuts and
// points other accounts' shortcuts that still start the launcher back at DynaRun V3.exe. Machine-wide COM keys that
// older versions copied are left alone (removing them could break elevated DynaRun); so are com-copied.txt and com-backup.
// Hardening of the elevated stage (see the comments at the functions): the vendor MSI is copied to an administrators-only
// folder and checked again there; the exe time stamp that is changed to refresh the manifest state is written to HKLM before it
// is changed, and put back at the next run if that failed.
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <sddl.h>
#include <aclapi.h>
#include <tlhelp32.h>
#include <delayimp.h>
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
static const WCHAR RUNKEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR POLKEY[] = L"Software\\Policies\\Microsoft\\Windows\\Explorer";
static const WCHAR APPID[] = L"DynaRunFix.DynaRunV3";           // AppUserModelID of the launcher shortcuts and of DynaRun (dynafix.c)
static const PROPERTYKEY PKEY_AUMID = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 5 };
// technique described by Gee Law (geelaw.blog/entries/msedge-pins/), also used by Firefox's PinToTaskbar plugin (MPL 2.0): calling the
// undocumented IPinnedList3::Modify of Explorer's taskband. The GUIDs and the vtable layout are facts of that interface; the code is
// our own (compared with Firefox's PinToTaskbar.cpp: different structure and wording, nothing copied).
static const GUID CLSID_TaskbandPin = { 0x90aa3a4e, 0x1cba, 0x4233, { 0xb8, 0xbb, 0x53, 0x57, 0x73, 0xd4, 0x84, 0x49 } };
static const GUID IID_IPinnedList3 = { 0x0dd79ae2, 0xd156, 0x45d4, { 0x9e, 0xeb, 0x3b, 0x54, 0x97, 0x69, 0xe9, 0x40 } };
typedef struct IPinnedList3 { const struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(struct IPinnedList3 *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(struct IPinnedList3 *);
    ULONG (STDMETHODCALLTYPE *Release)(struct IPinnedList3 *);
    void *others[13];
    HRESULT (STDMETHODCALLTYPE *Modify)(struct IPinnedList3 *, PCIDLIST_ABSOLUTE unpin, PCIDLIST_ABSOLUTE pin, int caller);
} *lpVtbl; } IPinnedList3;
static const WCHAR UNINSTKEY[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DynaRunFix";
static const WCHAR DYNARUN_REL[] = L"\\Dyna Pro Dynamometers\\DynaRun V3.exe";

BOOL g_zh, g_quiet, g_close, g_pin = TRUE, g_autostart = TRUE, g_keep;
int g_drvmode;                    // DRV_MODE_*: the wizard starts at the driver pages (driver.c, wizard.c)
int g_pinresult;
HWND g_hwnd;
WCHAR g_self[MAX_PATH], g_dir[MAX_PATH], g_launcher[MAX_PATH], g_exe[MAX_PATH];
static WCHAR g_user_sid[200];     // the user who started the setup (elevated stage: from its command line)
static BOOL g_exe_trusted;        // check_exe: DynaRun's exe and folder cannot be changed by ordinary users
#define TITLE T(L"DynaRunFix Setup", L"DynaRunFix 安裝程式")

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
    static char u[2048]; int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, sizeof(u) - 2, NULL, NULL); DWORD w;
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
                if (lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 >= 2 * MAX_PATH) { kept = TRUE; continue; }
                if (depth == 0 && name_in(keep, fd.cFileName)) { kept = TRUE; continue; }
                cat3(p, dir, L"\\", fd.cFileName);
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

/* ---------- elevated stage: one at a time, trust checks, the borrowed time stamp ---------- */

// The elevated stages (install, uninstall, MSI) of two setups must not interleave. Recursive for the owning thread.
static HANDLE g_stage_mx; static int g_stage_depth;

static BOOL stage_lock(void)
{
    DWORD r;
    if (!g_stage_mx && !(g_stage_mx = CreateMutexW(NULL, FALSE, L"Global\\DynaRunFix-machine-stage"))) return FALSE;
    r = WaitForSingleObject(g_stage_mx, 60000);
    if (r != WAIT_OBJECT_0 && r != WAIT_ABANDONED) return FALSE;     // an abandoned mutex (a setup that was killed) is ours now
    g_stage_depth++;
    return TRUE;
}

static void stage_unlock(void) { if (g_stage_depth > 0) { g_stage_depth--; ReleaseMutex(g_stage_mx); } }

static const WCHAR *busy_text(void)
{ return T(L"Another DynaRunFix setup is running. Wait until it finishes and try again.", L"另一個 DynaRunFix 安裝程式正在執行，請等它完成後再試一次。"); }

// A line in <install folder>\setup.log (only the elevated stage writes there; the folder is administrators-only)
static void setup_log(const WCHAR *line)
{
    static WCHAR p[MAX_PATH];
    if (g_dir[0] && exists(g_dir)) append_line(cat3(p, g_dir, L"\\setup.log", NULL), line);
}

static void warn(const WCHAR *text) { setup_log(text); if (!g_quiet) msg(text, MB_ICONWARNING); }

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
static BOOL user_writable(const WCHAR *path)
{
    static const WCHAR *const owners[] = { L"S-1-5-18", L"S-1-5-32-544", L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464", NULL };
    static const WCHAR *const others[] = { L"S-1-1-0", L"S-1-5-32-545", L"S-1-5-11", L"S-1-5-4", L"S-1-5-32-547", NULL };
    const WCHAR *const me[2] = { g_user_sid, NULL };
    PSID owner = NULL; PACL dacl = NULL; PSECURITY_DESCRIPTOR sd = NULL; BOOL bad = TRUE; DWORD i; ACCESS_ALLOWED_ACE *a;
    if (GetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, NULL, &dacl, NULL, &sd)) return TRUE;
    if (owner && dacl && sid_in(owner, owners)) {            // no DACL = full access for everyone: bad
        bad = FALSE;
        for (i = 0; i < dacl->AceCount && !bad; i++) {
            if (!GetAce(dacl, i, (void **)&a)) { bad = TRUE; break; }
            if (a->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || (a->Header.AceFlags & INHERIT_ONLY_ACE) || !(a->Mask & WRITE_MASK)) continue;
            if (sid_in((PSID)&a->SidStart, others) || (g_user_sid[0] && sid_in((PSID)&a->SidStart, me))) bad = TRUE;
        }
    }
    LocalFree(sd);
    return bad;
}

static BOOL (*g_is_writable)(const WCHAR *path) = user_writable;     // the tests replace this

// driver.c: may ordinary users (and the user who started the setup, sid) change what is at path?
BOOL path_user_writable(const WCHAR *path, const WCHAR *sid)
{
    if (sid && sid[0]) lstrcpynW(g_user_sid, sid, 200);
    return g_is_writable(path);
}

static BOOL is_dynarun_name(const WCHAR *p)
{
    const WCHAR *n = p + lstrlenW(p);
    while (n > p && n[-1] != '\\') n--;
    return !lstrcmpiW(n, L"DynaRun V3.exe");
}

typedef DWORD (WINAPI *GetFinalPathNameByHandleW_t)(HANDLE, LPWSTR, DWORD, DWORD);

// The elevated stage does not trust the DynaRun path it was given on its command line. It must be an absolute path on a
// fixed drive, named DynaRun V3.exe, an existing file. g_exe_trusted says whether what the elevated stage writes there
// (manifest), starts (the borrowed time stamp) and registers (COM servers in that folder) is safe: no link in the path, and
// neither the folder nor the exe changeable by ordinary users. FALSE: the path is unusable; g_exe_trusted FALSE only: skip those steps.
static BOOL check_exe(void)
{
    WCHAR root[4], fin[MAX_PATH + 8], dir[MAX_PATH], *e; HANDLE h; GetFinalPathNameByHandleW_t f; DWORD n;
    g_exe_trusted = FALSE;
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
    g_exe_trusted = !g_is_writable(dir) && !g_is_writable(g_exe);
    return TRUE;
}

// Observed in testing: Windows keeps using the manifest state it saw at the exe's last start until it sees a different LastWriteTime
// on a process creation. So: read the LastWriteTime, set another value, let Windows see it by creating the exe's process suspended
// and terminating it at once (none of the exe's code runs), then set the original value back. The exe's content and its
// timestamps end up exactly as they were. (Setting the value and restoring it without the process creation in between does not
// make Windows read the manifest.)
// The original time is written to HKLM\SOFTWARE\DynaRunFix (ExeOrigTimePath, ExeOrigTime) before it is changed and removed
// after it has been put back and read back; recover_exe_time puts back what a run that was killed or failed left.
enum { TOUCH_OK, TOUCH_NOTDONE, TOUCH_STUCK };     // NOTDONE: nothing was changed. STUCK: the original time could not be put back
static const WCHAR ORIGPATH[] = L"ExeOrigTimePath", ORIGTIME[] = L"ExeOrigTime";
static HKEY g_app_root = HKEY_LOCAL_MACHINE; static const WCHAR *g_app_key = APPKEY;     // where the record is kept (the tests use a key of their own)

static BOOL same_time(const FILETIME *a, const FILETIME *b) { return a->dwLowDateTime == b->dwLowDateTime && a->dwHighDateTime == b->dwHighDateTime; }

static BOOL time_set_verified(HANDLE h, const FILETIME *t)       // set, read back, retry a few times
{
    FILETIME c; int i;
    for (i = 0; i < 5; i++) {
        if (SetFileTime(h, NULL, NULL, t) && GetFileTime(h, NULL, NULL, &c) && same_time(&c, t)) return TRUE;
        Sleep(300);
    }
    return FALSE;
}

static HANDLE open_times(const WCHAR *p)
{ return CreateFileW(p, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL); }

static void forget_exe_time(HKEY k) { RegDeleteValueW(k, ORIGPATH); RegDeleteValueW(k, ORIGTIME); }

static int recover_exe_time(void)
{
    HKEY k; static WCHAR path[MAX_PATH]; FILETIME saved, cur; DWORD n = sizeof(saved), type; HANDLE h; int rc = TOUCH_OK;
    if (RegOpenKeyExW(g_app_root, g_app_key, 0, KEY_ALL_ACCESS, &k)) return TOUCH_OK;
    if (reg_str(g_app_root, g_app_key, ORIGPATH, path, MAX_PATH, 0) && is_dynarun_name(path) &&
        !RegQueryValueExW(k, ORIGTIME, NULL, &type, (BYTE *)&saved, &n) && type == REG_BINARY && n == sizeof(saved)) {
        if ((h = open_times(path)) == INVALID_HANDLE_VALUE) { if (!exists(path)) forget_exe_time(k); else rc = TOUCH_STUCK; }
        else {
            if (GetFileTime(h, NULL, NULL, &cur) && same_time(&cur, &saved)) forget_exe_time(k);
            else if (time_set_verified(h, &saved)) { forget_exe_time(k); setup_log(L"restored the time stamp of DynaRun V3.exe that an earlier run had left changed"); }
            else rc = TOUCH_STUCK;
            CloseHandle(h);
        }
    }
    RegCloseKey(k);
    return rc;
}

// The temporary time: now, but at least 4 s from the original (the file systems keep 2 s at the coarsest, FAT), on an even second.
// (After rounding down to an even second it is still more than 2 s away.)
static void pick_temp_time(const FILETIME *orig, FILETIME *out)
{
    ULARGE_INTEGER u, o; SYSTEMTIME st;
    GetSystemTimeAsFileTime(out);
    u.LowPart = out->dwLowDateTime; u.HighPart = out->dwHighDateTime; o.LowPart = orig->dwLowDateTime; o.HighPart = orig->dwHighDateTime;
    if ((u.QuadPart > o.QuadPart ? u.QuadPart - o.QuadPart : o.QuadPart - u.QuadPart) < 40000000ULL) u.QuadPart = o.QuadPart + 100000000ULL;
    out->dwLowDateTime = u.LowPart; out->dwHighDateTime = u.HighPart;
    if (FileTimeToSystemTime(out, &st)) { st.wSecond &= ~1; st.wMilliseconds = 0; SystemTimeToFileTime(&st, out); }
}

static int touch(const WCHAR *p)
{
    FILETIME orig, ft; HANDLE h; HKEY k; STARTUPINFOW si; PROCESS_INFORMATION pi; WCHAR *cmd; int rc = TOUCH_NOTDONE;
    if (!p[0] || !is_dynarun_name(p) || !stage_lock()) return TOUCH_NOTDONE;
    h = open_times(p);
    if (h != INVALID_HANDLE_VALUE) {
        if (GetFileTime(h, NULL, NULL, &orig) && !RegCreateKeyExW(g_app_root, g_app_key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) {
            if (!RegSetValueExW(k, ORIGPATH, 0, REG_SZ, (const BYTE *)p, (lstrlenW(p) + 1) * sizeof(WCHAR)) &&
                !RegSetValueExW(k, ORIGTIME, 0, REG_BINARY, (const BYTE *)&orig, sizeof(orig))) {
                pick_temp_time(&orig, &ft);
                if (SetFileTime(h, NULL, NULL, &ft)) {
                    zero(&si, sizeof si); si.cb = sizeof si;
                    if ((cmd = alloc((MAX_PATH + 16) * sizeof(WCHAR)))) {
                        cat3(cmd, L"\"", p, L"\"");
                        if (CreateProcessW(p, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
                            TerminateProcess(pi.hProcess, 0); WaitForSingleObject(pi.hProcess, 5000);
                            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
                        }
                        release(cmd);
                    }
                    if (time_set_verified(h, &orig)) { forget_exe_time(k); rc = TOUCH_OK; }
                    else rc = TOUCH_STUCK;                  // the record stays: the next install or uninstall puts it back
                } else forget_exe_time(k);                  // the time was not changed
            } else forget_exe_time(k);
            RegCloseKey(k);
        }
        CloseHandle(h);
    }
    stage_unlock();
    return rc;
}

// what the caller does with touch's result
static int touch_report(int t)
{
    if (t == TOUCH_STUCK)
        warn(T(L"The modified time of DynaRun V3.exe could not be put back as it was. It is kept in the registry and put back the next time DynaRunFix is installed or uninstalled.",
               L"DynaRun V3.exe 的修改時間無法還原。已記錄在登錄檔，下次安裝或解除安裝 DynaRunFix 時會再還原。"));
    else if (t == TOUCH_NOTDONE) setup_log(L"the manifest was not refreshed: DynaRun V3.exe could not be touched");
    return t;
}


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
    static const int csidl[] = { CSIDL_PROGRAM_FILESX86, CSIDL_PROGRAM_FILES };
    WCHAR pf[MAX_PATH]; int i;
    if (reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"DynaRunExe", g_exe, MAX_PATH, 0) && exists(g_exe)) return TRUE;
    // the program folders from the system (not from environment variables, which a process can set for the elevated stage)
    for (i = 0; i < 2; i++)
        if (SHGetSpecialFolderPathW(NULL, pf, csidl[i], FALSE) && lstrlenW(pf) + lstrlenW(DYNARUN_REL) < MAX_PATH && exists(cat3(g_exe, pf, DYNARUN_REL, NULL))) return TRUE;
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
// DynaRun is closed first (the user agrees in the wizard, or passes /close).
// What holds up the install (judge_proc): the DynaRun V3.exe that is the known one (g_exe; a DynaRun of another folder does not use
// our files), and the launcher and LEProc when they run from the install folder. A program whose exe path cannot be read counts as
// ours (better than a half-updated install). The setup closes only what it can tell is ours and that runs in its own session; the
// rest still holds up the install, but "close" would not help, and the wizard and the log say so.
typedef struct { DWORD pid; int bit; DWORD sess; int why; WCHAR path[MAX_PATH]; } runproc;
enum { JUDGE_SKIP, JUDGE_CLOSE, JUDGE_OTHERSESSION, JUDGE_NOPATH, JUDGE_NOEXE };   // not in the way / can be closed / cannot: why

static BOOL same_path(const WCHAR *a, const WCHAR *b)     // case-insensitive; 8.3 and long names of one file match
{
    WCHAR x[MAX_PATH], y[MAX_PATH]; DWORD m, n;
    if (!a[0] || !b[0]) return FALSE;
    if (!lstrcmpiW(a, b)) return TRUE;
    m = GetLongPathNameW(a, x, MAX_PATH); n = GetLongPathNameW(b, y, MAX_PATH);
    return m && m < MAX_PATH && n && n < MAX_PATH && !lstrcmpiW(x, y);
}

// bit: RUN_* of the program; path: its exe ("" when unreadable); exe: the known DynaRun V3.exe ("" when not known yet);
// dir: the install folder with a trailing backslash
static int judge_proc(int bit, BOOL same_session, const WCHAR *path, const WCHAR *exe, const WCHAR *dir)
{
    int len = lstrlenW(dir);
    if (bit == RUN_DYNARUN) {
        if (!path[0]) return JUDGE_NOPATH;
        if (!exe[0]) return JUDGE_NOEXE;
        if (!same_path(path, exe)) return JUDGE_SKIP;
    } else {
        if (!path[0]) return JUDGE_NOPATH;
        if (!(lstrlenW(path) > len && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, path, len, dir, len) == CSTR_EQUAL)) return JUDGE_SKIP;
    }
    return same_session ? JUDGE_CLOSE : JUDGE_OTHERSESSION;
}

// The programs in the way (up to max in list); the result has their RUN_* bits, with RUN_CANCLOSE / RUN_NOCLOSE for the two kinds.
static int scan_programs(runproc *list, int max, int *count)
{
    PROCESSENTRY32W pe; HANDLE s; WCHAR path[MAX_PATH], dir[MAX_PATH + 2]; int r = 0, bit, why; DWORD mysess = 0, sess;
    *count = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &mysess);
    s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return 0;
    cat3(dir, g_dir, L"\\", NULL);
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(s, &pe)) do {
        bit = !lstrcmpiW(pe.szExeFile, L"DynaRun V3.exe") ? RUN_DYNARUN : !lstrcmpiW(pe.szExeFile, L"DynaRunFix.exe") ? RUN_LAUNCHER :
              !lstrcmpiW(pe.szExeFile, L"LEProc.exe") ? RUN_LEPROC : 0;
        if (!bit) continue;
        if (!ProcessIdToSessionId(pe.th32ProcessID, &sess)) sess = (DWORD)-1;
        process_path(pe.th32ProcessID, path, MAX_PATH);
        why = judge_proc(bit, sess == mysess, path, g_exe, dir);
        if (why == JUDGE_SKIP) continue;
        r |= bit | (why == JUDGE_CLOSE ? RUN_CANCLOSE : RUN_NOCLOSE);
        if (list && *count < max) {
            list[*count].pid = pe.th32ProcessID; list[*count].bit = bit; list[*count].sess = sess; list[*count].why = why;
            lstrcpyW(list[*count].path, path); (*count)++;
        }
    } while (Process32NextW(s, &pe));
    CloseHandle(s);
    return r;
}

int running_programs(void)
{ int n; return scan_programs(NULL, 0, &n); }

// Why the setup stops with RC_RUNNING (nothing is shown with /quiet): one line per program in a log. The elevated stage writes
// setup.log in the install folder; before that folder exists (or without administrator rights) %TEMP%\DynaRunFix-setup.log.
static void run_log(const WCHAR *line)
{
    static WCHAR p[MAX_PATH];
    if (is_admin() && g_dir[0] && exists(g_dir)) { setup_log(line); return; }
    if (GetTempPathW(MAX_PATH - 24, p)) append_line(lstrcatW(p, L"DynaRunFix-setup.log"), line);
}

static void log_running(const WCHAR *stage, const runproc *l, int n, int r)
{
    static const WCHAR *const names[] = { L"DynaRun V3.exe", L"DynaRunFix.exe", L"LEProc.exe" };
    static const WCHAR *const reasons[] = { L"can be closed", L"cannot be closed: runs in another session",
                                            L"cannot be closed: its exe path cannot be read", L"cannot be closed: the DynaRun folder is not known" };
    WCHAR t[900]; SYSTEMTIME st; int i, k; DWORD me = 0;
    GetLocalTime(&st); ProcessIdToSessionId(GetCurrentProcessId(), &me);
    wsprintfW(t, L"%04u-%02u-%02u %02u:%02u:%02u  %s: stopped, exit code 6 (DynaRun is running), nothing was changed. %s", st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond, stage,
              (r & RUN_NOCLOSE) ? (g_close ? L"/close was not used: a program that cannot be closed is running." : L"A program that cannot be closed is running.")
                                : (g_close ? L"It was still running after /close." : L"Run with /close to close it."));
    run_log(t);
    for (i = 0; i < n; i++) {
        k = l[i].bit == RUN_DYNARUN ? 0 : l[i].bit == RUN_LAUNCHER ? 1 : 2;
        wsprintfW(t, L"  %s pid %lu, session %lu (this setup: %lu), %s%s%s", names[k], l[i].pid, l[i].sess, me, reasons[l[i].why - JUDGE_CLOSE],
                  l[i].path[0] ? L", " : L"", l[i].path);
        run_log(t);
    }
}

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
    for (i = 0; i < n; i++) if ((l[i].bit & bits) && l[i].why == JUDGE_CLOSE && (h[k] = OpenProcess(SYNCHRONIZE, FALSE, l[i].pid))) k++;
    if (k) WaitForMultipleObjects(k, h, TRUE, ms);
    while (k) CloseHandle(h[--k]);
}

// Closes DynaRun as if its window were closed (WM_CLOSE); what is still running 10 s later (e.g. a
// question DynaRun asks on exit) is ended. Runs in the elevated stage: an elevated DynaRun accepts
// neither messages nor TerminateProcess from a process that is not elevated.
static void close_programs(void)
{
    runproc *l = alloc(16 * sizeof(runproc)); int n, i; HANDLE h;      // on the heap: a stack of that size would need the C runtime's probe
    if (!l || !(scan_programs(l, 16, &n) & RUN_CANCLOSE)) { release(l); return; }
    for (i = 0; i < n; i++) if (l[i].bit == RUN_DYNARUN && l[i].why == JUDGE_CLOSE) EnumWindows(close_window, l[i].pid);
    wait_gone(l, n, RUN_DYNARUN, 10000);
    wait_gone(l, n, RUN_LAUNCHER | RUN_LEPROC, 2000);     // LEProc waits for DynaRun, the launcher ends by itself
    scan_programs(l, 16, &n);
    for (i = 0; i < n; i++)
        if (l[i].why == JUDGE_CLOSE && (h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, l[i].pid))) {
            TerminateProcess(h, 1); WaitForSingleObject(h, 5000); CloseHandle(h);
        }
    release(l);
}

// The elevated install stages: closes DynaRun first when the user agreed (or /close), then TRUE (and the reason logged) when something
// still holds up the install. Nothing is closed when part of it cannot be closed: the install stops anyway, and a test run would end for nothing.
static BOOL still_running(const WCHAR *stage)
{
    runproc *l = alloc(16 * sizeof(runproc)); int n, r = scan_programs(l, l ? 16 : 0, &n);
    if (g_close && r && !(r & RUN_NOCLOSE)) { close_programs(); r = scan_programs(l, l ? 16 : 0, &n); }
    if (r && l) log_running(stage, l, n, r);
    release(l);
    return r != 0;
}

// Before the UAC prompt: TRUE (logged) when /quiet without /close ends here. With /close the elevated stage decides: it can read more.
static BOOL running_early(const WCHAR *stage)
{
    runproc *l = alloc(16 * sizeof(runproc)); int n, r = scan_programs(l, l ? 16 : 0, &n);
    if (r && !g_close && l) log_running(stage, l, n, r);
    release(l);
    return r && !g_close;
}

/* ---------- COM registrations: HKU\<sid>\Software\Classes -> HKLM\SOFTWARE\Classes ---------- */

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
static const WCHAR *g_export_root = L"HKLM\\SOFTWARE\\Classes\\";     // where reg_export reads from (a test points it elsewhere)

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
    static WCHAR path[600], rel[300], id[128], v[MAX_PATH], prog[300], log[MAX_PATH], bak[MAX_PATH], line[700], tl[128], tlkey[200];
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
            wsprintfW(bak + lstrlenW(bak), L"\\%lu-%d.reg", GetTickCount(), j);
            reg_export(g_keys[j], bak);
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

// The installer copies nothing to HKLM\SOFTWARE\Classes (promote_com_in above is kept only for the tests).

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
static BOOL find_icon(const WCHAR *dir, BOOL recurse)
{
    WCHAR *p = alloc(3 * MAX_PATH * sizeof(WCHAR)), *ic, *x; WIN32_FIND_DATAW fd; HANDLE f; IShellLinkW *sl; int idx; BOOL r = FALSE;
    if (!p) return FALSE;
    ic = p + MAX_PATH; x = ic + MAX_PATH;
    // p has room for MAX_PATH characters only (ic and x follow it): longer paths are skipped
    f = lstrlenW(dir) + 3 > MAX_PATH ? INVALID_HANDLE_VALUE : FindFirstFileW(cat3(p, dir, L"\\*", NULL), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == '.' || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            if (lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 > MAX_PATH) continue;
            if (lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 > 2 * MAX_PATH) continue;     // a path too long for the buffer
            cat3(p, dir, L"\\", fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (recurse) r = find_icon(p, TRUE); continue; }
            if (lstrlenW(p) < 4 || lstrcmpiW(p + lstrlenW(p) - 4, L".lnk") || classify(p) == LNK_OTHER || !(sl = load_link(p))) continue;
            ic[0] = 0; idx = 0;
            IShellLinkW_GetIconLocation(sl, ic, MAX_PATH, &idx);
            IShellLinkW_Release(sl);
            if (ic[0] && lstrcmpiW(ic, g_exe) && ExpandEnvironmentStringsW(ic, x, MAX_PATH) && exists(x)) {
                lstrcpyW(g_icon, ic); g_iconidx = idx; r = TRUE;
            }
        } while (!r && FindNextFileW(f, &fd));
        FindClose(f);
    }
    release(p);
    return r;
}

static void dynarun_icon(void)
{
    static const int dirs[] = { CSIDL_STARTMENU, CSIDL_COMMON_STARTMENU, CSIDL_DESKTOPDIRECTORY, CSIDL_COMMON_DESKTOPDIRECTORY };
    WCHAR d[MAX_PATH]; int i;
    if (g_icontried) return;
    g_icontried = TRUE;
    for (i = 0; i < 4; i++)
        if (SHGetSpecialFolderPathW(NULL, d, dirs[i], FALSE) && find_icon(d, i < 2)) return;
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

static int scan(const WCHAR *dir, BOOL recurse, HKEY backup)   // returns number of DynaRun shortcuts
{
    WCHAR *p = alloc(2 * MAX_PATH * sizeof(WCHAR)); WIN32_FIND_DATAW fd; HANDLE f; int found = 0, c; WCHAR *ext;
    if (!p) return 0;
    f = lstrlenW(dir) + 3 > 2 * MAX_PATH ? INVALID_HANDLE_VALUE : FindFirstFileW(cat3(p, dir, L"\\*", NULL), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == '.') continue;
            if (lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 > 2 * MAX_PATH) continue;     // a path too long for the buffer
            cat3(p, dir, L"\\", fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (recurse) found += scan(p, TRUE, backup); continue; }
            ext = p + lstrlenW(p) - 4;
            if (ext < p || lstrcmpiW(ext, L".lnk")) continue;
            if ((c = classify(p)) == LNK_DYNARUN) retarget(p, backup);
            // ours from an older version (before v1.3.0 they had no AppUserModelID): written again with the ID, else the
            // running DynaRun (ID set by dynafix.dll) matches no shortcut and Windows shows its window icon, not DynaRun's
            else if (c == LNK_OURS) relink(p, g_launcher);
            if (c != LNK_OTHER) found++;
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    release(p);
    return found;
}

static BOOL find_ours(const WCHAR *dir, BOOL recurse, WCHAR *out)    // the first shortcut of ours below dir
{
    WCHAR *p = alloc(2 * MAX_PATH * sizeof(WCHAR)); WIN32_FIND_DATAW fd; HANDLE f; WCHAR *ext; BOOL r = FALSE;
    if (!p) return FALSE;
    f = lstrlenW(dir) + 3 > 2 * MAX_PATH ? INVALID_HANDLE_VALUE : FindFirstFileW(cat3(p, dir, L"\\*", NULL), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == '.' || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            if (lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 > 2 * MAX_PATH) continue;     // a path too long for the buffer
            cat3(p, dir, L"\\", fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (recurse) r = find_ours(p, TRUE, out); continue; }
            ext = p + lstrlenW(p) - 4;
            if (ext >= p && !lstrcmpiW(ext, L".lnk") && classify(p) == LNK_OURS) { lstrcpyW(out, p); r = TRUE; }
        } while (!r && FindNextFileW(f, &fd));
        FindClose(f);
    }
    release(p);
    return r;
}

static int fix_shortcuts(BOOL common, HKEY backup)
{
    // the Start menu root (not just Programs): some DynaRun installs put their shortcut there
    static const int user[] = { CSIDL_DESKTOPDIRECTORY, CSIDL_STARTMENU, CSIDL_PROGRAMS, CSIDL_APPDATA },
                     all[] = { CSIDL_COMMON_DESKTOPDIRECTORY, CSIDL_COMMON_STARTMENU, CSIDL_COMMON_PROGRAMS };
    WCHAR d[MAX_PATH]; int i, desk = 0, n = common ? 3 : 4, csidl;
    HMODULE msi = load_system_dll(L"msi.dll");
    if (msi) {
        pMsiGetShortcutTarget = (MsiGetShortcutTargetW_t)GetProcAddress(msi, "MsiGetShortcutTargetW");
        pMsiGetComponentPath = (MsiGetComponentPathW_t)GetProcAddress(msi, "MsiGetComponentPathW");
        if (!pMsiGetComponentPath) pMsiGetShortcutTarget = NULL;
    }
    for (i = 0; i < n; i++) {
        csidl = common ? all[i] : user[i];
        if (!SHGetSpecialFolderPathW(NULL, d, csidl, FALSE)) continue;
        if (csidl == CSIDL_APPDATA) lstrcatW(d, L"\\Microsoft\\Internet Explorer\\Quick Launch");  // incl. pinned taskbar items
        if (i == 0) desk = scan(d, FALSE, backup);
        else scan(d, csidl != CSIDL_STARTMENU && csidl != CSIDL_COMMON_STARTMENU, backup);  // Programs is scanned on its own
    }
    return desk;
}

// restore_shortcuts writes files at paths that come from the registry. A path is used only when it is a .lnk file below one of the
// shell folders the backups were made from (user: Desktop, Start menu, Programs, Quick Launch, %APPDATA%\DynaRunFix; all users: the
// common ones), with no ".." component and no link (junction) as its folder. When this process is elevated and the backups are in
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

static BOOL lnk_path_ok(const WCHAR *name, BOOL common)
{
    static const int user[] = { CSIDL_DESKTOPDIRECTORY, CSIDL_STARTMENU, CSIDL_PROGRAMS, CSIDL_APPDATA },
                     all[] = { CSIDL_COMMON_DESKTOPDIRECTORY, CSIDL_COMMON_STARTMENU, CSIDL_COMMON_PROGRAMS };
    static WCHAR dirs[10][MAX_PATH]; const WCHAR *list[10];
    WCHAR d[MAX_PATH], *e, par[MAX_PATH], lname[MAX_PATH]; int i, len = lstrlenW(name), n = common ? 3 : 4, nd = 0; DWORD a;
    if (len < 8 || len >= MAX_PATH) return FALSE;
    if (!common && is_admin()) {                                  // elevated: the user's own registry is not trusted beyond their profile
        if (!SHGetSpecialFolderPathW(NULL, d, CSIDL_PROFILE, FALSE)) return FALSE;
        if (!under_dir(name, d)) {
            lstrcpyW(lname, name); long_form(lname); long_form(d);        // 8.3 form on one side only
            if (!under_dir(lname, d)) return FALSE;
        }
    }
    for (i = 0; i < n; i++) {
        if (!SHGetSpecialFolderPathW(NULL, d, common ? all[i] : user[i], FALSE)) continue;
        if ((common ? all[i] : user[i]) == CSIDL_APPDATA) {
            if (lstrlenW(d) + 50 >= MAX_PATH) continue;
            lstrcpyW(dirs[nd], d); lstrcatW(dirs[nd], L"\\DynaRunFix"); list[nd] = dirs[nd]; nd++;          // desktop_link()
            lstrcatW(d, L"\\Microsoft\\Internet Explorer\\Quick Launch");
        }
        lstrcpyW(dirs[nd], d); list[nd] = dirs[nd]; nd++;
        long_form(d);                                              // the backup may hold the long form of a folder that the shell reports in 8.3 form
        lstrcpyW(dirs[nd], d); list[nd] = dirs[nd]; nd++;
    }
    lstrcpyW(par, name); for (e = par + len; e > par && *e != '\\'; e--); *e = 0;
    if (!lnk_name_in_dirs(name, list, nd)) {
        lstrcpyW(lname, par); long_form(lname);                    // the same, with the folder of the file in long form
        if (lstrlenW(lname) + lstrlenW(e + 1) + 2 >= MAX_PATH) return FALSE;
        lstrcatW(lname, L"\\"); lstrcatW(lname, e + 1);
        if (!lnk_name_in_dirs(lname, list, nd)) return FALSE;
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
static void unretarget(const WCHAR *dir, BOOL recurse)
{
    WCHAR *p = alloc(2 * MAX_PATH * sizeof(WCHAR)); WIN32_FIND_DATAW fd; HANDLE f; WCHAR *ext;
    if (!p) return;
    f = lstrlenW(dir) + 3 > 2 * MAX_PATH ? INVALID_HANDLE_VALUE : FindFirstFileW(cat3(p, dir, L"\\*", NULL), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == '.' || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            if (lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 > 2 * MAX_PATH) continue;     // a path too long for the buffer
            cat3(p, dir, L"\\", fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (recurse) unretarget(p, TRUE); continue; }
            ext = p + lstrlenW(p) - 4;
            if (ext >= p && !lstrcmpiW(ext, L".lnk") && g_exe[0] && classify(p) == LNK_OURS) relink(p, g_exe);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    release(p);
}

// What is left of our shortcuts in the shell folders after the backups were put back (a backup that is missing, as when an older version did
// not record one, or was refused): they point to DynaRun V3.exe again instead of the launcher that is being removed.
static void unretarget_shell(BOOL common)
{
    WCHAR d[MAX_PATH];
    if (!g_exe[0]) return;
    if (SHGetSpecialFolderPathW(NULL, d, common ? CSIDL_COMMON_DESKTOPDIRECTORY : CSIDL_DESKTOPDIRECTORY, FALSE)) unretarget(d, FALSE);
    if (SHGetSpecialFolderPathW(NULL, d, common ? CSIDL_COMMON_STARTMENU : CSIDL_STARTMENU, FALSE)) unretarget(d, FALSE);
    if (SHGetSpecialFolderPathW(NULL, d, common ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS, FALSE)) unretarget(d, TRUE);
    if (!common && SHGetSpecialFolderPathW(NULL, d, CSIDL_APPDATA, FALSE) && lstrlenW(d) + 50 < MAX_PATH) {      // pins the user made of the launcher
        lstrcatW(d, L"\\Microsoft\\Internet Explorer\\Quick Launch"); unretarget(d, TRUE);
    }
}

/* ---------- taskbar pin and sign-in start ---------- */

static void drop_run(HKEY hive, BOOL only_ours)     // the "Run" value; of other accounts only when it starts our launcher
{
    static WCHAR v[MAX_PATH]; HKEY k;
    if (only_ours && !(reg_str(hive, RUNKEY, L"DynaRunFix", v, MAX_PATH, 0) && contains_i(v, g_launcher))) return;
    if (!RegOpenKeyExW(hive, RUNKEY, 0, KEY_SET_VALUE, &k)) { RegDeleteValueW(k, L"DynaRunFix"); RegCloseKey(k); }
}

static WCHAR *layout_xml(WCHAR *out) { return cat3(out, g_dir, L"\\TaskbarLayout.xml", NULL); }

static void drop_policy(HKEY hive)                  // the Windows 11 taskbar layout policy, when it is still ours
{
    static WCHAR v[MAX_PATH], x[MAX_PATH]; HKEY k;
    if (reg_str(hive, POLKEY, L"StartLayoutFile", v, MAX_PATH, 0) && !lstrcmpiW(v, layout_xml(x)) &&
        !RegOpenKeyExW(hive, POLKEY, 0, KEY_SET_VALUE, &k)) {
        RegDeleteValueW(k, L"StartLayoutFile"); RegDeleteValueW(k, L"LockedStartLayout"); RegCloseKey(k);
    }
}

static const WCHAR XML_HEAD[] =
    L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    L"<LayoutModificationTemplate xmlns=\"http://schemas.microsoft.com/Start/2014/LayoutModification\" "
    L"xmlns:defaultlayout=\"http://schemas.microsoft.com/Start/2014/FullDefaultLayout\" "
    L"xmlns:start=\"http://schemas.microsoft.com/Start/2014/StartLayout\" "
    L"xmlns:taskbar=\"http://schemas.microsoft.com/Start/2014/TaskbarLayout\" Version=\"1\">\n"
    L"  <CustomTaskbarLayoutCollection>\n    <defaultlayout:TaskbarLayout>\n      <taskbar:TaskbarPinList>\n"
    L"        <taskbar:DesktopApp DesktopApplicationLinkPath=\"";

// Windows 11 (elevated stage): no program may pin itself any more; Microsoft's taskbar layout policy
// (learn.microsoft.com/windows/configuration/taskbar/pinned-apps) adds the pin at the user's next sign-in.
// The policy is left alone when an organization has already set a layout file.
static void taskbar_policy(const WCHAR *sid, HKEY app)
{
    static WCHAR x[MAX_PATH], l[MAX_PATH], xml[2400], path[300], v[MAX_PATH]; static char u[7200];
    DWORD ma, mi, b, ubr = 0, n = 4; HKEY k; BOOL gen;
    os_ver(&ma, &mi, &b);
    if (g_keep) return;
    if (!g_pin) {                                       // reinstalled without the pin: take our policy back
        if (reg_str(app, NULL, L"TaskbarPolicySid", v, MAX_PATH, 0) && !RegOpenKeyExW(HKEY_USERS, v, 0, KEY_READ | KEY_WRITE, &k)) { drop_policy(k); RegCloseKey(k); }
        RegDeleteValueW(app, L"TaskbarPolicy"); RegDeleteValueW(app, L"TaskbarPolicySid");
        return;
    }
    if (ma < 10 || b < 22000) return;
    cat3(path, sid, L"\\", POLKEY);
    layout_xml(x);
    if (reg_str(HKEY_USERS, path, L"StartLayoutFile", v, MAX_PATH, 0) && lstrcmpiW(v, x)) return;
    if (!save_link(cat3(l, g_dir, L"\\DynaRun V3.lnk", NULL), g_launcher, g_exe, 0, 0, 0, FALSE)) return;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_QUERY_VALUE | WOW, &k)) {
        RegQueryValueExW(k, L"UBR", NULL, NULL, (BYTE *)&ubr, &n); RegCloseKey(k);
    }
    // PinGeneration only on builds that know it: elsewhere it makes Windows ignore the pin
    gen = b >= 26200 || (b == 26100 && ubr >= 4484) || (b == 22631 && ubr >= 5549);
    lstrcpyW(xml, XML_HEAD); lstrcatW(xml, l);
    lstrcatW(xml, gen ? L"\" PinGeneration=\"1\"/>\n" : L"\"/>\n");
    lstrcatW(xml, L"      </taskbar:TaskbarPinList>\n    </defaultlayout:TaskbarLayout>\n  </CustomTaskbarLayoutCollection>\n</LayoutModificationTemplate>\n");
    n = WideCharToMultiByte(CP_UTF8, 0, xml, -1, u, sizeof(u), NULL, NULL);
    if (!n || !write_file(x, u, n - 1)) return;
    if (RegCreateKeyExW(HKEY_USERS, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) return;
    RegSetValueExW(k, L"StartLayoutFile", 0, REG_EXPAND_SZ, (const BYTE *)x, (lstrlenW(x) + 1) * sizeof(WCHAR));
    set_dword(k, L"LockedStartLayout", 1);
    RegCloseKey(k);
    set_dword(app, L"TaskbarPolicy", 1); set_str(app, L"TaskbarPolicySid", sid);
}

static BOOL pinned_ours(void)       // a shortcut to the launcher among the taskbar's pinned items (Windows 7+)
{
    static WCHAR d[MAX_PATH], f[MAX_PATH];
    if (!SHGetSpecialFolderPathW(NULL, d, CSIDL_APPDATA, FALSE)) return FALSE;
    lstrcatW(d, L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar");
    return find_ours(d, FALSE, f);
}

static BOOL wait_pinned(void)       // Explorer writes the shortcut a moment after the call returned
{
    int i;
    for (i = 0; i < 30; i++) { if (pinned_ours()) return TRUE; Sleep(100); }
    return FALSE;
}

typedef HRESULT (WINAPI *SHParseDisplayName_t)(PCWSTR, IBindCtx *, PIDLIST_ABSOLUTE *, ULONG, ULONG *);
typedef HRESULT (WINAPI *SHBindToParent_t)(PCIDLIST_ABSOLUTE, REFIID, void **, PCUITEMID_CHILD *);

static PIDLIST_ABSOLUTE item_id(const WCHAR *p)     // not on XP's shell32: looked up
{
    SHParseDisplayName_t f = (SHParseDisplayName_t)GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHParseDisplayName"); PIDLIST_ABSOLUTE id = NULL;
    if (!f || FAILED(f(p, NULL, &id, 0, NULL))) return NULL;
    return id;
}

static BOOL pin_verb(const WCHAR *lnk)              // Windows 7 .. Windows 10 1803: the shell's "Pin to taskbar" verb
{
    SHBindToParent_t bind = (SHBindToParent_t)GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHBindToParent");
    PIDLIST_ABSOLUTE id = item_id(lnk); IShellFolder *sf; PCUITEMID_CHILD ch; IContextMenu *cm; HMENU m; CMINVOKECOMMANDINFO ci; BOOL r = FALSE;
    if (id && bind && SUCCEEDED(bind(id, &IID_IShellFolder, (void **)&sf, &ch))) {
        if (SUCCEEDED(IShellFolder_GetUIObjectOf(sf, NULL, 1, &ch, &IID_IContextMenu, NULL, (void **)&cm))) {
            m = CreatePopupMenu();
            if (SUCCEEDED(IContextMenu_QueryContextMenu(cm, m, 0, 1, 0x7fff, CMF_NORMAL))) {
                zero(&ci, sizeof(ci)); ci.cbSize = sizeof(ci); ci.lpVerb = "taskbarpin"; ci.nShow = SW_HIDE;
                r = SUCCEEDED(IContextMenu_InvokeCommand(cm, &ci));
            }
            DestroyMenu(m); IContextMenu_Release(cm);
        }
        IShellFolder_Release(sf);
    }
    CoTaskMemFree(id);
    return r;
}

static BOOL pin_list(const WCHAR *lnk)              // Windows 10 1809 and later: Explorer's IPinnedList3
{
    IPinnedList3 *pl; PIDLIST_ABSOLUTE id = item_id(lnk); BOOL r = FALSE;
    if (id && SUCCEEDED(CoCreateInstance(&CLSID_TaskbandPin, NULL, CLSCTX_INPROC_SERVER, &IID_IPinnedList3, (void **)&pl))) {
        r = SUCCEEDED(pl->lpVtbl->Modify(pl, NULL, id, 0x7fffffff));
        pl->lpVtbl->Release(pl);
    }
    CoTaskMemFree(id);
    return r;
}

// Pins DynaRun (the shortcut lnk, which starts the launcher) to the taskbar of the current user. TRUE: done, or
// on Windows 11 under way (g_pinresult 2: the policy set by the machine stage applies at the next sign-in).
static BOOL pin_taskbar(const WCHAR *lnk)
{
    static WCHAR d[MAX_PATH], v[MAX_PATH], sub[64]; DWORD ma, mi, b; HKEY k;
    os_ver(&ma, &mi, &b);
    if (ma == 5 || (ma == 6 && !mi)) {                  // XP, Vista: the Quick Launch toolbar
        if (!SHGetSpecialFolderPathW(NULL, d, CSIDL_APPDATA, FALSE)) return FALSE;
        lstrcatW(d, L"\\Microsoft\\Internet Explorer\\Quick Launch");
        CreateDirectoryW(d, NULL);
        if (!save_link(lstrcatW(d, L"\\DynaRun V3.lnk"), g_launcher, g_exe, 0, 0, 0, FALSE)) return FALSE;
        if (!RegOpenKeyExW(HKEY_CURRENT_USER, cat3(sub, USERKEY, L"\\Shortcuts", NULL), 0, KEY_SET_VALUE, &k)) {
            RegSetValueExW(k, d, 0, REG_BINARY, (const BYTE *)"", 0);       // a shortcut we created: uninstall deletes it
            RegCloseKey(k);
        }
        return TRUE;
    }
    if (ma >= 10 && b >= 22000) {                       // Windows 11: see taskbar_policy
        if (reg_str(HKEY_CURRENT_USER, POLKEY, L"StartLayoutFile", v, MAX_PATH, 0) && !lstrcmpiW(v, layout_xml(d))) g_pinresult = 2;
        return g_pinresult == 2;
    }
    if (pinned_ours()) return TRUE;
    if (ma >= 10 && b >= 17763) pin_list(lnk); else pin_verb(lnk);
    return wait_pinned();
}

static BOOL desktop_link(WCHAR *out, HKEY backup)   // one of our shortcuts to pin: desktop, Start menu, else a new one
{
    static const int dirs[] = { CSIDL_DESKTOPDIRECTORY, CSIDL_COMMON_DESKTOPDIRECTORY, CSIDL_STARTMENU, CSIDL_COMMON_STARTMENU };
    WCHAR d[MAX_PATH]; int i;
    for (i = 0; i < 4; i++)
        if (SHGetSpecialFolderPathW(NULL, d, dirs[i], FALSE) && find_ours(d, i >= 2, out)) return TRUE;
    if (!SHGetSpecialFolderPathW(NULL, d, CSIDL_APPDATA, FALSE)) return FALSE;
    lstrcatW(d, L"\\DynaRunFix"); CreateDirectoryW(d, NULL);
    if (!save_link(cat3(out, d, L"\\DynaRun V3.lnk", NULL), g_launcher, g_exe, 0, 0, 0, FALSE)) return FALSE;
    RegSetValueExW(backup, out, 0, REG_BINARY, (const BYTE *)"", 0);
    return TRUE;
}

// The wizard's "Start DynaRun": started the way a double click on the shortcut does it (the shortcut gives the
// process the AppUserModelID that groups it with the pinned icon, its working folder and show command), not by
// running the launcher directly. Falls back to the launcher with DynaRun's folder as working directory.
void start_dynarun(void)
{
    static const int dirs[] = { CSIDL_DESKTOPDIRECTORY, CSIDL_COMMON_DESKTOPDIRECTORY, CSIDL_STARTMENU, CSIDL_COMMON_STARTMENU };
    static WCHAR lnk[MAX_PATH], dir[MAX_PATH], d[MAX_PATH];
    int i; WCHAR *q; BOOL have = FALSE;
    for (i = 0; i < 4 && !have; i++)
        have = SHGetSpecialFolderPathW(NULL, d, dirs[i], FALSE) && find_ours(d, i >= 2, lnk);
    if (!have && exists(cat3(lnk, g_dir, L"\\DynaRun V3.lnk", NULL))) have = TRUE;
    if (have) { ShellExecuteW(g_hwnd, NULL, lnk, NULL, NULL, SW_SHOWNORMAL); return; }
    lstrcpyW(dir, g_exe); q = dir + lstrlenW(dir); while (q > dir && *q != '\\') q--; *q = 0;
    ShellExecuteW(g_hwnd, NULL, g_launcher, NULL, dir[0] ? dir : NULL, SW_SHOWNORMAL);
}

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
    static WCHAR p[FOLDER_CCH], sub[300], tmphive[80]; HKEY hive = NULL; BOOL loaded = FALSE;
    wsprintfW(tmphive, HIVE_PREFIX L"-%lu-%lu-%d", GetCurrentProcessId(), GetTickCount(), seq++);     // unique: never a name another load uses
    if (lstrlenW(profile) + 12 >= MAX_PATH) return;                                  // (the caller checked; NTUSER.DAT must fit)
    if (RegOpenKeyExW(HKEY_USERS, sid, 0, KEY_READ | KEY_WRITE, &hive)) {          // not signed in: load their hive
        hive = NULL;
        if (!RegLoadKeyW(HKEY_USERS, tmphive, cat3(p, profile, L"\\NTUSER.DAT", NULL)) &&
            !RegOpenKeyExW(HKEY_USERS, tmphive, 0, KEY_READ | KEY_WRITE, &hive)) loaded = TRUE;
        else hive = NULL;
    }
    user_folder(hive, profile, L"Desktop", L"Desktop", p, FOLDER_CCH - 64);                          if (p[0]) unretarget(p, FALSE);
    user_folder(hive, profile, L"Start Menu", L"AppData\\Roaming\\Microsoft\\Windows\\Start Menu", p, FOLDER_CCH - 64); if (p[0]) unretarget(p, FALSE);
    user_folder(hive, profile, L"Programs", L"AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs", p, FOLDER_CCH - 64); if (p[0]) unretarget(p, TRUE);
    user_folder(hive, profile, L"AppData", L"AppData\\Roaming", p, FOLDER_CCH - 64);
    if (p[0]) { lstrcatW(p, L"\\Microsoft\\Internet Explorer\\Quick Launch");                       unretarget(p, TRUE); }
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

static int machine_install_locked(const WCHAR *sid)
{
    static WCHAR p[MAX_PATH + 16];
    HKEY k;
    if (!valid_user_sid(sid)) { error2(T(L"The user account is not valid.", L"使用者帳戶無效。"), sid); return 1; }
    lstrcpynW(g_user_sid, sid, 200);
    if (still_running(L"install")) return RC_RUNNING;   // checked again here: DynaRun may have been started since
    if (!check_exe()) { error2(T(L"DynaRun V3.exe was not found at the given path.", L"在指定的路徑找不到 DynaRun V3.exe。"), g_exe); return 1; }
    CreateDirectoryW(g_dir, NULL);
    if (!extract(1, g_launcher) || !extract(2, cat3(p, g_dir, L"\\dynafix.dll", NULL)) ||
        !extract(4, cat3(p, g_dir, L"\\LICENSE-miniz.txt", NULL)) || !extract(5, cat3(p, g_dir, L"\\LICENSE.txt", NULL))) {
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
    driver_machine_stage(p);                            // the Start menu shortcut to the driver installation (p: the installed setup)
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, APPKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) return 1;
    set_str(k, L"InstallDir", g_dir);
    if (g_exe_trusted) set_str(k, L"DynaRunExe", g_exe);     // a path that ordinary users can change is not recorded for the elevated stages (uninstall) to use
    else RegDeleteValueW(k, L"DynaRunExe");

    touch_report(recover_exe_time());                   // a time stamp an earlier run could not put back
    // the per-user COM registrations are not copied to HKLM any more (records of older versions are left alone)
    if (!g_exe_trusted) setup_log(L"DynaRun's folder or exe can be changed by ordinary users (or its path contains a link): no manifest was written");

    // garbled text under "Beta: Use Unicode UTF-8": external manifest with activeCodePage=Legacy
    if (GetACP() == CP_UTF8 && g_exe_trusted && !exists(cat3(p, g_exe, L".manifest", NULL)) && extract(3, p)) {
        set_dword(k, L"ManifestInstalled", 1);
        touch_report(touch(g_exe));
    }

    { HKEY b; if (!RegCreateKeyExW(k, L"Shortcuts", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &b, NULL)) { fix_shortcuts(TRUE, b); RegCloseKey(b); } }
    taskbar_policy(sid, k);
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
    return 0;
}

static int machine_install(const WCHAR *sid)
{
    int rc;
    if (!stage_lock()) { error2(busy_text(), NULL); return 1; }
    rc = machine_install_locked(sid);
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

static int machine_uninstall(const WCHAR *skip_sid)
{
    static WCHAR p[MAX_PATH + 16], polsid[200]; static const WCHAR KEEP[] = L"com-copied.txt\0com-backup\0com-skipped.txt\0setup.log\0";   // the record of the COM copies stays
    HKEY k; DWORD man = 0, n = 4; BOOL keep = FALSE, deleted = FALSE, stuck = FALSE;
    if (!stage_lock()) { error2(busy_text(), NULL); return 1; }
    if (skip_sid && skip_sid[0] && valid_user_sid(skip_sid)) lstrcpynW(g_user_sid, skip_sid, 200); else skip_sid = NULL;
    if (g_close) close_programs();                   // the user agreed (or /close): DynaRun keeps files of the folder open
    if (!g_exe[0]) locate_dynarun();                 // not recorded (an untrusted path is not): the default folders
    check_exe();                                     // g_exe is from HKLM; g_exe_trusted says whether to run it
    if (touch_report(recover_exe_time()) == TOUCH_STUCK) stuck = TRUE;
    restore_shortcuts(HKEY_LOCAL_MACHINE, APPKEY);
    unretarget_shell(TRUE);
    // the taskbar policy (Windows 11) of the user who runs the uninstall and of the user the install recorded: read before the key goes
    polsid[0] = 0; reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"TaskbarPolicySid", polsid, 200, 0);
    drop_policy_of(skip_sid); drop_policy_of(polsid);
    unretarget_all_users(skip_sid);
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, APPKEY, 0, KEY_READ, &k)) { RegQueryValueExW(k, L"ManifestInstalled", NULL, NULL, (BYTE *)&man, &n); RegCloseKey(k); }
    if (man == 1 && g_exe[0] && !g_exe_trusted && exists(cat3(p, g_exe, L".manifest", NULL)))
        setup_log(L"the manifest next to DynaRun V3.exe was left in place: DynaRun's folder or exe can be changed by ordinary users, so nothing is deleted or scheduled for deletion there");
    else if (man == 1 && g_exe[0] && exists(cat3(p, g_exe, L".manifest", NULL))) {
        if (!manifest_is_ours(p)) setup_log(L"the manifest next to DynaRun V3.exe is not the file DynaRunFix wrote (it was changed): left in place");
        else if (DeleteFileW(p)) deleted = TRUE;
        else if (move_aside(p)) {                    // in use: renamed at once (a reinstall before the restart writes its own file), the renamed one goes at the next restart
            deleted = TRUE;
            warn(T(L"The manifest file next to DynaRun V3.exe is in use and will be deleted at the next restart.", L"DynaRun V3.exe 旁的 manifest 檔正在使用中，將在下次重新啟動時刪除。"));
        }
        else {                                       // cannot even be renamed: it goes at the next restart, and the record that it is ours stays
            MoveFileExW(p, NULL, MOVEFILE_DELAY_UNTIL_REBOOT); keep = TRUE;
            warn(T(L"The manifest file next to DynaRun V3.exe is in use and will be deleted at the next restart.", L"DynaRun V3.exe 旁的 manifest 檔正在使用中，將在下次重新啟動時刪除。"));
        }
    }
    if (deleted) { if (g_exe_trusted) { if (touch_report(touch(g_exe)) == TOUCH_STUCK) stuck = TRUE; } else setup_log(L"the manifest state was not refreshed: DynaRun's folder or exe can be changed by ordinary users"); }
    // Before the key goes: the driver part ends a waiting task and resumes BitLocker if DynaRunFix suspended it (the flag is in the key).
    // An installed driver stays.
    driver_machine_uninstall();
    // stuck: the original time stamp of DynaRun V3.exe could not be put back; ExeOrigTimePath / ExeOrigTime stay for the next install or
    // uninstall (touch_report says so) - the key is not deleted then
    if (!keep && !stuck) RegDeleteKeyW(HKEY_LOCAL_MACHINE, APPKEY);
    else if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, APPKEY, 0, KEY_SET_VALUE, &k)) {     // what is no longer needed; ManifestInstalled, DynaRunExe (and the time stamp record) stay
        RegDeleteValueW(k, L"InstallDir"); RegDeleteValueW(k, L"TaskbarPolicy"); RegDeleteValueW(k, L"TaskbarPolicySid"); RegCloseKey(k);
    }
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, UNINSTKEY);
    delete_tree(g_dir, KEEP, 0);
    stage_unlock();
    return 0;
}

static int memcmp_(const BYTE *x, const BYTE *y, DWORD n) { while (n--) if (*x++ != *y++) return 1; return 0; }

// A copy of src as the new file dst (CREATE_NEW: nothing that exists is used), then held: the returned handle is opened for reading
// with FILE_SHARE_READ only, so as long as it is open nobody can write to, rename or delete the copy, and the content read through it
// is compared with src. INVALID_HANDLE_VALUE when any step fails (dst is deleted again). The copy can be run while the handle is open.
static HANDLE copy_and_hold(const WCHAR *src, const WCHAR *dst)
{
    static BYTE a[65536], b[65536];
    HANDLE s, d, h = INVALID_HANDLE_VALUE; DWORD na, nb; BOOL ok = TRUE; BY_HANDLE_FILE_INFORMATION bi;
    s = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s == INVALID_HANDLE_VALUE) return h;
    d = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (d == INVALID_HANDLE_VALUE) { CloseHandle(s); return h; }
    while (ok && ReadFile(s, a, sizeof(a), &na, NULL) && na) ok = WriteFile(d, a, na, &nb, NULL) && nb == na;
    CloseHandle(d);
    if (ok) {
        h = CreateFileW(dst, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            if (!GetFileInformationByHandle(h, &bi) || (bi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) ok = FALSE;
            SetFilePointer(s, 0, NULL, FILE_BEGIN);
            while (ok) {                              // the held copy has the content of the original
                BOOL r1 = ReadFile(s, a, sizeof(a), &na, NULL), r2 = ReadFile(h, b, sizeof(b), &nb, NULL);
                if (!r1 || !r2 || na != nb || memcmp_(a, b, na)) { ok = FALSE; break; }
                if (!na) break;
            }
            if (!ok) { CloseHandle(h); h = INVALID_HANDLE_VALUE; }
        }
    }
    CloseHandle(s);
    if (h == INVALID_HANDLE_VALUE) DeleteFileW(dst);
    return h;
}

// Runs "<exe> <args>" elevated (one UAC prompt) and returns its exit code; 2 = cancelled. exe: the program to run, NULL = this one.
int run_elevated(const WCHAR *args, const WCHAR *exe_path)
{
    SHELLEXECUTEINFOW se; DWORD rc = 1, err, tn; static WCHAR a[1024], root[4], tmp[MAX_PATH]; const WCHAR *exe = exe_path ? exe_path : g_self; HANDLE hold = INVALID_HANDLE_VALUE; LARGE_INTEGER q;
    lstrcpyW(a, args); if (g_quiet) lstrcatW(a, L" /quiet"); if (g_close) lstrcatW(a, L" /close");
    if (g_pin) lstrcatW(a, L" /pin");                // the elevated stage sets up the Windows 11 pin
    if (g_keep) lstrcatW(a, L" /keep");
    // Mapped network drives (e.g. a VM's shared folder) do not exist for the elevated process: run a copy in %TEMP%
    lstrcpynW(root, g_self, 4);
    if (!exe_path && root[1] == ':' && GetDriveTypeW(root) == DRIVE_REMOTE && (tn = GetTempPathW(MAX_PATH - 40, tmp)) != 0 && tn < MAX_PATH - 40) {
        q.QuadPart = 0; QueryPerformanceCounter(&q);
        wsprintfW(tmp + lstrlenW(tmp), L"DynaRunFix-Setup-%lu-%lu-%lu.exe", GetCurrentProcessId(), GetTickCount(), q.LowPart);
        // the copy is held open (nobody can change it) from here until the elevated process has ended: what the UAC prompt names is what runs
        if ((hold = copy_and_hold(g_self, tmp)) != INVALID_HANDLE_VALUE) exe = tmp;
    }
    zero(&se, sizeof(se));
    se.cbSize = sizeof(se); se.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    se.hwnd = g_hwnd; se.lpVerb = L"runas"; se.lpFile = exe; se.lpParameters = a; se.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&se)) {
        err = GetLastError();
        if (hold != INVALID_HANDLE_VALUE) { CloseHandle(hold); DeleteFileW(tmp); }
        return err == ERROR_CANCELLED ? 2 : 1;
    }
    WaitForSingleObject(se.hProcess, INFINITE);
    GetExitCodeProcess(se.hProcess, &rc);
    CloseHandle(se.hProcess);
    if (hold != INVALID_HANDLE_VALUE) { CloseHandle(hold); DeleteFileW(tmp); }
    return (int)rc;
}

static const WCHAR *no_admin(void)
{ return T(L"Administrator permission was not granted. Nothing was changed.", L"未取得系統管理員權限，沒有做任何變更。"); }

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

static int machine_full(const WCHAR *sid, const WCHAR *msi)
{
    static WCHAR me[200], txt[300], dir[MAX_PATH], staged[MAX_PATH]; int rc;
    if (!user_sid(me, 200) || lstrcmpiW(me, sid)) return 3;
    if (!stage_lock()) { error2(busy_text(), NULL); return 1; }
    if (still_running(L"full install")) { stage_unlock(); return RC_RUNNING; }
    if (!stage_msi(msi, dir, staged)) {
        stage_unlock();
        msg(T(L"The DynaRun V3 setup could not be copied for installation.", L"無法複製 DynaRun V3 安裝檔以進行安裝。"), MB_ICONERROR);
        return 4;
    }
    if (msi_check(staged, NULL, 0) != MSI_DYNARUN) {            // the check of the copy that is going to be run
        drop_admin_dir(dir); stage_unlock();
        msg(T(L"This is not the DynaRun V3 setup, so it will not be installed.", L"這不是 DynaRun V3 的安裝檔，所以不會安裝它。"), MB_ICONERROR);
        return 4;
    }
    rc = run_msiexec(staged);
    drop_admin_dir(dir);
    stage_unlock();
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
    g_pinresult = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, USERKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &b, NULL)) return;
    // another version is installed now: what update.c noted no longer applies; the first start of DynaRun looks again
    RegDeleteValueW(b, L"UpdateChecked"); RegDeleteValueW(b, L"UpdateVersion"); RegDeleteValueW(b, L"UpdateSha256"); RegDeleteValueW(b, L"UpdateAsk"); RegDeleteValueW(b, L"UpdateSkip");
    if (!RegCreateKeyExW(b, L"Shortcuts", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL)) {
        int desk = fix_shortcuts(FALSE, s);
        if (!desk && SHGetSpecialFolderPathW(NULL, p, CSIDL_COMMON_DESKTOPDIRECTORY, FALSE))
            desk = scan(p, FALSE, s);   // already handled by the machine stage; only counted here
        if (!desk && !g_keep && SHGetSpecialFolderPathW(NULL, p, CSIDL_DESKTOPDIRECTORY, FALSE) &&
            save_link(lstrcatW(p, L"\\DynaRun V3.lnk"), g_launcher, g_exe, 0, 0, 0, FALSE))
            RegSetValueExW(s, p, 0, REG_BINARY, (const BYTE *)"", 0);
        if (g_pin && !g_keep) {
            g_pinresult = 3;
            if (desktop_link(p, s) && pin_taskbar(p) && g_pinresult == 3) g_pinresult = 1;
        }
        RegCloseKey(s);
    }
    RegCloseKey(b);
    if (!g_keep && !RegCreateKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL)) {   // start at sign-in
        if (g_autostart) set_str(s, L"DynaRunFix", cat3(p, L"\"", g_launcher, L"\" /autostart"));
        else RegDeleteValueW(s, L"DynaRunFix");
        RegCloseKey(s);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}

int install_fix(void)
{
    static WCHAR sid[200], args[600]; int rc;
    if (!user_sid(sid, 200)) return 1;
    if (running_early(L"install (before elevation)")) return RC_RUNNING;   // before the UAC prompt; /quiet without /close ends here
    CoInitialize(NULL);
    if (is_admin()) rc = machine_install(sid);
    else {
        wsprintfW(args, L"/machine %s \"%s\"", sid, g_exe);
        if ((rc = run_elevated(args, NULL)) == 2) { msg(no_admin(), MB_ICONWARNING); return 2; }
    }
    if (rc) return rc;
    user_stage();
    return 0;
}

int install_all(const WCHAR *msi)
{
    static WCHAR sid[200], args[MAX_PATH + 300]; int rc;
    if (!user_sid(sid, 200)) return 1;
    if (running_early(L"full install (before elevation)")) return RC_RUNNING;
    CoInitialize(NULL);
    pkg_prepare_documents(msi);         // as this user, before the SYSTEM-side install (error 1305 otherwise)
    if (is_admin()) rc = machine_full(sid, msi);
    else {
        wsprintfW(args, L"/full %s \"%s\"", sid, msi);
        rc = run_elevated(args, NULL);
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

// The uninstall question's Yes / No buttons become "Fix only" / "Remove all".
static HHOOK g_cbt;
static LRESULT CALLBACK relabel(int code, WPARAM w, LPARAM l)
{
    HHOOK h = g_cbt;
    if (code == HCBT_ACTIVATE) {
        SetDlgItemTextW((HWND)w, IDYES, T(L"&Fix only", L"只移除修正(&F)"));
        SetDlgItemTextW((HWND)w, IDNO, T(L"&Remove all", L"全部移除(&R)"));
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

static int uninstall(void)
{
    static WCHAR p[MAX_PATH + 160], prod[40], sid[200], args[260]; int rc; BOOL all = FALSE;
    const WCHAR *elev = NULL;
    // The uninstaller runs from the installed program (no copy in %TEMP%): the elevated stage renames what it cannot delete (this very
    // file, while the user-side stage still runs) and Windows deletes it at the next restart.
    cat3(p, g_dir, L"\\DynaRunFix-Setup.exe", NULL);
    drop_old_temp_copies();                        // copies that earlier builds left in %TEMP%
    if (!exists(p) && !reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"DynaRunExe", g_exe, MAX_PATH, 0)) {
        info(T(L"DynaRunFix is not installed.", L"DynaRunFix 尚未安裝。"));
        return 0;
    }
    if (!g_quiet && !dynarun_product(prod)) {
        if (msg(T(L"Uninstall DynaRunFix?", L"要解除安裝 DynaRunFix 嗎？"), MB_YESNO | MB_ICONQUESTION) != IDYES) return 2;
    } else if (!g_quiet) {
        g_cbt = SetWindowsHookExW(WH_CBT, relabel, NULL, GetCurrentThreadId());
        rc = msg(T(L"Uninstall DynaRunFix?\n\n"
                   L"Fix only: the DynaRun shortcuts start the original DynaRun again; DynaRun V3 stays installed.\n\n"
                   L"Remove all: DynaRun V3 is then uninstalled too, by Dyna Pro's own uninstaller.",
                   L"要解除安裝 DynaRunFix 嗎？\n\n"
                   L"只移除修正：DynaRun 的捷徑改回開啟原版，DynaRun V3 保留。\n\n"
                   L"全部移除：接著再用 Dyna Pro 自己的解除安裝程式移除 DynaRun V3。"), MB_YESNOCANCEL | MB_ICONQUESTION);
        if (g_cbt) { UnhookWindowsHookEx(g_cbt); g_cbt = NULL; }
        if (rc == IDCANCEL) return 2;
        all = rc == IDNO;
    }
    // Running DynaRun / launcher / LEProc hold files of the folder: ask first, the elevated stage then closes them.
    // /quiet without /close goes on; what is still in use is deleted at the next restart.
    if (!g_quiet && running_programs()) {
        if (msg(T(L"DynaRun is running and must be closed to uninstall DynaRunFix.\n\n"
                  L"OK closes DynaRun now: a test run in progress stops, unsaved data is lost.",
                  L"解除安裝 DynaRunFix 前必須關閉 DynaRun。\n\n"
                  L"按「確定」會立即關閉 DynaRun，正在進行的測試會中斷，沒存檔的資料會遺失。"), MB_OKCANCEL | MB_ICONWARNING) != IDOK) return 2;
        g_close = TRUE;
    }
    reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"DynaRunExe", g_exe, MAX_PATH, 0);
    if (!g_exe[0]) locate_dynarun();
    CoInitialize(NULL);
    if (!user_sid(sid, 200)) sid[0] = 0;
    // The elevated stage comes first: when the UAC prompt is cancelled (or fails to start) nothing has been changed yet and the
    // user's shortcuts, Run value and pins still work with the installed launcher.
    if (is_admin()) rc = machine_uninstall(sid);
    else {
        // What runs elevated is the installed program in Program Files (administrators-only folder), never a copy in a folder the user can
        // write. Only when there is no installed copy (a broken install) is this file itself run, if it is not one of our %TEMP% copies.
        lstrcpynW(g_user_sid, sid, 200);
        if (exists(p)) {
            if (g_is_writable(g_dir) || g_is_writable(p)) {
                error2(T(L"The DynaRunFix folder can be changed by ordinary users, so its uninstaller is not run with administrator rights. Remove DynaRunFix as an administrator.",
                         L"DynaRunFix 資料夾可被一般使用者修改，因此不會以系統管理員權限執行其中的解除安裝程式。請以系統管理員身分移除 DynaRunFix。"), g_dir);
                return 1;
            }
            elev = p;
        }
        wsprintfW(args, L"/unmachine %s", sid);
        rc = run_elevated(sid[0] ? args : L"/unmachine", elev);
    }
    if (rc == 2) { msg(no_admin(), MB_ICONWARNING); return 2; }
    if (rc) {                                                           // the elevated stage did not finish: nothing user-side is undone either
        if (!g_quiet) { wsprintfW(p, T(L"The uninstall did not finish (code %d). Nothing else was changed.", L"解除安裝沒有完成（代碼 %d）。沒有做其他變更。"), rc); msg(p, MB_ICONERROR); }
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
        if (rc == ERROR_INSTALL_USEREXIT) { info(T(L"DynaRunFix has been uninstalled. DynaRun V3 was kept (its uninstall was cancelled).", L"DynaRunFix 已解除安裝。DynaRun V3 的解除安裝已取消，DynaRun V3 保留。")); return 2; }
        if (rc > 0 && rc != ERROR_SUCCESS_REBOOT_REQUIRED) {
            wsprintfW(p, T(L"DynaRunFix has been uninstalled, but uninstalling DynaRun V3 failed (Windows Installer error %d).",
                           L"DynaRunFix 已解除安裝，但 DynaRun V3 解除安裝失敗（Windows Installer 錯誤 %d）。"), rc);
            msg(p, MB_ICONERROR);
            return 4;
        }
        info(T(L"DynaRunFix and DynaRun V3 have been uninstalled.", L"DynaRunFix 和 DynaRun V3 都已解除安裝。"));
        return 0;
    }
    info(T(L"DynaRunFix has been uninstalled.", L"DynaRunFix 已解除安裝。"));
    return 0;
}

// the command line after its first n arguments, as typed (the launcher's arguments for /update)
static const WCHAR *after_args(int n)
{
    const WCHAR *c = GetCommandLineW(); BOOL q = FALSE;
    while (n--) {
        while (*c == ' ' || *c == '\t') c++;
        for (; *c && (q || (*c != ' ' && *c != '\t')); c++) if (*c == '"') q = !q;
    }
    while (*c == ' ' || *c == '\t') c++;
    return c;
}

void WinMainCRTStartup(void)
{
    typedef BOOL (WINAPI *SetDefaultDllDirectories_t)(DWORD);
    SetDefaultDllDirectories_t sdd = (SetDefaultDllDirectories_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDefaultDllDirectories");
    int argc, i, rc; BOOL pin = FALSE; WCHAR **argv;
    if (sdd) sdd(0x800);                    // LOAD_LIBRARY_SEARCH_SYSTEM32: no DLL is searched for in the exe's folder or the current folder
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_zh = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
    init_paths();
    for (i = 1; i < argc; i++) {
        if (!lstrcmpiW(argv[i], L"/quiet")) g_quiet = TRUE;
        else if (!lstrcmpiW(argv[i], L"/close")) g_close = TRUE;
        else if (!lstrcmpiW(argv[i], L"/notaskbar")) g_pin = FALSE;
        else if (!lstrcmpiW(argv[i], L"/noautostart")) g_autostart = FALSE;
        else if (!lstrcmpiW(argv[i], L"/pin")) pin = TRUE;
        else if (!lstrcmpiW(argv[i], L"/keep")) g_keep = TRUE;
    }
    if (argc >= 2 && (!lstrcmpiW(argv[1], L"/machine") || !lstrcmpiW(argv[1], L"/full"))) g_pin = pin;   // the elevated stage: as asked
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
        rc = machine_uninstall(argc >= 3 && argv[2][0] == 'S' && argv[2][1] == '-' ? argv[2] : NULL);
    } else if (argc >= 2 && (rc = driver_verb(argc, argv)) != DRV_NOVERB) {
        // /driver, /driver-continue (the wizard at the driver pages), /driver-prep, /driver-install, /driver-cancel (driver.c)
    } else if (argc >= 2 && !lstrcmpiW(argv[1], L"/uninstall")) rc = uninstall();
    else if (argc >= 2 && !lstrcmpiW(argv[1], L"/checkupdate")) rc = update_check();
    else if (argc >= 2 && !lstrcmpiW(argv[1], L"/update")) rc = update_run(after_args(2));
    else if (argc >= 2 && !lstrcmpiW(argv[1], L"/offer")) rc = update_offer();
    else if (g_quiet) rc = locate_dynarun() ? install_fix() : 1;
    else rc = wizard();
    // the elevated part (/machine, /full) ends here: let the wizard that started it take the foreground back
    AllowSetForegroundWindow(ASFW_ANY);
    ExitProcess(rc);
}
