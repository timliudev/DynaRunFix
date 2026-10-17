// Test harness for the dynafix.log code (logf / llog / trim_log). Not part of the release build; see tests\README.md.
// It #includes the real source, so what is tested is the shipped code:
//   logtest.exe       -> src\dynafix.c  (logf)
//   logtest_l.exe     -> src\launcher.c (llog; built with -DTEST_LAUNCHER)
// SRC (default ..\src\dynafix.c / ..\src\launcher.c) can point at another copy, e.g. an older revision (with its dynafix_*.inc.c and applog.h beside it, as in src).
// The log is %TEMP%\dynafix.log, so run it with TEMP pointing at a scratch directory.
#include <windows.h>
#include <stdio.h>

static FARPROC real_gpa(HMODULE m, LPCSTR n) { return GetProcAddress(m, n); }
static BOOL WINAPI fake_shexec(SHELLEXECUTEINFOA *se) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }   // nothing is launched
static HINSTANCE WINAPI fake_shexec_a(HWND w, LPCSTR v, LPCSTR f, LPCSTR p, LPCSTR d, INT s) { return (HINSTANCE)(UINT_PTR)33; }   // the "pass through" target
static FARPROC test_gpa(HMODULE m, LPCSTR n)
{
    if (n && !lstrcmpA(n, "ShellExecuteExA")) return (FARPROC)fake_shexec;
    return real_gpa(m, n);
}
#define GetProcAddress test_gpa

#ifdef TEST_LAUNCHER
#ifndef SRC
#define SRC "../src/launcher.c"
#endif
#else
#ifndef SRC
#define SRC "../src/dynafix.c"
#endif
#endif
#include SRC
#undef GetProcAddress

#ifdef TEST_LAUNCHER
#define LOG2(fmt, a, b) llog(fmt, a, b)
#else
#define LOG2(fmt, a, b) logf(fmt, a, b, 0)
#endif

static char *rep(char c, int n)
{
    char *s = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1); int i;
    for (i = 0; i < n; i++) s[i] = c;
    s[n] = 0;
    return s;
}

// long strings through the log, then a marker line that must stay intact
static int test_long(void)
{
    static const int len[] = { 10, 200, 500, 970, 1000, 1100, 2000, 5000 };
    int i;
    for (i = 0; i < (int)(sizeof(len) / sizeof(len[0])); i++) {
        char *s = rep('A' + i, len[i]);
        LOG2("LONG%u %s tail\r\n", len[i], (DWORD)(UINT_PTR)s);
        LOG2("NEXT%u fine\r\n", len[i], 0);
        HeapFree(GetProcessHeap(), 0, s);
    }
#ifndef TEST_LAUNCHER
    {
        char *p = rep('P', 4000), *f = (char *)HeapAlloc(GetProcessHeap(), 0, 5000);
        // the ShellExecute hook with a very long Setup_ path (rejected by the matcher, passed to the fake ShellExecuteA)
        lstrcpyA(f, "\\\\?\\C:\\"); lstrcatA(f, p); lstrcatA(f, "\\Setup_x.exe");
        r_shexec = (FARPROC)fake_shexec_a;   // an over-long path is not a first-run helper (see setup_match.h): it is passed through, nothing is launched
        H_ShellExecuteA(NULL, "open", f, "", "", 1);
        logf("NEXT-SHEXEC fine %u %u %u\r\n", 0, 0, 0);
        // the cloud-attribute path (module path in the log line)
        InterlockedExchange(&g_masked, 0);
        strip(0x00080000 | FILE_ATTRIBUTE_NORMAL | 0x00040000, "GetFileAttributesA", (void *)test_long);
        logf("NEXT-STRIP fine %u %u %u\r\n", 0, 0, 0);
#ifndef OLD_SRC   // w2u is new
        // window text of 29 CJK chars (87 UTF-8 bytes): too long for a 60-byte buffer, must be cut, not left unterminated
        { WCHAR w[40]; char t[60]; int k; for (k = 0; k < 29; k++) w[k] = 0x4E00 + k; w[29] = 0; for (k = 0; k < 60; k++) t[k] = 'Z'; w2u(t, sizeof(t), w);
          logf("W2U len %u first %02X last-nonzero %u\r\n", lstrlenA(t), (BYTE)t[0], t[lstrlenA(t)] == 0); }
#endif
    }
#endif
    LOG2("LONG-DONE %u %u\r\n", 1, 2);
    return 0;
}

#if !defined(TEST_LAUNCHER) && !defined(OLD_SRC)   // load_sys is new
// load_sys (a system DLL by name, from the system folder only): finds one that exists, refuses one that does not. Exit code 0 = ok.
static int test_load_sys(void)
{
    HMODULE a = load_sys("version.dll"), b = load_sys("drf-no-such-system-dll.dll");
    int bad = 0;
    if (!a) { printf("FAIL load_sys(version.dll) returned NULL\n"); bad++; }
    if (b) { printf("FAIL load_sys(nonexistent) returned a module\n"); bad++; }
    if (!bad) printf("load_sys: ok\n");
    return bad;
}
#endif

static int num(const char *s) { int n = 0; while (*s >= '0' && *s <= '9') n = n * 10 + *s++ - '0'; return n; }
typedef struct { DWORD id, count; int trim; } wr_t;
static DWORD WINAPI writer(LPVOID p)
{
    wr_t *w = (wr_t *)p; DWORD i;
    for (i = 0; i < w->count; i++) {
        LOG2("W%u L%08u xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx END\r\n", w->id, i);
        if (w->trim && i % 100 == 99) trim_log();
    }
    return 0;
}

// mp <id> <count> <nthreads>: each thread writes <count> numbered lines as writer id*10+t; thread 0 also trims the log
int main(int argc, char **argv)
{
#ifndef TEST_LAUNCHER
    char tmp[MAX_PATH];
    if (!GetEnvironmentVariableA("TEMP", tmp, MAX_PATH - 16)) return 2;
    lstrcpyA(g_logpath, tmp); lstrcatA(g_logpath, "\\dynafix.log");
#endif
#if !defined(TEST_LAUNCHER) && !defined(OLD_SRC)
    if (argc >= 2 && !lstrcmpA(argv[1], "loadsys")) return test_load_sys();
#endif
    if (argc >= 2 && !lstrcmpA(argv[1], "long")) return test_long();
    if (argc >= 5 && !lstrcmpA(argv[1], "mp")) {
        DWORD id = num(argv[2]), count = num(argv[3]); int nt = num(argv[4]), t; HANDLE h[16]; wr_t w[16];
        trim_log();
        for (t = 0; t < nt; t++) { w[t].id = id * 10 + t; w[t].count = count; w[t].trim = t == 0; h[t] = CreateThread(NULL, 0, writer, &w[t], 0, NULL); }
        WaitForMultipleObjects(nt, h, TRUE, INFINITE);
        return 0;
    }
    return 2;
}
