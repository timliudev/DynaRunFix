// The Dyna Pro USB driver (wizard pages in wizard.c, user documentation in docs/DRIVER.md).
//
// Dyna Pro's "Dyna Pro USB.zip" holds FTDI's FTDIBUS/FTDIPORT driver 2.08.24 (2012) with the Dyna Pro device id
// USB\VID_0403&PID_FDBC in ftdibus.inf. In that package ftdibus.inf and ftdiport.inf are not in the Microsoft-signed ftdibus.cat /
// ftdiport.cat that ship with them, so Windows 10/11 installs it only
// while driver signature enforcement is off, which a user can switch on for one start of Windows ("Startup Settings", key 7).
// This file automates that:
//
//   find       driver_fetch: a copy already on this computer first (the zip, or an unpacked folder), else the zip from dynapro.co.uk
//              (https, that host only). The 18 driver files in it are checked one by one against the pinned SHA-256 values below.
//   prep       (elevated, "/driver-prep <user SID> <zip | ->") stages the driver in %ProgramData%\DynaRunFix\driver (a folder that
//              only SYSTEM and the Administrators can change; an existing one is replaced, it may have been planted by an ordinary
//              user), copies this setup to %ProgramData%\DynaRunFix, suspends BitLocker (until the driver is installed; at most 3 restarts as a safety net), asks Windows for its
//              advanced start-up menu at the next start (bcdedit onetimeadvancedoptions) and registers the task
//              "DynaRunFix-DriverContinue" that starts "<setup> /driver-continue" when the user signs in. The wizard then restarts Windows.
//   continue   "/driver-continue": the wizard looks at the code integrity state: still enforced -> the user missed the menu, offer a
//              retry; off -> "/driver-install" (elevated): pnputil /add-driver <inf> /install, then the task and the menu flag go and BitLocker is resumed.
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
#include <wbemidl.h>
#include "setup.h"

// SHA-256 of "Dyna Pro USB.zip" as served by Dyna Pro on 2026-10-15 (2335114 bytes; Last-Modified 2026-05-09). A different file is never installed.
#define DRIVER_ZIP_SHA256 L"c3ae487018956767d65a08e3d5cd8434da42cdce81988753515ee0f72c49e943"
#define DRIVER_ZIP_NAME   L"Dyna Pro USB.zip"
#define TASK_NAME         L"DynaRunFix-DriverContinue"
enum { BDE_PS_ERROR = -2, BDE_PS_NONE = -1, BDE_PS_OFF = 0, BDE_PS_ON = 1, BDE_PS_UNKNOWN = 2 };   // protection status (WMI values, plus ours)
#define BDE_VALUE         L"DriverBitLockerSuspended"       // HKLM\SOFTWARE\DynaRunFix: 1 while we have BitLocker suspended until the driver is installed
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

// BitLocker: whether to suspend protection, from the OS volume's protection status (bde_query below; BDE_PS_*) and what WMI knows besides it
// (conv: GetConversionStatus, -1 unknown; nprot: number of key protectors, -1 unknown).
// Only a suspension we made may be undone later (bde_resume enables protection again): a volume that is not BitLocker-capable, fully decrypted
// or without key protectors has nothing to suspend, and one a user has suspended by hand (protection off, fully encrypted) must stay as the
// user left it. A status we could not read (unknown, query failed) is not "off": try the suspension and remember it on success, because a
// protected PC that is not suspended asks for the recovery key at the restart menu.
// Protection "off" does not always mean nothing to do: while a volume is still being encrypted or decrypted (also paused) its key protectors can
// already be in force although WMI reports "off", and the restart menu then asks for the recovery key. That case is suspended too,
// when the volume has at least one key protector.
BOOL drv_bde_suspend(int ps, int conv, int nprot)
{
    if (ps != BDE_PS_OFF) return ps != BDE_PS_NONE;
    return (conv == 2 || conv == 3 || conv == 4 || conv == 5) && nprot > 0;
}

// The same decision at prep time: also when our own flag says we suspended it earlier. A volume we suspended reports protection off (and may be
// fully encrypted), so a retry would otherwise not run manage-bde again and the restart count (-RebootCount 3) would not start over.
BOOL drv_bde_suspend_now(int ps, int conv, int nprot, BOOL ours)
{
    return drv_bde_suspend(ps, conv, nprot) || ours;
}

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

BOOL driver_partial(DWORD *have) { return pkg_partial(DRIVER_ZIP_NAME, L"DriverETag", have); }

#define DRIVER_ZIP_SIZE 2335114UL                 // the size of the official zip (see DRIVER_ZIP_SHA256)
#define DRV_MAX_ZIP     (64UL * 1024UL * 1024UL)  // a zip bigger than this is not looked at as a driver package
#define DRV_MAX_FILE    (32UL * 1024UL * 1024UL)  // limits for the files taken out of a zip: each, and all together
#define DRV_MAX_TOTAL   (96UL * 1024UL * 1024UL)

const WCHAR *driver_pinned_sha256(void) { return DRIVER_ZIP_SHA256; }

/* ---------- the files that get installed (pinned) ---------- */

// What Windows takes from "Dyna Pro USB\" when pnputil installs ftdibus.inf and ftdiport.inf: the two INFs, their catalogs (CatalogFile=) and the
// files of their [SourceDisksFiles] sections - the i386 and the amd64 variants (ftdibus.inf's amd64 section also uses i386\ftd2xx.dll for SysWOW64,
// and amd64\ftd2xx64.dll). Names are relative to that folder and compared without regard to case; '/' and '\' are the same. Size and SHA-256 are
// those of the official "Dyna Pro USB.zip" (2335114 bytes, SHA-256 above, served 2026-10-15). Other files of the zip (readme, FT Clean, the
// "64-Bit Drivers" copy, the root-level FTD2XX files, ftd2xx.inf) are not installed and not checked.
typedef pkg_file drv_file;
static const drv_file DRV_FILES[] = {
    { L"ftdibus.inf", 4868, L"47e413422655bf578e5a254432d739a816e8b2bfb90356a46f25a9f02559752c" },
    { L"ftdibus.cat", 12273, L"c60f69484dfb97d81b5814f5b25844b892d5f0f20d7fe71c11fb9b3dd6bd8bfb" },
    { L"ftdiport.inf", 5395, L"5688ec24769adae3d609a916c1dc55f1fc70216c984344f1eac3ea3bcbbe2262" },
    { L"ftdiport.cat", 11369, L"036e53ac494a2d8e6c69b510f96e9446e910c96f64bbbe8eb60b6a226ef03838" },
    { L"i386/ftdibus.sys", 62216, L"82eaaa4105fa1df8fe516bec815a7634db6aabcd176726e63761ad315f2c43ef" },
    { L"i386/ftbusui.dll", 105352, L"61032f868403855527e2fc91d176da07213ed190f93a9f99ee9f0cfb783e59fc" },
    { L"i386/ftlang.dll", 201096, L"c8d561a0f6e11970d1d70c790cfe78fa098788b12e57f54b715b110c615f806b" },
    { L"i386/ftser2k.sys", 73096, L"48f1e8d28c060eeb8e8c61d07b15df62d2f172fa34f2bae834c5c76f2a30f1c4" },
    { L"i386/ftserui2.dll", 52616, L"2fa6bab36be094e225d3cf814a84cfb643819f4af82b11a55f65b60abb429bec" },
    { L"i386/ftcserco.dll", 69000, L"9fabdabc53b8174bf19d53f08cd838db9ab6cb124360ec22c66473d1bb1c4577" },
    { L"amd64/ftdibus.sys", 75016, L"c24ac6d4e0e76b39625fc9051e092439642c3a10122f712c11a562860703f27a" },
    { L"amd64/ftbusui.dll", 108936, L"72777139f330a2e7653c0b5d427b57172275edd4535c5f743bb0ade50037a0f5" },
    { L"amd64/ftlang.dll", 214920, L"1d0f04c67da0c6e62c236d90123cbb2e89709f1e960f24ed0ba07fa691f47f99" },
    { L"amd64/ftser2k.sys", 85384, L"d4f9c5ced1e33446b45bd2affa6e716b4332af8716477a80437220ac20c6dfe0" },
    { L"amd64/ftserui2.dll", 55176, L"278c9a9a7b0167507f750d67d278ac77d98fe06873e250bede9ae4177c69e8b8" },
    { L"amd64/ftcserco.dll", 65416, L"94c3b96bdc73610cd926353c97b0918ec9515f7da64f57f15240d3966a5c2d38" },
    { L"i386/ftd2xx.dll", 218504, L"751258bb040197c7c10683a74b38a1b1aef9c68ca9a58ce2168c8a62cb913371" },
    { L"amd64/ftd2xx64.dll", 256392, L"0a776a6191c81d3682bb8d6784b45faea858a3dbfbe4e1345386068e02fb7d60" },
};
#define DRV_NFILES ((int)(sizeof(DRV_FILES) / sizeof(DRV_FILES[0])))
#define DRV_MAXF PKG_MAXF

int drv_file_count(void) { return DRV_NFILES; }

static BOOL rel_eq(const WCHAR *a, const WCHAR *b)
{
    for (; *a && *b; a++, b++) {
        WCHAR x = *a == '\\' ? '/' : *a, y = *b == '\\' ? '/' : *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return FALSE;
    }
    return !*a && !*b;
}

// The index of the pinned file with this name (relative to "Dyna Pro USB\"), or -1.
int drv_file_find(const WCHAR *rel)
{
    int i;
    for (i = 0; i < DRV_NFILES; i++) if (rel_eq(rel, DRV_FILES[i].rel)) return i;
    return -1;
}

BOOL drv_file_info(int i, const WCHAR **rel, DWORD *size, const WCHAR **sha)
{
    if (i < 0 || i >= DRV_NFILES) return FALSE;
    if (rel) *rel = DRV_FILES[i].rel;
    if (size) *size = DRV_FILES[i].size;
    if (sha) *sha = DRV_FILES[i].sha;
    return TRUE;
}

// Is a file of this size and SHA-256 the pinned file number i?
BOOL drv_file_match(int i, DWORD size, const WCHAR *sha)
{
    return i >= 0 && i < DRV_NFILES && size == DRV_FILES[i].size && sha && !lstrcmpiW(sha, DRV_FILES[i].sha);
}

// The shared machinery of package.c (safe unpacking, copying, checking) with the driver's table; these small wrappers keep the names the rest of this file
// and the tests use.
typedef pkg_check drv_check;
static int entry_index(const char *name, DWORD nlen) { return pkg_entry_index(DRV_FILES, DRV_NFILES, name, nlen); }
static BOOL file_path_at(WCHAR *out, const WCHAR *dir, const WCHAR *sub, int i) { return pkg_file_path(out, dir, sub, DRV_FILES, i); }
static BOOL file_path(WCHAR *out, const WCHAR *dir, int i) { return file_path_at(out, dir, L"\\Dyna Pro USB\\", i); }
static BOOL is_plain_dir(const WCHAR *p) { return pkg_is_plain_dir(p); }
static BOOL dirs_plain(const WCHAR *dir) { return pkg_dirs_plain(dir, L"Dyna Pro USB", DRV_FILES, DRV_NFILES); }
static int stage_files(const WCHAR *zip, const WCHAR *dir, volatile LONG *cancel, int *missing)
{ return pkg_stage_files(zip, dir, L"Dyna Pro USB", DRV_FILES, DRV_NFILES, DRV_MAX_FILE, DRV_MAX_TOTAL, cancel, missing); }
static int stage_from_dir(const WCHAR *src, const WCHAR *dir, int *missing)
{ return pkg_stage_from_dir(src, dir, L"Dyna Pro USB", DRV_FILES, DRV_NFILES, DRV_MAX_FILE, DRV_MAX_TOTAL, missing); }
static void check_files_at(const WCHAR *dir, const WCHAR *sub, WCHAR (*expect)[65], drv_check *c) { pkg_check_files(dir, sub, DRV_FILES, DRV_NFILES, expect, c); }
static void check_files(const WCHAR *dir, WCHAR (*expect)[65], drv_check *c) { check_files_at(dir, L"\\Dyna Pro USB\\", expect, c); }

// Tests: how many of the pinned files in dir (laid out as in the zip: dir\Dyna Pro USB\...) are right; *differ / *missing: the others.
int drv_check_dir(const WCHAR *dir, int *differ, int *missing)
{
    static drv_check c;
    check_files(dir, NULL, &c);
    if (differ) *differ = c.differ;
    if (missing) *missing = c.missing;
    return c.ok;
}

// "name: actual / expected" for every file that is not right (hashes shortened), for the warning page and the log.
static void check_report(const drv_check *c, WCHAR *out, int cch)
{
    WCHAR line[200], a[17], e[17]; int i;
    out[0] = 0;
    for (i = 0; i < DRV_NFILES; i++) {
        if (c->st[i] == 0) continue;
        if (c->st[i] == 2) wsprintfW(line, T(L"  %s: missing\n", L"  %s：缺少\n"), DRV_FILES[i].rel);
        else {
            lstrcpynW(a, c->hex[i][0] ? c->hex[i] : L"?", 17); lstrcpynW(e, DRV_FILES[i].sha, 17);
            wsprintfW(line, T(L"  %s: %s... (expected %s...)\n", L"  %s：%s…（已知 %s…）\n"), DRV_FILES[i].rel, a, e);
        }
        if (lstrlenW(out) + lstrlenW(line) + 1 >= cch) { lstrcatW(out, L"  ...\n"); break; }
        lstrcatW(out, line);
    }
}

static void zip_note(const WCHAR *path, const WCHAR *what)
{
    static WCHAR l[2 * MAX_PATH + 80]; DWORD a = GetFileAttributesW(path);
    lstrcpyW(l, a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) ? L"driver folder " : L"driver zip "); lstrcatW(l, path); lstrcatW(l, L": "); lstrcatW(l, what);
    user_log(l);
}

// For a package whose files are not the pinned ones and that the user may still choose to install (a newer vendor version): the Dyna Pro device id
// and the FTDIBUS install section, any driver version. (The pinned files are checked by hash; drv_inf_matches also wants version 2.08.24.)
static BOOL inf_loose(const BYTE *data, DWORD n)
{
    char *t = inf_ascii(data, n); BOOL ok;
    if (!t) return FALSE;
    ok = has_text(t, "usb\\vid_0403&pid_fdbc") && has_text(t, "[ftdibus.nt]");
    release(t);
    return ok;
}

static BOOL bus_inf_loose(const WCHAR *dir)
{
    WCHAR p[MAX_PATH]; BYTE *d; DWORD n = 0; BOOL ok = FALSE;
    if (!file_path(p, dir, 0)) return FALSE;                                   // 0: ftdibus.inf
    if ((d = slurp(p, &n, 1024 * 1024))) { ok = inf_loose(d, n); release(d); }
    return ok;
}

// Looks into a zip without installing anything: the pinned files are extracted into a scratch folder in %TEMP% and hashed there.
// *known: every pinned file is there and right. *sane: every pinned file is there and the bus INF names the Dyna Pro device id (so "install anyway"
// may be offered). rep: the files that are not right. FALSE: not a zip we can read (nothing else is meaningful then).
static BOOL zip_scan(const WCHAR *zip, BOOL *known, BOOL *sane, WCHAR *rep, int repcch)
{
    WCHAR dir[MAX_PATH]; volatile LONG nocancel = 0; static drv_check c; int missing = 0, rc;
    *known = *sane = FALSE; if (rep) rep[0] = 0;
    pkg_temp_dir(dir); lstrcatW(dir, L"\\drvcheck");
    delete_tree(dir, NULL, 0);
    if (!CreateDirectoryW(dir, NULL)) return FALSE;
    rc = stage_files(zip, dir, &nocancel, &missing);
    if (rc == PK_OK) {
        check_files(dir, NULL, &c);
        if (rep) check_report(&c, rep, repcch);
        *known = c.ok == DRV_NFILES;
        *sane = missing == 0 && bus_inf_loose(dir);
    }
    delete_tree(dir, NULL, 0);
    return rc == PK_OK;
}

// For the search of the user's folders: a zip whose pinned files are all there and right, whatever the zip is called or how it was packed.
static BOOL zip_has_all(const WCHAR *zip) { return pkg_zip_has(zip, DRV_FILES, DRV_NFILES, DRV_MAX_FILE) == DRV_NFILES; }   // cheap: only the central directory is read

static BOOL zip_known_cb(const WCHAR *zip)
{
    static WCHAR l[2 * MAX_PATH + 300], hex[65], rep[1500]; WIN32_FILE_ATTRIBUTE_DATA fa; ULONGLONG sz = 0; BOOL known = FALSE, sane = FALSE, ok;
    if (GetFileAttributesExW(zip, GetFileExInfoStandard, &fa)) sz = ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    wsprintfW(l, L"driver zip candidate %s (%lu bytes): ", zip, (DWORD)sz);
    if (!zip_has_all(zip)) { lstrcatW(l, L"not a driver package (not all of the 18 installed files are in it)"); user_log(l); return FALSE; }
    ok = zip_scan(zip, &known, &sane, rep, 1500);
    if (!ok) lstrcatW(l, L"the files could not be unpacked for the check");
    else if (known) {
        lstrcatW(l, L"all 18 installed files match the pinned ones");
        if (pkg_sha256(zip, hex, &sz)) { lstrcatW(l, L"; zip SHA-256 "); lstrcatW(l, hex); lstrcatW(l, lstrcmpiW(hex, DRIVER_ZIP_SHA256) ? L" (not the official zip as a whole)" : L" (the official zip)"); }
    } else lstrcatW(l, sane ? L"all 18 files are there but some differ from the pinned ones" : L"files differ or are missing");
    user_log(l);
    if (ok && !known && rep[0]) user_log(rep);
    return ok && known;
}

/* ---------- an already unpacked driver folder ---------- */

static BOOL write_utf16_file(const WCHAR *path, const WCHAR *text, int chars);

// The folder that holds ftdibus.inf, with the files at the same tail paths as in the zip (i386\..., amd64\...): "Downloads\Dyna Pro USB\" after
// the zip was unpacked there. The folders on the way (it, i386, amd64) must be plain ones, no link; a source file that is a link is refused by
// check_files_at like in a staging folder. All of it stays small: pkg_find_dirs looks at most two levels down in the usual folders.
static BOOL dir_layout_ok(const WCHAR *dir)
{
    WCHAR p[MAX_PATH]; WIN32_FILE_ATTRIBUTE_DATA fa;
    if (lstrlenW(dir) + 30 >= MAX_PATH || !is_plain_dir(dir)) return FALSE;
    lstrcpyW(p, dir); lstrcatW(p, L"\\ftdibus.inf");                           // cheap marker first
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return FALSE;
    lstrcpyW(p, dir); lstrcatW(p, L"\\i386");  if (!is_plain_dir(p)) return FALSE;
    lstrcpyW(p, dir); lstrcatW(p, L"\\amd64"); if (!is_plain_dir(p)) return FALSE;
    return TRUE;
}

static BOOL bus_inf_loose_at(const WCHAR *dir)
{
    WCHAR p[MAX_PATH]; BYTE *d; DWORD n = 0; BOOL ok = FALSE;
    if (!file_path_at(p, dir, L"\\", 0)) return FALSE;                         // 0: ftdibus.inf
    if ((d = slurp(p, &n, 1024 * 1024))) { ok = inf_loose(d, n); release(d); }
    return ok;
}

// Digest standing for a set of files when there is no zip to hash: the SHA-256 of the text ";rel|sha;..." of their hashes (tmp: a scratch file for
// it). It is what "install anyway" is bound to for a folder: prep computes it again from the files it copied and refuses a different one.
static BOOL digest_of(const drv_check *c, const WCHAR *tmp, WCHAR *hex)
{
    static WCHAR t[DRV_MAXF * 100]; ULONGLONG sz; int i; BOOL ok;
    t[0] = 0;
    for (i = 0; i < DRV_NFILES; i++) {
        if (c->st[i] == 2 || !c->hex[i][0]) return FALSE;
        lstrcatW(t, L";"); lstrcatW(t, DRV_FILES[i].rel); lstrcatW(t, L"|"); lstrcatW(t, c->hex[i]);
    }
    if (!write_utf16_file(tmp, t, lstrlenW(t))) return FALSE;
    ok = pkg_sha256(tmp, hex, &sz);
    DeleteFileW(tmp);
    return ok;
}

// For the search: a folder whose pinned files are all there and right (counts like a zip with the right content; no zip hash exists, only a log line).
static WCHAR g_other_dir[MAX_PATH];                // the first folder met on the way with all 18 files of which some differ (see driver_fetch: no second walk)

static BOOL dir_known_cb(const WCHAR *dir)
{
    static WCHAR l[2 * MAX_PATH + 200]; static drv_check c;
    if (!dir_layout_ok(dir)) return FALSE;
    check_files_at(dir, L"\\", NULL, &c);
    wsprintfW(l, L"driver folder candidate %s: %d of 18 installed files match the pinned ones, %d differ, %d missing", dir, c.ok, c.differ, c.missing);
    user_log(l);
    if (c.ok != DRV_NFILES && !g_other_dir[0] && c.missing == 0 && bus_inf_loose_at(dir)) lstrcpyW(g_other_dir, dir);
    return c.ok == DRV_NFILES;
}

// For the search when nothing matched: a folder with all 18 files where some differ and the bus INF names the Dyna Pro device id ("install anyway" may be offered).
static BOOL dir_other_cb(const WCHAR *dir)
{
    static drv_check c;
    if (!dir_layout_ok(dir)) return FALSE;
    check_files_at(dir, L"\\", NULL, &c);
    return c.missing == 0 && c.ok != DRV_NFILES && bus_inf_loose_at(dir);
}

// No zip with the right content: the newest zip called "Dyna Pro USB*" (our own cache first). It goes the way of a download that differs.
static BOOL driver_local_other(WCHAR *zip)
{
    WCHAR own[MAX_PATH];
    pkg_temp_dir(own); lstrcatW(own, L"\\" DRIVER_ZIP_NAME);
    if (exists(own)) { lstrcpyW(zip, own); return TRUE; }
    return pkg_find_zip_named(L"Dyna Pro USB", zip);
}

// Our own cache file is not the user's: a download that was refused (or that failed the checks) must not be offered again as a "local copy".
void driver_forget(const WCHAR *zip)
{
    WCHAR own[MAX_PATH];
    pkg_temp_dir(own); lstrcatW(own, L"\\" DRIVER_ZIP_NAME);
    if (!lstrcmpiW(own, zip)) DeleteFileW(zip);
}

static void log_differs(const WCHAR *what, const WCHAR *zip, const WCHAR *hex, const WCHAR *rep)
{
    static WCHAR l[2 * MAX_PATH + 200];
    lstrcpyW(l, what); lstrcatW(l, zip); lstrcatW(l, L" zip sha256 "); lstrcatW(l, hex); lstrcatW(l, L" (official zip: "); lstrcatW(l, DRIVER_ZIP_SHA256); lstrcatW(l, L")");
    user_log(l);
    if (rep && rep[0]) user_log(rep);                                          // the files that differ
}

static BOOL g_skip_other;                                                      // the user declined a local copy that differs: later driver_fetch calls in this run go to the website

void driver_skip_local_other(void) { g_skip_other = TRUE; }

// Looks at a driver package that was found, downloaded or chosen: a zip, or the folder it is unpacked in (the folder that holds ftdibus.inf, the given one
// or one up to two levels below it; path is replaced by that folder). PK_OK: the 18 installed files are the pinned ones (how gets DRV_ZIPDIFF when the zip as a
// whole is not the official one). DRV_W_HASH: some differ but all are there and the bus INF names the Dyna Pro device id - the user is asked; hex / size: the zip's
// SHA-256 and size (for a folder: the digest of the files' hashes, size 0), rep: the files that differ. DRV_E_HASH: not a usable driver package.
BOOL driver_layout_ok(const WCHAR *dir) { return dir_layout_ok(dir); }

int driver_check(WCHAR *path, int *how, WCHAR *hex, ULONGLONG *size, WCHAR *rep, int repcch)
{
    static drv_check c; WCHAR d[MAX_PATH], tmp[MAX_PATH]; DWORD a = GetFileAttributesW(path); BOOL known, sane;
    *how = DRV_SRC_LOCAL; hex[0] = 0; *size = 0; if (rep) rep[0] = 0;
    if (a == INVALID_FILE_ATTRIBUTES) return DRV_E_HASH;
    if (a & FILE_ATTRIBUTE_DIRECTORY) {
        *how |= DRV_SRC_DIR;
        if (!pkg_find_dir_in(path, 2, dir_layout_ok, d)) { zip_note(path, L"no ftdibus.inf with i386 and amd64 folders in it"); return DRV_E_HASH; }
        lstrcpyW(path, d);
        check_files_at(path, L"\\", NULL, &c);
        if (c.ok == DRV_NFILES) return PK_OK;
        if (c.missing == 0 && bus_inf_loose_at(path)) {
            if (rep) check_report(&c, rep, repcch);
            pkg_temp_dir(tmp); lstrcatW(tmp, L"\\drvdigest.tmp");
            if (digest_of(&c, tmp, hex)) {
                log_differs(L"driver folder, its files differ from the pinned ones: ", path, hex, rep);
                user_log(L"it passed the sanity checks: the user is asked");
                return DRV_W_HASH;
            }
        }
        zip_note(path, L"a folder that is not a usable driver package");
        return DRV_E_HASH;
    }
    if (!zip_scan(path, &known, &sane, rep, repcch) || !pkg_sha256(path, hex, size)) { zip_note(path, L"cannot be read as a driver zip"); return DRV_E_HASH; }
    if (known) {
        if (lstrcmpiW(hex, DRIVER_ZIP_SHA256)) { *how |= DRV_ZIPDIFF; zip_note(path, L"the zip as a whole is not the official one (re-packed?), the driver files are identical"); }
        return PK_OK;
    }
    log_differs(L"driver zip, its files differ from the pinned ones: ", path, hex, rep);
    if (sane) { user_log(L"it passed the sanity checks: the user is asked"); return DRV_W_HASH; }
    user_log(L"it failed the sanity checks: not offered");
    return DRV_E_HASH;
}

int driver_fetch(WCHAR *zip, progress_fn cb, volatile LONG *cancel, DWORD *err, int *how, WCHAR *hex, ULONGLONG *size, WCHAR *rep, int repcch)
{
    int rc, hw; BOOL skip_other = g_skip_other;                                 // kept for the whole run, so resume and retry do not offer the declined copy again
    *how = DRV_SRC_DOWNLOADED; hex[0] = 0; *size = 0; if (rep) rep[0] = 0;
    g_pkg_log = user_log;                                                      // the folder search says which folders it looks in
    g_other_dir[0] = 0;
    user_log(L"driver_fetch: looking for a copy of the driver on this computer");
    if (cb) cb((DWORD)-1, 2);                                                  // the page shows "looking" (a moving bar) until a download starts
    // 1. a copy whose driver files are the pinned ones (a zip of any name or packing, or an unpacked folder): nothing to download
    pkg_temp_dir(zip); lstrcatW(zip, L"\\" DRIVER_ZIP_NAME);
    if (((exists(zip) && zip_known_cb(zip)) || pkg_find_zip_if(DRV_MAX_ZIP, zip_known_cb, zip)) ||
        pkg_find_dirs(dir_known_cb, zip)) {
        hw = driver_check(zip, how, hex, size, rep, repcch);
        if (hw == PK_OK) { zip_note(zip, L"found on this computer, its driver files match the pinned ones: nothing is downloaded"); return PK_OK; }
    }
    // 2. a zip called "Dyna Pro USB*", or a folder with all 18 files, whose files differ goes the way of a download that differs: sanity checks, then the
    //    user is asked. One that is not a usable driver package is skipped (logged) and the official site is tried.
    if (skip_other) user_log(L"the user declined a local copy that differs: not offered again, the official site is used");
    if (!skip_other && (driver_local_other(zip) || (g_other_dir[0] && lstrcpyW(zip, g_other_dir)))) {   // (the folders were walked once, above)
        hw = driver_check(zip, how, hex, size, rep, repcch);
        if (hw == PK_OK || hw == DRV_W_HASH) return hw;
        zip_note(zip, L"differs from the pinned files and is not a usable driver package: skipped");
        driver_forget(zip);
        *how = DRV_SRC_DOWNLOADED; hex[0] = 0; *size = 0; if (rep) rep[0] = 0;
    }
    // 3. the official site
    rc = pkg_fetch(DYNAPRO_USB_ZIP, DRIVER_ZIP_NAME, L"DriverETag", DYNAPRO_HOST, zip, cb, cancel, err);
    if (rc != PK_OK) return rc;
    if (cb) cb((DWORD)-1, 1);                                                  // the download is over: the page shows "checking" (not pausable) until we return
    hw = driver_check(zip, how, hex, size, rep, repcch);
    *how &= ~DRV_SRC_LOCAL;                                                    // it is our own download
    if (hw == DRV_E_HASH) DeleteFileW(zip);                                    // the file stays only when the user may still choose "install anyway"
    return hw;
}

/* ---------- running the system tools ---------- */

static WCHAR g_logfile[MAX_PATH];

static void drv_log_line(const WCHAR *line)
{
    static char u[4096]; int n; HANDLE h; DWORD w;
    if (!g_logfile[0] || (n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, sizeof(u) - 2, NULL, NULL)) <= 0) return;
    h = CreateFileW(g_logfile, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    u[n - 1] = '\r'; u[n] = '\n';
    WriteFile(h, u, n + 1, &w, NULL);
    CloseHandle(h);
}

// A text of several lines is written as several lines (each one fits the buffer; a long one used to be dropped whole). The file is UTF-8 throughout.
static void drv_log(const WCHAR *line)
{
    WCHAR seg[1024]; const WCHAR *p = line, *q; int n;
    while (*p) {
        for (q = p; *q && *q != '\n'; q++);
        n = (int)(q - p); if (n > 1000) n = 1000;
        while (n > 0 && p[n - 1] == '\r') n--;
        if (n > 0) { lstrcpynW(seg, p, n + 1); drv_log_line(seg); }
        p = *q ? q + 1 : q;
    }
}

static void drv_log2(const WCHAR *what, const WCHAR *arg, DWORD v)
{
    static WCHAR b[1200];
    wsprintfW(b, L"%s %s -> %ld", what, arg ? arg : L"", (long)v);
    drv_log(b);
}

// "tool args", hidden, waited for; its exit code, or (DWORD)-1 when it could not be started or did not finish in 3 minutes.
static DWORD g_tool_ok_code;                       // an exit code that is not an error for the next run_tool (and what it means), see cancel_locked
static const WCHAR *g_tool_ok_text;

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
    if (g_tool_ok_text && rc == g_tool_ok_code) {
        static WCHAR l[2 * MAX_PATH + 240];
        lstrcpyW(l, tool); lstrcatW(l, L" "); lstrcatW(l, args); lstrcatW(l, L" -> "); lstrcatW(l, g_tool_ok_text);
        drv_log(l);
    } else drv_log2(tool, args, rc);
    release(cmd);
    return rc;
}

// Recovery passwords: 8 groups of 6 digits separated by '-'. Found by that pattern (any run of at least two such groups), not by a label, so it
// works in every language; the digits become '*', the '-' stay. Key protector IDs (GUIDs) are not touched.
static BOOL is_digit_byte(BYTE c) { return c >= '0' && c <= '9'; }
static BOOL six_digits(const BYTE *p) { int i; for (i = 0; i < 6; i++) if (!is_digit_byte(p[i])) return FALSE; return TRUE; }

static void mask_secrets(BYTE *b, DWORD n)
{
    DWORD i, j, k, end, groups;
    for (i = 0; i < n; ) {
        if (!is_digit_byte(b[i]) || (i && is_digit_byte(b[i - 1]))) { i++; continue; }
        groups = 0; j = i; end = i;
        while (j + 6 <= n && six_digits(b + j) && (j + 6 == n || !is_digit_byte(b[j + 6]))) {
            groups++; end = j + 6; j = end;
            if (j < n && b[j] == '-') j++; else break;
        }
        if (groups >= 2) { for (k = i; k < end; k++) if (is_digit_byte(b[k])) b[k] = '*'; i = end; }
        else i++;
    }
}

// Diagnostics only: "manage-bde <args>" with its output (OEM text, as is) appended to the file out, after the secrets in it are masked. The output
// is read through a pipe into memory and masked there: the plain text never reaches the disk. The exit code goes to the log like any tool's.
static void run_tool_to_file(const WCHAR *args, const WCHAR *out)
{
    static WCHAR win[MAX_PATH], exe[MAX_PATH]; static BYTE ob[16384]; BYTE tmp[1024];
    WCHAR *cmd; STARTUPINFOW si; PROCESS_INFORMATION pi; SECURITY_ATTRIBUTES sa; HANDLE rd = NULL, wr = NULL, f; DWORD rc = (DWORD)-1, got = 0, avail, r, waited = 0; BOOL done = FALSE;
    if (!GetWindowsDirectoryW(win, MAX_PATH - 40) || !drv_sys_tool(win, is_wow64(), L"manage-bde.exe", exe, MAX_PATH) || !exists(exe)) return;
    zero(&sa, sizeof(sa)); sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);                          // only the write end goes to the child
    if (!(cmd = alloc((lstrlenW(exe) + lstrlenW(args) + 8) * sizeof(WCHAR)))) { CloseHandle(rd); CloseHandle(wr); return; }
    lstrcpyW(cmd, L"\""); lstrcatW(cmd, exe); lstrcatW(cmd, L"\" "); lstrcatW(cmd, args);
    zero(&si, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES; si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    if (CreateProcessW(exe, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, win, &si, &pi)) {
        CloseHandle(wr); wr = NULL;                                            // the child holds the only other copy
        for (;;) {
            while (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail) {
                if (!ReadFile(rd, tmp, avail < sizeof(tmp) ? avail : sizeof(tmp), &r, NULL) || !r) break;
                if (got + r <= sizeof(ob)) { CopyMemory(ob + got, tmp, r); got += r; }   // more than 16 KB is cut off
            }
            if (done) break;
            if (WaitForSingleObject(pi.hProcess, 100) == WAIT_OBJECT_0) { GetExitCodeProcess(pi.hProcess, &rc); done = TRUE; continue; }   // one more drain
            if (++waited > 600) { TerminateProcess(pi.hProcess, 1); break; }   // 60 s
        }
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    if (wr) CloseHandle(wr);
    CloseHandle(rd);
    mask_secrets(ob, got);
    f = CreateFileW(out, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) { if (got) WriteFile(f, ob, got, &r, NULL); CloseHandle(f); }
    drv_log2(L"manage-bde (output, secrets masked, in the file next to this log)", args, rc);
    release(cmd);
}

/* ---------- the folder %ProgramData%\DynaRunFix ---------- */

static void base_dir(WCHAR *out)
{
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, SHGFP_TYPE_CURRENT, out))) lstrcpyW(out, L"C:\\ProgramData");
    lstrcatW(out, L"\\DynaRunFix");
}

// Owner Administrators; SYSTEM and Administrators full control, Users read and execute; the DACL does not inherit from ProgramData.
static const WCHAR SDDL_OWNED[] = L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)";
// The same for the one kept file (a file: no inheritance flags).
static const WCHAR SDDL_FILE[] = L"O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x1200a9;;;BU)";
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

// A folder (or file) that exists already: owner and DACL set to ours (an ordinary user may have created it).
static void reset_security(const WCHAR *dir, const WCHAR *sddl)
{
    PSECURITY_DESCRIPTOR sd; PSID owner = NULL; PACL dacl = NULL; BOOL present = FALSE, def = FALSE;
    enable_privilege(L"SeTakeOwnershipPrivilege"); enable_privilege(L"SeRestorePrivilege"); enable_privilege(L"SeBackupPrivilege");
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, NULL)) return;
    if (GetSecurityDescriptorOwner(sd, &owner, &def) && GetSecurityDescriptorDacl(sd, &present, &dacl, &def) && owner && present)
        SetNamedSecurityInfoW((LPWSTR)dir, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, owner, NULL, dacl, NULL);
    LocalFree(sd);
}

// Why the staging folder could not be made (OutputDebugString only: the folder is not trusted, so nothing is written into it).
static void base_fail(const WCHAR *why) { OutputDebugStringW(L"DynaRunFix driver: staging folder: "); OutputDebugStringW(why); OutputDebugStringW(L"\n"); }

// Does dir hold nothing but the one top-level file named keep?
static BOOL holds_only(const WCHAR *dir, const WCHAR *keep)
{
    WCHAR p[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE f; BOOL only = TRUE;
    if (lstrlenW(dir) + 3 >= MAX_PATH) return FALSE;
    lstrcpyW(p, dir); lstrcatW(p, L"\\*");
    if ((f = FindFirstFileW(p, &fd)) == INVALID_HANDLE_VALUE) return FALSE;
    do {
        if (!lstrcmpW(fd.cFileName, L".") || !lstrcmpW(fd.cFileName, L"..")) continue;
        if (lstrcmpiW(fd.cFileName, keep) || (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) only = FALSE;
    } while (only && FindNextFileW(f, &fd));
    FindClose(f);
    return only;
}

// The kept copy of the setup: a plain file with one link only (a hard link would share its security with a file elsewhere).
static BOOL plain_single_file(const WCHAR *path)
{
    BY_HANDLE_FILE_INFORMATION bi; BOOL ok;
    HANDLE h = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    ok = GetFileInformationByHandle(h, &bi) && !(bi.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && bi.nNumberOfLinks == 1;
    CloseHandle(h);
    return ok;
}

// A new, empty %ProgramData%\DynaRunFix that only SYSTEM and the Administrators can change; the later elevated steps run what is staged there.
// An existing folder is removed first; one that cannot be removed (something in it is held open, e.g. by an ordinary user: an open handle
// keeps its access whatever the ACL says) is never adopted: FALSE. The new folder is created with the protected DACL (no window with the
// parent's inheritance) and checked. keep_exe: our own copy of the setup is running from there (a retry after the restart): that one
// file stays, and the folder must hold nothing else. FALSE also when the folder is still changeable by users.
static BOOL make_base(const WCHAR *base, BOOL keep_exe, const WCHAR *sid)
{
    static const WCHAR KEEP[] = L"DynaRunFix-Setup.exe\0";
    DWORD a = GetFileAttributesW(base);
    if (a != INVALID_FILE_ATTRIBUTES) {
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) {                                  // a link: removed, never followed
            if (!((a & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(base) : DeleteFileW(base))) { base_fail(L"a link could not be removed"); return FALSE; }
        }
        else if (!(a & FILE_ATTRIBUTE_DIRECTORY)) { if (!DeleteFileW(base)) { base_fail(L"a file with that name could not be removed"); return FALSE; } }
        else { reset_security(base, SDDL_OWNED); delete_tree(base, keep_exe ? KEEP : NULL, 0); }
    }
    if (exists(base)) {
        if (!keep_exe || !holds_only(base, KEEP)) { base_fail(L"the old folder could not be removed (something in it is in use)"); return FALSE; }
        reset_security(base, SDDL_OWNED);                                                    // only our own running copy is left in it
    } else if (!make_protected(base, SDDL_OWNED) && !make_protected(base, SDDL_PLAIN)) { base_fail(L"the folder could not be created"); return FALSE; }
    if (!exists(base) || path_user_writable(base, sid)) { base_fail(L"the folder is not protected (owner or permissions)"); return FALSE; }
    if (keep_exe) {                                                              // the kept setup copy: its owner and permissions are ours too
        WCHAR f[MAX_PATH];
        if (lstrlenW(base) + lstrlenW(KEEP) + 2 >= MAX_PATH) { base_fail(L"the path is too long"); return FALSE; }
        lstrcpyW(f, base); lstrcatW(f, L"\\"); lstrcatW(f, KEEP);
        if (exists(f)) {
            if (!plain_single_file(f)) { base_fail(L"the kept setup file is a link or has several links"); return FALSE; }
            reset_security(f, SDDL_FILE);
            if (path_user_writable(f, sid)) { base_fail(L"the kept setup file is not protected (owner or permissions)"); return FALSE; }
        }
    }
    return TRUE;
}

// The log goes into the staging folder only once the folder has been checked (an ordinary user could have made it).
static void log_in_base(const WCHAR *base, const WCHAR *sid)
{
    g_logfile[0] = 0;
    if (exists(base) && !path_user_writable(base, sid) && lstrlenW(base) + 12 < MAX_PATH) { lstrcpyW(g_logfile, base); lstrcatW(g_logfile, L"\\driver.log"); }
}

/* ---------- elevated parts ---------- */

// When the user accepted a driver package whose files are not the pinned ones ("install anyway"), prep leaves this file in the staging folder
// (administrators-only): the SHA-256 of exactly the files it staged, as ";rel|sha;" for each. Later steps (a retry after a missed menu, the install
// after the restart) accept those files and nothing else, whatever is put into the folder. It is deleted when the driver step ends
// (cancel_locked: success, failure, cancel) and goes with the whole folder on uninstall. It is never a switch that accepts "anything".
static const WCHAR ACCEPT_FILE[] = L"\\accepted.txt";

static BOOL accept_path(WCHAR *f, const WCHAR *base)
{
    if (lstrlenW(base) + lstrlenW(ACCEPT_FILE) + 1 >= MAX_PATH) return FALSE;
    lstrcpyW(f, base); lstrcatW(f, ACCEPT_FILE);
    return TRUE;
}

static BOOL write_utf16_file(const WCHAR *path, const WCHAR *text, int chars);

static BOOL write_accepted(const WCHAR *base, const drv_check *c)
{
    static WCHAR t[DRV_MAXF * 100]; WCHAR f[MAX_PATH]; int i;
    t[0] = 0;
    for (i = 0; i < DRV_NFILES; i++) {
        if (c->st[i] == 2 || !c->hex[i][0]) return FALSE;
        lstrcatW(t, L";"); lstrcatW(t, DRV_FILES[i].rel); lstrcatW(t, L"|"); lstrcatW(t, c->hex[i]);
    }
    lstrcatW(t, L";");
    return accept_path(f, base) && write_utf16_file(f, t, lstrlenW(t));
}

static const WCHAR *wfind(const WCHAR *h, const WCHAR *needle)
{
    int i;
    for (; *h; h++) { for (i = 0; needle[i] && h[i] == needle[i]; i++); if (!needle[i]) return h; }
    return NULL;
}

// The accepted hashes in the order of the table; FALSE when the file is damaged or lacks a file.
static BOOL load_accepted(const WCHAR *base, WCHAR (*acc)[65])
{
    WCHAR f[MAX_PATH], needle[80]; BYTE *d; DWORD n = 0; const WCHAR *t, *h; BOOL ok = TRUE; int i, k;
    if (!accept_path(f, base) || !(d = slurp(f, &n, 16 * 1024)) || n < 2) { if (d) release(d); return FALSE; }
    t = (const WCHAR *)(d + 2);                                                // after the BOM; slurp leaves two zero bytes at the end
    for (i = 0; i < DRV_NFILES && ok; i++) {
        lstrcpyW(needle, L";"); lstrcatW(needle, DRV_FILES[i].rel); lstrcatW(needle, L"|");
        if (!(h = wfind(t, needle))) { ok = FALSE; break; }
        h += lstrlenW(needle);
        for (k = 0; k < 64; k++) {
            WCHAR ch = h[k];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) { ok = FALSE; break; }
            acc[i][k] = ch;
        }
        acc[i][64] = 0;
    }
    release(d);
    return ok;
}

// The staged files (below base\driver) are what pnputil is going to install: all there, no link anywhere, and each equal to the pinned file - or,
// when the user accepted another package, to the accepted file (which also has to carry Dyna Pro's device id).
static BOOL staged_ok(const WCHAR *base)
{
    static WCHAR root[MAX_PATH], acc[DRV_MAXF][65]; static drv_check c; WCHAR f[MAX_PATH]; BOOL accepted;
    if (lstrlenW(base) + 12 >= MAX_PATH) return FALSE;
    lstrcpyW(root, base); lstrcatW(root, L"\\driver");
    if (!dirs_plain(root)) return FALSE;
    accepted = accept_path(f, base) && exists(f);
    if (accepted && !load_accepted(base, acc)) return FALSE;
    check_files(root, accepted ? acc : NULL, &c);
    if (c.ok != DRV_NFILES) return FALSE;
    return accepted ? bus_inf_loose(root) : TRUE;
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

/* ---------- BitLocker protection status (WMI) ---------- */

static const GUID BDE_CLSID_WbemLocator = { 0x4590f811, 0x1d3a, 0x11d0, { 0x89, 0x1f, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24 } };
static const GUID BDE_IID_IWbemLocator  = { 0xdc12a687, 0x737f, 0x11cf, { 0x88, 0x4d, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24 } };

// Win32_EncryptableVolume.ProtectionStatus of the Windows drive ("C:"): BDE_PS_OFF / BDE_PS_ON / BDE_PS_UNKNOWN as WMI says,
// BDE_PS_NONE: no BitLocker here (namespace or class missing, e.g. Home editions; or no such volume), BDE_PS_ERROR: could not ask.
typedef struct { int conv, pct, nprot, suspcnt; HRESULT hr[4]; } bde_info;   // hr: GetObject + __PATH, then the three methods (diagnostics; 1 = not tried)
//   // GetConversionStatus (0 decrypted, 1 encrypted, 2 encrypting, 3 decrypting, 4 / 5 paused), its percentage,
                                                              // number of key protectors, GetSuspendCount; -1: not known

// One method of the volume object (its out-parameters in *out); in: the name and value of its one uint32 in-parameter, or NULL.
static HRESULT bde_call(IWbemServices *svc, IWbemClassObject *cls, const WCHAR *path, const WCHAR *method, const WCHAR *inname, LONG inval, IWbemClassObject **out)
{
    IWbemClassObject *sig = NULL, *in = NULL; BSTR bp = SysAllocString(path), bm = SysAllocString(method); HRESULT hr; VARIANT v;
    *out = NULL;
    if (!bp || !bm) { SysFreeString(bp); SysFreeString(bm); return E_OUTOFMEMORY; }
    hr = IWbemClassObject_GetMethod(cls, method, 0, &sig, NULL);
    if (SUCCEEDED(hr) && sig && inname) {
        hr = IWbemClassObject_SpawnInstance(sig, 0, &in);
        if (SUCCEEDED(hr)) { VariantInit(&v); v.vt = VT_I4; v.lVal = inval; hr = IWbemClassObject_Put(in, inname, 0, &v, 0); }
    }
    if (SUCCEEDED(hr)) hr = IWbemServices_ExecMethod(svc, bp, bm, 0, NULL, in, out, NULL);
    if (in) IWbemClassObject_Release(in);
    if (sig) IWbemClassObject_Release(sig);
    SysFreeString(bp); SysFreeString(bm);
    return hr;
}

static BOOL bde_get_int(IWbemClassObject *o, const WCHAR *name, int *val)
{
    VARIANT v; BOOL ok = FALSE;
    VariantInit(&v);
    if (o && SUCCEEDED(IWbemClassObject_Get(o, name, 0, &v, NULL, NULL)) && v.vt == VT_I4) { *val = (int)v.lVal; ok = TRUE; }
    VariantClear(&v);
    return ok;
}

// conversion status, protector count and suspend count of the volume object obj (what ProtectionStatus alone does not tell).
static void bde_details(IWbemServices *svc, IWbemClassObject *obj, bde_info *bi)
{
    IWbemClassObject *cls = NULL, *out = NULL; BSTR bcls = SysAllocString(L"Win32_EncryptableVolume"); VARIANT v, a; WCHAR path[300]; int rv = -1;
    bi->conv = bi->pct = bi->nprot = bi->suspcnt = -1;
    bi->hr[0] = bi->hr[1] = bi->hr[2] = bi->hr[3] = 1;
    VariantInit(&v);
    if (!bcls) goto done;
    bi->hr[0] = IWbemServices_GetObject(svc, bcls, 0, NULL, &cls, NULL);
    if (FAILED(bi->hr[0]) || !cls) goto done;
    bi->hr[0] = IWbemClassObject_Get(obj, L"__PATH", 0, &v, NULL, NULL);
    if (FAILED(bi->hr[0])) goto done;
    if (v.vt != VT_BSTR || !v.bstrVal || SysStringLen(v.bstrVal) >= 300) { bi->hr[0] = 2; goto done; }
    lstrcpyW(path, v.bstrVal);
    bi->hr[1] = bde_call(svc, cls, path, L"GetConversionStatus", L"PrecisionFactor", 0, &out);
    if (SUCCEEDED(bi->hr[1]) && bde_get_int(out, L"ReturnValue", &rv) && rv == 0) {
        bde_get_int(out, L"ConversionStatus", &bi->conv);
        bde_get_int(out, L"EncryptionPercentage", &bi->pct);
    }
    if (out) { IWbemClassObject_Release(out); out = NULL; }
    rv = -1;
    bi->hr[2] = bde_call(svc, cls, path, L"GetKeyProtectors", L"KeyProtectorType", 0, &out);
    if (SUCCEEDED(bi->hr[2]) && bde_get_int(out, L"ReturnValue", &rv) && rv == 0) {
        LONG lo = 0, hi = -1;
        VariantInit(&a);
        if (SUCCEEDED(IWbemClassObject_Get(out, L"VolumeKeyProtectorID", 0, &a, NULL, NULL))) {
            if ((a.vt & VT_ARRAY) && a.parray) { if (SUCCEEDED(SafeArrayGetLBound(a.parray, 1, &lo)) && SUCCEEDED(SafeArrayGetUBound(a.parray, 1, &hi))) bi->nprot = (int)(hi - lo + 1); }
            else if (a.vt == VT_NULL || a.vt == VT_EMPTY) bi->nprot = 0;
        }
        VariantClear(&a);
    }
    if (out) { IWbemClassObject_Release(out); out = NULL; }
    bi->hr[3] = bde_call(svc, cls, path, L"GetSuspendCount", NULL, 0, &out);
    if (SUCCEEDED(bi->hr[3]) && bde_get_int(out, L"ReturnValue", &rv) && rv == 0) bde_get_int(out, L"SuspendCount", &bi->suspcnt);
done:
    if (out) IWbemClassObject_Release(out);
    if (cls) IWbemClassObject_Release(cls);
    VariantClear(&v);
    SysFreeString(bcls);
}

static int bde_query(const WCHAR *drive, bde_info *bi)
{
    static const WCHAR q1[] = L"SELECT ProtectionStatus FROM Win32_EncryptableVolume WHERE DriveLetter = '";
    WCHAR q[120]; int ps = BDE_PS_ERROR; HRESULT hr, hrc;
    IWbemLocator *loc = NULL; IWbemServices *svc = NULL; IEnumWbemClassObject *en = NULL; IWbemClassObject *obj = NULL;
    BSTR bns = NULL, bql = NULL, bq = NULL, bprop = NULL; ULONG got = 0; VARIANT v;
    if (bi) { bi->conv = bi->pct = bi->nprot = bi->suspcnt = -1; bi->hr[0] = bi->hr[1] = bi->hr[2] = bi->hr[3] = 1; }
    hrc = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);                   // RPC_E_CHANGED_MODE: already initialised another way, usable
    if (FAILED(hrc) && hrc != RPC_E_CHANGED_MODE) return BDE_PS_ERROR;
    lstrcpyW(q, q1); lstrcatW(q, drive); lstrcatW(q, L"'");
    bns = SysAllocString(L"ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption"); bql = SysAllocString(L"WQL");
    bq = SysAllocString(q); bprop = SysAllocString(L"ProtectionStatus");
    if (!bns || !bql || !bq || !bprop) goto done;
    if (FAILED(CoCreateInstance(&BDE_CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &BDE_IID_IWbemLocator, (void **)&loc))) goto done;
    hr = IWbemLocator_ConnectServer(loc, bns, NULL, NULL, NULL, 0, NULL, NULL, &svc);
    if (FAILED(hr)) { if (hr == WBEM_E_INVALID_NAMESPACE) ps = BDE_PS_NONE; goto done; }
    if (FAILED(CoSetProxyBlanket((IUnknown *)svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE))) goto done;
    hr = IWbemServices_ExecQuery(svc, bql, bq, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &en);
    if (FAILED(hr)) { if (hr == WBEM_E_INVALID_CLASS) ps = BDE_PS_NONE; goto done; }
    CoSetProxyBlanket((IUnknown *)en, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    hr = IEnumWbemClassObject_Next(en, 10000, 1, &obj, &got);               // 10 s at most
    if (hr == WBEM_S_FALSE && !got) { ps = BDE_PS_NONE; goto done; }          // no such encryptable volume
    if (FAILED(hr) || hr == WBEM_S_TIMEDOUT || !got) goto done;
    VariantInit(&v);
    if (SUCCEEDED(IWbemClassObject_Get(obj, bprop, 0, &v, NULL, NULL)) && v.vt == VT_I4) ps = (v.lVal == BDE_PS_OFF || v.lVal == BDE_PS_ON) ? (int)v.lVal : BDE_PS_UNKNOWN;
    VariantClear(&v);
    if (bi) bde_details(svc, obj, bi);
done:
    if (obj) IWbemClassObject_Release(obj);
    if (en) IEnumWbemClassObject_Release(en);
    if (svc) IWbemServices_Release(svc);
    if (loc) IWbemLocator_Release(loc);
    SysFreeString(bns); SysFreeString(bql); SysFreeString(bq); SysFreeString(bprop);
    if (SUCCEEDED(hrc)) CoUninitialize();
    return ps;
}

static int bde_protection(const WCHAR *drive) { return bde_query(drive, NULL); }

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
    WCHAR a[80], dr[4]; DWORD rc; int ps;
    if (!bde_flag()) return;
    sys_drive(dr);
    lstrcpyW(a, L"-protectors -enable "); lstrcatW(a, dr);
    rc = run_tool(L"manage-bde.exe", a);            // the log has the code
    if (rc) {
        drv_log2(L"bitlocker resume failed", NULL, rc);
        ps = bde_protection(dr);
        if (ps != BDE_PS_ON && ps != BDE_PS_NONE) return;    // still not on: the flag stays, the next cancel / continue / uninstall tries again ("already on" and "no BitLocker here" clear it below)
    }
    set_bde_flag(FALSE);
}

// Undoes prep (and ends the installation): the task, the restart menu flag, the BitLocker suspension (protection back on).
static void cancel_locked(void)
{
    run_tool(L"schtasks.exe", L"/delete /tn " TASK_NAME L" /f");
    g_tool_ok_code = 1; g_tool_ok_text = L"not present (1: the start-up menu had already used the one-time option up): nothing to remove";
    run_tool(L"bcdedit.exe", L"/deletevalue {current} onetimeadvancedoptions");
    g_tool_ok_text = NULL;
    { WCHAR b[MAX_PATH], f[MAX_PATH]; base_dir(b); if (accept_path(f, b)) DeleteFileW(f); }   // an "install anyway" acceptance ends with the driver step
    bde_resume();
}

static int prep_locked(const WCHAR *sid, const WCHAR *zip, const WCHAR *accept)
{
    static WCHAR base[MAX_PATH], p[MAX_PATH + 40], z[MAX_PATH + 40], args[2 * MAX_PATH + 80], hex[65], xml[3000], dr[4];
    ULONGLONG size; volatile LONG nocancel = 0; int n, files, ps; BOOL bde_warn = FALSE, suspend, partly; bde_info bi;
    BOOL stage = lstrcmpW(zip, L"-") != 0, inside, fromdir = FALSE;
    if (!valid_user_sid(sid)) return DRV_E_SID;
    if (stage) { DWORD fa = GetFileAttributesW(zip); fromdir = fa != INVALID_FILE_ATTRIBUTES && (fa & FILE_ATTRIBUTE_DIRECTORY) && !(fa & FILE_ATTRIBUTE_REPARSE_POINT); }
    base_dir(base);
    if (lstrlenW(base) + 40 >= MAX_PATH) return DRV_E_DIR;
    lstrcpyW(p, base); lstrcatW(p, L"\\" SETUP_NAME);
    inside = !lstrcmpiW(g_self, p);
    if (stage) {
        static drv_check chk;
        if (!make_base(base, inside, sid)) return DRV_E_DIR;
        log_in_base(base, sid);
        drv_log(L"prep: staging");
        lstrcpyW(args, base); lstrcatW(args, L"\\driver");
        if (fromdir) {                                                         // an already unpacked folder: the pinned files are copied from it
            drv_log(L"prep: staging from an unpacked folder:"); drv_log(zip);
            if (!CreateDirectoryW(args, NULL)) return DRV_E_EXTRACT;           // make_base left the folder empty: this one is new
            n = stage_from_dir(zip, args, &files);
        } else {
            lstrcpyW(z, base); lstrcatW(z, L"\\driver.zip");
            if (!CopyFileW(zip, z, TRUE)) return DRV_E_COPY;                  // our own copy, in the administrators-only folder: what is read is this copy
            if (!pkg_sha256(z, hex, &size)) { DeleteFileW(z); return DRV_E_HASH; }
            drv_log2(L"prep: sha256 of the zip", hex, 0);
            if (!CreateDirectoryW(args, NULL)) { DeleteFileW(z); return DRV_E_EXTRACT; }     // make_base left the folder empty: this one is new
            n = stage_files(z, args, &nocancel, &files);                       // only the pinned files, into folders made here; files: how many are missing
            DeleteFileW(z);
        }
        drv_log2(L"prep: pinned files missing in the source", NULL, files);
        if (n != PK_OK || files) { delete_tree(args, NULL, 0); return DRV_E_EXTRACT; }
        // what is hashed now are the staged copies, and those are what pnputil installs: there is no second extraction between the check and the install
        check_files(args, NULL, &chk);
        if (fromdir) {                                                         // no zip hash: the digest of the staged files' hashes is what "install anyway" is bound to
            lstrcpyW(z, base); lstrcatW(z, L"\\digest.tmp");
            if (!digest_of(&chk, z, hex)) { delete_tree(args, NULL, 0); return DRV_E_HASH; }
        }
        drv_log2(L"prep: staged files that are the pinned ones", NULL, chk.ok);
        if (chk.ok == DRV_NFILES) {
            if (!fromdir && lstrcmpiW(hex, DRIVER_ZIP_SHA256)) drv_log(L"prep: the zip as a whole is not the official one, its driver files are identical");
        } else if (accept && accept[0] && !lstrcmpiW(accept, hex) && bus_inf_loose(args) && write_accepted(base, &chk)) {
            drv_log(L"prep: the staged files differ from the pinned ones; they are installed because the user chose \"install anyway\" for this zip:");
            drv_log(hex);
        } else {
            WCHAR rep[1500];
            drv_log(L"prep: the staged files are not the pinned ones and were not accepted:");
            check_report(&chk, rep, 1500); drv_log(rep);
            delete_tree(args, NULL, 0);
            return DRV_E_HASH;
        }
        if (!staged_ok(base)) return DRV_E_INF;
    } else {                                                                   // a retry: the staged driver must still be ours
        if (!exists(base) || path_user_writable(base, sid)) return DRV_E_DIR;
        log_in_base(base, sid);
        if (!staged_ok(base)) return DRV_E_INF;
    }
    if (!inside && ((!CopyFileW(g_self, p, FALSE)) || !same_file(g_self, p))) return DRV_E_COPY;     // the task starts this copy
    // BitLocker: the restart menu asks for the recovery key on an encrypted PC; suspend protection until the driver is installed (resumed by install/cancel).
    // -RebootCount 3 is the safety net: protection returns by itself after 3 restarts if the flow is abandoned; a retry suspends again and starts the count over.
    // "Not encrypted" is not an error.
    // Only when protection is on (see drv_bde_suspend), or when we suspended it ourselves earlier (a retry): what is off or was suspended by the user is not ours to switch on again later.
    sys_drive(dr);
    ps = bde_query(dr, &bi);
    drv_log2(L"bitlocker protection status (0 off, 1 on, 2 unknown, -1 none, -2 not readable)", NULL, ps);
    drv_log2(L"bitlocker conversion status (0 decrypted, 1 encrypted, 2 encrypting, 3 decrypting, 4/5 paused, -1 unknown)", NULL, bi.conv);
    drv_log2(L"bitlocker conversion percentage", NULL, bi.pct);
    drv_log2(L"bitlocker key protectors", NULL, bi.nprot);
    drv_log2(L"bitlocker suspend count", NULL, bi.suspcnt);
    drv_log2(L"bitlocker wmi details: HRESULT of GetObject/__PATH (1 = not tried)", NULL, (DWORD)bi.hr[0]);
    drv_log2(L"bitlocker wmi details: HRESULT of GetConversionStatus", NULL, (DWORD)bi.hr[1]);
    drv_log2(L"bitlocker wmi details: HRESULT of GetKeyProtectors", NULL, (DWORD)bi.hr[2]);
    drv_log2(L"bitlocker wmi details: HRESULT of GetSuspendCount", NULL, (DWORD)bi.hr[3]);
    suspend = drv_bde_suspend_now(ps, bi.conv, bi.nprot, bde_flag());
    partly = ps == BDE_PS_OFF;                                                 // off by WMI, yet partly encrypted with protectors (drv_bde_suspend): suspend anyway
    drv_log(suspend ? (partly ? L"bitlocker decision: suspend (protection reads off, but the volume is mid-conversion with key protectors, or we suspended it earlier)"
                              : L"bitlocker decision: suspend")
                    : L"bitlocker decision: leave alone (off, fully encrypted and suspended by the user, decrypted, or no BitLocker)");
    {   // diagnostics for the log: what BitLocker really says about the system drive (WMI "protection status" alone was once read as off on an encrypted PC)
        WCHAR f[MAX_PATH + 40], a[40];
        lstrcpyW(f, base); lstrcatW(f, L"\\bitlocker-before.txt");
        lstrcpyW(a, L"-status "); lstrcatW(a, dr); run_tool_to_file(a, f);
        lstrcpyW(a, L"-protectors -get "); lstrcatW(a, dr); run_tool_to_file(a, f);
    }
    if (suspend) {
        DWORD rc; int after; bde_info bj;
        lstrcpyW(args, L"-protectors -disable "); lstrcatW(args, dr); lstrcatW(args, L" -RebootCount 3");
        rc = run_tool(L"manage-bde.exe", args);
        if (rc == 0) set_bde_flag(TRUE);                                       // ours now, in the mid-conversion case too: bde_resume enables the protectors again (manage-bde -protectors -enable works while a conversion runs; its exit code is logged)
        after = bde_query(dr, &bj);                                            // a suspended volume reads "protection off"
        drv_log2(L"bitlocker protection status after the suspension", NULL, after);
        drv_log2(L"bitlocker conversion status after the suspension", NULL, bj.conv);
        drv_log2(L"bitlocker suspend count after the suspension", NULL, bj.suspcnt);
        // Protection was already off in the mid-conversion case, so "now off" proves nothing there: it counts as suspended when manage-bde said so (exit code 0)
        // and, where WMI has GetSuspendCount, the count did not go down. A failing manage-bde is the warning in both cases.
        if (partly && (rc != 0 || (bi.suspcnt >= 0 && bj.suspcnt >= 0 && bj.suspcnt < bi.suspcnt))) bde_warn = TRUE;
        // protection was on (or may have been) and is not demonstrably suspended: the restart menu will ask for the recovery key.
        // Everything else is armed; the wizard lets the user decide (DRV_W_BDE). "Not readable / no BitLocker" with a failing manage-bde is no warning.
        if ((ps == BDE_PS_ON || ps == BDE_PS_UNKNOWN) && (rc != 0 || after == BDE_PS_ON || after == BDE_PS_UNKNOWN)) bde_warn = TRUE;
    }
    if (run_tool(L"bcdedit.exe", L"/set {current} onetimeadvancedoptions on") != 0) { bde_resume(); return DRV_E_BCD; }
    n = drv_task_xml(xml, 3000, sid, p);
    lstrcpyW(z, base); lstrcatW(z, L"\\task.xml");
    if (n < 0 || !write_utf16_file(z, xml, n)) { cancel_locked(); return DRV_E_TASK; }
    lstrcpyW(args, L"/create /tn " TASK_NAME L" /xml \""); lstrcatW(args, z); lstrcatW(args, L"\" /f");
    n = (int)run_tool(L"schtasks.exe", args);
    DeleteFileW(z);
    if (n != 0) { cancel_locked(); return DRV_E_TASK; }
    drv_log(bde_warn ? L"prep: done, but BitLocker could not be suspended" : L"prep: done");
    return bde_warn ? DRV_W_BDE : DRV_SUCCESS;
}

static int install_locked(void)
{
    static WCHAR base[MAX_PATH], inf[MAX_PATH + 40], args[2 * MAX_PATH + 40]; int res = DRV_SUCCESS; DWORD rc;
    base_dir(base);
    // what is installed must be what prep staged: the folder is ours (only SYSTEM / Administrators can change it)
    if (!exists(base) || path_user_writable(base, NULL)) return DRV_E_DIR;
    log_in_base(base, NULL);
    // never install while this Windows start still checks driver signatures: pnputil would fail, and the cleanup below would take the armed restart
    // (task, boot option) away. Nothing is changed here; the wizard offers the restart again.
    if (driver_ci_state() == DRV_CI_ENFORCED) { drv_log(L"install: signature checking is still on in this session: not installing, the restart stays armed"); return DRV_E_ENFORCED; }
    if (!staged_ok(base)) return DRV_E_INF;
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

int driver_prep(const WCHAR *zip, const WCHAR *accept)
{
    static WCHAR sid[200], args[2 * MAX_PATH + 300];
    if (!user_sid(sid, 200)) return 1;
    if (is_admin()) return prep_locked(sid, zip, accept);
    if (accept && accept[0]) wsprintfW(args, L"/driver-prep %s \"%s\" /unverified %s", sid, zip, accept);
    else wsprintfW(args, L"/driver-prep %s \"%s\"", sid, zip);
    return run_elevated(args, NULL);
}

int driver_bitlocker(void)
{
    WCHAR dr[4]; int ps; bde_info bi;
    if (!is_admin()) return -1;                                                // WMI's BitLocker namespace refuses a process that is not elevated: do not even try (it can take many seconds)
    sys_drive(dr);
    ps = bde_query(dr, &bi);
    if (ps == BDE_PS_ERROR) return -1;                                         // not elevated: WMI refuses, nothing can be told
    return drv_bde_suspend(ps, bi.conv, bi.nprot) ? 1 : 0;                   // the same rule as prep's, so the hint on the restart page agrees with what will happen
}

int driver_install(void)
{
    if (is_admin()) return install_locked();
    return run_elevated(L"/driver-install", NULL);
}

int driver_cancel(void)
{
    static WCHAR base[MAX_PATH];
    if (is_admin()) { base_dir(base); log_in_base(base, NULL); cancel_locked(); return 0; }
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
// Called before HKLM\SOFTWARE\DynaRunFix is deleted: that key holds the flag that tells us we suspended BitLocker. The suspension is
// ended whenever the flag is set, with or without a waiting task.
void driver_machine_uninstall(void)
{
    static WCHAR base[MAX_PATH]; WCHAR p[MAX_PATH]; int i;
    for (i = 0; i < 2; i++) if (lnk_path(p, i)) DeleteFileW(p);
    base_dir(base);
    log_in_base(base, NULL);
    if (exists(base) && run_tool(L"schtasks.exe", L"/query /tn " TASK_NAME) == 0) cancel_locked();   // the task, the boot option, BitLocker
    else bde_resume();                                                         // no task any more (finished, cancelled by hand): BitLocker only
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
    if (!lstrcmpiW(v, L"/driver-prep")) return argc >= 4 ? prep_locked(argv[2], argv[3], argc >= 6 && !lstrcmpiW(argv[4], L"/unverified") && lstrlenW(argv[5]) == 64 ? argv[5] : NULL) : 1;
    if (!lstrcmpiW(v, L"/driver-install")) return install_locked();
    return driver_cancel();
}
