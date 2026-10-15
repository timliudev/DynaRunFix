// Tests of pieces of src\launcher.c: the DynaRunExe value read (a long path, a wrong type, a value without terminator), which DynaRun
// counts as running, and the token elevation query. The real source is #included. Host-native, with the CRT; nothing is launched.
// Writes only HKCU\Software\DynaRunFixTest-<pid> and deletes it again.
#define DRF_NO_MAIN
#include "../src/launcher.c"
#include <stdio.h>

static int g_pass, g_fail;
#define CHECK(c, what) do { if (c) g_pass++; else { g_fail++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static char g_sub[64];
static void setval(const char *name, DWORD type, const void *data, DWORD n)
{
    HKEY k;
    RegCreateKeyExA(HKEY_CURRENT_USER, g_sub, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL);
    RegSetValueExA(k, name, 0, type, (const BYTE *)data, n);
    RegCloseKey(k);
}

int main(void)
{
    char exe[MAX_PATH], longp[400]; int i;
    wsprintfA(g_sub, "Software\\DynaRunFixTest-%u", GetCurrentProcessId());

    read_dynarun_exe(HKEY_CURRENT_USER, g_sub, exe, sizeof(exe));
    CHECK(!lstrcmpA(exe, DEFAULT_EXE), "no key: default");

    // a recorded path longer than the launcher's own path (the old code asked with the launcher path's length and got ERROR_MORE_DATA)
    { const char *p = "D:\\A fairly long folder name for Dyna Pro\\Another long folder name\\Dyna Pro Dynamometers\\DynaRun V3.exe";
      setval("DynaRunExe", REG_SZ, p, lstrlenA(p) + 1);
      read_dynarun_exe(HKEY_CURRENT_USER, g_sub, exe, sizeof(exe));
      CHECK(!lstrcmpA(exe, p), "a long recorded path is read in full"); }

    // a path of 259 characters (the longest that fits MAX_PATH)
    for (i = 0; i < 259; i++) longp[i] = 'a';
    longp[259] = 0; longp[0] = 'D'; longp[1] = ':'; longp[2] = '\\';
    setval("DynaRunExe", REG_SZ, longp, 260);
    read_dynarun_exe(HKEY_CURRENT_USER, g_sub, exe, sizeof(exe));
    CHECK(!lstrcmpA(exe, longp), "259 characters fit");

    // one that does not fit: the default, not a cut-off path
    for (i = 0; i < 300; i++) longp[i] = 'a';
    longp[300] = 0;
    setval("DynaRunExe", REG_SZ, longp, 301);
    read_dynarun_exe(HKEY_CURRENT_USER, g_sub, exe, sizeof(exe));
    CHECK(!lstrcmpA(exe, DEFAULT_EXE), "a value that does not fit: default");

    { DWORD d = 5; setval("DynaRunExe", REG_DWORD, &d, 4);
      read_dynarun_exe(HKEY_CURRENT_USER, g_sub, exe, sizeof(exe));
      CHECK(!lstrcmpA(exe, DEFAULT_EXE), "not a string: default"); }

    { char b[8] = { 'D', ':', '\\', 'x', '.', 'e', 'x', 'e' };            // REG_SZ stored without the terminator
      setval("DynaRunExe", REG_SZ, b, sizeof(b));
      read_dynarun_exe(HKEY_CURRENT_USER, g_sub, exe, sizeof(exe));
      CHECK(!lstrcmpA(exe, "D:\\x.exe"), "stored without NUL: terminated"); }

    { const char *p = "%ProgramFiles%\\Dyna Pro\\DynaRun V3.exe";           // REG_EXPAND_SZ is not accepted (the setup writes REG_SZ): default, not an unexpanded path
      setval("DynaRunExe", REG_EXPAND_SZ, p, lstrlenA(p) + 1);
      read_dynarun_exe(HKEY_CURRENT_USER, g_sub, exe, sizeof(exe));
      CHECK(!lstrcmpA(exe, DEFAULT_EXE), "REG_EXPAND_SZ: default"); }

    // a value that fills the buffer (size 8 here): it fits only with its terminator inside
    { char b[8] = { 'D', ':', '\\', 'x', 'y', '.', 'e', 'x' };
      setval("DynaRunExe", REG_SZ, b, sizeof(b));
      read_dynarun_exe(HKEY_CURRENT_USER, g_sub, exe, 8);
      CHECK(!lstrcmpA(exe, DEFAULT_EXE), "fills the buffer without NUL: default, not a cut-off path");
      b[7] = 0;
      setval("DynaRunExe", REG_SZ, b, sizeof(b));
      read_dynarun_exe(HKEY_CURRENT_USER, g_sub, exe, 8);
      CHECK(!lstrcmpA(exe, "D:\\xy.e"), "fills the buffer with its NUL: read"); }

    setval("DynaRunExe", REG_SZ, "", 1);
    read_dynarun_exe(HKEY_CURRENT_USER, g_sub, exe, sizeof(exe));
    CHECK(!lstrcmpA(exe, DEFAULT_EXE), "empty: default");

    // running(): no DynaRun V3.exe process of this session here, so 0 (and no crash in the session lookup)
    CHECK(running() == 0, "no DynaRun running");

    // token_state: this process's own token is classified (0 or 1), never -1
    { HANDLE t; if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) { int s = token_state(t); CHECK(s == 0 || s == 1, "own token classified"); CloseHandle(t); } }

    // an unknown state (-1: the query failed, here for an invalid handle) is never taken for "not elevated": token_elevated fails closed,
    // and relaunch_non_elevated (which stops on a negative state before it starts anything) is not reached with a usable answer
#pragma warning(suppress: 4996)
    if ((BYTE)GetVersion() >= 6) {
        CHECK(token_state(NULL) == -1, "invalid token handle: state unknown (-1)");
        CHECK(token_elevated(NULL), "unknown state counts as elevated (fail closed)");
    }

    // which processes count as ours: the same session; a session that cannot be told does not count (unless ours cannot be read either)
    CHECK(in_session(TRUE, 1, TRUE, 1), "same session counts");
    CHECK(!in_session(TRUE, 1, TRUE, 2), "another session (fast user switching) does not count");
    CHECK(!in_session(TRUE, 1, TRUE, 0), "session 0 (services) is not session 1");
    CHECK(!in_session(TRUE, 1, FALSE, 1), "process whose session cannot be looked up does not count");
    CHECK(in_session(FALSE, 0, TRUE, 5), "own session unknown: every DynaRun counts");

    RegDeleteKeyA(HKEY_CURRENT_USER, g_sub);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
