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

    setval("DynaRunExe", REG_SZ, "", 1);
    read_dynarun_exe(HKEY_CURRENT_USER, g_sub, exe, sizeof(exe));
    CHECK(!lstrcmpA(exe, DEFAULT_EXE), "empty: default");

    // running(): no DynaRun V3.exe process of this session here, so 0 (and no crash in the session lookup)
    CHECK(running() == 0, "no DynaRun running");

    // token_state: this process's own token is classified (0 or 1), never -1
    { HANDLE t; if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) { int s = token_state(t); CHECK(s == 0 || s == 1, "own token classified"); CloseHandle(t); } }

    RegDeleteKeyA(HKEY_CURRENT_USER, g_sub);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
