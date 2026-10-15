// Tests of the Dyna Pro USB driver code (src\driver.c) and of the multi-entry zip extraction (src\package.c). Host-native, with the CRT;
// not part of the shipped binaries (see tests\build-driver-tests.cmd). setup.c and driver.c are compiled into this program, so the shipped
// code is what runs. Nothing outside %TEMP%\drf-drv-<pid> (deleted at the end) is touched: no system tool is started, no HKLM write.
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

// The parts of Dyna Pro's ftdibus.inf (FTDI 2.08.24, edited by Dyna Pro) that matter, as they are in the zip: ANSI, CRLF.
static const char DYNA_INF[] =
    "; FTDIBUS.INF\r\n; \r\n; Copyright \xA9 2000-2012 Future Technology Devices International Limited\r\n;\r\n\r\n"
    "[Version]\r\nSignature=\"$Windows NT$\"\r\nDriverPackageType=PlugAndPlay\r\nDriverPackageDisplayName=%DESC%\r\nClass=USB\r\n"
    "ClassGUID={36fc9e60-c465-11cf-8056-444553540000}\r\nProvider=%FTDI%\r\nCatalogFile=ftdibus.cat\r\nDriverVer=04/10/2012,2.08.24\r\n\r\n"
    "[Manufacturer]\r\n%Ftdi%=FtdiHw,NTamd64\r\n\r\n"
    "[FtdiHw]\r\n%USB\\VID_0403&PID_FFA8.DeviceDesc%=FtdiBus.NT,USB\\VID_0403&PID_FFA8\r\n%USB\\VID_0403&PID_FDB9.DeviceDesc%=FtdiBus.NT,USB\\VID_0403&PID_FDB9\r\n"
    "%USB\\VID_0403&PID_FDBC.DeviceDesc%=FtdiBus.NT,USB\\VID_0403&PID_FDBC\r\n\r\n"
    "[FtdiHw.NTamd64]\r\n%USB\\VID_0403&PID_FDBC.DeviceDesc%=FtdiBus.NTamd64,USB\\VID_0403&PID_FDBC\r\n\r\n"
    "[FtdiBus.NT]\r\nCopyFiles=FtdiBus.NT.Copy,FtdiBus.NT.Copy2\r\nAddReg=FtdiBus.NT.AddReg\r\n\r\n"
    "[Strings]\r\nFtdi=\"BitBox\"\r\nUSB\\VID_0403&PID_FDBC.DeviceDesc=\"DynaPro Dynamometer\"\r\n";

// FTDI's own CDM 2.12.36.4 bus INF: newer version, no Dyna Pro id
static const char STOCK_INF[] =
    "[Version]\r\nSignature=\"$Windows NT$\"\r\nClass=USB\r\nProvider=%FTDI%\r\nCatalogFile=ftdibus.cat\r\nDriverVer=03/05/2021,2.12.36.4\r\n\r\n"
    "[FtdiHw]\r\n%USB\\VID_0403&PID_6001.DeviceDesc%=FtdiBus.NT,USB\\VID_0403&PID_6001\r\n%USB\\VID_0403&PID_6010&MI_00.DeviceDesc%=FtdiBus.NT,USB\\VID_0403&PID_6010&MI_00\r\n\r\n"
    "[FtdiBus.NT]\r\nCopyFiles=FtdiBus.NT.Copy\r\n";

// the FTDIPORT INF of the same package: the Dyna Pro id appears only as FTDIBUS\COMPORT&..., and it is a different driver
static const char PORT_INF[] =
    "[Version]\r\nSignature=\"$Windows NT$\"\r\nClass=Ports\r\nCatalogFile=ftdiport.cat\r\nDriverVer=04/10/2012,2.08.24\r\n\r\n"
    "[FtdiHw]\r\n%VID_0403&PID_FDB9.DeviceDesc%=FtdiPort232.NT,FTDIBUS\\COMPORT&VID_0403&PID_FDBC\r\n\r\n[FtdiPort232.NT]\r\nCopyFiles=FtdiPort232.NT.Copy\r\n";

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
    CHECK(drv_inf_matches((const BYTE *)DYNA_INF, (DWORD)strlen(DYNA_INF)), L"Dyna Pro bus INF (ANSI)");
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

    CHECK(!drv_inf_matches((const BYTE *)STOCK_INF, (DWORD)strlen(STOCK_INF)), L"stock FTDI bus INF (no Dyna Pro id, newer version)");
    CHECK(!drv_inf_matches((const BYTE *)PORT_INF, (DWORD)strlen(PORT_INF)), L"the FTDIPORT INF is not the bus INF");
    t = replace_first(DYNA_INF, "2.08.24", "2.12.28");
    CHECK(!drv_inf_matches((const BYTE *)t, (DWORD)strlen(t)), L"Dyna Pro id but another driver version"); free(t);
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
    u = to_utf16(STOCK_INF, TRUE, FALSE, &n); CHECK(!drv_inf_matches(u, n), L"stock INF, UTF-16"); free(u);
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

static const BYTE TEXT66[] = "; FTDIBUS.INF line one\r\n; line two, line two, line two, line two\r\n";     // 66 bytes
static const BYTE TEXT66_DEFLATE[] = { 0xb3,0x56,0x70,0x0b,0x71,0xf1,0x74,0x0a,0x0d,0xd6,0xf3,0xf4,0x73,0x53,0xc8,0xc9,0xcc,0x4b,0x55,0xc8,0xcf,0x4b,0xe5,0xe5,0xb2,0x86,0xb0,0x4b,0xca,0xf3,0x75,0xf0,0xb2,0x78,0xb9,0x00 };
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
    wprintf(L"[task xml]\n"); test_xml();
    wprintf(L"[tools]\n"); test_tools();
    wprintf(L"[zip names]\n"); test_zip_names();
    wprintf(L"[zip extraction]\n"); test_zip_extract();
    wprintf(L"[zip walk]\n"); test_zip_walk();
    wprintf(L"[misc]\n"); test_misc();
    wprintf(L"[real zip]\n"); test_real_zip();
    delete_tree(g_base, NULL, 0);
    wprintf(L"\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
