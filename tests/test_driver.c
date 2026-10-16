// Tests of the Dyna Pro USB driver code (src\driver.c) and of the multi-entry zip extraction (src\package.c). Host-native, with the CRT;
// not part of the shipped binaries (see tests\build-driver-tests.cmd). setup.c and driver.c are compiled into this program, so the shipped
// code is what runs. Nothing outside %TEMP%\drf-drv-<pid> (deleted at the end) is touched: no system tool is started, no HKLM write.
//
// (The pinned-file tests also use %TEMP%\DynaRunFix\drvcheck, the scratch folder of the zip scan; it is deleted again.)
//
// Optional: set DRF_DRIVER_ZIP to the real "Dyna Pro USB.zip" to check the pinned SHA-256, the extraction and the INF matcher against it.
#define _CRT_SECURE_NO_WARNINGS
#define WinMainCRTStartup setup_main_not_used_here
#include "../src/setup.c"
#include "../src/driver.c"
#include <stdio.h>

// what setup.c calls elsewhere and the tests do not need
int wizard(void) { return 0; }
int update_check(void) { return 0; }
int update_offer(void) { return 0; }
int update_run(const WCHAR *a) { (void)a; return 0; }

static int g_fail, g_pass;
#define CHECK(c, what) do { if (c) { g_pass++; } else { g_fail++; wprintf(L"FAIL line %d: %s\n", __LINE__, what); } } while (0)
#define CHECKA(c, what) do { if (c) { g_pass++; } else { g_fail++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static WCHAR g_base[MAX_PATH];

/* ---------- 1. INF matcher ---------- */

// A hand-written bus INF with the same structure as the one the matcher looks for (names and strings are invented; the section name
// [FtdiBus.NT], the hardware id USB\VID_0403&PID_FDBC and the DriverVer version 2.08.24 are what drv_inf_matches checks): ANSI, CRLF,
// mixed case, comments, an entry that does not match.
static const char DYNA_INF[] =
    "; Example bus driver INF (synthetic test fixture)\r\n; Copyright \xA9 Example Maker\r\n;\r\n\r\n"
    "[Version]\r\nSignature=\"$Windows NT$\"\r\nDriverPackageType=PlugAndPlay\r\nDriverPackageDisplayName=%DESC%\r\nClass=USB\r\n"
    "ClassGUID={36fc9e60-c465-11cf-8056-444553540000}\r\nProvider=%Maker%\r\nCatalogFile=example.cat\r\nDriverVer=01/02/2011,2.08.24\r\n\r\n"
    "[Manufacturer]\r\n%Maker%=ExampleHw,NTamd64\r\n\r\n"
    "[ExampleHw]\r\n%USB\\VID_0403&PID_FFA1.DeviceDesc%=FtdiBus.NT,USB\\VID_0403&PID_FFA1\r\n"
    "%USB\\VID_0403&PID_FDBC.DeviceDesc%=FtdiBus.NT,USB\\VID_0403&PID_FDBC\r\n\r\n"
    "[ExampleHw.NTamd64]\r\n%USB\\VID_0403&PID_FDBC.DeviceDesc%=FtdiBus.NTamd64,USB\\VID_0403&PID_FDBC\r\n\r\n"
    "[FtdiBus.NT]\r\nCopyFiles=FtdiBus.NT.Copy\r\nAddReg=FtdiBus.NT.AddReg\r\n\r\n"
    "[Strings]\r\nMaker=\"Example Maker\"\r\nUSB\\VID_0403&PID_FDBC.DeviceDesc=\"Example Dyno USB\"\r\n";

// a newer version of a similar INF without that hardware id
static const char STOCK_INF[] =
    "[Version]\r\nSignature=\"$Windows NT$\"\r\nClass=USB\r\nProvider=%Maker%\r\nCatalogFile=example.cat\r\nDriverVer=03/04/2020,2.12.0.1\r\n\r\n"
    "[ExampleHw]\r\n%USB\\VID_0403&PID_7001.DeviceDesc%=FtdiBus.NT,USB\\VID_0403&PID_7001\r\n%USB\\VID_0403&PID_7002&MI_00.DeviceDesc%=FtdiBus.NT,USB\\VID_0403&PID_7002&MI_00\r\n\r\n"
    "[FtdiBus.NT]\r\nCopyFiles=FtdiBus.NT.Copy\r\n";

// the companion port INF: the hardware id appears only as a child id of the bus, and it is a different driver
static const char PORT_INF[] =
    "[Version]\r\nSignature=\"$Windows NT$\"\r\nClass=Ports\r\nCatalogFile=exampleport.cat\r\nDriverVer=01/02/2011,2.08.24\r\n\r\n"
    "[ExampleHw]\r\n%PORT.DeviceDesc%=ExamplePort.NT,FTDIBUS\\COMPORT&VID_0403&PID_FDBC\r\n\r\n[ExamplePort.NT]\r\nCopyFiles=ExamplePort.NT.Copy\r\n";

static BYTE *to_utf16(const char *s, BOOL bom, BOOL be, DWORD *n)     // ASCII text as UTF-16 (the 0xA9 of the copyright sign stays a character)
{
    DWORD len = (DWORD)strlen(s), i, k = 0; BYTE *o = malloc(2 * len + 4);
    if (bom) { o[k++] = be ? 0xFE : 0xFF; o[k++] = be ? 0xFF : 0xFE; }
    for (i = 0; i < len; i++) { BYTE c = (BYTE)s[i]; if (be) { o[k++] = 0; o[k++] = c; } else { o[k++] = c; o[k++] = 0; } }
    *n = k;
    return o;
}

static char *swap_case(const char *s, BOOL upper)
{
    char *c = _strdup(s); size_t i;
    for (i = 0; c[i]; i++) c[i] = (char)(upper ? toupper((unsigned char)c[i]) : tolower((unsigned char)c[i]));
    return c;
}

static char *replace_first(const char *s, const char *a, const char *b)
{
    const char *p = strstr(s, a); char *o;
    if (!p) return _strdup(s);
    o = malloc(strlen(s) + strlen(b) + 1);
    memcpy(o, s, (size_t)(p - s)); strcpy(o + (p - s), b); strcat(o, p + strlen(a));
    return o;
}

static char *replace_all(const char *s, const char *a, const char *b)
{
    char *cur = _strdup(s), *nxt;
    while (strstr(cur, a) && strcmp(a, b)) { nxt = replace_first(cur, a, b); free(cur); cur = nxt; }
    return cur;
}

static void test_inf(void)
{
    DWORD n; BYTE *u; char *t;
    CHECK(drv_inf_matches((const BYTE *)DYNA_INF, (DWORD)strlen(DYNA_INF)), L"synthetic bus INF (ANSI)");
    CHECK(drv_inf_matches((const BYTE *)DYNA_INF, (DWORD)strlen(DYNA_INF) + 1), L"... with the terminating NUL counted");
    u = to_utf16(DYNA_INF, TRUE, FALSE, &n);   CHECK(drv_inf_matches(u, n), L"UTF-16LE with a byte order mark"); free(u);
    u = to_utf16(DYNA_INF, FALSE, FALSE, &n);  CHECK(drv_inf_matches(u, n), L"UTF-16LE without a byte order mark"); free(u);
    u = to_utf16(DYNA_INF, TRUE, TRUE, &n);    CHECK(drv_inf_matches(u, n), L"UTF-16BE with a byte order mark"); free(u);
    { BYTE *b = malloc(strlen(DYNA_INF) + 3); memcpy(b, "\xEF\xBB\xBF", 3); memcpy(b + 3, DYNA_INF, strlen(DYNA_INF)); CHECK(drv_inf_matches(b, (DWORD)strlen(DYNA_INF) + 3), L"UTF-8 with a byte order mark"); free(b); }
    t = swap_case(DYNA_INF, FALSE); CHECK(drv_inf_matches((const BYTE *)t, (DWORD)strlen(t)), L"all lower case"); free(t);
    t = swap_case(DYNA_INF, TRUE);  CHECK(drv_inf_matches((const BYTE *)t, (DWORD)strlen(t)), L"ALL UPPER CASE"); free(t);
    t = replace_first(DYNA_INF, "DriverVer=04/10/2012,2.08.24", "DriverVer = 04/10/2012 , 2.08.24 ; the bus driver");
    CHECK(drv_inf_matches((const BYTE *)t, (DWORD)strlen(t)), L"spaces around the DriverVer fields and a comment after them"); free(t);
    // as Windows keeps it in the driver store: oemNN.inf is a copy
    CHECK(drv_inf_matches((const BYTE *)DYNA_INF, sizeof(DYNA_INF) - 1), L"the copy in the driver store");

    CHECK(!drv_inf_matches((const BYTE *)STOCK_INF, (DWORD)strlen(STOCK_INF)), L"newer bus INF without the hardware id");
    CHECK(!drv_inf_matches((const BYTE *)PORT_INF, (DWORD)strlen(PORT_INF)), L"the port INF is not the bus INF");
    t = replace_first(DYNA_INF, "2.08.24", "2.12.28");
    CHECK(!drv_inf_matches((const BYTE *)t, (DWORD)strlen(t)), L"the hardware id but another driver version"); free(t);
    t = replace_first(DYNA_INF, "DriverVer=", ";DriverVer=");
    CHECK(!drv_inf_matches((const BYTE *)t, (DWORD)strlen(t)), L"the version only in a comment"); free(t);
    t = replace_first(DYNA_INF, "2.08.24", "2.08.240");
    CHECK(!drv_inf_matches((const BYTE *)t, (DWORD)strlen(t)), L"version 2.08.240 is not 2.08.24"); free(t);
    t = replace_all(DYNA_INF, "PID_FDBC", "PID_FDBD");
    CHECK(!drv_inf_matches((const BYTE *)t, (DWORD)strlen(t)), L"another device id"); free(t);
    t = replace_first(DYNA_INF, "[FtdiBus.NT]", "[Other.NT]");
    CHECK(!drv_inf_matches((const BYTE *)t, (DWORD)strlen(t)), L"no FtdiBus section"); free(t);
    CHECK(!drv_inf_matches((const BYTE *)"", 0), L"empty file");
    CHECK(!drv_inf_matches((const BYTE *)"\xFF\xFE", 2), L"only a byte order mark");
    CHECK(!drv_inf_matches((const BYTE *)"\0\0\0\0\0\0\0\0", 8), L"zero bytes");
    CHECK(!drv_inf_matches((const BYTE *)"MZ\x90\x00\x03\x00\x00\x00", 8), L"an exe");
    u = to_utf16(STOCK_INF, TRUE, FALSE, &n); CHECK(!drv_inf_matches(u, n), L"newer INF, UTF-16"); free(u);
}

/* ---------- 2. code integrity ---------- */

static void test_ci(void)
{
    CHECK(drv_ci_enforced(0x1), L"ENABLED: enforced");
    CHECK(drv_ci_enforced(0x1 | 0x2), L"ENABLED + TESTSIGN: enforced");
    CHECK(drv_ci_enforced(0x401), L"ENABLED + HVCI: enforced");
    CHECK(drv_ci_enforced(0x2001), L"ENABLED + HVCI IUM: enforced");
    CHECK(drv_ci_enforced(0xFFFFFFFF), L"all bits: enforced");
    CHECK(!drv_ci_enforced(0), L"no options: enforcement off");
    CHECK(!drv_ci_enforced(0x2), L"TESTSIGN only: enforcement off");
    CHECK(!drv_ci_enforced(0x80), L"DEBUGMODE only: enforcement off");
    CHECK(!drv_ci_enforced(0xFFFFFFFE), L"everything but ENABLED: enforcement off");
    // the live state of this machine can be read (no assertion on the value: it depends on the machine)
    { int s = driver_ci_state(); wprintf(L"info: code integrity on this machine: %s\n", s == DRV_CI_ENFORCED ? L"enforced" : s == DRV_CI_OFF ? L"off" : L"unknown"); CHECK(s == DRV_CI_ENFORCED || s == DRV_CI_OFF || s == DRV_CI_UNKNOWN, L"state is one of the three"); }
}

static void test_bde(void)
{
    // (protection status, conversion status: 0 decrypted, 1 encrypted, 2 encrypting, 3 decrypting, 4 / 5 paused, -1 unknown; number of key protectors)
    CHECK(drv_bde_suspend(BDE_PS_ON, 1, 2), L"protection on: suspend");
    CHECK(drv_bde_suspend(BDE_PS_ON, -1, -1), L"protection on, nothing else known: suspend");
    CHECK(!drv_bde_suspend(BDE_PS_OFF, 1, 2), L"off and fully encrypted (suspended by the user): leave alone");
    CHECK(!drv_bde_suspend(BDE_PS_OFF, 0, 0), L"off and fully decrypted: nothing to suspend");
    CHECK(!drv_bde_suspend(BDE_PS_OFF, -1, -1), L"off, nothing else known: leave alone");
    CHECK(drv_bde_suspend(BDE_PS_OFF, 2, 1), L"off but encrypting with a key protector: suspend");
    CHECK(drv_bde_suspend(BDE_PS_OFF, 4, 1), L"off but encryption paused with a key protector: suspend");
    CHECK(drv_bde_suspend(BDE_PS_OFF, 3, 1), L"off but decrypting with a key protector: suspend");
    CHECK(drv_bde_suspend(BDE_PS_OFF, 5, 2), L"off but decryption paused with key protectors: suspend");
    CHECK(!drv_bde_suspend(BDE_PS_OFF, 2, 0), L"off, encrypting, but no key protector: nothing to suspend");
    CHECK(!drv_bde_suspend(BDE_PS_OFF, 2, -1), L"off, encrypting, protector count unknown: leave alone");
    CHECK(!drv_bde_suspend(BDE_PS_NONE, 2, 1), L"no BitLocker on the volume / namespace missing: nothing to suspend");
    CHECK(drv_bde_suspend(BDE_PS_UNKNOWN, -1, -1), L"status unknown: old behaviour, try");
    CHECK(drv_bde_suspend(BDE_PS_ERROR, -1, -1), L"WMI not readable: old behaviour, try");
    CHECK(drv_bde_suspend(7, -1, -1), L"an unexpected value is not 'off'");
    // at prep time (also a retry): a volume we suspended earlier reports protection off, our own flag then still starts the count over
    CHECK(drv_bde_suspend_now(BDE_PS_ON, 1, 2, FALSE), L"prep, protection on: suspend");
    CHECK(drv_bde_suspend_now(BDE_PS_ON, 1, 2, TRUE), L"prep, protection on, flag set: suspend");
    CHECK(drv_bde_suspend_now(BDE_PS_OFF, 1, 2, TRUE), L"retry: off because we suspended it (flag set): suspend again");
    CHECK(drv_bde_suspend_now(BDE_PS_NONE, -1, -1, TRUE), L"retry, flag set, no status: suspend again");
    CHECK(!drv_bde_suspend_now(BDE_PS_OFF, 1, 2, FALSE), L"off, fully encrypted and not ours: leave alone");
    CHECK(!drv_bde_suspend_now(BDE_PS_NONE, -1, -1, FALSE), L"no BitLocker and not ours: nothing to do");
    CHECK(drv_bde_suspend_now(BDE_PS_UNKNOWN, -1, -1, FALSE), L"status unknown: try, as before");
    CHECK(drv_bde_suspend_now(BDE_PS_OFF, 2, 1, FALSE), L"off but encrypting with a key protector: suspend");
}

/* ---------- 3. task XML ---------- */

static int count_str(const WCHAR *s, const WCHAR *sub)
{
    int n = 0; size_t l = wcslen(sub);
    for (; (s = wcsstr(s, sub)) != NULL; s += l) n++;
    return n;
}

static void test_xml(void)
{
    static WCHAR x[4000]; int n;
    const WCHAR *sid = L"S-1-5-21-1111111111-2222222222-3333333333-1001", *exe = L"C:\\ProgramData\\DynaRunFix\\DynaRunFix-Setup.exe";
    n = drv_task_xml(x, 4000, sid, exe);
    CHECK(n > 500 && n == (int)wcslen(x), L"builds, returns the length");
    CHECK(wcsstr(x, L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>") == x, L"UTF-16 declaration first");
    CHECK(count_str(x, L"<UserId>S-1-5-21-1111111111-2222222222-3333333333-1001</UserId>") == 2, L"the SID in the trigger and in the principal");
    CHECK(wcsstr(x, L"<LogonTrigger>") && wcsstr(x, L"</LogonTrigger>"), L"logon trigger");
    CHECK(wcsstr(x, L"<LogonType>InteractiveToken</LogonType>"), L"interactive token");
    CHECK(wcsstr(x, L"<RunLevel>HighestAvailable</RunLevel>"), L"highest available");
    CHECK(wcsstr(x, L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"), L"starts on battery");
    CHECK(wcsstr(x, L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"), L"keeps running on battery");
    CHECK(wcsstr(x, L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>"), L"no time limit");
    CHECK(wcsstr(x, L"<Command>C:\\ProgramData\\DynaRunFix\\DynaRunFix-Setup.exe</Command>"), L"the command");
    CHECK(wcsstr(x, L"<Arguments>/driver-continue</Arguments>"), L"the argument");
    CHECK(!wcschr(x, '\n') || wcsstr(x, L"\r\n"), L"CRLF line ends");

    n = drv_task_xml(x, 4000, L"S-1-5-21-1-2-3-1001", L"C:\\Tom & Jerry's <dir>\\\"x\".exe");
    CHECK(n > 0, L"a path with XML special characters builds");
    CHECK(wcsstr(x, L"<Command>C:\\Tom &amp; Jerry&apos;s &lt;dir&gt;\\&quot;x&quot;.exe</Command>"), L"& ' < > \" are escaped");
    CHECK(!wcsstr(x, L"Tom & Jerry") && !wcsstr(x, L"<dir>"), L"nothing raw is left");
    n = drv_task_xml(x, 4000, L"S-1-5-21-1&2<3>", exe);
    CHECK(n > 0 && wcsstr(x, L"<UserId>S-1-5-21-1&amp;2&lt;3&gt;</UserId>"), L"the SID is escaped too");

    CHECK(drv_task_xml(x, 200, sid, exe) == -1, L"buffer too small");
    CHECK(drv_task_xml(x, 4000, L"", exe) == -1, L"no SID");
    CHECK(drv_task_xml(x, 4000, sid, L"") == -1, L"no path");
    CHECK(drv_task_xml(x, 4000, NULL, exe) == -1 && drv_task_xml(x, 4000, sid, NULL) == -1, L"NULL arguments");
    CHECK(drv_task_xml(x, 4000, sid, L"C:\\a\nb.exe") == -1, L"a control character in the path");
    // exactly the size: n characters + the end of string
    n = drv_task_xml(x, 4000, sid, exe);
    CHECK(drv_task_xml(x, n + 1, sid, exe) == n, L"fits exactly");
    CHECK(drv_task_xml(x, n, sid, exe) == -1, L"one short");

    // well-formed XML and the byte order mark: written like prep does, read by Windows PowerShell's XML parser
    {
        WCHAR f[MAX_PATH], c[MAX_PATH + 300]; DWORD rc = 99; STARTUPINFOW si; PROCESS_INFORMATION pi;
        n = drv_task_xml(x, 4000, sid, L"C:\\Tom & Jerry's <dir>\\a.exe");
        wsprintfW(f, L"%s\\task.xml", g_base);
        CHECK(write_utf16_file(f, x, n), L"the XML file is written");
        wsprintfW(c, L"powershell.exe -NoProfile -NonInteractive -Command \"try { $d = [xml](Get-Content -Raw -Encoding Unicode -LiteralPath '%s'); if ($d.Task.Actions.Exec.Command -eq 'C:\\Tom & Jerry''s <dir>\\a.exe' -and $d.Task.Triggers.LogonTrigger.UserId -eq '%s') { exit 0 } else { exit 3 } } catch { exit 2 }\"", f, sid);
        ZeroMemory(&si, sizeof si); si.cb = sizeof si;
        if (CreateProcessW(NULL, c, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 60000); GetExitCodeProcess(pi.hProcess, &rc); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            CHECK(rc == 0, L"the XML parses and gives back the (unescaped) command and SID");
        } else wprintf(L"info: powershell not available: XML parse check skipped\n");
        {   // the file starts with the FF FE byte order mark
            BYTE head[2] = { 0, 0 }; DWORD got = 0; HANDLE h = CreateFileW(f, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) { ReadFile(h, head, 2, &got, NULL); CloseHandle(h); }
            CHECK(got == 2 && head[0] == 0xFF && head[1] == 0xFE, L"byte order mark FF FE");
        }
        DeleteFileW(f);
    }
}

/* ---------- 4. tools ---------- */

static void test_tools(void)
{
    WCHAR o[MAX_PATH], tiny[10];
    CHECK(drv_sys_tool(L"C:\\Windows", TRUE, L"pnputil.exe", o, MAX_PATH) && !lstrcmpW(o, L"C:\\Windows\\Sysnative\\pnputil.exe"), L"WOW64: Sysnative");
    CHECK(drv_sys_tool(L"C:\\Windows", FALSE, L"bcdedit.exe", o, MAX_PATH) && !lstrcmpW(o, L"C:\\Windows\\System32\\bcdedit.exe"), L"native: System32");
    CHECK(drv_sys_tool(L"D:\\WINNT\\", TRUE, L"schtasks.exe", o, MAX_PATH) && !lstrcmpW(o, L"D:\\WINNT\\Sysnative\\schtasks.exe"), L"a trailing backslash is not doubled");
    CHECK(!drv_sys_tool(L"C:\\Windows", TRUE, L"manage-bde.exe", tiny, 10), L"does not fit");
    CHECK(!drv_sys_tool(L"", TRUE, L"manage-bde.exe", o, MAX_PATH), L"no Windows folder");

    CHECK(drv_pnputil_ok(0) && drv_pnputil_ok(3010) && drv_pnputil_ok(259), L"pnputil: 0, 3010 and 259 are success");
    CHECK(!drv_pnputil_ok(1) && !drv_pnputil_ok(5) && !drv_pnputil_ok(87) && !drv_pnputil_ok(0xE0000242) && !drv_pnputil_ok((DWORD)-1), L"pnputil: errors");

    CHECK(drv_pnputil_args(o, MAX_PATH, L"C:\\ProgramData\\DynaRunFix\\driver\\Dyna Pro USB\\ftdibus.inf") &&
          !lstrcmpW(o, L"/add-driver \"C:\\ProgramData\\DynaRunFix\\driver\\Dyna Pro USB\\ftdibus.inf\" /install"), L"pnputil arguments: the path is quoted");
    CHECK(!drv_pnputil_args(o, MAX_PATH, L"C:\\a\" /delete-driver x"), L"a quote in the path is refused");
    CHECK(!drv_pnputil_args(tiny, 10, L"C:\\ftdibus.inf") && !drv_pnputil_args(o, MAX_PATH, L""), L"too small, empty");
    CHECK(driver_supported() == (os_build() >= 10240), L"driver_supported follows the build number");
}

/* ---------- 5. zip ---------- */

static DWORD crc32_of(const BYTE *d, DWORD n)
{
    DWORD c = 0xFFFFFFFF, i, j;
    for (i = 0; i < n; i++) { c ^= d[i]; for (j = 0; j < 8; j++) c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1; }
    return ~c;
}

typedef struct {
    const char *name;
    const BYTE *plain; DWORD plain_len;          // what the file holds
    int method;                                   // 0 stored, 8 deflate (then raw/raw_len is the deflate stream), others as given
    const BYTE *raw; DWORD raw_len;
    WORD flags;
    int crc_delta;                                // makes the stored CRC wrong
} zent;

static void put16(BYTE *p, DWORD v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }
static void put32(BYTE *p, DWORD v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

// A zip file made of the entries (no compression of our own: stored, or a given deflate stream)
static void build_zip(const WCHAR *path, const zent *e, int n)
{
    BYTE *buf = malloc(1 << 20); DWORD k = 0, cdstart, offs[64], i; HANDLE h; DWORD w;
    int j;
    for (j = 0; j < n; j++) {
        DWORD nl = (DWORD)strlen(e[j].name), clen = e[j].method == 8 ? e[j].raw_len : e[j].plain_len;
        const BYTE *data = e[j].method == 8 ? e[j].raw : e[j].plain;
        offs[j] = k;
        put32(buf + k, 0x04034b50); put16(buf + k + 4, 20); put16(buf + k + 6, e[j].flags); put16(buf + k + 8, e[j].method);
        put32(buf + k + 10, 0x21212121); put32(buf + k + 14, crc32_of(e[j].plain, e[j].plain_len) + e[j].crc_delta);
        put32(buf + k + 18, clen); put32(buf + k + 22, e[j].plain_len); put16(buf + k + 26, nl); put16(buf + k + 28, 0);
        memcpy(buf + k + 30, e[j].name, nl); k += 30 + nl;
        if (clen) memcpy(buf + k, data, clen);
        k += clen;
    }
    cdstart = k;
    for (j = 0; j < n; j++) {
        DWORD nl = (DWORD)strlen(e[j].name), clen = e[j].method == 8 ? e[j].raw_len : e[j].plain_len;
        put32(buf + k, 0x02014b50); put16(buf + k + 4, 20); put16(buf + k + 6, 20); put16(buf + k + 8, e[j].flags); put16(buf + k + 10, e[j].method);
        put32(buf + k + 12, 0x21212121); put32(buf + k + 16, crc32_of(e[j].plain, e[j].plain_len) + e[j].crc_delta);
        put32(buf + k + 20, clen); put32(buf + k + 24, e[j].plain_len); put16(buf + k + 28, nl); put16(buf + k + 30, 0); put16(buf + k + 32, 0);
        put16(buf + k + 34, 0); put16(buf + k + 36, 0); put32(buf + k + 38, 0); put32(buf + k + 42, offs[j]);
        memcpy(buf + k + 46, e[j].name, nl); k += 46 + nl;
    }
    put32(buf + k, 0x06054b50); put16(buf + k + 4, 0); put16(buf + k + 6, 0); put16(buf + k + 8, n); put16(buf + k + 10, n);
    put32(buf + k + 12, k - cdstart); put32(buf + k + 16, cdstart); put16(buf + k + 20, 0); k += 22;
    (void)i;
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    WriteFile(h, buf, k, &w, NULL); CloseHandle(h); free(buf);
}

static BOOL file_is(const WCHAR *dir, const WCHAR *rel, const BYTE *d, DWORD n)
{
    WCHAR p[MAX_PATH]; DWORD got; BYTE *f; BOOL ok;
    wsprintfW(p, L"%s\\%s", dir, rel);
    f = read_file(p, &got);
    ok = f && got == n && !memcmp(f, d, n);
    release(f);
    return ok;
}

static const BYTE TEXT66[] = "; sample file line one\r\n; line two, line two, line two, line two\r\n";     // 66 bytes
static const BYTE TEXT66_DEFLATE[] = { 0xb3,0x56,0x28,0x4e,0xcc,0x2d,0xc8,0x49,0x55,0x48,0xcb,0x04,0x12,0x39,0x99,0x79,0xa9,0x0a,0xf9,0x79,0xa9,0xbc,0x5c,0xd6,0x10,0x76,0x49,0x79,0xbe,0x0e,0x5e,0x16,0x2f,0x17,0x00 };   // raw deflate of TEXT66
static const BYTE BIN[] = { 1, 2, 3, 0, 255, 254, 0, 7 };
// a deflate "stored" block: header 01, LEN, NLEN, data
static const BYTE STORED_BLOCK[] = { 0x01, 0x08, 0x00, 0xF7, 0xFF, 1, 2, 3, 0, 255, 254, 0, 7 };

static void test_zip_names(void)
{
    BOOL d;
#define OKN(s) do { CHECKA(zip_name_safe(s, (DWORD)strlen(s), &d), "safe: " s); } while (0)
#define NON(s) do { CHECKA(!zip_name_safe(s, (DWORD)strlen(s), &d), "unsafe: " s); } while (0)
    OKN("Dyna Pro USB/ftdibus.inf"); OKN("Dyna Pro USB/64-Bit Drivers/amd64/ftd2xx64.dll"); OKN("a.b/c.d"); OKN("x"); OKN(".hidden/file"); OKN("a-b_c (1)/d~1.txt");
    CHECK(zip_name_safe("dir/", 4, &d) && d, L"a folder entry says isdir");
    CHECK(zip_name_safe("a/b.txt", 7, &d) && !d, L"a file entry says not isdir");
    NON("../evil.txt"); NON("a/../../evil.txt"); NON("a/.."); NON(".."); NON("."); NON("a/./b"); NON("/etc/passwd"); NON("/abs.txt");
    NON("C:/Windows/x.dll"); NON("C:x"); NON("a:b"); NON("a\\b"); NON("..\\evil.txt"); NON("a//b"); NON("a/"  "/"); NON("/");
    NON("file.txt:stream"); NON("a/b*c"); NON("a?"); NON("a<b"); NON("a>b"); NON("a|b"); NON("a\"b");
    NON("a./b"); NON("a/b."); NON("a /b"); NON("a/..."); NON("a/.. ");
    NON("con"); NON("CON.txt"); NON("a/nul"); NON("a/NUL.inf"); NON("COM1"); NON("a/lpt9.txt"); NON("prn"); NON("AUX");
    OKN("com10"); OKN("console"); OKN("nulls"); OKN("COM0"); OKN("lpt");
    CHECK(!zip_name_safe("", 0, &d), L"empty name");
    CHECK(!zip_name_safe("a\x01" "b", 3, &d) && !zip_name_safe("a\x7f", 2, &d), L"control characters");
    { char longn[300]; memset(longn, 'a', 299); longn[299] = 0; CHECK(!zip_name_safe(longn, 299, &d), L"too long"); memset(longn, 'a', 100); CHECK(zip_name_safe(longn, 100, &d), L"100 characters are fine"); }
#undef OKN
#undef NON
}

static void test_zip_extract(void)
{
    WCHAR zip[MAX_PATH], out[MAX_PATH], evil[MAX_PATH], p[MAX_PATH]; int files = -1, rc; volatile LONG nocancel = 0; DWORD a;
    zent good[] = {
        { "Pack/", NULL, 0, 0, NULL, 0, 0, 0 },
        { "Pack/a.txt", TEXT66, 66, 0, NULL, 0, 0, 0 },
        { "Pack/sub/", NULL, 0, 0, NULL, 0, 0, 0 },
        { "Pack/sub/b.inf", TEXT66, 66, 8, TEXT66_DEFLATE, sizeof(TEXT66_DEFLATE), 2, 0 },
        { "Pack/sub/deep/er/c.bin", BIN, 8, 0, NULL, 0, 0, 0 },
        { "Pack/sub/block.bin", BIN, 8, 8, STORED_BLOCK, sizeof(STORED_BLOCK), 2, 0 },
        { "Pack/empty.txt", NULL, 0, 0, NULL, 0, 0, 0 },
        { "top.txt", TEXT66, 66, 0, NULL, 0, 0, 0 },
        { "Pack/Dyna Pro USB/ftdibus.inf", (const BYTE *)DYNA_INF, sizeof(DYNA_INF) - 1, 0, NULL, 0, 0, 0 },
    };
    wsprintfW(zip, L"%s\\good.zip", g_base); wsprintfW(out, L"%s\\out_good", g_base); CreateDirectoryW(out, NULL);
    build_zip(zip, good, 9);
    rc = zip_extract_all(zip, out, &nocancel, &files);
    CHECK(rc == PK_OK && files == 7, L"a good zip: extracted, 7 files");
    CHECK(file_is(out, L"Pack\\a.txt", TEXT66, 66), L"stored file content");
    CHECK(file_is(out, L"Pack\\sub\\b.inf", TEXT66, 66), L"deflated file content (real deflate stream)");
    CHECK(file_is(out, L"Pack\\sub\\deep\\er\\c.bin", BIN, 8), L"file in nested folders without folder entries");
    CHECK(file_is(out, L"Pack\\sub\\block.bin", BIN, 8), L"deflate stored block");
    CHECK(file_is(out, L"Pack\\empty.txt", (const BYTE *)"", 0), L"empty file");
    CHECK(file_is(out, L"top.txt", TEXT66, 66), L"file at the top");
    wsprintfW(p, L"%s\\Pack\\Dyna Pro USB\\ftdibus.inf", out);
    { DWORD n; BYTE *f = read_file(p, &n); CHECK(f && drv_inf_matches(f, n), L"the extracted INF is recognised"); release(f); }
    a = GetFileAttributesW(out); CHECK(a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY), L"destination is a folder");
    wsprintfW(p, L"%s\\Pack\\sub", out); a = GetFileAttributesW(p); CHECK(a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY), L"folder entry created a folder");

    // a second run over the same files overwrites
    files = -1; rc = zip_extract_all(zip, out, &nocancel, &files);
    CHECK(rc == PK_OK && files == 7, L"extracting again works");

    // names that would leave the destination
    {
        static const char *const bad[] = { "../evil.txt", "Pack/../../evil.txt", "/evil.txt", "C:/evil.txt", "..\\evil.txt", "Pack\\..\\..\\evil.txt", "a/../../evil.txt", "C:\\evil.txt", NULL };
        int i; WCHAR title[120];
        wsprintfW(evil, L"%s\\evil.txt", g_base);
        for (i = 0; bad[i]; i++) {
            zent z[2] = { { "ok.txt", TEXT66, 66, 0, NULL, 0, 0, 0 }, { bad[i], TEXT66, 66, 0, NULL, 0, 0, 0 } };
            WCHAR o2[MAX_PATH];
            wsprintfW(zip, L"%s\\bad%d.zip", g_base, i); wsprintfW(o2, L"%s\\out_bad%d", g_base, i); CreateDirectoryW(o2, NULL);
            build_zip(zip, z, 2);
            files = 0; rc = zip_extract_all(zip, o2, &nocancel, &files);
            wsprintfW(title, L"rejected: %S", bad[i]);
            CHECK(rc == PK_FORMAT, title);
            CHECK(!exists(evil), L"nothing was written outside the destination");
            wsprintfW(p, L"%s\\evil.txt", o2); CHECK(!exists(p), L"nor inside it");
            wsprintfW(p, L"%s\\..\\..\\evil.txt", o2); CHECK(!exists(p), L"nor two levels up");
        }
    }
    // encrypted entry, unsupported method, damaged entry
    {
        zent enc[1] = { { "x.txt", TEXT66, 66, 0, NULL, 0, 1, 0 } };
        zent meth[1] = { { "x.txt", TEXT66, 66, 12, TEXT66, 66, 0, 0 } };
        zent crc[1] = { { "x.txt", TEXT66, 66, 0, NULL, 0, 0, 1 } };
        zent crcd[1] = { { "x.txt", TEXT66, 66, 8, TEXT66_DEFLATE, sizeof(TEXT66_DEFLATE), 2, 1 } };
        zent dirfile[2] = { { "a/", NULL, 0, 0, NULL, 0, 0, 0 }, { "a", TEXT66, 66, 0, NULL, 0, 0, 0 } };
        zent clash[2] = { { "a", TEXT66, 66, 0, NULL, 0, 0, 0 }, { "a/b.txt", TEXT66, 66, 0, NULL, 0, 0, 0 } };
        const struct { const zent *z; int n; const WCHAR *what; } cases[] = {
            { enc, 1, L"an encrypted entry" }, { meth, 1, L"compression method 12" }, { crc, 1, L"a stored entry with a wrong CRC" },
            { crcd, 1, L"a deflated entry with a wrong CRC" }, { dirfile, 2, L"a file named like a folder" }, { clash, 2, L"a folder below a file" } };
        int i;
        for (i = 0; i < 6; i++) {
            WCHAR o2[MAX_PATH];
            wsprintfW(zip, L"%s\\c%d.zip", g_base, i); wsprintfW(o2, L"%s\\out_c%d", g_base, i); CreateDirectoryW(o2, NULL);
            build_zip(zip, cases[i].z, cases[i].n);
            files = 0; rc = zip_extract_all(zip, o2, &nocancel, &files);
            CHECK(rc == PK_FORMAT || rc == PK_IO, cases[i].what);
        }
    }
    // not a zip / truncated / missing / cancelled
    {
        WCHAR o2[MAX_PATH]; HANDLE h; DWORD w; BYTE junk[300]; LONG cancel = 1; BYTE *whole; DWORD wn;
        wsprintfW(o2, L"%s\\out_x", g_base); CreateDirectoryW(o2, NULL);
        memset(junk, 'x', sizeof(junk));
        wsprintfW(zip, L"%s\\junk.zip", g_base);
        h = CreateFileW(zip, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); WriteFile(h, junk, sizeof(junk), &w, NULL); CloseHandle(h);
        CHECK(zip_extract_all(zip, o2, &nocancel, &files) == PK_FORMAT, L"not a zip");
        wsprintfW(zip, L"%s\\good.zip", g_base);
        whole = read_file(zip, &wn);
        wsprintfW(zip, L"%s\\cut.zip", g_base);
        h = CreateFileW(zip, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); WriteFile(h, whole, wn - 30, &w, NULL); CloseHandle(h);
        CHECK(zip_extract_all(zip, o2, &nocancel, &files) == PK_FORMAT, L"a zip cut off at the end");
        release(whole);
        wsprintfW(zip, L"%s\\missing.zip", g_base);
        CHECK(zip_extract_all(zip, o2, &nocancel, &files) == PK_IO, L"a missing file");
        wsprintfW(zip, L"%s\\good.zip", g_base); wsprintfW(o2, L"%s\\out_cancel", g_base); CreateDirectoryW(o2, NULL);
        CHECK(zip_extract_all(zip, o2, &cancel, &files) == PK_CANCEL, L"cancelled");
    }
}

static int g_walk_n; static char g_walk_first[80];
static BOOL walk_cb(const char *name, DWORD nl, const zipent *e, void *ctx)
{
    (void)ctx;
    if (!g_walk_n++) { memcpy(g_walk_first, name, nl); g_walk_first[nl] = 0; }
    return e->data > 0;
}

static void test_zip_walk(void)
{
    WCHAR zip[MAX_PATH];
    wsprintfW(zip, L"%s\\good.zip", g_base);
    g_walk_n = 0;
    CHECK(zip_walk(zip, walk_cb, NULL) == PK_OK && g_walk_n == 9 && !strcmp(g_walk_first, "Pack/"), L"zip_walk visits the 9 entries in order");
    // ... and the existing single-entry reader still sees no .msi in it
    { zipent e; CHECK(zip_open(zip, &e) == PK_FORMAT, L"zip_open (first .msi) is unchanged: none here"); }
}

/* ---------- 6. folders, the pinned hash, the real zip ---------- */

static void test_misc(void)
{
    WCHAR d[MAX_PATH], f[MAX_PATH];
    CHECK(lstrlenW(DRIVER_ZIP_SHA256) == 64, L"the pinned SHA-256 has 64 hex digits");
    { const WCHAR *c; BOOL ok = TRUE; for (c = DRIVER_ZIP_SHA256; *c; c++) ok = ok && ((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f')); CHECK(ok, L"... in lower case"); }
    base_dir(d); CHECK(wcsstr(d, L"\\DynaRunFix") && lstrlenW(d) > 20, L"the staging folder is below ProgramData");
    // the SDDL strings are valid (they are what protects the staging folder)
    {
        PSECURITY_DESCRIPTOR sd; BOOL a = ConvertStringSecurityDescriptorToSecurityDescriptorW(SDDL_OWNED, SDDL_REVISION_1, &sd, NULL);
        CHECK(a, L"SDDL with owner parses"); if (a) LocalFree(sd);
        a = ConvertStringSecurityDescriptorToSecurityDescriptorW(SDDL_PLAIN, SDDL_REVISION_1, &sd, NULL);
        CHECK(a, L"SDDL without owner parses"); if (a) LocalFree(sd);
    }
    // a temporary folder with the protected DACL (as far as a non-administrator can create one): users have no write right
    wsprintfW(f, L"%s\\prot", g_base);
    if (make_protected(f, SDDL_PLAIN)) {
        PACL dacl = NULL; PSECURITY_DESCRIPTOR sd = NULL; DWORD i; ACCESS_ALLOWED_ACE *ace; BOOL users_write = FALSE, users_read = FALSE, inherits = FALSE; SECURITY_DESCRIPTOR_CONTROL ctl; DWORD rev;
        CHECK(GetNamedSecurityInfoW(f, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &dacl, NULL, &sd) == 0 && dacl, L"the protected folder has a DACL");
        if (dacl) {
            PSID bu; ConvertStringSidToSidW(L"S-1-5-32-545", &bu);
            for (i = 0; i < dacl->AceCount; i++) {
                GetAce(dacl, i, (void **)&ace);
                if (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && EqualSid((PSID)&ace->SidStart, bu)) { users_read = TRUE; if (ace->Mask & WRITE_MASK) users_write = TRUE; }
            }
            LocalFree(bu);
            CHECK(users_read && !users_write, L"Users: read and execute only");
            GetSecurityDescriptorControl(sd, &ctl, &rev); inherits = !(ctl & SE_DACL_PROTECTED);
            CHECK(!inherits, L"the DACL is protected (no inheritance from the parent)");
        }
        LocalFree(sd);
        delete_tree(f, NULL, 0);
    } else wprintf(L"info: could not create the protected folder here (%lu): ACL checks skipped\n", GetLastError());
}

static BOOL fake_ok(const WCHAR *p) { (void)p; return FALSE; }      // "not user-writable"
static BOOL fake_bad(const WCHAR *p) { (void)p; return TRUE; }
static BOOL fake_bad_exe(const WCHAR *p) { int n = lstrlenW(p); return n > 4 && !lstrcmpiW(p + n - 4, L".exe"); }   // only a file ending in .exe

static void mb_file(const WCHAR *dir, const WCHAR *name)
{
    WCHAR f[MAX_PATH]; HANDLE h;
    wsprintfW(f, L"%s\\%s", dir, name);
    h = CreateFileW(f, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

// make_base never adopts an old folder it cannot remove, and checks the result
static void test_make_base(void)
{
    static const WCHAR SID[] = L"S-1-5-21-1-2-3-1001";
    WCHAR b[MAX_PATH], f[MAX_PATH], g[MAX_PATH]; HANDLE h;
    BOOL (*saved)(const WCHAR *) = g_is_writable;
    g_is_writable = fake_ok;
    wsprintfW(b, L"%s\\mb_new", g_base);
    CHECK(make_base(b, FALSE, SID) && exists(b), L"no folder yet: created");
    delete_tree(b, NULL, 0);
    wsprintfW(b, L"%s\\mb_old", g_base); CreateDirectoryW(b, NULL); mb_file(b, L"stale.txt");
    wsprintfW(f, L"%s\\stale.txt", b);
    CHECK(make_base(b, FALSE, SID) && exists(b) && !exists(f), L"an old folder is replaced by an empty one");
    delete_tree(b, NULL, 0);
    wsprintfW(b, L"%s\\mb_file", g_base); mb_file(g_base, L"mb_file");
    CHECK(make_base(b, FALSE, SID) && (GetFileAttributesW(b) & FILE_ATTRIBUTE_DIRECTORY), L"a file with that name is replaced by the folder");
    delete_tree(b, NULL, 0);
    // something in the old folder is held open (no sharing): the folder cannot go, so it is not adopted
    wsprintfW(b, L"%s\\mb_held", g_base); CreateDirectoryW(b, NULL);
    wsprintfW(f, L"%s\\held.txt", b);
    h = CreateFileW(f, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(h != INVALID_HANDLE_VALUE, L"a file is held open");
    CHECK(!make_base(b, FALSE, SID), L"a folder with an open file in it: make_base fails");
    CHECK(!make_base(b, TRUE, SID), L"... also when our own copy may stay (another file is in the way)");
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    delete_tree(b, NULL, 0);
    // our own running copy stays, nothing else
    wsprintfW(b, L"%s\\mb_keep", g_base); CreateDirectoryW(b, NULL); mb_file(b, L"DynaRunFix-Setup.exe"); mb_file(b, L"other.bin");
    wsprintfW(f, L"%s\\DynaRunFix-Setup.exe", b);
    CHECK(make_base(b, TRUE, SID) && exists(f), L"keep_exe: the setup copy stays, the folder is accepted");
    wsprintfW(f, L"%s\\other.bin", b);
    CHECK(!exists(f), L"... and the other file is gone");
    delete_tree(b, NULL, 0);
    // the kept copy must be a single plain file: one with a second hard link is refused (its security would be shared)
    wsprintfW(b, L"%s\\mb_link", g_base); CreateDirectoryW(b, NULL); mb_file(b, L"DynaRunFix-Setup.exe");
    wsprintfW(f, L"%s\\DynaRunFix-Setup.exe", b);
    wsprintfW(g, L"%s\\mb_link_other.bin", g_base);
    if (CreateHardLinkW(g, f, NULL)) {
        CHECK(!make_base(b, TRUE, SID), L"keep_exe: a kept setup copy with a second hard link is refused");
        DeleteFileW(g);
    } else wprintf(L"info: hard links not available here: that case is not checked\n");
    delete_tree(b, NULL, 0);
    // the kept copy, when users could change it, is refused (the folder itself is fine)
    wsprintfW(b, L"%s\\mb_keepbad", g_base); CreateDirectoryW(b, NULL); mb_file(b, L"DynaRunFix-Setup.exe");
    g_is_writable = fake_bad_exe;
    CHECK(!make_base(b, TRUE, SID), L"keep_exe: a setup copy that users can change is refused");
    g_is_writable = fake_ok;
    delete_tree(b, NULL, 0);
    // the result is checked: a folder that users could change is refused
    g_is_writable = fake_bad;
    wsprintfW(b, L"%s\\mb_bad", g_base);
    CHECK(!make_base(b, FALSE, SID), L"a staging folder that users can change is refused");
    delete_tree(b, NULL, 0);
    g_is_writable = saved;
}

/* ---------- the pinned files (what is installed is what is checked) ---------- */

static char g_names[24][100];

// A zip with the pinned files (the first n of the table; contents: the INF for ftdibus.inf, else a short text) and the extra entries given.
static void build_driver_zip(const WCHAR *path, int n, WORD flags, const char *extra1, const char *extra2)
{
    zent e[28]; int i, k = 0;
    memset(e, 0, sizeof(e));
    for (i = 0; i < n && i < drv_file_count(); i++) {
        const WCHAR *rel; int j;
        drv_file_info(i, &rel, NULL, NULL);
        sprintf(g_names[i], "Dyna Pro USB/");
        for (j = 0; rel[j]; j++) g_names[i][13 + j] = (char)rel[j];
        g_names[i][13 + j] = 0;
        e[k].name = g_names[i];
        e[k].plain = i == 0 ? (const BYTE *)DYNA_INF : TEXT66; e[k].plain_len = i == 0 ? (DWORD)strlen(DYNA_INF) : 66;
        e[k].method = 0; e[k].flags = flags; k++;
    }
    if (extra1) { e[k].name = extra1; e[k].plain = TEXT66; e[k].plain_len = 66; k++; }
    if (extra2) { e[k].name = extra2; e[k].plain = TEXT66; e[k].plain_len = 66; k++; }
    build_zip(path, e, k);
}

static void test_files(void)
{
    WCHAR zip[MAX_PATH], dir[MAX_PATH], p[MAX_PATH], rel[MAX_PATH]; int i, differ, missing, n = drv_file_count(), miss; volatile LONG nocancel = 0; BOOL known, sane;
    const WCHAR *r, *sha; DWORD size; static WCHAR acc[DRV_MAXF][65]; static drv_check c;
    // the table
    CHECK(n == 18, L"18 files are installed (2 INFs, 2 catalogs, 7 + 7 for i386 / amd64)");
    CHECK(drv_file_find(L"ftdibus.inf") == 0, L"ftdibus.inf is file 0 (bus_inf_loose relies on it)");
    CHECK(drv_file_find(L"FTDIBUS.INF") == 0 && drv_file_find(L"I386\\FtdiBus.sys") == drv_file_find(L"i386/ftdibus.sys") && drv_file_find(L"i386/ftdibus.sys") > 0, L"names: any case, / or \\");
    CHECK(drv_file_find(L"readme.txt") < 0 && drv_file_find(L"ftd2xx.inf") < 0 && drv_file_find(L"amd64/ftd2xx.dll") < 0 && drv_file_find(L"") < 0 && drv_file_find(L"i386/ftdibus.sys.") < 0, L"files that are not installed are not in the table");
    CHECK(drv_file_find(L"i386/ftd2xx.dll") >= 0 && drv_file_find(L"amd64/ftd2xx64.dll") >= 0, L"the i386 ftd2xx.dll (SysWOW64 on x64) and amd64\\ftd2xx64.dll are in it");
    for (i = 0; i < n; i++) {
        BOOL ok = drv_file_info(i, &r, &size, &sha);
        CHECK(ok && lstrlenW(sha) == 64 && size > 0 && drv_file_find(r) == i, L"every row: name, size, 64 hex digits, found by name");
        CHECK(drv_file_match(i, size, sha) && !drv_file_match(i, size + 1, sha) && !drv_file_match(i, size, L"0000000000000000000000000000000000000000000000000000000000000000") && !drv_file_match(i, size, NULL), L"match needs size and SHA-256");
    }
    CHECK(!drv_file_info(-1, NULL, NULL, NULL) && !drv_file_info(n, NULL, NULL, NULL), L"outside the table");
    // zip entry names -> table index: after a folder boundary, any case, shortest path wins elsewhere
    CHECK(entry_index("Dyna Pro USB/ftdibus.inf", 24) == 0, L"entry: Dyna Pro USB/ftdibus.inf");
    CHECK(entry_index("Dyna Pro USB/64-Bit Drivers/ftdibus.inf", 39) == 0, L"entry: the 64-Bit Drivers copy maps to the same file");
    CHECK(entry_index("x/AMD64/FTDIBUS.SYS", 19) == drv_file_find(L"amd64/ftdibus.sys"), L"entry: re-zipped under another top folder, other case");
    CHECK(entry_index("ftdibus.inf", 11) == 0, L"entry: no folder at all");
    CHECK(entry_index("evilftdibus.inf", 15) < 0 && entry_index("Dyna Pro USB/readme.txt", 23) < 0 && entry_index("Dyna Pro USB/", 13) < 0, L"entry: a longer name, other files, a folder: no");
    // staging from a zip with garbage contents under the pinned names
    wsprintfW(zip, L"%s\\drv_all.zip", g_base); build_driver_zip(zip, 18, 0, "Dyna Pro USB/readme.txt", "../evil.txt");
    wsprintfW(dir, L"%s\\stage1", g_base); CreateDirectoryW(dir, NULL);
    CHECK(stage_files(zip, dir, &nocancel, &miss) == PK_OK && miss == 0, L"stage: all 18 names present");
    CHECK(drv_check_dir(dir, &differ, &missing) == 0 && differ == 18 && missing == 0, L"check: the INF fixture and the garbage files are none of them the pinned ones");
    wsprintfW(p, L"%s\\Dyna Pro USB\\readme.txt", dir); CHECK(!exists(p), L"stage: an extra file of the zip is not extracted");
    wsprintfW(p, L"%s\\..\\evil.txt", dir); CHECK(!exists(p), L"stage: a ../ entry is not extracted anywhere");
    wsprintfW(p, L"%s\\Dyna Pro USB\\amd64", dir); CHECK(is_plain_dir(p), L"stage: the folders are plain ones");
    CHECK(stage_files(zip, dir, &nocancel, &miss) == PK_IO, L"stage: into a folder that already has the staging folders: refused (never into what exists)");
    // missing files, encrypted entries (do not count), a damaged zip
    wsprintfW(zip, L"%s\\drv_part.zip", g_base); build_driver_zip(zip, 10, 0, NULL, NULL);
    wsprintfW(dir, L"%s\\stage2", g_base); CreateDirectoryW(dir, NULL);
    CHECK(stage_files(zip, dir, &nocancel, &miss) == PK_OK && miss == 8, L"stage: 8 files missing in a zip of 10");
    CHECK(drv_check_dir(dir, &differ, &missing) == 0 && missing == 8 && differ == 10, L"check: missing and differing files are told apart");
    wsprintfW(zip, L"%s\\drv_enc.zip", g_base); build_driver_zip(zip, 18, 1, NULL, NULL);
    wsprintfW(dir, L"%s\\stage3", g_base); CreateDirectoryW(dir, NULL);
    CHECK(stage_files(zip, dir, &nocancel, &miss) == PK_OK && miss == 18, L"stage: encrypted entries count as missing");
    wsprintfW(zip, L"%s\\drv_bad.zip", g_base); { HANDLE h = CreateFileW(zip, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); DWORD w; WriteFile(h, "not a zip", 9, &w, NULL); CloseHandle(h); }
    wsprintfW(dir, L"%s\\stage4", g_base); CreateDirectoryW(dir, NULL);
    CHECK(stage_files(zip, dir, &nocancel, &miss) == PK_FORMAT, L"stage: not a zip");
    // the scan (what the wizard side decides on): all there + the bus INF names the device id = sane; the contents are not the pinned ones = not known
    wsprintfW(zip, L"%s\\drv_all.zip", g_base);
    CHECK(zip_scan(zip, &known, &sane, NULL, 0) && !known && sane, L"scan: all files there, not the pinned ones: sane but not known");
    CHECK(!zip_known_cb(zip), L"scan: not accepted as a copy of the official package");
    wsprintfW(zip, L"%s\\drv_part.zip", g_base);
    CHECK(zip_scan(zip, &known, &sane, NULL, 0) && !known && !sane, L"scan: files missing: not sane (no 'install anyway')");
    { WCHAR rep[1500]; wsprintfW(zip, L"%s\\drv_part.zip", g_base); zip_scan(zip, &known, &sane, rep, 1500); CHECK(wcsstr(rep, L"amd64/ftd2xx64.dll: missing") && wcsstr(rep, L"i386/ftdibus.sys: ") && !wcsstr(rep, L"ftdibus.inf: missing"), L"scan: the report names the missing and the differing files"); }
    // the accepted list ("install anyway"): exactly the staged files, round trip; one changed file, or no list = refused
    wsprintfW(dir, L"%s\\tb", g_base); CreateDirectoryW(dir, NULL);
    wsprintfW(p, L"%s\\driver", dir); CreateDirectoryW(p, NULL);
    wsprintfW(zip, L"%s\\drv_all.zip", g_base);
    CHECK(stage_files(zip, p, &nocancel, &miss) == PK_OK, L"accepted: staged");
    CHECK(!staged_ok(dir), L"accepted: files that are not the pinned ones are refused without an accepted list");
    check_files(p, NULL, &c);
    CHECK(write_accepted(dir, &c) && load_accepted(dir, acc), L"accepted: the list is written and read back");
    for (i = 0; i < n; i++) CHECK(!lstrcmpiW(acc[i], c.hex[i]), L"accepted: every hash is the staged file's");
    CHECK(staged_ok(dir), L"accepted: the staged files are accepted with their list");
    file_path(rel, p, 3); { HANDLE h = CreateFileW(rel, FILE_APPEND_DATA, 0, NULL, OPEN_EXISTING, 0, NULL); DWORD w; WriteFile(h, "x", 1, &w, NULL); CloseHandle(h); }
    CHECK(!staged_ok(dir), L"accepted: one staged file changed afterwards: refused (the list is not a switch for anything else)");
    { WCHAR f[MAX_PATH]; accept_path(f, dir); DeleteFileW(f); }
    CHECK(!staged_ok(dir), L"accepted: without its list the files are refused again");
    // an already unpacked folder (the folder that holds ftdibus.inf): found by its layout, checked in place, copied by name
    { WCHAR sd[MAX_PATH], nd[MAX_PATH], q[MAX_PATH];
      wsprintfW(sd, L"%s\\stage1\\Dyna Pro USB", g_base);
      CHECK(dir_layout_ok(sd) && !dir_known_cb(sd) && dir_other_cb(sd), L"folder: all files there, not the pinned ones: offered through the warning path only");
      wsprintfW(nd, L"%s\\tc", g_base); CreateDirectoryW(nd, NULL);
      CHECK(stage_from_dir(sd, nd, &miss) == PK_OK && miss == 0, L"folder: copied into a new staging folder");
      CHECK(drv_check_dir(nd, &differ, &missing) == 0 && differ == 18, L"folder: the staged copies are what gets hashed");
      CHECK(stage_from_dir(sd, nd, &miss) == PK_IO, L"folder: never into a staging folder that already exists");
      wsprintfW(q, L"%s\\amd64\\ftser2k.sys", sd); DeleteFileW(q);
      wsprintfW(nd, L"%s\\td", g_base); CreateDirectoryW(nd, NULL);
      CHECK(stage_from_dir(sd, nd, &miss) == PK_OK && miss == 1 && !dir_other_cb(sd), L"folder: a missing file is counted; such a folder is not offered");
      wsprintfW(q, L"%s\\i386", sd); CHECK(!dir_known_cb(q) && !dir_layout_ok(q), L"folder: a folder without ftdibus.inf is not a driver folder");
    }
    delete_tree(dir, NULL, 0);
}

/* ---------- a regression guard on the wizard's job table ---------- */

// Once a comment after the call in `case JOB_DRV_PREP: rc = driver_prep(...);  // ... break;` swallowed the break, and the install ran right after
// the prep (no restart in between). Every `case JOB_...:` of job_proc in src\wizard.c must have a break outside of comments before the next one.
static void test_job_table(void)
{
    char *t, *a, *end, *c, *next; DWORD n = 0; BYTE *f = read_file(L"src\\wizard.c", &n); int labels = 0;
    if (!f) { wprintf(L"info: src\\wizard.c not found from this folder: the job table is not checked\n"); return; }
    t = (char *)f;
    a = strstr(t, "static DWORD WINAPI job_proc"); end = a ? strstr(a, "WM_JOBDONE") : NULL;
    CHECK(a && end, L"job_proc is in src\\wizard.c");
    for (c = a ? strstr(a, "case JOB_") : NULL; c && c < end; c = next) {
        char *p, *stop; BOOL has_break = FALSE;
        next = strstr(c + 5, "case JOB_"); stop = next && next < end ? next : end;
        for (p = c; p < stop && !has_break; p++) {
            if (p[0] == '/' && p[1] == '/') { while (p < stop && *p != '\n') p++; continue; }       // a comment runs to the end of its line
            if (!strncmp(p, "break;", 6)) has_break = TRUE;
        }
        CHECK(has_break, L"every case of job_proc ends with a break that is not inside a comment");
        labels++;
        if (!(next && next < end)) break;
    }
    CHECK(labels >= 8, L"all eight jobs were looked at");
    release(f);
}

static void test_real_zip(void)
{
    WCHAR zip[MAX_PATH], out[MAX_PATH], p[MAX_PATH], hex[65]; ULONGLONG size; int files = 0, rc; volatile LONG nocancel = 0; DWORD n; BYTE *f;
    if (!GetEnvironmentVariableW(L"DRF_DRIVER_ZIP", zip, MAX_PATH)) { wprintf(L"info: DRF_DRIVER_ZIP not set: the real zip is not checked\n"); return; }
    CHECK(pkg_sha256(zip, hex, &size) && !lstrcmpiW(hex, DRIVER_ZIP_SHA256), L"the real zip has the pinned SHA-256");
    wsprintfW(out, L"%s\\out_real", g_base); CreateDirectoryW(out, NULL);
    rc = zip_extract_all(zip, out, &nocancel, &files);
    CHECK(rc == PK_OK, L"the real zip extracts"); CHECK(files == 49, L"49 files (56 entries, 7 of them folders)");
    wsprintfW(p, L"%s\\Dyna Pro USB\\ftdibus.inf", out);
    f = read_file(p, &n); CHECK(f && drv_inf_matches(f, n), L"its ftdibus.inf is recognised"); release(f);
    wsprintfW(p, L"%s\\Dyna Pro USB\\ftdiport.inf", out);
    f = read_file(p, &n); CHECK(f && !drv_inf_matches(f, n), L"its ftdiport.inf is not the bus INF"); release(f);
    wsprintfW(p, L"%s\\Dyna Pro USB\\ftd2xx.inf", out);
    f = read_file(p, &n); CHECK(f && !drv_inf_matches(f, n), L"its legacy ftd2xx.inf is not the bus INF"); release(f);
    wsprintfW(p, L"%s\\Dyna Pro USB\\amd64\\ftdibus.sys", out); CHECK(exists(p), L"amd64\\ftdibus.sys is there");
    wsprintfW(p, L"%s\\Dyna Pro USB\\ftdibus.cat", out); CHECK(exists(p), L"ftdibus.cat is there");
    // the pinned files: extracted by name only, every one of them is the pinned one; extra files of the zip are ignored
    {
        WCHAR st[MAX_PATH], rep[1500]; BOOL known, sane; int miss = -1, differ = -1, missing = -1;
        CHECK(zip_scan(zip, &known, &sane, rep, 1500) && known && sane && !rep[0], L"scan: the real zip: all 18 installed files are the pinned ones");
        CHECK(zip_known_cb(zip), L"scan: the real zip is accepted as the official package");
        wsprintfW(st, L"%s\\stage_real", g_base); CreateDirectoryW(st, NULL);
        CHECK(stage_files(zip, st, &nocancel, &miss) == PK_OK && miss == 0, L"stage: the real zip, the 18 files");
        CHECK(drv_check_dir(st, &differ, &missing) == 18 && differ == 0 && missing == 0, L"stage: all 18 staged files match the table");
        { WCHAR ud[MAX_PATH]; wsprintfW(ud, L"%s\\Dyna Pro USB", st); CHECK(dir_known_cb(ud), L"folder: the unpacked official package is accepted in place"); }
        wsprintfW(p, L"%s\\Dyna Pro USB\\FT Clean", st); CHECK(!exists(p), L"stage: FT Clean and the other files that are not installed are not extracted");
        wsprintfW(p, L"%s\\Dyna Pro USB\\64-Bit Drivers", st); CHECK(!exists(p), L"stage: the 64-Bit Drivers copy is not extracted");
        { HANDLE h; DWORD w; wsprintfW(p, L"%s\\Dyna Pro USB\\amd64\\ftdibus.sys", st); h = CreateFileW(p, FILE_APPEND_DATA, 0, NULL, OPEN_EXISTING, 0, NULL); WriteFile(h, "x", 1, &w, NULL); CloseHandle(h); }
        CHECK(drv_check_dir(st, &differ, &missing) == 17 && differ == 1, L"check: one changed file is found");
    }
}

int wmain(void)
{
    g_quiet = TRUE; g_zh = FALSE;
    setvbuf(stdout, NULL, _IONBF, 0);
    GetTempPathW(MAX_PATH, g_base);
    { WCHAR lp[MAX_PATH]; DWORD n = GetLongPathNameW(g_base, lp, MAX_PATH); if (n && n < MAX_PATH) lstrcpyW(g_base, lp); }
    wsprintfW(g_base + lstrlenW(g_base), L"drf-drv-%lu", GetCurrentProcessId());
    CreateDirectoryW(g_base, NULL);
    wprintf(L"[inf]\n"); test_inf();
    wprintf(L"[code integrity]\n"); test_ci();
    wprintf(L"[bitlocker]\n"); test_bde();
    wprintf(L"[task xml]\n"); test_xml();
    wprintf(L"[tools]\n"); test_tools();
    wprintf(L"[zip names]\n"); test_zip_names();
    wprintf(L"[zip extraction]\n"); test_zip_extract();
    wprintf(L"[zip walk]\n"); test_zip_walk();
    wprintf(L"[misc]\n"); test_misc();
    wprintf(L"[staging folder]\n"); test_make_base();
    wprintf(L"[pinned files]\n"); test_files();
    wprintf(L"[job table]\n"); test_job_table();
    wprintf(L"[real zip]\n"); test_real_zip();
    delete_tree(g_base, NULL, 0);
    wprintf(L"\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
