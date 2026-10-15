// Tests of the hardening in src\update.c (the noted release is checked again, the log lock, the downloaded setup is hashed and
// started through one open handle). Host-native, with the CRT; update.c is compiled into this program. Nothing is installed,
// nothing is downloaded: GitHub's answer is replaced by canned text, the "setup" that is started is a copy of hostname.exe in a
// temporary folder, %TEMP% for the log is a temporary folder too. See tests\build-hardening-tests.cmd.
#include "../src/update.c"
#include <stdio.h>

BOOL g_zh, g_quiet; HWND g_hwnd; WCHAR g_launcher[MAX_PATH];
void zero(void *p, SIZE_T n) { memset(p, 0, n); }
void *alloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
void release(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
int msg(const WCHAR *t, UINT f) { (void)t; (void)f; return IDOK; }
int running_programs(void) { return 0; }
int pkg_fetch(const WCHAR *u, const WCHAR *n, const WCHAR *e, const WCHAR *h, WCHAR *o, progress_fn cb, volatile LONG *c, DWORD *err) { (void)u; (void)n; (void)e; (void)h; (void)o; (void)cb; (void)c; *err = 0; return PK_NET; }
HINTERNET pkg_open_following(HINTERNET in, const WCHAR *url, const WCHAR *host, const WCHAR *hdr, DWORD *status, DWORD *err, BOOL *refused) { (void)in; (void)url; (void)host; (void)hdr; *status = 0; *err = ERROR_INTERNET_CANNOT_CONNECT; *refused = FALSE; return NULL; }

static int g_fail, g_pass;
#define CHECK(c, what) do { if (c) { g_pass++; } else { g_fail++; wprintf(L"FAIL line %d: %s\n", __LINE__, what); } } while (0)

static const char *g_json;
static char *fake_get(const WCHAR *url, DWORD *status)
{
    char *b = alloc(lstrlenA(g_json) + 1); (void)url;
    *status = 200; lstrcpyA(b, g_json);
    return b;
}

#define DIGEST "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define REL(tag, url, dig) "{\"tag_name\":\"" tag "\",\"assets\":[{\"name\":\"other.zip\",\"digest\":\"sha256:" DIGEST "\",\"browser_download_url\":\"https://x\"}," \
    "{\"name\":\"DynaRunFix-Setup.exe\",\"digest\":\"" dig "\",\"browser_download_url\":\"" url "\"}]}"

static void test_release(void)
{
    char hex[65]; const char *good = "https://github.com/timliudev/DynaRunFix/releases/download/v9.9.9/DynaRunFix-Setup.exe";
    static const char *j1 = REL("v9.9.9", "https://github.com/timliudev/DynaRunFix/releases/download/v9.9.9/DynaRunFix-Setup.exe", "sha256:" DIGEST);
    static const char *j2 = REL("v9.9.9", "https://github.com/evil/DynaRunFix/releases/download/v9.9.9/DynaRunFix-Setup.exe", "sha256:" DIGEST);
    static const char *j3 = REL("v9.9.9", "https://github.com/timliudev/DynaRunFix/releases/download/v9.9.8/DynaRunFix-Setup.exe", "sha256:" DIGEST);
    static const char *j4 = REL("v9.9.9", "https://github.com/timliudev/DynaRunFix/releases/download/v9.9.9/DynaRunFix-Setup.exe", "sha256:zz23456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    static const char *j5 = REL("v9.9.9", "https://github.com/timliudev/DynaRunFix/releases/download/v9.9.9/DynaRunFix-Setup.exe", "md5:" DIGEST);
    static const char *j6 = "{\"tag_name\":\"v9.9.9\",\"assets\":[]}";
    (void)good;
    CHECK(release_asset(j1, "v9.9.9", hex) && !lstrcmpA(hex, DIGEST), L"a good release is accepted");
    CHECK(!release_asset(j2, "v9.9.9", hex), L"download URL of another repository refused");
    CHECK(!release_asset(j3, "v9.9.9", hex), L"download URL of another tag refused");
    CHECK(!release_asset(j4, "v9.9.9", hex), L"digest that is not hex refused");
    CHECK(!release_asset(j5, "v9.9.9", hex), L"digest that is not sha256 refused");
    CHECK(!release_asset(j6, "v9.9.9", hex), L"release without the asset refused");

    // verify_note against canned GitHub answers (this build is "1.0.0")
    g_http_get = fake_get;
    g_json = j1;
    CHECK(verify_note(L"v9.9.9", L"" DIGEST), L"the note that GitHub confirms is accepted");
    CHECK(verify_note(L"v9.9.9", L"0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"), L"digest compare ignores case");
    CHECK(!verify_note(L"v9.9.8", L"" DIGEST), L"a note with another tag than GitHub's latest is refused");
    CHECK(!verify_note(L"v9.9.9", L"fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"), L"a note with another digest is refused");
    CHECK(!verify_note(L"v1.0.0", L"" DIGEST), L"the note must be newer than this version");
    CHECK(!verify_note(L"v0.9.0", L"" DIGEST), L"an older release (downgrade) is refused");
    CHECK(!verify_note(L"v9.9.9\" /x", L"" DIGEST), L"a tag with odd characters is refused");
    CHECK(!verify_note(L"v9.9.9", L"abc"), L"a short digest is refused");
    CHECK(!verify_note(L"..\\..\\x", L"" DIGEST), L"a path as the tag is refused");
    g_json = j2;
    CHECK(!verify_note(L"v9.9.9", L"" DIGEST), L"GitHub's answer with a foreign URL is refused");
    g_json = "";
    CHECK(!verify_note(L"v9.9.9", L"" DIGEST), L"no usable answer: refused");
}

static void mkfile(const WCHAR *p, const char *c)
{ HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); DWORD w; WriteFile(h, c, (DWORD)strlen(c), &w, NULL); CloseHandle(h); }

static void test_hold(const WCHAR *dir)
{
    WCHAR sysexe[MAX_PATH], f[MAX_PATH], moved[MAX_PATH], hash[65], cmd[MAX_PATH + 20]; HANDLE hold, w; int rc; DWORD e;
    // sha256 of "abc"
    wsprintfW(f, L"%s\\abc.txt", dir); mkfile(f, "abc");
    w = CreateFileW(f, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(sha256_handle(w, hash) && !lstrcmpiW(hash, L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), L"SHA-256 of abc");
    SetFilePointer(w, 1, NULL, FILE_BEGIN);
    CHECK(sha256_handle(w, hash) && !lstrcmpiW(hash, L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), L"hash always starts at the beginning");
    CloseHandle(w);

    // the file that was hashed is the file that is started: while the handle is open nobody can change, rename or delete it
    GetSystemDirectoryW(sysexe, MAX_PATH); lstrcatW(sysexe, L"\\hostname.exe");
    wsprintfW(f, L"%s\\setup-copy.exe", dir);
    CHECK(CopyFileW(sysexe, f, FALSE), L"copy of a harmless exe");
    hold = CreateFileW(f, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(hold != INVALID_HANDLE_VALUE, L"hold opened");
    w = CreateFileW(f, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL); e = GetLastError();
    CHECK(w == INVALID_HANDLE_VALUE && e == ERROR_SHARING_VIOLATION, L"writing to the held file fails");
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
    w = CreateFileW(f, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(w == INVALID_HANDLE_VALUE, L"opening the held file for delete fails");
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
    CHECK(!DeleteFileW(f), L"deleting the held file fails");
    wsprintfW(moved, L"%s\\moved.exe", dir);
    CHECK(!MoveFileW(f, moved), L"renaming the held file fails");
    CHECK(!CopyFileW(sysexe, f, FALSE), L"replacing the held file fails");
    // ... and it still starts, through the same function the updater uses
    wsprintfW(cmd, L"\"%s\"", f);
    rc = run_wait(f, cmd);
    CHECK(rc == 0, L"the held exe starts and runs (run_wait returns its exit code 0)");
    CloseHandle(hold);
    CHECK(DeleteFileW(f), L"after the handle is closed it can be deleted");
}

static DWORD WINAPI hold_mutex(LPVOID a)
{ HANDLE m = log_lock(); *(volatile LONG *)a = 1; Sleep(3500); if (m) ReleaseMutex(m); return 0; }

static void test_log(const WCHAR *dir)
{
    WCHAR f[MAX_PATH]; HANDLE h; char buf[2000]; DWORD n = 0, t0; volatile LONG flag = 0; HANDLE th;
    SetEnvironmentVariableW(L"TEMP", dir);
    wsprintfW(f, L"%s\\dynafix.log", dir);
    uplog("first %s %d", "line", 1);
    uplog("second %s %d", "line", 2);
    h = CreateFileW(f, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) { ReadFile(h, buf, sizeof(buf) - 1, &n, NULL); CloseHandle(h); }
    buf[n] = 0;
    CHECK(strstr(buf, "first line 1\r\n") && strstr(buf, "second line 2\r\n") && strstr(buf, "update (v"), L"log lines written");
    // another process (here: thread) holds the log mutex: uplog waits at most about 2 s and then writes anyway
    th = CreateThread(NULL, 0, hold_mutex, (LPVOID)&flag, 0, NULL);
    while (!flag) Sleep(10);
    t0 = GetTickCount();
    uplog("third %s %d", "line", 3);
    n = GetTickCount() - t0;
    CHECK(n >= 1500 && n < 3300, L"uplog waited for the lock, but not for ever");
    h = CreateFileW(f, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) { ReadFile(h, buf, sizeof(buf) - 1, &n, NULL); CloseHandle(h); } else n = 0;
    buf[n] = 0;
    CHECK(strstr(buf, "third line 3\r\n") != NULL, L"... and the line is there");
    WaitForSingleObject(th, 5000); CloseHandle(th);
}

int wmain(void)
{
    WCHAR dir[MAX_PATH], c[600];
    setvbuf(stdout, NULL, _IONBF, 0);
    GetTempPathW(MAX_PATH, dir); wsprintfW(dir + lstrlenW(dir), L"drf-upd-%lu", GetCurrentProcessId()); CreateDirectoryW(dir, NULL);
    wprintf(L"[release]\n"); test_release();
    wprintf(L"[hold]\n"); test_hold(dir);
    wprintf(L"[log]\n"); test_log(dir);
    wsprintfW(c, L"cmd.exe /c rmdir /s /q \"%s\"", dir);
    { STARTUPINFOW si; PROCESS_INFORMATION pi; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
      if (CreateProcessW(NULL, c, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { WaitForSingleObject(pi.hProcess, 20000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); } }
    wprintf(L"\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
