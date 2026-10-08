// Updates of an installed DynaRunFix (the copy of DynaRunFix-Setup.exe in Program Files\DynaRunFix):
//
//   /checkupdate     no UI. Asks GitHub for the latest release; a newer one than this build is noted in
//                    HKCU\Software\DynaRunFix (UpdateVersion = its tag, UpdateSha256 = the SHA-256 GitHub lists for
//                    its DynaRunFix-Setup.exe). The launcher runs it once a day (UpdateChecked, set only when
//                    GitHub answered; an install clears it) before it starts DynaRun, waiting up to 5 s; offline, behind a proxy that blocks it, or on Windows without TLS 1.2
//                    (XP) it just finds nothing.
//   /update <args>   the launcher found a noted update and DynaRun is not running. Asks "Update now?"; on yes
//                    downloads that release's DynaRunFix-Setup.exe from github.com/timliudev/DynaRunFix only,
//                    checks its SHA-256, runs it with "/quiet /keep" (one UAC prompt) and waits. In every case it
//                    then starts the launcher with "/noupdate <args>" (after an update without it). "Later", a cancelled UAC prompt or a
//                    failure asks again a day later (UpdateAsk).
#include <windows.h>
#include <wininet.h>
#include <wincrypt.h>
#include "setup.h"
#include "version.h"

#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)

#define API_LATEST L"https://api.github.com/repos/timliudev/DynaRunFix/releases/latest"
#define DL_PREFIX  "https://github.com/timliudev/DynaRunFix/releases/download/"
#define ASSET      "DynaRunFix-Setup.exe"
#define DAY        864000000000ULL      // FILETIME units (100 ns)

static const WCHAR KEY[] = L"Software\\DynaRunFix";

/* ---------- versions ---------- */

// "1.2.3" or "v1.2.3" (one to three numbers); anything else, such as "dev" or "1.3.0-rc1", is not a release
static BOOL parse_ver(const char *s, DWORD v[3])
{
    int i = 0;
    v[0] = v[1] = v[2] = 0;
    if (*s == 'v' || *s == 'V') s++;
    for (;;) {
        if (*s < '0' || *s > '9' || i == 3) return FALSE;
        while (*s >= '0' && *s <= '9') v[i] = v[i] * 10 + (*s++ - '0');
        i++;
        if (!*s) return TRUE;
        if (*s++ != '.') return FALSE;
    }
}

static BOOL newer(const DWORD a[3], const DWORD b[3])
{
    int i;
    for (i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] > b[i];
    return FALSE;
}

static ULONGLONG now(void)
{
    FILETIME f; ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&f);
    u.LowPart = f.dwLowDateTime; u.HighPart = f.dwHighDateTime;
    return u.QuadPart;
}

// one line in %TEMP%\dynafix.log, next to the launcher's and dynafix.dll's
static void uplog(const char *fmt, const char *a, int b)
{
    char path[MAX_PATH], line[300]; DWORD n; HANDLE h; SYSTEMTIME t;
    if (!GetEnvironmentVariableA("TEMP", path, MAX_PATH - 16)) return;
    lstrcatA(path, "\\dynafix.log");
    GetLocalTime(&t);
    n = wsprintfA(line, "%04d-%02d-%02d %02d:%02d:%02d.%03d update (v" DRF_DISPLAY "): ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    wsprintfA(line + n, fmt, a, b); lstrcatA(line, "\r\n");
    h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, line, lstrlenA(line), &n, NULL);
    CloseHandle(h);
}

static void set_time(HKEY k, const WCHAR *name, ULONGLONG t) { RegSetValueExW(k, name, 0, REG_QWORD, (const BYTE *)&t, 8); }

/* ---------- the release, from GitHub's API ---------- */

static char *http_get(const WCHAR *url, DWORD *status)
{
    HINTERNET in, h; DWORD timeout = 15000, n = sizeof(*status), size = 0, cap = 512 * 1024; char *buf = NULL;
    in = InternetOpenW(L"DynaRunFix/" WIDEN(DRF_VERSION), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    *status = 0;
    if (!in) { *status = GetLastError(); return NULL; }
    InternetSetOptionW(in, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(in, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    h = InternetOpenUrlW(in, url, L"Accept: application/vnd.github+json\r\n", (DWORD)-1,
                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_COOKIES, 0);
    if (!h) *status = GetLastError();         // WinINet error (12xxx), e.g. 12007 no name resolution, 12157 TLS
    if (h && HttpQueryInfoW(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, status, &n, NULL) && *status == 200 &&
        (buf = alloc(cap + 1))) {
        while (InternetReadFile(h, buf + size, cap - size, &n) && n && (size += n) < cap);
        if (size == 0 || size >= cap) { release(buf); buf = NULL; }   // nothing, or not the release we expect
    }
    if (h) InternetCloseHandle(h);
    InternetCloseHandle(in);
    return buf;                         // zero-terminated by alloc
}

static const char *find(const char *s, const char *sub)
{
    int n = lstrlenA(sub);
    for (; *s; s++) if (*s == *sub && CompareStringA(LOCALE_INVARIANT, 0, s, n, sub, n) == CSTR_EQUAL) return s;
    return NULL;
}

static const char *skip_ws(const char *s) { while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++; return s; }

// The first plain string value of "key" at or after p, into out. Returns where it ends, NULL when there is none
// (a value with escapes is never one of those we need). Inside other strings the quotes are escaped (\"), so
// "key" cannot match there.
static const char *json_str(const char *p, const char *key, char *out, int cch)
{
    char pat[40]; const char *v; int n;
    wsprintfA(pat, "\"%s\"", key);
    for (; (p = find(p, pat)); p++) {
        v = skip_ws(p + lstrlenA(pat));
        if (*v != ':') continue;
        v = skip_ws(v + 1);
        if (*v++ != '"') continue;
        for (n = 0; v[n] && v[n] != '"' && v[n] != '\\' && n < cch - 1; n++) out[n] = v[n];
        if (v[n] != '"') return NULL;
        out[n] = 0;
        return v + n + 1;
    }
    return NULL;
}

static BOOL is_hex64(const char *s)
{
    int i;
    for (i = 0; i < 64; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) return FALSE;
    return s[64] == 0;
}

int update_check(void)
{
    static char tag[64], name[128], dig[100], url[300], want[300];
    DWORD cur[3], v[3], st; char *j; const char *p, *d, *u; HKEY k; WCHAR w[100];
    if (!parse_ver(DRF_VERSION, cur)) return 1;         // a developer build never updates itself
    if (RegCreateKeyExW(HKEY_CURRENT_USER, KEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) return 1;
    // without an answer UpdateChecked stays as it was: the next start of DynaRun tries again
    if (!(j = http_get(API_LATEST, &st))) { uplog("%sno answer from GitHub (%d)", "", (int)st); RegCloseKey(k); return 1; }
    if (!json_str(j, "tag_name", tag, sizeof(tag)) || !parse_ver(tag, v)) uplog("%sGitHub's answer has no release tag %d", "", 0);
    else {
        set_time(k, L"UpdateChecked", now());           // answered: once a day at most
        if (!newer(v, cur)) {
            uplog("latest is %s: nothing to do %d", tag, 0);
            RegDeleteValueW(k, L"UpdateVersion"); RegDeleteValueW(k, L"UpdateSha256"); RegDeleteValueW(k, L"UpdateAsk");
        }
        else {
            // the asset named DynaRunFix-Setup.exe; its digest comes before its download URL, which must be the
            // one of this tag in this repository
            for (p = j; (p = json_str(p, "name", name, sizeof(name))) && lstrcmpA(name, ASSET); );
            wsprintfA(want, DL_PREFIX "%s/" ASSET, tag);
            if (p && (d = json_str(p, "digest", dig, sizeof(dig))) && (u = json_str(p, "browser_download_url", url, sizeof(url))) &&
                d < u && !lstrcmpA(url, want) && CompareStringA(LOCALE_INVARIANT, 0, dig, 7, "sha256:", 7) == CSTR_EQUAL && is_hex64(dig + 7)) {
                MultiByteToWideChar(CP_ACP, 0, tag, -1, w, 100);
                RegSetValueExW(k, L"UpdateVersion", 0, REG_SZ, (const BYTE *)w, (lstrlenW(w) + 1) * sizeof(WCHAR));
                MultiByteToWideChar(CP_ACP, 0, dig + 7, -1, w, 100);
                RegSetValueExW(k, L"UpdateSha256", 0, REG_SZ, (const BYTE *)w, (lstrlenW(w) + 1) * sizeof(WCHAR));
                uplog("%s is newer: noted, asked at the next start of DynaRun %d", tag, 0);
            } else uplog("%s is newer, but its " ASSET " or its SHA-256 is missing %d", tag, 0);
        }
    }
    release(j);
    RegCloseKey(k);
    return 0;
}

/* ---------- installing it ---------- */

static BOOL sha256_file(const WCHAR *p, WCHAR *hex)
{
    static BYTE buf[65536]; BYTE d[32]; DWORD n, dl = 32, i; HCRYPTPROV cp; HCRYPTHASH h; HANDLE f; BOOL ok = FALSE;
    if (!CryptAcquireContextW(&cp, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return FALSE;
    f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE && CryptCreateHash(cp, CALG_SHA_256, 0, 0, &h)) {
        while ((ok = ReadFile(f, buf, sizeof(buf), &n, NULL)) && n && (ok = CryptHashData(h, buf, n, 0)));
        ok = ok && CryptGetHashParam(h, HP_HASHVAL, d, &dl, 0) && dl == 32;
        for (i = 0; ok && i < 32; i++) wsprintfW(hex + 2 * i, L"%02x", d[i]);
        CryptDestroyHash(h);
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    CryptReleaseContext(cp, 0);
    return ok;
}

static HWND g_label;
static void say(const WCHAR *t) { MSG m; SetWindowTextW(g_label, t); while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m); }
static void dl_progress(DWORD done, DWORD total)
{
    WCHAR t[120];
    wsprintfW(t, T(L"Downloading the update... %d%%", L"正在下載更新… %d%%"), total ? MulDiv(done, 100, total) : 0);
    say(t);
}

// a small window that says what is going on (the download, then the install with its UAC prompt)
static void show_window(void)
{
    RECT wa; int w = 380, h = 110;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g_hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"STATIC", L"DynaRunFix", WS_POPUP | WS_CAPTION | WS_VISIBLE,
                             (wa.left + wa.right - w) / 2, (wa.top + wa.bottom - h) / 2, w, h, NULL, NULL, NULL, NULL);
    g_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 0, 0, w - 8, h - 36, g_hwnd, NULL, NULL, NULL);
    SendMessageW(g_label, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SetForegroundWindow(g_hwnd);
}

static int run_wait(const WCHAR *exe, WCHAR *cmd)
{
    STARTUPINFOW si; PROCESS_INFORMATION pi; DWORD rc = 1; MSG m;
    zero(&si, sizeof(si)); si.cb = sizeof(si);
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 1;
    AllowSetForegroundWindow(pi.dwProcessId);
    while (MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)rc;
}

static int install(const WCHAR *tag, const WCHAR *sha)
{
    static WCHAR url[300], name[100], out[MAX_PATH], got[65], cmd[MAX_PATH + 40]; DWORD err; volatile LONG cancel = 0; int rc;
    wsprintfW(url, WIDEN(DL_PREFIX) L"%s/" WIDEN(ASSET), tag);
    wsprintfW(name, L"DynaRunFix-Setup-%s.exe", tag);
    show_window();
    say(T(L"Downloading the update...", L"正在下載更新…"));
    rc = pkg_fetch(url, name, L"UpdateETag", out, dl_progress, &cancel, &err);
    if (rc != PK_OK) { uplog("%sdownload failed (%d)", "", (int)err); return 100 + rc; }
    if (!sha256_file(out, got) || lstrcmpiW(got, sha)) { uplog("%sSHA-256 does not match: not installed %d", "", 0); DeleteFileW(out); return 200; }   // not the file GitHub lists
    say(T(L"Installing the update...", L"正在安裝更新…"));
    wsprintfW(cmd, L"\"%s\" /quiet /keep", out);
    rc = run_wait(out, cmd);
    uplog("%sthe new setup ended with %d", "", rc);
    DeleteFileW(out);
    return rc;
}

// updated: the new launcher, which may not know "/noupdate" (nothing is noted any more, so it does not ask);
// otherwise this version's launcher, told not to ask again
static void start_launcher(const WCHAR *args, BOOL updated)
{
    static WCHAR cmd[MAX_PATH * 4]; STARTUPINFOW si; PROCESS_INFORMATION pi;
    zero(&si, sizeof(si)); si.cb = sizeof(si);
    wsprintfW(cmd, L"\"%s\"%s %s", g_launcher, updated ? L"" : L" /noupdate", args);
    if (CreateProcessW(g_launcher, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        AllowSetForegroundWindow(pi.dwProcessId);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
}

int update_run(const WCHAR *args)
{
    static WCHAR tag[64], sha[80], txt[600]; HKEY k; DWORD n; int rc = 0; BOOL updated = FALSE;
    HANDLE mx = CreateMutexW(NULL, FALSE, L"Local\\DynaRunFix-update");
    if (GetLastError() != ERROR_ALREADY_EXISTS && !(running_programs() & RUN_DYNARUN) &&
        !RegOpenKeyExW(HKEY_CURRENT_USER, KEY, 0, KEY_ALL_ACCESS, &k)) {
        n = sizeof(tag) - 2; if (RegQueryValueExW(k, L"UpdateVersion", NULL, NULL, (BYTE *)tag, &n)) tag[0] = 0;
        n = sizeof(sha) - 2; if (RegQueryValueExW(k, L"UpdateSha256", NULL, NULL, (BYTE *)sha, &n)) sha[0] = 0;
        if (tag[0] && lstrlenW(sha) == 64) {
            wsprintfW(txt, T(L"A new version of DynaRunFix is available: %s (this is v%s).\n\n"
                             L"Update now? It takes about a minute; DynaRun opens afterwards.\n\n"
                             L"\"No\" asks again tomorrow.",
                             L"DynaRunFix 有新版本：%s（目前是 v%s）。\n\n"
                             L"現在更新嗎？大約一分鐘，更新完 DynaRun 會自動開啟。\n\n"
                             L"按「否」明天會再問一次。"), tag, WIDEN(DRF_VERSION));
            rc = msg(txt, MB_YESNO | MB_ICONQUESTION) == IDYES;
            { char a[64]; WideCharToMultiByte(CP_ACP, 0, tag, -1, a, sizeof(a), NULL, NULL); uplog("asked about %s: %d (1 = yes)", a, rc); }
            if (rc) {
                rc = install(tag, sha);
                if (!rc) { updated = TRUE; RegDeleteValueW(k, L"UpdateVersion"); RegDeleteValueW(k, L"UpdateSha256"); RegDeleteValueW(k, L"UpdateAsk"); }
            } else rc = 2;
            if (rc) set_time(k, L"UpdateAsk", now() + DAY);
            if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }
            if (rc && rc != 2) {        // 2: "No", or the UAC prompt was cancelled (the new setup said so already)
                wsprintfW(txt, T(L"The update did not finish (error %d). DynaRun opens as usual; the update is tried again tomorrow.",
                                 L"更新沒有完成（錯誤 %d）。DynaRun 會照常開啟，明天會再試一次。"), rc);
                msg(txt, MB_ICONWARNING);
            }
        }
        RegCloseKey(k);
    }
    start_launcher(args, updated);
    if (mx) CloseHandle(mx);
    return rc;
}
