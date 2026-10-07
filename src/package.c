// Getting the DynaRun V3 setup for the wizard: find or download Dyna Pro's password-protected
// "Dyna Pro Dynamometers.zip", decrypt (ZIP 2.0 "ZipCrypto") and inflate its Setup.msi with the
// password the user typed, read the license text from the MSI and run msiexec.
// Nothing of Dyna Pro's is shipped with DynaRunFix: the zip comes from their site or the user.
#include <windows.h>
#include <shlobj.h>
#include <wininet.h>
#include <msi.h>
#include <msiquery.h>
#include "setup.h"
#include "version.h"
#include "miniz.h"     // inflate only (third_party/miniz, built with the MINIZ_NO_* options in build.cmd)

#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)
#define CHUNK 65536
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#endif

static DWORD StrToIntW_(const WCHAR *s) { DWORD v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }
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

// Ranking: a checked DynaRun MSI first, then a zip named like Dyna Pro's download, then any other zip
// with an .msi inside; newest first within each. The name only orders the zips, it proves nothing.
static void scan(const WCHAR *dir, BOOL subdirs, candidate *best)
{
    WCHAR p[MAX_PATH * 2], *e; WIN32_FIND_DATAW fd; HANDLE f; int score; zipent z;
    if (!dir[0] || lstrlenW(dir) + 4 >= MAX_PATH) return;
    lstrcpyW(p, dir); lstrcatW(p, L"\\*");
    if ((f = FindFirstFileW(p, &fd)) == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.' || lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 >= MAX_PATH) continue;
        lstrcpyW(p, dir); lstrcatW(p, L"\\"); lstrcatW(p, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (subdirs && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) scan(p, FALSE, best);
            continue;
        }
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN)) continue;  // cloud-only: do not download it just to look
        e = fd.cFileName + lstrlenW(fd.cFileName) - 4;
        if (e < fd.cFileName) continue;
        if (!lstrcmpiW(e, L".msi")) score = 3;
        else if (!lstrcmpiW(e, L".zip")) score = CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, fd.cFileName, 4, L"dyna", 4) == CSTR_EQUAL ? 2 : 1;
        else continue;
        if (score < best->score || (score == best->score && CompareFileTime(&fd.ftLastWriteTime, &best->t) <= 0)) continue;
        if (rejected(p)) continue;
        if (score == 3 ? msi_check(p, NULL, 0) != MSI_DYNARUN : zip_open(p, &z) != PK_OK) continue;
        lstrcpyW(best->path, p); best->score = score; best->t = fd.ftLastWriteTime;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

BOOL pkg_find_local(WCHAR *out)
{
    WCHAR dirs[4][MAX_PATH], raw[MAX_PATH], *e; HKEY k; DWORD n = sizeof(raw), i; candidate *best = alloc(sizeof(candidate));
    out[0] = 0;
    if (!best) return FALSE;
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
    zero(best, sizeof(*best));
    for (i = 0; i < 4; i++) scan(dirs[i], i == 1, best);   // also one level into Downloads (e.g. Downloads\Compressed)
    lstrcpyW(out, best->path);
    release(best);
    return out[0] != 0;
}

/* ---------- download (resumable; the server sends ETag and supports Range) ---------- */

static BOOL query_str(HINTERNET h, DWORD what, WCHAR *out, DWORD cch)
{ DWORD n = cch * sizeof(WCHAR); out[0] = 0; return HttpQueryInfoW(h, what, out, &n, NULL); }

int pkg_download(WCHAR *out, progress_fn cb, volatile LONG *cancel, DWORD *err)
{
    static WCHAR part[MAX_PATH], tag[200], oldtag[200], hdr[400], num[32];
    static BYTE buf[CHUNK];
    HINTERNET in, h = NULL; HANDLE f; DWORD have, total, status, n, w, tries, timeout = 30000;
    LARGE_INTEGER sz; HKEY k; DWORD tn; int rc = PK_NET;

    *err = 0;
    pkg_temp_dir(out); lstrcatW(out, L"\\Dyna Pro Dynamometers.zip");
    lstrcpyW(part, out); lstrcatW(part, L".part");
    oldtag[0] = 0;
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\DynaRunFix", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) {
        tn = sizeof(oldtag) - 2;
        if (RegQueryValueExW(k, L"DownloadETag", NULL, NULL, (BYTE *)oldtag, &tn)) oldtag[0] = 0;
        RegCloseKey(k);
    }
    in = InternetOpenW(L"DynaRunFix-Setup/" WIDEN(DRF_VERSION), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!in) { *err = GetLastError(); return PK_NET; }
    InternetSetOptionW(in, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(in, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    for (tries = 0; tries < 5 && !*cancel; tries++) {
        f = CreateFileW(part, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) { *err = GetLastError(); rc = PK_IO; break; }
        GetFileSizeEx(f, &sz); have = sz.LowPart;
        hdr[0] = 0;
        if (have && oldtag[0]) wsprintfW(hdr, L"Range: bytes=%lu-\r\nIf-Range: %s\r\n", have, oldtag);   // resume only the same file
        h = InternetOpenUrlW(in, DYNAPRO_ZIP, hdr[0] ? hdr : NULL, (DWORD)-1,
                             INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_KEEP_CONNECTION, 0);
        if (!h) { *err = GetLastError(); CloseHandle(f); rc = PK_NET; Sleep(1000); continue; }
        n = sizeof(status);
        if (!HttpQueryInfoW(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &n, NULL)) status = 0;
        if (status != 200 && status != 206) { *err = status; rc = PK_HTTP; CloseHandle(f); InternetCloseHandle(h); h = NULL; break; }
        if (status == 200) { have = 0; SetFilePointer(f, 0, NULL, FILE_BEGIN); SetEndOfFile(f); }
        else SetFilePointer(f, 0, NULL, FILE_END);
        if (query_str(h, HTTP_QUERY_ETAG, tag, 200) && lstrcmpW(tag, oldtag) &&
            !RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\DynaRunFix", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) {
            RegSetValueExW(k, L"DownloadETag", 0, REG_SZ, (const BYTE *)tag, (lstrlenW(tag) + 1) * sizeof(WCHAR));
            RegCloseKey(k); lstrcpyW(oldtag, tag);
        }
        total = query_str(h, HTTP_QUERY_CONTENT_LENGTH, num, 32) ? (DWORD)StrToIntW_(num) : 0;
        if (total) total += have;
        rc = PK_NET;
        for (;;) {
            if (*cancel) { rc = PK_CANCEL; break; }
            if (!InternetReadFile(h, buf, sizeof(buf), &n)) { *err = GetLastError(); break; }
            if (!n) { rc = (!total || have == total) ? PK_OK : PK_NET; break; }
            if (!WriteFile(f, buf, n, &w, NULL) || w != n) { *err = GetLastError(); rc = PK_IO; break; }
            have += n;
            if (cb) cb(have, total);
        }
        CloseHandle(f);
        InternetCloseHandle(h); h = NULL;
        if (rc != PK_NET) break;
        Sleep(1000);
    }
    InternetCloseHandle(in);
    if (rc == PK_OK && !MoveFileExW(part, out, MOVEFILE_REPLACE_EXISTING)) { *err = GetLastError(); rc = PK_IO; }
    return rc;
}

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
    return h[11] == (BYTE)((e->flags & 8) ? e->dostime >> 8 : e->crc >> 24);
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

// Basic UI only (/qb): the wizard pages of this Wise setup, the only part that uses VBScript, are skipped.
int run_msiexec(const WCHAR *msi)
{
    static WCHAR exe[MAX_PATH], cmd[MAX_PATH * 2 + 64];
    STARTUPINFOW si; PROCESS_INFORMATION pi; DWORD rc = 1;
    GetSystemDirectoryW(exe, MAX_PATH); lstrcatW(exe, L"\\msiexec.exe");
    wsprintfW(cmd, L"\"%s\" /i \"%s\" /qb! REBOOT=ReallySuppress", exe, msi);
    zero(&si, sizeof(si)); si.cb = sizeof(si);
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return (int)GetLastError();
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)rc;
}

/* ---------- OneDrive placeholders the setup will overwrite ---------- */


static BOOL name_listed(const WCHAR *list, const WCHAR *name)
{
    for (; *list; list += lstrlenW(list) + 1) if (!lstrcmpiW(list, name)) return TRUE;
    return FALSE;
}

static int hydrate_tree(const WCHAR *dir, const WCHAR *names)
{
    static BYTE buf[CHUNK];
    WCHAR *p = alloc(2 * MAX_PATH * sizeof(WCHAR)); WIN32_FIND_DATAW fd; HANDLE f, h; DWORD n; int count = 0;
    if (!p) return 0;
    lstrcpyW(p, dir); lstrcatW(p, L"\\*");
    if ((f = FindFirstFileW(p, &fd)) != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == '.' || lstrlenW(dir) + lstrlenW(fd.cFileName) + 2 >= MAX_PATH) continue;
            lstrcpyW(p, dir); lstrcatW(p, L"\\"); lstrcatW(p, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { count += hydrate_tree(p, names); continue; }
            if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_OFFLINE))) continue;
            if (!name_listed(names, fd.cFileName)) continue;
            h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            if (h == INVALID_HANDLE_VALUE) continue;
            while (ReadFile(h, buf, sizeof(buf), &n, NULL) && n) ;
            CloseHandle(h);
            count++;
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    release(p);
    return count;
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
