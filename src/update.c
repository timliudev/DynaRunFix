// Updates of an installed DynaRunFix (the copy of DynaRunFix-Setup.exe in Program Files\DynaRunFix):
//
//   /checkupdate     no UI. Asks GitHub for the latest release; a newer one than this build is noted in
//                    HKCU\Software\DynaRunFix (UpdateVersion = its tag, UpdateSha256 = the SHA-256 GitHub lists for
//                    its DynaRunFix-Setup.exe). The launcher starts it in the background (it does not wait for it, and DynaRun
//                    never waits) at most once a day (UpdateChecked, set only when GitHub answered; an install clears it), after it
//                    has started DynaRun; offline, behind a proxy that blocks it, or on Windows without TLS 1.2 (XP) it just finds nothing.
//   /offer           what the launcher runs after it has started DynaRun (see update_offer) when a newer release is noted: asks
//                    beside the running DynaRun "Update?" with the answers yes (install once DynaRun is closed), skip this
//                    version (No: UpdateSkip) and later (Cancel or closing the window: UpdateAsk, a day). The install downloads that release's DynaRunFix-Setup.exe
//                    from github.com/timliudev/DynaRunFix only, runs it with "/quiet /keep" (one UAC prompt) and waits.
//   /update <args>   the older way, still understood (a launcher of an earlier version runs it when DynaRun is not running): the
//                    same question and install, then the launcher is started with "/noupdate <args>".
// What HKCU\Software\DynaRunFix holds (UpdateVersion, UpdateSha256) is only a note that any process of this user could have
// written: before anything is downloaded or run the release is looked up on GitHub again (verify_note), and the downloaded
// file is hashed and started through one open handle that nobody else can write to (install).
#include <windows.h>
#include <wininet.h>
#include <wincrypt.h>
#include <sddl.h>
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

// The log is shared with the launcher and dynafix.dll: one named mutex ("Local\DynaRunFix.dynafix.log", the same as theirs, open to
// every user so an elevated and a normal process share it) is held around an append; at most 2 s are waited, a line is then
// written anyway (an append is atomic by itself) - only the trim of the log needs the lock for certain.
static HANDLE log_lock(void)
{
    static HANDLE m; DWORD r;
    if (!m) {
        // explicit DACL (as in the launcher): current user, SYSTEM, Administrators - not a NULL DACL
        SECURITY_ATTRIBUTES sa; PSECURITY_DESCRIPTOR sd = NULL; HANDLE tk; DWORD buf[64], n = 0; char *sid = NULL, sddl[200]; BOOL have = FALSE;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tk)) {
            if (GetTokenInformation(tk, TokenUser, buf, sizeof(buf), &n) && ConvertSidToStringSidA(((TOKEN_USER *)buf)->User.Sid, &sid)) {
                if (lstrlenA(sid) < 120) {
                    wsprintfA(sddl, "D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;%s)", sid);
                    have = ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl, SDDL_REVISION_1, &sd, NULL);
                }
                LocalFree(sid);
            }
            CloseHandle(tk);
        }
        sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = sd; sa.bInheritHandle = FALSE;
        m = CreateMutexA(have ? &sa : NULL, FALSE, "Local\\DynaRunFix.dynafix.log");
        if (sd) LocalFree(sd);
        if (!m) m = INVALID_HANDLE_VALUE;
    }
    if (m == INVALID_HANDLE_VALUE) return NULL;
    r = WaitForSingleObject(m, 2000);
    return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED ? m : NULL;
}

// one line in %TEMP%\dynafix.log, next to the launcher's and dynafix.dll's
static void uplog(const char *fmt, const char *a, int b)
{
    char path[MAX_PATH], line[300]; DWORD n; HANDLE h, m; SYSTEMTIME t; BY_HANDLE_FILE_INFORMATION bi;
    n = GetEnvironmentVariableA("TEMP", path, MAX_PATH - 16);
    if (!n || n >= MAX_PATH - 16) return;                    // not set, or too long for the buffer (n = the size it needs)
    lstrcatA(path, "\\dynafix.log");
    GetLocalTime(&t);
    n = wsprintfA(line, "%04d-%02d-%02d %02d:%02d:%02d.%03d update (v" DRF_DISPLAY "): ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    wsprintfA(line + n, fmt, a, b); lstrcatA(line, "\r\n");
    m = log_lock();
    h = CreateFileA(path, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        // a symbolic link or junction in place of the log is not followed
        if (GetFileInformationByHandle(h, &bi) && !(bi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
            WriteFile(h, line, lstrlenA(line), &n, NULL);
        CloseHandle(h);
    }
    if (m) ReleaseMutex(m);
}

static void set_time(HKEY k, const WCHAR *name, ULONGLONG t) { RegSetValueExW(k, name, 0, REG_QWORD, (const BYTE *)&t, 8); }

/* ---------- the release, from GitHub's API ---------- */

// GET url over https (INTERNET_FLAG_SECURE; certificate errors are never ignored) and the answer's body; *status = the HTTP status,
// or a WinINet error number when there was none. WinINet is not left to follow redirects: every hop (the URL and each redirect
// target) is checked against GITHUB_HOSTS before a request is sent to it (pkg_open_following, package.c). NULL unless the
// status is 200 and the whole body (at most 512 KB) was read.
static char *http_get(const WCHAR *url, DWORD *status)
{
    HINTERNET in, h; DWORD timeout = 15000, proto, n, size = 0, cap = 512 * 1024, err; char *buf = NULL; BOOL done = FALSE, refused;
    in = InternetOpenW(L"DynaRunFix/" WIDEN(DRF_VERSION), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    *status = 0;
    if (!in) { *status = GetLastError(); return NULL; }
    InternetSetOptionW(in, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(in, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    // TLS 1.2 and later only (as in package.c); Windows 7 needs TLS 1.2 switched on this way (KB3140245). Where the option is not
    // supported (XP, Vista) the system default stays and the connection to GitHub simply fails.
    proto = 0x2800;
    if (!InternetSetOptionW(in, 184 /* INTERNET_OPTION_SECURE_PROTOCOLS */, &proto, sizeof(proto))) { proto = 0x800; InternetSetOptionW(in, 184, &proto, sizeof(proto)); }
    h = pkg_open_following(in, url, GITHUB_HOSTS, L"Accept: application/vnd.github+json\r\n", status, &err, &refused);
    if (!h) *status = refused ? ERROR_INTERNET_INVALID_URL : err;     // WinINet error (12xxx), e.g. 12007 no name resolution, 12157 TLS; refused: off GitHub or off https
    else {
        if (*status == 200 && (buf = alloc(cap + 1))) {
            for (;;) {
                if (!InternetReadFile(h, buf + size, cap - size, &n)) break;      // a broken connection is not a complete answer
                if (!n) { done = TRUE; break; }
                if ((size += n) >= cap) break;
            }
            if (!done || size == 0) { release(buf); buf = NULL; }        // incomplete, nothing, or larger than any release we expect
        }
        InternetCloseHandle(h);
    }
    InternetCloseHandle(in);
    return buf;                         // zero-terminated by alloc
}

static char *(*g_http_get)(const WCHAR *url, DWORD *status) = http_get;     // the tests replace this

static const char *skip_ws(const char *s) { while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++; return s; }

static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// The JSON string that starts at the opening quote p: its characters, with the escapes (\" \\ \/ \b \f \n \r \t \uXXXX) resolved,
// into out (cch chars with the terminator; \u above 127 becomes '?', the strings we use are ASCII). A string that does not fit
// is read to its end all the same and *cut is set (out is then an unfinished copy: not to be used). Returns where the string
// ends (after its closing quote), NULL if it is not terminated or has a bad escape.
static const char *json_tok(const char *p, char *out, int cch, BOOL *cut)
{
    int n = 0, i; unsigned u; char c;
    *cut = FALSE;
    for (p++; *p != '"'; p++) {
        if (!*p) return NULL;
        c = *p;
        if (c == '\\') {
            switch (*++p) {
            case '"': case '\\': case '/': c = *p; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u':
                for (u = 0, i = 1; i <= 4; i++) { if (hexval(p[i]) < 0) return NULL; u = u * 16 + hexval(p[i]); }
                p += 4;
                c = u && u < 128 ? (char)u : '?';
                break;
            default: return NULL;
            }
        }
        if (n < cch - 1) out[n++] = c; else *cut = TRUE;
    }
    out[n] = 0;
    return p + 1;
}

// The first string value of the member "key" at or after p, into out. Returns where the value ends, NULL when there is none (or
// the text is not JSON). A value that is not a string, or too long for out, is skipped like a member of another name. The text is read token by token from p, which must be outside any
// string (the start of the answer, or a position this returned): "key" is only matched as a member name, never inside a value.
static const char *json_str(const char *p, const char *key, char *out, int cch)
{
    char k[40], *o = out; const char *v; BOOL cut, kcut;
    for (;;) {
        while (*p && *p != '"') p++;
        if (!*p) return NULL;
        if (!(p = json_tok(p, k, sizeof(k), &kcut))) return NULL;
        v = skip_ws(p);
        if (*v != ':' || kcut || lstrcmpA(k, key)) continue;      // a value string, or a member of another name
        v = skip_ws(v + 1);
        if (*v != '"') continue;
        if (!(p = json_tok(v, o, cch, &cut))) return NULL;
        if (cut) { out[0] = 0; continue; }
        return p;
    }
}

static BOOL is_hex64(const char *s)
{
    int i;
    for (i = 0; i < 64; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) return FALSE;
    return s[64] == 0;
}

// The asset named DynaRunFix-Setup.exe of the release json j (tag already checked): its digest comes before its download URL,
// which must be the one of this tag in this repository. hex: the 64 digits of the SHA-256 GitHub lists.
static BOOL release_asset(const char *j, const char *tag, char hex[65])
{
    static char name[128], dig[100], url[300], want[300]; const char *p, *d, *u;
    for (p = j; (p = json_str(p, "name", name, sizeof(name))) && lstrcmpA(name, ASSET); );
    wsprintfA(want, DL_PREFIX "%s/" ASSET, tag);
    if (p && (d = json_str(p, "digest", dig, sizeof(dig))) && (u = json_str(p, "browser_download_url", url, sizeof(url))) &&
        d < u && !lstrcmpA(url, want) && CompareStringA(LOCALE_INVARIANT, 0, dig, 7, "sha256:", 7) == CSTR_EQUAL && is_hex64(dig + 7)) {
        lstrcpyA(hex, dig + 7);
        return TRUE;
    }
    return FALSE;
}

int update_check(void)
{
    static char tag[64], hex[65];
    DWORD cur[3], v[3], st; char *j; HKEY k; WCHAR w[100];
    if (!parse_ver(DRF_VERSION, cur)) return 1;         // a developer build never updates itself
    if (RegCreateKeyExW(HKEY_CURRENT_USER, KEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) return 1;
    // without an answer UpdateChecked stays as it was: the next start of DynaRun tries again
    if (!(j = g_http_get(API_LATEST, &st))) { uplog("%sno answer from GitHub (%d)", "", (int)st); RegCloseKey(k); return 1; }
    if (!json_str(j, "tag_name", tag, sizeof(tag)) || !parse_ver(tag, v)) uplog("%sGitHub's answer has no release tag %d", "", 0);
    else {
        set_time(k, L"UpdateChecked", now());           // answered: once a day at most
        if (!newer(v, cur)) {
            uplog("latest is %s: nothing to do %d", tag, 0);
            RegDeleteValueW(k, L"UpdateVersion"); RegDeleteValueW(k, L"UpdateSha256"); RegDeleteValueW(k, L"UpdateAsk"); RegDeleteValueW(k, L"UpdateSkip");
        }
        else {
            if (release_asset(j, tag, hex)) {
                MultiByteToWideChar(CP_ACP, 0, tag, -1, w, 100);
                RegSetValueExW(k, L"UpdateVersion", 0, REG_SZ, (const BYTE *)w, (lstrlenW(w) + 1) * sizeof(WCHAR));
                MultiByteToWideChar(CP_ACP, 0, hex, -1, w, 100);
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

static BOOL sha256_handle(HANDLE f, WCHAR *hex)         // from the start of the open file
{
    static BYTE buf[65536]; BYTE d[32]; DWORD n, dl = 32, i; HCRYPTPROV cp; HCRYPTHASH h; BOOL ok = FALSE;
    if (!CryptAcquireContextW(&cp, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return FALSE;
    SetFilePointer(f, 0, NULL, FILE_BEGIN);
    if (CryptCreateHash(cp, CALG_SHA_256, 0, 0, &h)) {
        while ((ok = ReadFile(f, buf, sizeof(buf), &n, NULL)) && n && (ok = CryptHashData(h, buf, n, 0)));
        ok = ok && CryptGetHashParam(h, HP_HASHVAL, d, &dl, 0) && dl == 32;
        for (i = 0; ok && i < 32; i++) wsprintfW(hex + 2 * i, L"%02x", d[i]);
        CryptDestroyHash(h);
    }
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

// The tag and digest come from the registry (a note any process of this user could have written): they must still be the
// latest release GitHub lists, newer than this one, with this digest for its DynaRunFix-Setup.exe.
static BOOL verify_note(const WCHAR *wtag, const WCHAR *wsha)
{
    char tag[64], hex[65], now_hex[65], rtag[64]; DWORD cur[3], v[3], st; char *j; BOOL ok = FALSE; int i;
    if (lstrlenW(wtag) >= 40 || lstrlenW(wsha) != 64) return FALSE;
    for (i = 0; wtag[i]; i++) { if (wtag[i] > 126) return FALSE; tag[i] = (char)wtag[i]; }
    tag[i] = 0;
    for (i = 0; i < 64; i++) { if (wsha[i] > 126) return FALSE; hex[i] = (char)wsha[i]; }
    hex[64] = 0;
    if (!parse_ver(DRF_VERSION, cur) || !parse_ver(tag, v) || !newer(v, cur)) return FALSE;
    if (!(j = g_http_get(API_LATEST, &st))) return FALSE;
    if (json_str(j, "tag_name", rtag, sizeof(rtag)) && !lstrcmpA(rtag, tag) && release_asset(j, tag, now_hex)) ok = !lstrcmpiA(now_hex, hex);
    release(j);
    return ok;
}

static int install(const WCHAR *tag, const WCHAR *sha)
{
    static WCHAR url[300], name[100], out[MAX_PATH], got[65], cmd[MAX_PATH + 40]; DWORD err; volatile LONG cancel = 0; int rc; HANDLE hold;
    if (!verify_note(tag, sha)) { uplog("%sthe noted release is not what GitHub lists now: not installed %d", "", 0); return 201; }
    wsprintfW(url, WIDEN(DL_PREFIX) L"%s/" WIDEN(ASSET), tag);
    wsprintfW(name, L"DynaRunFix-Setup-%s.exe", tag);
    show_window();
    say(T(L"Downloading the update...", L"正在下載更新…"));
    rc = pkg_fetch(url, name, L"UpdateETag", GITHUB_HOSTS, out, dl_progress, &cancel, &err);
    if (rc != PK_OK) { uplog("%sdownload failed (%d)", "", (int)err); return 100 + rc; }
    // %TEMP% is writable by every program of this user: the file is opened so that nobody can write to or delete it, hashed through
    // that handle, and started while the handle is still open (the file that was checked is the file that runs)
    hold = CreateFileW(out, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hold == INVALID_HANDLE_VALUE) { uplog("%scannot open the downloaded setup %d", "", 0); return 202; }
    if (!sha256_handle(hold, got) || lstrcmpiW(got, sha)) { uplog("%sSHA-256 does not match: not installed %d", "", 0); CloseHandle(hold); DeleteFileW(out); return 200; }   // not the file GitHub lists
    say(T(L"Installing the update...", L"正在安裝更新…"));
    wsprintfW(cmd, L"\"%s\" /quiet /keep", out);
    rc = run_wait(out, cmd);
    uplog("%sthe new setup ended with %d", "", rc);
    CloseHandle(hold);
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

// Called by the launcher AFTER it has started DynaRun (DynaRun never waits for this): the question is asked beside the
// running DynaRun, and an accepted update is installed once DynaRun has been closed (the installer replaces files DynaRun
// holds). Yes = install after DynaRun closes, No = skip this version (UpdateSkip, not offered again; a newer release is),
// Cancel or the window's close button (X, Esc) = ask again tomorrow (the launcher has already set UpdateAsk). The launcher is not
// started again.
int update_offer(void)
{
    static WCHAR tag[64], sha[80], txt[700]; HKEY k; DWORD n, t0; int rc = 0, ans;
    HANDLE mx = CreateMutexW(NULL, FALSE, L"Local\\DynaRunFix-update");
    if (GetLastError() != ERROR_ALREADY_EXISTS && !RegOpenKeyExW(HKEY_CURRENT_USER, KEY, 0, KEY_ALL_ACCESS, &k)) {
        n = sizeof(tag) - 2; if (RegQueryValueExW(k, L"UpdateVersion", NULL, NULL, (BYTE *)tag, &n)) tag[0] = 0;
        n = sizeof(sha) - 2; if (RegQueryValueExW(k, L"UpdateSha256", NULL, NULL, (BYTE *)sha, &n)) sha[0] = 0;
        // DynaRun is still opening: do not put a window over its start-up
        for (t0 = GetTickCount(); GetTickCount() - t0 < 10000 && (running_programs() & RUN_DYNARUN); ) Sleep(500);   // a deadline, not a count of sleeps
        if (tag[0] && lstrlenW(sha) == 64 && (running_programs() & RUN_DYNARUN)) {
            wsprintfW(txt, T(L"A new version of DynaRunFix is available: %s (this is v%s).\n\n"
                             L"Yes: install it after you close DynaRun (DynaRun keeps running now).\n"
                             L"No: skip this version (it is not offered again; a newer one is).\n"
                             L"Cancel, or closing this window: ask again tomorrow.",
                             L"DynaRunFix 有新版本：%s（目前是 v%s）。\n\n"
                             L"是：等您關閉 DynaRun 後再安裝（DynaRun 照常使用）。\n"
                             L"否：略過這個版本（不再詢問；有更新的版本才會再問）。\n"
                             L"取消，或直接關閉這個視窗：明天再問。"), tag, WIDEN(DRF_VERSION));
            ans = msg(txt, MB_YESNOCANCEL | MB_ICONQUESTION);
            { char a[64]; WideCharToMultiByte(CP_ACP, 0, tag, -1, a, sizeof(a), NULL, NULL);
              uplog("asked about %s while DynaRun runs: %d (6 = after DynaRun closes, 7 = skip, 2 = Cancel/closed: tomorrow)", a, ans); }
            // Cancel and the window's close button (X, Esc) both answer IDCANCEL: they must not skip a version for good,
            // so they mean "tomorrow" (UpdateAsk is already set by the launcher); the explicit skip is "No"
            if (ans == IDNO) {
                RegSetValueExW(k, L"UpdateSkip", 0, REG_SZ, (const BYTE *)tag, (lstrlenW(tag) + 1) * sizeof(WCHAR));
            } else if (ans == IDYES) {
                while (running_programs()) Sleep(2000);     // until DynaRun (and what the setup replaces) is closed
                Sleep(3000);
                rc = running_programs() ? 3 : install(tag, sha);
                if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }
                if (!rc) { RegDeleteValueW(k, L"UpdateVersion"); RegDeleteValueW(k, L"UpdateSha256"); RegDeleteValueW(k, L"UpdateAsk"); RegDeleteValueW(k, L"UpdateSkip"); }
                else {
                    set_time(k, L"UpdateAsk", now() + DAY);
                    if (rc != 2) {
                        wsprintfW(txt, T(L"The update did not finish (error %d). It is tried again tomorrow.",
                                         L"更新沒有完成（錯誤 %d）。明天會再試一次。"), rc);
                        msg(txt, MB_ICONWARNING);
                    }
                }
            }
        }
        RegCloseKey(k);
    }
    if (mx) CloseHandle(mx);
    return rc;
}