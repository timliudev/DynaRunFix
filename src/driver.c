// The Dyna Pro USB driver (wizard pages in wizard.c, user documentation in docs/DRIVER.md).
//
// Dyna Pro's "Dyna Pro USB.zip" holds FTDI's FTDIBUS/FTDIPORT driver 2.08.24 (2012) with the Dyna Pro device id
// USB\VID_0403&PID_FDBC added to ftdibus.inf. Microsoft signed the catalog before that edit, so Windows 10/11 installs the package only
// while driver signature enforcement is off, which a user can switch on for one start of Windows ("Startup Settings", key 7).
// This file automates that:
//
//   download   driver_fetch: the zip from dynapro.co.uk (https, that host only), checked against the pinned SHA-256 below.
//   prep       (elevated, "/driver-prep <user SID> <zip | ->") stages the driver in %ProgramData%\DynaRunFix\driver (a folder that
//              only SYSTEM and the Administrators can change; an existing one is replaced, it may have been planted by an ordinary
//              user), copies this setup to %ProgramData%\DynaRunFix, suspends BitLocker for one restart, asks Windows for its
//              advanced start-up menu at the next start (bcdedit onetimeadvancedoptions) and registers the task
//              "DynaRunFix-DriverContinue" that starts "<setup> /driver-continue" when the user signs in. The wizard then restarts Windows.
//   continue   "/driver-continue": the wizard looks at the code integrity state: still enforced -> the user missed the menu, offer a
//              retry; off -> "/driver-install" (elevated): pnputil /add-driver <inf> /install, then the task and the menu flag go.
//   cancel     "/driver-cancel": the task, the menu flag and the BitLocker suspension are taken back.
//
// Nothing here bypasses a security feature on its own: the user chooses the restart option and the "install anyway" answer.
// The system tools run hidden; nothing locale dependent is parsed (exit codes only). This is a 32-bit program: on 64-bit Windows the
// tools are the 64-bit ones in %windir%\Sysnative.
//
// The first part (INF matcher, code integrity classifier, task XML, tool paths, pnputil exit codes) has no side effects and is
// unit-tested (tests\test_driver.c).
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <sddl.h>
#include <aclapi.h>
#include "setup.h"

// SHA-256 of "Dyna Pro USB.zip" as served by Dyna Pro on 2026-10-15 (2335114 bytes; Last-Modified 2026-05-09). A different file is never installed.
#define DRIVER_ZIP_SHA256 L"c3ae487018956767d65a08e3d5cd8434da42cdce81988753515ee0f72c49e943"
#define DRIVER_ZIP_NAME   L"Dyna Pro USB.zip"
#define TASK_NAME         L"DynaRunFix-DriverContinue"
#define BDE_VALUE         L"DriverBitLockerSuspended"       // HKLM\SOFTWARE\DynaRunFix: 1 while we have BitLocker suspended for the restart
#define DRV_APPKEY        L"SOFTWARE\\DynaRunFix"
#define SETUP_NAME        L"DynaRunFix-Setup.exe"
#define BUS_INF_REL       L"\\driver\\Dyna Pro USB\\ftdibus.inf"
#define PORT_INF_REL      L"\\driver\\Dyna Pro USB\\ftdiport.inf"

/* ====================== pure helpers (no side effects) ====================== */

/* ---------- INF ---------- */

// The INF text as lower-case 7-bit ASCII on the heap (other characters become '?'): a UTF-16 file (with or without a byte order mark)
// or an ANSI / UTF-8 one. NULL when out of memory.
static char *inf_ascii(const BYTE *d, DWORD n)
{
    char *o; DWORD i, k = 0; BOOL w16 = FALSE, be = FALSE;
    if (n >= 2 && d[0] == 0xFF && d[1] == 0xFE) { w16 = TRUE; d += 2; n -= 2; }
    else if (n >= 2 && d[0] == 0xFE && d[1] == 0xFF) { w16 = TRUE; be = TRUE; d += 2; n -= 2; }
    else if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) { d += 3; n -= 3; }
    else if (n >= 4 && d[0] && !d[1] && d[2] && !d[3]) w16 = TRUE;          // UTF-16LE without a byte order mark
    if (!(o = alloc(n + 1))) return NULL;
    if (w16) {
        for (i = 0; i + 1 < n; i += 2) {
            BYTE lo = be ? d[i + 1] : d[i], hi = be ? d[i] : d[i + 1];
            o[k++] = (char)(hi || lo >= 0x80 || !lo ? '?' : (lo >= 'A' && lo <= 'Z' ? lo + 32 : lo));
        }
    } else {
        for (i = 0; i < n; i++) o[k++] = (char)(d[i] >= 0x80 || !d[i] ? '?' : (d[i] >= 'A' && d[i] <= 'Z' ? d[i] + 32 : d[i]));
    }
    o[k] = 0;
    return o;
}

static BOOL has_text(const char *hay, const char *needle)
{
    int i;
    for (; *hay; hay++) { for (i = 0; needle[i] && hay[i] == needle[i]; i++); if (!needle[i]) return TRUE; }
    return FALSE;
}

// A "DriverVer = <date>,<version>" line (lower-case text) whose version is exactly ver.
static BOOL driver_ver_is(const char *t, const char *ver)
{
    static const char key[] = "driverver";
    const char *p = t, *l; int i;
    while (*p) {
        l = p;
        while (*l == ' ' || *l == '\t') l++;
        for (i = 0; key[i] && l[i] == key[i]; i++);
        if (!key[i]) {
            l += i;
            while (*l == ' ' || *l == '\t') l++;
            if (*l == '=') {
                l++;
                while (*l && *l != ',' && *l != '\n' && *l != '\r') l++;
                if (*l == ',') {
                    l++;
                    while (*l == ' ' || *l == '\t') l++;
                    for (i = 0; ver[i] && l[i] == ver[i]; i++);
                    if (!ver[i] && (!l[i] || l[i] == ' ' || l[i] == '\t' || l[i] == ';' || l[i] == '\r' || l[i] == '\n' || l[i] == ',')) return TRUE;
                }
            }
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return FALSE;
}

// Is this INF (file content) the bus INF of Dyna Pro's driver package: FTDIBUS 2.08.24 with USB\VID_0403&PID_FDBC? Stock FTDI drivers
// (newer versions, no FDBC) and the FTDIPORT INF (its ids are FTDIBUS\COMPORT&...) are not.
BOOL drv_inf_matches(const BYTE *data, DWORD n)
{
    char *t = inf_ascii(data, n); BOOL ok;
    if (!t) return FALSE;
    ok = has_text(t, "usb\\vid_0403&pid_fdbc") && has_text(t, "[ftdibus.nt]") && driver_ver_is(t, "2.08.24");
    release(t);
    return ok;
}

/* ---------- code integrity ---------- */

// NtQuerySystemInformation(SystemCodeIntegrityInformation = 103) fills { ULONG Length; ULONG CodeIntegrityOptions; } (Length = 8 on input).
// Bits (winternl / the Windows kernel headers): 0x1 ENABLED, 0x2 TESTSIGN, 0x4 UMCI_ENABLED, 0x8 UMCI_AUDITMODE, 0x10 UMCI_EXCLUSIONPATHS,
// 0x20 TEST_BUILD, 0x40 PREPRODUCTION_BUILD, 0x80 DEBUGMODE_ENABLED, 0x100 FLIGHT_BUILD, 0x200 FLIGHTING_ENABLED, 0x400 HVCI_KMCI_ENABLED
// (memory integrity), 0x800 HVCI_KMCI_AUDITMODE, 0x1000 HVCI_KMCI_STRICTMODE, 0x2000 HVCI_IUM_ENABLED.
// "Disable driver signature enforcement" is expected to clear 0x1 (to be confirmed on a machine: see docs / the implementation notes).
#define CI_OPT_ENABLED  0x1
#define CI_OPT_TESTSIGN 0x2
#define CI_OPT_HVCI     0x400

// The one place that decides what "enforcement is on" means.
BOOL drv_ci_enforced(DWORD options) { return (options & CI_OPT_ENABLED) != 0; }

/* ---------- the task's XML ---------- */

static BOOL xml_app(WCHAR **p, WCHAR *end, const WCHAR *s, BOOL esc)
{
    for (; *s; s++) {
        const WCHAR *r = NULL;
        if (esc) {
            if (*s < 0x20) return FALSE;                    // no control characters in a path or an id
            switch (*s) { case '&': r = L"&amp;"; break; case '<': r = L"&lt;"; break; case '>': r = L"&gt;"; break; case '"': r = L"&quot;"; break; case '\'': r = L"&apos;"; break; }
        }
        if (r) { for (; *r; r++) { if (*p >= end) return FALSE; *(*p)++ = *r; } }
        else { if (*p >= end) return FALSE; *(*p)++ = *s; }
    }
    return TRUE;
}

// The Task Scheduler XML (a UTF-16 text; write it with a byte order mark) of the task that starts "<exe> /driver-continue" when user
// <sid> signs in: runs as that user with the token the sign-in gave (interactive, so its window is on the desktop), with the highest
// rights that user has (an administrator gets the elevated token without a prompt), also on battery, without a time limit.
// Returns the number of characters (without the end of string), or -1 (bad input, buffer too small).
int drv_task_xml(WCHAR *out, int cch, const WCHAR *sid, const WCHAR *exe)
{
    WCHAR *p = out, *end;
    if (!out || cch < 64 || !sid || !sid[0] || !exe || !exe[0]) return -1;
    end = out + cch - 1;
#define XA(s) do { if (!xml_app(&p, end, (s), FALSE)) return -1; } while (0)
#define XE(s) do { if (!xml_app(&p, end, (s), TRUE)) return -1; } while (0)
    XA(L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n");
    XA(L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n");
    XA(L"  <RegistrationInfo><Description>DynaRunFix: continues the Dyna Pro USB driver installation after the restart</Description></RegistrationInfo>\r\n");
    XA(L"  <Triggers>\r\n    <LogonTrigger>\r\n      <Enabled>true</Enabled>\r\n      <UserId>"); XE(sid); XA(L"</UserId>\r\n    </LogonTrigger>\r\n  </Triggers>\r\n");
    XA(L"  <Principals>\r\n    <Principal id=\"Author\">\r\n      <UserId>"); XE(sid); XA(L"</UserId>\r\n");
    XA(L"      <LogonType>InteractiveToken</LogonType>\r\n      <RunLevel>HighestAvailable</RunLevel>\r\n    </Principal>\r\n  </Principals>\r\n");
    XA(L"  <Settings>\r\n    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n");
    XA(L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n");
    XA(L"    <AllowHardTerminate>true</AllowHardTerminate>\r\n    <StartWhenAvailable>false</StartWhenAvailable>\r\n    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n");
    XA(L"    <Enabled>true</Enabled>\r\n    <Hidden>false</Hidden>\r\n    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n    <Priority>7</Priority>\r\n  </Settings>\r\n");
    XA(L"  <Actions Context=\"Author\">\r\n    <Exec>\r\n      <Command>"); XE(exe); XA(L"</Command>\r\n      <Arguments>/driver-continue</Arguments>\r\n    </Exec>\r\n  </Actions>\r\n");
    XA(L"</Task>\r\n");
#undef XA
#undef XE
    *p = 0;
    return (int)(p - out);
}

/* ---------- system tools, pnputil ---------- */

// The path of a system tool. A 32-bit process on 64-bit Windows (wow64) would get the 32-bit System32 (SysWOW64), which has no
// pnputil/bcdedit/manage-bde that can do the job: "Sysnative" is the way to the real one. FALSE: does not fit.
BOOL drv_sys_tool(const WCHAR *windir, BOOL wow64, const WCHAR *tool, WCHAR *out, int cch)
{
    const WCHAR *sub = wow64 ? L"\\Sysnative\\" : L"\\System32\\";
    int n = lstrlenW(windir);
    while (n > 0 && windir[n - 1] == '\\') n--;
    if (!n || n + lstrlenW(sub) + lstrlenW(tool) + 1 > cch) return FALSE;
    lstrcpynW(out, windir, n + 1); lstrcatW(out, sub); lstrcatW(out, tool);
    return TRUE;
}

// pnputil /add-driver exit codes: 0 done; 3010 (ERROR_SUCCESS_REBOOT_REQUIRED) done, restart wanted; 259 (ERROR_NO_MORE_ITEMS) is what
// /install reports when the package was added but no device is connected that it could be installed on.
// (Unconfirmed on a machine: see the implementation notes.)
BOOL drv_pnputil_ok(DWORD code) { return code == 0 || code == 3010 || code == 259; }

// The arguments of pnputil for one INF (absolute path).
BOOL drv_pnputil_args(WCHAR *out, int cch, const WCHAR *inf)
{
    static const WCHAR a[] = L"/add-driver \"", b[] = L"\" /install";
    int n = lstrlenW(inf);
    if (!n || n + (int)(sizeof(a) + sizeof(b)) / (int)sizeof(WCHAR) + 1 > cch) return FALSE;
    for (; *inf; inf++) if (*inf == '"') return FALSE;
    lstrcpyW(out, a); lstrcatW(out, inf - n); lstrcatW(out, b);
    return TRUE;
}

/* ====================== the rest ====================== */

BOOL driver_supported(void) { return os_build() >= 10240; }

static BOOL is_wow64(void)
{
    typedef BOOL (WINAPI *IsWow64Process_t)(HANDLE, PBOOL);
    IsWow64Process_t f = (IsWow64Process_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process"); BOOL w = FALSE;
    return f && f(GetCurrentProcess(), &w) && w;
}

static void *slurp(const WCHAR *p, DWORD *n, DWORD limit)
{
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL); void *d = NULL; DWORD sz;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    if (sz != INVALID_FILE_SIZE && sz <= limit && (d = alloc(sz + 2)) && !ReadFile(h, d, sz, n, NULL)) { release(d); d = NULL; }
    CloseHandle(h);
    return d;
}

// The Dyna Pro bus driver is in the driver store when one of its copies, %windir%\INF\oem*.inf, is that INF.
BOOL driver_installed(void)
{
    WCHAR dir[MAX_PATH], pat[MAX_PATH], f[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE h; BYTE *d; DWORD n; BOOL found = FALSE;
    if (!GetWindowsDirectoryW(dir, MAX_PATH - 20)) return FALSE;
    lstrcatW(dir, L"\\INF");
    lstrcpyW(pat, dir); lstrcatW(pat, L"\\oem*.inf");
    if ((h = FindFirstFileW(pat, &fd)) == INVALID_HANDLE_VALUE) return FALSE;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 >= MAX_PATH) continue;
        lstrcpyW(f, dir); lstrcatW(f, L"\\"); lstrcatW(f, fd.cFileName);
        if ((d = slurp(f, &n, 2 * 1024 * 1024))) { found = drv_inf_matches(d, n); release(d); }
    } while (!found && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

int driver_ci_state(void)
{
    typedef LONG (WINAPI *NtQuerySystemInformation_t)(ULONG, PVOID, ULONG, PULONG);
    NtQuerySystemInformation_t q = (NtQuerySystemInformation_t)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
    struct { ULONG Length; ULONG CodeIntegrityOptions; } ci; ULONG got = 0;
    if (!q) return DRV_CI_UNKNOWN;
    ci.Length = sizeof(ci); ci.CodeIntegrityOptions = 0;
    if (q(103, &ci, sizeof(ci), &got) < 0 || got < sizeof(ci)) return DRV_CI_UNKNOWN;
    return drv_ci_enforced(ci.CodeIntegrityOptions) ? DRV_CI_ENFORCED : DRV_CI_OFF;
}

BOOL driver_reboot(void)
{
    enable_privilege(L"SeShutdownPrivilege");
    return ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_INSTALLATION | SHTDN_REASON_FLAG_PLANNED);
}

/* ---------- download ---------- */

int driver_fetch(WCHAR *zip, progress_fn cb, volatile LONG *cancel, DWORD *err)
{
    WCHAR hex[65]; ULONGLONG size = 0; int rc;
    rc = pkg_fetch(DYNAPRO_USB_ZIP, DRIVER_ZIP_NAME, L"DriverETag", DYNAPRO_HOST, zip, cb, cancel, err);
    if (rc != PK_OK) return rc;
    if (!pkg_sha256(zip, hex, &size) || lstrcmpiW(hex, DRIVER_ZIP_SHA256)) { DeleteFileW(zip); return DRV_E_HASH; }   // not the file this program was made for
    return PK_OK;
}

/* ---------- running the system tools ---------- */

static WCHAR g_logfile[MAX_PATH];

static void drv_log(const WCHAR *line)
{
    static char u[2048]; int n; HANDLE h; DWORD w;
    if (!g_logfile[0] || (n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, sizeof(u) - 2, NULL, NULL)) <= 0) return;
    h = CreateFileW(g_logfile, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    u[n - 1] = '\r'; u[n] = '\n';
    WriteFile(h, u, n + 1, &w, NULL);
    CloseHandle(h);
}

static void drv_log2(const WCHAR *what, const WCHAR *arg, DWORD v)
{
    static WCHAR b[1200];
    wsprintfW(b, L"%s %s -> %ld", what, arg ? arg : L"", (long)v);
    drv_log(b);
}

// "tool args", hidden, waited for; its exit code, or (DWORD)-1 when it could not be started or did not finish in 3 minutes.
static DWORD run_tool(const WCHAR *tool, const WCHAR *args)
{
    static WCHAR win[MAX_PATH], exe[MAX_PATH]; WCHAR *cmd; STARTUPINFOW si; PROCESS_INFORMATION pi; DWORD rc = (DWORD)-1;
    if (!GetWindowsDirectoryW(win, MAX_PATH - 40) || !drv_sys_tool(win, is_wow64(), tool, exe, MAX_PATH) || !exists(exe)) { drv_log2(tool, L"not found", rc); return rc; }
    if (!(cmd = alloc((lstrlenW(exe) + lstrlenW(args) + 8) * sizeof(WCHAR)))) return rc;
    lstrcpyW(cmd, L"\""); lstrcatW(cmd, exe); lstrcatW(cmd, L"\" "); lstrcatW(cmd, args);
    zero(&si, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, win, &si, &pi)) {
        if (WaitForSingleObject(pi.hProcess, 180000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
        else GetExitCodeProcess(pi.hProcess, &rc);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    drv_log2(tool, args, rc);
    release(cmd);
    return rc;
}

/* ---------- the folder %ProgramData%\DynaRunFix ---------- */

static void base_dir(WCHAR *out)
{
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, SHGFP_TYPE_CURRENT, out))) lstrcpyW(out, L"C:\\ProgramData");
    lstrcatW(out, L"\\DynaRunFix");
}

// Owner Administrators; SYSTEM and Administrators full control, Users read and execute; the DACL does not inherit from ProgramData.
static const WCHAR SDDL_OWNED[] = L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)";
static const WCHAR SDDL_PLAIN[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)";

static BOOL make_protected(const WCHAR *dir, const WCHAR *sddl)
{
    PSECURITY_DESCRIPTOR sd; SECURITY_ATTRIBUTES sa; BOOL ok;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, NULL)) return FALSE;
    sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = sd; sa.bInheritHandle = FALSE;
    ok = CreateDirectoryW(dir, &sa);
    LocalFree(sd);
    return ok;
}

// A folder that exists already: owner and DACL set to ours (an ordinary user may have created it).
static void reset_security(const WCHAR *dir)
{
    PSECURITY_DESCRIPTOR sd; PSID owner = NULL; PACL dacl = NULL; BOOL present = FALSE, def = FALSE;
    enable_privilege(L"SeTakeOwnershipPrivilege"); enable_privilege(L"SeRestorePrivilege"); enable_privilege(L"SeBackupPrivilege");
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(SDDL_OWNED, SDDL_REVISION_1, &sd, NULL)) return;
    if (GetSecurityDescriptorOwner(sd, &owner, &def) && GetSecurityDescriptorDacl(sd, &present, &dacl, &def) && owner && present)
        SetNamedSecurityInfoW((LPWSTR)dir, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, owner, NULL, dacl, NULL);
    LocalFree(sd);
}

// A new, empty %ProgramData%\DynaRunFix that only SYSTEM and the Administrators can change. keep_exe: our own copy of the setup is running
// from there (a retry after the restart) and stays. FALSE when that cannot be reached (the folder is still changeable by users).
static BOOL make_base(const WCHAR *base, BOOL keep_exe, const WCHAR *sid)
{
    static const WCHAR KEEP[] = L"DynaRunFix-Setup.exe\0";
    DWORD a = GetFileAttributesW(base);
    if (a != INVALID_FILE_ATTRIBUTES) {
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) { if (!RemoveDirectoryW(base)) return FALSE; }      // a link: removed, never followed
        else if (!(a & FILE_ATTRIBUTE_DIRECTORY)) { if (!DeleteFileW(base)) return FALSE; }
        else { reset_security(base); delete_tree(base, keep_exe ? KEEP : NULL, 0); }
    }
    if (!exists(base) && !make_protected(base, SDDL_OWNED) && !make_protected(base, SDDL_PLAIN)) return FALSE;
    if (a != INVALID_FILE_ATTRIBUTES && exists(base)) reset_security(base);
    return exists(base) && !path_user_writable(base, sid);
}

/* ---------- elevated parts ---------- */

static BOOL staged_inf_ok(const WCHAR *base)
{
    WCHAR p[MAX_PATH]; BYTE *d; DWORD n = 0; BOOL ok = FALSE;
    if (lstrlenW(base) + lstrlenW(BUS_INF_REL) >= MAX_PATH) return FALSE;
    lstrcpyW(p, base); lstrcatW(p, BUS_INF_REL);
    if ((d = slurp(p, &n, 1024 * 1024))) { ok = drv_inf_matches(d, n); release(d); }
    return ok;
}

static BOOL write_utf16_file(const WCHAR *path, const WCHAR *text, int chars)
{
    static const BYTE bom[2] = { 0xFF, 0xFE };
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); DWORD w; BOOL ok;
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    ok = WriteFile(h, bom, 2, &w, NULL) && w == 2 && WriteFile(h, text, chars * sizeof(WCHAR), &w, NULL) && w == chars * sizeof(WCHAR);
    CloseHandle(h);
    return ok;
}

static void sys_drive(WCHAR *out)               // "C:" (the drive Windows is on)
{
    WCHAR w[MAX_PATH];
    GetWindowsDirectoryW(w, MAX_PATH);
    out[0] = w[0]; out[1] = ':'; out[2] = 0;
}

static BOOL bde_flag(void)
{
    HKEY k; DWORD v = 0, n = 4;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, DRV_APPKEY, 0, KEY_QUERY_VALUE, &k)) return FALSE;
    if (RegQueryValueExW(k, BDE_VALUE, NULL, NULL, (BYTE *)&v, &n)) v = 0;
    RegCloseKey(k);
    return v == 1;
}

static void set_bde_flag(BOOL on)
{
    HKEY k; DWORD v = 1;
    if (on) {
        if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, DRV_APPKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) { RegSetValueExW(k, BDE_VALUE, 0, REG_DWORD, (const BYTE *)&v, 4); RegCloseKey(k); }
    } else if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, DRV_APPKEY, 0, KEY_SET_VALUE, &k)) { RegDeleteValueW(k, BDE_VALUE); RegCloseKey(k); }
}

static void bde_resume(void)                    // BitLocker protection on again, if we are the ones who suspended it
{
    WCHAR a[80], dr[4];
    if (!bde_flag()) return;
    sys_drive(dr);
    lstrcpyW(a, L"-protectors -enable "); lstrcatW(a, dr);
    run_tool(L"manage-bde.exe", a);                 // the restart count resumes it by itself; "already on" is not an error that matters
    set_bde_flag(FALSE);
}

// Undoes prep (and ends the installation): the task, the restart menu flag, the BitLocker suspension.
static void cancel_locked(void)
{
    run_tool(L"schtasks.exe", L"/delete /tn " TASK_NAME L" /f");
    run_tool(L"bcdedit.exe", L"/deletevalue {current} onetimeadvancedoptions");
    bde_resume();
}

static int prep_locked(const WCHAR *sid, const WCHAR *zip)
{
    static WCHAR base[MAX_PATH], p[MAX_PATH + 40], z[MAX_PATH + 40], args[2 * MAX_PATH + 80], hex[65], xml[3000], dr[4];
    ULONGLONG size; volatile LONG nocancel = 0; int n, files;
    BOOL stage = lstrcmpW(zip, L"-") != 0, inside;
    if (!valid_user_sid(sid)) return DRV_E_SID;
    base_dir(base);
    if (lstrlenW(base) + 40 >= MAX_PATH) return DRV_E_DIR;
    lstrcpyW(g_logfile, base); lstrcatW(g_logfile, L"\\driver.log");
    lstrcpyW(p, base); lstrcatW(p, L"\\" SETUP_NAME);
    inside = !lstrcmpiW(g_self, p);
    if (stage) {
        if (!make_base(base, inside, sid)) return DRV_E_DIR;
        drv_log(L"prep: staging");
        lstrcpyW(z, base); lstrcatW(z, L"\\driver.zip");
        if (!CopyFileW(zip, z, TRUE)) return DRV_E_COPY;                      // our own copy: what was checked is what is unpacked
        if (!pkg_sha256(z, hex, &size) || lstrcmpiW(hex, DRIVER_ZIP_SHA256)) { DeleteFileW(z); return DRV_E_HASH; }
        lstrcpyW(args, base); lstrcatW(args, L"\\driver");
        if (!CreateDirectoryW(args, NULL)) return DRV_E_EXTRACT;
        n = zip_extract_all(z, args, &nocancel, &files);
        DeleteFileW(z);
        drv_log2(L"extracted files", NULL, files);
        if (n != PK_OK) return DRV_E_EXTRACT;
        if (!staged_inf_ok(base)) return DRV_E_INF;
    } else {                                                                   // a retry: the staged driver must still be ours
        if (!exists(base) || path_user_writable(base, sid)) return DRV_E_DIR;
        if (!staged_inf_ok(base)) return DRV_E_INF;
    }
    if (!inside && ((!CopyFileW(g_self, p, FALSE)) || !same_file(g_self, p))) return DRV_E_COPY;     // the task starts this copy
    // BitLocker: the restart menu asks for the recovery key on an encrypted PC; suspend protection for one restart. "Not encrypted" is not an error.
    sys_drive(dr);
    lstrcpyW(args, L"-protectors -disable "); lstrcatW(args, dr); lstrcatW(args, L" -RebootCount 1");
    if (run_tool(L"manage-bde.exe", args) == 0) set_bde_flag(TRUE);
    if (run_tool(L"bcdedit.exe", L"/set {current} onetimeadvancedoptions on") != 0) { bde_resume(); return DRV_E_BCD; }
    n = drv_task_xml(xml, 3000, sid, p);
    lstrcpyW(z, base); lstrcatW(z, L"\\task.xml");
    if (n < 0 || !write_utf16_file(z, xml, n)) { cancel_locked(); return DRV_E_TASK; }
    lstrcpyW(args, L"/create /tn " TASK_NAME L" /xml \""); lstrcatW(args, z); lstrcatW(args, L"\" /f");
    n = (int)run_tool(L"schtasks.exe", args);
    DeleteFileW(z);
    if (n != 0) { cancel_locked(); return DRV_E_TASK; }
    drv_log(L"prep: done");
    return DRV_SUCCESS;
}

static int install_locked(void)
{
    static WCHAR base[MAX_PATH], inf[MAX_PATH + 40], args[2 * MAX_PATH + 40]; int res = DRV_SUCCESS; DWORD rc;
    base_dir(base);
    lstrcpyW(g_logfile, base); lstrcatW(g_logfile, L"\\driver.log");
    // what is installed must be what prep staged: the folder is ours (only SYSTEM / Administrators can change it)
    if (!exists(base) || path_user_writable(base, NULL)) return DRV_E_DIR;
    if (!staged_inf_ok(base)) return DRV_E_INF;
    lstrcpyW(inf, base); lstrcatW(inf, BUS_INF_REL);
    if (!drv_pnputil_args(args, 2 * MAX_PATH + 40, inf)) return DRV_E_DIR;
    rc = run_tool(L"pnputil.exe", args);
    if (!drv_pnputil_ok(rc)) res = DRV_E_PNP;
    else {                                                                     // the COM port part: for the virtual port; not needed by DynaRun
        lstrcpyW(inf, base); lstrcatW(inf, PORT_INF_REL);
        if (drv_pnputil_args(args, 2 * MAX_PATH + 40, inf)) run_tool(L"pnputil.exe", args);
    }
    cancel_locked();                                                           // the task and the menu flag go, whatever the result
    return res;
}

/* ---------- from the wizard (not elevated): run the elevated parts ---------- */

int driver_prep(const WCHAR *zip)
{
    static WCHAR sid[200], args[2 * MAX_PATH + 300];
    if (!user_sid(sid, 200)) return 1;
    if (is_admin()) return prep_locked(sid, zip);
    wsprintfW(args, L"/driver-prep %s \"%s\"", sid, zip);
    return run_elevated(args, NULL);
}

int driver_install(void)
{
    if (is_admin()) return install_locked();
    return run_elevated(L"/driver-install", NULL);
}

int driver_cancel(void)
{
    static WCHAR base[MAX_PATH];
    if (is_admin()) { base_dir(base); lstrcpyW(g_logfile, base); lstrcatW(g_logfile, L"\\driver.log"); cancel_locked(); return 0; }
    return run_elevated(L"/driver-cancel", NULL);
}

/* ---------- Start menu shortcut, uninstall ---------- */

static const WCHAR *lnk_names[2] = { L"Install Dyna Pro USB driver.lnk", L"安裝 Dyna Pro USB 驅動程式.lnk" };

static BOOL lnk_path(WCHAR *out, int which)
{
    WCHAR d[MAX_PATH];
    if (!SHGetSpecialFolderPathW(NULL, d, CSIDL_COMMON_PROGRAMS, FALSE) || lstrlenW(d) + lstrlenW(lnk_names[which]) + 2 >= MAX_PATH) return FALSE;
    lstrcpyW(out, d); lstrcatW(out, L"\\"); lstrcatW(out, lnk_names[which]);
    return TRUE;
}

void driver_machine_stage(const WCHAR *setup)
{
    WCHAR p[MAX_PATH]; IShellLinkW *sl; IPersistFile *pf; int i;
    for (i = 0; i < 2; i++) if (lnk_path(p, i)) DeleteFileW(p);          // one shortcut, in the language of the installing user
    if (!driver_supported() || !lnk_path(p, g_zh ? 1 : 0)) return;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) return;
    IShellLinkW_SetPath(sl, setup);
    IShellLinkW_SetArguments(sl, L"/driver");
    IShellLinkW_SetIconLocation(sl, setup, 0);
    IShellLinkW_SetDescription(sl, L"DynaRunFix: Dyna Pro USB driver");
    if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) { IPersistFile_Save(pf, p, TRUE); IPersistFile_Release(pf); }
    IShellLinkW_Release(sl);
}

// The installed driver stays: it belongs to Windows' driver store and to the user's dynamometer.
void driver_machine_uninstall(void)
{
    static WCHAR base[MAX_PATH]; WCHAR p[MAX_PATH]; int i;
    for (i = 0; i < 2; i++) if (lnk_path(p, i)) DeleteFileW(p);
    base_dir(base);
    lstrcpyW(g_logfile, base); lstrcatW(g_logfile, L"\\driver.log");
    if (exists(base) && run_tool(L"schtasks.exe", L"/query /tn " TASK_NAME) == 0) cancel_locked();
    delete_tree(base, NULL, 0);
}

/* ---------- command line ---------- */

int driver_verb(int argc, WCHAR **argv)
{
    const WCHAR *v = argv[1];
    if (!lstrcmpiW(v, L"/driver")) { g_drvmode = DRV_MODE_STANDALONE; return wizard(); }
    if (!lstrcmpiW(v, L"/driver-continue")) { g_drvmode = DRV_MODE_CONTINUE; return wizard(); }
    if (lstrcmpiW(v, L"/driver-prep") && lstrcmpiW(v, L"/driver-install") && lstrcmpiW(v, L"/driver-cancel")) return DRV_NOVERB;
    if (!is_admin()) return 1;                                                 // the elevated parts: only with the rights they need
    if (!lstrcmpiW(v, L"/driver-prep")) return argc >= 4 ? prep_locked(argv[2], argv[3]) : 1;
    if (!lstrcmpiW(v, L"/driver-install")) return install_locked();
    return driver_cancel();
}
