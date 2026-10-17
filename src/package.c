// Getting the DynaRun V3 setup for the wizard: find or download Dyna Pro's password-protected
// "Dyna Pro Dynamometers.zip", decrypt (ZIP 2.0 "ZipCrypto") and inflate its Setup.msi with the
// password the user typed, read the license text from the MSI and run msiexec.
// Nothing of Dyna Pro's is shipped with DynaRunFix: the zip comes from their site or the user.
#include <windows.h>
#include <shlobj.h>
#include <wininet.h>
#include <wincrypt.h>
#include <msi.h>
#include <msiquery.h>
#include <netlistmgr.h>
#include "setup.h"
#include "version.h"
#include "miniz.h"     // inflate only (third_party/miniz, built with the MINIZ_NO_* options in build.cmd)

#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)
#define CHUNK 65536
#ifndef INTERNET_OPTION_SECURE_PROTOCOLS
#define INTERNET_OPTION_SECURE_PROTOCOLS 184
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#endif

static BOOL StrStrW_(const WCHAR *h, const WCHAR *n)
{
    int i;
    for (; *h; h++) { for (i = 0; n[i] && h[i] == n[i]; i++); if (!n[i]) return TRUE; }
    return FALSE;
}

void pkg_temp_dir(WCHAR *out)
{
    GetTempPathW(MAX_PATH - 20, out);
    lstrcatW(out, L"DynaRunFix");
    CreateDirectoryW(out, NULL);
}

/* ---------- finding a package the user already has ---------- */

// The setup is recognised by what it is, not by its file name: an MSI must pass msi_check, a zip must
// hold an .msi (that one can only be checked after the password, see the wizard). Files the wizard
// found to be something else are remembered here and not offered again.
static WCHAR g_rejected[8][MAX_PATH];
static int g_nrejected;

void pkg_reject(const WCHAR *path)
{
    lstrcpynW(g_rejected[g_nrejected % 8], path, MAX_PATH);
    g_nrejected++;
}

static BOOL rejected(const WCHAR *path)
{
    int i;
    for (i = 0; i < 8 && i < g_nrejected; i++) if (!lstrcmpiW(g_rejected[i], path)) return TRUE;
    return FALSE;
}

typedef struct { WCHAR path[MAX_PATH]; int score; FILETIME t; } candidate;

// Ranking: a checked DynaRun MSI first, then a zip named like Dyna Pro's download, then any other
// password-protected zip with an .msi inside; newest first within each. The name only orders the zips, it proves nothing.
static void scan(const WCHAR *dir, BOOL subdirs, candidate *best);
typedef struct { candidate *best; BOOL subdirs; } scan_ctx;

static BOOL scan_entry(const WCHAR *p, const WIN32_FIND_DATAW *fd, void *vc)
{
    scan_ctx *c = vc; candidate *best = c->best; const WCHAR *e; int score; zipent z;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (c->subdirs && !(fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) scan(p, FALSE, best);
        return TRUE;
    }
    if (fd->dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN)) return TRUE;  // cloud-only: do not download it just to look
    e = fd->cFileName + lstrlenW(fd->cFileName) - 4;
    if (e < fd->cFileName) return TRUE;
    if (!lstrcmpiW(e, L".msi")) score = 3;
    else if (!lstrcmpiW(e, L".zip")) score = CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, fd->cFileName, 4, L"dyna", 4) == CSTR_EQUAL ? 2 : 1;
    else return TRUE;
    if (score < best->score || (score == best->score && CompareFileTime(&fd->ftLastWriteTime, &best->t) <= 0)) return TRUE;
    if (rejected(p)) return TRUE;
    // Dyna Pro's zip is password-protected: an unencrypted zip that happens to hold some .msi
    // (another vendor's tool) is not offered unasked, only through "Other file..."
    if (score == 3 ? msi_check(p, NULL, 0) != MSI_DYNARUN : zip_open(p, &z) != PK_OK || !(z.flags & 1)) return TRUE;
    lstrcpyW(best->path, p); best->score = score; best->t = fd->ftLastWriteTime;
    return TRUE;
}

static void scan(const WCHAR *dir, BOOL subdirs, candidate *best)
{
    WCHAR p[MAX_PATH * 2]; scan_ctx c;
    if (!dir[0] || lstrlenW(dir) + 4 >= MAX_PATH) return;
    c.best = best; c.subdirs = subdirs;
    dir_walk(dir, p, MAX_PATH * 2, MAX_PATH, scan_entry, &c);
}

// The folders a package the user already has is looked for in: 0 where this setup is, 1 Downloads (also one level into it), 2 Desktop, 3 Documents.
static void search_dirs(WCHAR dirs[4][MAX_PATH])
{
    WCHAR raw[MAX_PATH], *e; HKEY k; DWORD n = sizeof(raw);
    lstrcpyW(dirs[0], g_self); for (e = dirs[0] + lstrlenW(dirs[0]); e > dirs[0] && *e != '\\'; e--); *e = 0;
    dirs[1][0] = dirs[2][0] = dirs[3][0] = 0;
    // Downloads: the user's (possibly moved) folder, else %USERPROFILE%\Downloads
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders", 0, KEY_QUERY_VALUE, &k)) {
        if (!RegQueryValueExW(k, L"{374DE290-123F-4565-9164-39C4925E467B}", NULL, NULL, (BYTE *)raw, &n)) ExpandEnvironmentStringsW(raw, dirs[1], MAX_PATH);
        RegCloseKey(k);
    }
    if (!dirs[1][0] && ExpandEnvironmentStringsW(L"%USERPROFILE%\\Downloads", dirs[1], MAX_PATH) == 0) dirs[1][0] = 0;
    SHGetSpecialFolderPathW(NULL, dirs[2], CSIDL_DESKTOPDIRECTORY, FALSE);
    SHGetSpecialFolderPathW(NULL, dirs[3], CSIDL_PERSONAL, FALSE);
}

typedef BOOL (*zip_accept_fn)(const WCHAR *path);

// Where the search says what it looks at (the caller sets it; NULL = silent): one line per folder, and per zip that is skipped for its size.
void (*g_pkg_log)(const WCHAR *line);
static void plog(const WCHAR *a, const WCHAR *b)
{
    static WCHAR l[2 * MAX_PATH + 80];
    if (!g_pkg_log || lstrlenW(a) + lstrlenW(b) + 1 >= (int)(sizeof(l) / sizeof(l[0]))) return;
    lstrcpyW(l, a); lstrcatW(l, b);
    g_pkg_log(l);
}

// Two modes. accept given: the first zip (at most maxsize bytes) that accept() approves - by its content, whatever it is called. prefix given
// instead: the newest zip whose name starts with prefix, whatever it contains (*bt: its time, in/out).
static BOOL find_zip_in(const WCHAR *dir, BOOL subdirs, DWORD maxsize, zip_accept_fn accept, const WCHAR *prefix, FILETIME *bt, WCHAR *out);
typedef struct { BOOL subdirs; DWORD maxsize; zip_accept_fn accept; const WCHAR *prefix; FILETIME *bt; WCHAR *out; BOOL found; int plen; } zipfind_ctx;

static void zip_one(zipfind_ctx *c, const WCHAR *p, const WIN32_FIND_DATAW *fd)
{
    int n;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (c->subdirs && !(fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && find_zip_in(p, FALSE, c->maxsize, c->accept, c->prefix, c->bt, c->out)) c->found = TRUE;
        return;
    }
    if (fd->dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN)) return;   // cloud-only: do not download it just to look
    n = lstrlenW(fd->cFileName);
    if (n < 4 || lstrcmpiW(fd->cFileName + n - 4, L".zip")) return;
    if (c->prefix) {
        if (n < c->plen || CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, fd->cFileName, c->plen, c->prefix, c->plen) != CSTR_EQUAL) return;
        if (c->out[0] && CompareFileTime(&fd->ftLastWriteTime, c->bt) <= 0) return;
        lstrcpyW(c->out, p); *c->bt = fd->ftLastWriteTime; c->found = TRUE;
        return;
    }
    if (fd->nFileSizeHigh || fd->nFileSizeLow > c->maxsize) { plog(L"  skipped (too big for a driver package): ", p); return; }   // the driver package is a few MB
    if (c->accept(p)) { lstrcpyW(c->out, p); c->found = TRUE; }
}

// (the walk goes on after every entry, except once a zip is found when not looking for the newest by name)
static BOOL zip_entry(const WCHAR *p, const WIN32_FIND_DATAW *fd, void *vc)
{
    zipfind_ctx *c = vc;
    zip_one(c, p, fd);
    return c->prefix || !c->found;
}

static BOOL find_zip_in(const WCHAR *dir, BOOL subdirs, DWORD maxsize, zip_accept_fn accept, const WCHAR *prefix, FILETIME *bt, WCHAR *out)
{
    WCHAR p[MAX_PATH * 2]; zipfind_ctx c;
    if (!dir[0] || lstrlenW(dir) + 4 >= MAX_PATH) { plog(L"zip search: a folder with no usable path", L""); return FALSE; }
    plog(prefix ? L"zip search by name in " : L"zip search by content in ", dir);
    c.subdirs = subdirs; c.maxsize = maxsize; c.accept = accept; c.prefix = prefix; c.bt = bt; c.out = out; c.found = FALSE; c.plen = prefix ? lstrlenW(prefix) : 0;
    if (dir_walk(dir, p, MAX_PATH * 2, MAX_PATH, zip_entry, &c) == WALK_NOOPEN) { plog(L"  the folder cannot be read or does not exist", L""); return FALSE; }
    return c.found;
}

// The first zip (at most maxsize bytes) in the folders pkg_find_local looks in (and one level into Downloads) that accept() approves.
BOOL pkg_find_zip_if(DWORD maxsize, BOOL (*accept)(const WCHAR *path), WCHAR *out)
{
    static WCHAR dirs[4][MAX_PATH]; int i;
    out[0] = 0;
    search_dirs(dirs);
    for (i = 0; i < 4; i++) if (find_zip_in(dirs[i], i == 1, maxsize, accept, NULL, NULL, out)) return TRUE;
    return FALSE;
}

// The newest zip whose name starts with prefix in those folders (the candidate when no zip has the right content).
BOOL pkg_find_zip_named(const WCHAR *prefix, WCHAR *out)
{
    static WCHAR dirs[4][MAX_PATH]; FILETIME bt; int i; BOOL found = FALSE;
    out[0] = 0; bt.dwLowDateTime = bt.dwHighDateTime = 0;
    search_dirs(dirs);
    for (i = 0; i < 4; i++) if (find_zip_in(dirs[i], i == 1, 0, NULL, prefix, &bt, out)) found = TRUE;
    return found;
}

// Depth-limited walk: accept(dir) for dir itself, then for its sub-folders down to `depth` levels (never into a link). *budget: the most folders
// still to be looked at in all (so a big Documents folder cannot make it slow).
static BOOL find_dir_in(const WCHAR *dir, int depth, int *budget, BOOL (*accept)(const WCHAR *dir), WCHAR *out);
typedef struct { int depth, *budget; BOOL (*accept)(const WCHAR *dir); WCHAR *out; BOOL found; } dirfind_ctx;

static BOOL dir_entry(const WCHAR *p, const WIN32_FIND_DATAW *fd, void *vc)
{
    dirfind_ctx *c = vc;
    if ((fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        c->found = find_dir_in(p, c->depth - 1, c->budget, c->accept, c->out);
    return !c->found && *c->budget >= 0;
}

static BOOL find_dir_in(const WCHAR *dir, int depth, int *budget, BOOL (*accept)(const WCHAR *dir), WCHAR *out)
{
    WCHAR p[MAX_PATH * 2]; dirfind_ctx c;
    if (!dir[0] || lstrlenW(dir) + 4 >= MAX_PATH || --*budget < 0) return FALSE;
    if (accept(dir)) { lstrcpyW(out, dir); return TRUE; }
    if (depth <= 0) return FALSE;
    c.depth = depth; c.budget = budget; c.accept = accept; c.out = out; c.found = FALSE;
    dir_walk(dir, p, MAX_PATH * 2, MAX_PATH, dir_entry, &c);
    return c.found;
}

// The first folder that accept() approves, in the folders pkg_find_local looks in: the setup's folder, Desktop and Documents one level down,
// Downloads two levels down (Downloads\Dyna Pro USB\64-Bit Drivers), at most 600 folders in all.
BOOL pkg_find_dirs(BOOL (*accept)(const WCHAR *dir), WCHAR *out)
{
    static WCHAR dirs[4][MAX_PATH]; int i, budget = 600;
    out[0] = 0;
    search_dirs(dirs);
    for (i = 0; i < 4; i++) {
        plog(L"folder search in ", dirs[i]);
        if (find_dir_in(dirs[i], i == 1 ? 2 : 1, &budget, accept, out)) return TRUE;
    }
    return FALSE;
}

// The same walk below one given folder (the folder the user picked).
BOOL pkg_find_dir_in(const WCHAR *root, int depth, BOOL (*accept)(const WCHAR *dir), WCHAR *out)
{
    int budget = 600;
    out[0] = 0;
    return find_dir_in(root, depth, &budget, accept, out);
}

BOOL pkg_find_local(WCHAR *out)
{
    WCHAR dirs[4][MAX_PATH]; DWORD i; candidate *best = alloc(sizeof(candidate));
    out[0] = 0;
    if (!best) return FALSE;
    search_dirs(dirs);
    zero(best, sizeof(*best));
    for (i = 0; i < 4; i++) scan(dirs[i], i == 1, best);   // also one level into Downloads (e.g. Downloads\Compressed)
    lstrcpyW(out, best->path);
    release(best);
    return out[0] != 0;
}

/* ---------- is there a network at all ---------- */

// NET_NO only when Windows says it is clearly disconnected (no adapter with a connection). "Local network only" or "no internet" is NOT NET_NO: behind a
// proxy, or at a captive portal, Windows says that although the download works, so it is just tried. NET_YES: Windows reports internet access.
int pkg_net_state(void)
{
    static const GUID clsid = { 0xDCB00C01, 0x570F, 0x4A9B, { 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B } };
    static const GUID iid = { 0xDCB00000, 0x570F, 0x4A9B, { 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B } };
    INetworkListManager *nlm = NULL; NLM_CONNECTIVITY c = NLM_CONNECTIVITY_DISCONNECTED; HRESULT hrc; int r = NET_UNKNOWN; BOOL asked = FALSE; DWORD f = 0;
    hrc = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);                       // RPC_E_CHANGED_MODE: already initialised another way, usable
    if (SUCCEEDED(hrc) || hrc == RPC_E_CHANGED_MODE) {
        if (SUCCEEDED(CoCreateInstance(&clsid, NULL, CLSCTX_ALL, &iid, (void **)&nlm))) {
            if (SUCCEEDED(nlm->lpVtbl->GetConnectivity(nlm, &c))) {
                asked = TRUE;
                if (c == NLM_CONNECTIVITY_DISCONNECTED) r = NET_NO;
                else if (c & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET)) r = NET_YES;
            }
            nlm->lpVtbl->Release(nlm);
        }
        if (SUCCEEDED(hrc)) CoUninitialize();
    }
    if (!asked && !InternetGetConnectedState(&f, 0)) r = NET_NO;                // no network list manager (Windows XP / Vista): the older, coarser check
    return r;
}

// A WinINet session for our requests: connect timeout and receive timeout in milliseconds, TLS 1.2 and later only (SP_PROT_TLS1_3_CLIENT | SP_PROT_TLS1_2_CLIENT;
// Windows 7 needs TLS 1.2 switched on this way; where the option is not supported (XP, Vista) the system default stays and a TLS 1.2-only site simply fails).
HINTERNET pkg_net_open(DWORD connect_ms, DWORD receive_ms)
{
    HINTERNET in = InternetOpenW(L"DynaRunFix-Setup/" WIDEN(DRF_VERSION), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0); DWORD proto = 0x2800;
    if (!in) return NULL;
    InternetSetOptionW(in, INTERNET_OPTION_CONNECT_TIMEOUT, &connect_ms, sizeof(connect_ms));
    InternetSetOptionW(in, INTERNET_OPTION_RECEIVE_TIMEOUT, &receive_ms, sizeof(receive_ms));
    if (!InternetSetOptionW(in, INTERNET_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto))) { proto = 0x800; InternetSetOptionW(in, INTERNET_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto)); }
    return in;
}

/* ---------- download (resumable; the server sends ETag and supports Range) ---------- */

static BOOL query_str(HINTERNET h, DWORD what, WCHAR *out, DWORD cch)
{ DWORD n = cch * sizeof(WCHAR); out[0] = 0; return HttpQueryInfoW(h, what, out, &n, NULL); }

int pkg_download(WCHAR *out, progress_fn cb, volatile LONG *cancel, DWORD *err)
{ return pkg_fetch(DYNAPRO_ZIP, L"Dyna Pro Dynamometers.zip", L"DownloadETag", DYNAPRO_HOST, out, cb, cancel, err); }

// ASCII-only, case-insensitive comparison of n characters (host names are ASCII; no locale or Unicode case folding is involved)
static BOOL ascii_ieq(const WCHAR *a, const WCHAR *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        WCHAR x = a[i], y = b[i];
        if (!x || !y) return FALSE;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return FALSE;
    }
    return TRUE;
}

// Every URL a request is sent to (the first one and each redirect target) must be https and, when host is given, exactly that host.
// A user name in the URL ("host@evil") or a port is not accepted for a pinned host.
// u must be https:// and, unless hosts is NULL, on one of the hosts: a ';'-separated list of exact host names (no port, no user name).
static BOOL url_ok(const WCHAR *u, const WCHAR *hosts)
{
    int i, n;
    for (i = 0; i < 8; i++) if (!u[i]) return FALSE;
    if (!ascii_ieq(u, L"https://", 8)) return FALSE;
    u += 8;
    if (!hosts) return TRUE;
    while (*hosts) {
        for (n = 0; hosts[n] && hosts[n] != ';'; n++);
        for (i = 0; i < n && u[i]; i++);
        if (i == n && n > 0 && ascii_ieq(u, hosts, n) && (u[n] == 0 || u[n] == '/' || u[n] == '?' || u[n] == '#')) return TRUE;
        hosts += n; if (*hosts == ';') hosts++;
    }
    return FALSE;
}

// Certificate / secure-channel failures: never retried, never bypassed (no INTERNET_FLAG_IGNORE_CERT_* or
// SECURITY_FLAG_IGNORE_* is set anywhere; NO_UI means WinINet fails instead of asking the user to continue).
static BOOL is_cert_error(DWORD e)
{
    return e == ERROR_INTERNET_SEC_CERT_DATE_INVALID || e == ERROR_INTERNET_SEC_CERT_CN_INVALID || e == ERROR_INTERNET_INVALID_CA ||
           e == ERROR_INTERNET_SEC_CERT_ERRORS || e == ERROR_INTERNET_SEC_CERT_REV_FAILED || e == ERROR_INTERNET_SEC_CERT_NO_REV ||
           e == ERROR_INTERNET_SEC_CERT_REVOKED || e == ERROR_INTERNET_SEC_INVALID_CERT || e == ERROR_INTERNET_SECURITY_CHANNEL_ERROR ||
           e == ERROR_INTERNET_CLIENT_AUTH_CERT_NEEDED;
}

#define MAX_DOWNLOAD (300UL * 1024UL * 1024UL)     // nothing this program downloads is larger (the DynaRun zip is a few MB)
#define MAX_HOPS 5
#define URLCCH 2100
static DWORD parse_len(const WCHAR *s)           // decimal number, saturating
{
    ULONGLONG v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); if (v > 0xFFFFFFFFULL) return 0xFFFFFFFF; }
    return (DWORD)v;
}

BOOL pkg_url_ok(const WCHAR *u, const WCHAR *hosts) { return url_ok(u, hosts); }   // for the tests

// The URL a redirect from `cur` to `loc` leads to, into out (cch chars): an absolute https target, or a new path on the same host.
// FALSE when it cannot be followed (http://, another scheme, a protocol-relative or relative path, too long) or is not on `host`
// (NULL = any host, https still required).
BOOL pkg_redirect_target(const WCHAR *cur, const WCHAR *loc, const WCHAR *host, WCHAR *out, int cch)
{
    int a, n;
    if (url_ok(loc, NULL)) {                                                               // absolute https target
        if (lstrlenW(loc) >= cch) return FALSE;
        lstrcpyW(out, loc);
    }
    else if (loc[0] == '/' && loc[1] != '/') {                                             // same host, new path
        if (!url_ok(cur, NULL) || lstrlenW(cur) >= cch) return FALSE;
        for (a = 8; cur[a] && cur[a] != '/' && cur[a] != '?' && cur[a] != '#'; a++);
        n = lstrlenW(loc);
        if (a + n >= cch) return FALSE;
        lstrcpynW(out, cur, a + 1); lstrcatW(out, loc);
    }
    else return FALSE;                                                                     // http://, another scheme, a relative path
    return url_ok(out, host);
}

// Opens url without letting WinINet follow redirects: each hop is checked (https, and the pinned host) before a request is sent to it.
// Returns the open request (status = its HTTP status, a 3xx without a Location included), or NULL: *refused when a URL was refused,
// else *err is the Win32 error. Also used by the update check (update.c).
static HINTERNET open_following(HINTERNET in, const WCHAR *url, const WCHAR *host, const WCHAR *hdr, DWORD *status, DWORD *err, BOOL *refused,
                                WCHAR *cur, WCHAR *loc, WCHAR *nxt)
{
    HINTERNET h; DWORD n, hop;
    *status = 0; *err = 0; *refused = FALSE;
    lstrcpynW(cur, url, URLCCH);
    for (hop = 0; hop <= MAX_HOPS; hop++) {
        h = InternetOpenUrlW(in, cur, hdr, (DWORD)-1,
                             INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI |
                             INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_AUTO_REDIRECT, 0);
        if (!h) { *err = GetLastError(); return NULL; }
        n = sizeof(*status);
        if (!HttpQueryInfoW(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, status, &n, NULL)) *status = 0;
        if (*status != 301 && *status != 302 && *status != 303 && *status != 307 && *status != 308) {
            n = URLCCH * sizeof(WCHAR); loc[0] = 0;               // what the connection really is: still https on the host
            if (!InternetQueryOptionW(h, INTERNET_OPTION_URL, loc, &n) || !url_ok(loc, host)) { InternetCloseHandle(h); *refused = TRUE; return NULL; }
            return h;
        }
        n = URLCCH * sizeof(WCHAR); loc[0] = 0;
        if (!HttpQueryInfoW(h, HTTP_QUERY_LOCATION, loc, &n, NULL) || !loc[0]) return h;   // no target: the caller reports the status
        InternetCloseHandle(h);
        if (hop == MAX_HOPS) break;
        if (!pkg_redirect_target(cur, loc, host, nxt, URLCCH)) break;
        lstrcpyW(cur, nxt);
    }
    *refused = TRUE;
    return NULL;
}

// The three URL buffers belong to the call (the update hint and a download can run at the same time); they are on the heap: no C runtime here, so no stack probe for 12 KB.
HINTERNET pkg_open_following(HINTERNET in, const WCHAR *url, const WCHAR *host, const WCHAR *hdr, DWORD *status, DWORD *err, BOOL *refused)
{
    WCHAR *u = alloc(3 * URLCCH * sizeof(WCHAR)); HINTERNET h;
    if (!u) { *status = 0; *err = ERROR_OUTOFMEMORY; *refused = FALSE; return NULL; }
    h = open_following(in, url, host, hdr, status, err, refused, u, u + URLCCH, u + 2 * URLCCH);
    release(u);
    return h;
}

static BOOL zip_nothing(const char *name, DWORD nlen, const zipent *e, void *ctx) { (void)name; (void)nlen; (void)e; (void)ctx; return TRUE; }

static BOOL is_zip_name(const WCHAR *name)
{ int n = lstrlenW(name); return n >= 4 && ascii_ieq(name + n - 4, L".zip", 4); }

// url (https only) into %TEMP%\DynaRunFix\<name>; resumes a partial download of the same file (its ETag is kept in etagval).
// host: the host(s) every request (the URL and each redirect target) must be on - one name, or several separated by ';' - NULL = any host
// (https still required).
// At most MAX_DOWNLOAD bytes. Without Content-Length the end of the data cannot be told from a dropped connection: a .zip is
// then accepted only if it is a complete archive (its central directory is there); other files are checked by the caller (update.c: SHA-256).
int pkg_fetch(const WCHAR *url, const WCHAR *name, const WCHAR *etagval, const WCHAR *host, WCHAR *out, progress_fn cb, volatile LONG *cancel, DWORD *err)
{
    static WCHAR part[MAX_PATH], tag[200], oldtag[200], hdr[400], num[32], lmod[64];
    static BYTE buf[CHUNK];
    static const DWORD backoff[5] = { 1, 2, 4, 8, 15 };       // seconds to wait after the 1st .. 5th failed attempt in a row; the 6th gives up
    HINTERNET in, h = NULL; HANDLE f; DWORD have, start, total, status, n, w, fails, first = 0, ms; BOOL refused, nolen;
    LARGE_INTEGER sz; HKEY k; DWORD tn; int rc = PK_NET;

    *err = 0;
    if (!url_ok(url, host)) return PK_URL;
    pkg_temp_dir(out); lstrcatW(out, L"\\"); lstrcatW(out, name);                     // first: out always names our own file in %TEMP%, never a path the caller held before
    if (pkg_net_state() == NET_NO) { *err = ERROR_INTERNET_DISCONNECTED; return PK_NET; }   // clearly no network at all (cable out, adapter off): the error page at once, no waiting
    lstrcpyW(part, out); lstrcatW(part, L".part");
    oldtag[0] = 0;
    if (app_key_create(APP_USER, &k)) {
        tn = sizeof(oldtag) - 2;
        if (RegQueryValueExW(k, etagval, NULL, NULL, (BYTE *)oldtag, &tn)) oldtag[0] = 0;
        RegCloseKey(k);
    }
    if (!(in = pkg_net_open(10000, 30000))) { *err = GetLastError(); return PK_NET; }   // a host that does not answer is given up after 10 s; data that stops coming after 30 s

    // Attempts follow each other with a growing pause (backoff); an attempt that received bytes starts the count over (the connection worked),
    // and the error that began the current run of failures is the one reported (a later DNS error while reconnecting is not the cause).
    for (fails = 0; !*cancel; ) {
        f = CreateFileW(part, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) { *err = GetLastError(); rc = PK_IO; break; }
        GetFileSizeEx(f, &sz);
        if (sz.QuadPart > (LONGLONG)MAX_DOWNLOAD) { SetFilePointer(f, 0, NULL, FILE_BEGIN); SetEndOfFile(f); sz.QuadPart = 0; }   // not a partial file of ours
        have = sz.LowPart;
        hdr[0] = 0;
        if (have && oldtag[0]) wsprintfW(hdr, L"Range: bytes=%lu-\r\nIf-Range: %s\r\n", have, oldtag);   // resume only the same file
        h = pkg_open_following(in, url, host, hdr[0] ? hdr : NULL, &status, err, &refused);
        if (!h) {
            CloseHandle(f);
            if (refused) { *err = 0; rc = PK_URL; break; }                  // redirected off https or off the host
            if (is_cert_error(*err)) { rc = PK_CERT; break; }
            if (*err == ERROR_INTERNET_HTTPS_TO_HTTP_ON_REDIR) { rc = PK_URL; break; }
            if (!first) first = *err;
            rc = PK_NET; goto again;
        }
        if (status == 416) {            // Range not satisfiable: the .part is already complete, or is not a part of this file; start over
            CloseHandle(f); InternetCloseHandle(h); h = NULL;
            DeleteFileW(part); oldtag[0] = 0; rc = PK_NET;
            goto again;
        }
        if (status != 200 && status != 206) { *err = status; rc = PK_HTTP; CloseHandle(f); InternetCloseHandle(h); h = NULL; break; }
        if (status == 200) { have = 0; SetFilePointer(f, 0, NULL, FILE_BEGIN); SetEndOfFile(f); }
        else SetFilePointer(f, 0, NULL, FILE_END);
        if (query_str(h, HTTP_QUERY_ETAG, tag, 200) && lstrcmpW(tag, oldtag) &&
            app_key_create(APP_USER, &k)) {
            RegSetValueExW(k, etagval, 0, REG_SZ, (const BYTE *)tag, (lstrlenW(tag) + 1) * sizeof(WCHAR));
            RegCloseKey(k); lstrcpyW(oldtag, tag);
        }
        query_str(h, HTTP_QUERY_LAST_MODIFIED, lmod, 64);
        nolen = !query_str(h, HTTP_QUERY_CONTENT_LENGTH, num, 32) || !num[0];
        total = nolen ? 0 : parse_len(num);
        if (!nolen) {
            if (total > MAX_DOWNLOAD || have > MAX_DOWNLOAD - total) {      // larger than anything this program should download
                CloseHandle(f); InternetCloseHandle(h); h = NULL; DeleteFileW(part); *err = 0; rc = PK_FORMAT; break;
            }
            total += have;
        }
        rc = PK_NET; start = have;
        for (;;) {
            if (*cancel) { rc = PK_CANCEL; break; }
            if (!InternetReadFile(h, buf, sizeof(buf), &n)) { *err = GetLastError(); break; }
            if (!n) { if (nolen || have == total) rc = PK_OK; else { rc = PK_NET; *err = ERROR_INTERNET_CONNECTION_ABORTED; } break; }   // the server ended the data early
            if (n > MAX_DOWNLOAD - have) { *err = 0; rc = PK_FORMAT; break; }   // more than the limit, whatever the server announced
            if (!WriteFile(f, buf, n, &w, NULL) || w != n) { *err = GetLastError(); rc = PK_IO; break; }
            have += n;
            if (cb) cb(have, total);
        }
        CloseHandle(f);
        InternetCloseHandle(h); h = NULL;
        if (rc == PK_FORMAT) DeleteFileW(part);
        if (rc == PK_OK && nolen && is_zip_name(name) && zip_walk(part, zip_nothing, NULL) != PK_OK) rc = PK_NET;   // cut off: try to continue
        if (rc != PK_NET) break;
        if (have > start) { fails = 0; first = 0; }                         // progress: the connection worked, start the count over
        if (!first) first = *err;
again:
        if (rc != PK_NET || *cancel || ++fails > 5) break;
        for (ms = backoff[fails - 1] * 1000; ms && !*cancel; ms -= 100) Sleep(100);   // pause, but a cancel / pause is answered at once
    }
    if (rc == PK_NET) { if (first) *err = first; if (*cancel) rc = PK_CANCEL; }   // the first error of the failed run, not the last one; stopped by the user between attempts
    InternetCloseHandle(in);
    if (rc == PK_OK && !MoveFileExW(part, out, MOVEFILE_REPLACE_EXISTING)) { *err = GetLastError(); rc = PK_IO; }
    if (rc == PK_OK && app_key_create(APP_USER, &k)) {   // what a completed download was (for the hint "the site has another file")
        WCHAR rec[320], nm[80];
        if (lstrlenW(etagval) + 5 < 80) {
            wsprintfW(rec, L"%s|%lu|%s", tag, have, lmod); lstrcpyW(nm, etagval); lstrcatW(nm, L"Done");
            RegSetValueExW(k, nm, 0, REG_SZ, (const BYTE *)rec, (lstrlenW(rec) + 1) * sizeof(WCHAR));
        }
        RegCloseKey(k);
    }
    return rc;
}

/* ---------- is there a newer DynaRun V3 (a hint on the first page; never an update, never blocking) ---------- */

// "3.26.0" -> its numbers; the count (0 when s does not start with a version: digits separated by dots).
static int ver_parts(const WCHAR *s, DWORD v[4])
{
    int n = 0;
    v[0] = v[1] = v[2] = v[3] = 0;
    while (n < 4 && *s >= '0' && *s <= '9') {
        DWORD x = 0;
        while (*s >= '0' && *s <= '9') { if (x < 100000000) x = x * 10 + (*s - '0'); s++; }
        v[n++] = x;
        if (*s == '.' && s[1] >= '0' && s[1] <= '9') s++; else break;
    }
    return n;
}

// <0, 0, >0 as a is older than, the same as, newer than b (missing parts count as 0: "3.26" = "3.26.0"). A text that is not a version compares as 0 ("no news").
int ver_compare(const WCHAR *a, const WCHAR *b)
{
    DWORD x[4], y[4]; int i;
    if (!ver_parts(a, x) || !ver_parts(b, y)) return 0;
    for (i = 0; i < 4; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

static char lc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static BOOL starts_ci(const char *p, const char *lower) { int i; for (i = 0; lower[i]; i++) if (lc(p[i]) != lower[i]) return FALSE; return TRUE; }

// The first case-insensitive occurrence of needle (lower case) in text, from `from`; NULL when there is none.
static const char *ifind(const char *text, const char *needle)
{
    int i;
    for (; *text; text++) { for (i = 0; needle[i] && lc(text[i]) == needle[i]; i++); if (!needle[i]) return text; }
    return NULL;
}

// A dotted number of at least three parts ("3.26.0") that starts at or after p within `window` characters; copied to out as a wide string.
static BOOL find_version(const char *p, int window, WCHAR *out, int cch)
{
    int i, j, parts;
    for (i = 0; i < window && p[i]; i++) {
        if (p[i] < '0' || p[i] > '9' || (i && ((p[i - 1] >= '0' && p[i - 1] <= '9') || p[i - 1] == '.'))) continue;
        j = i; parts = 0;
        for (;;) {
            if (!(p[j] >= '0' && p[j] <= '9')) break;
            while (p[j] >= '0' && p[j] <= '9') j++;
            parts++;
            if (p[j] == '.' && p[j + 1] >= '0' && p[j + 1] <= '9') j++; else break;
        }
        if (parts >= 3 && j - i < cch) { for (parts = 0; parts < j - i; parts++) out[parts] = (WCHAR)p[i + parts]; out[j - i] = 0; return TRUE; }
        i = j;
    }
    return FALSE;
}

// The DynaRun V3 version the page https://dynapro.co.uk/Software_Release.htm names. The page's download table has a row "Dyna Run V3 Software (...) 3.26.0 (Released:
// ...)" and its news text says "Version 3.26.0"; both are looked for in the page's text (tags, scripts and entities removed), the table row first. Tolerant: no version
// found (the page changed) is just FALSE. html: the page as received (ASCII / UTF-8 bytes, n of them).
BOOL pkg_page_version(const char *html, DWORD n, WCHAR *out, int cch)
{
    char *t, *o; DWORD i; BOOL sp = TRUE, found = FALSE; const char *p;
    out[0] = 0;
    if (!(t = alloc(n + 2))) return FALSE;
    for (i = 0, o = t; i < n && html[i]; ) {
        if (html[i] == '<') {                                                // a tag (a script or style block is skipped as a whole)
            const char *e;
            if (starts_ci(html + i, "<script") && (e = ifind(html + i, "</script>")) != NULL) i = (DWORD)(e - html) + 9;
            else if (starts_ci(html + i, "<style") && (e = ifind(html + i, "</style>")) != NULL) i = (DWORD)(e - html) + 8;
            else { while (i < n && html[i] && html[i] != '>') i++; if (i < n) i++; }
            if (!sp) { *o++ = ' '; sp = TRUE; }
            continue;
        }
        if (html[i] == '&') {                                                // the few entities the page uses
            if (starts_ci(html + i, "&nbsp;")) { i += 6; if (!sp) { *o++ = ' '; sp = TRUE; } continue; }
            if (starts_ci(html + i, "&amp;")) { i += 5; *o++ = '&'; sp = FALSE; continue; }
        }
        if (html[i] == ' ' || html[i] == '\t' || html[i] == '\r' || html[i] == '\n') { if (!sp) { *o++ = ' '; sp = TRUE; } i++; continue; }
        *o++ = html[i++]; sp = FALSE;
    }
    *o = 0;
    for (p = ifind(t, "dyna run v3 software"); p && !found; p = ifind(p + 20, "dyna run v3 software")) {
        const char *q = p + 20;
        while (*q == ' ') q++;
        if (lc(q[0]) == 'm' && lc(q[1]) == 'a' && lc(q[2]) == 'n' && lc(q[3]) == 'u') continue;   // "Dyna Run V3 Software Manual" is the manual
        found = find_version(q, 400, out, cch);
    }
    for (p = ifind(t, "version "); p && !found; p = ifind(p + 8, "version ")) found = find_version(p + 8, 3, out, cch);
    release(t);
    return found;
}

// The installed DynaRun V3's version (the Windows Installer's own record of its product, found by the upgrade code), FALSE when it is not installed that way.
BOOL dynarun_version(WCHAR *out, int cch)
{
    WCHAR prod[40]; DWORD n = (DWORD)cch;
    out[0] = 0;
    if (!dynarun_product(prod)) return FALSE;
    return !MsiGetProductInfoW(prod, L"VersionString", out, &n) && out[0];
}

static BOOL query_named(HINTERNET h, const WCHAR *name, WCHAR *out, DWORD cch)
{ DWORD n = cch * sizeof(WCHAR); lstrcpyW(out, name); return HttpQueryInfoW(h, HTTP_QUERY_CUSTOM, out, &n, NULL); }

// A page of the site into buf (at most cap - 1 bytes, NUL-terminated); a single try, short timeouts: this is a hint, nobody waits for it.
static BOOL fetch_text(const WCHAR *url, char *buf, DWORD cap, DWORD *len)
{
    HINTERNET in, h; DWORD status, err, got = 0, n; BOOL refused, ok = FALSE;
    *len = 0; buf[0] = 0;
    if (!(in = pkg_net_open(10000, 15000))) return FALSE;
    h = pkg_open_following(in, url, DYNAPRO_HOST, NULL, &status, &err, &refused);
    if (h) {
        if (status == 200) {
            while (got + 1 < cap && InternetReadFile(h, buf + got, cap - 1 - got, &n) && n) got += n;
            buf[got] = 0; *len = got; ok = got > 0;
        }
        InternetCloseHandle(h);
    }
    InternetCloseHandle(in);
    return ok;
}

// The zip on the site, asked for its first byte only (headers are what is wanted; the same redirect checks as a download): its ETag, length and Last-Modified,
// to be compared with what the last download of it recorded.
static BOOL probe_zip(WCHAR *etag, DWORD *len, WCHAR *mod)
{
    HINTERNET in, h; DWORD status, err, n; WCHAR range[80]; BOOL refused, ok = FALSE;
    etag[0] = mod[0] = 0; *len = 0;
    if (!(in = pkg_net_open(10000, 15000))) return FALSE;
    h = pkg_open_following(in, DYNAPRO_ZIP, DYNAPRO_HOST, L"Range: bytes=0-0\r\n", &status, &err, &refused);
    if (h) {
        if (status == 200 || status == 206) {
            query_str(h, HTTP_QUERY_ETAG, etag, 200);
            query_str(h, HTTP_QUERY_LAST_MODIFIED, mod, 64);
            if (status == 206 && query_named(h, L"Content-Range", range, 80)) { WCHAR *s = range; while (*s && *s != '/') s++; if (*s == '/') *len = parse_len(s + 1); }
            else if (status == 200 && query_str(h, HTTP_QUERY_CONTENT_LENGTH, range, 80)) *len = parse_len(range);
            ok = etag[0] || *len || mod[0];
        }
        InternetCloseHandle(h);
    }
    InternetCloseHandle(in);
    (void)n;
    return ok;
}

// What the last completed download of the DynaRun zip recorded (pkg_fetch: "etag|length|last-modified" under HKCU\Software\DynaRunFix\DownloadETagDone).
static BOOL recorded_download(WCHAR *etag, DWORD *len, WCHAR *mod)
{
    WCHAR v[320], *a, *b; HKEY k; DWORD n = sizeof(v) - 2; BOOL ok = FALSE;
    etag[0] = mod[0] = 0; *len = 0; v[0] = 0;
    if (app_key_open(APP_USER, KEY_QUERY_VALUE, &k)) {
        ok = !RegQueryValueExW(k, L"DownloadETagDone", NULL, NULL, (BYTE *)v, &n) && v[0];
        RegCloseKey(k);
    }
    if (!ok) return FALSE;
    for (a = v; *a && *a != '|'; a++);
    if (*a) { *a++ = 0; for (b = a; *b && *b != '|'; b++); if (*b) *b++ = 0; *len = parse_len(a); lstrcpynW(mod, b, 64); }
    lstrcpynW(etag, v, 200);
    return TRUE;
}

// The hint. 0: nothing to say. 1: the site names a newer version (latest, installed filled in). 2: the site does not name a version (or could not be read) but its
// zip is not the one the last download recorded: "there may be a newer one". Nothing is changed, nothing is asked; every outcome is one line in the log (log may be NULL).
int pkg_update_hint(WCHAR *installed, WCHAR *latest, int cch, void (*log)(const WCHAR *line))
{
    static char page[262144]; DWORD len; WCHAR l[300], e1[200], m1[64], e2[200], m2[64]; DWORD l1, l2; int r = HINT_NONE;
    latest[0] = 0;
    if (!dynarun_version(installed, cch)) { if (log) log(L"DynaRun update hint: no installed DynaRun version (not installed by its MSI)"); return HINT_NONE; }
    if (pkg_net_state() == NET_NO) { if (log) log(L"DynaRun update hint: no network"); return HINT_NONE; }
    if (fetch_text(DYNAPRO_PAGE, page, sizeof(page), &len) && pkg_page_version(page, len, latest, cch)) {
        r = ver_compare(latest, installed) > 0 ? HINT_NEWER : HINT_NONE;
        if (log) { wsprintfW(l, L"DynaRun update hint: installed %s, the website names %s: %s", installed, latest, r ? L"newer version available" : L"nothing newer"); log(l); }
        return r;
    }
    if (log) log(L"DynaRun update hint: the website names no version (or could not be read): comparing its zip with the last download");
    if (!probe_zip(e1, &l1, m1) || !recorded_download(e2, &l2, m2)) { if (log) log(L"DynaRun update hint: nothing to compare"); return HINT_NONE; }
    if (e1[0] && e2[0]) r = lstrcmpW(e1, e2) != 0;
    else r = (l1 && l2 && l1 != l2) || (m1[0] && m2[0] && lstrcmpW(m1, m2));
    if (log) log(r ? L"DynaRun update hint: the zip on the site differs from the one downloaded last (there may be a newer version)" : L"DynaRun update hint: the zip on the site is the one downloaded last");
    return r ? HINT_ZIPDIFF : HINT_NONE;
}




// A partial download that pkg_fetch can continue: <name>.part exists, is not empty, and the ETag of the file it belongs to is known
// (without it the whole file would be fetched again). *have: its size in bytes.
BOOL pkg_partial(const WCHAR *name, const WCHAR *etagval, DWORD *have)
{
    static WCHAR p[MAX_PATH], tag[200]; HKEY k; DWORD tn = sizeof(tag) - 2; HANDLE f; LARGE_INTEGER sz; BOOL ok = FALSE;
    pkg_temp_dir(p); lstrcatW(p, L"\\"); lstrcatW(p, name); lstrcatW(p, L".part");
    tag[0] = 0;
    if (app_key_open(APP_USER, KEY_QUERY_VALUE, &k)) {
        if (RegQueryValueExW(k, etagval, NULL, NULL, (BYTE *)tag, &tn)) tag[0] = 0;
        RegCloseKey(k);
    }
    if (!tag[0]) return FALSE;
    f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        if (GetFileSizeEx(f, &sz) && sz.QuadPart > 0 && sz.QuadPart <= (LONGLONG)MAX_DOWNLOAD) { *have = sz.LowPart; ok = TRUE; }
        CloseHandle(f);
    }
    return ok;
}

BOOL pkg_download_partial(DWORD *have) { return pkg_partial(L"Dyna Pro Dynamometers.zip", L"DownloadETag", have); }

/* ---------- zip: central directory, ZipCrypto, inflate ---------- */

static DWORD g_crctab[256];
static void crc_init(void)
{
    DWORD i, j, c;
    if (g_crctab[1]) return;
    for (i = 0; i < 256; i++) { for (c = i, j = 0; j < 8; j++) c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1; g_crctab[i] = c; }
}
#define CRC1(c, b) (g_crctab[((c) ^ (b)) & 0xFF] ^ ((c) >> 8))

typedef struct { DWORD k0, k1, k2; } zkeys;
static void zk_update(zkeys *z, BYTE c)
{
    z->k0 = CRC1(z->k0, c);
    z->k1 = (z->k1 + (z->k0 & 0xFF)) * 134775813 + 1;
    z->k2 = CRC1(z->k2, (BYTE)(z->k1 >> 24));
}
static BYTE zk_decrypt(zkeys *z, BYTE c)
{
    WORD t = (WORD)(z->k2 | 2);
    c ^= (BYTE)((t * (t ^ 1)) >> 8);
    zk_update(z, c);
    return c;
}
static void zk_init(zkeys *z, const char *pw)
{ z->k0 = 0x12345678; z->k1 = 0x23456789; z->k2 = 0x34567890; while (*pw) zk_update(z, (BYTE)*pw++); }

#define U16(p) ((DWORD)(p)[0] | (DWORD)(p)[1] << 8)
#define U32(p) (U16(p) | U16((p) + 2) << 16)

static BOOL read_at(HANDLE f, DWORD off, void *b, DWORD n)
{ DWORD r; return SetFilePointer(f, off, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER && ReadFile(f, b, n, &r, NULL) && r == n; }

int zip_open(const WCHAR *zip, zipent *e)
{
    HANDLE f = CreateFileW(zip, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, tail, i, cdoff, cdsize, adj, lho, nl, xl; BYTE *t = NULL, *cd = NULL, *p, lh[30]; int rc = PK_FORMAT;
    if (f == INVALID_HANDLE_VALUE) return PK_IO;
    crc_init();
    zero(e, sizeof(*e)); lstrcpynW(e->path, zip, MAX_PATH);
    size = GetFileSize(f, NULL);
    tail = size < 65557 ? size : 65557;
    if (size < 22 || !(t = alloc(tail)) || !read_at(f, size - tail, t, tail)) goto out;
    for (i = tail - 22 + 1; i-- > 0;) if (t[i] == 'P' && t[i + 1] == 'K' && t[i + 2] == 5 && t[i + 3] == 6) break;
    if (i == (DWORD)-1) goto out;
    cdsize = U32(t + i + 12); cdoff = U32(t + i + 16);
    // archives that start with the "PK00" spanning marker store offsets without those 4 bytes
    adj = (cdoff + cdsize + (tail - i) == size) ? 0 : 4;
    if (cdsize > size || !(cd = alloc(cdsize)) || !read_at(f, cdoff + adj, cd, cdsize)) goto out;
    for (p = cd; p + 46 <= cd + cdsize && U32(p) == 0x02014b50; p += 46 + nl + U16(p + 30) + U16(p + 32)) {
        nl = U16(p + 28);
        if (nl < 4 || p + 46 + nl > cd + cdsize) break;
        if (CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, (char *)p + 46 + nl - 4, 4, ".msi", 4) != CSTR_EQUAL) continue;
        e->flags = U16(p + 8); e->method = U16(p + 10); e->dostime = U16(p + 12);
        e->crc = U32(p + 16); e->csize = U32(p + 20); e->usize = U32(p + 24); lho = U32(p + 42) + adj;
        if (!read_at(f, lho, lh, 30) || U32(lh) != 0x04034b50) break;
        nl = U16(lh + 26); xl = U16(lh + 28);
        e->data = lho + 30 + nl + xl;
        rc = (e->method == 0 || e->method == 8) ? PK_OK : PK_FORMAT;
        break;
    }
out:
    release(t); release(cd); CloseHandle(f);
    return rc;
}

int zip_walk(const WCHAR *zip, zip_visit_fn fn, void *ctx)
{
    HANDLE f = CreateFileW(zip, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, tail, i, cdoff, cdsize, adj, lho, nl, count, seen = 0; BYTE *t = NULL, *cd = NULL, *p, lh[30]; int rc = PK_FORMAT; zipent e;
    if (f == INVALID_HANDLE_VALUE) return PK_IO;
    crc_init();
    size = GetFileSize(f, NULL);
    tail = size < 65557 ? size : 65557;
    if (size < 22 || !(t = alloc(tail)) || !read_at(f, size - tail, t, tail)) goto out;
    for (i = tail - 22 + 1; i-- > 0;) if (t[i] == 'P' && t[i + 1] == 'K' && t[i + 2] == 5 && t[i + 3] == 6) break;
    if (i == (DWORD)-1) goto out;
    count = U16(t + i + 10); cdsize = U32(t + i + 12); cdoff = U32(t + i + 16);
    if (count == 0xFFFF || cdsize == 0xFFFFFFFF || cdoff == 0xFFFFFFFF) goto out;      // Zip64
    adj = (cdoff + cdsize + (tail - i) == size) ? 0 : 4;
    if (cdsize > size || !(cd = alloc(cdsize + 1)) || !read_at(f, cdoff + adj, cd, cdsize)) goto out;
    for (p = cd; seen < count; p += 46 + nl + U16(p + 30) + U16(p + 32), seen++) {
        if (p + 46 > cd + cdsize || U32(p) != 0x02014b50) goto out;
        nl = U16(p + 28);
        if (p + 46 + nl + U16(p + 30) + U16(p + 32) > cd + cdsize) goto out;
        zero(&e, sizeof(e)); lstrcpynW(e.path, zip, MAX_PATH);
        e.flags = U16(p + 8); e.method = U16(p + 10); e.dostime = U16(p + 12);
        e.crc = U32(p + 16); e.csize = U32(p + 20); e.usize = U32(p + 24); lho = U32(p + 42);
        if (e.csize == 0xFFFFFFFF || e.usize == 0xFFFFFFFF || lho == 0xFFFFFFFF) goto out;
        lho += adj;
        if (!read_at(f, lho, lh, 30) || U32(lh) != 0x04034b50) goto out;
        e.data = lho + 30 + U16(lh + 26) + U16(lh + 28);
        if (!fn((const char *)p + 46, nl, &e, ctx)) { rc = PK_CANCEL; goto out; }
    }
    rc = PK_OK;
out:
    release(t); release(cd); CloseHandle(f);
    return rc;
}

static BOOL name_char_bad(BYTE c)
{ return c < 0x20 || c == 0x7F || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|'; }

static BOOL reserved_device(const char *c, DWORD n)         // CON, PRN, AUX, NUL, COM1-9, LPT1-9 (also with an extension)
{
    static const char *const names[] = { "con", "prn", "aux", "nul", NULL };
    DWORD base = 0, i; int k;
    while (base < n && c[base] != '.') base++;
    for (k = 0; names[k]; k++) {
        if (base != 3) break;
        for (i = 0; i < 3 && (c[i] | 0x20) == names[k][i]; i++);
        if (i == 3) return TRUE;
    }
    if (base == 4 && c[3] >= '1' && c[3] <= '9' &&
        (((c[0] | 0x20) == 'c' && (c[1] | 0x20) == 'o' && (c[2] | 0x20) == 'm') || ((c[0] | 0x20) == 'l' && (c[1] | 0x20) == 'p' && (c[2] | 0x20) == 't'))) return TRUE;
    return FALSE;
}

BOOL zip_name_safe(const char *name, DWORD nlen, BOOL *isdir)
{
    DWORD i, start = 0;
    *isdir = FALSE;
    if (!nlen || nlen > 240 || name[0] == '/') return FALSE;
    if (name[nlen - 1] == '/') { *isdir = TRUE; nlen--; if (!nlen) return FALSE; }
    for (i = 0; i <= nlen; i++) {
        if (i < nlen && name_char_bad((BYTE)name[i])) return FALSE;
        if (i == nlen || name[i] == '/') {
            DWORD n = i - start; const char *c = name + start;
            if (!n) return FALSE;                                          // "a//b"
            if ((n == 1 && c[0] == '.') || (n == 2 && c[0] == '.' && c[1] == '.')) return FALSE;
            if (c[n - 1] == '.' || c[n - 1] == ' ') return FALSE;         // Windows drops them: "..." / ".. " would become ".."
            if (reserved_device(c, n)) return FALSE;
            start = i + 1;
        }
    }
    return TRUE;
}

/* ---------- the files of a package named in a pinned table: safe unpacking, copying and checking ---------- */

// Used by the driver (its 18 files) and by the DynaRun setup (pkg_prepare_target / pkg_extract_checked below): nothing a zip says about a path is ever used
// (every destination is built from the program's own table or name), what is written is written into folders made here, links are refused, sizes are capped.

static BOOL rel_prefix_eq(const WCHAR *a, const WCHAR *b, int len)
{
    int i;
    for (i = 0; i < len; i++) if (a[i] != b[i]) return FALSE;
    return TRUE;
}

// The sub-folders the relative paths of the table need, each once, shortest first ("i386", "amd64", "a" before "a/b"): visit(prefix, len, ctx).
// mode 0: create dir\<prefix> below root (new: an existing one is an error); mode 1: check that it is a plain folder.
static BOOL each_subdir(const WCHAR *root, const pkg_file *t, int n, int mode)
{
    WCHAR p[MAX_PATH]; int i, j, k;
    for (i = 0; i < n; i++) {
        for (k = 0; t[i].rel[k]; k++) {
            if (t[i].rel[k] != '/') continue;
            for (j = 0; j < i; j++) if (t[j].rel[k] == '/' && rel_prefix_eq(t[j].rel, t[i].rel, k)) break;     // an earlier file has made this folder
            if (j < i) continue;
            if (lstrlenW(root) + 1 + k + 1 >= MAX_PATH) return FALSE;
            lstrcpyW(p, root); lstrcatW(p, L"\\"); lstrcpynW(p + lstrlenW(p), t[i].rel, k + 1);
            { int q; for (q = lstrlenW(root); p[q]; q++) if (p[q] == '/') p[q] = '\\'; }
            if (mode == 0 ? !CreateDirectoryW(p, NULL) : !pkg_is_plain_dir(p)) return FALSE;
        }
    }
    return TRUE;
}

BOOL pkg_is_plain_dir(const WCHAR *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}

// dir + sub + <rel> (with '\'); FALSE when it does not fit. sub: "\<top>\" (a staging area or an unpacked zip) or "\" (the folder that holds the files).
BOOL pkg_file_path(WCHAR *out, const WCHAR *dir, const WCHAR *sub, const pkg_file *t, int i)
{
    int n = lstrlenW(dir), k;
    if (n + lstrlenW(sub) + lstrlenW(t[i].rel) + 1 >= MAX_PATH) return FALSE;
    lstrcpyW(out, dir); lstrcatW(out, sub); lstrcatW(out, t[i].rel);
    for (k = n; out[k]; k++) if (out[k] == '/') out[k] = '\\';
    return TRUE;
}

// The zip entry name (as stored, '/' separators, not 0-terminated) is table file i when it is that name at the end, after a folder boundary: "<top>/i386/x.sys" and a
// re-zipped "other/i386/x.sys" both are. Case does not matter.
int pkg_entry_index(const pkg_file *t, int n, const char *name, DWORD nlen)
{
    int i; DWORD rl, k;
    for (i = 0; i < n; i++) {
        const WCHAR *r = t[i].rel; BOOL ok = TRUE;
        rl = (DWORD)lstrlenW(r);
        if (nlen < rl || (nlen > rl && name[nlen - rl - 1] != '/')) continue;
        for (k = 0; k < rl && ok; k++) {
            char a = name[nlen - rl + k], b = (char)r[k];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) ok = FALSE;
        }
        if (ok) return i;
    }
    return -1;
}

// The folders of a staging area are ones we made: dir, dir\<top> and the sub-folders of the table are plain folders, no link among them.
BOOL pkg_dirs_plain(const WCHAR *dir, const WCHAR *top, const pkg_file *t, int n)
{
    WCHAR root[MAX_PATH];
    if (!pkg_is_plain_dir(dir) || lstrlenW(dir) + 1 + lstrlenW(top) + 1 >= MAX_PATH) return FALSE;
    lstrcpyW(root, dir); lstrcatW(root, L"\\"); lstrcatW(root, top);
    return pkg_is_plain_dir(root) && each_subdir(root, t, n, 1);
}

typedef struct { const pkg_file *t; int n; DWORD maxfile; zipent e[PKG_MAXF]; DWORD nlen[PKG_MAXF]; BOOL have[PKG_MAXF]; } pkgzset;

// Collects the zip entries that are table files. Of several entries of the same name (a duplicate folder) the one with the shortest path is taken. An encrypted
// entry, another method than stored / deflate, or one that is too big does not count: that file is "missing".
static BOOL pkgzset_visit(const char *name, DWORD nlen, const zipent *e, void *ctx)
{
    pkgzset *z = (pkgzset *)ctx; int i = pkg_entry_index(z->t, z->n, name, nlen);
    if (i < 0 || (e->flags & 1) || (e->method != 0 && e->method != 8) || e->usize > z->maxfile) return TRUE;
    if (!z->have[i] || nlen < z->nlen[i]) { z->e[i] = *e; z->nlen[i] = nlen; z->have[i] = TRUE; }
    return TRUE;
}

// How many table files the zip has (a cheap look at its central directory).
int pkg_zip_has(const WCHAR *zip, const pkg_file *t, int n, DWORD maxfile)
{
    static pkgzset z; int i, c = 0;
    zero(&z, sizeof(z)); z.t = t; z.n = n; z.maxfile = maxfile;
    if (zip_walk(zip, pkgzset_visit, &z) != PK_OK) return -1;
    for (i = 0; i < n; i++) if (z.have[i]) c++;
    return c;
}

// Takes the table's files out of the zip into dir\<top>\ (dir exists; the folders below are created here, so they are new: a link cannot be in the way). Only
// those files are extracted (the other ones of the zip never touch the disk); limits: maxfile each, maxtotal all together (zip-bomb guard); a file that is not there
// counts in *missing. Entries are checked against their CRC and size by zip_extract. PK_OK, or PK_FORMAT / PK_IO / PK_CANCEL.
int pkg_stage_files(const WCHAR *zip, const WCHAR *dir, const WCHAR *top, const pkg_file *t, int n, DWORD maxfile, DWORD maxtotal, volatile LONG *cancel, int *missing)
{
    static pkgzset z; WCHAR root[MAX_PATH], p[MAX_PATH], sub[80]; int i, rc; DWORD total = 0, a;
    *missing = 0;
    zero(&z, sizeof(z)); z.t = t; z.n = n; z.maxfile = maxfile;
    if (zip_walk(zip, pkgzset_visit, &z) != PK_OK) return PK_FORMAT;
    if (lstrlenW(dir) + 1 + lstrlenW(top) + 1 >= MAX_PATH || lstrlenW(top) + 3 >= 80) return PK_IO;
    lstrcpyW(root, dir); lstrcatW(root, L"\\"); lstrcatW(root, top);
    if (!CreateDirectoryW(root, NULL) || !each_subdir(root, t, n, 0)) return PK_IO;
    if (!pkg_dirs_plain(dir, top, t, n)) return PK_FORMAT;
    lstrcpyW(sub, L"\\"); lstrcatW(sub, top); lstrcatW(sub, L"\\");
    for (i = 0; i < n; i++) {
        if (!z.have[i]) { (*missing)++; continue; }
        if ((total += z.e[i].usize) > maxtotal) return PK_FORMAT;
        if (!pkg_file_path(p, dir, sub, t, i)) return PK_IO;
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) return PK_FORMAT;         // the path is new: whatever is there is not ours
        rc = zip_extract(&z.e[i], "", p, NULL, cancel);
        if (rc == PK_PASSWORD) rc = PK_FORMAT;                                          // CRC or size mismatch: the entry is damaged
        if (rc != PK_OK) return rc;
        a = GetFileAttributesW(p);
        if (a == INVALID_FILE_ATTRIBUTES || (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return PK_FORMAT;
    }
    return PK_OK;
}

// The same for an unpacked folder (src: the folder that holds the files): they are COPIED from there into dir\<top>\ (new folders made here, a path that
// exists is refused). The source folders on the way must be plain ones, a source file that is a link, a folder or too big is refused; what is hashed afterwards
// is the copy, not the source. *missing: files that are not there.
int pkg_stage_from_dir(const WCHAR *src, const WCHAR *dir, const WCHAR *top, const pkg_file *t, int n, DWORD maxfile, DWORD maxtotal, int *missing)
{
    WCHAR root[MAX_PATH], p[MAX_PATH], s[MAX_PATH], sub[80]; int i; DWORD total = 0, a; WIN32_FILE_ATTRIBUTE_DATA fa;
    *missing = 0;
    if (!pkg_is_plain_dir(src) || !each_subdir(src, t, n, 1)) return PK_FORMAT;
    if (lstrlenW(dir) + 1 + lstrlenW(top) + 1 >= MAX_PATH || lstrlenW(top) + 3 >= 80) return PK_IO;
    lstrcpyW(root, dir); lstrcatW(root, L"\\"); lstrcatW(root, top);
    if (!CreateDirectoryW(root, NULL) || !each_subdir(root, t, n, 0)) return PK_IO;
    if (!pkg_dirs_plain(dir, top, t, n)) return PK_FORMAT;
    lstrcpyW(sub, L"\\"); lstrcatW(sub, top); lstrcatW(sub, L"\\");
    for (i = 0; i < n; i++) {
        if (!pkg_file_path(s, src, L"\\", t, i) || !pkg_file_path(p, dir, sub, t, i)) return PK_IO;
        if (!GetFileAttributesExW(s, GetFileExInfoStandard, &fa)) { (*missing)++; continue; }
        if ((fa.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || fa.nFileSizeHigh || fa.nFileSizeLow > maxfile) return PK_FORMAT;
        if ((total += fa.nFileSizeLow) > maxtotal) return PK_FORMAT;
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) return PK_FORMAT;
        if (!CopyFileW(s, p, TRUE)) return PK_IO;
        a = GetFileAttributesW(p);
        if (a == INVALID_FILE_ATTRIBUTES || (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return PK_FORMAT;
    }
    return PK_OK;
}

// The files below dir + sub against the table (expect NULL) or against a list of accepted hashes: a missing file, one that is a link or a folder, a hash that differs.
void pkg_check_files(const WCHAR *dir, const WCHAR *sub, const pkg_file *t, int n, WCHAR (*expect)[65], pkg_check *c)
{
    WCHAR p[MAX_PATH]; ULONGLONG sz; int i; DWORD a;
    zero(c, sizeof(*c));
    for (i = 0; i < n; i++) {
        if (!pkg_file_path(p, dir, sub, t, i) || (a = GetFileAttributesW(p)) == INVALID_FILE_ATTRIBUTES) { c->st[i] = PKC_MISSING; c->missing++; continue; }
        if ((a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || !pkg_sha256(p, c->hex[i], &sz)) { c->st[i] = PKC_DIFFER; c->differ++; c->hex[i][0] = 0; continue; }
        if (lstrcmpiW(c->hex[i], expect ? expect[i] : t[i].sha)) { c->st[i] = PKC_DIFFER; c->differ++; continue; }
        c->ok++;
    }
}

// A path that is going to be written (the DynaRun Setup.msi taken out of the zip, in our temp folder): what is there from an earlier try is a plain file and is
// removed; a link is removed itself and never followed; a folder is refused. FALSE: it cannot be made free.
BOOL pkg_prepare_target(const WCHAR *path)
{
    DWORD a = GetFileAttributesW(path);
    if (a == INVALID_FILE_ATTRIBUTES) return TRUE;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) return (a & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(path) : DeleteFileW(path);
    if (a & FILE_ATTRIBUTE_DIRECTORY) return FALSE;
    return DeleteFileW(path);
}

// One zip entry into path: refused when it is bigger than maxsize (the entry's own claim; zip_extract also checks what really comes out), and the target is
// made free first (pkg_prepare_target).
int pkg_extract_checked(const zipent *e, const char *pw, const WCHAR *out, DWORD maxsize, progress_fn cb, volatile LONG *cancel)
{
    if (e->usize > maxsize) return PK_FORMAT;
    if (!pkg_prepare_target(out)) return PK_IO;
    return zip_extract(e, pw, out, cb, cancel);
}


// The 12-byte encryption header ends with a check byte: right for the right password, and for
// about 1 in 256 wrong ones (those are caught by the CRC after extraction).
BOOL zip_password_plausible(const zipent *e, const char *pw)
{
    HANDLE f; BYTE h[12]; zkeys z; int i; BOOL ok;
    if (!(e->flags & 1)) return TRUE;
    f = CreateFileW(e->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    ok = read_at(f, e->data, h, 12);
    CloseHandle(f);
    if (!ok) return FALSE;
    zk_init(&z, pw);
    for (i = 0; i < 12; i++) h[i] = zk_decrypt(&z, h[i]);
    ok = h[11] == (BYTE)((e->flags & 8) ? e->dostime >> 8 : e->crc >> 24);
    SecureZeroMemory(&z, sizeof(z)); SecureZeroMemory(h, sizeof(h));     // the keys are derived from the password
    return ok;
}

int zip_extract(const zipent *e, const char *pw, const WCHAR *out, progress_fn cb, volatile LONG *cancel)
{
    static tinfl_decompressor inf;
    static BYTE in[CHUNK], dict[TINFL_LZ_DICT_SIZE];
    HANDLE f, o; zkeys z; DWORD left, i, n, w, crc = 0xFFFFFFFF, done = 0, avail = 0, dofs = 0; BYTE *ip = in;
    int rc = PK_OK; tinfl_status st = TINFL_STATUS_NEEDS_MORE_INPUT;

    f = CreateFileW(e->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return PK_IO;
    o = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (o == INVALID_HANDLE_VALUE) { CloseHandle(f); return PK_IO; }
    SetFilePointer(f, e->data, NULL, FILE_BEGIN);
    left = e->csize;
    if (e->flags & 1) {                           // skip the 12-byte encryption header
        zk_init(&z, pw);
        if (!ReadFile(f, in, 12, &n, NULL) || n != 12 || left < 12) { rc = PK_FORMAT; goto out; }
        for (i = 0; i < 12; i++) zk_decrypt(&z, in[i]);
        left -= 12;
    }
    tinfl_init(&inf);
    for (;;) {
        if (*cancel) { rc = PK_CANCEL; break; }
        if (!avail && left) {
            if (!ReadFile(f, in, left < CHUNK ? left : CHUNK, &avail, NULL) || !avail) { rc = PK_IO; break; }
            left -= avail; ip = in;
            if (e->flags & 1) for (i = 0; i < avail; i++) in[i] = zk_decrypt(&z, in[i]);
        }
        if (e->method == 0) {                     // stored
            if (!avail) break;
            n = avail;
            for (i = 0; i < n; i++) crc = CRC1(crc, ip[i]);
            if (!WriteFile(o, ip, n, &w, NULL) || w != n) { rc = PK_IO; break; }
            done += n; avail = 0;
        } else {
            size_t isz = avail, osz = TINFL_LZ_DICT_SIZE - dofs;
            st = tinfl_decompress(&inf, ip, &isz, dict, dict + dofs, &osz, left ? TINFL_FLAG_HAS_MORE_INPUT : 0);
            ip += isz; avail -= (DWORD)isz;
            for (i = 0; i < osz; i++) crc = CRC1(crc, dict[dofs + i]);
            if (osz && (!WriteFile(o, dict + dofs, (DWORD)osz, &w, NULL) || w != osz)) { rc = PK_IO; break; }
            dofs = (dofs + (DWORD)osz) & (TINFL_LZ_DICT_SIZE - 1);
            done += (DWORD)osz;
            if (st < TINFL_STATUS_DONE) { rc = PK_PASSWORD; break; }   // garbage in: wrong password (or a damaged file)
            if (st == TINFL_STATUS_DONE) break;
            if (st == TINFL_STATUS_NEEDS_MORE_INPUT && !avail && !left) { rc = PK_PASSWORD; break; }
        }
        if (cb) cb(done, e->usize);
    }
    if (rc == PK_OK && ((crc ^ 0xFFFFFFFF) != e->crc || done != e->usize)) rc = PK_PASSWORD;
out:
    SecureZeroMemory(&z, sizeof(z)); SecureZeroMemory(in, sizeof(in));      // keys derived from the password; the buffer held decrypted data
    CloseHandle(o); CloseHandle(f);
    if (rc != PK_OK) DeleteFileW(out);
    return rc;
}

/* ---------- the MSI ---------- */

static char *query1(MSIHANDLE db, const WCHAR *sql, const WCHAR *col1_contains)
{
    MSIHANDLE v, r; char *res = NULL; WCHAR key[128]; DWORD n;
    if (MsiDatabaseOpenViewW(db, sql, &v)) return NULL;
    if (!MsiViewExecute(v, 0)) {
        while (!res && !MsiViewFetch(v, &r)) {
            n = 128; key[0] = 0;
            MsiRecordGetStringW(r, 1, key, &n);
            CharLowerW(key);
            if (!col1_contains || StrStrW_(key, col1_contains)) {
                n = 0;
                if (MsiRecordGetStringA(r, 2, "", &n) == ERROR_MORE_DATA && (res = alloc(++n + 1)) && MsiRecordGetStringA(r, 2, res, &n)) { release(res); res = NULL; }
            }
            MsiCloseHandle(r);
        }
    }
    MsiViewClose(v); MsiCloseHandle(v);
    return res;
}

char *msi_license_rtf(const WCHAR *msi)
{
    MSIHANDLE db; char *rtf;
    if (MsiOpenDatabaseW(msi, (LPCWSTR)MSIDBOPEN_READONLY, &db)) return NULL;
    rtf = query1(db, L"SELECT `Dialog_`, `Text` FROM `Control` WHERE `Type` = 'ScrollableText'", L"licen");
    MsiCloseHandle(db);
    return rtf;
}

// DynaRun V3 is recognised by the UpgradeCode of its MSI, which stays the same in every version of the
// product; the ProductName must also still name it (any version). Never by file name or hash.
#define DYNARUN_UPGRADE_CODE "{4787E5B2-F7CE-45B9-8D1D-68E167D06DF7}"

static BOOL names_dynarun(const char *s)          // "Dyna Run V3", "DynaRun 4", ...: "dynarun" ignoring spaces and case
{
    static const char want[] = "dynarun";
    int i = 0;
    for (; s && *s; s++) {
        char c = *s >= 'A' && *s <= 'Z' ? *s + 32 : *s;
        if (c == ' ') continue;
        if (c == want[i]) { if (!want[++i]) return TRUE; }
        else i = c == want[0];
    }
    return FALSE;
}

int msi_check(const WCHAR *msi, WCHAR *version, int cch)
{
    MSIHANDLE db; char *code, *name, *ver; int r;
    if (version) version[0] = 0;
    if (MsiOpenDatabaseW(msi, (LPCWSTR)MSIDBOPEN_READONLY, &db)) return MSI_NONE;
    code = query1(db, L"SELECT `Property`, `Value` FROM `Property` WHERE `Property` = 'UpgradeCode'", NULL);
    name = query1(db, L"SELECT `Property`, `Value` FROM `Property` WHERE `Property` = 'ProductName'", NULL);
    ver = query1(db, L"SELECT `Property`, `Value` FROM `Property` WHERE `Property` = 'ProductVersion'", NULL);
    MsiCloseHandle(db);
    r = code && !lstrcmpiA(code, DYNARUN_UPGRADE_CODE) && names_dynarun(name) ? MSI_DYNARUN : MSI_OTHER;
    if (version && ver) MultiByteToWideChar(CP_ACP, 0, ver, -1, version, cch);
    release(code); release(name); release(ver);
    return r;
}

// SHA-256 and size of a file, for the user to compare with what Dyna Pro gives them. Display only.
BOOL pkg_sha256(const WCHAR *path, WCHAR *hex, ULONGLONG *size)
{
    static BYTE buf[CHUNK]; BYTE d[32]; DWORD n, dl = 32, i; HCRYPTPROV cp; HCRYPTHASH h; LARGE_INTEGER sz; BOOL ok = FALSE;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    hex[0] = 0; if (size) *size = 0;
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    if (size && GetFileSizeEx(f, &sz)) *size = (ULONGLONG)sz.QuadPart;
    if (CryptAcquireContextW(&cp, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(cp, CALG_SHA_256, 0, 0, &h)) {
            while ((ok = ReadFile(f, buf, sizeof(buf), &n, NULL)) && n && (ok = CryptHashData(h, buf, n, 0)));
            ok = ok && CryptGetHashParam(h, HP_HASHVAL, d, &dl, 0) && dl == 32;
            for (i = 0; ok && i < 32; i++) wsprintfW(hex + 2 * i, L"%02x", d[i]);
            CryptDestroyHash(h);
        }
        CryptReleaseContext(cp, 0);
    }
    CloseHandle(f);
    if (!ok) hex[0] = 0;
    return ok;
}

// ProductVersion and Manufacturer from the MSI's Property table (display only; nothing is decided on them).
void msi_info(const WCHAR *msi, WCHAR *version, WCHAR *manufacturer, int cch)
{
    MSIHANDLE db; char *ver, *mfr;
    version[0] = manufacturer[0] = 0;
    if (MsiOpenDatabaseW(msi, (LPCWSTR)MSIDBOPEN_READONLY, &db)) return;
    ver = query1(db, L"SELECT `Property`, `Value` FROM `Property` WHERE `Property` = 'ProductVersion'", NULL);
    mfr = query1(db, L"SELECT `Property`, `Value` FROM `Property` WHERE `Property` = 'Manufacturer'", NULL);
    MsiCloseHandle(db);
    if (ver) MultiByteToWideChar(CP_ACP, 0, ver, -1, version, cch);
    if (mfr) MultiByteToWideChar(CP_ACP, 0, mfr, -1, manufacturer, cch);
    release(ver); release(mfr);
}

// Cabinet files the MSI keeps outside itself (a Media.Cabinet name without the leading '#'): a multi-string (heap) of plain
// file names, or NULL when there are none. The setup copies them along with the MSI (see stage_msi).
// A file name that would not name a plain file in the staging folder: a DOS device (CON, NUL, COM1, LPT2.x ...), a trailing dot or
// space, wildcard / reserved characters, or a control character.
static BOOL bad_cab_name(const WCHAR *s)
{
    static const WCHAR *const dev[] = { L"CON", L"PRN", L"AUX", L"NUL", NULL };
    WCHAR base[16]; int i, n;
    for (i = 0; s[i]; i++)
        if (s[i] < 32 || s[i] == '"' || s[i] == '<' || s[i] == '>' || s[i] == '|' || s[i] == '*' || s[i] == '?') return TRUE;
    if (i == 0 || s[i - 1] == '.' || s[i - 1] == ' ') return TRUE;
    for (n = 0; s[n] && s[n] != '.' && n < 15; n++) base[n] = s[n];
    if (s[n] && s[n] != '.') return FALSE;                      // longer than any device name before the first dot
    base[n] = 0;
    while (n > 0 && base[n - 1] == ' ') base[--n] = 0;          // "NUL .txt" is the device too
    for (i = 0; dev[i]; i++) if (!lstrcmpiW(base, dev[i])) return TRUE;
    if (n == 4 && (CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, base, 3, L"COM", 3) == CSTR_EQUAL ||
                   CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, base, 3, L"LPT", 3) == CSTR_EQUAL) &&
        ((base[3] >= '0' && base[3] <= '9') || base[3] == 0xB9 || base[3] == 0xB2 || base[3] == 0xB3)) return TRUE;   // COM1-9, LPT1-9 (and the superscript digits)
    return FALSE;
}

// The cabinets the MSI keeps outside itself (Media table), as a multi-string of plain file names. Call it on the copy that
// is going to be installed, not on the user's original.
WCHAR *msi_external_files(const WCHAR *msi)
{
    MSIHANDLE db, v, r; WCHAR *list, one[200], *c; DWORD n, used = 0, cap = 4096;
    if (MsiOpenDatabaseW(msi, (LPCWSTR)MSIDBOPEN_READONLY, &db)) return NULL;
    if (!(list = alloc(cap * sizeof(WCHAR)))) { MsiCloseHandle(db); return NULL; }
    if (!MsiDatabaseOpenViewW(db, L"SELECT `Cabinet` FROM `Media`", &v)) {
        if (!MsiViewExecute(v, 0))
            while (!MsiViewFetch(v, &r)) {
                n = 200; one[0] = 0;
                if (!MsiRecordGetStringW(r, 1, one, &n) && one[0] && one[0] != '#') {
                    for (c = one; *c && *c != '\\' && *c != '/' && *c != ':'; c++);     // a plain file name only
                    if (!*c && !bad_cab_name(one) && used + n + 2 < cap) { lstrcpyW(list + used, one); used += n + 1; }
                }
                MsiCloseHandle(r);
            }
        MsiViewClose(v); MsiCloseHandle(v);
    }
    MsiCloseHandle(db);
    if (!used) { release(list); return NULL; }
    list[used] = 0;
    return list;
}

// Basic UI only (/qb): the wizard pages of this Wise setup, the only part that uses VBScript, are skipped.
int run_msiexec(const WCHAR *msi)
{
    static WCHAR exe[MAX_PATH], cmd[MAX_PATH * 2 + 64];
    DWORD rc = 1;
    GetSystemDirectoryW(exe, MAX_PATH); lstrcatW(exe, L"\\msiexec.exe");
    wsprintfW(cmd, L"\"%s\" /i \"%s\" /qb! REBOOT=ReallySuppress", exe, msi);
    AllowSetForegroundWindow(ASFW_ANY);   // msiexec's progress window may come to the front
    if (run_process(exe, cmd, NULL, 0, FALSE, INFINITE, &rc) == PROC_NOSTART) return (int)GetLastError();
    return (int)rc;
}

// Installed DynaRun V3 (by UpgradeCode): its product code in out (39 chars), FALSE when there is none.
BOOL dynarun_product(WCHAR *out)
{
    return !MsiEnumRelatedProductsW(L"" DYNARUN_UPGRADE_CODE, 0, 0, out);
}

// Dyna Pro's own uninstall of DynaRun V3 (basic UI, like the install). msiexec exit code; RM_NONE = not installed.
int remove_dynarun(void)
{
    static WCHAR exe[MAX_PATH], cmd[MAX_PATH + 96], prod[40];
    DWORD rc = 1;
    if (!dynarun_product(prod)) return RM_NONE;
    GetSystemDirectoryW(exe, MAX_PATH); lstrcatW(exe, L"\\msiexec.exe");
    wsprintfW(cmd, L"\"%s\" /x %s /qb! REBOOT=ReallySuppress", exe, prod);
    AllowSetForegroundWindow(ASFW_ANY);
    if (run_process(exe, cmd, NULL, 0, FALSE, INFINITE, &rc) == PROC_NOSTART) return (int)GetLastError();
    return (int)rc;
}

/* ---------- OneDrive placeholders the setup will overwrite ---------- */


static BOOL name_listed(const WCHAR *list, const WCHAR *name)
{
    for (; *list; list += lstrlenW(list) + 1) if (!lstrcmpiW(list, name)) return TRUE;
    return FALSE;
}

static int hydrate_tree(const WCHAR *dir, const WCHAR *names);
typedef struct { const WCHAR *names; int count; } hydrate_ctx;

static BOOL hydrate_entry(const WCHAR *p, const WIN32_FIND_DATAW *fd, void *vc)
{
    static BYTE buf[CHUNK];
    hydrate_ctx *c = vc; HANDLE h; DWORD n;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { c->count += hydrate_tree(p, c->names); return TRUE; }
    if (!(fd->dwFileAttributes & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_OFFLINE))) return TRUE;
    if (!name_listed(c->names, fd->cFileName)) return TRUE;
    h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return TRUE;
    while (ReadFile(h, buf, sizeof(buf), &n, NULL) && n) ;
    CloseHandle(h);
    c->count++;
    return TRUE;
}

static int hydrate_tree(const WCHAR *dir, const WCHAR *names)
{
    WCHAR *p = alloc(2 * MAX_PATH * sizeof(WCHAR)); hydrate_ctx c;
    if (!p) return 0;
    c.names = names; c.count = 0;
    dir_walk(dir, p, 2 * MAX_PATH, MAX_PATH, hydrate_entry, &c);
    release(p);
    return c.count;
}

// The setup also installs manuals and example data to Documents\Dyna Pro Dynamometers. When Documents
// is in OneDrive and an earlier copy there is "online-only", Windows Installer (a SYSTEM service)
// cannot read it and stops with error 1305. Reading those files as the user makes OneDrive download
// them first. Only files the setup will overwrite are read; contents and OneDrive settings stay as they are.
int pkg_prepare_documents(const WCHAR *msi)
{
    MSIHANDLE db, v, r; WCHAR *names, *w, root[MAX_PATH], one[300], *bar; DWORD n, used = 0, cap = 64 * 1024; int count = 0;
    if (!SHGetSpecialFolderPathW(NULL, root, CSIDL_PERSONAL, FALSE) || lstrlenW(root) > MAX_PATH - 30) return 0;
    lstrcatW(root, L"\\Dyna Pro Dynamometers");
    if (!exists(root) || MsiOpenDatabaseW(msi, (LPCWSTR)MSIDBOPEN_READONLY, &db)) return 0;
    if (!(names = alloc(cap * sizeof(WCHAR)))) { MsiCloseHandle(db); return 0; }
    if (!MsiDatabaseOpenViewW(db, L"SELECT `FileName` FROM `File`", &v)) {
        if (!MsiViewExecute(v, 0))
            while (!MsiViewFetch(v, &r)) {
                n = 300;
                if (!MsiRecordGetStringW(r, 1, one, &n)) {
                    for (w = one, bar = NULL; *w; w++) if (*w == '|') bar = w;   // "SHORT~1.PDF|Long Name.pdf"
                    w = bar ? bar + 1 : one;
                    n = lstrlenW(w);
                    if (used + n + 2 < cap) { lstrcpyW(names + used, w); used += n + 1; }
                }
                MsiCloseHandle(r);
            }
        MsiViewClose(v); MsiCloseHandle(v);
    }
    MsiCloseHandle(db);
    names[used] = 0;
    if (used) count = hydrate_tree(root, names);
    release(names);
    return count;
}
