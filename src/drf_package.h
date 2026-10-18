// package.c: finding and downloading the DynaRun setup and the driver package, zip and MSI handling.
#pragma once
#include <windows.h>
#include <wininet.h>
#include "errors.h"                         // PK_*, PKC_*

#define DYNAPRO_PAGE L"https://dynapro.co.uk/Software_Release.htm"
#define DYNAPRO_HOST L"dynapro.co.uk"
// Hosts a download from GitHub may be served from (releases/download/... redirects to the asset CDN): every hop is checked against them.
#define GITHUB_HOSTS L"github.com;api.github.com;objects.githubusercontent.com;release-assets.githubusercontent.com"
#define DYNAPRO_ZIP  L"https://dynapro.co.uk/Site/graphics/Technical_Info/Software/Dyna%20Pro%20Dynamometers.zip"
typedef void (*progress_fn)(DWORD done, DWORD total);

typedef struct {
    WCHAR path[MAX_PATH];
    DWORD data;                             // offset of the (encrypted) entry data
    DWORD csize, usize, crc, flags, method, dostime;
} zipent;

BOOL pkg_find_local(WCHAR *out);            // a DynaRun MSI, or a zip with an .msi inside, in Downloads/Desktop/Documents or next to us
// What a search for a driver package looks at and tells: accept(path, ctx) approves a zip / folder (the walk stops at the first one), log (NULL: silent)
// gets one line per folder looked at. pkg_find_zip_named uses only log (s may be NULL there).
typedef struct { BOOL (*accept)(const WCHAR *path, void *ctx); void *ctx; void (*log)(const WCHAR *line); } pkg_search;
BOOL pkg_find_zip_if(DWORD maxsize, const pkg_search *s, WCHAR *out);   // the first zip (at most maxsize bytes) in the same places that s->accept() approves
BOOL pkg_find_dirs(const pkg_search *s, WCHAR *out);   // the first folder s->accept() approves, in the same places (two levels down in Downloads)
BOOL pkg_find_dir_in(const WCHAR *root, int depth, const pkg_search *s, WCHAR *out);   // s->accept() for root and the folders up to `depth` levels below it
BOOL pkg_find_zip_named(const WCHAR *prefix, const pkg_search *s, WCHAR *out);   // the newest zip whose name starts with prefix in the same places
void pkg_reject(const WCHAR *path);       // not the DynaRun setup after all: pkg_find_local skips it
int pkg_download(WCHAR *out, progress_fn cb, volatile LONG *cancel, DWORD *err);
HINTERNET pkg_open_following(HINTERNET in, const WCHAR *url, const WCHAR *host, const WCHAR *hdr, DWORD *status, DWORD *err, BOOL *refused);   // redirects followed by hand, each hop checked
BOOL pkg_partial(const WCHAR *name, const WCHAR *etagval, DWORD *have);   // a partial download pkg_fetch can continue (.part and its ETag are there); *have: bytes so far
BOOL pkg_download_partial(DWORD *have);     // ... of the DynaRun zip
enum { NET_UNKNOWN = 0, NET_NO = 1, NET_YES = 2 };
HINTERNET pkg_net_open(DWORD connect_ms, DWORD receive_ms);   // a session with these timeouts, TLS 1.2+ (NULL: none)
BOOL dynarun_version(WCHAR *out, int cch);                    // the installed DynaRun V3's version (Windows Installer record), FALSE when not installed that way
int pkg_update_hint(WCHAR *installed, WCHAR *latest, int cch, void (*log)(const WCHAR *line));   // 0 nothing, 1 the site names a newer version, 2 the site's zip differs from the last download
int pkg_net_state(void);                    // NET_NO: Windows says it is disconnected (the download is not even tried); NET_YES / NET_UNKNOWN: try
int pkg_fetch(const WCHAR *url, const WCHAR *name, const WCHAR *etagval, const WCHAR *host, WCHAR *out, progress_fn cb, volatile LONG *cancel, DWORD *err);
int zip_open(const WCHAR *zip, zipent *e);  // first *.msi entry
// The files of a package named in a pinned table (the driver's 18 files): safe unpacking / copying and checking (package.c). Used by the driver; the DynaRun setup
// uses pkg_prepare_target / pkg_extract_checked. Nothing a zip says about a path is used, destinations are built from the table; links are refused; sizes capped.
typedef struct { const WCHAR *rel; DWORD size; const WCHAR *sha; } pkg_file;   // name below the package folder ('/' separators), size, SHA-256 (lower case)
#define PKG_MAXF 24
typedef struct { int ok, differ, missing; BYTE st[PKG_MAXF]; WCHAR hex[PKG_MAXF][65]; } pkg_check;   // st: 0 ok, 1 differs, 2 missing; hex: what the file hashes to
int pkg_entry_index(const pkg_file *t, int n, const char *name, DWORD nlen);   // the table file a zip entry name is (after a folder boundary, any case), or -1
BOOL pkg_is_plain_dir(const WCHAR *p);
BOOL pkg_file_path(WCHAR *out, const WCHAR *dir, const WCHAR *sub, const pkg_file *t, int i);
BOOL pkg_dirs_plain(const WCHAR *dir, const WCHAR *top, const pkg_file *t, int n);
int pkg_zip_has(const WCHAR *zip, const pkg_file *t, int n, DWORD maxfile);   // how many table files the zip has (central directory only), -1: not a zip
int pkg_stage_files(const WCHAR *zip, const WCHAR *dir, const WCHAR *top, const pkg_file *t, int n, DWORD maxfile, DWORD maxtotal, volatile LONG *cancel, int *missing);
int pkg_stage_from_dir(const WCHAR *src, const WCHAR *dir, const WCHAR *top, const pkg_file *t, int n, DWORD maxfile, DWORD maxtotal, int *missing);
void pkg_check_files(const WCHAR *dir, const WCHAR *sub, const pkg_file *t, int n, WCHAR (*expect)[65], pkg_check *c);
int pkg_extract_checked(const zipent *e, const char *pw, const WCHAR *out, DWORD maxsize, progress_fn cb, volatile LONG *cancel);   // zip_extract with a size cap and a free target
BOOL zip_password_plausible(const zipent *e, const char *pw);
int zip_extract(const zipent *e, const char *pw, const WCHAR *out, progress_fn cb, volatile LONG *cancel);
BOOL pkg_sha256(const WCHAR *path, WCHAR *hex, ULONGLONG *size);   // lower-case hex (65 WCHARs) and size of a file, read-only
void msi_info(const WCHAR *msi, WCHAR *version, int vcch, WCHAR *manufacturer, int mcch);   // ProductVersion / Manufacturer (Windows Installer API, read-only; "" when absent), each cut to its buffer, control characters as spaces (msi_text)
char *msi_license_rtf(const WCHAR *msi);    // RTF of the setup's license page (heap) or NULL
// The DynaRun V3 version the fix was tested with, and how a ProductVersion stands to it (the wizard's notices and the V4 page follow from the class).
#define DYNARUN_TESTED_VERSION L"3.26.0"
enum { VER_UNKNOWN, VER_TESTED, VER_UNTESTED, VER_V4PLUS };
int dynarun_ver_class(const WCHAR *ver);   // VER_UNKNOWN: not readable; VER_TESTED: the tested version; VER_UNTESTED: another version below 4; VER_V4PLUS: 4 or later
enum { MSI_NONE, MSI_OTHER, MSI_DYNARUN };
int msi_check(const WCHAR *msi, WCHAR *version, int cch);   // MSI_NONE: not an MSI at all. DynaRun: by UpgradeCode
WCHAR *msi_external_files(const WCHAR *msi);   // multi-string (heap) of cabinet files kept outside the MSI, or NULL
int run_msiexec(const WCHAR *msi);          // msiexec exit code
BOOL dynarun_product(WCHAR *out);           // product code of the installed DynaRun V3 (39 chars)
int remove_dynarun(void);                   // Dyna Pro's uninstall of DynaRun V3; msiexec exit code, -1 none
void pkg_temp_dir(WCHAR *out);              // %TEMP%\DynaRunFix (created)
int pkg_prepare_documents(const WCHAR *msi);   // download OneDrive "online-only" copies the setup overwrites
